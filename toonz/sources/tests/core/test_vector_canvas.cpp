// Vector images drawn on a canvas instead of straight to OpenGL
// (tvectorcanvas.h, tvectorrasterizer.h; doc/nexttoonz_canvas_ir.md).
//
// The first tests record what the traversal and the migrated styles ask a
// canvas to draw and read it as text: which calls, in which order, with
// which paint. They need no rasteriser and no OpenGL. The last ones
// rasterise, in a build that has Skia.

#include <gtest/gtest.h>

#include "canvas/tcanvasrecorder.h"

#include "tpalette.h"
#include "tregion.h"
#include "tsimplecolorstyles.h"
#include "tstroke.h"
#include "tvectorcanvas.h"
#include "tvectorimage.h"
#include "tvectorrasterizer.h"
#include "tvectorrenderdata.h"

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace {

TStroke *makeLine(const TPointD &a, const TPointD &b, double thick) {
  std::vector<TThickPoint> pts;
  for (int i = 0; i <= 2; ++i)
    pts.push_back(TThickPoint(a + (i / 2.0) * (b - a), thick));
  return new TStroke(pts);
}

TStroke *makeCircle(const TPointD &c, double r, double thick) {
  std::vector<TThickPoint> pts;
  const int n       = 8;
  const double step = 2.0 * std::acos(-1.0) / n;
  const double cr   = r / std::cos(step / 2.0);
  for (int i = 0; i < n; ++i) {
    const double a = i * step, am = (i + 0.5) * step;
    pts.push_back(
        TThickPoint(c.x + r * std::cos(a), c.y + r * std::sin(a), thick));
    pts.push_back(
        TThickPoint(c.x + cr * std::cos(am), c.y + cr * std::sin(am), thick));
  }
  pts.push_back(pts[0]);
  TStroke *s = new TStroke(pts);
  s->setSelfLoop(true);
  return s;
}

// A style of the palette's first page, and its id.
int addStyle(TPalette *palette, TColorStyle *style) {
  const int id = palette->addStyle(style);
  palette->getPage(0)->addStyle(id);
  return id;
}

// A 100 x 100 picture with the image's origin in the middle.
TVectorRenderData production(TPalette *palette) {
  return TVectorRenderData(TVectorRenderData::ProductionSettings(),
                           TTranslation(50, 50), TRect(0, 0, 99, 99), palette);
}

TVectorRenderData viewer(TPalette *palette) {
  return TVectorRenderData(TVectorRenderData::ViewerSettings(),
                           TTranslation(50, 50), TRect(0, 0, 99, 99), palette);
}

// What drawing the image asks of a canvas, one call or one line of
// geometry per element.
std::vector<std::string> record(const TVectorImageP &vi,
                                const TVectorRenderData &rd) {
  TCanvasRecorder recorder(TCanvasIRect(0, 0, 100, 100));
  tcanvasDraw(recorder, rd, vi.getPointer());

  std::vector<std::string> lines;
  std::istringstream text(recorder.displayList().dump());
  for (std::string line; std::getline(text, line);) {
    const std::size_t start = line.find_first_not_of(' ');
    lines.push_back(start == std::string::npos ? "" : line.substr(start));
  }
  return lines;
}

// The calls only, without their geometry.
std::vector<std::string> calls(const std::vector<std::string> &lines) {
  std::vector<std::string> out;
  for (const std::string &line : lines) {
    const char first = line.empty() ? ' ' : line[0];
    if (first == 'M' || line.rfind("positions:", 0) == 0 ||
        line.rfind("colors:", 0) == 0)
      continue;
    out.push_back(line);
  }
  return out;
}

int count(const std::vector<std::string> &lines, const std::string &prefix) {
  int n = 0;
  for (const std::string &line : lines) n += line.rfind(prefix, 0) == 0;
  return n;
}

// A style that derives from the plain colour and has not said that it
// draws on a canvas, like the dozens of them in colorfx.
class DerivedColorStyle final : public TSolidColorStyle {
public:
  DerivedColorStyle() : TSolidColorStyle(TPixel32(0, 255, 0, 255)) {}
  TColorStyle *clone() const override { return new DerivedColorStyle(*this); }
};

// Sets the stroke weight switch for a test and puts it back afterwards.
// Tests that read the calls set it themselves, so that they say the same
// whatever NEXTTOONZ_STROKE_GROW happens to be.
struct StrokeGrow {
  explicit StrokeGrow(double pixels) {
    TVectorRasterizer::setStrokeGrow(pixels);
  }
  ~StrokeGrow() {
    TVectorRasterizer::setStrokeGrow(TVectorRasterizer::defaultStrokeGrow());
  }
};

}  // namespace

