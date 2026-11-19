// Differential test of TFileStatus and the file-system functions of TSystem
// against the code they had before migration step 6a of
// doc/nexttoonz_core_noqt.md, when they went through QFileInfo, QDir and
// QFile. legacy/legacy_tsystem.* is that code verbatim, renamed
// LegacyTFileStatus and LegacyTSystem. Both read one tree holding every kind
// of entry (hidden by name and by flag, links to files and folders, broken
// links, a fifo, unreadable entries, decomposed and composed names, many
// names to sort...), and both run the same operations on two copies of a
// tree; what they say and what they leave on disk must be the same. Built in
// the Qt build only, off Windows (where the old code is still the code).

#include <gtest/gtest.h>

#ifndef _WIN32

#include "legacy/legacy_tsystem.h"
#include "tconvert.h"
#include "tfilepath.h"
#include "tfiletype.h"
#include "tsystem.h"
#include "../../common/tsystem/tsystem_qtsemantics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {

void registerTypes() {
  static bool done = false;
  if (done) return;
  done = true;
  using namespace TFileType;
  const std::pair<const char *, Type> types[] = {
      {"png", RASTER_IMAGE},  {"tif", RASTER_IMAGE}, {"tlv", CMAPPED_LEVEL},
      {"pli", VECTOR_LEVEL},  {"tpl", PALETTE_LEVEL}, {"psd", RASTER_LEVEL},
      {"txt", UNKNOW_FILE}};
  for (auto &t : types)
    if (t.second != UNKNOW_FILE) declare(t.first, t.second);
}

std::string esc(const std::string &s) {
  std::string out;
  char buf[8];
  for (unsigned char c : s)
    if (c >= 0x20 && c < 0x7f && c != '\\')
      out += char(c);
    else
      snprintf(buf, sizeof buf, "\\x%02X", c), out += buf;
  return out;
}

std::string esc(const std::wstring &w) {
  std::string out;
  char buf[16];
  for (wchar_t c : w)
    if (c >= 0x20 && c < 0x7f && c != '\\')
      out += char(c);
    else
      snprintf(buf, sizeof buf, "\\u{%X}", unsigned(c)), out += buf;
  return out;
}

const time_t kOldTime = 1000000000;  // every entry of a fresh tree

//------------------------------------------------------------
//  Building trees
//------------------------------------------------------------

bool writeFile(const std::string &path, const std::string &content,
               mode_t mode = 0644) {
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd < 0) return false;
  bool ok = ::write(fd, content.data(), content.size()) == (ssize_t)content.size();
  ::close(fd);
  ::chmod(path.c_str(), mode);
  return ok;
}

void setOldTimes(const std::string &path) {
  struct timespec t[2];
  t[0].tv_sec = t[1].tv_sec = kOldTime;
  t[0].tv_nsec = t[1].tv_nsec = 0;
  ::utimensat(AT_FDCWD, path.c_str(), t, AT_SYMLINK_NOFOLLOW);
}

