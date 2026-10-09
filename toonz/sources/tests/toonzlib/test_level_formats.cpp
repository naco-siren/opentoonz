// Round-trip tests for the level formats of the reference project:
//   .pli  vector levels   (image/pli)
//   .tlv  Toonz raster    (image/tzl, compressed through the lzo helpers)
//   .png  full-colour frame sequences (image/png)
//
// Each test loads a committed fixture level, checks what the generator
// (tests/fixtures/gen_reference_project.cpp) put into it, writes it to a
// scratch directory with TLevelWriter, reloads the copy and compares it with
// the original. The comparisons are structural (strokes, styles) or exact
// pixel values, never images rendered through OpenGL.

#include "toonzlib_test_env.h"

#include <gtest/gtest.h>

#include "tlevel.h"
#include "tlevel_io.h"
#include "timage_io.h"
#include "tfilepath.h"
#include "tsystem.h"
#include "tpalette.h"
#include "tcolorstyles.h"
#include "tvectorimage.h"
#include "tstroke.h"
#include "tregion.h"
#include "ttoonzimage.h"
#include "trasterimage.h"
#include "traster.h"
#include "trastercm.h"
#include "tpixelcm.h"

#include <string>
#include <vector>

using nexttoonz_test::fixturePath;
using nexttoonz_test::makeScratchDir;

namespace {

const int kFrames = 8;
const int kWidth  = 320;
const int kHeight = 180;

// A level read frame by frame through TLevelReader.
struct LoadedLevel {
  TLevelP level;  // frame table and palette, as returned by loadInfo()
  std::vector<TFrameId> fids;
  std::vector<TImageP> images;  // one per fid, same order
};

LoadedLevel loadLevel(const TFilePath &path) {
  LoadedLevel out;
  TLevelReaderP reader(path);
  if (!reader) return out;
  out.level = reader->loadInfo();
  if (!out.level) return out;
  for (TLevel::Iterator it = out.level->begin(); it != out.level->end(); ++it) {
    out.fids.push_back(it->first);
    TImageReaderP frameReader = reader->getFrameReader(it->first);
    out.images.push_back(frameReader ? frameReader->load() : TImageP());
  }
  return out;
}

std::vector<TFrameId> expectedFids() {
  std::vector<TFrameId> fids;
  for (int f = 1; f <= kFrames; ++f) fids.push_back(TFrameId(f));
  return fids;
}

// Number of pixels that differ; dumps the first mismatch.
template <class Pixel, class Equal, class Describe>
int countPixelDifferences(const TRasterPT<Pixel> &a, const TRasterPT<Pixel> &b,
                          Equal equal, Describe describe) {
  if (a->getLx() != b->getLx() || a->getLy() != b->getLy()) return -1;
  int diffs = 0;
  a->lock();
  b->lock();
  for (int y = 0; y < a->getLy(); ++y) {
    const Pixel *pa = a->pixels(y);
    const Pixel *pb = b->pixels(y);
    for (int x = 0; x < a->getLx(); ++x) {
      if (equal(pa[x], pb[x])) continue;
      if (diffs == 0)
        ADD_FAILURE() << "first pixel difference at (" << x << ", " << y
                      << "): " << describe(pa[x]) << " vs " << describe(pb[x]);
      ++diffs;
    }
  }
  b->unlock();
  a->unlock();
  return diffs;
}

std::string describeCM(const TPixelCM32 &p) {
  return "ink " + std::to_string(p.getInk()) + " paint " +
         std::to_string(p.getPaint()) + " tone " + std::to_string(p.getTone());
}

std::string describeRGBM(const TPixel32 &p) {
  return "rgbm(" + std::to_string(p.r) + ", " + std::to_string(p.g) + ", " +
         std::to_string(p.b) + ", " + std::to_string(p.m) + ")";
}

int countCMDifferences(const TRasterCM32P &a, const TRasterCM32P &b) {
  return countPixelDifferences<TPixelCM32>(
      a, b,
      [](const TPixelCM32 &p, const TPixelCM32 &q) {
        return p.getValue() == q.getValue();
      },
      describeCM);
}

int countRGBMDifferences(const TRaster32P &a, const TRaster32P &b) {
  return countPixelDifferences<TPixel32>(
      a, b, [](const TPixel32 &p, const TPixel32 &q) { return p == q; },
      describeRGBM);
}

//-----------------------------------------------------------------------------
// Vector helpers

struct StrokeSummary {
  int styleId;
  int controlPointCount;
  bool selfLoop;
  std::vector<TThickPoint> points;
};

std::vector<StrokeSummary> summarizeStrokes(const TVectorImageP &vi) {
  std::vector<StrokeSummary> out;
  for (UINT i = 0; i < vi->getStrokeCount(); ++i) {
    const TStroke *s = vi->getStroke(i);
    StrokeSummary sum;
    sum.styleId           = s->getStyle();
    sum.controlPointCount = s->getControlPointCount();
    sum.selfLoop          = s->isSelfLoop();
    for (int p = 0; p < s->getControlPointCount(); ++p)
      sum.points.push_back(s->getControlPoint(p));
    out.push_back(sum);
  }
  return out;
}

std::vector<int> regionStyles(const TVectorImageP &vi) {
  std::vector<int> styles;
  for (UINT i = 0; i < vi->getRegionCount(); ++i)
    styles.push_back(vi->getRegion(i)->getStyle());
  return styles;
}

}  // namespace

