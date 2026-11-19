

#include "tconvert.h"
//#include "texception.h"

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#ifdef _WIN32
#include "windows.h"
#endif

#include <algorithm>
#include <cstring>
#include <iterator>
#include <sstream>

//-----------------------------------------------------------------------------
//  UTF-8 and wide strings
//
//  These went through QString until Phase 2 of doc/nexttoonz_core_noqt.md,
//  and they still give exactly what QString (Qt 5) gave, quirks included:
//  tests/core/test_tstring_qt.cpp compares them with the old code over all
//  short byte strings, every code point and random strings.
//
//  to_wstring(s) was QString::fromStdString(s).toStdWString(). Qt decodes
//  UTF-8 dropping a byte order mark at the very start, and replaces what it
//  cannot decode with U+FFFD: one for the first byte of a bad sequence, after
//  which decoding goes on from the next byte (so every stray continuation
//  byte gets its own); a sequence cut short by the end of the string gives
//  one U+FFFD for each of its bytes. Overlong forms, surrogates and code
//  points above U+10FFFF are bad sequences; noncharacters are not.
//
//  to_string(ws) was QString::fromStdWString(ws), then UTF-8. Qt reads the
//  wide string as UTF-32 (UTF-16 where wchar_t has 16 bits) with byte order
//  detection: a first U+FEFF is dropped, and a first 0xFFFE0000 (U+FEFF with
//  its bytes swapped) is dropped and makes it swap the bytes of everything
//  after it. Every code from 0x10000 up becomes a surrogate pair computed
//  without a range check, so codes above U+10FFFF turn into garbage
//  surrogates (a code like 0x4CA103AC even gives a NUL). On the way to UTF-8
//  a surrogate that is not half of a pair becomes '?', and the result ends at
//  its first NUL (to_wstring keeps NULs).
//-----------------------------------------------------------------------------

