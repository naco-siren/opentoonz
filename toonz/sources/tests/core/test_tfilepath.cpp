// Unit tests for TFilePath and TFrameId (include/tfilepath.h).
//
// TFilePath is the path type used by every file format reader, the scene
// model and the project manager. It is also one of the core headers that
// currently leaks QString, so its behaviour is pinned down here before the
// Qt-free rewrite planned for Phase 2.

#include <gtest/gtest.h>

#include "tfilepath.h"
#include "tfiletype.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

// Frame-number parsing in TFilePath consults the TFileType registry: only
// extensions declared as (non-level) image types are treated as frame
// sequences. The application fills that registry in initImageIo(); the test
// declares the handful of types it needs so this dependency stays explicit.
class FileTypeRegistry : public ::testing::Environment {
public:
  void SetUp() override {
    TFileType::declare("png", TFileType::RASTER_IMAGE);
    TFileType::declare("tif", TFileType::RASTER_IMAGE);
    TFileType::declare("pli", TFileType::VECTOR_LEVEL);
    TFileType::declare("tlv", TFileType::CMAPPED_LEVEL);
  }
};

const ::testing::Environment *const kRegistry =
    ::testing::AddGlobalTestEnvironment(new FileTypeRegistry);

}  // namespace

TEST(TFilePath, DecomposesNameTypeAndParent) {
  TFilePath fp("scenes/shot_01/bg.png");

  EXPECT_EQ(fp.getType(), "png");
  EXPECT_EQ(fp.getUndottedType(), "png");
  EXPECT_EQ(fp.getName(), "bg");
  EXPECT_EQ(fp.getDots(), ".");
  EXPECT_EQ(fp.getParentDir(), TFilePath("scenes/shot_01"));
  EXPECT_FALSE(fp.isEmpty());
}

TEST(TFilePath, NoExtension) {
  TFilePath fp("scenes/shot_01/README");
  EXPECT_EQ(fp.getType(), "");
  EXPECT_EQ(fp.getDots(), "");
  EXPECT_EQ(fp.getName(), "README");
}

TEST(TFilePath, WithTypeAndWithName) {
  TFilePath fp("levels/a.tlv");
  EXPECT_EQ(fp.withType("pli"), TFilePath("levels/a.pli"));
  EXPECT_EQ(fp.withName("b"), TFilePath("levels/b.tlv"));
  EXPECT_EQ(fp.withParentDir(TFilePath("other")), TFilePath("other/a.tlv"));
  EXPECT_EQ(fp.withoutParentDir(), TFilePath("a.tlv"));
}

TEST(TFilePath, Concatenation) {
  TFilePath dir("project/scenes");
  EXPECT_EQ(dir + TFilePath("shot.tnz"), TFilePath("project/scenes/shot.tnz"));
  EXPECT_EQ(dir + "shot.tnz", TFilePath("project/scenes/shot.tnz"));

  TFilePath accumulated("a");
  accumulated += TFilePath("b");
  accumulated += std::string("c.txt");
  EXPECT_EQ(accumulated, TFilePath("a/b/c.txt"));
}

TEST(TFilePath, FrameNumberRoundTrip) {
  TFilePath fp("drawings/a.0003.png");
  EXPECT_EQ(fp.getFrame().getNumber(), 3);
  EXPECT_EQ(fp.getDots(), "..");
  EXPECT_EQ(fp.getName(), "a");
  EXPECT_EQ(fp.getType(), "png");

  EXPECT_EQ(fp.withFrame(12), TFilePath("drawings/a.0012.png"));
  EXPECT_EQ(fp.withNoFrame(), TFilePath("drawings/a.png"));
  // withFrame() with no argument produces the "level" form a..png
  EXPECT_EQ(fp.withFrame().getDots(), "..");
}

TEST(TFilePath, LevelPathHasEmptyFrame) {
  TFilePath level("drawings/a..pli");
  EXPECT_EQ(level.getDots(), "..");
  EXPECT_TRUE(level.getFrame().isEmptyFrame());
  // pli is a single-file level format, so frame numbers never appear in
  // its file name: withFrame() leaves the level path untouched.
  EXPECT_EQ(level.withFrame(7), level);

  // A frame-sequence level, on the other hand, expands to per-frame files.
  TFilePath seq("drawings/a..png");
  EXPECT_TRUE(seq.getFrame().isEmptyFrame());
  EXPECT_EQ(seq.withFrame(7), TFilePath("drawings/a.0007.png"));
}