void forEachEntry(const std::string &root,
                  const std::function<void(const std::string &)> &f) {
  DIR *d = ::opendir(root.c_str());
  if (!d) return;
  std::vector<std::string> names;
  while (struct dirent *e = ::readdir(d))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
      names.push_back(e->d_name);
  ::closedir(d);
  for (const std::string &n : names) {
    std::string p = root + "/" + n;
    struct stat st;
    if (::lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
      forEachEntry(p, f);
    f(p);
  }
}

#ifdef __APPLE__
bool makeAlias(const std::string &target, const std::string &alias) {
  CFURLRef t = CFURLCreateFromFileSystemRepresentation(
      nullptr, (const UInt8 *)target.c_str(), (CFIndex)target.size(), false);
  CFURLRef a = CFURLCreateFromFileSystemRepresentation(
      nullptr, (const UInt8 *)alias.c_str(), (CFIndex)alias.size(), false);
  bool ok = false;
  if (t && a) {
    CFDataRef bm = CFURLCreateBookmarkData(
        nullptr, t, kCFURLBookmarkCreationSuitableForBookmarkFile, nullptr,
        nullptr, nullptr);
    if (bm) {
      ok = CFURLWriteBookmarkDataToFile(bm, a, 0, nullptr);
      CFRelease(bm);
    }
  }
  if (t) CFRelease(t);
  if (a) CFRelease(a);
  return ok;
}
#endif

// Names of many kinds, in UTF-8; some cannot coexist on a file system that
// ignores case or normalization, and the second is then not made.
const char *const kNames[] = {
    "Alpha", "beta.txt", "Gamma.png", "gamma2.png", "_under", "-dash", "0zero",
    "9.png", "10.png", "Zeta", "zeta2", "caf\xC3\xA9",       // café, composed
    "cafe\xCC\x81" "2",                                     // café, decomposed
    "\xC3\x89t\xC3\xA9",                                    // Été
    "E\xCC\x81te\xCC\x81" "2",                              // Été decomposed
    "\xE2\x84\xA6" "ohm",                                   // U+2126 OHM SIGN
    "\xCE\xA9" "omega",                                     // U+03A9
    "\xE6\x97\xA5\xE6\x9C\xAC" ".png",                      // 日本
    "emoji\xF0\x9F\x98\x80.png", "\xC3\x9F" "eta",          // ß
    "SSeta2", "\xC5\xBF" "x",                               // ſ (long s)
    "\xC7\x85" "dz",                                        // ǅ
    "with space", "Mixed.Case.Dots", "UPPER", "lower", "a#b.psd",
    "\xC4\xB0" "stanbul",                                   // İ
    "i\xCC\x87" "stanbul2", "\xEF\xAC\x81" "ligature",      // ﬁ
    "Ä", "a\xCC\x88" "2", "z\xC3\xBC" "rich", "Zurich", "\xC3\xB8" "re",
};

// The tree every test reads, and the operations change a copy of.
void makeTree(const std::string &r) {
  auto md = [&](const std::string &p, mode_t m = 0755) {
    ::mkdir((r + "/" + p).c_str(), m);
  };
  auto wf = [&](const std::string &p, const std::string &c, mode_t m = 0644) {
    writeFile(r + "/" + p, c, m);
  };
  ::mkdir(r.c_str(), 0755);
  for (const char *n : kNames) wf(n, std::string("content of ") + n);
  wf(".hidden", "hidden by name");
  wf("hiddenflag.txt", "hidden by flag");
#ifdef __APPLE__
  ::chflags((r + "/hiddenflag.txt").c_str(), UF_HIDDEN);
#endif
  wf("mode640.txt", "640", 0640);
  wf("mode755.sh", "755", 0755);
  wf("mode444.txt", "444", 0444);
  wf("unreadable", "nobody reads me", 0000);
  wf("hideme.txt", "to hide");
  wf("seq.0001.png", "s1");
  wf("seq.0002.png", "s2");
  wf("seq.0003a.png", "s3a");
  wf("seq_palette.tpl", "pal");
  wf("seq.tpl", "pal2");
  wf("pippo_0001.tif", "p1");
  wf("pippo_0002.tif", "p2");
  wf("lev.tlv", "tlv");
  wf("lev.tpl", "tpl");
  wf("doc#layer.psd", "psd");
  wf("big.bin", std::string(100000, 'x'));
  wf("empty.txt", "");
  md("dir1");
  wf("dir1/inner.txt", "inner");
  wf("dir1/.innerhidden", "inner hidden");
  md("dir1/sub");
  wf("dir1/sub/deep.png", "deep");
  md("Dir2");
  md(".hiddendir");
  wf(".hiddendir/x", "x");
  md("hiddenflagdir");
#ifdef __APPLE__
  ::chflags((r + "/hiddenflagdir").c_str(), UF_HIDDEN);
#endif
  md("emptydir");
  md("dir.with");
  md("dir.with.dots");
  md("emptyrel");
  md("unreadable_dir", 0755);
  wf("unreadable_dir/secret", "s");
  ::chmod((r + "/unreadable_dir").c_str(), 0000);
  md("noexec_dir", 0755);
  wf("noexec_dir/f", "f");
  ::chmod((r + "/noexec_dir").c_str(), 0644);
  md("tree_plain");
  md("tree_plain/a");
  wf("tree_plain/a/f1", "1");
  wf("tree_plain/f2", "2");
  md("tree_hidden");
  wf("tree_hidden/.h", "h");
  wf("tree_hidden/v", "v");
  md("linktarget");
  wf("linktarget/t", "t");
  md("tree_links");
  wf("tree_links/f", "f");
  ::symlink("../linktarget", (r + "/tree_links/to_dir").c_str());
  ::symlink("../Alpha", (r + "/tree_links/to_file").c_str());
  ::symlink("Alpha", (r + "/link_to_file").c_str());
  ::symlink("dir1", (r + "/link_to_dir").c_str());
  ::symlink("nowhere", (r + "/broken_link").c_str());
  ::symlink("nowhere2", (r + "/broken_link2").c_str());
  ::symlink(".hidden", (r + "/link_to_hidden").c_str());
  ::mkfifo((r + "/fifo").c_str(), 0644);
#ifdef __APPLE__
  makeAlias(r + "/Alpha", r + "/alias_to_file");
  makeAlias(r + "/dir1", r + "/alias_to_dir");
#endif
  // a name that is not UTF-8 (refused by Apple's file systems)
  wf("bad\xFF.png", "bad");
  // many names, to sort
  md("many");
  md("dirs");
  int k = 0;
  for (const char *a : kNames)
    for (const char *b : {"", "x", "X", "_1", "10", "9"}) {
      std::string n = std::string(a) + b;
      wf("many/" + n, "m");
      if (++k % 3 == 0) {
        md("dirs/" + n);
        wf("dirs/" + n + "/f.png", "f");
        if (k % 2 == 0) md("dirs/" + n + "/Sub" + b);
      }
    }
  forEachEntry(r, setOldTimes);
  setOldTimes(r);
}

void chmodAll(const std::string &root) {
  ::chmod(root.c_str(), 0755);
  DIR *d = ::opendir(root.c_str());
  if (!d) return;
  std::vector<std::string> names;
  while (struct dirent *e = ::readdir(d))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
      names.push_back(e->d_name);
  ::closedir(d);
  for (const std::string &n : names) {
    std::string p = root + "/" + n;
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0) continue;
#ifdef __APPLE__
    ::lchflags(p.c_str(), 0);
#endif
    if (S_ISDIR(st.st_mode)) chmodAll(p);
  }
}

void removeTree(const std::string &root) {
  chmodAll(root);
  std::function<void(const std::string &)> rm = [&](const std::string &p) {
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
      DIR *d = ::opendir(p.c_str());
      std::vector<std::string> names;
      if (d) {
        while (struct dirent *e = ::readdir(d))
          if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            names.push_back(e->d_name);
        ::closedir(d);
      }
      for (const std::string &n : names) rm(p + "/" + n);
      ::rmdir(p.c_str());
    } else
      ::unlink(p.c_str());
  };
  rm(root);
}

// What a tree holds, byte for byte: names, kinds, modes, flags, contents,
// link targets, and whether a file still has the time it was made with.
std::string snapshot(const std::string &root, const std::string &rel = "") {
  std::string dir = rel.empty() ? root : root + "/" + rel;
  DIR *d          = ::opendir(dir.c_str());
  if (!d) return rel + " <unreadable>\n";
  std::vector<std::string> names;
  while (struct dirent *e = ::readdir(d))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
      names.push_back(e->d_name);
  ::closedir(d);
  std::sort(names.begin(), names.end());
  std::ostringstream out;
  for (const std::string &n : names) {
    std::string r = rel.empty() ? n : rel + "/" + n;
    std::string p = root + "/" + r;
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0) {
      out << esc(r) << " <gone>\n";
      continue;
    }
    out << esc(r) << " mode=" << std::oct << (st.st_mode & 0177777) << std::dec;
#ifdef __APPLE__
    out << " flags=" << (st.st_flags & UF_HIDDEN);
#endif
    if (S_ISLNK(st.st_mode)) {
      char buf[1024];
      ssize_t k = ::readlink(p.c_str(), buf, sizeof buf);
      out << " -> " << esc(std::string(buf, k > 0 ? k : 0)) << "\n";
    } else if (S_ISREG(st.st_mode)) {
      out << " size=" << st.st_size
          << (st.st_mtime == kOldTime ? " old" : " new");
      std::string content;
      ::chmod(p.c_str(), (st.st_mode & 07777) | S_IRUSR);
      int fd = ::open(p.c_str(), O_RDONLY);
      if (fd >= 0) {
        char buf[4096];
        ssize_t k;
        while ((k = ::read(fd, buf, sizeof buf)) > 0) content.append(buf, k);
        ::close(fd);
      }
      ::chmod(p.c_str(), st.st_mode & 07777);
      std::hash<std::string> h;
      if (n.rfind("alias_", 0) == 0) content = "<bookmark>";
      out << " content=" << h(content) << "\n";
    } else if (S_ISDIR(st.st_mode)) {
      out << " dir\n";
      ::chmod(p.c_str(), (st.st_mode & 07777) | S_IRWXU);
      out << snapshot(root, r);
      ::chmod(p.c_str(), st.st_mode & 07777);
    } else
      out << " special\n";
  }
  return out.str();
}

