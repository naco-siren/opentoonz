

#ifdef _WIN32
// #define UNICODE  // for UNC conversion functions
#include <windows.h>
#include <lm.h>

const char slash        = '\\';
const char auxslash     = '/';
const wchar_t wslash    = L'\\';
const wchar_t wauxslash = L'/';
#else
const char slash        = '/';
const char auxslash     = '\\';
const wchar_t wslash    = L'/';
const wchar_t wauxslash = L'\\';
#endif

//=============================================================================

#include "tfilepath.h"
#include "tconvert.h"
#include "tfiletype.h"
#include <cmath>
#include <cctype>
#include <sstream>

#include <climits>
#include <cstdio>
#include <cwchar>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

// Declared in tconvert.h; defined here so that tstring.cpp needs no TFilePath.
std::string to_string(const TFilePath &fp) {
  return ::to_string(fp.getWideString());
}

bool TFilePath::m_underscoreFormatAllowed  = true;
bool TFilePath::m_noSeparatorFormatAllowed = false;

// specifies file path condition for sequential image for each project.
// See filepathproperties.h
bool TFilePath::m_useStandard             = true;
bool TFilePath::m_acceptNonAlphabetSuffix = false;
int TFilePath::m_letterCountForSuffix     = 1;

namespace {

// Returns true if the string between the fromSeg position and the toSeg
// position is "4 digits".
bool isNumbers(std::wstring str, int fromSeg, int toSeg) {
  /*
    if (toSeg - fromSeg != 5) return false;
    for (int pos = fromSeg + 1; pos < toSeg; pos++) {
      if (str[pos] < '0' || str[pos] > '9') return false;
    }
  */
  // Let's check if it follows the format ####A (i.e 00001 or 00001a)
  int numDigits = 0, numLetters = 0;
  for (int pos = fromSeg + 1; pos < toSeg; pos++) {
    if ((str[pos] >= L'A' && str[pos] <= L'Z') ||
        (str[pos] >= L'a' && str[pos] <= L'z')) {
      // Not the right format if we ran into a letter without first finding a
      // number
      if (!numDigits) return false;

      // We'll keep track of the number of letters we find.
      // NOTE: From here on out we should only see letters
      numLetters++;
    } else if (str[pos] >= L'0' && str[pos] <= L'9') {
      // Not the right format if we ran into a number that followed a letter.
      // This format is not something we expect currently
      if (numLetters) return false;  // not right format

      // We'll keep track of the number of digits we find.
      numDigits++;
    } else  // Not the right format if we found something we didn't expect
      return false;
  }

  // Not the right format if we see too many letters. At the time of this
  // logic, we only expect 1 letter.  Can expand to 2 or more later.

  if (numLetters > 1) return false;

  return true;  // We're good!
}

// The type as QString::fromStdString(type).toLower() had it: lowercased by
// Unicode's rules (std toLower() only knows ASCII).
std::string lowerType(const std::string &type) {
  return toUtf8AsQString(toLowerAsQString(::to_wstring(type)));
}

bool checkForSeqNum(const std::string &type) {
  TFileType::Type typeInfo = TFileType::getInfoFromExtension(type);
  if ((typeInfo & TFileType::IMAGE) && !(typeInfo & TFileType::LEVEL))
    return true;
  else
    return false;
}

bool checkForSeqNum(const std::wstring &extension) {
  return checkForSeqNum(toUtf8AsQString(extension));
}

bool parseFrame(const std::wstring &str, int &frame, std::wstring &letter,
                int &padding) {
  if (str.empty()) return false;

  int i = 0, number = 0;
  while (i < (int)str.size() && str[i] >= L'0' && str[i] <= L'9')
    number = number * 10 + str[i++] - L'0';
  int digits = i;
  wchar_t l  = str[i] >= L'a' && str[i] <= L'z'   ? str[i++]
               : str[i] >= L'A' && str[i] <= L'Z' ? str[i++]
                                                  : L'\0';
  if (digits <= 0 || i < (int)str.size()) return false;

  frame   = number;
  letter  = l ? std::wstring(1, l) : std::wstring();
  padding = str[0] == L'0' ? digits : 0;
  return true;
}

};  // namespace

TFrameId::TFrameId(const std::string &str, char s)
    : m_frame(EMPTY_FRAME), m_letter(), m_zeroPadding(4), m_startSeqInd(s) {
  if (str.empty()) return;
  if (!parseFrame(to_wstring(str), m_frame, m_letter, m_zeroPadding))
    m_frame = NO_FRAME;
}

TFrameId::TFrameId(const std::wstring &str, char s)
    : m_frame(EMPTY_FRAME), m_letter(), m_zeroPadding(4), m_startSeqInd(s) {
  if (str.empty()) return;
  if (!parseFrame(str, m_frame, m_letter, m_zeroPadding)) m_frame = NO_FRAME;
}

// The order of frame letters ("pippo.0001a.tzp") was
// QString::localeAwareCompare's. Qt compares an empty string with any other
// by code units, and otherwise compares the two in their composed normal
// form (NFC) by the platform's rules: on Apple CFStringCompare with
// kCFCompareLocalized, which is what this does too. Elsewhere the Qt build
// keeps asking Qt (tfilepath_qt.cpp).
#if defined(__APPLE__)
namespace {
CFMutableStringRef makeNfcString(const std::wstring &s) {
  std::u16string units;
  for (wchar_t c : s) {
    char32_t cp = char32_t(c);
    if (sizeof(wchar_t) == 4 && cp >= 0x10000) {
      units.push_back(char16_t(0xd800 + ((cp - 0x10000) >> 10)));
      units.push_back(char16_t(0xdc00 + ((cp - 0x10000) & 0x3ff)));
    } else
      units.push_back(char16_t(cp));
  }
  CFMutableStringRef str = CFStringCreateMutable(kCFAllocatorDefault, 0);
  CFStringAppendCharacters(str, (const UniChar *)units.data(),
                           (CFIndex)units.size());
  CFStringNormalize(str, kCFStringNormalizationFormC);
  return str;
}
}  // namespace
#endif

