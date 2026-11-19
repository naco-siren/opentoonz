// The desktop's TKeyValueStore against the QSettings code tenv had before
// step 6b of doc/nexttoonz_core_noqt.md, and tenv's SystemVar.ini going
// through whatever store is injected. Built in the Qt build only.
//
//  - Reading: the same ini files (written by QSettings and by hand: commas,
//    which QSettings reads as lists, quotes, backslashes, escapes, non-ASCII,
//    sections, @String) give the same value for every key, through the old
//    code (QSettings::value(key).toString() into a TFilePath) and the store.
//  - Writing: setting the same values in copies of the same files, the old
//    way and through the store, leaves the same bytes.
//  - tenv: with a memory store injected, TEnv reads the system variables
//    from it, and on Linux writes the stuff folder's root into it.

#include <gtest/gtest.h>

#include "tconvert.h"
#include "tenv.h"
#include "tfilepath.h"
#include "tkeyvaluestore.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QVariant>

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

QByteArray readAll(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return QByteArray("<missing>");
  return f.readAll();
}

void writeAll(const QString &path, const QByteArray &bytes) {
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(bytes);
}

// EnvGlobals::getSystemVarPath before step 6b
std::wstring oldRead(const QString &file, const std::string &varName) {
  QSettings settings(file, QSettings::IniFormat);
  QString qStr      = QString::fromStdString(varName);
  QString systemVar = settings.value(qStr).toString();
  return TFilePath(systemVar.toStdWString()).getWideString();
}

// the same through the store
std::wstring newRead(const QString &file, const std::string &varName) {
  std::unique_ptr<TKeyValueStore> settings =
      TKeyValueStore::open(TFilePath(file.toStdWString()));
  return TFilePath(settings->stringValue(varName)).getWideString();
}

// writeRootVar before step 6b
void oldWrite(const QString &file, const std::string &key,
              const TFilePath &value) {
  QSettings settings(file, QSettings::IniFormat);
  settings.setValue(QString::fromStdString(key), value.getQString());
  settings.sync();
}

void newWrite(const QString &file, const std::string &key,
              const TFilePath &value) {
  std::unique_ptr<TKeyValueStore> settings =
      TKeyValueStore::open(TFilePath(file.toStdWString()));
  settings->setStringValue(key, value.getWideString());
  settings->sync();
}

const std::vector<std::string> kKeys = {
    "TOONZROOT", "TOONZPROFILES", "TOONZCONFIG", "comma", "commas",
    "quoted", "quotedcomma", "backslash", "escapes", "unicode", "percent",
    "spaces", "empty", "atstring", "semicolon", "hash", "equals",
    "Group/inner", "group/inner", "missing", "Section/key", "a/b/c",
    "list", "list/size", "list/1/name", "General/x", "%General/x"};

// Ini files as people and programs leave them.
const char *const kHandWritten[] = {
    "[General]\n"
    "TOONZROOT=/Users/me/Library/OpenToonz/stuff\n"
    "TOONZPROFILES=/a,b/profiles\n"
    "comma=/path/with,comma\n"
    "commas=a, b, c\n"
    "quoted=\"/path/with,comma\"\n"
    "quotedcomma=\"a\", \"b\"\n"
    "backslash=C:\\\\Users\\\\me\\\\stuff\n"
    "escapes=tab\\there\\nnewline\\x41\\x00e9\n"
    "unicode=/Users/\xC3\xA9t\xC3\xA9/caf\xC3\xA9/\xE6\x97\xA5\xE6\x9C\xAC\n"
    "percent=100%25 sure\n"
    "spaces=   padded   \n"
    "empty=\n"
    "atstring=@String(/x,y)\n"
    "semicolon=a;b ; c\n"
    "hash=a#b\n"
    "equals=a=b\n"
    "\n[Group]\ninner=g1\n"
    "\n[group]\ninner=g2\n"
    "\n[Section]\nkey=\"/s p a c e\"\n"
    "\n[a]\nb\\c=deep\n"
    "\n[list]\n1\\name=first\nsize=1\n",
    // no [General], CRLF, a BOM
    "\xEF\xBB\xBFTOONZROOT=/crlf/root\r\nTOONZCONFIG=x,y\r\n",
    // a value spread over a quoted string with escapes
    "[General]\nTOONZROOT=\"/a\\\"b\\\"/c\"\nunicode=\\x65e5\\x672c\n",
    "",
};

const std::vector<std::wstring> kValues = {
    L"/Users/me/Library/OpenToonz/stuff",
    L"/path/with,comma",
    L"/path/with \"quotes\"",
    L"/caf\u00e9/\u00c9t\u00e9/\u65e5\u672c/\U0001F600",
    L"/back\\slash",
    L"  padded  ",
    L"a;b#c=d%e",
    L"",
};

class TKeyValueStoreQt : public ::testing::Test {
protected:
  QTemporaryDir dir;
  QString path(const QString &name) { return dir.path() + "/" + name; }
};

}  // namespace

