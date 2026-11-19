// TLevelWriter::renumberFids() without Qt (step 8d-0 of
// doc/nexttoonz_core_noqt.md) against the Qt version the build with Qt still
// uses: the same frame files, renumbered the same way, leave the same
// folders behind. Qt build only.

#include <gtest/gtest.h>

#include "tfilepath.h"
#include "tsystem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <map>
#include <random>

// tlevel_io.cpp and tlevel_io_qt.cpp
void renumberFidsWithQt(const TFilePath &path,
                        const std::map<TFrameId, TFrameId> &table);
void renumberFidsWithoutQt(const TFilePath &path,
                           const std::map<TFrameId, TFrameId> &table);

namespace {

void write(const QString &path, const QByteArray &content) {
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly)) << path.toStdString();
  f.write(content);
}

// Every entry of the folder, with what a file holds.
QStringList listing(const QString &dir) {
  QStringList out;
  QDir d(dir);
  for (const QFileInfo &fi :
       d.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System |
                           QDir::NoDotAndDotDot,
                       QDir::Name)) {
    QString line = fi.fileName();
    if (fi.isSymLink()) line += " -> " + fi.symLinkTarget().section('/', -1);
    if (fi.isDir())
      line += "/";
    else if (fi.exists()) {
      QFile f(fi.filePath());
      f.open(QIODevice::ReadOnly);
      line += " = " + QString::fromUtf8(f.readAll());
    }
    out << line;
  }
  return out;
}

// A level a..png with frames 1 to n, written in a folder with its
// neighbours: the other way of writing a frame number, other levels, other
// cases, hidden files, a folder and links that look like frames.
void makeFolder(const QString &dir, int n, std::mt19937 &rnd) {
  QDir().mkpath(dir);
  for (int i = 1; i <= n; ++i) {
    QString num = QString("%1").arg(i, 4, 10, QChar('0'));
    QString sep = (rnd() % 4 == 0) ? "_" : ".";
    write(dir + "/a" + sep + num + ".png", ("frame " + num).toUtf8());
  }
  write(dir + "/a.0001a.png", "with a letter");
  write(dir + "/A.0002.PNG", "another case");
  write(dir + "/.a.0003.png", "hidden");
  write(dir + "/b.0001.png", "another level");
  write(dir + "/a.0004.png_", "a leftover temporary");
  write(dir + "/a.png", "no frame");
  QDir().mkpath(dir + "/a.0005.png");
  QFile::link(dir + "/a.0001.png", dir + "/a.0099.png");
  QFile::link(dir + "/gone", dir + "/a.0098.png");
}

}  // namespace

TEST(RenumberFids, WithoutQtLeavesWhatQtLeaves) {
  std::mt19937 rnd(9);
  for (int round = 0; round < 60; ++round) {
    SCOPED_TRACE(round);
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const int n = 2 + int(rnd() % 8);
    std::mt19937 layout(round);
    makeFolder(tmp.path() + "/qt", n, layout);
    layout.seed(round);
    makeFolder(tmp.path() + "/std", n, layout);

    // a table: frames kept, moved (onto each other, onto frames that
    // exist, onto new ones) and left out (removed)
    std::map<TFrameId, TFrameId> table;
    for (int i = 1; i <= n + 2; ++i) {
      int r = int(rnd() % 5);
      if (r == 0) continue;
      table[TFrameId(i)] = TFrameId(r == 1 ? i : 1 + int(rnd() % (n + 3)));
    }
    if (rnd() % 3 == 0) table[TFrameId(1, L"a")] = TFrameId(7);

    const std::wstring level = L"/a..png";
    renumberFidsWithQt(TFilePath((tmp.path() + "/qt").toStdWString() + level),
                       table);
    renumberFidsWithoutQt(
        TFilePath((tmp.path() + "/std").toStdWString() + level), table);
    EXPECT_EQ(listing(tmp.path() + "/std"), listing(tmp.path() + "/qt"));
  }
}

TEST(RenumberFids, AFolderThatIsNotThereIsLeftAlone) {
  std::map<TFrameId, TFrameId> table{{TFrameId(1), TFrameId(2)}};
  renumberFidsWithoutQt(TFilePath(L"/no/such/folder/a..png"), table);
  renumberFidsWithQt(TFilePath(L"/no/such/folder/a..png"), table);
}