#if defined(__APPLE__) || !defined(NEXTTOONZ_LETTER_ORDER_FROM_QT)
int TFrameId::compareLetters(const std::wstring &a, const std::wstring &b) {
  if (a.empty() || b.empty()) return int(a.size()) - int(b.size());
#if defined(__APPLE__)
  CFMutableStringRef ca = makeNfcString(a), cb = makeNfcString(b);
  int result = (int)CFStringCompare(ca, cb, kCFCompareLocalized);
  CFRelease(ca);
  CFRelease(cb);
  return result;
#else
  // Without Qt on a platform that is not Apple's: the C library's collation
  // for the current locale, then code points. Not what Qt gave there (ICU);
  // see doc/nexttoonz_core_noqt.md, section 10.
  int result = std::wcscoll(a.c_str(), b.c_str());
  return result != 0 ? result : a.compare(b);
#endif
}
#endif

// TFrameId::operator string() const
std::string TFrameId::expand(FrameFormat format) const {
  if (m_frame == EMPTY_FRAME)
    return "";
  else if (m_frame == NO_FRAME)
    return "-";
  std::ostringstream o_buff;
  if (format == FOUR_ZEROS || format == UNDERSCORE_FOUR_ZEROS ||
      format == NO_SEP_FOUR_ZEROS) {
    o_buff.fill('0');
    o_buff.width(4);
    o_buff << m_frame;
    o_buff.width(0);
  } else if (format == CUSTOM_PAD || format == UNDERSCORE_CUSTOM_PAD ||
             format == NO_SEP_CUSTOM_PAD) {
    o_buff.fill('0');
    o_buff.width(m_zeroPadding);
    o_buff << m_frame;
    o_buff.width(0);
  } else {
    o_buff << m_frame;
  }
  if (m_letter.empty())
    return o_buff.str();
  else
    return o_buff.str() + toUtf8AsQString(m_letter);
}

//-------------------------------------------------------------------

const TFrameId &TFrameId::operator++() {
  ++m_frame;
  m_letter.clear();
  // m_letter = 0;
  return *this;
}

//-------------------------------------------------------------------

const TFrameId &TFrameId::operator--() {
  if (!m_letter.empty()) m_letter.clear();
  // if (m_letter > 0)
  //  m_letter = 0;
  else
    --m_frame;
  return *this;
}

//=============================================================================

inline bool isSlash(char c) { return c == slash || c == auxslash; }

//-----------------------------------------------------------------------------

inline bool isSlash(wchar_t c) { return c == wslash || c == wauxslash; }

//-----------------------------------------------------------------------------

// look for the last '/' or '\'. If not found return -1
// so that the substring starting from getLastSlash() + 1 is name.frame.type
inline int getLastSlash(const std::wstring &path) {
  int i;
  for (i = path.length() - 1; i >= 0 && !isSlash(path[i]); i--) {
  }
  return i;
}

//-----------------------------------------------------------------------------

/*
void TFilePath::setPath(string path)
{
bool isUncName = false;
  // remove '//', './' and trailing '/'; fix wrong slashes.
  // if path starts with "<alpha>:" add a slash
  int length =path.length();
  int pos = 0;
  if(path.length()>=2 && isalpha(path[0]) && path[1] == ':')
    {
     m_path.append(path,0,2);
     pos=2;
     if(path.length()==2 || !isSlash(path[pos])) m_path.append(1,slash);
    }
#ifdef _WIN32
  else  // if it's a UNC path it's like "\\\\MachineName"
        // DOUBLE CHECK! IF WE HAVE IP ADDRESS IT FAILED!
    if (path.length() >= 3 && path[0] == '\\' &&  path[1] == '\\' &&
(isalpha(path[2]) || isdigit(path[2])) )
      {
      isUncName = true;
      m_path.append(path,0,3);
      pos = 3;
      }
#endif
  for(;pos<length;pos++)
    if(path[pos] == '.')
    {
      pos++;
      if(pos>=length)
      {
        if(pos>1) m_path.append(1,'.');
      }
      else if(!isSlash(path[pos])) m_path.append(path,pos-1,2);
      else {
         while(pos+1<length && isSlash(path[pos+1]))
           pos++;
      }
    }
    else if(isSlash(path[pos]))
    {
      do pos++;
      while(pos<length && isSlash(path[pos]));
      pos--;
      m_path.append(1,slash);
    }
    else
    {
      m_path.append(1,path[pos]);
    }

    // remove trailing '/' (unless m_path == "/" or m_path == "<letter>:\"
    // or it's UNC (Windows only) )
    if(!(m_path.length()==1 && m_path[0] == slash ||
         m_path.length()==3 && isalpha(m_path[0]) && m_path[1] == ':' &&
m_path[2] == slash)
      && m_path.length()>1 && m_path[m_path.length()-1] == slash)
      m_path.erase(m_path.length()-1, 1);

  if (isUncName && m_path.find_last_of('\\') == 1) // only machine name is
specified... m_path.append(1, slash);
}
*/

//-----------------------------------------------------------------------------
/*
void append(string &out, wchar_t c)
{
  if(32 <= c && c<=127 && c!='&') out.append(1, (char)c);
  else if(c=='&') out.append("&amp;");
  else
    {
     ostringstream ss;
     ss << "&#" <<  c << ";" << '\0';
     out += ss.str();
     ss.freeze(0);
    }
}
*/
//-----------------------------------------------------------------------------