std::string makeRoot(const char *tag) {
  const char *tmp = getenv("TMPDIR");
  std::string t   = std::string(tmp && *tmp ? tmp : "/tmp");
  while (t.size() > 1 && t.back() == '/') t.pop_back();
  std::string tmpl = t + "/nt_tsystem_" + tag + "_XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  if (!::mkdtemp(buf.data())) return std::string();
  // the real path, so that the paths Qt makes absolute look the same
  char real[PATH_MAX];
  if (!::realpath(buf.data(), real)) return std::string();
  return real;
}

TFilePath tp(const std::string &utf8) { return TFilePath(::to_wstring(utf8)); }

std::string str(const TFilePathSet &s) {
  std::string out;
  for (const TFilePath &p : s) out += esc(p.getWideString()) + "\n";
  return out;
}

std::string str(const QStringList &l) {
  std::string out;
  for (const QString &s : l) out += esc(s.toStdWString()) + "\n";
  return out;
}

std::string str(const std::vector<std::wstring> &l) {
  std::string out;
  for (const std::wstring &s : l) out += esc(s) + "\n";
  return out;
}

// what a call gives, or the exception it throws
std::string attempt(const std::function<std::string()> &f) {
  try {
    return "ok " + f();
  } catch (const TSystemException &e) {
    return "TSystemException " + esc(e.getMessage()) + "\n";
  } catch (const TException &e) {
    return "TException " + esc(e.getMessage()) + "\n";
  } catch (...) {
    return "exception\n";
  }
}

template <class Status>
std::string statusOf(const TFilePath &p) {
  Status s(p);
  std::ostringstream o;
  o << "exist=" << s.doesExist() << " dir=" << s.isDirectory()
    << " link=" << s.isLink() << " size=" << s.getSize()
    << " perm=" << std::hex << int(s.getPermissions()) << std::dec
    << " w=" << s.isWritable() << " r=" << s.isReadable()
    << " wdir=" << s.isWritableDir()
    << " mtime=" << s.getLastModificationTime().toMSecsSinceEpoch()
    << " ctime=" << s.getCreationTime().toMSecsSinceEpoch()
    << " user=" << s.getUser().toStdString()
    << " group=" << s.getGroup().toStdString();
  return o.str();
}

// The two implementations, behind one set of names.
#define SYSTEM_FUNCTIONS(Name, NS, Status)                                    \
  struct Name {                                                               \
    typedef Status FileStatus;                                                \
    static TFilePathSet readDirectory(const TFilePath &p, bool g, bool o,    \
                                      bool h) {                              \
      return NS::readDirectory(p, g, o, h);                                   \
    }                                                                         \
    static TFilePathSet readDirectoryTree(const TFilePath &p, bool g,        \
                                          bool o) {                          \
      return NS::readDirectoryTree(p, g, o);                                  \
    }                                                                         \
    static void readGroupAndAll(TFilePathSet &g, TFilePathSet &a,             \
                                const TFilePath &p) {                        \
      NS::readDirectory(g, a, p);                                             \
    }                                                                         \
    static void readDirReadExe(TFilePathSet &d, const TFilePath &p) {        \
      NS::readDirectory_Dir_ReadExe(d, p);                                    \
    }                                                                         \
    static void readDirItems(QStringList &d, const TFilePath &p) {           \
      NS::readDirectory_DirItems(d, p);                                       \
    }                                                                         \
    static bool doesExistFileOrLevel(const TFilePath &p) {                    \
      return NS::doesExistFileOrLevel(p);                                     \
    }                                                                         \
    static void mkDir(const TFilePath &p) { NS::mkDir(p); }                   \
    static void rmDir(const TFilePath &p) { NS::rmDir(p); }                   \
    static void rmDirTree(const TFilePath &p) { NS::rmDirTree(p); }           \
    static void copyDir(const TFilePath &d, const TFilePath &s) {             \
      NS::copyDir(d, s);                                                      \
    }                                                                         \
    static bool touchParentDir(const TFilePath &p) {                          \
      return NS::touchParentDir(p);                                           \
    }                                                                         \
    static void copyFile(const TFilePath &d, const TFilePath &s, bool o) {    \
      NS::copyFile(d, s, o);                                                  \
    }                                                                         \
    static void renameFile(const TFilePath &d, const TFilePath &s, bool o) {  \
      NS::renameFile(d, s, o);                                                \
    }                                                                         \
    static void replaceFile(const TFilePath &d, const TFilePath &s) {         \
      NS::replaceFile(d, s);                                                  \
    }                                                                         \
    static void deleteFile(const TFilePath &p) { NS::deleteFile(p); }         \
    static void hideFile(const TFilePath &p) { NS::hideFile(p); }             \
    static void copyFileOrLevel(const TFilePath &d, const TFilePath &s) {     \
      NS::copyFileOrLevel_throw(d, s);                                        \
    }                                                                         \
    static void renameFileOrLevel(const TFilePath &d, const TFilePath &s,     \
                                  bool pal) {                                \
      NS::renameFileOrLevel_throw(d, s, pal);                                 \
    }                                                                         \
    static void removeFileOrLevel(const TFilePath &p) {                       \
      NS::removeFileOrLevel_throw(p);                                         \
    }                                                                         \
    static void hideFileOrLevel(const TFilePath &p) {                         \
      NS::hideFileOrLevel_throw(p);                                           \
    }                                                                         \
  };

SYSTEM_FUNCTIONS(NewSystem, TSystem, TFileStatus)
SYSTEM_FUNCTIONS(OldSystem, LegacyTSystem, LegacyTFileStatus)

void replaceAll(std::string &s, const std::string &a, const std::string &b) {
  for (size_t i = 0; (i = s.find(a, i)) != std::string::npos; i += b.size())
    s.replace(i, a.size(), b);
}

// The paths a test asks about, under a tree's root.
std::vector<std::string> pathsToAsk(const std::string &root) {
  std::vector<std::string> v = {root, root + "/missing", root + "/",
                                root + "/./Alpha", root + "/dir1/../Alpha",
                                root + "//Alpha", root + "/Alpha/",
                                "", ".", "Alpha", "dir1", "missing"};
  DIR *d = ::opendir(root.c_str());
  while (struct dirent *e = d ? ::readdir(d) : nullptr) {
    // not "..": the temporary folder, which other tests change meanwhile
    if (!strcmp(e->d_name, "..")) continue;
    v.push_back(root + "/" + e->d_name);
    v.push_back(root + "/" + e->d_name + "/x");
  }
  if (d) ::closedir(d);
  // the names composed and decomposed, whichever is on disk
  for (const char *n : kNames) v.push_back(root + "/" + n);
  v.push_back(root + "/cafe\xCC\x81");
  v.push_back(root + "/caf\xC3\xA9" "2");
  v.push_back(root + "/ALPHA");
  v.push_back(root + "/seq..png");
  v.push_back(root + "/seq.png");
  v.push_back(root + "/pippo_.tif");
  v.push_back(root + "/doc.psd");
  v.push_back(root + "/doc#layer#2.psd");
  v.push_back(root + "/missing..png");
  return v;
}

