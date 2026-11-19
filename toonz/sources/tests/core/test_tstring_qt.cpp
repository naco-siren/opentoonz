// Differential test of the UTF-8 / wide string conversions of tconvert.h
// against the QString code they replaced (doc/nexttoonz_core_noqt.md,
// migration step 3). The old functions are kept here verbatim; every input
// must give the same result, byte for byte. Built in the Qt build only.

#include <gtest/gtest.h>

#include "tconvert.h"

#include <QString>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace {

// tstring.cpp before step 3.
std::wstring oldToWstring(std::string s) {
  QString testString = QString::fromStdString(s);
  QString qString    = QString::fromUtf8(s.c_str());

  // To detect if 's' is UTF-8 encoded or not
  if (qString != testString && std::string(qString.toUtf8()) == s)
    return qString.toStdWString();
  else
    return testString.toStdWString();
}

std::string oldToString(std::wstring ws) {
  QString const qString = QString::fromStdWString(ws);

  // Test if 'ws' is not unicode (UTF-8)
  if (qString.toLatin1() == qString) return qString.toStdString();

  return std::string(qString.toUtf8());
}

std::string hex(const std::string &s) {
  std::string out;
  char buf[4];
  for (unsigned char c : s) snprintf(buf, sizeof buf, "%02X ", c), out += buf;
  return out;
}

std::string hex(const std::wstring &s) {
  std::string out;
  char buf[12];
  for (wchar_t c : s) snprintf(buf, sizeof buf, "%X ", unsigned(c)), out += buf;
  return out;
}

// Counts mismatches and reports the first few.
struct Checker {
  int failures = 0;
  void toWstring(const std::string &s) {
    std::wstring a = oldToWstring(s), b = ::to_wstring(s);
    if (a != b && ++failures <= 5)
      ADD_FAILURE() << "to_wstring(" << hex(s) << "): Qt " << hex(a)
                    << "/ new " << hex(b);
  }
  void toString(const std::wstring &w) {
    std::string a = oldToString(w), b = ::to_string(w);
    if (a != b && ++failures <= 5)
      ADD_FAILURE() << "to_string(" << hex(w) << "): Qt " << hex(a)
                    << "/ new " << hex(b);
  }
};

// Bytes that matter to a UTF-8 decoder: ASCII, NUL, every continuation
// boundary, every lead byte boundary, the BOM's bytes, the invalid leads.
const std::vector<unsigned char> kInterestingBytes = {
    0x00, 0x41, 0x7f, 0x80, 0x8f, 0x90, 0x9f, 0xa0, 0xbb, 0xbf, 0xbe, 0xc0,
    0xc1, 0xc2, 0xc3, 0xdf, 0xe0, 0xe1, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1,
    0xf3, 0xf4, 0xf5, 0xf7, 0xf8, 0xfb, 0xfc, 0xfe, 0xff};

}  // namespace

TEST(TStringQtDifferential, ToWstringOnEveryStringOfUpToTwoBytes) {
  Checker check;
  check.toWstring("");
  for (int a = 0; a < 256; ++a) {
    check.toWstring(std::string(1, char(a)));
    for (int b = 0; b < 256; ++b) {
      std::string s;
      s += char(a), s += char(b);
      check.toWstring(s);
    }
  }
  EXPECT_EQ(check.failures, 0);
}

TEST(TStringQtDifferential, ToWstringOnInterestingStringsOfThreeToFiveBytes) {
  Checker check;
  const auto &v = kInterestingBytes;
  for (unsigned char a : v)
    for (unsigned char b : v)
      for (unsigned char c : v) {
        std::string s{char(a), char(b), char(c)};
        check.toWstring(s);
        for (unsigned char d : v) {
          check.toWstring(s + char(d));
          check.toWstring(s + char(d) + 'x');
        }
      }
  EXPECT_EQ(check.failures, 0);
}

TEST(TStringQtDifferential, ToWstringOnEveryEncodedCodePoint) {
  // The well-formed encoding of every code point, also with a BOM before it
  // and between ASCII; surrogates and the overlong forms come out of the
  // exhaustive tests above.
  Checker check;
  for (uint32_t c = 0; c <= 0x10ffff; ++c) {
    if (c >= 0xd800 && c <= 0xdfff) continue;
    QString q = QString::fromUcs4(&c, 1);
    std::string s = q.toStdString();
    check.toWstring(s);
    if (c % 7 == 0) {
      check.toWstring("\xEF\xBB\xBF" + s);
      check.toWstring("a" + s + "b");
    }
  }
  EXPECT_EQ(check.failures, 0);
}

TEST(TStringQtDifferential, ToWstringOnRandomStrings) {
  Checker check;
  std::mt19937 rng(20261118);
  std::uniform_int_distribution<int> length(0, 24), pick(0, 99),
      anyByte(0, 255);
  std::uniform_int_distribution<size_t> interesting(
      0, kInterestingBytes.size() - 1);
  for (int n = 0; n < 300000; ++n) {
    std::string s;
    int len = length(rng);
    for (int i = 0; i < len; ++i) {
      int p = pick(rng);
      if (p < 40)
        s += char(kInterestingBytes[interesting(rng)]);
      else if (p < 70)
        s += char(0x80 + anyByte(rng) % 64);  // continuation bytes
      else
        s += char(anyByte(rng));
    }
    check.toWstring(s);
  }
  EXPECT_EQ(check.failures, 0);
}

TEST(TStringQtDifferential, ToStringOnEveryCodeUnit) {
  // Every value a single wchar_t can hold up to U+10FFFF and some beyond,
  // alone, after ASCII and before ASCII (a first U+FEFF is a byte order
  // mark to Qt, so position matters).
  Checker check;
  check.toString(L"");
  auto one = [&](uint32_t c) {
    std::wstring w(1, wchar_t(c));
    check.toString(w);
    check.toString(L"a" + w);
    check.toString(w + L"a");
    check.toString(w + w);
  };
  for (uint32_t c = 0; c <= 0x10ffff; ++c) one(c);
  for (uint32_t c : {0x110000u, 0x1fffffu, 0x7fffffffu, 0x80000000u,
                     0xfffe0000u, 0xfeff0000u, 0xffffffffu, 0x41000000u})
    one(c);
  EXPECT_EQ(check.failures, 0);
}

TEST(TStringQtDifferential, ToStringOnRandomWideStrings) {
  Checker check;
  std::mt19937 rng(18112026);
  std::uniform_int_distribution<int> length(0, 12), pick(0, 99);
  std::uniform_int_distribution<uint32_t> bmp(0, 0xffff), any(0, 0x10ffff),
      wild(0, 0xffffffffu), surrogate(0xd800, 0xdfff);
  const uint32_t marks[] = {0xfeff, 0xfffe0000u, 0xfffe, 0xffff};
  for (int n = 0; n < 300000; ++n) {
    std::wstring w;
    int len = length(rng);
    for (int i = 0; i < len; ++i) {
      int p = pick(rng);
      uint32_t c = p < 30   ? 0x20 + p
                   : p < 55 ? bmp(rng)
                   : p < 75 ? any(rng)
                   : p < 88 ? surrogate(rng)
                   : p < 94 ? marks[p % 4]
                            : wild(rng);
      w.push_back(wchar_t(c));
    }
    check.toString(w);
  }
  EXPECT_EQ(check.failures, 0);
}