void TFilePath::setPath(std::wstring path) {
  bool isUncName = false;
  // remove '//', './' and trailing '/'; fix wrong slashes.
  // if path starts with "<alpha>:" add a slash
  int length = path.length();
  int pos    = 0;

  if (path.length() >= 2 && iswalpha(path[0]) && path[1] == L':') {
    m_path.append(1, (wchar_t)path[0]);
    m_path.append(1, L':');
    // m_path.append(path,0,2);
    pos = 2;
    if (path.length() == 2 || !isSlash(path[pos])) m_path.append(1, wslash);
  }
  // if it's a UNC path it's like "\\\\MachineName"
  else if ((path.length() >= 3 && path[0] == L'\\' && path[1] == L'\\' &&
            iswalnum(path[2])) ||
           (path.length() >= 3 && path[0] == L'/' && path[1] == L'/' &&
            iswalnum(path[2]))) {
    isUncName = true;
    m_path.append(2, L'\\');
    m_path.append(1, path[2]);
    pos = 3;
  }
  for (; pos < length; pos++)
    if (path[pos] == L'.') {
      pos++;
      if (pos >= length) {
        if (pos > 1) m_path.append(1, L'.');
      } else if (!isSlash(path[pos])) {
        m_path.append(1, L'.');
        m_path.append(1, path[pos]);
      } else {
        while (pos + 1 < length && isSlash(path[pos + 1])) pos++;
      }
    } else if (isSlash(path[pos])) {
      int firstSlashPos = pos;
      do pos++;
      while (pos < length && isSlash(path[pos]));
      if (firstSlashPos == 0 && pos == 4)  // Case "\\\\MachineName"
        m_path.append(2, wslash);
      else
        m_path.append(1, wslash);
      pos--;
    } else {
      m_path.append(1, path[pos]);
    }

  // remove trailing '/' (unless m_path == "/" or m_path == "<letter>:\"
  // or it's UNC (Windows only) )
  if (!((m_path.length() == 1 && m_path[0] == wslash) ||
        (m_path.length() == 3 && iswalpha(m_path[0]) && m_path[1] == L':' &&
         m_path[2] == wslash)) &&
      (m_path.length() > 1 && m_path[m_path.length() - 1] == wslash))
    m_path.erase(m_path.length() - 1, 1);

  if (isUncName &&
      !(m_path.find_last_of(L'\\') > 1 ||
        m_path.find_last_of(L'/') > 1))  // only machine name is specified...
    m_path.append(1, wslash);
}

//-----------------------------------------------------------------------------

TFilePath::TFilePath(const char *path) { setPath(::to_wstring(path)); }

//-----------------------------------------------------------------------------

TFilePath::TFilePath(const std::string &path) { setPath(::to_wstring(path)); }

//-----------------------------------------------------------------------------

TFilePath::TFilePath(const std::wstring &path) { setPath(path); }

//-----------------------------------------------------------------------------

bool TFilePath::operator==(const TFilePath &fp) const {
#ifdef _WIN32
  return _wcsicmp(m_path.c_str(), fp.m_path.c_str()) == 0;
#else
  return m_path == fp.m_path;
#endif
}

//-----------------------------------------------------------------------------

bool TFilePath::operator<(const TFilePath &fp) const {
  std::wstring iName = m_path;
  std::wstring jName = fp.m_path;
  int i1 = 0, j1 = 0;
  int i2 = m_path.find(L"\\");
  int j2 = fp.m_path.find(L"\\");
  if (i2 == j2 && j2 == -1)
#ifdef _WIN32
    return _wcsicmp(m_path.c_str(), fp.m_path.c_str()) < 0;
#else
    return m_path < fp.m_path;
#endif
  if (!i2) {
    ++i1;
    i2 = m_path.find(L"\\", i1);
  }
  if (!j2) {
    ++j1;
    j2 = fp.m_path.find(L"\\", j1);
  }
  while (i2 != -1 || j2 != -1) {
    iName = (i2 != -1) ? m_path.substr(i1, i2 - i1) : m_path;
    jName = (j2 != -1) ? fp.m_path.substr(j1, j2 - j1) : fp.m_path;
// if the two path parts, between slashes, are equal
// iterate the comparison process otherwise return
#ifdef _WIN32
    char differ;
    differ = _wcsicmp(iName.c_str(), jName.c_str());
    if (differ != 0) return differ < 0 ? true : false;
#else
    if (TFilePath(iName) != TFilePath(jName))
      return TFilePath(iName) < TFilePath(jName);
#endif
    i1 = (i2 != -1) ? i2 + 1 : m_path.size();
    j1 = (j2 != -1) ? j2 + 1 : fp.m_path.size();
    i2 = m_path.find(L"\\", i1);
    j2 = fp.m_path.find(L"\\", j1);
  }

  iName = m_path.substr(i1, m_path.size() - i1);
  jName = fp.m_path.substr(j1, fp.m_path.size() - j1);
#ifdef _WIN32
  return _wcsicmp(iName.c_str(), jName.c_str()) < 0;
#else
  return TFilePath(iName) < TFilePath(jName);
#endif
}

#ifdef LEVO
bool TFilePath::operator<(const TFilePath &fp) const {
  /*
wstring a = m_path, b = fp.m_path;
for(;;)
{
wstring ka,kb;
int i;
i = a.find_first_of("/\\");
if(i==wstring::npos) {ka = a; a = L"";}
i = b.find_first_of("/\\");
if(i==wstring::npos) {ka = a; a = L"";}

}
*/
  wstring a = toLower(m_path), b = toLower(fp.m_path);
  return a < b;
}
#endif

//-----------------------------------------------------------------------------

TFilePath &TFilePath::operator+=(const TFilePath &fp) {
  if (fp.isAbsolute()) {
    // Log error or handle appropriately
    fprintf(stderr, "Cannot append absolute path to another path\n");
    return *this;
  }

  if (fp.isEmpty())
    return *this;
  else if (isEmpty()) {
    *this = fp;
    return *this;
  } else if (m_path.length() != 1 || m_path[0] != slash) {
    if (m_path.empty()) {
      // Handle empty path case
      *this = fp;
      return *this;
    }
    if (!isSlash(m_path[m_path.length() - 1])) m_path.append(1, wslash);
    m_path += fp.m_path;
    return *this;
  } else {
    *this = TFilePath(m_path + fp.m_path);
    return *this;
  }
}
//-----------------------------------------------------------------------------
TFilePath TFilePath::operator+(const TFilePath &fp) const {
  TFilePath ret(*this);
  ret += fp;
  return ret;
}
//-----------------------------------------------------------------------------

TFilePath &TFilePath::operator+=(const std::wstring &s) {
  if (s.empty()) return *this;
  // if(m_path.length()!=1 || m_path[0] != slash)
  //  m_path += slash;
  if (m_path.length() > 0 && !isSlash(m_path[m_path.length() - 1]))
    m_path.append(1, wslash);
  m_path += s;
  return *this;
}

//-----------------------------------------------------------------------------

const std::wstring TFilePath::getWideString() const {
  return m_path;
  /*
std::wstring s;
int n = m_path.size();
for(int i=0;i<n; i++)
{
char c = m_path[i];
if(c!='&') s.append(1, (unsigned short)c);
else
 {
  i++;
  if(m_path[i] == '#')
    {
     unsigned short w = 0;
     i++;
     while(i<n)
       {
        c = m_path[i];
        if('0'<=c && c<='9')
          w = w*10 + c - '0';
        else if('a' <=c && c<='f')
          w = w*10 + c - 'a' + 10;
        else if('A' <=c && c<='F')
          w = w*10 + c - 'A' + 10;
        else
          break;
        i++;
       }
     s.append(1, w);
    }
 }
}
return s;
*/
}
/*
#else
const wstring TFilePath::getWideString() const
{
   wstring a(L"dummy string");
   return a;
}
#endif
*/

