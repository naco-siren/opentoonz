// Unit tests for TFilePath and TFrameId (include/tfilepath.h).
//
// TFilePath is the path type used by every file format reader, the scene
// model and the project manager. It is also one of the core headers that
// currently leaks QString, so its behaviour is pinned down here before the
// Qt-free rewrite planned for Phase 2.

#include <gtest/gtest.h>

#include "tfilepath.h"
#include "tfiletype.h"

#include <string>

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