//-----------------------------------------------------------------------------
// What the traversal emits

TEST(VectorCanvas, ASolidStrokeIsOneMeshUnderTheImagesTransform) {
  const StrokeGrow none(0.0);
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int red =
      addStyle(palette, new TSolidColorStyle(TPixel32(255, 0, 0, 255)));
  TStroke *s = makeLine(TPointD(-30, 0), TPointD(30, 10), 3.0);
  s->setStyle(red);
  vi->addStroke(s);

  const std::vector<std::string> expected = {
      "save", "concat [1 0 50; 0 1 50]", "drawMesh strip color=(1 0 0 1)",
      "restore"};
  EXPECT_EQ(calls(record(vi, production(palette))), expected);
}

// Strokes keep the weight the old renderer's fringe gave them: half a pixel
// more on each side (the owner's decision on question Q2 of the design).
TEST(VectorCanvas, StrokesAreGrownByHalfAPixelUnlessToldOtherwise) {
  EXPECT_EQ(TVectorRasterizer::defaultStrokeGrow(), 0.5);

  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  TStroke *s = makeLine(TPointD(-30, 0), TPointD(30, 10), 3.0);
  s->setStyle(1);  // the black every palette starts with
  vi->addStroke(s);

  {
    const StrokeGrow usual(TVectorRasterizer::defaultStrokeGrow());
    EXPECT_EQ(TVectorRasterizer::strokeGrow(), 0.5);
    EXPECT_EQ(count(record(vi, production(palette)),
                    "drawMesh strip color=(0 0 0 1) grow=0.5px"),
              1);
  }
  {
    const StrokeGrow trueWidth(0.0);
    EXPECT_EQ(count(record(vi, production(palette)),
                    "drawMesh strip color=(0 0 0 1)"),
              1);
    EXPECT_EQ(count(record(vi, production(palette)),
                    "drawMesh strip color=(0 0 0 1) grow"),
              0);
  }
  // Without antialiasing too: there the old renderer drew a line one pixel
  // wide down each side of the stroke, which is the same half pixel, and
  // without which a stroke thinner than a pixel comes out in pieces.
  const StrokeGrow usual(TVectorRasterizer::defaultStrokeGrow());
  TVectorRenderData jaggy = production(palette);
  jaggy.m_antiAliasing    = false;
  EXPECT_EQ(count(record(vi, jaggy),
                  "drawMesh strip color=(0 0 0 1) noaa grow=0.5px"),
            1);
}