//-----------------------------------------------------------------------------

std::ostream &operator<<(std::ostream &out, const TFilePath &path) {
  std::wstring w = path.getWideString();
  return out << ::to_string(w) << " ";
  //  string w = path.getString();
  //  return out << w << " ";
}

//-----------------------------------------------------------------------------

/*
TFilePath TFilePath::operator+ (const TFilePath &fp) const
{
assert(!fp.isAbsolute());
if(fp.isEmpty()) return *this;
else if(isEmpty()) return fp;
else if(m_path.length()!=1 || m_path[0] != slash)
  return TFilePath(m_path + slash + fp.m_path);
else
  return TFilePath(m_path + fp.m_path);
}
*/
//-----------------------------------------------------------------------------

bool TFilePath::isAbsolute() const {
  return ((m_path.length() >= 1 && m_path[0] == slash) ||
          (m_path.length() >= 2 && iswalpha(m_path[0]) && m_path[1] == ':'));
}

//-----------------------------------------------------------------------------

bool TFilePath::isRoot() const {
  return ((m_path.length() == 1 && m_path[0] == slash) ||
          (m_path.length() == 3 && iswalpha(m_path[0]) && m_path[1] == ':' &&
           m_path[2] == slash) ||
          ((m_path.length() > 2 && m_path[0] == slash && m_path[1] == slash) &&
           (std::string::npos == m_path.find(slash, 2) ||
            m_path.find(slash, 2) == (m_path.size() - 1))));
}

//-----------------------------------------------------------------------------

// returns "" (no type, no dot), "." (file with type) or ".." (file with type
// and frame)
std::string TFilePath::getDots() const {
  if (!TFilePath::m_useStandard) {
    TFilePathInfo info = analyzePath();
    if (info.extension.empty()) return "";
    if (info.sepChar == 0) return ".";
    // return ".." regardless of sepChar type (either "_" or ".")
    return "..";
  }
  //-----

  std::string type = lowerType(getType());
  if (isFfmpegType()) return ".";
  int i            = getLastSlash(m_path);
  std::wstring str = m_path.substr(i + 1);
  // I could also have a.b.c.d where d is the extension
  i = str.rfind(L".");
  if (i == (int)std::wstring::npos || str == L"..") return "";

  int j = str.substr(0, i).rfind(L".");
  if (j == (int)std::wstring::npos && m_underscoreFormatAllowed)
    j = str.substr(0, i).rfind(L"_");

  if (j != (int)std::wstring::npos)
    return (j == i - 1 || (checkForSeqNum(type) && isNumbers(str, j, i))) ? ".."
                                                                          : ".";
  else
    return ".";
}

//-----------------------------------------------------------------------------

wchar_t TFilePath::getSepCharW() const {
  if (!TFilePath::m_useStandard) return analyzePath().sepChar;
  //-----
  std::string type = lowerType(getType());
  if (isFfmpegType()) return 0;
  int i            = getLastSlash(m_path);
  std::wstring str = m_path.substr(i + 1);
  // I could also have a.b.c.d where d is the extension
  i = str.rfind(L".");
  if (i == (int)std::wstring::npos || str == L"..") return 0;

  int j = str.substr(0, i).rfind(L".");

  if (j != (int)std::wstring::npos)
    return (j == i - 1 || (checkForSeqNum(type) && isNumbers(str, j, i)))
               ? L'.'
               : 0;
  if (!m_underscoreFormatAllowed) return 0;

  j = str.substr(0, i).rfind(L"_");
  if (j != (int)std::wstring::npos)
    return (j == i - 1 || (checkForSeqNum(type) && isNumbers(str, j, i)))
               ? L'_'
               : 0;
  else
    return 0;
}

//-----------------------------------------------------------------------------

std::string TFilePath::getDottedType()
    const  // returns extension with DOT (if present)
{
  if (!TFilePath::m_useStandard) {
    std::wstring ext = analyzePath().extension;
    if (ext.empty()) return "";
    return "." + toUtf8AsQString(toLowerAsQString(ext));
  }

  int i            = getLastSlash(m_path);
  std::wstring str = m_path.substr(i + 1);
  i                = str.rfind(L".");
  if (i == (int)std::wstring::npos) return "";

  return toLower(::to_string(str.substr(i)));
}

//-----------------------------------------------------------------------------

std::string TFilePath::getUndottedType() const  // returns extension without DOT
{
  if (!TFilePath::m_useStandard) {
    std::wstring ext = analyzePath().extension;
    if (ext.empty())
      return "";
    else
      return toUtf8AsQString(toLowerAsQString(ext));
  }

  size_t i         = getLastSlash(m_path);
  std::wstring str = m_path.substr(i + 1);
  i                = str.rfind(L".");
  if (i == std::wstring::npos || i == str.length() - 1) return "";
  return toLower(::to_string(str.substr(i + 1)));
}

//-----------------------------------------------------------------------------

std::wstring TFilePath::getWideName() const  // noDot! noSlash!
{
  if (!TFilePath::m_useStandard) {
    return analyzePath().levelName;
  }

  std::string type     = lowerType(getType());
  int i            = getLastSlash(m_path);  // look for the last slash
  std::wstring str = m_path.substr(i + 1);
  i                = str.rfind(L".");
  if (i == (int)std::wstring::npos) return str;
  int j = str.substr(0, i).rfind(L".");
  if (j != (int)std::wstring::npos) {
    if (checkForSeqNum(type) && isNumbers(str, j, i)) i = j;
  } else if (m_underscoreFormatAllowed) {
    j = str.substr(0, i).rfind(L"_");
    if (j != (int)std::wstring::npos && checkForSeqNum(type) &&
        isNumbers(str, j, i))
      i = j;
  }
  return str.substr(0, i);
}

//-----------------------------------------------------------------------------

std::string TFilePath::getName() const  // noDot! noSlash!
{
  return ::to_string(getWideName());
}

