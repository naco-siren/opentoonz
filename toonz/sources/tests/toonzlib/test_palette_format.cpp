// Round-trip tests for .tpl palettes (TPalette persisted through
// TIStream/TOStream).
//
// tz_rect.tpl is the palette of the tlv level built by the generator: the
// two default styles of a new Toonz raster palette plus the paint and ink
// styles the generator added.

#include "toonzlib_test_env.h"

#include <gtest/gtest.h>

#include "tpalette.h"
#include "tcolorstyles.h"
#include "tstream.h"
#include "tpersist.h"
#include "tfilepath.h"
#include "tsystem.h"

#include <string>
#include <vector>

using nexttoonz_test::fixturePath;
using nexttoonz_test::makeScratchDir;

namespace {

// Reads a palette the way TLevelReaderTzl::readPalette() does: match the
// <palette> tag and call loadData().
TPaletteP loadPaletteByTag(const TFilePath &path) {
  TIStream is(path);
  if (!is) return TPaletteP();
  std::string tagName;
  if (!is.matchTag(tagName) || tagName != "palette") return TPaletteP();
  TPaletteP palette(new TPalette());
  palette->loadData(is);
  is.matchEndTag();
  return palette;
}

// Reads a palette through the persist registry (operator>>(TPersist *&)),
// the way scene files load their objects.
TPaletteP loadPaletteByPersist(const TFilePath &path) {
  TIStream is(path);
  if (!is) return TPaletteP();
  TPersist *p = nullptr;
  is >> p;
  TPalette *palette = dynamic_cast<TPalette *>(p);
  if (!palette) delete p;
  return TPaletteP(palette);
}

void savePalette(TPalette *palette, const TFilePath &path) {
  TOStream os(path);
  os << palette;  // writes <palette id='1'> ... </palette>
}

void expectSamePalette(const TPaletteP &a, const TPaletteP &b) {
  ASSERT_EQ(a->getStyleCount(), b->getStyleCount());
  for (int i = 0; i < a->getStyleCount(); ++i) {
    SCOPED_TRACE("style " + std::to_string(i));
    const TColorStyle *sa = a->getStyle(i);
    const TColorStyle *sb = b->getStyle(i);
    ASSERT_NE(sa, nullptr);
    ASSERT_NE(sb, nullptr);
    EXPECT_EQ(sa->getMainColor(), sb->getMainColor());
    EXPECT_EQ(sa->getTagId(), sb->getTagId());
    EXPECT_EQ(sa->getName(), sb->getName());
  }
  ASSERT_EQ(a->getPageCount(), b->getPageCount());
  for (int p = 0; p < a->getPageCount(); ++p) {
    SCOPED_TRACE("page " + std::to_string(p));
    const TPalette::Page *pa = a->getPage(p);
    const TPalette::Page *pb = b->getPage(p);
    EXPECT_EQ(pa->getName(), pb->getName());
    ASSERT_EQ(pa->getStyleCount(), pb->getStyleCount());
    for (int i = 0; i < pa->getStyleCount(); ++i)
      EXPECT_EQ(pa->getStyleId(i), pb->getStyleId(i));
  }
}

}  // namespace

TEST(TplPalette, FixtureContents) {
  TPaletteP palette = loadPaletteByTag(fixturePath("drawings/tz_rect.tpl"));
  ASSERT_TRUE(palette);

  ASSERT_EQ(palette->getStyleCount(), 4);
  EXPECT_EQ(palette->getStyle(0)->getMainColor(), TPixel32(255, 255, 255, 0));
  EXPECT_EQ(palette->getStyle(1)->getMainColor(), TPixel32(0, 0, 0, 255));
  EXPECT_EQ(palette->getStyle(2)->getMainColor(), TPixel32(60, 160, 220, 255));
  EXPECT_EQ(palette->getStyle(3)->getMainColor(), TPixel32(10, 10, 10, 255));

  ASSERT_EQ(palette->getPageCount(), 1);
  EXPECT_EQ(palette->getPage(0)->getName(), L"colors");
  EXPECT_EQ(palette->getPage(0)->getStyleCount(), 4);
}

TEST(TplPalette, RoundTripPreservesStylesAndPages) {
  const TFilePath scratch                 = makeScratchDir("tpl_roundtrip");
  const std::vector<std::string> palettes = {
      "drawings/tz_rect.tpl",
      "palettes/Toonz_Raster_Palette.tpl",
      "palettes/Toonz_Vector_Palette.tpl",
  };
  for (const std::string &rel : palettes) {
    SCOPED_TRACE(rel);
    TPaletteP original = loadPaletteByTag(fixturePath(rel));
    ASSERT_TRUE(original);

    const TFilePath copyPath = scratch + TFilePath(rel).withoutParentDir();
    savePalette(original.getPointer(), copyPath);
    ASSERT_TRUE(TFileStatus(copyPath).doesExist());

    // Reload both ways: by tag and through the persist registry.
    TPaletteP byTag = loadPaletteByTag(copyPath);
    ASSERT_TRUE(byTag);
    expectSamePalette(original, byTag);

    TPaletteP byPersist = loadPaletteByPersist(copyPath);
    ASSERT_TRUE(byPersist);
    expectSamePalette(original, byPersist);
  }
}