//=============================================================================
// .pli

// vec_circle.pli: 8 frames, each a circle (closed stroke, 17 control points)
// whose region is filled, plus a thick open line (3 control points) added
// after the fill. The level palette is the default vector palette (styles 0
// and 1) plus the fill (style 2) and ink (style 3) the generator added.
TEST(PliLevel, FixtureContents) {
  LoadedLevel lvl = loadLevel(fixturePath("drawings/vec_circle.pli"));
  ASSERT_TRUE(lvl.level);
  EXPECT_EQ(lvl.fids, expectedFids());
  ASSERT_NE(lvl.level->getPalette(), nullptr);
  EXPECT_EQ(lvl.level->getPalette()->getStyleCount(), 4);

  const int kFillStyle = 2, kInkStyle = 3;
  for (size_t i = 0; i < lvl.images.size(); ++i) {
    SCOPED_TRACE("frame " + std::to_string(i + 1));
    TVectorImageP vi = lvl.images[i];
    ASSERT_TRUE(vi);
    ASSERT_EQ(vi->getStrokeCount(), 2u);

    const TStroke *circle = vi->getStroke(0);
    const TStroke *line   = vi->getStroke(1);
    EXPECT_TRUE(circle->isSelfLoop());
    EXPECT_FALSE(line->isSelfLoop());
    EXPECT_EQ(circle->getControlPointCount(), 17);
    EXPECT_EQ(line->getControlPointCount(), 3);
    EXPECT_EQ(circle->getStyle(), kInkStyle);
    EXPECT_EQ(line->getStyle(), kInkStyle);

    // The region topology is stored in the pli, so findRegions() keeps the
    // single filled region of the circle.
    vi->findRegions();
    ASSERT_EQ(vi->getRegionCount(), 1u);
    EXPECT_EQ(vi->getRegion(0)->getStyle(), kFillStyle);

    // Characterisation: the line crosses the circle on every frame, but the
    // generator added it after computing the regions. Computing regions
    // from scratch over the same two strokes splits the circle into two
    // (unfilled) regions, while the loaded image keeps one region even when
    // its regions are invalidated and recomputed: the intersection data read
    // from the pli drives the incremental recomputation.
    TVectorImageP fresh = new TVectorImage();
    for (UINT s = 0; s < vi->getStrokeCount(); ++s)
      fresh->addStroke(new TStroke(*vi->getStroke(s)));
    fresh->findRegions();
    EXPECT_EQ(fresh->getRegionCount(), 2u);

    vi->validateRegions(false);
    vi->findRegions();
    EXPECT_EQ(vi->getRegionCount(), 1u);
  }
}