//-----------------------------------------------------------------------------
// e.g. TFilePath("/pippo/pluto.0001.gif").getLevelName() == "pluto..gif"
std::string TFilePath::getLevelName() const {
  return ::to_string(getLevelNameW());
}

//-----------------------------------------------------------------------------
// e.g. TFilePath("/pippo/pluto.0001.gif").getLevelName() == "pluto..gif"

std::wstring TFilePath::getLevelNameW() const {
  if (!TFilePath::m_useStandard) {
    TFilePathInfo info = analyzePath();
    if (info.extension.empty()) return info.levelName;
    std::wstring name = info.levelName;
    if (info.sepChar != 0) name += info.sepChar;
    name += L"." + info.extension;
    return name;
  }

  int i            = getLastSlash(m_path);  // look for the last slash
  std::wstring str = m_path.substr(i + 1);  // str is m_path without directory
  std::string type     = lowerType(getType());
  if (isFfmpegType()) return str;
  int j = str.rfind(L".");                       // str[j..] = ".type"
  if (j == (int)std::wstring::npos) return str;  // no frame; no type
  i = str.substr(0, j).rfind(L'.');
  if (i == (int)std::wstring::npos && m_underscoreFormatAllowed)
    i = str.substr(0, j).rfind(L'_');

  if (j == i || j - i == 1)  // prova.tif or prova..tif
    return str;

  if (!checkForSeqNum(type) || !isNumbers(str, i, j) ||
      i == (int)std::wstring::npos)
    return str;
  // prova.0001.tif
  return str.erase(i + 1, j - i - 1);
}

//-----------------------------------------------------------------------------

TFilePath TFilePath::getParentDir() const  // noSlash!
{
  int i = getLastSlash(m_path);  // look for the last slash
  if (i < 0) {
    if (m_path.length() >= 2 &&
        ((L'a' <= m_path[0] && m_path[0] <= L'z') ||
         (L'A' <= m_path[0] && m_path[0] <= L'Z')) &&
        m_path[1] == ':')
      return TFilePath(m_path.substr(0, 2));
    else
      return TFilePath("");
  } else if (i == 0)
    return TFilePath("/");
  else
    return TFilePath(m_path.substr(0, i));
}

//-----------------------------------------------------------------------------
// return true if the fID is EMPTY_FRAME
bool TFilePath::isLevelName() const {
  if (!TFilePath::m_useStandard) {
    return analyzePath().fId.getNumber() == TFrameId::EMPTY_FRAME;
  }

  std::string type = lowerType(getType());
  if (isFfmpegType() || !checkForSeqNum(type)) return false;
  try {
    return getFrame() == TFrameId(TFrameId::EMPTY_FRAME);
  } catch (...) {
    return false;
  }
}

TFrameId TFilePath::getFrame() const {
  if (!TFilePath::m_useStandard) {
    return analyzePath().fId;
  }

  //-----
  int i            = getLastSlash(m_path);  // look for the last slash
  std::wstring str = m_path.substr(i + 1);  // str is the path without parentdir
  std::string type     = lowerType(getType());
  i                = str.rfind(L'.');
  if (i == (int)std::wstring::npos || str == L"." || str == L"..")
    return TFrameId(TFrameId::NO_FRAME);
  int j;

  j = str.substr(0, i).rfind(L'.');
  if (j == (int)std::wstring::npos && m_underscoreFormatAllowed)
    j = str.substr(0, i).rfind(L'_');

  if (j == (int)std::wstring::npos) return TFrameId(TFrameId::NO_FRAME);
  if (i == j + 1) return TFrameId(TFrameId::EMPTY_FRAME);

  // Exclude cases with non-numeric characters inbetween. (In case the file name
  // contains "_" or ".")
  if (!checkForSeqNum(type) || !isNumbers(str, j, i))
    return TFrameId(TFrameId::NO_FRAME);

  return TFrameId(str.substr(j + 1, i - j - 1), str[j]);
}

//-----------------------------------------------------------------------------

bool TFilePath::isFfmpegType() const {
  std::string type = lowerType(getType());
  return (type == "gif" || type == "mp4" || type == "webm");
}

//-----------------------------------------------------------------------------

bool TFilePath::isUneditable() const {
  std::string type = lowerType(getType());
  return (type == "psd" || type == "gif" || type == "mp4" || type == "webm");
}

//-----------------------------------------------------------------------------
TFilePath TFilePath::withType(const std::string &type) const {
  if (type.length() >= 2 && type.substr(0, 2) == "..") {
    fprintf(stderr, "Invalid type: %s\n", type.c_str());
    return *this;
  }

  int i            = getLastSlash(m_path);  // look for the last slash
  std::wstring str = m_path.substr(i + 1);  // str is the path without parentdir
  int j            = str.rfind(L'.');
  if (j == (int)std::wstring::npos || str == L"..")
  // the original path has no type
  {
    if (type == "")
      return *this;
    else if (type[0] == '.')
      return TFilePath(m_path + ::to_wstring(type));
    else
      return TFilePath(m_path + ::to_wstring("." + type));
  } else
  // the original path already has a type
  {
    if (type == "")
      return TFilePath(m_path.substr(0, i + j + 1));
    else if (type[0] == '.')
      return TFilePath(m_path.substr(0, i + j + 1) + ::to_wstring(type));
    else
      return TFilePath(m_path.substr(0, i + j + 2) + ::to_wstring(type));
  }
}

//-----------------------------------------------------------------------------

TFilePath TFilePath::withName(const std::string &name) const {
  return withName(::to_wstring(name));
}

//-----------------------------------------------------------------------------