// Everything the read-only functions say about the tree, as text.
template <class S>
std::string readEverything(const std::string &root) {
  std::ostringstream out;
  char *here      = ::getcwd(nullptr, 0);
  ::chdir(root.c_str());
  for (const std::string &p : pathsToAsk(root)) {
    TFilePath fp = tp(p);
    out << "== " << esc(p) << "\n";
    out << "status " << statusOf<typename S::FileStatus>(fp) << "\n";
    out << "exists-or-level " << S::doesExistFileOrLevel(fp) << "\n";
    for (int flags = 0; flags < 8; ++flags)
      out << "readDirectory " << flags << " "
          << attempt([&] {
               return "\n" + str(S::readDirectory(fp, flags & 1, flags & 2,
                                                  flags & 4));
             });
    for (int flags = 0; flags < 4; ++flags)
      out << "readDirectoryTree " << flags << " "
          << attempt([&] {
               return "\n" + str(S::readDirectoryTree(fp, flags & 1, flags & 2));
             });
    out << "groupAndAll " << attempt([&] {
      TFilePathSet g, a;
      S::readGroupAndAll(g, a, fp);
      return "\n" + str(g) + "--\n" + str(a);
    });
    out << "dirReadExe " << attempt([&] {
      TFilePathSet d;
      S::readDirReadExe(d, fp);
      return "\n" + str(d);
    });
    out << "dirItems " << attempt([&] {
      QStringList d;
      S::readDirItems(d, fp);
      return "\n" + str(d);
    });
  }
  ::chdir(here);
  free(here);
  return out.str();
}

// The operations, each on a fresh name or a name of the tree, with what each
// gave; then the tree.
template <class S>
std::string operate(const std::string &root) {
  std::ostringstream out;
  auto P = [&](const std::string &rel) { return tp(root + "/" + rel); };
  auto op = [&](const std::string &what, const std::function<void()> &f) {
    out << what << ": " << attempt([&] {
      f();
      return std::string("\n");
    });
  };
  char *here = ::getcwd(nullptr, 0);
  ::chdir(root.c_str());

  op("mkDir new/sub/deep", [&] { S::mkDir(P("new/sub/deep")); });
  op("mkDir plus+dir", [&] { S::mkDir(P("plus+dir")); });
  op("mkDir under a file", [&] { S::mkDir(P("Alpha/x")); });
  op("mkDir existing", [&] { S::mkDir(P("dir1")); });
  op("mkDir relative", [&] { S::mkDir(tp("rel/a/b")); });
  op("mkDir composed", [&] { S::mkDir(P("caf\xC3\xA9" "dir/\xC3\x89t\xC3\xA9")); });
  op("mkDir over link", [&] { S::mkDir(P("link_to_dir/made")); });
  op("rmDir emptydir", [&] { S::rmDir(P("emptydir")); });
  op("rmDir dir.with.dots", [&] { S::rmDir(P("dir.with.dots")); });
  op("rmDir missing", [&] { S::rmDir(P("missing")); });
  op("rmDir not empty", [&] { S::rmDir(P("dir1")); });
  op("rmDir relative", [&] { S::rmDir(tp("emptyrel")); });
  op("rmDirTree plain", [&] { S::rmDirTree(P("tree_plain")); });
  op("rmDirTree hidden", [&] { S::rmDirTree(P("tree_hidden")); });
  op("rmDirTree links", [&] { S::rmDirTree(P("tree_links")); });
  op("rmDirTree missing", [&] { S::rmDirTree(P("missing")); });
  op("copyDir", [&] { S::copyDir(P("copy1"), P("dir1")); });
  op("copyDir again", [&] { S::copyDir(P("copy1"), P("dir1")); });
  op("copyDir many", [&] { S::copyDir(P("copymany"), P("dirs")); });
  op("copyFile", [&] { S::copyFile(P("c1"), P("Alpha"), true); });
  op("copyFile no overwrite", [&] { S::copyFile(P("beta.txt"), P("Alpha"), false); });
  op("copyFile overwrite", [&] { S::copyFile(P("beta.txt"), P("Alpha"), true); });
  op("copyFile new parent", [&] { S::copyFile(P("np/x/c2"), P("Alpha"), true); });
  op("copyFile a folder", [&] { S::copyFile(P("c3"), P("dir1"), true); });
  op("copyFile missing", [&] { S::copyFile(P("c4"), P("missing"), true); });
  op("copyFile link", [&] { S::copyFile(P("c5"), P("link_to_file"), true); });
  op("copyFile unreadable", [&] { S::copyFile(P("c6"), P("unreadable"), true); });
  op("copyFile modes", [&] {
    S::copyFile(P("c640"), P("mode640.txt"), true);
    S::copyFile(P("c755"), P("mode755.sh"), true);
    S::copyFile(P("c444"), P("mode444.txt"), true);
  });
  op("copyFile decomposed", [&] { S::copyFile(P("c7"), P("cafe\xCC\x81" "2"), true); });
  op("copyFile to composed", [&] { S::copyFile(P("\xC3\xA9" "copy"), P("Alpha"), true); });
  op("copyFile onto broken link", [&] { S::copyFile(P("broken_link"), P("Alpha"), false); });
  op("copyFile fifo", [&] { S::copyFile(P("c8"), P("fifo_missing"), true); });
  op("copyFile same", [&] { S::copyFile(P("Alpha"), P("Alpha"), true); });
  op("copyFile empty", [&] { S::copyFile(P("c9"), P("empty.txt"), true); });
  op("copyFile big", [&] { S::copyFile(P("c10"), P("big.bin"), true); });
  op("renameFile", [&] { S::renameFile(P("r1"), P("c1"), true); });
  op("renameFile no overwrite", [&] { S::renameFile(P("beta.txt"), P("r1"), false); });
  op("renameFile overwrite", [&] { S::renameFile(P("beta.txt"), P("r1"), true); });
  op("renameFile case", [&] { S::renameFile(P("ALPHA"), P("Alpha"), false); });
  op("renameFile long s", [&] { S::renameFile(P("Sx"), P("\xC5\xBF" "x"), false); });
  op("renameFile sharp s", [&] { S::renameFile(P("SSETA2"), P("SSeta2"), false); });
  op("renameFile folder", [&] { S::renameFile(P("r_dir"), P("copy1"), false); });
  op("renameFile broken link", [&] { S::renameFile(P("r2"), P("broken_link"), false); });
  op("renameFile broken link 2", [&] { S::renameFile(P("r2b"), P("broken_link2"), false); });
  op("renameFile broken link onto a file", [&] { S::renameFile(P("Zeta"), P("r2b"), false); });
  op("renameFile link", [&] { S::renameFile(P("r3"), P("link_to_file"), false); });
  op("renameFile composed", [&] { S::renameFile(P("r4"), P("caf\xC3\xA9"), false); });
  op("renameFile missing", [&] { S::renameFile(P("r5"), P("missing"), false); });
  op("renameFile into missing folder", [&] { S::renameFile(P("nofolder/r6"), P("c5"), false); });
  op("replaceFile", [&] { S::replaceFile(P("c9"), P("c10")); });
  op("replaceFile missing", [&] { S::replaceFile(P("c9"), P("missing")); });
  op("deleteFile", [&] { S::deleteFile(P("c9")); });
  op("deleteFile missing", [&] { S::deleteFile(P("missing")); });
  op("deleteFile folder", [&] { S::deleteFile(P("dir1")); });
  op("deleteFile link", [&] { S::deleteFile(P("link_to_dir")); });
  op("hideFile", [&] { S::hideFile(P("hideme.txt")); });
  op("touchParentDir", [&] { out << S::touchParentDir(P("tp/a/b.txt")); });
  op("touchParentDir file", [&] { out << S::touchParentDir(P("Alpha2/x/b.txt")); });
  op("copyFileOrLevel", [&] { S::copyFileOrLevel(P("cseq..png"), P("seq..png")); });
  op("copyFileOrLevel file", [&] { S::copyFileOrLevel(P("cl.tlv"), P("lev.tlv")); });
  op("renameFileOrLevel", [&] { S::renameFileOrLevel(P("rseq..png"), P("cseq..png"), true); });
  op("renameFileOrLevel palette", [&] { S::renameFileOrLevel(P("rl.tlv"), P("lev.tlv"), true); });
  op("renameFileOrLevel underscore", [&] { S::renameFileOrLevel(P("rp_.tif"), P("pippo_.tif"), false); });
  op("removeFileOrLevel", [&] { S::removeFileOrLevel(P("rseq..png")); });
  op("hideFileOrLevel", [&] { S::hideFileOrLevel(P("seq..png")); });
  op("mkDir relative composed", [&] { S::mkDir(tp("\xC3\x89t\xC3\xA9rel")); });

  ::chdir(here);
  free(here);
  std::string s = out.str();
  replaceAll(s, esc(::to_wstring(root)), "<root>");
  replaceAll(s, esc(root), "<root>");
  return s + "-- tree\n" + snapshot(root);
}