TEST_F(TKeyValueStoreQt, ReadsAsTheOldCodeDid) {
  ASSERT_TRUE(dir.isValid());
  int n = 0, nonEmpty = 0;
  std::vector<QString> files;
  for (const char *text : kHandWritten) {
    QString f = path(QString("hand%1.ini").arg(n++));
    writeAll(f, QByteArray(text));
    files.push_back(f);
  }
  // and files QSettings wrote itself
  for (const std::wstring &v : kValues) {
    QString f = path(QString("qt%1.ini").arg(n++));
    {
      QSettings s(f, QSettings::IniFormat);
      for (const std::string &k : kKeys)
        s.setValue(QString::fromStdString(k), QString::fromStdWString(v));
    }
    files.push_back(f);
  }
  for (const QString &f : files)
    for (const std::string &k : kKeys) {
      std::wstring a = oldRead(f, k), b = newRead(f, k);
      EXPECT_EQ(a, b) << f.toStdString() << " " << k;
      if (!a.empty()) ++nonEmpty;
    }
  EXPECT_GT(nonEmpty, 100);
  // the files were only read
  for (int i = 0; i < 4; ++i)
    EXPECT_EQ(readAll(files[i]), QByteArray(kHandWritten[i]));
}

TEST_F(TKeyValueStoreQt, WritesTheSameBytes) {
  ASSERT_TRUE(dir.isValid());
  int n = 0;
  std::vector<QByteArray> starts;
  for (const char *text : kHandWritten) starts.push_back(QByteArray(text));
  starts.push_back(QByteArray());  // no file at all
  for (const QByteArray &start : starts)
    for (const std::wstring &v : kValues)
      for (const std::string key : {"TOONZROOT", "OPENTOONZROOT", "Group/inner",
                                    "a/b/c"}) {
        QString a = path(QString("a%1.ini").arg(n));
        QString b = path(QString("b%1.ini").arg(n));
        ++n;
        if (!start.isNull()) {
          writeAll(a, start);
          writeAll(b, start);
        }
        oldWrite(a, key, TFilePath(v));
        newWrite(b, key, TFilePath(v));
        EXPECT_EQ(readAll(a), readAll(b)) << n << " " << key;
      }
  EXPECT_GT(n, 150);
}

namespace {

std::map<std::string, std::wstring> memory;
std::vector<std::wstring> opened;
std::vector<std::string> asked;

class MemoryStore final : public TKeyValueStore {
public:
  bool contains(const std::string &key) const override {
    return memory.count(key) != 0;
  }
  std::wstring stringValue(const std::string &key,
                           const std::wstring &defaultValue) const override {
    asked.push_back(key);
    auto it = memory.find(key);
    return it == memory.end() ? defaultValue : it->second;
  }
  void setStringValue(const std::string &key,
                      const std::wstring &value) override {
    memory[key] = value;
  }
  void sync() override {}
};

std::unique_ptr<TKeyValueStore> openMemory(const TFilePath &path) {
  opened.push_back(path.getWideString());
  return std::unique_ptr<TKeyValueStore>(new MemoryStore());
}

}  // namespace

#ifndef _WIN32
TEST(TEnvKeyValueStore, SystemVariablesGoThroughTheInjectedStore) {
  memory.clear();
  opened.clear();
  asked.clear();
  TKeyValueStore::Factory previous = TKeyValueStore::setFactory(&openMemory);
  const std::string prefix = TEnv::getSystemVarPrefix();
  memory[prefix + "CONFIG"] = L"/injected/config";

  EXPECT_EQ(TEnv::getSystemVarStringValue(prefix + "CONFIG"),
            "/injected/config");
  EXPECT_EQ(TEnv::getSystemVarPathValue(prefix + "CONFIG"),
            TFilePath("/injected/config"));
  ASSERT_FALSE(opened.empty());
  EXPECT_NE(opened.back().find(L"SystemVar.ini"), std::wstring::npos);
  EXPECT_EQ(asked.back(), prefix + "CONFIG");

#ifdef LINUX
  // the stuff folder's root is written through the store too: with a stuff
  // folder already in place and no root in the ini, initUserStuffDir writes
  // the root (EnvGlobals::getSystemVarFile follows $HOME)
  QTemporaryDir home;
  ASSERT_TRUE(home.isValid());
  QByteArray oldHome = qgetenv("HOME");
  qputenv("HOME", home.path().toLocal8Bit());
  QString stuff = home.path() + "/.config/" +
                  QString::fromStdString(TEnv::getApplicationName()) +
                  "/stuff";
  ASSERT_TRUE(QDir().mkpath(stuff));
  memory.clear();
  opened.clear();
  TEnv::initUserStuffDir();
  qputenv("HOME", oldHome);
  EXPECT_EQ(memory[TEnv::getRootVarName()], stuff.toStdWString());
  EXPECT_TRUE(QFile::exists(home.path() + "/.config/" +
                            QString::fromStdString(TEnv::getApplicationName()) +
                            "/SystemVar.ini") == false);
#endif

  TKeyValueStore::setFactory(previous);
}
#endif