TFilePath TFilePath::withName(const std::wstring &name) const {
  if (!TFilePath::m_useStandard) {
    TFilePathInfo info = analyzePath();

    std::wstring ret = info.parentDir + roundTripAsQString(name);
    if (info.fId.getNumber() != TFrameId::NO_FRAME) {
      std::wstring sepChar = (info.sepChar == 0) ? L"."
                             : (info.sepChar == L'#' &&
                                info.fId.getNumber() != TFrameId::EMPTY_FRAME)
                                 ? L""
                                 : std::wstring(1, info.sepChar);
      ret += sepChar +
             ::to_wstring(info.fId.expand(info.fId.getCurrentFormat()));
    }
    if (!info.extension.empty()) ret += L"." + info.extension;

    return TFilePath(ret);
  }

  int i            = getLastSlash(m_path);  // look for the last slash
  std::wstring str = m_path.substr(i + 1);  // str is the path without parentdir
  std::string type     = lowerType(getType());
  int j;
  j = str.rfind(L'.');

  if (j == (int)std::wstring::npos) {
    if (m_underscoreFormatAllowed) {
      j = str.rfind(L'_');
      if (j != (int)std::wstring::npos)
        return TFilePath(m_path.substr(0, i + 1) + name + str.substr(j));
    }
    return TFilePath(m_path.substr(0, i + 1) + name);
  }
  int k;

  k = str.substr(0, j).rfind(L".");
  if (k == (int)std::wstring::npos && m_underscoreFormatAllowed)
    k = str.substr(0, j).rfind(L"_");

  if (k == (int)(std::wstring::npos))
    k = j;
  else if (k != j - 1 && (!checkForSeqNum(type) || !isNumbers(str, k, j)))
    k = j;

  return TFilePath(m_path.substr(0, i + 1) + name + str.substr(k));
}

//-----------------------------------------------------------------------------

TFilePath TFilePath::withParentDir(const TFilePath &dir) const {
  int i = getLastSlash(m_path);  // look for the last slash
  return dir + TFilePath(m_path.substr(i + 1));
}

//-----------------------------------------------------------------------------
// TODO: This function might need to create a path based on
// the value of `format` unless `format` is set to `USE_CURRENT_FORMAT`.
// The potential impact must be investigated before making such changes.

TFilePath TFilePath::withFrame(const TFrameId &frame,
                               TFrameId::FrameFormat format) const {
  if (!TFilePath::m_useStandard) {
    TFilePathInfo info = analyzePath();
    // Override format input because it may be wrong.
    if (checkForSeqNum(info.extension)) format = frame.getCurrentFormat();
    // override format if the original fid is available
    else if (info.fId.getNumber() != TFrameId::NO_FRAME)
      format = info.fId.getCurrentFormat();

    if (info.extension.empty()) {
      if (frame.isEmptyFrame() || frame.isNoFrame()) return *this;

      return TFilePath(m_path + L"." + ::to_wstring(frame.expand(format)));
    }
    if (frame.isNoFrame()) {
      return TFilePath(info.parentDir + info.levelName + L"." + info.extension);
    }
    // QString(char) took the char as Latin-1, a NUL included.
    std::wstring sepChar =
        (info.sepChar == 0)
            ? std::wstring(1, wchar_t((unsigned char)frame.getStartSeqInd()))
        : (info.sepChar == L'#' && frame.getNumber() != TFrameId::EMPTY_FRAME)
            ? std::wstring()
            : std::wstring(1, info.sepChar);

    return TFilePath(info.parentDir + info.levelName + sepChar +
                     ::to_wstring(frame.expand(format)) + L"." +
                     info.extension);
  }

  //-----------------

  const std::wstring dot = L".", dotDot = L"..";
  int i            = getLastSlash(m_path);  // look for the last slash
  std::wstring str = m_path.substr(i + 1);  // str is the path without parentdir
  std::string type     = lowerType(getType());
  if (str == dot || str == dotDot) {
    fprintf(stderr, "Invalid path for frame operation: %ls\n", str.c_str());
    return *this;
  }

  int j          = str.rfind(L'.');
  const char *ch = ".";
  // Override format input because it may be wrong.
  if (!isFfmpegType() && checkForSeqNum(type))
    format = frame.getCurrentFormat();
  if (m_underscoreFormatAllowed && (format == TFrameId::UNDERSCORE_FOUR_ZEROS ||
                                    format == TFrameId::UNDERSCORE_NO_PAD ||
                                    format == TFrameId::UNDERSCORE_CUSTOM_PAD))
    ch = "_";

  // no extension case
  if (j == (int)std::wstring::npos) {
    if (frame.isEmptyFrame() || frame.isNoFrame())
      return *this;
    else
      return TFilePath(m_path + ::to_wstring(ch + frame.expand(format)));
  }

  int k = str.substr(0, j).rfind(L'.');

  bool hasValidFrameNum = false;
  if (!isFfmpegType() && checkForSeqNum(type) && isNumbers(str, k, j))
    hasValidFrameNum = true;
  std::string frameString;
  if (frame.isNoFrame())
    frameString = "";
  else if (!frame.isEmptyFrame() && getDots() != "." && !hasValidFrameNum) {
    if (k != (int)std::wstring::npos) {
      std::wstring wstr = str.substr(k, j - k);
      std::string str2(wstr.begin(), wstr.end());
      frameString = str2;
    } else
      frameString = "";
  } else
    frameString = ch + frame.expand(format);

  if (k != (int)std::wstring::npos)
    return TFilePath(m_path.substr(0, k + i + 1) + ::to_wstring(frameString) +
                     str.substr(j));
  else if (m_underscoreFormatAllowed) {
    k = str.substr(0, j).rfind(L'_');
    if (k != (int)std::wstring::npos &&
        (k == j - 1 ||
         (checkForSeqNum(type) &&
          isNumbers(str, k,
                    j))))  // -- In case of "_." or "_[numbers]." --
      return TFilePath(m_path.substr(0, k + i + 1) +
                       ((frame.isNoFrame())
                            ? L""
                            : ::to_wstring("_" + frame.expand(format))) +
                       str.substr(j));
  }
  return TFilePath(m_path.substr(0, j + i + 1) + ::to_wstring(frameString) +
                   str.substr(j));
}

//-----------------------------------------------------------------------------

bool TFilePath::isAncestorOf(const TFilePath &possibleDescendent) const {
  size_t len = m_path.length();
  if (len == 0) {
    // the dot is ancestor of all non-absolute paths
    return !possibleDescendent.isAbsolute();
  }

  // a path is ancestor of itself
  if (m_path == possibleDescendent.m_path) return true;

  int possibleDescendentLen = possibleDescendent.m_path.length();
  // otherwise the ancestor must be shorter
  if ((int)len >= possibleDescendentLen) return false;
  // and must match the first part of the descendant
  if (toLower(m_path) != toLower(possibleDescendent.m_path.substr(0, len)))
    return false;
  // if the ancestor does not end with slash there must be a slash in the
  // descendant, right after
  if (m_path[len - 1] != wslash && possibleDescendent.m_path[len] != wslash)
    return false;

  return true;
}