// Reports where two texts differ, block by block (a block starts with a line
// beginning with the given marker); with NEXTTOONZ_TSYSTEM_DUMP set to a
// folder, also writes both texts there to compare them with diff(1).
void expectSameText(const std::string &oldText, const std::string &newText,
                    const std::string &marker, const std::string &name) {
  if (const char *dump = getenv("NEXTTOONZ_TSYSTEM_DUMP")) {
    for (int i = 0; i < 2; ++i) {
      std::string path = std::string(dump) + "/" + name + (i ? ".new" : ".old");
      if (FILE *f = fopen(path.c_str(), "wb")) {
        const std::string &t = i ? newText : oldText;
        fwrite(t.data(), 1, t.size(), f);
        fclose(f);
      }
    }
  }
  if (oldText == newText) return;
  auto blocks = [&](const std::string &t) {
    std::vector<std::string> v;
    std::istringstream in(t);
    std::string line;
    while (std::getline(in, line)) {
      if (v.empty() || (!marker.empty() && line.rfind(marker, 0) == 0))
        v.emplace_back();
      v.back() += line + "\n";
    }
    return v;
  };
  std::vector<std::string> a = blocks(oldText), b = blocks(newText);
  int shown = 0;
  for (size_t i = 0; i < std::max(a.size(), b.size()) && shown < 8; ++i) {
    std::string x = i < a.size() ? a[i] : "<none>";
    std::string y = i < b.size() ? b[i] : "<none>";
    if (x == y) continue;
    ++shown;
    std::istringstream sx(x), sy(y);
    std::string lx, ly, msg;
    int lines = 0;
    for (;;) {
      bool gx = bool(std::getline(sx, lx)), gy = bool(std::getline(sy, ly));
      if (!gx && !gy) break;
      if (!gx) lx = "<none>";
      if (!gy) ly = "<none>";
      if (lines == 0) msg += lx + "\n";
      else if (lx != ly)
        msg += "  old: " + lx.substr(0, 300) + "\n  new: " + ly.substr(0, 300) + "\n";
      if (++lines > 400) break;
    }
    ADD_FAILURE() << msg;
  }
  EXPECT_EQ(a.size(), b.size());
}

class TSystemQt : public ::testing::Test {
protected:
  static QCoreApplication *app;
  static std::string root;

  static void SetUpTestSuite() {
    registerTypes();
    // the old TFileStatus needs one, or it reports every path as missing
    static int argc = 1;
    static char arg0[] = "nexttoonz_core_tests";
    static char *argv[] = {arg0, nullptr};
    if (!QCoreApplication::instance()) app = new QCoreApplication(argc, argv);
    root = makeRoot("read");
    makeTree(root);
  }
  static void TearDownTestSuite() {
    if (!root.empty()) removeTree(root);
    delete app;
    app = nullptr;
  }
};

QCoreApplication *TSystemQt::app = nullptr;
std::string TSystemQt::root;

}  // namespace

TEST_F(TSystemQt, TreeHasEveryKindOfEntry) {
  ASSERT_FALSE(root.empty());
  struct stat st;
  EXPECT_EQ(::lstat((root + "/broken_link").c_str(), &st), 0);
  EXPECT_EQ(::lstat((root + "/fifo").c_str(), &st), 0);
  EXPECT_TRUE(S_ISFIFO(st.st_mode));
#ifdef __APPLE__
  EXPECT_EQ(::lstat((root + "/alias_to_file").c_str(), &st), 0);
  EXPECT_EQ(::lstat((root + "/hiddenflag.txt").c_str(), &st), 0);
  EXPECT_TRUE(st.st_flags & UF_HIDDEN);
#endif
}