TEST(TFilePath, AbsoluteAndAncestor) {
  TFilePath abs("/tmp/project/scene.tnz");
  TFilePath rel("project/scene.tnz");
  EXPECT_TRUE(abs.isAbsolute());
  EXPECT_FALSE(rel.isAbsolute());

  EXPECT_TRUE(TFilePath("/tmp/project").isAncestorOf(abs));
  EXPECT_FALSE(TFilePath("/tmp/other").isAncestorOf(abs));
  EXPECT_EQ(abs - TFilePath("/tmp"), TFilePath("project/scene.tnz"));
}

TEST(TFilePath, Ordering) {
  EXPECT_TRUE(TFilePath("a.png") < TFilePath("b.png"));
  EXPECT_FALSE(TFilePath("b.png") < TFilePath("a.png"));
  EXPECT_TRUE(TFilePath("a.png") == TFilePath("a.png"));
  EXPECT_TRUE(TFilePath("a.png") != TFilePath("a.tif"));
}

TEST(TFrameId, ExpandPadsToFourDigits) {
  EXPECT_EQ(TFrameId(7).expand(), "0007");
  EXPECT_EQ(TFrameId(1234).expand(), "1234");
  EXPECT_EQ(TFrameId(7).expand(TFrameId::NO_PAD), "7");
}

TEST(TFrameId, ComparisonAndIncrement) {
  TFrameId a(3), b(4);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(a != b);
  ++a;
  EXPECT_TRUE(a == b);
}

TEST(TFrameId, ParsesFromString) {
  TFrameId fid("0012");
  EXPECT_EQ(fid.getNumber(), 12);
  EXPECT_FALSE(fid.isEmptyFrame());
}

// Project settings that change how file names are read
// (filepathproperties.h): TFilePath then parses with analyzePath(), which
// used QRegularExpression until migration step 4 of
// doc/nexttoonz_core_noqt.md. The expected values are what that code gave;
// test_tfilepath_qt.cpp compares the two implementations over far more
// names in the Qt build.
TEST(TFilePath, ProjectSettingsParsing) {
  // png comes from FileTypeRegistry above
  TFileType::declare("mp4", TFileType::RASTER_LEVEL);
  struct Case {
    bool nonAlphabetSuffix;
    int letterCount;
    bool noSeparator;
    const wchar_t *path, *levelName, *wideName;
    int frame;
    const wchar_t *letter;
    int padding;
    const char *dots;
  };
  const Case cases[] = {
      {false, 1, false, L"/d/a_0001.png", L"a_.png", L"a", 1, L"", 4, ".."},
      {false, 1, false, L"/d/a.0012b.png", L"a..png", L"a", 12, L"b", 4, ".."},
      {false, 1, false, L"/d/a0003.png", L"a0003.png", L"a0003", -2, L"", 0, "."},
      {false, 1, false, L"/d/a#.png", L"a#.png", L"a#", -2, L"", 0, "."},
      {false, 1, false, L"/d/a..png", L"a..png", L"a", -1, L"", 4, ".."},
      {false, 1, false, L"/d/a.png", L"a.png", L"a", -2, L"", 0, "."},
      {false, 1, false, L"/d/a_b_0001xyz.png", L"a_b_0001xyz.png",
       L"a_b_0001xyz", -2, L"", 0, "."},
      {false, 1, false, L"/d/clip.0001.mp4", L"clip.0001.mp4", L"clip.0001", -2,
       L"", 0, "."},
      {false, 1, false, L"/d/noext", L"noext", L"noext", -2, L"", 0, ""},
      {true, 0, true, L"/d/a0003.png", L"a#.png", L"a", 3, L"", 4, ".."},
      {true, 0, true, L"/d/a#.png", L"a#.png", L"a", -1, L"", 4, ".."},
      {true, 0, true, L"/d/a_b_0001xyz.png", L"a_b_.png", L"a_b", 1, L"xyz", 4,
       ".."},
      {true, 0, true, L"/d/clip.0001.mp4", L"clip.0001.mp4", L"clip.0001", -2,
       L"", 0, "."},
  };
  for (const Case &c : cases) {
    TFilePath::setFilePathProperties(false, c.nonAlphabetSuffix, c.letterCount,
                                     c.noSeparator);
    TFilePath p(c.path);
    TFrameId f = p.getFrame();
    EXPECT_EQ(p.getLevelNameW(), c.levelName) << c.path;
    EXPECT_EQ(p.getWideName(), c.wideName) << c.path;
    EXPECT_EQ(f.getNumber(), c.frame) << c.path;
    EXPECT_EQ(f.getLetterW(), c.letter) << c.path;
    EXPECT_EQ(f.getZeroPadding(), c.padding) << c.path;
    EXPECT_EQ(p.getDots(), c.dots) << c.path;
  }
  TFilePath::setFilePathProperties(true, false, 1, false);  // the defaults
}

