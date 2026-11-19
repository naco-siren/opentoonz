// Differential test of the UTF-8 / wide string conversions of tconvert.h
// against the QString code they replaced (doc/nexttoonz_core_noqt.md,
// migration step 3). The old functions are kept here verbatim; every input
// must give the same result, byte for byte. Built in the Qt build only.

#include <gtest/gtest.h>

#include "tconvert.h"

#include <QDir>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
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

// equalsIgnoringCaseAsQString (step 6a, QFile::rename's case check) against
// QString::compare(..., Qt::CaseInsensitive) for every code point paired with
// its folding, lowercase and uppercase, and inside a longer name.
TEST(TStringQtDifferential, EqualsIgnoringCaseOnEveryCodePoint) {
  int checked = 0;
  for (uint c = 0; c <= 0x10ffff; ++c) {
    if (c >= 0xd800 && c <= 0xdfff) continue;
    QString a = QString::fromUcs4(&c, 1);
    uint f = QChar::toCaseFolded(c), l = QChar::toLower(c), u = QChar::toUpper(c);
    for (uint d : {f, l, u, c + 1}) {
      if (d >= 0xd800 && d <= 0xdfff) continue;
      if (d > 0x10ffff) continue;
      QString b = QString::fromUcs4(&d, 1);
      for (int wrap = 0; wrap < 2; ++wrap) {
        QString x = wrap ? "Ab" + a + "z" : a, y = wrap ? "aB" + b + "Z" : b;
        bool qt   = QString::compare(x, y, Qt::CaseInsensitive) == 0;
        ASSERT_EQ(equalsIgnoringCaseAsQString(x.toStdWString(), y.toStdWString()),
                  qt)
            << std::hex << c << " " << d << " " << wrap;
        ++checked;
      }
    }
  }
  EXPECT_GT(checked, 4000000);
}

//------------------------------------------------------------
// Step 8a: numbers and wildcards as Qt 5.15 wrote and read them

namespace {
std::string qtNumber(double v) {
  return QString::number(v, 'g', 17).toStdString();
}
std::vector<double> interestingDoubles() {
  const double inf = std::numeric_limits<double>::infinity();
  std::vector<double> v = {0.0, -0.0, 1.0, -1.0, 0.1, -0.1, 1.5, 2.5, 1e20,
      1e-5, 1e-4, 1e-3, 1e15, 1e16, 1e17, 1e18, 123456789012345678.0,
      9.999999999999999e16, 99999999999999984.0, 1.0 / 3.0, 2.0 / 3.0,
      0.9999999999999999, 1.0000000000000002, inf, -inf,
      std::numeric_limits<double>::quiet_NaN(),
      -std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::signaling_NaN(),
      std::numeric_limits<double>::denorm_min(),
      -std::numeric_limits<double>::denorm_min(),
      std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
      std::numeric_limits<double>::min(), std::numeric_limits<double>::epsilon(),
      1e-300, 1e300, 1e-310, 5e-324, 2.2250738585072009e-308};
  for (int e = -330; e <= 310; ++e) {
    v.push_back(std::pow(10.0, e));
    v.push_back(-std::pow(10.0, e) * 3.7);
    v.push_back(std::nextafter(std::pow(10.0, e), inf));
  }
  return v;
}
}  // namespace

TEST(TStringQtDifferential, NumberAsQtWroteIt) {
  // measured on Qt 5.15.19: both zeros give "0", any NaN gives "nan"
  EXPECT_EQ(toStringAsQString(-0.0), "0");
  EXPECT_EQ(qtNumber(-0.0), "0");
  EXPECT_EQ(toStringAsQString(-std::numeric_limits<double>::quiet_NaN()), "nan");
  EXPECT_EQ(toStringAsQString(-std::numeric_limits<double>::infinity()), "-inf");
  EXPECT_EQ(toStringAsQString(1e20), "1e+20");
  EXPECT_EQ(toStringAsQString(1e-5), "1.0000000000000001e-05");
  EXPECT_EQ(toStringAsQString(0.1), "0.10000000000000001");
  // an exact tie at the 18th digit goes away from zero (printf: to even)
  EXPECT_EQ(qtNumber(-121544296184850.125), "-121544296184850.13");
  EXPECT_EQ(toStringAsQString(-121544296184850.125), "-121544296184850.13");
  EXPECT_EQ(toStringAsQString(121544296184850.125), "121544296184850.13");
  for (double d : interestingDoubles())
    ASSERT_EQ(toStringAsQString(d), qtNumber(d)) << d;
  std::mt19937_64 rng(8);
  for (int i = 0; i < 1000000; ++i) {
    uint64_t bits = rng();
    double d;
    std::memcpy(&d, &bits, 8);
    ASSERT_EQ(toStringAsQString(d), qtNumber(d)) << std::hex << bits;
    double nice = double(int64_t(rng() % 2000001) - 1000000) /
                  std::pow(10.0, double(rng() % 12));
    ASSERT_EQ(toStringAsQString(nice), qtNumber(nice)) << nice;
  }
}

