// The functions that draw a vector drawing, or one stroke or region of it,
// into a raster (migration step 6 of doc/nexttoonz_canvas_ir.md), with the
// CPU rasteriser selected. These tests run without a window system and so
// without an OpenGL context: had any of them fallen back on TOfflineGL, it
// would have had none to draw with.

#include <gtest/gtest.h>

#include "toonzlib_test_env.h"

#include "tcolorstyles.h"
#include "tpalette.h"
#include "traster.h"
#include "trasterimage.h"
#include "tregion.h"
#include "tsimplecolorstyles.h"
#include "tstroke.h"
#include "ttoonzimage.h"
#include "tvectorimage.h"
#include "tvectorrasterizer.h"

#include "toonz/toonzimageutils.h"
#include "toonz/trasterimageutils.h"

#include <cmath>
#include <vector>

namespace {

TStroke *circle(const TPointD &c, double r, double thick, int style) {
  std::vector<TThickPoint> pts;
  const int n       = 8;
  const double step = 2.0 * std::acos(-1.0) / n;
  for (int i = 0; i <= n; ++i) {
    const double a = i * step, am = a + step / 2, cr = r / std::cos(step / 2);
    pts.push_back(
        TThickPoint(c.x + r * std::cos(a), c.y + r * std::sin(a), thick));
    if (i < n)
      pts.push_back(TThickPoint(c.x + cr * std::cos(am),
                                c.y + cr * std::sin(am), thick));
  }
  pts.back()  = pts.front();
  TStroke *s = new TStroke(pts);
  s->setSelfLoop(true);
  s->setStyle(style);
  return s;
}

TStroke *line(const TPointD &a, const TPointD &b, double thick, int style) {
  std::vector<TThickPoint> pts = {TThickPoint(a, thick),
                                  TThickPoint((a + b) * 0.5, thick),
                                  TThickPoint(b, thick)};
  TStroke *s = new TStroke(pts);
  s->setStyle(style);
  return s;
}

// The CPU rasteriser selected for the test, and deselected afterwards.
class OffscreenCanvas : public ::testing::Test {
protected:
  bool m_wasSelected = false;  // put back afterwards
  TPaletteP m_palette;
  int m_ink = 0, m_thinInk = 0, m_paint = 0;
  TVectorImageP m_vi;

  void SetUp() override {
    if (!TVectorRasterizer::isAvailable())
      GTEST_SKIP() << "this build has no rasteriser but OpenGL";
    m_wasSelected = TVectorRasterizer::isSelected();
    TVectorRasterizer::select(true);

    m_palette = new TPalette();
    auto add  = [&](const TPixel32 &color) {
      const int id = m_palette->addStyle(color);
      m_palette->getPage(0)->addStyle(id);
      return id;
    };
    m_ink     = add(TPixel32(20, 20, 120, 255));
    m_thinInk = add(TPixel32(200, 30, 30, 255));
    m_paint   = add(TPixel32(250, 210, 60, 255));

    // A filled disc 60 across with an outline 3 thick, and across it a
    // line thinner than a pixel.
    m_vi = new TVectorImage();
    m_vi->setPalette(m_palette.getPointer());
    m_vi->addStroke(circle(TPointD(0, 0), 30, 3.0, m_ink));
    m_vi->findRegions();
    for (UINT i = 0; i < m_vi->getRegionCount(); ++i)
      m_vi->getRegion(i)->setStyle(m_paint);
    m_vi->addStroke(line(TPointD(-45, 20), TPointD(45, 20), 0.3, m_thinInk));
  }

  void TearDown() override { TVectorRasterizer::select(m_wasSelected); }
};

}  // namespace

// The conversion the colour-map fxs need on vector levels: paint where the
// disc is, ink along the outline and along the thin line, each stroke's
// coverage the tone.
TEST_F(OffscreenCanvas, AVectorImageBecomesAToonzImage) {
  TToonzImageP ti = ToonzImageUtils::vectorToToonzImage(
      m_vi, TAffine(), m_palette.getPointer(), TPointD(-50, -50),
      TDimension(100, 100), nullptr, true);
  ASSERT_TRUE(ti);
  TRasterCM32P ras = ti->getRaster();

  // The middle of the disc: paint, no ink.
  const TPixelCM32 middle = ras->pixels(50)[50];
  EXPECT_EQ(middle.getPaint(), m_paint);
  EXPECT_EQ(middle.getTone(), 255);
  // The outline at the right: the ink, fully.
  const TPixelCM32 outline = ras->pixels(50)[80];
  EXPECT_EQ(outline.getInk(), m_ink);
  EXPECT_LT(outline.getTone(), 255);
  // Outside: nothing.
  EXPECT_EQ(ras->pixels(5)[5].getPaint(), 0);
  EXPECT_EQ(ras->pixels(5)[5].getTone(), 255);

  // The line thinner than a pixel is there all along: grown by half a
  // pixel as it is through OpenGL, not broken into pieces.
  int covered = 0;
  for (int x = 10; x < 90; ++x) {
    bool hit = false;
    for (int y = 66; y <= 73; ++y) {
      const TPixelCM32 p = ras->pixels(y)[x];
      hit = hit || (p.getInk() == m_thinInk && p.getTone() < 255);
    }
    covered += hit;
  }
  EXPECT_EQ(covered, 80);
}

