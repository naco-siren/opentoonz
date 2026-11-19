// Unit tests for the string conversions of tconvert.h (common/tcore/
// tstring.cpp), without Qt: they run in the no-Qt build and in the iPad
// simulator. The expected values are what the old QString code gave; the
// Qt build also compares the two implementations directly over far more
// inputs (test_tstring_qt.cpp).

#include <gtest/gtest.h>

#include "tconvert.h"

#include <string>

namespace {

std::wstring w(std::initializer_list<unsigned> codes) {
  std::wstring s;
  for (unsigned c : codes) s.push_back(wchar_t(c));
  return s;
}

std::string b(std::initializer_list<unsigned> bytes) {
  std::string s;
  for (unsigned c : bytes) s.push_back(char(c));
  return s;
}

}  // namespace

TEST(TStringConvert, Utf8ToWide) {
  EXPECT_EQ(to_wstring(""), L"");
  EXPECT_EQ(to_wstring("abc"), L"abc");
  EXPECT_EQ(to_wstring("\xC3\xA9t\xC3\xA9"), w({0xe9, 't', 0xe9}));
  EXPECT_EQ(to_wstring("\xE6\x97\xA5\xE6\x9C\xAC"), w({0x65e5, 0x672c}));
  EXPECT_EQ(to_wstring("\xF0\x9F\x98\x80"),
            sizeof(wchar_t) == 4 ? w({0x1f600}) : w({0xd83d, 0xde00}));
  EXPECT_EQ(to_wstring(std::string("A\0B", 3)), w({'A', 0, 'B'}));
}

TEST(TStringConvert, Utf8ToWideDropsOneLeadingByteOrderMark) {
  EXPECT_EQ(to_wstring("\xEF\xBB\xBF" "A"), L"A");
  EXPECT_EQ(to_wstring("\xEF\xBB\xBF\xEF\xBB\xBF" "A"), w({0xfeff, 'A'}));
  EXPECT_EQ(to_wstring("A\xEF\xBB\xBF"), w({'A', 0xfeff}));
}

TEST(TStringConvert, Utf8ToWideReplacesWhatItCannotDecode) {
  const unsigned R = 0xfffd;
  EXPECT_EQ(to_wstring(b({0x80, 'A'})), w({R, 'A'}));
  EXPECT_EQ(to_wstring(b({0xff, 'A'})), w({R, 'A'}));
  // Cut short by the end: one for each byte.
  EXPECT_EQ(to_wstring(b({'A', 0xc2})), w({'A', R}));
  EXPECT_EQ(to_wstring(b({'A', 0xe2, 0x82})), w({'A', R, R}));
  // Cut short by another character: one for the lead byte, one for the
  // stray continuation byte.
  EXPECT_EQ(to_wstring(b({0xe2, 0x82, 'A'})), w({R, R, 'A'}));
  // Overlong, surrogate, above U+10FFFF, invalid lead: one per byte.
  EXPECT_EQ(to_wstring(b({0xc0, 0x80, 'A'})), w({R, R, 'A'}));
  EXPECT_EQ(to_wstring(b({0xe0, 0x80, 0x80, 'A'})), w({R, R, R, 'A'}));
  EXPECT_EQ(to_wstring(b({0xed, 0xa0, 0x80, 'A'})), w({R, R, R, 'A'}));
  EXPECT_EQ(to_wstring(b({0xf4, 0x90, 0x80, 0x80, 'A'})),
            w({R, R, R, R, 'A'}));
  EXPECT_EQ(to_wstring(b({0xf5, 0x80, 0x80, 0x80, 'A'})),
            w({R, R, R, R, 'A'}));
  // Noncharacters decode.
  EXPECT_EQ(to_wstring(b({0xef, 0xbf, 0xbe})), w({0xfffe}));
}

TEST(TStringConvert, WideToUtf8) {
  EXPECT_EQ(to_string(std::wstring()), "");
  EXPECT_EQ(to_string(L"abc"), "abc");
  EXPECT_EQ(to_string(w({0xe9})), "\xC3\xA9");
  EXPECT_EQ(to_string(w({0x65e5, 0x672c})), "\xE6\x97\xA5\xE6\x9C\xAC");
  EXPECT_EQ(to_string(w({0xfffe, 'A'})), "\xEF\xBF\xBE" "A");
  if (sizeof(wchar_t) == 4) {
    EXPECT_EQ(to_string(w({0x1f600})), "\xF0\x9F\x98\x80");
  }
}

TEST(TStringConvert, WideToUtf8EndsAtTheFirstNul) {
  EXPECT_EQ(to_string(w({'A', 0, 'B'})), "A");
  EXPECT_EQ(to_string(w({0, 'A'})), "");
  EXPECT_EQ(to_string(w({0xe9, 0, 'B'})), "\xC3\xA9");
}

TEST(TStringConvert, WideToUtf8ByteOrderMarks) {
  // A first U+FEFF is dropped.
  EXPECT_EQ(to_string(w({0xfeff, 'A'})), "A");
  EXPECT_EQ(to_string(w({'A', 0xfeff})), "A\xEF\xBB\xBF");
  if (sizeof(wchar_t) == 4) {
    // A first 0xFFFE0000 is dropped and swaps the bytes of the rest: 'A'
    // becomes 0x41000000, which Qt splits into two broken surrogates.
    EXPECT_EQ(to_string(w({0xfffe0000u, 'A'})), "\xE1\x9F\x80?");
  }
}

TEST(TStringConvert, WideToUtf8BadSurrogatesBecomeQuestionMarks) {
  EXPECT_EQ(to_string(w({0xd800, 'A'})), "?A");
  EXPECT_EQ(to_string(w({'A', 0xdc00})), "A?");
  if (sizeof(wchar_t) == 4) {
    // Above U+10FFFF: two low surrogates.
    EXPECT_EQ(to_string(w({0x110000, 'A'})), "??A");
  }
}

TEST(TStringConvert, RoundTrip) {
  const char *samples[] = {"", "plain", "caf\xC3\xA9", "\xE6\x97\xA5\xE6\x9C\xAC",
                           "\xF0\x9F\x98\x80 x", "tab\tnew\nline"};
  for (const char *s : samples) EXPECT_EQ(to_string(to_wstring(s)), s);
}