//-----------------------------------------------------------------------------

TFilePath TFilePath::operator-(const TFilePath &fp) const {
  if (toLower(m_path) == toLower(fp.m_path)) return TFilePath("");
  if (!fp.isAncestorOf(*this)) return *this;
  int len = fp.m_path.length();
  if (len == 0 || fp.m_path[len - 1] != wslash) len++;

  return TFilePath(m_path.substr(len));
}

//-----------------------------------------------------------------------------

bool TFilePath::match(const TFilePath &fp) const {
  if (!TFilePath::m_useStandard) {
    if (getParentDir() != fp.getParentDir()) return false;

    TFilePathInfo info     = analyzePath();
    TFilePathInfo info_ext = fp.analyzePath();

    return (info.levelName == info_ext.levelName && info.fId == info_ext.fId &&
            info.extension == info_ext.extension);
  }

  return getParentDir() == fp.getParentDir() && getName() == fp.getName() &&
         getFrame() == fp.getFrame() && getType() == fp.getType();
}

//-----------------------------------------------------------------------------

void TFilePath::split(std::wstring &head, TFilePath &tail) const {
  TFilePath ancestor = getParentDir();
  if (ancestor == TFilePath()) {
    head = getWideString();
    tail = TFilePath();
    return;
  }
  for (;;) {
    if (ancestor.isRoot()) break;
    TFilePath p = ancestor.getParentDir();
    if (p == TFilePath()) break;
    ancestor = p;
  }
  head = ancestor.getWideString();
  tail = *this - ancestor;
}

//-----------------------------------------------------------------------------

// fidRegExpStr() is in tfilepath_qt.cpp. analyzePath() used to match three
// regular expressions built from it with QRegularExpression; the code below
// matches the same patterns by hand, trying the alternatives in the order
// PCRE does (greedy, first match wins), and gives the same captures. The
// patterns, for the record:
//   levelname  [^\\/:,;*?"<>|]+
//   sepchar    [._]
//   framenum   \d+                                (ASCII digits)
//   suffix     [a-zA-Z]{0,n}, or with m_acceptNonAlphabetSuffix
//              [^._ \\/:,;*?"<>|0-9]{0,n}         (n = m_letterCountForSuffix,
//                                                  no limit when 0)
//   extension  [^._ \\/:,;*?"<>|]+
//   1. ^(?:levelname)?sepchar(?:framenum suffix)?\.extension$
//   2. ^(?:levelname_nosep)?(?:(#)|(?:framenum suffix))\.extension$
//      where levelname_nosep is [^\\/:,;*?"<>|]*[^\d\\/:,;*?"<>|]
//   3. ^(?:levelname)?\.extension$
// $ also matches before a newline that ends the string, as in PCRE.

namespace {

bool isLevelNameChar(wchar_t c) {
  switch (c) {
  case L'\\':
  case L'/':
  case L':':
  case L',':
  case L';':
  case L'*':
  case L'?':
  case L'"':
  case L'<':
  case L'>':
  case L'|':
    return false;
  default:
    return true;
  }
}

bool isAsciiDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

bool isExtensionChar(wchar_t c) {
  return c != L'.' && c != L'_' && c != L' ' && isLevelNameChar(c);
}

class PathNameMatcher {
  const std::wstring &m_s;
  int m_n;
  bool m_nonAlphabetSuffix;
  int m_suffixMax;  // -1: no limit

public:
  // Captures of a match; -1 marks a group that did not take part.
  int levelEnd = -1, sepPos = -1, numBegin = -1, numEnd = -1, suffixEnd = -1,
      extBegin = -1, extEnd = -1;

  PathNameMatcher(const std::wstring &s, bool nonAlphabetSuffix,
                  int letterCount)
      : m_s(s)
      , m_n((int)s.size())
      , m_nonAlphabetSuffix(nonAlphabetSuffix)
      , m_suffixMax(letterCount == 0 ? -1 : letterCount) {}

  bool atEnd(int p) const {
    return p == m_n || (p == m_n - 1 && m_s[m_n - 1] == L'\n');
  }

  bool isSuffixChar(wchar_t c) const {
    return m_nonAlphabetSuffix ? isExtensionChar(c) && !isAsciiDigit(c)
                               : (c >= L'a' && c <= L'z') ||
                                     (c >= L'A' && c <= L'Z');
  }

  // \.extension$ at p
  bool matchDotExtension(int p) {
    if (p >= m_n || m_s[p] != L'.') return false;
    int b = p + 1, e = b;
    while (e < m_n && isExtensionChar(m_s[e])) ++e;
    for (int k = e; k > b; --k)
      if (atEnd(k)) {
        extBegin = b, extEnd = k;
        return true;
      }
    return false;
  }

  // framenum suffix \.extension$ at p
  bool matchFrameThenExtension(int p) {
    int d = p;
    while (d < m_n && isAsciiDigit(m_s[d])) ++d;
    for (int q = d; q > p; --q) {
      int r = q;
      while (r < m_n && isSuffixChar(m_s[r]) &&
             (m_suffixMax < 0 || r - q < m_suffixMax))
        ++r;
      for (int k = r; k >= q; --k)
        if (matchDotExtension(k)) {
          numBegin = p, numEnd = q, suffixEnd = k;
          return true;
        }
    }
    return false;
  }

  // Pattern 1
  bool matchWithSeparator() {
    int maxLevel = 0;
    while (maxLevel < m_n && isLevelNameChar(m_s[maxLevel])) ++maxLevel;
    for (int k = maxLevel; k >= 0; --k) {
      if (k >= m_n || (m_s[k] != L'.' && m_s[k] != L'_')) continue;
      numBegin = numEnd = suffixEnd = -1;
      if (matchFrameThenExtension(k + 1) || matchDotExtension(k + 1)) {
        levelEnd = k, sepPos = k;
        return true;
      }
    }
    return false;
  }

