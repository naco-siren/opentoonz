// Differential test of TFilePath and TFrameId against the code they had
// before migration step 4 of doc/nexttoonz_core_noqt.md, when they went
// through QString and QRegularExpression. legacy/legacy_tfilepath.* is that
// code verbatim, with the classes renamed LegacyTFilePath and
// LegacyTFrameId; both are fed the same paths under every combination of
// the path settings, and everything they say about each path must be the
// same, string for string. Built in the Qt build only.

#include <gtest/gtest.h>

#include "legacy/legacy_tfilepath.h"
#include "tconvert.h"
#include "tfilepath.h"
#include "tfiletype.h"

#include <QString>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <unistd.h>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

void registerTypes() {
  static bool done = false;
  if (done) return;
  done = true;
  using namespace TFileType;
  const std::pair<const char *, Type> types[] = {
      {"png", RASTER_IMAGE},  {"tif", RASTER_IMAGE},  {"tiff", RASTER_IMAGE},
      {"jpg", RASTER_IMAGE},  {"tga", RASTER_IMAGE},  {"exr", RASTER_IMAGE},
      {"tzp", CMAPPED_IMAGE}, {"svg", VECTOR_IMAGE},  {"mesh", MESH_IMAGE},
      {"tlv", CMAPPED_LEVEL}, {"pli", VECTOR_LEVEL},  {"tzl", RASTER_LEVEL},
      {"tpl", PALETTE_LEVEL}, {"psd", RASTER_LEVEL},  {"mov", RASTER_LEVEL},
      {"mp4", RASTER_LEVEL},  {"gif", RASTER_LEVEL},  {"webm", RASTER_LEVEL},
      {"avi", RASTER_LEVEL},  {"tzm", META_LEVEL},    {"wav", AUDIO_LEVEL}};
  for (auto &t : types) declare(t.first, t.second);
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

std::string esc(const std::string &s) { return esc(::to_wstring(s)); }

std::wstring letterOf(const TFrameId &f) { return f.getLetterW(); }
std::wstring letterOf(const LegacyTFrameId &f) {
  return f.getLetter().toStdWString();
}
unsigned sepOf(const TFilePath &p) { return unsigned(p.getSepCharW()); }
unsigned sepOf(const LegacyTFilePath &p) { return p.getSepChar().unicode(); }

template <class FID>
std::string describeFrame(const FID &f) {
  std::ostringstream o;
  o << f.getNumber() << "/" << esc(letterOf(f)) << "/" << f.getZeroPadding()
    << "/" << int(f.getStartSeqInd());
  for (auto fmt : {FID::FOUR_ZEROS, FID::NO_PAD, FID::CUSTOM_PAD,
                   FID::UNDERSCORE_FOUR_ZEROS})
    o << "/" << esc(f.expand(fmt));
  return o.str();
}

// Everything the class says about a path (or, not full, what it says about
// its name, frame and type).
template <class FP, class FID>
std::string describe(const FP &p, bool full) {
  std::ostringstream o;
  auto field = [&](const char *name, const std::function<std::string()> &f) {
    o << name << "=";
    try {
      o << f();
    } catch (...) {
      o << "<exception>";
    }
    o << "\n";
  };
  auto path = [](const FP &q) { return esc(q.getWideString()); };
  field("path", [&] { return path(p); });
  field("dots", [&] { return p.getDots(); });
  field("dottedType", [&] { return esc(p.getDottedType()); });
  field("undottedType", [&] { return esc(p.getUndottedType()); });
  field("type", [&] { return esc(p.getType()); });
  field("wideName", [&] { return esc(p.getWideName()); });
  field("name", [&] { return esc(p.getName()); });
  field("levelNameW", [&] { return esc(p.getLevelNameW()); });
  field("levelName", [&] { return esc(p.getLevelName()); });
  field("frame", [&] { return describeFrame(p.getFrame()); });
  field("isLevelName", [&] { return std::to_string(p.isLevelName()); });
  field("isFfmpeg", [&] { return std::to_string(p.isFfmpegType()); });
  field("isUneditable", [&] { return std::to_string(p.isUneditable()); });
  field("sepChar", [&] { return std::to_string(sepOf(p)); });
  if (!full) return o.str();
  field("qstring", [&] { return esc(p.getQString().toStdWString()); });
  field("parent", [&] { return path(p.getParentDir()); });
  field("withType png", [&] { return path(p.withType("png")); });
  field("withType .tif", [&] { return path(p.withType(".tif")); });
  field("withType ''", [&] { return path(p.withType("")); });
  field("withName nm", [&] { return path(p.withName(std::wstring(L"nm"))); });
  field("withName wide", [&] {
    return path(p.withName(std::wstring(L"日本_1")));
  });
  field("withName ''", [&] { return path(p.withName(std::wstring())); });
  field("withName bom", [&] {
    return path(p.withName(std::wstring(1, wchar_t(0xfeff)) + L"x"));
  });
  field("withFrame 7", [&] { return path(p.withFrame(FID(7))); });
  field("withFrame 12b", [&] { return path(p.withFrame(FID(12, 'b'))); });
  field("withFrame empty",
        [&] { return path(p.withFrame(FID(FID::EMPTY_FRAME))); });
  field("withFrame none", [&] { return path(p.withFrame(FID(FID::NO_FRAME))); });
  field("withFrame 3c_", [&] { return path(p.withFrame(FID(3, 'c', 0, '_'))); });
  field("withFrame 5 pad6",
        [&] { return path(p.withFrame(FID(5, 0, 6, '.'), FID::CUSTOM_PAD)); });
  field("withFrame 9 nosep",
        [&] { return path(p.withFrame(FID(9, 0, 4, '\0'))); });
  field("withNoFrame", [&] { return path(p.withNoFrame()); });
  return o.str();
}

struct Settings {
  bool standard, nonAlpha, noSep, underscore;
  int letters;
};

void apply(const Settings &s) {
  for (auto f : {&TFilePath::setFilePathProperties,
                 &LegacyTFilePath::setFilePathProperties})
    f(s.standard, s.nonAlpha, s.letters, s.noSep);
  TFilePath::setUnderscoreFormatAllowed(s.underscore);
  LegacyTFilePath::setUnderscoreFormatAllowed(s.underscore);
}

std::vector<Settings> allSettings() {
  std::vector<Settings> v;
  for (bool underscore : {true, false})
    for (bool noSep : {false, true}) {
      v.push_back({true, false, noSep, underscore, 1});
      for (bool nonAlpha : {false, true})
        for (int letters : {0, 1, 3}) v.push_back({false, nonAlpha, noSep,
                                                   underscore, letters});
    }
  return v;
}

std::string settingsName(const Settings &s) {
  std::ostringstream o;
  o << "standard=" << s.standard << " nonAlpha=" << s.nonAlpha
    << " letters=" << s.letters << " noSep=" << s.noSep
    << " underscore=" << s.underscore;
  return o.str();
}

// File names from parts that matter to the parsing.
std::vector<std::wstring> corpus() {
  const std::wstring names[]  = {L"",    L"a",  L"a b", L"a.b",
                                 L"a_b", L"x1", L"12",  L"a#",
                                 L"日本", L"a."};
  const std::wstring seps[]   = {L"", L".", L"_", L"#", L"..", L"._"};
  const std::wstring frames[] = {L"",     L"0001",        L"1",
                                 L"0001a", L"01ab",        L"1A",
                                 L"1abcd", L"1é",     L"99999999999",
                                 L"1 "};
  const std::wstring exts[]   = {L"",     L".png",       L".PNG",  L".tlv",
                                 L".pli", L".mp4",       L".GIF",  L".psd",
                                 L".jpg", L".pn g",      L".p_ng", L".Kng",
                                 L".İx", L".png\n", L"."};
  std::vector<std::wstring> v;
  for (auto &n : names)
    for (auto &s : seps)
      for (auto &f : frames)
        for (auto &e : exts) v.push_back(n + s + f + e);
  return v;
}

std::vector<std::wstring> randomCorpus(int count) {
  const wchar_t alphabet[] = L"aZ09._#- é日\n,:;\\/*?<>|\"";
  const int n              = sizeof(alphabet) / sizeof(wchar_t) - 1;
  std::mt19937 rng(4);
  std::uniform_int_distribution<int> len(0, 14), pick(0, n - 1),
      odd(0, 49);
  std::vector<std::wstring> v;
  for (int i = 0; i < count; ++i) {
    std::wstring s;
    int l = len(rng);
    for (int k = 0; k < l; ++k) {
      int o = odd(rng);
      if (o == 0)
        s.push_back(wchar_t(0xd800));  // a surrogate on its own
      else if (o == 1)
        s.push_back(wchar_t(0x1f600));
      else if (o == 2)
        s.push_back(wchar_t(0xfeff));
      else
        s.push_back(alphabet[pick(rng)]);
    }
    v.push_back(s);
  }
  return v;
}

// The settings the full comparison runs under: the default, and the
// project settings that change the parsing most.
std::vector<Settings> mainSettings() {
  return {{true, false, false, true, 1},  {false, false, false, true, 1},
          {false, true, true, true, 0},   {false, true, false, false, 3},
          {true, false, true, false, 1}};
}

// Both implementations warn on stderr about some of the paths; the
// comparison would bury the test's output under thousands of lines.
struct QuietStderr {
  int saved;
  QuietStderr() {
    fflush(stderr);
    saved    = dup(2);
    int null = open("/dev/null", O_WRONLY);
    dup2(null, 2);
    close(null);
  }
  ~QuietStderr() {
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
  }
};

// The old code is slow (it builds its regular expressions on every call,
// some 300 microseconds a path under project settings), so by default the
// tests take every few names of the corpora; NEXTTOONZ_FULL_DIFFERENTIAL=1
// takes them all (a few minutes).
bool fullRun() { return getenv("NEXTTOONZ_FULL_DIFFERENTIAL") != nullptr; }

std::vector<std::wstring> sample(const std::vector<std::wstring> &v,
                                 int stride) {
  if (fullRun()) return v;
  std::vector<std::wstring> out;
  for (size_t i = 0; i < v.size(); i += stride) out.push_back(v[i]);
  return out;
}

// Frame numbers of ten digits or more overflow an int in parseFrame(), the
// standard settings' parser, old and new alike: undefined behaviour, which
// the sanitizer build stops on (doc/nexttoonz_known_issues.md, item 27). The
// project settings' parser has no such problem, so those names are compared
// there only.
bool hasLongNumber(const std::wstring &w) {
  int run = 0;
  for (wchar_t c : w) {
    run = (c >= L'0' && c <= L'9') ? run + 1 : 0;
    if (run >= 10) return true;
  }
  return false;
}

int compareAll(const std::vector<std::wstring> &files,
               const std::vector<std::wstring> &dirs,
               const std::vector<Settings> &settings, bool full) {
  registerTypes();
  int failures = 0;
  for (const Settings &s : settings) {
    apply(s);
    for (auto &d : dirs)
      for (auto &f : files) {
        std::wstring w = d + f;
        if (s.standard && hasLongNumber(w)) continue;
        std::string a, b;
        {
          QuietStderr quiet;
          a = describe<LegacyTFilePath, LegacyTFrameId>(LegacyTFilePath(w),
                                                        full);
          b = describe<TFilePath, TFrameId>(TFilePath(w), full);
        }
        if (a != b && ++failures <= 3)
          ADD_FAILURE() << settingsName(s) << "\npath " << esc(w)
                        << "\n--- old\n" << a << "--- new\n" << b;
      }
  }
  apply({true, false, false, true, 1});  // the defaults
  return failures;
}

}  // namespace