TEST(VectorCanvas, ARegionIsOneFilledPathUnderItsStrokes) {
  const StrokeGrow none(0.0);
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int ink =
      addStyle(palette, new TSolidColorStyle(TPixel32(0, 0, 255, 255)));
  const int paint =
      addStyle(palette, new TSolidColorStyle(TPixel32(255, 255, 0, 255)));
  TStroke *s = makeCircle(TPointD(0, 0), 20, 2.0);
  s->setStyle(ink);
  vi->addStroke(s);
  vi->findRegions();
  ASSERT_EQ(vi->getRegionCount(), 1u);
  vi->fill(TPointD(0, 0), paint);

  const std::vector<std::string> lines = record(vi, production(palette));
  // Grown by half a pixel, which is what closes the seam towards a
  // neighbouring region (question Q1 of the design).
  const std::vector<std::string> expected = {
      "save",
      "concat [1 0 50; 0 1 50]",
      "fillPath nonzero color=(1 1 0 1) grow=0.5px",
      "restore",
      "save",
      "concat [1 0 50; 0 1 50]",
      "drawMesh strip color=(0 0 1 1)",
      "restore"};
  EXPECT_EQ(calls(lines), expected);
  EXPECT_EQ(count(lines, "M "), 1) << "one contour";

  // The viewers do not antialias regions: no smooth edge, nothing to grow.
  EXPECT_EQ(count(record(vi, viewer(palette)),
                  "fillPath nonzero color=(1 1 0 1) noaa"),
            1);

  TVectorRenderData inksOnly = production(palette);
  inksOnly.m_drawRegions     = false;
  EXPECT_EQ(count(record(vi, inksOnly), "fillPath"), 0);
}

TEST(VectorCanvas, ARegionWithAHoleIsOnePathOfTwoContours) {
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int paint =
      addStyle(palette, new TSolidColorStyle(TPixel32(255, 255, 0, 128)));
  vi->addStroke(makeCircle(TPointD(0, 0), 30, 0.0));
  vi->addStroke(makeCircle(TPointD(0, 0), 10, 0.0));
  vi->findRegions();
  vi->fill(TPointD(20, 0), paint);  // the ring between the circles

  const std::vector<std::string> lines = record(vi, production(palette));
  EXPECT_EQ(count(lines, "fillPath nonzero"), 1);
  EXPECT_EQ(count(lines, "M "), 2);
  // The circles themselves have no thickness; a render does not show them.
  EXPECT_EQ(count(lines, "drawMesh"), 0);
  EXPECT_EQ(count(lines, "strokePath"), 0);
}

// The "constant" line style, drawn the way the old renderer drew it on the
// drivers it was checked on: a ribbon, quad by quad, with a line of one
// pixel down each side. The dashes are its 16-bit stipple.
TEST(VectorCanvas, TheConstantLineStyleIsARibbonWithALineDownEachSide) {
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int dashed = addStyle(
      palette, new TCenterLineStrokeStyle(TPixel32(0, 0, 255, 255), 0xFF00, 2.0));
  TStroke *s = makeLine(TPointD(-30, 0), TPointD(30, 0), 0.0);
  s->setStyle(dashed);
  vi->addStroke(s);

  const std::vector<std::string> lines = record(vi, production(palette));
  const std::vector<std::string> expected = {
      "save",
      "concat [1 0 50; 0 1 50]",
      "drawMesh strip hardedges color=(1 1 1 1)",
      "strokePath width=1px dash=[0 8 8 0] color=(0 0 1 1)",
      "restore"};
  EXPECT_EQ(calls(lines), expected);
  EXPECT_EQ(count(lines, "colors:"), 1) << "the ribbon's colour is per vertex";
  EXPECT_EQ(count(lines, "M "), 2) << "the two side lines, in one call";
}

TEST(VectorCanvas, LinesOfNoWidthShowInTheViewersOnly) {
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int noWidth = addStyle(
      palette, new TCenterLineStrokeStyle(TPixel32(0, 0, 0, 255), 0x0, 0.0));
  TStroke *constant = makeLine(TPointD(-30, 0), TPointD(30, 0), 0.0);
  constant->setStyle(noWidth);
  vi->addStroke(constant);
  // And a stroke of the plain colour whose thickness is nothing.
  TStroke *thin = makeLine(TPointD(-30, 10), TPointD(30, 10), 0.0);
  thin->setStyle(1);
  vi->addStroke(thin);

  EXPECT_EQ(count(record(vi, production(palette)), "drawMesh"), 0);
  EXPECT_EQ(count(record(vi, production(palette)), "strokePath"), 0);
  EXPECT_EQ(count(record(vi, viewer(palette)), "drawMesh strip hardedges"), 2);
}