TEST(PliLevel, RoundTripPreservesStrokesAndStyles) {
  LoadedLevel original = loadLevel(fixturePath("drawings/vec_circle.pli"));
  ASSERT_TRUE(original.level);
  ASSERT_EQ(original.images.size(), size_t(kFrames));
  TPalette *palette = original.level->getPalette();
  ASSERT_NE(palette, nullptr);

  const TFilePath copyPath = makeScratchDir("pli_roundtrip") + "copy.pli";
  {
    // The pli writer takes the palette from the images and writes the file
    // when it is destroyed.
    TLevelWriterP writer(copyPath);
    ASSERT_TRUE(writer);
    for (size_t i = 0; i < original.images.size(); ++i) {
      TVectorImageP vi = original.images[i];
      ASSERT_TRUE(vi);
      vi->setPalette(palette);
      writer->getFrameWriter(original.fids[i])->save(vi);
    }
  }
  ASSERT_TRUE(TFileStatus(copyPath).doesExist());

  LoadedLevel copy = loadLevel(copyPath);
  ASSERT_TRUE(copy.level);
  EXPECT_EQ(copy.fids, original.fids);
  ASSERT_EQ(copy.images.size(), original.images.size());
  ASSERT_NE(copy.level->getPalette(), nullptr);
  EXPECT_EQ(copy.level->getPalette()->getStyleCount(),
            palette->getStyleCount());

  for (size_t i = 0; i < original.images.size(); ++i) {
    SCOPED_TRACE("frame " + std::to_string(original.fids[i].getNumber()));
    TVectorImageP a = original.images[i];
    TVectorImageP b = copy.images[i];
    ASSERT_TRUE(b);

    std::vector<StrokeSummary> sa = summarizeStrokes(a);
    std::vector<StrokeSummary> sb = summarizeStrokes(b);
    ASSERT_EQ(sa.size(), sb.size());
    for (size_t s = 0; s < sa.size(); ++s) {
      SCOPED_TRACE("stroke " + std::to_string(s));
      EXPECT_EQ(sa[s].styleId, sb[s].styleId);
      EXPECT_EQ(sa[s].selfLoop, sb[s].selfLoop);
      ASSERT_EQ(sa[s].controlPointCount, sb[s].controlPointCount);
      for (size_t p = 0; p < sa[s].points.size(); ++p) {
        EXPECT_NEAR(sa[s].points[p].x, sb[s].points[p].x, 1e-3);
        EXPECT_NEAR(sa[s].points[p].y, sb[s].points[p].y, 1e-3);
        EXPECT_NEAR(sa[s].points[p].thick, sb[s].points[p].thick, 1e-3);
      }
    }

    a->findRegions();
    b->findRegions();
    EXPECT_EQ(a->getRegionCount(), b->getRegionCount());
    EXPECT_EQ(regionStyles(a), regionStyles(b));
  }
}

//=============================================================================
// .tlv

// tz_rect.tlv: 8 frames of 320x180 colour-mapped rasters. Style 2 is the
// paint, style 3 the ink (tz_rect.tpl).
TEST(TlvLevel, FixtureContents) {
  LoadedLevel lvl = loadLevel(fixturePath("drawings/tz_rect.tlv"));
  ASSERT_TRUE(lvl.level);
  EXPECT_EQ(lvl.fids, expectedFids());

  // The reader picks up tz_rect.tpl next to the level.
  TPalette *palette = lvl.level->getPalette();
  ASSERT_NE(palette, nullptr);
  EXPECT_EQ(palette->getStyleCount(), 4);

  for (size_t i = 0; i < lvl.images.size(); ++i) {
    SCOPED_TRACE("frame " + std::to_string(i + 1));
    TToonzImageP ti = lvl.images[i];
    ASSERT_TRUE(ti);
    EXPECT_EQ(ti->getSize(), TDimension(kWidth, kHeight));
    TRasterCM32P ras = ti->getRaster();
    ASSERT_TRUE(ras);
    EXPECT_EQ(ras->getLx(), kWidth);
    EXPECT_EQ(ras->getLy(), kHeight);
  }

  // Frame 1: the rectangle spans x 60..200, y 40..130 with a 3-pixel ink
  // border; its centre is pure paint (tone 255), its corner pure ink.
  TToonzImageP first = lvl.images[0];
  TRasterCM32P ras   = first->getRaster();
  ras->lock();
  const TPixelCM32 centre = ras->pixels(85)[130];
  const TPixelCM32 corner = ras->pixels(40)[60];
  const TPixelCM32 empty  = ras->pixels(5)[5];
  ras->unlock();
  EXPECT_EQ(centre.getPaint(), 2);
  EXPECT_EQ(centre.getTone(), 255);
  EXPECT_EQ(corner.getInk(), 3);
  EXPECT_EQ(corner.getPaint(), 2);
  EXPECT_EQ(corner.getTone(), 0);
  EXPECT_EQ(empty.getPaint(), 0);
  EXPECT_EQ(empty.getTone(), 255);
}