TEST(TFilePathQtDifferential, GeneratedNamesFully) {
  EXPECT_EQ(compareAll(sample(corpus(), 11), {L"", L"d/e.f_1/"},
                       mainSettings(), true),
            0);
}

TEST(TFilePathQtDifferential, GeneratedNamesUnderAllSettings) {
  EXPECT_EQ(compareAll(sample(corpus(), 29), {L"/dir/"}, allSettings(), false),
            0);
}

TEST(TFilePathQtDifferential, RandomNames) {
  EXPECT_EQ(
      compareAll(sample(randomCorpus(4000), 20),
                 {L"", L"/x/", std::wstring(1, wchar_t(0xfeff)) + L"/y/"},
                 mainSettings(), true),
      0);
  EXPECT_EQ(compareAll(sample(randomCorpus(8000), 40), {L""}, allSettings(),
                       false),
            0);
}

TEST(TFilePathQtDifferential, FrameIdsFromStrings) {
  const char *samples[] = {"",     "1",    "0001", "0001a", "12b", "1A",
                           "1ab",  "a",    "-1",   "1.",    " 1",  "007",
                           "1\xC3\xA9", "999999999"};
  for (const char *s : samples)
    for (char sep : {'.', '_'}) {
      EXPECT_EQ(describeFrame(LegacyTFrameId(std::string(s), sep)),
                describeFrame(TFrameId(std::string(s), sep)))
          << s;
      EXPECT_EQ(describeFrame(LegacyTFrameId(::to_wstring(s), sep)),
                describeFrame(TFrameId(::to_wstring(s), sep)))
          << s;
    }
}