namespace {

const char32_t kReplacementCharacter = 0xFFFD;

inline bool isContinuationByte(unsigned char b) { return (b & 0xc0) == 0x80; }
inline bool isSurrogate(char32_t c) { return c >= 0xd800 && c <= 0xdfff; }
inline bool isHighSurrogate(char32_t c) { return c >= 0xd800 && c <= 0xdbff; }
inline bool isLowSurrogate(char32_t c) { return c >= 0xdc00 && c <= 0xdfff; }

std::u32string decodeUtf8(const std::string &s) {
  const unsigned char *src = (const unsigned char *)s.data();
  const unsigned char *end = src + s.size();
  if (s.size() >= 3 && src[0] == 0xef && src[1] == 0xbb && src[2] == 0xbf)
    src += 3;  // the byte order mark

  std::u32string out;
  out.reserve(end - src);
  while (src < end) {
    unsigned char b = *src++;
    if (b < 0x80) {
      out.push_back(b);
      continue;
    }

    int needed;  // bytes of the sequence, the lead byte included
    char32_t minimum, c;
    if (b < 0xc2) {  // a continuation byte, or C0/C1 (always overlong)
      out.push_back(kReplacementCharacter);
      continue;
    } else if (b < 0xe0)
      needed = 2, minimum = 0x80, c = b & 0x1f;
    else if (b < 0xf0)
      needed = 3, minimum = 0x800, c = b & 0x0f;
    else if (b < 0xf5)
      needed = 4, minimum = 0x10000, c = b & 0x07;
    else {
      out.push_back(kReplacementCharacter);
      continue;
    }

    long available = end - src;
    if (available < needed - 1) {
      // Too short: an error if what there is already is not a continuation,
      // otherwise the string ends inside the sequence.
      if ((available > 0 && !isContinuationByte(src[0])) ||
          (available > 1 && !isContinuationByte(src[1]))) {
        out.push_back(kReplacementCharacter);
        continue;
      }
      out.append(1 + available, kReplacementCharacter);
      break;
    }

    bool bad = false;
    for (int i = 0; i < needed - 1; ++i) {
      if (!isContinuationByte(src[i])) {
        bad = true;
        break;
      }
      c = (c << 6) | (src[i] & 0x3f);
    }
    if (bad || c < minimum || isSurrogate(c) || c > 0x10ffff) {
      out.push_back(kReplacementCharacter);
      continue;  // from the byte after the lead byte
    }
    out.push_back(c);
    src += needed - 1;
  }
  return out;
}

void appendUtf8(std::string &out, char32_t c) {
  if (c < 0x80)
    out.push_back(char(c));
  else if (c < 0x800) {
    out.push_back(char(0xc0 | (c >> 6)));
    out.push_back(char(0x80 | (c & 0x3f)));
  } else if (c < 0x10000) {
    out.push_back(char(0xe0 | (c >> 12)));
    out.push_back(char(0x80 | ((c >> 6) & 0x3f)));
    out.push_back(char(0x80 | (c & 0x3f)));
  } else {
    out.push_back(char(0xf0 | (c >> 18)));
    out.push_back(char(0x80 | ((c >> 12) & 0x3f)));
    out.push_back(char(0x80 | ((c >> 6) & 0x3f)));
    out.push_back(char(0x80 | (c & 0x3f)));
  }
}

// UTF-16 to UTF-8; a surrogate that is not half of a pair becomes '?'.
std::string encodeUtf8(const std::u16string &units) {
  std::string out;
  out.reserve(units.size());
  for (size_t i = 0; i < units.size(); ++i) {
    char16_t u = units[i];
    if (isHighSurrogate(u)) {
      if (i + 1 < units.size() && isLowSurrogate(units[i + 1])) {
        appendUtf8(out, 0x10000 + ((char32_t(u) - 0xd800) << 10) +
                            (char32_t(units[i + 1]) - 0xdc00));
        ++i;
      } else
        out.push_back('?');
    } else if (isLowSurrogate(u))
      out.push_back('?');
    else
      appendUtf8(out, u);
  }
  return out;
}

// What QString::fromStdWString makes of a wide string, as UTF-16.
std::u16string wideToUtf16(const std::wstring &ws) {
  std::u16string units;
  units.reserve(ws.size());
  size_t i = 0;
  bool swap = false;
  if (sizeof(wchar_t) == 4) {
    if (!ws.empty() && char32_t(ws[0]) == 0x0000feff)
      i = 1;
    else if (!ws.empty() && char32_t(ws[0]) == 0xfffe0000)
      i = 1, swap = true;
    for (; i < ws.size(); ++i) {
      char32_t c = char32_t(ws[i]);
      if (swap)
        c = (c >> 24) | ((c >> 8) & 0xff00) | ((c << 8) & 0xff0000) | (c << 24);
      if (c >= 0x10000) {
        units.push_back(char16_t((c >> 10) + 0xd7c0));
        units.push_back(char16_t((c % 0x400) + 0xdc00));
      } else
        units.push_back(char16_t(c));
    }
  } else {
    // Not compared with Qt yet (no Windows build here).
    if (!ws.empty() && char16_t(ws[0]) == 0xfeff)
      i = 1;
    else if (!ws.empty() && char16_t(ws[0]) == 0xfffe)
      i = 1, swap = true;
    for (; i < ws.size(); ++i) {
      char16_t u = char16_t(ws[i]);
      units.push_back(swap ? char16_t((u >> 8) | (u << 8)) : u);
    }
  }
  return units;
}

// UTF-16 to code points, the way QString::toStdWString (toUcs4) reads it: a
// surrogate that is not half of a pair becomes U+FFFD.
std::u32string utf16ToCodes(const std::u16string &units) {
  std::u32string codes;
  codes.reserve(units.size());
  for (size_t i = 0; i < units.size(); ++i) {
    char16_t u = units[i];
    if (isHighSurrogate(u) && i + 1 < units.size() &&
        isLowSurrogate(units[i + 1])) {
      codes.push_back(0x10000 + ((char32_t(u) - 0xd800) << 10) +
                      (char32_t(units[i + 1]) - 0xdc00));
      ++i;
    } else if (isSurrogate(u))
      codes.push_back(kReplacementCharacter);
    else
      codes.push_back(u);
  }
  return codes;
}

std::wstring codesToWide(const std::u32string &codes) {
  std::wstring ws;
  ws.reserve(codes.size());
  for (char32_t c : codes) {
    if (sizeof(wchar_t) == 4 || c < 0x10000)
      ws.push_back(wchar_t(c));
    else {
      ws.push_back(wchar_t(0xd800 + ((c - 0x10000) >> 10)));
      ws.push_back(wchar_t(0xdc00 + ((c - 0x10000) & 0x3ff)));
    }
  }
  return ws;
}

// The code points of a wide string that holds valid ones.
std::u32string wideToCodes(const std::wstring &ws) {
  if (sizeof(wchar_t) == 4) {
    std::u32string codes;
    codes.reserve(ws.size());
    for (wchar_t c : ws) codes.push_back(char32_t(c));
    return codes;
  }
  std::u16string units;
  units.reserve(ws.size());
  for (wchar_t c : ws) units.push_back(char16_t(c));
  return utf16ToCodes(units);
}

struct LowercaseMapping {
  char32_t from, to;
};

const LowercaseMapping kLowercaseTable[] = {
#include "tlowercasetable.inc"
};

}  // namespace

std::wstring toLowerAsQString(const std::wstring &s) {
  std::u32string out;
  for (char32_t c : wideToCodes(s)) {
    if (c == 0x130) {  // LATIN CAPITAL LETTER I WITH DOT ABOVE: i + U+0307
      out.push_back(0x69);
      out.push_back(0x307);
      continue;
    }
    const LowercaseMapping *end = std::end(kLowercaseTable);
    const LowercaseMapping *m   = std::lower_bound(
        std::begin(kLowercaseTable), end, c,
        [](const LowercaseMapping &a, char32_t b) { return a.from < b; });
    out.push_back(m != end && m->from == c ? m->to : c);
  }
  return codesToWide(out);
}