namespace {
void expectToDoubleAsQt(const std::string &s) {
  bool okQt = false, ok = false;
  double q = QString::fromStdString(s).toDouble(&okQt);
  double n = toDoubleAsQString(s, &ok);
  ASSERT_EQ(ok, okQt) << "\"" << s << "\"";
  if (ok) {
    if (std::isnan(q))
      ASSERT_TRUE(std::isnan(n)) << s;
    else
      ASSERT_EQ(std::memcmp(&q, &n, 8), 0) << s << " " << q << " " << n;
  }
}
}  // namespace

TEST(TStringQtDifferential, DoubleAsQtReadIt) {
  // measured on Qt 5.15.19
  bool ok = true;
  EXPECT_EQ(toDoubleAsQString("1e-400", &ok), 0);
  EXPECT_FALSE(ok);  // an underflow is not ok
  toDoubleAsQString("1e400", &ok);
  EXPECT_FALSE(ok);  // nor an overflow
  toDoubleAsQString("+nan", &ok);
  EXPECT_FALSE(ok);
  toDoubleAsQString("infinity", &ok);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(std::signbit(toDoubleAsQString("-0", &ok)));
  EXPECT_TRUE(ok);
  EXPECT_EQ(toDoubleAsQString("\xc2\xa0" "3 ", &ok), 3);  // NBSP, space
  EXPECT_TRUE(ok);
  // U+2212 MINUS SIGN wherever '-' goes; U+FEFF is not a space (a leading
  // one is a byte order mark, which fromStdString drops)
  EXPECT_EQ(toDoubleAsQString("\xe2\x88\x92" "2.5", &ok), -2.5);
  EXPECT_TRUE(ok);
  EXPECT_EQ(toDoubleAsQString("1e" "\xe2\x88\x92" "1", &ok), 0.1);
  EXPECT_TRUE(ok);
  toDoubleAsQString("5" "\xef\xbb\xbf", &ok);
  EXPECT_FALSE(ok);
  EXPECT_EQ(toIntAsQString("\xe2\x88\x92" "7", &ok), -7);
  EXPECT_TRUE(ok);

  const char *corpus[] = {"1.5", " 1.5 ", "1.5x", "+1.5", "-0", ".5", "5.",
      "1e5", "1E5", "1e+05", "inf", "INF", "Inf", "-inf", "+inf", "nan",
      "NaN", "+nan", "-nan", "1e400", "-1e400", "1e-400", "-1e-400", "0x10",
      "", "  ", "1,5", "\t2\n", "1.5e", "e5", "infinity", "\xd9\xa3",
      "\xc2\xa0" "3", "3" "\xe2\x80\x83", "1_0", "00012", "1.2.3", "--1",
      "+-1", "1e5.5", ".", "-.", "+.e1", "1e", "1e+", "1e-0", "0e0", "1.e3",
      ".e3", "1 2", "1\x00" "2", "\xef\xbb\xbf" "1", "1\xef\xbb\xbf", "1e310", "4e-320",
      "\xe2\x88\x92" "inf", "\xe2\x88\x92" "nan", "\xe2\x88\x92" ".5",
      " \xe2\x88\x92" "1", "\xe2\x88\x92" " 1", "+\xe2\x88\x92" "1",
      "2.2250738585072011e-308", "1e-323", "1e-324", "3e-324"};
  for (const char *s : corpus) expectToDoubleAsQt(s);
  // every code point before and after a number (which ones are spaces)
  for (char32_t c = 1; c < 0x3100; ++c) {
    std::wstring w(1, wchar_t(c));
    std::string ch = ::to_string(w);
    expectToDoubleAsQt(ch + "2.5");
    expectToDoubleAsQt("2.5" + ch);
  }
  // what number() writes reads back
  for (double d : interestingDoubles()) expectToDoubleAsQt(qtNumber(d));
  std::mt19937_64 rng(9);
  for (int i = 0; i < 300000; ++i) {
    uint64_t bits = rng();
    double d;
    std::memcpy(&d, &bits, 8);
    expectToDoubleAsQt(qtNumber(d));
  }
  // random strings over the characters numbers are made of
  const char alphabet[] = "0123456789.eE+- infaNI";
  for (int i = 0; i < 300000; ++i) {
    std::string s;
    int n = int(rng() % 10);
    for (int k = 0; k < n; ++k) s += alphabet[rng() % (sizeof alphabet - 1)];
    expectToDoubleAsQt(s);
  }
}