TEST_F(TSystemQt, ReadsTheTreeAsQtDid) {
  std::string before = snapshot(root);
  std::string oldText = readEverything<OldSystem>(root);
  std::string newText = readEverything<NewSystem>(root);
  EXPECT_EQ(snapshot(root), before);
  expectSameText(oldText, newText, "== ", "read");
}

TEST_F(TSystemQt, OperatesOnATreeAsQtDid) {
  std::string a = makeRoot("ops");
  std::string b = makeRoot("ops");
  ASSERT_FALSE(a.empty());
  ASSERT_FALSE(b.empty());
  makeTree(a);
  makeTree(b);
  expectSameText(snapshot(a), snapshot(b), "", "trees");
  std::string oldText = operate<OldSystem>(a);
  std::string newText = operate<NewSystem>(b);
  removeTree(a);
  removeTree(b);
  expectSameText(oldText, newText, "-- tree", "operations");
}

// The decision of step 6a: without a QCoreApplication the old TFileStatus
// said that nothing existed; the new one reports the file system as it is.
TEST(TSystemNoQCoreApplication, StatusIsTheRealOne) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  std::string root = makeRoot("noapp");
  ASSERT_FALSE(root.empty());
  ASSERT_TRUE(writeFile(root + "/file.txt", "12345"));
  TFilePath file = tp(root + "/file.txt");
  EXPECT_FALSE(LegacyTFileStatus(file).doesExist());
  EXPECT_FALSE(LegacyTFileStatus(tp(root)).doesExist());

  TFileStatus fs(file);
  EXPECT_TRUE(fs.doesExist());
  EXPECT_FALSE(fs.isDirectory());
  EXPECT_EQ(fs.getSize(), 5);
  EXPECT_TRUE(fs.isReadable());
  EXPECT_TRUE(TFileStatus(tp(root)).isDirectory());
  EXPECT_TRUE(TFileStatus(tp(root)).isWritableDir());
  EXPECT_FALSE(TFileStatus(tp(root + "/missing")).doesExist());
  ::unlink((root + "/file.txt").c_str());
  ::rmdir(root.c_str());
}

//==============================================================
//  Step 6c: the process's paths and information, and tenv's first-run copy
//==============================================================

namespace {

// Sets an environment variable (nullptr unsets it) until destroyed.
class ScopedEnv {
  std::string m_name, m_old;
  bool m_had;

public:
  ScopedEnv(const char *name, const char *value) : m_name(name) {
    const char *old = getenv(name);
    m_had           = old != nullptr;
    if (old) m_old = old;
    if (value)
      setenv(name, value, 1);
    else
      unsetenv(name);
  }
  ~ScopedEnv() {
    if (m_had)
      setenv(m_name.c_str(), m_old.c_str(), 1);
    else
      unsetenv(m_name.c_str());
  }
};

std::wstring w(const QString &s) { return s.toStdWString(); }
QString q(const std::wstring &s) { return QString::fromStdWString(s); }

}  // namespace