TEST(VectorCanvas, CheckModesDrawWithTheirOwnColours) {
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int ink =
      addStyle(palette, new TSolidColorStyle(TPixel32(0, 0, 255, 255)));
  const int paint =
      addStyle(palette, new TSolidColorStyle(TPixel32(255, 255, 0, 255)));
  TStroke *s = makeCircle(TPointD(0, 0), 20, 2.0);
  s->setStyle(ink);
  vi->addStroke(s);
  vi->findRegions();
  vi->fill(TPointD(0, 0), paint);

  TVectorRenderData rd = viewer(palette);
  rd.m_tcheckEnabled   = true;
  rd.m_tCheckInk       = TPixel32(255, 0, 0, 255);
  rd.m_tCheckPaint     = TPixel32(0, 255, 0, 255);
  const std::vector<std::string> lines = record(vi, rd);
  EXPECT_EQ(count(lines, "fillPath nonzero color=(0 1 0 1)"), 1);
  EXPECT_EQ(count(lines, "drawMesh strip color=(1 0 0 1)"), 1);
}

//-----------------------------------------------------------------------------
// Which images can be drawn this way

TEST(VectorCanvas, AStyleAnswersForItselfNotForWhatItDerivesFrom) {
  TSolidColorStyle plain;
  EXPECT_EQ(plain.canvasSupport(),
            TColorStyle::StrokesOnCanvas | TColorStyle::RegionsOnCanvas);
  TCenterLineStrokeStyle constant;
  EXPECT_EQ(constant.canvasSupport(), (int)TColorStyle::StrokesOnCanvas);
  DerivedColorStyle derived;
  EXPECT_EQ(derived.canvasSupport(), (int)TColorStyle::NoCanvas);
}

TEST(VectorCanvas, AnImageCanBeDrawnWhenEveryStyleItUsesIsOnCanvas) {
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int plain =
      addStyle(palette, new TSolidColorStyle(TPixel32(255, 0, 0, 255)));
  const int derived = addStyle(palette, new DerivedColorStyle());

  TStroke *s = makeCircle(TPointD(0, 0), 20, 2.0);
  s->setStyle(plain);
  vi->addStroke(s);
  vi->findRegions();
  vi->fill(TPointD(0, 0), plain);

  const TVectorRenderData rd = production(palette);
  EXPECT_TRUE(tcanvasCanDraw(rd, vi.getPointer()))
      << "a style in the palette that nothing uses does not count";

  // One region with a style that is not there yet: the whole image is not.
  vi->fill(TPointD(0, 0), derived);
  EXPECT_FALSE(tcanvasCanDraw(rd, vi.getPointer()));
  TVectorRenderData inksOnly = rd;
  inksOnly.m_drawRegions     = false;
  EXPECT_TRUE(tcanvasCanDraw(inksOnly, vi.getPointer()));
  // A check mode paints over the style with a plain colour.
  TVectorRenderData checked = rd;
  checked.m_tcheckEnabled   = true;
  EXPECT_TRUE(tcanvasCanDraw(checked, vi.getPointer()));

  vi->fill(TPointD(0, 0), plain);
  vi->getStroke(0)->setStyle(derived);
  EXPECT_FALSE(tcanvasCanDraw(rd, vi.getPointer()));
  vi->getStroke(0)->setStyle(plain);
  ASSERT_TRUE(tcanvasCanDraw(rd, vi.getPointer()));

  // What only the OpenGL viewers have.
  TVectorRenderData view3d = rd;
  view3d.m_is3dView        = true;
  EXPECT_FALSE(tcanvasCanDraw(view3d, vi.getPointer()));
  TVectorRenderData guided   = rd;
  guided.m_showGuidedDrawing = true;
  EXPECT_FALSE(tcanvasCanDraw(guided, vi.getPointer()));
}

//-----------------------------------------------------------------------------
// One stroke, one region