// A stroke added to a Toonz image, with and without antialiasing, and the
// colours under a stroke changed.
TEST_F(OffscreenCanvas, StrokesAreAddedToAToonzImage) {
  for (bool antialias : {true, false}) {
    SCOPED_TRACE(antialias ? "antialiased" : "aliased");
    TRasterCM32P ras(100, 100);
    ras->clear();
    TToonzImageP ti(ras, ras->getBounds());
    ti->setPalette(m_palette.getPointer());

    // (The clip is in the raster's pixels; an empty one draws nothing.)
    ToonzImageUtils::addInkStroke(ti, m_vi->getStroke(0), m_ink, false, false,
                                  TRectD(0, 0, 100, 100), antialias);
    int ink = 0, partial = 0;
    for (int y = 0; y < 100; ++y)
      for (int x = 0; x < 100; ++x) {
        const TPixelCM32 p = ras->pixels(y)[x];
        if (p.getInk() == m_ink && p.getTone() < 255) ++ink;
        if (p.getTone() > 0 && p.getTone() < 255) ++partial;
      }
    // A ring about 190 around and 6 across (a thickness of 3 is how far
    // the outline reaches on either side).
    EXPECT_GT(ink, 1000);
    EXPECT_LT(ink, 1800);
    if (antialias)
      EXPECT_GT(partial, 0);
    else
      EXPECT_EQ(partial, 0) << "without antialiasing a pixel is ink or not";
  }

  // The paint under a thick line across the image changed.
  TRasterCM32P ras(100, 100);
  ras->fill(TPixelCM32(0, m_paint, 255));
  TToonzImageP ti(ras, ras->getBounds());
  ti->setPalette(m_palette.getPointer());
  TStroke *across = line(TPointD(-50, 0), TPointD(50, 0), 6.0, 1);
  ToonzImageUtils::ChangeColorStrokeSettings settings;
  settings.stroke      = across;
  settings.colorId     = m_thinInk;
  settings.maskPaintId = -1;
  settings.changeInk   = false;
  settings.changePaint = true;
  ToonzImageUtils::changeColorStroke(ti, settings);
  delete across;
  EXPECT_EQ(ras->pixels(50)[50].getPaint(), m_thinInk);
  EXPECT_EQ(ras->pixels(80)[50].getPaint(), m_paint);
}

// The same on a full colour image: the stroke in its colour, at half
// opacity when asked.
TEST_F(OffscreenCanvas, StrokesAreAddedToAFullColourImage) {
  for (double opacity : {1.0, 0.5}) {
    SCOPED_TRACE(opacity);
    TRaster32P ras(100, 100);
    ras->fill(TPixel32::White);
    TRasterImageP ri(ras);
    // A copy: at an opacity under 1, addStroke() changes the colour of the
    // stroke's style in the palette it is given, and leaves it changed.
    TPaletteP copy = m_palette->clone();
    ri->setPalette(copy.getPointer());
    TStroke *across = line(TPointD(-40, 0), TPointD(40, 0), 4.0, m_ink);
    TRasterImageUtils::addStroke(ri, across, TRectD(), opacity, true);
    delete across;
    const TPixel32 p = ras->pixels(50)[50];
    if (opacity == 1.0)
      EXPECT_EQ(p, TPixel32(20, 20, 120, 255));
    else {
      EXPECT_NEAR(p.r, (20 + 255) / 2, 2);
      EXPECT_NEAR(p.b, (120 + 255) / 2, 2);
    }
    EXPECT_EQ(ras->pixels(80)[50], TPixel32::White);
  }

  TRasterImageP full = TRasterImageUtils::vectorToFullColorImage(
      m_vi, TAffine(), m_palette.getPointer(), TPointD(-50, -50),
      TDimension(100, 100), nullptr, true);
  ASSERT_TRUE(full);
  TRaster32P ras = full->getRaster();
  EXPECT_EQ(ras->pixels(50)[50], TPixel32(250, 210, 60, 255)) << "the paint";
  EXPECT_EQ(ras->pixels(50)[80], TPixel32(20, 20, 120, 255)) << "the ink";
}

// TVectorImage::render, which the clipboard and the histogram use.
TEST_F(OffscreenCanvas, AnImageRendersItself) {
  const TRaster32P all = m_vi->render(false);
  ASSERT_TRUE(all);
  const TRect box = convert(m_vi->getBBox());
  EXPECT_EQ(all->getSize(), box.getSize());
  const TPoint middle = TPoint(0, 0) - box.getP00();
  EXPECT_EQ(all->pixels(middle.y)[middle.x], TPixel32(250, 210, 60, 255));

  const TRaster32P strokes = m_vi->render(true);
  ASSERT_TRUE(strokes);
  EXPECT_EQ(strokes->pixels(middle.y)[middle.x].m, 0) << "no regions";
}

// A style's icon: a drawing in the style on white. (A plain colour fills
// its icon without drawing anything; a style derived from it draws.)
TEST_F(OffscreenCanvas, AStyleDrawsItsIcon) {
  TColorStyle *style = TColorStyle::create(1125);  // MovingSolidColor
  ASSERT_NE(style, nullptr);
  style->setMainColor(TPixel32(250, 210, 60, 255));
  m_palette->addStyle(style);
  style->invalidateIcon();
  const TRaster32P icon = style->getIcon(TDimension(40, 30));
  ASSERT_TRUE(icon);
  EXPECT_EQ(icon->getSize(), TDimension(40, 30));
  EXPECT_EQ(icon->pixels(15)[20], TPixel32(250, 210, 60, 255));
}