TEST(TStringQtDifferential, IntAsQtReadIt) {
  auto expectAsQt = [](const std::string &s) {
    bool okQt = false, ok = false;
    int q = QString::fromStdString(s).toInt(&okQt);
    int n = toIntAsQString(s, &ok);
    ASSERT_EQ(ok, okQt) << "\"" << s << "\"";
    ASSERT_EQ(n, q) << "\"" << s << "\"";
  };
  for (const char *s : {"0", "12", " 12 ", "+12", "-12", "007", "", " ",
                        "\xe2\x88\x92" "0", " \xe2\x88\x92" "1", "\xef\xbb\xbf" "4",
                        "1.0", "1e3", "0x10", "2147483647", "2147483648",
                        "-2147483648", "-2147483649", "99999999999", "--1",
                        "+", "-", "1 2", "\xc2\xa0" "5", "\xd9\xa3", "12a"})
    expectAsQt(s);
  std::mt19937_64 rng(10);
  const char alphabet[] = "0123456789+- x";
  for (int i = 0; i < 200000; ++i) {
    std::string s;
    int n = int(rng() % 12);
    for (int k = 0; k < n; ++k) s += alphabet[rng() % (sizeof alphabet - 1)];
    expectAsQt(s);
  }
}

TEST(TStringQtDifferential, WildcardsAsQDirMatched) {
  auto expectAsQt = [](const std::vector<std::wstring> &filters,
                       const std::wstring &name) {
    QStringList q;
    for (const std::wstring &f : filters) q << QString::fromStdWString(f);
    ASSERT_EQ(matchesWildcardsAsQDir(filters, name),
              QDir::match(q, QString::fromStdWString(name)))
        << ::to_string(filters.empty() ? L"" : filters[0]) << " ~ "
        << ::to_string(name);
  };
  // the filters TrailStyles uses, on names of every kind
  std::vector<std::wstring> trail;
  for (const char *f : {"*.pli", "*.tlv", "*.png", "*.tif", "*.tiff", "*.tga",
                        "*.sgi", "*.rgb", "*.pct", "*.pic", "*.pict", "*.exr",
                        "*.bmp", "*.jpg", "*.jpeg", "*.nol"})
    trail.push_back(::to_wstring(f));
  std::vector<std::wstring> names = {L"a.pli", L"A.PLI", L".png", L"a.pl",
      L"a.pngx", L"x.tlv.png", L"ſ.png", L"a.ſgi", L"a.Kli",
      L"a.tİf", L"a.tıf", L"café.JPG", L"", L"png", L"a/b.png",
      L"日本.TIFF"};
  for (const std::wstring &n : names) expectAsQt(trail, n);
  // every code point in place of each letter of an extension
  for (char32_t c = 1; c < 0x10000; ++c) {
    if (c >= 0xd800 && c <= 0xdfff) continue;
    std::wstring ch(1, wchar_t(c));
    expectAsQt({L"*.sgi"}, L"x." + ch + L"gi");
    expectAsQt({L"*.pict"}, L"x.pi" + ch + L"t");
    expectAsQt({L"[a-k]?"}, ch + L"z");
  }
  // the other wildcards and their corners
  const wchar_t *patterns[] = {L"?", L"??", L"a?c", L"*", L"**", L"a*", L"*a",
      L"[abc]", L"[!abc]", L"[^abc]", L"[a-c]x", L"[]a]", L"[!]a]", L"[a-]",
      L"[-a]", L"[c-a]", L"[a", L"a]", L"[a/b]", L"\\a", L"a\\", L"a.b",
      L"a+b", L"(a)", L"a|b", L"a$", L"^a", L"{a}", L"[\\]]", L"[[]", L"",
      L"*.[pP][lL][iI]", L"[A-Z]*"};
  const wchar_t *cases[] = {L"", L"a", L"A", L"b", L"ab", L"abc", L"ABC",
      L"aXc", L"x", L"]", L"]a", L"-", L"a.b", L"aXb", L"a+b", L"(a)", L"a|b",
      L"a$", L"^a", L"{a}", L"\\a", L"a\\", L"\\", L"[", L"a/c", L"z.PLI",
      L"é", L"K", L"Q"};
  for (const wchar_t *p : patterns)
    for (const wchar_t *n : cases) expectAsQt({p}, n);
  expectAsQt({L"*.x", L"*.y"}, L"a.y");
  expectAsQt({}, L"a");
}