// What tglDraw(rd, stroke) and tglDraw(rd, region) draw, which the code that
// converts a drawing one stroke or one region at a time uses.
TEST(VectorCanvas, OneStrokeOrOneRegionIsDrawnOnItsOwn) {
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int ink = addStyle(palette, new TSolidColorStyle(TPixel32(0, 0, 255)));
  const int paint =
      addStyle(palette, new TSolidColorStyle(TPixel32(255, 255, 0)));
  TStroke *outer = makeCircle(TPointD(0, 0), 30, 2.0);
  outer->setStyle(ink);
  vi->addStroke(outer);
  TStroke *inner = makeCircle(TPointD(0, 0), 10, 2.0);
  inner->setStyle(ink);
  vi->addStroke(inner);
  vi->findRegions();
  for (UINT i = 0; i < vi->getRegionCount(); ++i) {
    vi->getRegion(i)->setStyle(paint);
    for (UINT j = 0; j < vi->getRegion(i)->getSubregionCount(); ++j)
      vi->getRegion(i)->getSubregion(j)->setStyle(paint);
  }

  const TVectorRenderData rd = production(palette);
  const auto dump = [&](auto draw) {
    TCanvasRecorder recorder(TCanvasIRect(0, 0, 100, 100));
    draw(recorder);
    return calls([&] {
      std::vector<std::string> lines;
      std::istringstream text(recorder.displayList().dump());
      for (std::string line; std::getline(text, line);) {
        const std::size_t start = line.find_first_not_of(' ');
        lines.push_back(start == std::string::npos ? "" : line.substr(start));
      }
      return lines;
    }());
  };

  const TStroke *stroke = vi->getStroke(0);
  ASSERT_TRUE(tcanvasCanDraw(rd, stroke));
  std::vector<std::string> lines =
      dump([&](TCanvas &canvas) { tcanvasDraw(canvas, rd, stroke); });
  EXPECT_EQ(count(lines, "drawMesh strip color=(0 0 1 1)"), 1);
  EXPECT_EQ(count(lines, "fillPath"), 0);

  // The outer region, then the one inside it.
  TRegion *region = 0;
  for (UINT i = 0; i < vi->getRegionCount(); ++i)
    if (vi->getRegion(i)->getSubregionCount() > 0) region = vi->getRegion(i);
  ASSERT_NE(region, nullptr);
  ASSERT_TRUE(tcanvasCanDraw(rd, region));
  lines = dump([&](TCanvas &canvas) { tcanvasDraw(canvas, rd, region); });
  EXPECT_EQ(count(lines, "fillPath nonzero color=(1 1 0 1)"),
            1 + (int)region->getSubregionCount());
  EXPECT_EQ(count(lines, "drawMesh"), 0);

  // A style that cannot be drawn on a canvas says so for its stroke.
  const int derived = addStyle(palette, new DerivedColorStyle());
  TStroke *other    = makeLine(TPointD(-30, 0), TPointD(30, 0), 3.0);
  other->setStyle(derived);
  EXPECT_FALSE(tcanvasCanDraw(rd, other));
  delete other;
}

//-----------------------------------------------------------------------------
// Rasterising

TEST(VectorRasterizer, IsSelectedOnlyInABuildThatHasIt) {
  TVectorRasterizer::select(true);
  EXPECT_EQ(TVectorRasterizer::isSelected(), TVectorRasterizer::isAvailable());
  TVectorRasterizer::select(false);
  EXPECT_FALSE(TVectorRasterizer::isSelected());
}

