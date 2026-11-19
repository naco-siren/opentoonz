#include "tsystem.h"

#include <set>
#include <atomic>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include "tfilepath_io.h"
#include "tconvert.h"

#include <cwctype>  // Include the header for towlower

// The file-system functions below are the ones of macOS, iOS and Linux; on
// Windows they are still the Qt-based ones, in tsystem_qt.cpp
// (doc/nexttoonz_core_noqt.md, step 6a). They do what QFileInfo, QDir and
// QFile did here before, rule for rule; tests/core/test_tsystem_qt.cpp
// compares them with that code on a tree of every kind of entry.
#ifndef _WIN32
#include <algorithm>
#include <string>
#include <vector>
#include <cerrno>
#include <climits>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <sys/clonefile.h>
#endif
#endif

namespace {

std::atomic<int> HasMainLoop{-1};  // Changed to atomic for thread safety

}  // namespace

//-----------------------------------------------------------------------------------

bool TSystem::doHaveMainLoop() {
  int current = HasMainLoop.load(std::memory_order_acquire);

  if (current == -1) {
#ifndef NDEBUG
    // was qFatal in a debug Qt build
    fprintf(stderr, "TSystem::hasMainLoop was not called\n");
    abort();
#else
    fprintf(stderr, "Main loop not initialized\n");
#endif
    return false;
  }
  return current == 1;
}

//-----------------------------------------------------------------------------------