TEST(TlvLevel, RoundTripIsPixelExact) {
  LoadedLevel original = loadLevel(fixturePath("drawings/tz_rect.tlv"));
  ASSERT_TRUE(original.level);
  ASSERT_EQ(original.images.size(), size_t(kFrames));
  TPalette *palette = original.level->getPalette();
  ASSERT_NE(palette, nullptr);

  const TFilePath copyPath = makeScratchDir("tlv_roundtrip") + "copy.tlv";
  {
    TLevelWriterP writer(copyPath);
    ASSERT_TRUE(writer);
    writer->setPalette(palette);
    for (size_t i = 0; i < original.images.size(); ++i)
      writer->getFrameWriter(original.fids[i])->save(original.images[i]);
  }
  ASSERT_TRUE(TFileStatus(copyPath).doesExist());
  // The tlv writer saves the palette as copy.tpl next to the level.
  EXPECT_TRUE(TFileStatus(copyPath.withType("tpl")).doesExist());

  LoadedLevel copy = loadLevel(copyPath);
  ASSERT_TRUE(copy.level);
  EXPECT_EQ(copy.fids, original.fids);
  ASSERT_EQ(copy.images.size(), original.images.size());
  ASSERT_NE(copy.level->getPalette(), nullptr);
  EXPECT_EQ(copy.level->getPalette()->getStyleCount(),
            palette->getStyleCount());

  // Every frame, not only two: the comparison is cheap.
  for (size_t i = 0; i < original.images.size(); ++i) {
    SCOPED_TRACE("frame " + std::to_string(i + 1));
    TToonzImageP a = original.images[i];
    TToonzImageP b = copy.images[i];
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(b->getSize(), a->getSize());
    EXPECT_EQ(b->getSavebox(), a->getSavebox());
    EXPECT_EQ(countCMDifferences(a->getRaster(), b->getRaster()), 0);
  }
}

//=============================================================================
// .png frame sequence

// bg_gradient..png: 8 frames of 320x180 RGBM, opaque gradient, a moving
// white square and a half-transparent band.
TEST(PngLevel, FixtureContents) {
  LoadedLevel lvl = loadLevel(fixturePath("drawings/bg_gradient..png"));
  ASSERT_TRUE(lvl.level);
  EXPECT_EQ(lvl.fids, expectedFids());

  for (size_t i = 0; i < lvl.images.size(); ++i) {
    SCOPED_TRACE("frame " + std::to_string(i + 1));
    TRasterImageP ri = lvl.images[i];
    ASSERT_TRUE(ri);
    TRaster32P ras = ri->getRaster();
    ASSERT_TRUE(ras);
    EXPECT_EQ(ras->getLx(), kWidth);
    EXPECT_EQ(ras->getLy(), kHeight);

    // Square of frame f starts at x = 20 + 30 (f - 1), rows 60..119.
    const int sx = 20 + int(i) * 30;
    ras->lock();
    EXPECT_EQ(ras->pixels(90)[sx + 10], TPixel32(255, 255, 255, 255));
    // Gradient pixel at (0, 0): r = 0, g = 0, b = 128.
    EXPECT_EQ(ras->pixels(0)[0], TPixel32(0, 0, 128, 255));
    // Half-transparent band, rows 140..169. The generator wrote the
    // premultiplied pixel (0, 0, 128, 128); the png stores it
    // depremultiplied and the reader premultiplies it back.
    EXPECT_EQ(ras->pixels(150)[10], TPixel32(0, 0, 128, 128));
    ras->unlock();
  }
}

TEST(PngLevel, RoundTripIsPixelExact) {
  LoadedLevel original = loadLevel(fixturePath("drawings/bg_gradient..png"));
  ASSERT_TRUE(original.level);
  ASSERT_EQ(original.images.size(), size_t(kFrames));

  const TFilePath copyPath = makeScratchDir("png_roundtrip") + "copy..png";
  {
    TLevelWriterP writer(copyPath);
    ASSERT_TRUE(writer);
    for (size_t i = 0; i < original.images.size(); ++i)
      writer->getFrameWriter(original.fids[i])->save(original.images[i]);
  }
  EXPECT_TRUE(TFileStatus(copyPath.withFrame(TFrameId(1))).doesExist());

  LoadedLevel copy = loadLevel(copyPath);
  ASSERT_TRUE(copy.level);
  EXPECT_EQ(copy.fids, original.fids);
  ASSERT_EQ(copy.images.size(), original.images.size());

  for (size_t i = 0; i < original.images.size(); ++i) {
    SCOPED_TRACE("frame " + std::to_string(i + 1));
    TRasterImageP a = original.images[i];
    TRasterImageP b = copy.images[i];
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    TRaster32P ra = a->getRaster();
    TRaster32P rb = b->getRaster();
    ASSERT_TRUE(ra);
    ASSERT_TRUE(rb);
    EXPECT_EQ(countRGBMDifferences(ra, rb), 0);
  }
}