TEST(VectorRasterizer, ReplacesTheRasterWithTheImage) {
  const StrokeGrow none(0.0);
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int ink =
      addStyle(palette, new TSolidColorStyle(TPixel32(0, 0, 255, 255)));
  const int paint =
      addStyle(palette, new TSolidColorStyle(TPixel32(255, 0, 0, 128)));
  TStroke *s = makeCircle(TPointD(0, 0), 30, 3.0);
  s->setStyle(ink);
  vi->addStroke(s);
  vi->findRegions();
  vi->fill(TPointD(0, 0), paint);

  TRaster32P raster(100, 100);
  raster->fill(TPixel32(9, 9, 9, 9));
  const bool drawn =
      TVectorRasterizer::rasterize(raster, vi, production(palette));

  if (!TVectorRasterizer::isAvailable()) {
    EXPECT_FALSE(drawn);
    EXPECT_EQ(raster->pixels(50)[50], TPixel32(9, 9, 9, 9)) << "left alone";
    return;
  }
  ASSERT_TRUE(drawn);
  // Outside the image: transparent, not what was there.
  EXPECT_EQ(raster->pixels(2)[2], TPixel32(0, 0, 0, 0));
  // The fill: premultiplied.
  const TPixel32 inside = raster->pixels(50)[50];
  EXPECT_NEAR(inside.r, 128, 1);
  EXPECT_EQ(inside.g, 0);
  EXPECT_EQ(inside.b, 0);
  EXPECT_NEAR(inside.m, 128, 1);
  // The stroke: 3 units either side of the circle of radius 30.
  EXPECT_EQ(raster->pixels(50)[80], TPixel32(0, 0, 255, 255));
  EXPECT_EQ(raster->pixels(80)[50], TPixel32(0, 0, 255, 255));
  EXPECT_EQ(raster->pixels(50)[85], TPixel32(0, 0, 0, 0));

  // A raster of another depth gets the same picture.
  TRaster64P deep(100, 100);
  ASSERT_TRUE(TVectorRasterizer::rasterize(deep, vi, production(palette)));
  EXPECT_EQ(deep->pixels(50)[80].b, 65535);
  EXPECT_EQ(deep->pixels(50)[80].m, 65535);
  EXPECT_EQ(deep->pixels(2)[2].m, 0);
}

// draw() puts the image over what is there already, the way a context that
// has been cleared to some colour and then drawn on does.
TEST(VectorRasterizer, DrawsOverWhatIsThere) {
  const StrokeGrow none(0.0);
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int ink =
      addStyle(palette, new TSolidColorStyle(TPixel32(0, 0, 255, 255)));
  TStroke *s = makeLine(TPointD(-30, 0), TPointD(30, 0), 3.0);
  s->setStyle(ink);
  vi->addStroke(s);

  TRaster32P raster(100, 100);
  raster->fill(TPixel32::White);
  const bool drawn =
      TVectorRasterizer::draw(raster, vi.getPointer(), production(palette));
  if (!TVectorRasterizer::isAvailable()) {
    EXPECT_FALSE(drawn);
    EXPECT_EQ(raster->pixels(50)[50], TPixel32::White) << "left alone";
    return;
  }
  ASSERT_TRUE(drawn);
  EXPECT_EQ(raster->pixels(50)[50], TPixel32(0, 0, 255, 255));
  EXPECT_EQ(raster->pixels(80)[50], TPixel32::White) << "not cleared";

  // One stroke on its own, through the same entry point.
  raster->fill(TPixel32::White);
  ASSERT_TRUE(TVectorRasterizer::draw(raster, vi->getStroke(0),
                                      production(palette)));
  EXPECT_EQ(raster->pixels(50)[50], TPixel32(0, 0, 255, 255));
  EXPECT_EQ(raster->pixels(80)[50], TPixel32::White);
}

TEST(VectorRasterizer, LeavesAnImageItCannotDrawToTheCaller) {
  TPalette *palette = new TPalette();
  TVectorImageP vi  = new TVectorImage();
  vi->setPalette(palette);
  const int derived = addStyle(palette, new DerivedColorStyle());
  TStroke *s        = makeLine(TPointD(-30, 0), TPointD(30, 0), 3.0);
  s->setStyle(derived);
  vi->addStroke(s);

  TRaster32P raster(100, 100);
  raster->fill(TPixel32(9, 9, 9, 9));
  EXPECT_FALSE(TVectorRasterizer::rasterize(raster, vi, production(palette)));
  EXPECT_EQ(raster->pixels(50)[50], TPixel32(9, 9, 9, 9));
}