void TSystem::hasMainLoop(bool state) {
  int expected = -1;
  int desired  = state ? 1 : 0;

  if (!HasMainLoop.compare_exchange_strong(expected, desired,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
#ifndef NDEBUG
    fprintf(stderr, "TSystem::hasMainLoop called multiple times (debug check)\n");
#endif
  }
}

//-----------------------------------------------------------------------------------

#ifndef MACOSX
// The macOS implementation is in tsystem_mac.mm.
void TSystem::disableWindowStateRestoration() {}
#endif

//------------------------------------------------------------

TFilePath TSystem::getTestDir(std::string name) {
  return TFilePath("C:") + TFilePath(name);
}

//------------------------------------------------------------

namespace {
class CaselessFilepathLess final {
public:
  bool operator()(const TFilePath &a, const TFilePath &b) const {
    // Perform case sensitive compare, fallback to case insensitive.
    const std::wstring a_str = a.getWideString();
    const std::wstring b_str = b.getWideString();

    size_t i            = 0;
    int case_compare    = -1;
    const size_t maxLen = std::max(a_str.size(), b_str.size());
    for (size_t i = 0; i < maxLen; ++i) {
      wchar_t a_char = (i < a_str.size()) ? a_str[i] : L'\0';
      wchar_t b_char = (i < b_str.size()) ? b_str[i] : L'\0';

      if (a_char != b_char) {
        const wchar_t a_wchar = towlower(a_char);
        const wchar_t b_wchar = towlower(b_char);
        if (a_wchar < b_wchar) {
          return true;
        } else if (a_wchar > b_wchar) {
          return false;
        } else if (case_compare == -1) {
          case_compare = (a_char < b_char) ? 1 : 0;
        }
      }
    }
    return (case_compare == 1);
  }
};
}  // namespace


#ifndef _WIN32

namespace {

//------------------------------------------------------------
//  Names: what QFile::encodeName and QFile::decodeName did
//------------------------------------------------------------

std::u16string toUtf16(const std::wstring &s) {
  std::u16string units;
  units.reserve(s.size());
  for (wchar_t c : s) {
    char32_t cp = char32_t(c);
    if (sizeof(wchar_t) == 4 && cp >= 0x10000) {
      units.push_back(char16_t(0xd800 + ((cp - 0x10000) >> 10)));
      units.push_back(char16_t(0xdc00 + ((cp - 0x10000) & 0x3ff)));
    } else
      units.push_back(char16_t(cp));
  }
  return units;
}

#ifdef __APPLE__
bool isAscii(const std::wstring &s) {
  for (wchar_t c : s)
    if (char32_t(c) >= 0x80) return false;
  return true;
}

// QString::normalized, by CoreFoundation.
std::wstring normalized(const std::wstring &s, CFStringNormalizationForm form) {
  if (isAscii(s)) return s;
  std::u16string units = toUtf16(s);
  CFMutableStringRef str = CFStringCreateMutable(kCFAllocatorDefault, 0);
  CFStringAppendCharacters(str, (const UniChar *)units.data(),
                           (CFIndex)units.size());
  CFStringNormalize(str, form);
  CFIndex n = CFStringGetLength(str);
  std::u16string out(size_t(n), u'\0');
  CFStringGetCharacters(str, CFRangeMake(0, n), (UniChar *)&out[0]);
  CFRelease(str);
  std::wstring ws;
  ws.reserve(out.size());
  for (size_t i = 0; i < out.size(); ++i) {
    char16_t u = out[i];
    if (sizeof(wchar_t) == 4 && u >= 0xd800 && u < 0xdc00 &&
        i + 1 < out.size() && out[i + 1] >= 0xdc00 && out[i + 1] < 0xe000) {
      ws.push_back(wchar_t(0x10000 + ((char32_t(u) - 0xd800) << 10) +
                           (char32_t(out[i + 1]) - 0xdc00)));
      ++i;
    } else
      ws.push_back(wchar_t(u));
  }
  return ws;
}
#endif

// QFile::encodeName(QString::fromStdWString(path)): the bytes Qt handed the
// system for a path. UTF-8, decomposed (NFD) on Apple's systems. Empty for a
// path with a NUL in it, which Qt refused.
std::string encodeName(const std::wstring &path) {
  std::wstring s = roundTripAsQString(path);
  if (s.find(L'\0') != std::wstring::npos) return std::string();
#ifdef __APPLE__
  s = normalized(s, kCFStringNormalizationFormD);
#endif
  return toUtf8AsQString(s);
}
std::string encodeName(const TFilePath &path) {
  return encodeName(path.getWideString());
}

// QFile::decodeName: UTF-8 as QString::fromUtf8 read it, composed (NFC) on
// Apple's systems.
std::wstring decodeName(const std::string &bytes) {
  std::wstring s = ::to_wstring(bytes);
#ifdef __APPLE__
  s = normalized(s, kCFStringNormalizationFormC);
#endif
  return s;
}

#ifndef __APPLE__
// Whether a name decodes as UTF-8 without a replacement character that the
// bytes did not hold themselves.
bool isDecodable(const char *name) {
  std::string bytes(name);
  size_t held = 0;
  for (size_t i = bytes.find("\xEF\xBF\xBD"); i != std::string::npos;
       i     = bytes.find("\xEF\xBF\xBD", i + 3))
    ++held;
  size_t got = 0;
  for (wchar_t c : ::to_wstring(bytes))
    if (c == 0xfffd) ++got;
  return got == held;
}
#endif

std::wstring fileNameOf(const std::wstring &path) {
  size_t i = path.find_last_of(L'/');
  return i == std::wstring::npos ? path : path.substr(i + 1);
}

// The path of QDir(path): "." for an empty one, without a trailing slash.
std::wstring qdirPath(const std::wstring &path) {
  std::wstring p = roundTripAsQString(path);
  if (p.empty()) return L".";
  if (p.size() > 1 && p.back() == L'/') p.pop_back();
  return p;
}

// QDir(dir).filePath(name)
std::wstring qdirFilePath(const std::wstring &dir, const std::wstring &name) {
  if (!name.empty() && name[0] == L'/') return name;
  std::wstring d = qdirPath(dir);
  if (name.empty()) return d;
  return d.back() == L'/' ? d + name : d + L"/" + name;
}

// QDir::cleanPath of an absolute path
std::wstring cleanAbsolutePath(const std::wstring &path) {
  std::vector<std::wstring> parts;
  size_t i = 0;
  while (i <= path.size()) {
    size_t j = path.find(L'/', i);
    if (j == std::wstring::npos) j = path.size();
    std::wstring part = path.substr(i, j - i);
    if (part == L"..") {
      if (!parts.empty()) parts.pop_back();
    } else if (!part.empty() && part != L".")
      parts.push_back(part);
    i = j + 1;
  }
  std::wstring out;
  for (const std::wstring &p : parts) out += L"/" + p;
  return out.empty() ? L"/" : out;
}

// QFileInfo::absoluteFilePath
std::wstring absoluteFilePath(const std::wstring &path) {
  std::wstring p = path;
  if (p.empty() || p[0] != L'/') {
    std::wstring cwd;
    if (char *buf = ::getcwd(nullptr, 0)) {
      cwd = decodeName(buf);
      ::free(buf);
    }
    if (p.empty() || p == L".")
      p = cwd;
    else
      p = cwd + L"/" + p;
  }
  return cleanAbsolutePath(p);
}

//------------------------------------------------------------
//  QFileInfo
//------------------------------------------------------------

#ifdef __APPLE__
// What Qt asked CoreFoundation about a file: kCFURLIsHiddenKey (hidden by its
// flags, as Finder hides it) and kCFURLIsAliasFileKey (a symbolic link or a
// Finder alias).
bool resourceFlag(const std::string &native, bool isDirectory, CFStringRef key) {
  CFStringRef path =
      CFStringCreateWithFileSystemRepresentation(kCFAllocatorDefault, native.c_str());
  if (!path) return false;
  CFURLRef url = CFURLCreateWithFileSystemPath(
      kCFAllocatorDefault, path, kCFURLPOSIXPathStyle, isDirectory);
  CFRelease(path);
  if (!url) return false;
  bool result     = false;
  CFTypeRef value = nullptr;
  if (CFURLCopyResourcePropertyForKey(url, key, &value, nullptr)) {
    result = value == kCFBooleanTrue;
    if (value) CFRelease(value);
  }
  CFRelease(url);
  return result;
}
#endif

// What QFileInfo said about the file at the given bytes: lstat(2), stat(2)
// and access(2), as QFileSystemEngine::fillMetaData asked them.
struct FileInfo {
  std::string native;
  bool lstatOk = false, exists = false, isDir = false, isFile = false;
  bool symLink  = false;  // a symbolic link, by lstat
  bool readable = false, writable = false, executable = false;
  struct stat st = {};

  explicit FileInfo(const std::string &nativePath) : native(nativePath) {
    statFully();
  }

  // An entry of a listing: what readdir(3) said of its type, which Qt took
  // without asking stat(2) (QFileSystemMetaData::fillFromDirEnt).
  FileInfo(const std::string &nativePath, unsigned char type)
      : native(nativePath) {
    if (type == DT_DIR || type == DT_REG) {
      exists = true;
      isDir  = type == DT_DIR;
      isFile = type == DT_REG;
      readable = ::access(native.c_str(), R_OK) == 0;
    } else
      statFully();
  }

  // lstat(2), stat(2) and access(2), as QFileSystemEngine::fillMetaData
  void statFully() {
    lstatOk = exists = isDir = isFile = symLink = false;
    readable = writable = executable = false;
    if (native.empty()) return;
    struct stat lst;
    lstatOk = ::lstat(native.c_str(), &lst) == 0;
    symLink = lstatOk && S_ISLNK(lst.st_mode);
    if (lstatOk && !symLink) {
      st     = lst;
      exists = true;
    } else if (symLink)
      exists = ::stat(native.c_str(), &st) == 0;
    if (exists) {
      isDir      = S_ISDIR(st.st_mode);
      isFile     = S_ISREG(st.st_mode);
      readable   = ::access(native.c_str(), R_OK) == 0;
      writable   = ::access(native.c_str(), W_OK) == 0;
      executable = ::access(native.c_str(), X_OK) == 0;
    }
  }

  // QFileInfo::isSymLink: a symbolic link or, on Apple's systems, an alias
  bool isSymLink() const {
    if (symLink) return true;
#ifdef __APPLE__
    if (exists) return resourceFlag(native, isDir, kCFURLIsAliasFileKey);
#endif
    return false;
  }

  // QFileInfo::isHidden, for a file of the given name
  bool isHidden(const std::wstring &fileName) const {
    if (!fileName.empty() && fileName[0] == L'.') return true;
#ifdef __APPLE__
    if (exists) return resourceFlag(native, isDir, kCFURLIsHiddenKey);
#endif
    return false;
  }

  // QFileInfo::permissions
  TFileStatus::Permissions permissions() const {
    if (!exists) return 0;
    TFileStatus::Permissions p = 0;
    mode_t m                   = st.st_mode;
    if (m & S_IRUSR) p |= TFileStatus::ReadOwner;
    if (m & S_IWUSR) p |= TFileStatus::WriteOwner;
    if (m & S_IXUSR) p |= TFileStatus::ExeOwner;
    if (m & S_IRGRP) p |= TFileStatus::ReadGroup;
    if (m & S_IWGRP) p |= TFileStatus::WriteGroup;
    if (m & S_IXGRP) p |= TFileStatus::ExeGroup;
    if (m & S_IROTH) p |= TFileStatus::ReadOther;
    if (m & S_IWOTH) p |= TFileStatus::WriteOther;
    if (m & S_IXOTH) p |= TFileStatus::ExeOther;
    if (readable) p |= TFileStatus::ReadUser;
    if (writable) p |= TFileStatus::WriteUser;
    if (executable) p |= TFileStatus::ExeUser;
    return p;
  }
};

// QFile::setPermissions
bool setPermissions(const std::string &native, TFileStatus::Permissions p) {
  mode_t mode = 0;
  if (p & (TFileStatus::ReadOwner | TFileStatus::ReadUser)) mode |= S_IRUSR;
  if (p & (TFileStatus::WriteOwner | TFileStatus::WriteUser)) mode |= S_IWUSR;
  if (p & (TFileStatus::ExeOwner | TFileStatus::ExeUser)) mode |= S_IXUSR;
  if (p & TFileStatus::ReadGroup) mode |= S_IRGRP;
  if (p & TFileStatus::WriteGroup) mode |= S_IWGRP;
  if (p & TFileStatus::ExeGroup) mode |= S_IXGRP;
  if (p & TFileStatus::ReadOther) mode |= S_IROTH;
  if (p & TFileStatus::WriteOther) mode |= S_IWOTH;
  if (p & TFileStatus::ExeOther) mode |= S_IXOTH;
  return !native.empty() && ::chmod(native.c_str(), mode) == 0;
}

// QFileInfo::exists (QFile::exists)
bool exists(const std::string &native) {
  struct stat st;
  return !native.empty() && ::stat(native.c_str(), &st) == 0;
}

//------------------------------------------------------------
//  QDir listings
//------------------------------------------------------------

// QDir's filters, the ones this file uses.
enum Filter {
  Dirs           = 0x001,
  Files          = 0x002,
  Readable       = 0x010,
  Hidden         = 0x100,
  AllDirs        = 0x400,
  NoDotAndDotDot = 0x6000
};

// One entry of a directory listing, as QDirIterator gave it.
struct Entry {
  std::wstring name;  // QFileInfo::fileName
  std::wstring path;  // QFileInfo::filePath
  std::string native; // the bytes the entry was read and checked through
  bool isDir;         // QFileInfo::isDir
};

// The entries of the directory at the given path (as QDir(path) has it) that
// pass QDirIteratorPrivate::matchesFilters with the given filters, in the
// order readdir(3) gives them.
std::vector<Entry> listDirectory(const std::wstring &dir, int filters) {
  std::vector<Entry> out;
  std::string nativeDir = encodeName(qdirPath(dir));
  if (nativeDir.empty()) return out;
  if (nativeDir.back() != '/') nativeDir.push_back('/');
  DIR *d = ::opendir(nativeDir.c_str());
  if (!d) return out;
  while (struct dirent *e = ::readdir(d)) {
#ifndef __APPLE__
    // QFileSystemIterator skipped the names the locale's codec (UTF-8)
    // could not decode
    if (!isDecodable(e->d_name)) continue;
#endif
    Entry entry;
    entry.native = nativeDir + e->d_name;
    entry.path   = decodeName(entry.native);
    entry.name   = fileNameOf(entry.path);

    const std::wstring &name = entry.name;
    if (name.empty()) continue;
    bool dotOrDotDot = name == L"." || name == L"..";
    if ((filters & NoDotAndDotDot) && dotOrDotDot) continue;

    FileInfo fi(entry.native, e->d_type);
    if (!(filters & Hidden) && !dotOrDotDot) {
#ifdef __APPLE__
      // Qt asked stat(2) for the flags that hide a file; when it could not
      // (a folder that may be read but not searched), it forgot what readdir
      // had said too, and the entry was then not a file
      if (name[0] != L'.') fi.statFully();
#endif
      if (fi.isHidden(name)) continue;
    }
    // system files: neither file nor directory nor link, or a broken link
    if (!(fi.isFile || fi.isDir || fi.symLink) && !fi.isSymLink()) continue;
    if (!fi.exists && fi.symLink) continue;
    if (!(filters & (Dirs | AllDirs)) && fi.isDir) continue;
    if (!(filters & Files) && fi.isFile) continue;
    if ((filters & Readable) && !fi.readable) continue;
    entry.isDir = fi.isDir;
    out.push_back(entry);
  }
  ::closedir(d);
  return out;
}

// QString::compare of two names lowered by QString::toLower: by UTF-16
// code units.
bool lessIgnoringCase(const std::wstring &a, const std::wstring &b) {
  return toUtf16(toLowerAsQString(a)) < toUtf16(toLowerAsQString(b));
}

// QDir's default sort, QDir::Name | QDir::IgnoreCase, done the way Qt did it:
// std::sort over the entries in readdir order.
void sortIgnoringCase(std::vector<Entry> &v) {
  std::sort(v.begin(), v.end(), [](const Entry &a, const Entry &b) {
    return lessIgnoringCase(a.name, b.name);
  });
}

// QDir::Name | QDir::LocaleAware: QString::localeAwareCompare of the names,
// which is also the order of frame letters.
void sortLocaleAware(std::vector<Entry> &v) {
  std::sort(v.begin(), v.end(), [](const Entry &a, const Entry &b) {
    return TFrameId::compareLetters(a.name, b.name) < 0;
  });
}

// QDir(dir).entryInfoList(): files and directories, not hidden ones, "." and
// ".." included, sorted by name ignoring case.
std::vector<Entry> entryInfoList(const std::wstring &dir) {
  std::vector<Entry> v = listDirectory(dir, Dirs | Files);
  sortIgnoringCase(v);
  return v;
}

//------------------------------------------------------------
//  QFile and QDir operations
//------------------------------------------------------------

// QFile::remove
bool removeFile(const std::string &native) {
  return !native.empty() && ::unlink(native.c_str()) == 0;
}

// QFileSystemEngine::renameFile: a rename that never replaces a file.
bool renameNoReplace(const std::string &src, const std::string &dst) {
  if (src.empty() || dst.empty()) return false;
#if defined(__APPLE__)
  if (::renameatx_np(AT_FDCWD, src.c_str(), AT_FDCWD, dst.c_str(),
                     RENAME_EXCL) == 0)
    return true;
  if (errno != ENOTSUP) return false;
#elif defined(__linux__) && defined(RENAME_NOREPLACE)
  if (::renameat2(AT_FDCWD, src.c_str(), AT_FDCWD, dst.c_str(),
                  RENAME_NOREPLACE) == 0)
    return true;
  if (errno != EINVAL) return false;
#endif
  if (::link(src.c_str(), dst.c_str()) == 0) {
    if (::unlink(src.c_str()) == 0) return true;
    int saved = errno;
    ::unlink(dst.c_str());
    errno = saved;
    return false;
  }
  switch (errno) {
  case EACCES:
  case EEXIST:
  case ENAMETOOLONG:
  case ENOENT:
  case ENOTDIR:
  case EROFS:
  case EXDEV:
    return false;
  default:
    return ::rename(src.c_str(), dst.c_str()) == 0;
  }
}

// Copies the bytes of one open file into another, 4096 at a time; the number
// of bytes read, or -1.
long long copyBytes(int in, int out) {
  char block[4096];
  long long total = 0;
  for (;;) {
    ssize_t n = ::read(in, block, sizeof block);
    if (n < 0) return -1;
    if (n == 0) return total;
    total += n;
    if (::write(out, block, size_t(n)) != n) return -1;
  }
}

// open(2) for reading, as QFile::open(QIODevice::ReadOnly): not a directory
int openForReading(const std::string &native) {
  int fd = ::open(native.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  struct stat st;
  if (::fstat(fd, &st) != 0 || S_ISDIR(st.st_mode)) {
    ::close(fd);
    return -1;
  }
  return fd;
}

// QFile::copy(src, dst): refuses an existing destination; on Apple's systems
// clonefile(2) first; otherwise copies through a temporary file next to the
// destination, renamed into place, and gives it the source's permissions.
bool copyFileAsQt(const std::wstring &src, const std::wstring &dst) {
  const std::string nSrc = encodeName(src), nDst = encodeName(dst);
  if (roundTripAsQString(src).empty()) return false;
  if (exists(nDst)) return false;
  if (nSrc.empty() || nDst.empty()) return false;
#ifdef __APPLE__
  if (::clonefile(nSrc.c_str(), nDst.c_str(), 0) == 0) return true;
#endif
  int in = openForReading(nSrc);
  if (in < 0) return false;
  struct stat sst;
  if (::fstat(in, &sst) != 0) {
    ::close(in);
    return false;
  }
  // QFileInfo(dst).path(): its folder, "." when it has none
  std::wstring dstPath = roundTripAsQString(dst);
  size_t slash         = dstPath.find_last_of(L'/');
  std::wstring folder  = slash == std::wstring::npos ? L"."
                         : slash == 0               ? L"/"
                                                    : dstPath.substr(0, slash);
  std::string tmpl = encodeName(folder);
  if (tmpl.empty() || tmpl.back() != '/') tmpl.push_back('/');
  tmpl += "qt_temp.XXXXXX";
  std::vector<char> tmpName(tmpl.begin(), tmpl.end());
  tmpName.push_back('\0');
  int out = ::mkstemp(tmpName.data());
  if (out < 0) {
    ::close(in);
    return false;
  }
  long long total = copyBytes(in, out);
  ::close(in);
  ::close(out);
  bool ok = total == (long long)sst.st_size &&
            renameNoReplace(tmpName.data(), nDst);
  if (!ok) {
    ::unlink(tmpName.data());
    return false;
  }
  setPermissions(nDst, FileInfo(nSrc).permissions());
  return true;
}

// QFile::rename(src, dst): refuses a missing source (a broken link is not
// missing) and an existing destination, unless that is the same file under a
// name that differs in case only; falls back to copying a regular file and
// removing it.
bool renameFileAsQt(const std::wstring &src, const std::wstring &dst) {
  const std::wstring qSrc = roundTripAsQString(src), qDst = roundTripAsQString(dst);
  if (qSrc.empty()) return false;
  if (qSrc == qDst) return false;
  const std::string nSrc = encodeName(qSrc), nDst = encodeName(qDst);
  // QFile::exists() (the member, unlike the static one) counts a symbolic
  // link as existing even when what it points to is gone
  struct stat l, a, b;
  if (nSrc.empty() || ::lstat(nSrc.c_str(), &l) != 0) return false;
  const bool srcStat = ::stat(nSrc.c_str(), &a) == 0;

  bool changingCase = false;
  if (!nDst.empty() && ::stat(nDst.c_str(), &b) == 0) {
    changingCase = srcStat && a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
                   equalsIgnoringCaseAsQString(qSrc, qDst);
    if (!changingCase) return false;
  }

#ifdef __linux__
  // As Qt does on Linux, where rename(2) between two names of one file does
  // nothing: through a temporary name.
  if (changingCase) {
    for (int attempt = 0; attempt < 16; ++attempt) {
      static const char chars[] =
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
      std::string tmp = nSrc + ".";
      for (int i = 0; i < 6; ++i) tmp.push_back(chars[std::rand() % 62]);
      if (!renameNoReplace(nSrc, tmp)) continue;
      if (renameNoReplace(tmp, nDst)) return true;
      renameNoReplace(tmp, nSrc);
      return false;
    }
    return false;
  }
#endif

  if (changingCase ? ::rename(nSrc.c_str(), nDst.c_str()) == 0
                   : renameNoReplace(nSrc, nDst))
    return true;

  // the block copy: regular files only ("sequential" ones are refused)
  if (!srcStat || !S_ISREG(a.st_mode)) return false;
  int in = openForReading(nSrc);
  if (in < 0) return false;
  int out = ::open(nDst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if (out < 0) {
    ::close(in);
    return false;
  }
  bool ok = copyBytes(in, out) >= 0;
  ::close(out);
  ::close(in);
  if (ok && !removeFile(nSrc)) ok = false;
  if (!ok) removeFile(nDst);
  return ok;
}

// QDir::mkpath: every missing folder, mode 0777.
bool makePathParents(const std::string &native) {
  if (::mkdir(native.c_str(), 0777) == 0) return true;
  struct stat st;
  if (errno == EEXIST)
    return ::stat(native.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
  if (errno != ENOENT) return false;
  size_t slash = native.find_last_of('/');
  if (slash == std::string::npos || slash < 1) return false;
  if (!makePathParents(native.substr(0, slash))) return false;
  if (::mkdir(native.c_str(), 0777) == 0) return true;
  return errno == EEXIST && ::stat(native.c_str(), &st) == 0 &&
         S_ISDIR(st.st_mode);
}

bool makePath(const std::wstring &path) {
  std::wstring p = roundTripAsQString(path);
  if (p.empty()) return false;
  // QDir::current().filePath(p): the same folder as p from here
  std::wstring dirName = p;
  while (dirName.size() > 1 && dirName.back() == L'/') dirName.pop_back();
  std::string native = encodeName(dirName);
  if (native.empty()) return false;
  return makePathParents(native);
}

// QDir::mkdir: one folder, mode 0777.
bool makeDir(const std::wstring &path) {
  std::wstring p = roundTripAsQString(path);
  if (p.empty()) return false;
  while (p.size() > 1 && p.back() == L'/') p.pop_back();
  std::string native = encodeName(p);
  return !native.empty() && ::mkdir(native.c_str(), 0777) == 0;
}

// QDir::rmdir
bool removeDir(const std::wstring &path) {
  std::string native = encodeName(path);
  return !native.empty() && ::rmdir(native.c_str()) == 0;
}

}  // namespace

//-----------------------------------------------------------------------------------

TFileStatus::TFileStatus(const TFilePath &path)
    : m_path(path.getWideString()) {
  FileInfo fi(encodeName(m_path));
  m_exist       = fi.exists;
  m_isDirectory = fi.isDir;
  m_isLink      = fi.symLink;
  m_size        = fi.exists ? (TINT64)fi.st.st_size : 0;
  m_permissions = fi.permissions();
}

bool TFileStatus::isLink() const {
  if (m_isLink) return true;
#ifdef __APPLE__
  // an alias, which Qt counted as a link too
  if (m_exist)
    return resourceFlag(encodeName(m_path), m_isDirectory,
                        kCFURLIsAliasFileKey);
#endif
  return false;
}

//------------------------------------------------------------

namespace {
TFilePathSet getPathsToCreate(const TFilePath &path) {
  TFilePathSet pathList;
  if (path.isEmpty()) return pathList;
  TFilePath parentDir = path;
  while (!TFileStatus(parentDir).doesExist()) {
    if (parentDir == parentDir.getParentDir()) return TFilePathSet();
    pathList.push_back(parentDir);
    parentDir = parentDir.getParentDir();
  }
  return pathList;
}
}  // namespace

// handle exception
void TSystem::mkDir(const TFilePath &path) {
  TFilePathSet pathSet = getPathsToCreate(path);
  std::wstring qPath   = roundTripAsQString(path.getWideString());

  if (qPath.find(L'+') != std::wstring::npos) {
    throw TSystemException(path, "Invalid character '+' in path!");
  }

  if (!makePath(qPath)) throw TSystemException(path, "can't create folder!");

  // QFile::setPermissions with every user, group and other bit
  for (const auto &p : pathSet)
    setPermissions(encodeName(p),
                   TFileStatus::ReadUser | TFileStatus::WriteUser |
                       TFileStatus::ExeUser | TFileStatus::ReadGroup |
                       TFileStatus::WriteGroup | TFileStatus::ExeGroup |
                       TFileStatus::ReadOther | TFileStatus::WriteOther |
                       TFileStatus::ExeOther);
}

//------------------------------------------------------------
// handle exception
void TSystem::rmDir(const TFilePath &path) {
  // QDir(parent).rmdir(getName()): the name without its type, as it always was
  const std::wstring name = ::to_wstring(path.getName());
  if (name.empty() ||
      !removeDir(qdirFilePath(path.getParentDir().getWideString(), name)))
    throw TSystemException(path, "can't remove folder!");
}

//------------------------------------------------------------

namespace {
void rmDirTree(const std::wstring &path) {
  for (const Entry &fi : entryInfoList(path)) {
    if (fi.name == L"." || fi.name == L"..") continue;
    std::wstring son = absoluteFilePath(fi.path);
    FileInfo sonInfo(encodeName(son));
    if (sonInfo.isDir)
      rmDirTree(son);
    else if (sonInfo.isFile)
      if (!removeFile(encodeName(son)))
        throw TSystemException("can't remove file" + ::to_string(son));
  }
  if (!removeDir(path)) throw TSystemException("can't remove path!");
}
}  // namespace

void TSystem::rmDirTree(const TFilePath &path) {
  ::rmDirTree(roundTripAsQString(path.getWideString()));
}

//------------------------------------------------------------

void TSystem::copyDir(const TFilePath &dst, const TFilePath &src) {
  std::vector<Entry> fil = entryInfoList(src.getWideString());

  makeDir(dst.getWideString());

  for (const Entry &fi : fil) {
    if (fi.name == L"." || fi.name == L"..") continue;
    if (fi.isDir) {
      TFilePath srcDir = TFilePath(fi.path);
      TFilePath dstDir = dst + srcDir.getName();
      copyDir(dstDir, srcDir);
    } else {
      TFilePath srcFi = dst + TFilePath(fi.name);
      copyFileAsQt(fi.path, srcFi.getWideString());
    }
  }
}

//------------------------------------------------------------

void TSystem::copyFile(const TFilePath &dst, const TFilePath &src,
                       bool overwrite) {
  if (dst.isEmpty()) {
    throw TSystemException(dst, "Destination path is empty!");
  }

  if (dst == src) return;

  // Create the containing folder before trying to copy or it will crash!
  touchParentDir(dst);

  const std::string nDst = encodeName(dst);
  if (overwrite && exists(nDst)) {
    if (!removeFile(nDst))
      throw TSystemException(dst, "can't remove existing file!");
  }

  if (!copyFileAsQt(src.getWideString(), dst.getWideString()))
    throw TSystemException(dst, "can't copy file!");
}

//------------------------------------------------------------

void TSystem::renameFile(const TFilePath &dst, const TFilePath &src,
                         bool overwrite) {
  if (dst.isEmpty()) {
    throw TSystemException(dst, "Destination path is empty!");
  }

  if (dst == src) return;

  const std::string nDst = encodeName(dst);
  if (overwrite && exists(nDst)) {
    if (!removeFile(nDst))
      throw TSystemException(dst, "can't remove existing file!");
  }

  if (!renameFileAsQt(src.getWideString(), dst.getWideString()))
    throw TSystemException(dst, "can't rename file!");
}

//------------------------------------------------------------

void TSystem::replaceFile(const TFilePath &dst, const TFilePath &src) {
  if (dst.isEmpty())
    throw TSystemException(dst, "Destination path is empty!");

  if (dst == src) return;

  const std::string srcPath = encodeName(src);
  const std::string dstPath = encodeName(dst);
  if (std::rename(srcPath.c_str(), dstPath.c_str()) != 0)
    throw TSystemException(dst, "can't replace file!");
}

//------------------------------------------------------------

// handle errors with GetLastError?
void TSystem::deleteFile(const TFilePath &fp) {
  if (!removeFile(encodeName(fp)))
    throw TSystemException(fp, "can't delete file!");
}

//------------------------------------------------------------

void TSystem::hideFile(const TFilePath &fp) {
  // MACOSX, and others
  TSystem::renameFile(TFilePath(fp.getParentDir() + L"." + fp.getLevelNameW()),
                      fp);
}

//------------------------------------------------------------
// return the folder item list which is readable and executable
// (returns only names, not full path)
void TSystem::readDirectory_DirItems(std::vector<std::wstring> &dst,
                                     const TFilePath &path) {
  if (!TFileStatus(path).isDirectory())
    throw TSystemException(path, " is not a directory");

  // QDir::entryList(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Readable,
  //                 QDir::Name | QDir::LocaleAware)
  std::vector<Entry> v =
      listDirectory(path.getWideString(), AllDirs | NoDotAndDotDot | Readable);
  sortLocaleAware(v);
  for (const Entry &e : v) dst.push_back(e.name);
}

//------------------------------------------------------------
/*! to retrieve the both lists with groupFrames option = on and off.
 */
void TSystem::readDirectory(TFilePathSet &groupFpSet, TFilePathSet &allFpSet,
                            const TFilePath &path) {
  if (!TFileStatus(path).isDirectory())
    throw TSystemException(path, " is not a directory");

  std::set<TFilePath, CaselessFilepathLess> fileSet_group;
  std::set<TFilePath, CaselessFilepathLess> fileSet_all;

  // QDir::entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::Readable); its
  // order does not matter, the sets sort
  std::vector<Entry> fil =
      listDirectory(path.getWideString(), Files | NoDotAndDotDot | Readable);

  if (fil.empty()) return;

  for (const Entry &fi : fil) {
    TFilePath son = path + TFilePath(fi.name);

    // store all file paths
    fileSet_all.insert(son);

    // in case of the sequential files
    if (son.getDots() == "..") son = son.withFrame();

    // store the group. insertion avoids duplication of the item
    fileSet_group.insert(son);
  }

  groupFpSet.insert(groupFpSet.end(), fileSet_group.begin(),
                    fileSet_group.end());
  allFpSet.insert(allFpSet.end(), fileSet_all.begin(), fileSet_all.end());
}

//------------------------------------------------------------

void TSystem::readDirectory(TFilePathSet &dst, const TFilePath &path,
                            bool groupFrames, bool onlyFiles,
                            bool getHiddenFiles) {
  // QDir(path) with the filters Files, Dirs, Hidden, as readDirectory(QDir)
  // read it
  const std::wstring dirPath = qdirPath(path.getWideString());
  FileInfo dirInfo(encodeName(dirPath));
  if (!(dirInfo.exists && dirInfo.isDir))
    throw TSystemException(TFilePath(dirPath), " is not a directory");

  int filters = Files | NoDotAndDotDot;
  if (!onlyFiles) filters |= Dirs;
  if (getHiddenFiles) filters |= Hidden;

  TFilePath tDirPath(dirPath);
  std::set<TFilePath, CaselessFilepathLess> fpSet;

  for (const Entry &entry : listDirectory(dirPath, filters)) {
    TFilePath fp(tDirPath + TFilePath(entry.name));

    if (groupFrames && fp.getDots() == "..") fp = fp.withFrame();

    fpSet.insert(fp);
  }

  dst.insert(dst.end(), fpSet.begin(), fpSet.end());
}

//------------------------------------------------------------

void TSystem::readDirectoryTree(TFilePathSet &dst, const TFilePath &path,
                                bool groupFrames, bool onlyFiles) {
  if (!TFileStatus(path).isDirectory())
    throw TSystemException(path, " is not a directory");

  std::set<TFilePath, CaselessFilepathLess> fpSet;

  for (const Entry &fi : entryInfoList(path.getWideString())) {
    if (fi.name == L"." || fi.name == L"..") continue;
    TFilePath son = TFilePath(fi.path);
    if (TFileStatus(son).isDirectory()) {
      if (!onlyFiles) dst.push_back(son);
      readDirectoryTree(dst, son, groupFrames, onlyFiles);
    } else {
      if (groupFrames && son.getDots() == "..") {
        son = son.withFrame();
      }
      fpSet.insert(son);
    }
  }

  dst.insert(dst.end(), fpSet.begin(), fpSet.end());
}

//------------------------------------------------------------

TFilePathSet TSystem::getDisks() {
  // QDir::drives(): the root, on these systems
  TFilePathSet filePathSet;
  filePathSet.push_back(TFilePath("/"));
  return filePathSet;
}

#endif  // !_WIN32

//------------------------------------------------------------

TINT64 TFileStatus::getSize() const { return m_exist ? m_size : 0; }
TFileStatus::Permissions TFileStatus::getPermissions() const {
  return m_exist ? m_permissions : 0;
}
bool TFileStatus::isDirectory() const { return m_exist && m_isDirectory; }

//------------------------------------------------------------
/*! return the folder path list which is readable and executable
 */
void TSystem::readDirectory_Dir_ReadExe(TFilePathSet &dst,
                                        const TFilePath &path) {
  std::vector<std::wstring> dirItems;
  readDirectory_DirItems(dirItems, path);

  for (const std::wstring &item : dirItems) {
    TFilePath son = path + TFilePath(item);
    dst.push_back(son);
  }
}

//------------------------------------------------------------

void TSystem::readDirectory(TFilePathSet &dst, const TFilePathSet &pathSet,
                            bool groupFrames, bool onlyFiles,
                            bool getHiddenFiles) {
  for (const auto &path : pathSet)
    readDirectory(dst, path, groupFrames, onlyFiles, getHiddenFiles);
}

//------------------------------------------------------------

TFilePathSet TSystem::readDirectory(const TFilePath &path, bool groupFrames,
                                    bool onlyFiles, bool getHiddenFiles) {
  TFilePathSet filePathSet;
  readDirectory(filePathSet, path, groupFrames, onlyFiles, getHiddenFiles);
  return filePathSet;
}

//------------------------------------------------------------

TFilePathSet TSystem::readDirectory(const TFilePathSet &pathSet,
                                    bool groupFrames, bool onlyFiles,
                                    bool getHiddenFiles) {
  TFilePathSet dst;
  readDirectory(dst, pathSet, groupFrames, onlyFiles, getHiddenFiles);
  return dst;
}

//------------------------------------------------------------

void TSystem::readDirectoryTree(TFilePathSet &dst, const TFilePathSet &pathSet,
                                bool groupFrames, bool onlyFiles) {
  for (const auto &path : pathSet)
    readDirectoryTree(dst, path, groupFrames, onlyFiles);
}

//------------------------------------------------------------

TFilePathSet TSystem::readDirectoryTree(const TFilePath &path, bool groupFrames,
                                        bool onlyFiles) {
  TFilePathSet dst;
  readDirectoryTree(dst, path, groupFrames, onlyFiles);
  return dst;
}

//------------------------------------------------------------

TFilePathSet TSystem::readDirectoryTree(const TFilePathSet &pathSet,
                                        bool groupFrames, bool onlyFiles) {
  TFilePathSet dst;
  readDirectoryTree(dst, pathSet, groupFrames, onlyFiles);
  return dst;
}

//------------------------------------------------------------

TFilePathSet TSystem::packLevelNames(const TFilePathSet &fps) {
  std::set<TFilePath> tmpSet;
  for (const auto &fp : fps)
    tmpSet.insert(fp.getParentDir() + fp.getLevelName());

  TFilePathSet fps2;
  for (const auto &fp : tmpSet) {
    fps2.push_back(fp);
  }
  return fps2;
}

//------------------------------------------------------------

bool TSystem::doesExistFileOrLevel(const TFilePath &fp) {
  if (TFileStatus(fp).doesExist()) return true;

  if (fp.isLevelName()) {
    const TFilePath &parentDir = fp.getParentDir();
    if (!TFileStatus(parentDir).doesExist()) return false;

    TFilePathSet files;
    try {
      files = TSystem::readDirectory(parentDir, false, true, true);
    } catch (...) {
      return false;
    }

    for (const auto &file : files) {
      if (file.getLevelNameW() == fp.getLevelNameW()) return true;
    }
  } else if (fp.getType() == "psd") {
    // was a QString: the same characters and indices
    std::wstring name = roundTripAsQString(fp.getWideName());
    name.append(::to_wstring(fp.getDottedType()));

    size_t sepPos = name.find(L"#");
    if (sepPos != std::wstring::npos) {
      size_t dotPos              = name.find(L".", sepPos);
      size_t doubleUnderscorePos = name.find(L"__", sepPos);
      int dot   = dotPos == std::wstring::npos ? -1 : int(dotPos);
      int under = doubleUnderscorePos == std::wstring::npos
                      ? -1
                      : int(doubleUnderscorePos);
      int sep   = int(sepPos);

      int removeChars = 0;
      if (dot > sep) {
        removeChars = dot - sep;
      }
      if (under > 0 && under < dot) {
        removeChars = under - sep;
      }

      if (removeChars > 0) {
        name.erase(sepPos, removeChars);
      }
    }

    TFilePath psdpath(fp.getParentDir() + TFilePath(name));
    if (TFileStatus(psdpath).doesExist()) return true;
  }

  return false;
}

//--------------------------------------------------------------

void TSystem::copyFileOrLevel_throw(const TFilePath &dst,
                                    const TFilePath &src) {
  if (src.isLevelName()) {
    TFilePathSet files;
    files = TSystem::readDirectory(src.getParentDir(), false);

    for (const auto &file : files) {
      if (file.getLevelNameW() == src.getLevelNameW()) {
        TFilePath src1 = file;
        TFilePath dst1 = dst.withFrame(file.getFrame());

        TSystem::copyFile(dst1, src1);
      }
    }
  } else {
    TSystem::copyFile(dst, src);
  }
}

//--------------------------------------------------------------

bool TSystem::renameImageSequence(const TFilePathSet &files,
                                  const TFilePath &levelPath,
                                  int prefixLength) {
  std::string levelBaseName = levelPath.withoutParentDir().getName();
  if (files.empty() || !files.begin()->isAbsolute() ||
      !levelPath.isAbsolute()) {
    return false;
  }

  std::wstring wstr;
  TFilePath dst;

  for (const TFilePath &file : files) {
    wstr = file.getWideName();
    if (wstr.size() > static_cast<size_t>(prefixLength))
      wstr = wstr.substr(prefixLength);
    else
      wstr.clear();

    dst = file.getParentDir() + (levelBaseName + "." + TFrameId(wstr).expand() +
                                 file.getDottedType());
    try {
      TSystem::renameFile(dst, file, false);
    } catch (...) {
      return false;
    }
  }
  return true;
}

void TSystem::renameFileOrLevel_throw(const TFilePath &dst,
                                      const TFilePath &src,
                                      bool renamePalette) {
  if (renamePalette && ((src.getType() == "tlv") || (src.getType() == "tzp") ||
                        (src.getType() == "tzu"))) {
    // Special case: since renames cannot be 'grouped' in the UI, palettes are
    // automatically renamed here if required
    const char *type = (src.getType() == "tlv") ? "tpl" : "plt";

    TFilePath srcpltname(src.withNoFrame().withType(type));
    TFilePath dstpltname(dst.withNoFrame().withType(type));

    if (TSystem::doesExistFileOrLevel(src) &&
        TSystem::doesExistFileOrLevel(srcpltname))
      TSystem::renameFile(dstpltname, srcpltname, false);
  }

  if (src.isLevelName()) {
    TFilePathSet files;
    files = TSystem::readDirectory(src.getParentDir(), false);

    for (const auto &file : files) {
      if (file.getLevelName() == src.getLevelName()) {
        TFilePath src1 = file;
        TFilePath dst1 = dst.withFrame(file.getFrame());

        TSystem::renameFile(dst1, src1);
      }
    }
  } else {
    TSystem::renameFile(dst, src);
  }
}

//--------------------------------------------------------------

void TSystem::removeFileOrLevel_throw(const TFilePath &fp) {
  if (fp.isLevelName()) {
    TFilePathSet files;
    files = TSystem::readDirectory(fp.getParentDir(), false, true, true);

    for (const auto &file : files) {
      if (file.getLevelName() == fp.getLevelName()) TSystem::deleteFile(file);
    }
  } else {
    TSystem::deleteFile(fp);
  }
}

//--------------------------------------------------------------

void TSystem::hideFileOrLevel_throw(const TFilePath &fp) {
  if (fp.isLevelName()) {
    TFilePathSet files;
    files = TSystem::readDirectory(fp.getParentDir(), false);

    for (const auto &file : files) {
      if (file.getLevelNameW() == fp.getLevelNameW()) TSystem::hideFile(file);
    }
  } else {
    TSystem::hideFile(fp);
  }
}

//--------------------------------------------------------------

// moveFileOrLevelToRecycleBin_throw is in tsystempd.cpp, with
// moveFileToRecycleBin.

//--------------------------------------------------------------

bool TSystem::copyFileOrLevel(const TFilePath &dst, const TFilePath &src) {
  try {
    copyFileOrLevel_throw(dst, src);
  } catch (...) {
    return false;
  }
  return true;
}

//--------------------------------------------------------------

bool TSystem::renameFileOrLevel(const TFilePath &dst, const TFilePath &src,
                                bool renamePalette) {
  try {
    renameFileOrLevel_throw(dst, src, renamePalette);
  } catch (...) {
    return false;
  }
  return true;
}

//--------------------------------------------------------------

bool TSystem::removeFileOrLevel(const TFilePath &fp) {
  try {
    removeFileOrLevel_throw(fp);
  } catch (...) {
    return false;
  }
  return true;
}

//--------------------------------------------------------------

bool TSystem::hideFileOrLevel(const TFilePath &fp) {
  try {
    hideFileOrLevel_throw(fp);
  } catch (...) {
    return false;
  }
  return true;
}

//--------------------------------------------------------------

// moveFileOrLevelToRecycleBin is in tsystempd.cpp.

//--------------------------------------------------------------

bool TSystem::touchParentDir(const TFilePath &fp) {
  TFilePath parentDir = fp.getParentDir();
  TFileStatus fs(parentDir);
  if (fs.isDirectory())
    return true;
  else if (fs.doesExist())
    return false;
  try {
    mkDir(parentDir);
  } catch (...) {
    return false;
  }
  return true;
}

//--------------------------------------------------------------

// showDocument, sleep, getProcessorCount, isDLLBlackListed, getHostName,
// getUserName, getTempDir, getSystemValue, getBinDir and getUniqueFile are in
// tsystem_qt.cpp until step 6c.

//--------------------------------------------------------------

TString TSystemException::getMessage() const {
  std::wstring msg;
  switch (m_err) {
  case -1:
    msg = m_msg;
    break;  // // nothing
  case EEXIST:
    msg =
        L": Directory was not created because filename is the name of an "
        L"existing file, directory, or device";
    break;
  case ENOENT:
    msg =
        L": Path was not found, or the named file does not exist or is a null "
        L"pathname.";
    break;
  case ENOTEMPTY:
    msg =
        L": Given path is not a directory; directory is not empty; or "
        L"directory is either current working directory or root directory";
    break;
  case EACCES:
    msg =
        L": Search permission is denied by a component of the path prefix, or "
        L"write permission on the file named by path is denied, or times is "
        L"NULL, and write access is denied";
    break;
  case EFAULT:
    msg =
        L": Times is not NULL and, or points outside the process's allocated "
        L"address space.";
    break;
  case EINTR:
    msg = L": A signal was caught during the utime system call.";
    break;
  case ENAMETOOLONG:
    msg =
        L": The length of the path argument exceeds {PATH_MAX}, or the length "
        L"of a path component exceeds {NAME_MAX} while _POSIX_NO_TRUNC is in "
        L"effect.";
    break;
  case ENOTDIR:
    msg = L": A component of the path prefix is not a directory.";
    break;
  case EPERM:
    msg =
        L": The calling process does not have the super-user privilege, the "
        L"effective user ID is not the owner of the file, and times is not "
        L"NULL, or the file system containing the file is mounted read-only";
    break;
  case EROFS:
    msg =
        L": The current file system level range does not envelop the level of "
        L"the file named by path, and the calling process does not have the "
        L"super-user privilege.";
    break;
  case ENOSYS:
    msg =
        L": When the named file cannot have its time reset.  The file is on a "
        L"file system that doesn't have this operation.";
    break;
  case EMFILE:
    msg = L": The maximum number of file descriptors are currently open.";
    break;
  case ENFILE:
    msg = L": The system file table is full.";
    break;
  case EBADF:
    msg =
        L": The file descriptor determined by the DIR stream is no longer "
        L"valid.  This result occurs if the DIR stream has been closed.";
    break;
  case EINVAL:
    msg = L": 64-bit and non-64-bit calls were mixed in a sequence of calls.";
    break;
  default:
    msg = L": Unknown error";
    break;
#ifndef _WIN32
  case ELOOP:
    msg = L": Too many symbolic links were encountered in translating path.";
    break;
#ifndef MACOSX
  case EMULTIHOP:
    msg =
        L": Components of path require hopping to multiple remote machines and "
        L"the file system does not allow it.";
    break;
  case ENOLINK:
    msg =
        L": Path points to a remote machine and the link to that machine is no "
        L"longer active.";
    break;
#endif
#if defined(__sgi)
  case EDIRCORRUPTED:
    msg = L": The directory is corrupted on disk.";
    break;
#endif
  case EOVERFLOW:
    msg =
        L": One of the inode number values or offset values did not fit in 32 "
        L"bits, and the 64-bit interfaces were not used.";
    break;
#endif
  }
  return m_fname.getWideString() + L"\n" + msg;
}

//--------------------------------------------------------------

TSystemException::TSystemException(const TFilePath &fname, int err)
    : m_fname(fname)
    , m_err(err)
    , m_msg(L"")

{}

//--------------------------------------------------------------

TSystemException::TSystemException(const TFilePath &fname,
                                   const std::string &msg)
    : m_fname(fname), m_err(-1), m_msg(::to_wstring(msg)) {}
//--------------------------------------------------------------

TSystemException::TSystemException(const TFilePath &fname,
                                   const std::wstring &msg)
    : m_fname(fname), m_err(-1), m_msg(msg) {}

//--------------------------------------------------------------

TSystemException::TSystemException(const std::string &msg)
    : m_fname(""), m_err(-1), m_msg(::to_wstring(msg)) {}
//--------------------------------------------------------------

TSystemException::TSystemException(const std::wstring &msg)
    : m_fname(""), m_err(-1), m_msg(msg) {}