TEST_F(TSystemQt, ProcessInformationIsQts) {
  using namespace TSystemQtSemantics;
  EXPECT_EQ(applicationFilePath(), w(QCoreApplication::applicationFilePath()));
  EXPECT_EQ(applicationDirPath(), w(QCoreApplication::applicationDirPath()));
  EXPECT_FALSE(applicationDirPath().empty());
  EXPECT_EQ(TSystem::getBinDir(), LegacyTSystem::getBinDir());
  EXPECT_EQ(TSystem::getDllDir(), LegacyTSystem::getDllDir());
  EXPECT_EQ(TSystem::getHostNameW(), w(QHostInfo::localHostName()));
  EXPECT_EQ(TSystem::getHostName(), LegacyTSystem::getHostName());
  EXPECT_EQ(TSystem::getProcessorCount(), QThread::idealThreadCount());

  for (const char *user : {"me", "", "\xC3\xBCn\xC3\xAF", "a=b", (const char *)nullptr}) {
    ScopedEnv env("USER", user);
    EXPECT_EQ(TSystem::getUserNameW(), w(LegacyTSystem::getUserName()))
        << (user ? user : "(unset)");
    EXPECT_EQ(TSystem::getUserName(), LegacyTSystem::getUserName());
  }

  for (const char *tmp : {"/tmp/", "//private//tmp//x/../", "relative/dir/",
                          "/", "cafe\xCC\x81/", "", "/a/./b/.", (const char *)nullptr}) {
    ScopedEnv env("TMPDIR", tmp);
    EXPECT_EQ(tempPath(), w(QDir::tempPath())) << (tmp ? tmp : "(unset)");
    EXPECT_EQ(TSystem::getTempDir(), LegacyTSystem::getTempDir());
    // a random name, so the format only: the same folder, a backslash, the
    // field, 32 lowercase hexadecimal digits of a version 4 UUID
    for (const wchar_t *field : {L"", L"externfx", L"café"}) {
      std::wstring a = LegacyTSystem::getUniqueFile(q(field)).getWideString();
      std::wstring b = TSystem::getUniqueFileW(field).getWideString();
      ASSERT_EQ(a.size(), b.size()) << ::to_string(a) << " " << ::to_string(b);
      ASSERT_GE(b.size(), 32u);
      EXPECT_EQ(a.substr(0, a.size() - 32), b.substr(0, b.size() - 32));
      std::wstring uuid = b.substr(b.size() - 32);
      for (wchar_t c : uuid)
        EXPECT_TRUE((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'));
      EXPECT_EQ(uuid[12], L'4');
      EXPECT_NE(std::wstring(L"89ab").find(uuid[16]), std::wstring::npos);
      std::wstring other = LegacyTSystem::getUniqueFile(q(field)).getWideString();
      EXPECT_EQ(other[other.size() - 32 + 12], L'4');
    }
  }

  for (const char *home : {"/Users/me", "/a//b/../c/", "", "cafe\xCC\x81", "/",
                           (const char *)nullptr}) {
    ScopedEnv env("HOME", home);
    EXPECT_EQ(homePath(), w(QDir::homePath())) << (home ? home : "(unset)");
  }

  for (const char *name : {"HOME", "PATH", "NEXTTOONZ_SURELY_UNSET"}) {
    EXPECT_EQ(environmentVariable(name),
              w(QString::fromLocal8Bit(qgetenv(name))));
  }
  {
    ScopedEnv env("NEXTTOONZ_TEST_VALUE", "caf\xC3\xA9 \xF0\x9F\x98\x80");
    EXPECT_EQ(environmentVariable("NEXTTOONZ_TEST_VALUE"),
              w(QString::fromLocal8Bit(qgetenv("NEXTTOONZ_TEST_VALUE"))));
  }

  // the current folder, also under decomposed and non-Latin-1 names and
  // through a link
  char *here = ::getcwd(nullptr, 0);
  std::string cur = root + "/cwd_cafe\xCC\x81_\xE6\x97\xA5";
  ::mkdir(cur.c_str(), 0755);
  for (const std::string &dir : {root, cur, root + "/link_to_dir", root + "/dir1/sub"}) {
    ASSERT_EQ(::chdir(dir.c_str()), 0) << dir;
    EXPECT_EQ(currentPath(), w(QDir::currentPath())) << dir;
    QByteArray latin1 = QDir::currentPath().toLatin1();
    EXPECT_EQ(toLatin1(currentPath()),
              std::string(latin1.constData(), size_t(latin1.size())));
  }
  ::chdir(here);
  free(here);
  ::rmdir(cur.c_str());

  for (const std::wstring &s :
       {std::wstring(L"café"), std::wstring(L"日\U0001F600x"),
        std::wstring(L"ÿĀ")}) {
    QByteArray latin1 = q(s).toLatin1();
    EXPECT_EQ(toLatin1(s), std::string(latin1.constData(), size_t(latin1.size())));
  }
}

TEST_F(TSystemQt, CleanPathIsQts) {
  using TSystemQtSemantics::cleanPath;
  // every string of up to 7 characters made of 'a', '/' and '.'
  std::vector<std::wstring> all = {L""};
  int checked = 0;
  for (int len = 1; len <= 7; ++len) {
    std::vector<std::wstring> next;
    for (const std::wstring &p : all)
      if (p.size() == size_t(len - 1))
        for (wchar_t c : {L'a', L'/', L'.'}) next.push_back(p + c);
    for (const std::wstring &p : next) {
      ASSERT_EQ(cleanPath(p), w(QDir::cleanPath(q(p)))) << ::to_string(p);
      ++checked;
    }
    all.insert(all.end(), next.begin(), next.end());
  }
  for (const wchar_t *p : {L"/Users/me/../../../x", L"a/b/c/../../../../d",
                           L"~/x/./y//", L"..../.../x", L"é/../é"})
    EXPECT_EQ(cleanPath(p), w(QDir::cleanPath(q(p)))) << ::to_string(std::wstring(p));
  EXPECT_GT(checked, 3000);
}

TEST_F(TSystemQt, PathsOfTheTreeAreQts) {
  using namespace TSystemQtSemantics;
  char *here = ::getcwd(nullptr, 0);
  ::chdir(root.c_str());
  for (const std::string &p : pathsToAsk(root)) {
    std::wstring wp = ::to_wstring(p);
    QFileInfo fi(q(wp));
    EXPECT_EQ(absoluteFilePath(wp), w(fi.absoluteFilePath())) << p;
    EXPECT_EQ(absolutePath(wp), w(fi.absolutePath())) << p;
    EXPECT_EQ(canonicalFilePath(wp), w(fi.canonicalFilePath())) << p;
  }
  ::chdir(here);
  free(here);
}

namespace {

std::map<std::string, std::string> probeRun(const std::string &command) {
  std::map<std::string, std::string> out;
  FILE *f = popen(command.c_str(), "r");
  if (!f) return out;
  char line[4096];
  while (fgets(line, sizeof line, f)) {
    std::string l(line);
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
    size_t eq = l.find('=');
    if (eq != std::string::npos) out[l.substr(0, eq)] = l.substr(eq + 1);
  }
  out["status"] = std::to_string(pclose(f));
  return out;
}

std::string quote(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  return out + "'";
}

}  // namespace

TEST_F(TSystemQt, ExecutablePathIsQtsHoweverLaunched) {
  const std::string probe = NEXTTOONZ_APPINFO_PROBE;
  const std::string probeDir = probe.substr(0, probe.find_last_of('/'));
  const std::string probeName = probe.substr(probe.find_last_of('/') + 1);
#ifdef __APPLE__
  const std::string libs = "DYLD_LIBRARY_PATH=" + quote(probeDir) + " ";
#else
  const std::string libs = "LD_LIBRARY_PATH=" + quote(probeDir) + " ";
#endif
  std::string base = makeRoot("probe");
  ASSERT_FALSE(base.empty());
  std::string link = base + "/probe link";
  ASSERT_EQ(::symlink(probe.c_str(), link.c_str()), 0);
  std::vector<std::pair<std::string, std::string>> forms = {
      {"absolute", libs + quote(probe)},
      {"relative", "cd " + quote(probeDir) + " && " + libs + "./" + quote(probeName)},
      {"PATH", "cd / && PATH=" + quote(probeDir) + " " + libs + quote(probeName)},
      {"symlink", libs + quote(link)},
      {"symlink, relative", "cd " + quote(base) + " && " + libs + "'./probe link'"}};
#ifdef __APPLE__
  // in an application bundle, as OpenToonz.app's programs are; also
  // through a link to it
  std::string app = base + "/Probe d\xC3\xA9j\xC3\xA0.app/Contents";
  std::string macos = app + "/MacOS";
  ASSERT_EQ(system(("mkdir -p " + quote(macos) + " && cp " + quote(probe) + " " +
                    quote(macos + "/Probe")).c_str()),
            0);
  writeFile(app + "/Info.plist",
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<plist version=\"1.0\"><dict>"
            "<key>CFBundleExecutable</key><string>Probe</string>"
            "<key>CFBundleIdentifier</key><string>io.nexttoonz.probe</string>"
            "<key>CFBundlePackageType</key><string>APPL</string>"
            "</dict></plist>\n");
  std::string appLink = base + "/applink";
  ASSERT_EQ(::symlink((macos + "/Probe").c_str(), appLink.c_str()), 0);
  forms.push_back({"bundle", libs + quote(macos + "/Probe")});
  forms.push_back({"bundle, relative", "cd " + quote(macos) + " && " + libs + "./Probe"});
  forms.push_back({"link into the bundle", libs + quote(appLink)});
#endif
  for (const auto &form : forms) {
    auto r = probeRun(form.second);
    SCOPED_TRACE(form.first);
    ASSERT_EQ(r["status"], "0");
    EXPECT_FALSE(r["qt.file"].empty());
    EXPECT_EQ(r["core.after.file"], r["qt.file"]);
    EXPECT_EQ(r["core.after.dir"], r["qt.dir"]);
    EXPECT_EQ(r["core.after.bin"], r["qt.bin"]);
    // and the same before the QCoreApplication (step 6c's decision)
    EXPECT_EQ(r["core.before.file"], r["qt.file"]);
    EXPECT_EQ(r["core.before.dir"], r["qt.dir"]);
    EXPECT_EQ(r["core.before.bin"], r["qt.bin"]);
  }
  removeTree(base);
}

namespace {

// A tree for the removal and the copy: nested folders, hidden entries,
// read-only files and folders, links to a folder and a file outside it, a
// broken link, decomposed names. withSpecial adds what makes a copy fail
// or block (a fifo, a broken link, an unreadable folder).
void makeStuffTree(const std::string &base, bool withSpecial) {
  ::mkdir(base.c_str(), 0755);
  std::string t = base + "/tree";
  auto md = [&](const std::string &p, mode_t m = 0755) {
    ::mkdir((t + "/" + p).c_str(), m);
  };
  auto wf = [&](const std::string &p, const std::string &c, mode_t m = 0644) {
    writeFile(t + "/" + p, c, m);
  };
  ::mkdir((base + "/outside").c_str(), 0755);
  writeFile(base + "/outside/keep", "keep");
  writeFile(base + "/outsidefile", "outside file");
  ::mkdir(t.c_str(), 0755);
  md("a");
  md("a/b");
  md("a/b/c");
  wf("a/b/c/deep.txt", "deep");
  wf("a/.hidden", "hidden");
  md(".hiddendir");
  wf(".hiddendir/x", "x");
  wf("readonly.txt", "ro", 0444);
  md("readonlydir");
  wf("readonlydir/inside", "inside");
  ::chmod((t + "/readonlydir").c_str(), 0555);
  md("caf\xC3\xA9");
  wf("cafe\xCC\x81" ".txt", "nfd");
  md("empty");
  ::symlink("../outside", (t + "/link_to_dir").c_str());
  ::symlink("../outsidefile", (t + "/link_to_file").c_str());
  wf("Zeta", "z");
  wf("alpha", "a");
  if (withSpecial) {
    ::symlink("nowhere", (t + "/broken_link").c_str());
    md("unreadable");
    wf("unreadable/secret", "s");
    ::chmod((t + "/unreadable").c_str(), 0000);
    ::mkfifo((t + "/fifo").c_str(), 0644);
  }
  forEachEntry(base, setOldTimes);
}

// tenv's copyDirOrFail before step 6c
bool legacyCopyDirOrFail(const QString &dst, const QString &src) {
  if (!QDir().mkpath(dst)) return false;

  const QFileInfoList entries = QDir(src).entryInfoList(
      QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
  for (const QFileInfo &fi : entries) {
    const QString target = dst + "/" + fi.fileName();
    // copy symlinks as files: following them could recurse forever
    if (fi.isDir() && !fi.isSymLink()) {
      if (!legacyCopyDirOrFail(target, fi.filePath())) return false;
    } else if (!QFile::copy(fi.filePath(), target)) {
      return false;
    }
  }
  return true;
}

}  // namespace

TEST_F(TSystemQt, RemovesRecursivelyAsQtDid) {
  for (bool special : {false, true}) {
    std::string a = makeRoot("rm"), b = makeRoot("rm");
    makeStuffTree(a, special);
    makeStuffTree(b, special);
    ASSERT_EQ(snapshot(a), snapshot(b));
    std::ostringstream ra, rb;
    for (const char *p : {"/tree/a/b", "/tree", "/missing", "/outsidefile"}) {
      ra << QDir(QString::fromStdString(a + p)).removeRecursively();
      rb << TSystemQtSemantics::removeRecursively(::to_wstring(b + p));
    }
    EXPECT_EQ(ra.str(), rb.str()) << special;
    expectSameText(snapshot(a), snapshot(b), "", "remove");
    // the folder a link pointed to is still there
    EXPECT_NE(snapshot(b).find("outside/keep"), std::string::npos);
    removeTree(a);
    removeTree(b);
  }
}

TEST_F(TSystemQt, CopiesTheStuffTreeAsTenvDid) {
  // no fifo here: QFile::copy would wait on it forever, before and after
  std::string a = makeRoot("cp"), b = makeRoot("cp");
  makeStuffTree(a, false);
  makeStuffTree(b, false);
  ::symlink("nowhere", (a + "/tree/zz_broken").c_str());
  ::symlink("nowhere", (b + "/tree/zz_broken").c_str());
  std::ostringstream ra, rb;
  // a fresh copy (the broken link, last by name, fails it), a copy into
  // a folder that exists, and one of a folder without the broken link
  ra << legacyCopyDirOrFail(QString::fromStdString(a + "/copy.incomplete"),
                            QString::fromStdString(a + "/tree"));
  rb << TSystemQtSemantics::copyDirTree(::to_wstring(b + "/copy.incomplete"),
                                        ::to_wstring(b + "/tree"));
  ra << legacyCopyDirOrFail(QString::fromStdString(a + "/copy.incomplete"),
                            QString::fromStdString(a + "/tree/a"));
  rb << TSystemQtSemantics::copyDirTree(::to_wstring(b + "/copy.incomplete"),
                                        ::to_wstring(b + "/tree/a"));
  ra << legacyCopyDirOrFail(QString::fromStdString(a + "/copy2/new/deep"),
                            QString::fromStdString(a + "/tree/a"));
  rb << TSystemQtSemantics::copyDirTree(::to_wstring(b + "/copy2/new/deep"),
                                        ::to_wstring(b + "/tree/a"));
  // and the rest of the first run: rename into place, mkpath
  ra << QDir().rename(QString::fromStdString(a + "/copy2"), QString::fromStdString(a + "/stuff"))
     << QDir().rename(QString::fromStdString(a + "/missing"), QString::fromStdString(a + "/x"))
     << QDir().rename(QString::fromStdString(a + "/tree/a"), QString::fromStdString(a + "/stuff"))
     << QDir().mkpath(QString::fromStdString(a + "/stuff/projects/library"))
     << QDir().mkpath(QString::fromStdString(a + "/tree/Zeta/x"))
     << QDir().mkpath(QString::fromStdString(a + "/stuff"));
  using namespace TSystemQtSemantics;
  rb << TSystemQtSemantics::rename(::to_wstring(b + "/copy2"), ::to_wstring(b + "/stuff"))
     << TSystemQtSemantics::rename(::to_wstring(b + "/missing"), ::to_wstring(b + "/x"))
     << TSystemQtSemantics::rename(::to_wstring(b + "/tree/a"), ::to_wstring(b + "/stuff"))
     << mkpath(::to_wstring(b + "/stuff/projects/library"))
     << mkpath(::to_wstring(b + "/tree/Zeta/x"))
     << mkpath(::to_wstring(b + "/stuff"));
  EXPECT_EQ(ra.str(), rb.str());
  expectSameText(snapshot(a), snapshot(b), "", "copy");
  removeTree(a);
  removeTree(b);
}

#endif  // !_WIN32