TEST(TFilePathQtDifferential, FrameIdOrder) {
  // Letters as they come out of file names, and some that test the
  // collation: case, accents, composed and decomposed forms, digits.
  const std::vector<std::wstring> letters = {
      L"",       L"a",          L"b",        L"A",          L"B",
      L"z",      L"Z",          L"aa",       L"ab",         L"aB",
      L"é", L"é",    L"e",        L"E",          L"É",
      L"1",      L"_",          L"#",        L"日",     L"ß",
      L"ss",     L"İ",     L"i",        L"K",     L"k",
      L"\U0001F600", L"ä", L"ä",  L"Å",     L"Å"};
  int failures = 0;
  for (auto &x : letters)
    for (auto &y : letters) {
      LegacyTFrameId la(3, QString::fromStdWString(x)),
          lb(3, QString::fromStdWString(y));
      TFrameId na(3, x), nb(3, y);
      bool before = la < lb, after = na < nb;
      bool eqBefore = la == lb, eqAfter = na == nb;
      if ((before != after || eqBefore != eqAfter) && ++failures <= 5)
        ADD_FAILURE() << esc(x) << " vs " << esc(y) << ": old <" << before
                      << " ==" << eqBefore << ", new <" << after << " =="
                      << eqAfter;
    }
  EXPECT_EQ(failures, 0);
  // The Qt members of TFrameId agree with the wide ones.
  TFrameId q(4, QString("xy"), 2, '_');
  EXPECT_EQ(q.getLetterW(), L"xy");
  EXPECT_EQ(q.getLetter(), QString("xy"));
}

TEST(TFilePathQtDifferential, RegExpStringIsUnchanged) {
  for (const Settings &s : allSettings()) {
    apply(s);
    EXPECT_EQ(TFilePath::fidRegExpStr(), LegacyTFilePath::fidRegExpStr());
  }
  apply({true, false, false, true, 1});
}