TEST(TFrameId, LettersOrderAsQtDid) {
  // Empty letters first; otherwise the platform's collation, which puts
  // lower case before upper case and an accent after its base letter.
  EXPECT_TRUE(TFrameId(1) < TFrameId(1, 'a'));
  EXPECT_TRUE(TFrameId(1, 'a') < TFrameId(1, 'b'));
  EXPECT_TRUE(TFrameId(1, 'z') < TFrameId(2));
  EXPECT_FALSE(TFrameId(1, 'a') < TFrameId(1, 'a'));
#ifdef __APPLE__
  EXPECT_TRUE(TFrameId(1, 'a') < TFrameId(1, 'B'));
  EXPECT_TRUE(TFrameId(1, 'a') < TFrameId(1, 'A'));
  EXPECT_TRUE(TFrameId(1, std::wstring(L"e")) < TFrameId(1, std::wstring(L"é")));
  // Composed and decomposed forms are the same letter to the order.
  EXPECT_FALSE(TFrameId(1, std::wstring(L"é")) <
               TFrameId(1, std::wstring(L"é")));
  EXPECT_FALSE(TFrameId(1, std::wstring(L"é")) <
               TFrameId(1, std::wstring(L"é")));
#endif
}

// The order of frame letters, pinned (doc/nexttoonz_core_noqt.md, Q9): on
// Apple CoreFoundation's, which is what Qt gave; elsewhere the platform's
// (Qt's ICU while the core links Qt, then the C library's on Linux and
// CompareStringEx on Windows). What every platform agrees on is checked
// everywhere; the whole Apple order, mixed case included, on Apple only. On
// Linux the order of letters that differ in case or are not ASCII may
// change once the core stops linking Qt: an accepted difference.
TEST(TFrameId, LetterOrderIsPinned) {
  auto before = [](const wchar_t *a, const wchar_t *b) {
    return TFrameId(1, std::wstring(a)) < TFrameId(1, std::wstring(b));
  };
  // Everywhere: no letter first, then by letter within one case, a prefix
  // before what extends it, and the frame number before any letter.
  const wchar_t *lower[] = {L"", L"a", L"aa", L"ab", L"abc", L"b", L"ba",
                            L"z"};
  for (size_t i = 0; i + 1 < sizeof(lower) / sizeof(*lower); ++i) {
    EXPECT_TRUE(before(lower[i], lower[i + 1])) << i;
    EXPECT_FALSE(before(lower[i + 1], lower[i])) << i;
  }
  EXPECT_TRUE(before(L"A", L"B"));
  EXPECT_TRUE(before(L"AB", L"AC"));
  EXPECT_FALSE(before(L"ab", L"ab"));
  EXPECT_TRUE(TFrameId(1, std::wstring(L"z")) < TFrameId(2, std::wstring()));
  EXPECT_TRUE(TFrameId(1, std::wstring(L"Z")) < TFrameId(2, std::wstring(L"a")));
#ifdef __APPLE__
  // The whole order, mixed case included: a letter's two cases together,
  // lower case first.
  std::vector<std::wstring> letters = {
      L"z", L"Ab", L"", L"bA", L"B",  L"aA", L"ab", L"A",
      L"ba", L"Z", L"aa", L"abc", L"b", L"aB", L"Aa", L"a"};
  std::sort(letters.begin(), letters.end(),
            [](const std::wstring &a, const std::wstring &b) {
              return TFrameId(1, a) < TFrameId(1, b);
            });
  const std::vector<std::wstring> expected = {
      L"",   L"a",  L"A",   L"aa", L"aA", L"Aa", L"ab", L"aB",
      L"Ab", L"abc", L"b", L"B",  L"ba", L"bA", L"z",  L"Z"};
  EXPECT_EQ(letters, expected);
#endif
}