  // Pattern 2; sepPos is the '#' if that alternative matched
  bool matchWithoutSeparator() {
    auto rest = [&](int k) {
      numBegin = numEnd = suffixEnd = -1;
      if (k < m_n && m_s[k] == L'#' && matchDotExtension(k + 1)) {
        sepPos = k;
        return true;
      }
      sepPos = -1;
      return matchFrameThenExtension(k);
    };
    int maxLevel = 0;
    while (maxLevel < m_n && isLevelNameChar(m_s[maxLevel])) ++maxLevel;
    for (int j = maxLevel; j >= 0; --j) {
      // [..]* took j characters; the last character must not be a digit
      if (j >= m_n || !isLevelNameChar(m_s[j]) || isAsciiDigit(m_s[j]))
        continue;
      if (rest(j + 1)) {
        levelEnd = j + 1;
        return true;
      }
    }
    if (rest(0)) {
      levelEnd = 0;
      return true;
    }
    return false;
  }

  // Pattern 3
  bool matchWithoutFrame() {
    int maxLevel = 0;
    while (maxLevel < m_n && isLevelNameChar(m_s[maxLevel])) ++maxLevel;
    for (int k = maxLevel; k >= 0; --k)
      if (matchDotExtension(k)) {
        levelEnd = k;
        return true;
      }
    return false;
  }

  std::wstring part(int b, int e) const {
    return (b < 0 || e <= b) ? std::wstring() : m_s.substr(b, e - b);
  }
};

// QString::toInt() of the digits: 0 when they overflow an int.
int toIntAsQString(const std::wstring &digits) {
  long long v = 0;
  for (wchar_t c : digits) {
    v = v * 10 + (c - L'0');
    if (v > INT_MAX) return 0;
  }
  return int(v);
}

// The frame of a match, as analyzePath built it.
TFrameId frameOf(const std::wstring &numberStr, const std::wstring &suffix,
                 char sepChar) {
  if (numberStr.empty())  // empty frame case : hogehoge..jpg
    return TFrameId(TFrameId::EMPTY_FRAME, 0, 4, sepChar);
  int number  = toIntAsQString(numberStr);
  int padding = 0;
  if (numberStr[0] == L'0')  // with padding
    padding = (int)numberStr.length();
  return TFrameId(number, suffix, padding, sepChar);
}

}  // namespace

//-----------------------------------------------------------------------------

TFilePath::TFilePathInfo TFilePath::analyzePath() const {
  // analyzePath should only be called when m_useStandard is false
  if (TFilePath::m_useStandard) {
#ifndef NDEBUG
    fprintf(stderr,
            "analyzePath called with m_useStandard enabled - returning empty "
            "TFilePathInfo\n");
#endif
    // Return empty structure since standard path analysis should be used
    // instead
    return TFilePathInfo();
  }

  TFilePath::TFilePathInfo info;

  int i            = getLastSlash(m_path);
  std::wstring str = m_path.substr(i + 1);

  // The parts went through QString: what it did to them, they still get.
  if (i >= 0) info.parentDir = roundTripAsQString(m_path.substr(0, i + 1));

  if (!isValidAsQString(str)) {
    // QRegularExpression matches nothing in a string that is not valid
    // UTF-16: the "no periods" case below.
    info.levelName = roundTripAsQString(str);
    info.sepChar   = 0;
    info.fId = TFrameId(TFrameId::NO_FRAME, 0, 0);  // initialize with NO_PAD
    return info;
  }
  const std::wstring fileName = roundTripAsQString(str);

  PathNameMatcher rx(fileName, m_acceptNonAlphabetSuffix,
                     m_letterCountForSuffix);
  if (rx.matchWithSeparator()) {
    std::wstring levelName = rx.part(0, rx.levelEnd);
    std::wstring sepChar   = rx.part(rx.sepPos, rx.sepPos + 1);
    std::wstring numberStr = rx.part(rx.numBegin, rx.numEnd);
    std::wstring suffix    = rx.part(rx.numEnd, rx.suffixEnd);
    info.levelName         = levelName;
    info.sepChar           = sepChar[0];
    info.extension         = rx.part(rx.extBegin, rx.extEnd);

    // ignore frame numbers on non-sequential (i.e. movie) extension case
    if (!checkForSeqNum(info.extension)) {
      info.levelName = levelName + sepChar + numberStr + suffix;
      info.sepChar   = 0;
      info.fId = TFrameId(TFrameId::NO_FRAME, 0, 0);  // initialize with NO_PAD
    } else
      info.fId = frameOf(numberStr, suffix, (char)info.sepChar);
    return info;
  }
  // check no-separator sequencial images case (hogehoge0001.jpg /
  // hogehoge#.jpg)
  if (m_noSeparatorFormatAllowed) {
    PathNameMatcher rx_nosep(fileName, m_acceptNonAlphabetSuffix,
                             m_letterCountForSuffix);
    if (rx_nosep.matchWithoutSeparator()) {
      std::wstring levelName = rx_nosep.part(0, rx_nosep.levelEnd);
      std::wstring numberStr = rx_nosep.part(rx_nosep.numBegin, rx_nosep.numEnd);
      std::wstring suffix =
          rx_nosep.part(rx_nosep.numEnd, rx_nosep.suffixEnd);
      info.levelName = levelName;
      info.sepChar   = L'#';
      info.extension = rx_nosep.part(rx_nosep.extBegin, rx_nosep.extEnd);

      // ignore frame numbers on non-sequential (i.e. movie) extension case
      if (!checkForSeqNum(info.extension)) {
        info.levelName = levelName + numberStr + suffix;
        info.sepChar   = 0;
        info.fId =
            TFrameId(TFrameId::NO_FRAME, 0, 0);  // initialize with NO_PAD
      } else
        info.fId = frameOf(numberStr, suffix, '#');
      return info;
    }
  }

  // no frame case : hogehoge.jpg
  // no level name case : .jpg
  PathNameMatcher rx_nf(fileName, m_acceptNonAlphabetSuffix,
                        m_letterCountForSuffix);
  if (rx_nf.matchWithoutFrame()) {
    info.levelName = rx_nf.part(0, rx_nf.levelEnd);
    info.sepChar   = 0;
    info.fId = TFrameId(TFrameId::NO_FRAME, 0, 0);  // initialize with NO_PAD
    info.extension = rx_nf.part(rx_nf.extBegin, rx_nf.extEnd);
    return info;
  }

  // no periods
  info.levelName = fileName;
  info.sepChar   = 0;
  info.fId = TFrameId(TFrameId::NO_FRAME, 0, 0);  // initialize with NO_PAD
  info.extension.clear();
  return info;
}