std::wstring roundTripAsQString(const std::wstring &s) {
  return codesToWide(utf16ToCodes(wideToUtf16(s)));
}

bool isValidAsQString(const std::wstring &s) {
  std::u16string units = wideToUtf16(s);
  for (size_t i = 0; i < units.size(); ++i) {
    if (isHighSurrogate(units[i]) && i + 1 < units.size() &&
        isLowSurrogate(units[i + 1]))
      ++i;
    else if (isSurrogate(units[i]))
      return false;
  }
  return true;
}

std::string toUtf8AsQString(const std::wstring &s) {
  std::string out;
  for (char32_t c : wideToCodes(s)) appendUtf8(out, c);
  return out;
}

std::wstring to_wstring(std::string s) {
  std::u32string codes = decodeUtf8(s);
  std::wstring ws;
  ws.reserve(codes.size());
  for (char32_t c : codes) {
    if (sizeof(wchar_t) == 4 || c < 0x10000)
      ws.push_back(wchar_t(c));
    else {
      ws.push_back(wchar_t(0xd800 + ((c - 0x10000) >> 10)));
      ws.push_back(wchar_t(0xdc00 + ((c - 0x10000) & 0x3ff)));
    }
  }
  return ws;
}

std::string to_string(std::wstring ws) {
  std::string s = encodeUtf8(wideToUtf16(ws));
  // The old code returned QString::toStdString() when the string was ASCII
  // (it compared the string with its Latin-1 bytes, measured up to the first
  // NUL), and otherwise built the std::string from the QByteArray's
  // const char *: either way the result ends at the first NUL.
  s.resize(strlen(s.c_str()));
  return s;
}

/*!
  The  default precision is six decimal places. If the
  precision is less than of the decimal places in the fractonal
  part, the remainder is not cut off but rounded.
*/

std::string to_string(double value, int prec) {
  if (prec < 0) {
    return std::to_string(value);
  }

  std::ostringstream out;
  out.setf(std::ios_base::fixed, std::ios_base::floatfield);
  out.precision(prec);
  out << value;
  return out.str();
}

std::string to_string(void* p) {
  std::ostringstream out;
  out << p;
  return out.str();
}

bool isInt(std::string s) {
  int i = 0, len = (int)s.size();
  if (len == 0) return false;
  if (s[0] == '-') {
    if (len == 1)
      return false;
    else
      i++;
  }

  while (i < len) {
    if (s[i] < '0' || s[i] > '9') return false;
    i++;
  }
  return true;
}

bool isDouble(std::string s) {
  int i = 0, len = (int)s.size();
  if (len == 0) return false;
  if (i < len && s[i] == '-') i++;
  while (i < len && s[i] != '.') {
    if (s[i] < '0' || s[i] > '9') return false;
    i++;
  }
  if (i >= len) return true;
  i++;
  while (i < len) {
    if (s[i] < '0' || s[i] > '9') return false;
    i++;
  }
  return true;
}

bool isInt(std::wstring s) { return isInt(::to_string(s)); }
bool isDouble(std::wstring s) { return isDouble(::to_string(s)); }

std::string toUpper(std::string a) {
#ifdef _WIN32
  size_t size      = a.size();
  const char* cstr = a.c_str();
  std::vector<char> buf(cstr, cstr + size + 1);
  return _strupr(&buf[0]);
#else
  std::string ret(a);
  for (int i = 0; i < (int)ret.length(); i++) ret[i] = toupper(ret[i]);
  return ret;
#endif
}

std::string toLower(std::string a) {
#ifdef _WIN32
  size_t size      = a.size();
  const char* cstr = a.c_str();
  std::vector<char> buf(cstr, cstr + size + 1);
  return _strlwr(&buf[0]);
#else
  std::string ret(a);
  for (int i = 0; i < (int)ret.length(); i++) ret[i] = tolower(ret[i]);
  return ret;
#endif
}

std::wstring toUpper(std::wstring a) {
#ifdef _WIN32
  size_t size         = a.size();
  const wchar_t* cstr = a.c_str();
  std::vector<wchar_t> buf(cstr, cstr + size + 1);
  return _wcsupr(&buf[0]);
#else
  std::wstring ret(a);
  for (int i = 0; i < (int)ret.length(); i++) ret[i] = towupper(ret[i]);
  return ret;
#endif
}

std::wstring toLower(std::wstring a) {
#ifdef _WIN32
  size_t size         = a.size();
  const wchar_t* cstr = a.c_str();
  std::vector<wchar_t> buf(cstr, cstr + size + 1);
  return _wcslwr(&buf[0]);
#else
  std::wstring ret(a);
  for (int i = 0; i < (int)ret.length(); i++) ret[i] = towlower(ret[i]);
  return ret;
#endif
}
