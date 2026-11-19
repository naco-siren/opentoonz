// What goes through the seams to OpenGL (tglentries.h, step 8b of
// doc/nexttoonz_core_noqt.md) when the styles and their props draw: fakes
// are put in the seams, and what reaches them is compared with what the
// code did with OpenGL before. These tests run without an OpenGL context:
// nothing here reaches OpenGL.

#include <gtest/gtest.h>

#include "toonzlib_test_env.h"

#include "canvas/tcanvasrecorder.h"

#include "tcolorstyles.h"
#include "tglentries.h"
#include "tpalette.h"
#include "traster.h"
#include "tregion.h"
#include "tregionoutline.h"
#include "tregionprop.h"
#include "tsimplecolorstyles.h"
#include "tstroke.h"
#include "tstrokeoutline.h"
#include "tstrokeprop.h"
#include "tsystem.h"
#include "tvectorimage.h"
#include "tvectorrasterizer.h"
#include "tvectorrenderdata.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<std::string> g_events;

//-----------------------------------------------------------------------------
// The viewer canvas: a recorder stands in for the OpenGL context.

void fakePaint(TGLEntries::Painter painter, void *what,
               bool saveAllAttributes) {
  TCanvasRecorder recorder(TCanvasIRect(0, 0, 200, 200));
  painter(recorder, true, what);
  g_events.push_back(
      std::string(saveAllAttributes ? "paint saving all" : "paint") +
      (recorder.displayList().dump().empty() ? " nothing" : ""));
}
void fakePush(const TAffine &) { g_events.push_back("push"); }
void fakePop() { g_events.push_back("pop"); }
double fakePixelSize2() {
  g_events.push_back("pixel size");
  return 1.0;
}
const TGLEntries::ViewerCanvas g_fakeCanvas = {&fakePaint, &fakePush,
                                               &fakePop, &fakePixelSize2};

class FakeViewerCanvas {
public:
  FakeViewerCanvas() {
    g_events.clear();
    TGLEntries::setViewerCanvas(&g_fakeCanvas);
  }
  ~FakeViewerCanvas() { TGLEntries::setViewerCanvas(nullptr); }
};

//-----------------------------------------------------------------------------
// Drawers by class

const void *g_drawnStyle = nullptr;

void fakeStroke(const TColorStyle &style, const TColorFunction *,
                const TStroke *) {
  g_drawnStyle = &style;
}
void fakeOutlineStroke(const TColorStyle &style, const TColorFunction *,
                       TStrokeOutline *, const TStroke *) {
  g_drawnStyle = &style;
}
void fakeRegion(const TColorStyle &style, const TColorFunction *, bool,
                TRegionOutline &) {
  g_drawnStyle = &style;
}
void fakePatternStroke(const TColorStyle &style, const TVectorRenderData &,
                       const std::vector<TAffine> &, const TStroke *) {
  g_drawnStyle = &style;
}

template <class Drawer>
TGLEntries::AnyDrawer any(Drawer drawer) {
  return reinterpret_cast<TGLEntries::AnyDrawer>(drawer);
}

//-----------------------------------------------------------------------------
// Offline contexts: one that writes down what is done with it.

class RecordingContext final : public TGLEntries::OfflineContext {
  TDimension m_size;

public:
  explicit RecordingContext(const TDimension &size) : m_size(size) {}
  void clear(TPixel32 color) override {
    g_events.push_back("clear " + std::to_string(color.r) + " " +
                       std::to_string(color.g) + " " +
                       std::to_string(color.b) + " " +
                       std::to_string(color.m));
  }
  void makeCurrent() override { g_events.push_back("make current"); }
  void draw(TVectorImage *image, const TVectorRenderData &,
            bool doInitMatrix) override {
    g_events.push_back(std::string("draw") + (image ? "" : " no image") +
                       (doInitMatrix ? " init matrix" : ""));
  }
  TRaster32P getRaster() override {
    g_events.push_back("get raster");
    TRaster32P ras(m_size);
    ras->fill(TPixel32::Red);
    return ras;
  }
};

std::unique_ptr<TGLEntries::OfflineContext> recordingFactory(
    const TDimension &size, bool stock) {
  g_events.push_back("open " + std::to_string(size.lx) + "x" +
                     std::to_string(size.ly) + (stock ? " stock" : " new"));
  return std::unique_ptr<TGLEntries::OfflineContext>(
      new RecordingContext(size));
}

// The fake factory, and OpenGL as the way to rasterise: the paths these
// tests are about are the ones without a CPU rasteriser.
class RecordingOffline {
  bool m_rasterizer;

public:
  RecordingOffline() : m_rasterizer(TVectorRasterizer::isSelected()) {
    g_events.clear();
    TGLEntries::setOfflineFactory(&recordingFactory);
    TVectorRasterizer::select(false);
  }
  ~RecordingOffline() {
    TVectorRasterizer::select(m_rasterizer);
    TGLEntries::setOfflineFactory(nullptr);
  }
};

//-----------------------------------------------------------------------------

TStroke *wave(double thickness) {
  std::vector<TThickPoint> points;
  for (int i = 0; i < 9; ++i)
    points.push_back(
        TThickPoint(-90 + 22.5 * i, 35 + ((i % 2) ? 12 : -12), thickness));
  return new TStroke(points);
}

TStroke *loop(double r, int n) {
  std::vector<TThickPoint> points;
  const double step = 2.0 * std::acos(-1.0) / n;
  for (int i = 0; i < n; ++i) {
    const double a = i * step, am = a + step / 2;
    const double cr = r / std::cos(step / 2);
    points.push_back(TThickPoint(r * std::cos(a), r * std::sin(a), 0.0));
    points.push_back(TThickPoint(cr * std::cos(am), cr * std::sin(am), 0.0));
  }
  points.push_back(points[0]);
  TStroke *stroke = new TStroke(points);
  stroke->setSelfLoop(true);
  return stroke;
}

TVectorRenderData renderData(TPalette *palette) {
  return TVectorRenderData(TTranslation(100, 100), TRect(), palette, 0, true,
                           true);
}

// The entries of the styles that set OpenGL state of their own.
const char *const kDrawerEntries[] = {
    "TCenterLineStrokeStyle::drawStroke",
    "TRasterImagePatternStrokeStyle::drawStroke",
    "TVectorImagePatternStrokeStyle::drawStroke",
    "TSketchStrokeStyle::drawStroke",
    "FlowLineStrokeStyle::drawStroke",
    "TNormal2StrokeStyle::drawStroke",
    "TPointShadowFillStyle::drawRegion",
    "TStripeFillStyle::drawRegion",
    "TCircleStripeFillStyle::drawRegion"};

}  // namespace

//=============================================================================
// 1. The viewer canvas

// A prop puts its transform in place through the hooks, asks for the size
// of a pixel there and pops the transform when its style has drawn on the
// canvas: what glPushMatrix(), tglGetPixelSize2() and glPopMatrix() did.
TEST(GlSeams, AStrokePropDrawsBetweenAPushAndAPopOfTheTransform) {
  TPaletteP palette = new TPalette();
  const int id = palette->getPage(0)->addStyle(new TSolidColorStyle(
      TPixel32::Blue));
  std::unique_ptr<TStroke> stroke(wave(5.0));
  stroke->setStyle(id);
  std::unique_ptr<TStrokeProp> prop(
      palette->getStyle(id)->makeStrokeProp(stroke.get()));

  FakeViewerCanvas fake;
  prop->draw(renderData(palette.getPointer()));
  const std::vector<std::string> expected = {"push", "pixel size", "paint",
                                             "pop"};
  EXPECT_EQ(g_events, expected);
}

// A region of a colorfx style that draws on the canvas has all of OpenGL's
// attributes saved around it (what drawThroughGl() did); a plain one not.
TEST(GlSeams, RegionPropsDrawBetweenAPushAndAPopAndSomeSaveEverything) {
  struct Case {
    int tag;
    const char *paint;
  } cases[] = {{1136 /* chess */, "paint saving all"},
               {1138 /* linear gradient */, "paint"},
               {3 /* the plain colour */, "paint"}};
  for (const Case &c : cases) {
    SCOPED_TRACE(c.tag);
    TPaletteP palette = new TPalette();
    TColorStyle *style = c.tag == 3 ? new TSolidColorStyle(TPixel32::Green)
                                    : TColorStyle::create(c.tag);
    ASSERT_NE(style, nullptr);
    const int id = palette->getPage(0)->addStyle(style);
    TVectorImageP vi = new TVectorImage();
    vi->setPalette(palette.getPointer());
    TStroke *outline = loop(30, 8);
    outline->setStyle(0);
    vi->addStroke(outline);
    vi->findRegions();
    vi->fill(TPointD(-22, 0), id);
    ASSERT_EQ(vi->getRegionCount(), 1u);
    std::unique_ptr<TRegionProp> prop(style->makeRegionProp(vi->getRegion(0)));

    FakeViewerCanvas fake;
    prop->draw(renderData(palette.getPointer()));
    const std::vector<std::string> expected = {"push", "pixel size", c.paint,
                                               "pop"};
    EXPECT_EQ(g_events, expected);
  }
}

// Every region style of colorfx but the three with OpenGL state of their
// own draws on the canvas the viewer hands it (with no OpenGL context here,
// one that still called OpenGL would crash).
TEST(GlSeams, ColorFxRegionStylesDrawOnTheViewerCanvas) {
  const int tags[] = {1125, 1127, 1128, 1130, 1131, 1132, 1135,
                      1136, 1138, 1139, 1141, 1142, 1143};
  for (int tag : tags) {
    SCOPED_TRACE(tag);
    TPaletteP palette = new TPalette();
    TColorStyle *style = TColorStyle::create(tag);
    ASSERT_NE(style, nullptr);
    const int id = palette->getPage(0)->addStyle(style);
    TVectorImageP vi = new TVectorImage();
    vi->setPalette(palette.getPointer());
    TStroke *outline = loop(30, 8);
    outline->setStyle(0);
    vi->addStroke(outline);
    vi->findRegions();
    vi->fill(TPointD(-22, 0), id);
    ASSERT_EQ(vi->getRegionCount(), 1u);
    std::unique_ptr<TRegionProp> prop(style->makeRegionProp(vi->getRegion(0)));

    FakeViewerCanvas fake;
    prop->draw(renderData(palette.getPointer()));
    ASSERT_EQ(g_events.size(), 4u);
    EXPECT_EQ(g_events[0], "push");
    EXPECT_EQ(g_events[1], "pixel size");
    EXPECT_EQ(g_events[2].rfind("paint", 0), 0u);
    EXPECT_EQ(g_events[3], "pop");
  }
}

// The styles of colorfx that drew on a canvas over OpenGL now draw on the
// canvas the viewer hands them, every one of them.
TEST(GlSeams, ColorFxStrokeStylesDrawOnTheViewerCanvas) {
  const int tags[] = {103, 104, 106, 107, 108, 109, 111, 114, 117,
                      123, 125, 126, 127, 129, 130, 132, 133, 134,
                      135, 136, 138, 141};
  for (int tag : tags) {
    SCOPED_TRACE(tag);
    TPaletteP palette = new TPalette();
    TColorStyle *style = TColorStyle::create(tag);
    ASSERT_NE(style, nullptr);
    const int id = palette->getPage(0)->addStyle(style);
    std::unique_ptr<TStroke> stroke(wave(5.0));
    stroke->setStyle(id);
    std::unique_ptr<TStrokeProp> prop(style->makeStrokeProp(stroke.get()));

    FakeViewerCanvas fake;
    prop->draw(renderData(palette.getPointer()));
    ASSERT_GE(g_events.size(), 3u);
    EXPECT_EQ(g_events.front(), "push");
    EXPECT_EQ(g_events.back(), "pop");
    int paints = 0;
    for (const std::string &e : g_events) {
      EXPECT_NE(e, "paint saving all");
      paints += e.rfind("paint", 0) == 0;
    }
    EXPECT_GE(paints, 1);
  }
}

//=============================================================================
// 2. Drawers by class

// Each of the nine has a drawer of its own, built in, and its OpenGL entry
// point goes to whatever drawer is set for it, with the style itself.
TEST(GlSeams, TheStylesWithOpenGlStateOfTheirOwnGoToTheirDrawers) {
  for (const char *entry : kDrawerEntries)
    EXPECT_NE(TGLEntries::findDrawer(entry), nullptr) << entry;

  TStrokeOutline strokeOutline;
  TRegionOutline regionOutline;
  std::unique_ptr<TStroke> stroke(wave(5.0));
  TPaletteP palette = new TPalette();

  struct Case {
    const char *entry;
    TColorStyle *style;
  };
  std::vector<Case> cases = {
      {"TCenterLineStrokeStyle::drawStroke", new TCenterLineStrokeStyle()},
      {"TRasterImagePatternStrokeStyle::drawStroke",
       new TRasterImagePatternStrokeStyle()},
      {"TVectorImagePatternStrokeStyle::drawStroke",
       new TVectorImagePatternStrokeStyle()},
      {"TSketchStrokeStyle::drawStroke", TColorStyle::create(113)},
      {"FlowLineStrokeStyle::drawStroke", TColorStyle::create(201)},
      {"TNormal2StrokeStyle::drawStroke", TColorStyle::create(120)},
      {"TPointShadowFillStyle::drawRegion", TColorStyle::create(1129)},
      {"TStripeFillStyle::drawRegion", TColorStyle::create(1137)},
      {"TCircleStripeFillStyle::drawRegion", TColorStyle::create(1140)}};
  ASSERT_EQ(cases.size(), sizeof(kDrawerEntries) / sizeof(*kDrawerEntries));

  for (const Case &c : cases) {
    SCOPED_TRACE(c.entry);
    ASSERT_NE(c.style, nullptr);
    palette->getPage(0)->addStyle(c.style);
    const std::string entry = c.entry;
    TGLEntries::AnyDrawer fake =
        entry.find("drawRegion") != std::string::npos ? any(&fakeRegion)
        : entry.rfind("TNormal2", 0) == 0             ? any(&fakeOutlineStroke)
        : entry.find("Pattern") != std::string::npos  ? any(&fakePatternStroke)
                                                      : any(&fakeStroke);
    const TGLEntries::AnyDrawer builtIn = TGLEntries::setDrawer(c.entry, fake);
    g_drawnStyle = nullptr;
    FakeViewerCanvas canvas;  // nothing may reach it

    if (auto *s = dynamic_cast<TRasterImagePatternStrokeStyle *>(c.style))
      s->drawStroke(renderData(palette.getPointer()), {}, stroke.get());
    else if (auto *s = dynamic_cast<TVectorImagePatternStrokeStyle *>(c.style))
      s->drawStroke(renderData(palette.getPointer()), {}, stroke.get());
    else if (entry.find("drawRegion") != std::string::npos)
      dynamic_cast<TOutlineStyle *>(c.style)->drawRegion(0, true,
                                                         regionOutline);
    else if (auto *s = dynamic_cast<TSimpleStrokeStyle *>(c.style))
      s->drawStroke(0, stroke.get());
    else
      dynamic_cast<TOutlineStyle *>(c.style)->drawStroke(0, &strokeOutline,
                                                         stroke.get());

    EXPECT_EQ(g_drawnStyle, c.style);
    EXPECT_TRUE(g_events.empty());
    EXPECT_EQ(TGLEntries::setDrawer(c.entry, builtIn), fake);
  }
}

//=============================================================================
// 3. Offline contexts

// A vector image rendered by itself: a new context the size of the image,
// cleared to transparent, made current and drawn into without setting the
// matrix up, as before.
TEST(GlSeams, AVectorImageRendersIntoANewClearedContext) {
  TPaletteP palette = new TPalette();
  const int id =
      palette->getPage(0)->addStyle(new TSolidColorStyle(TPixel32::Blue));
  TVectorImageP vi = new TVectorImage();
  vi->setPalette(palette.getPointer());
  TStroke *stroke = wave(5.0);
  stroke->setStyle(id);
  vi->addStroke(stroke);
  const TRect bbox = convert(vi->getBBox());

  RecordingOffline offline;
  TRaster32P ras = vi->render(false);
  const std::vector<std::string> expected = {
      "open " + std::to_string(bbox.getLx()) + "x" +
          std::to_string(bbox.getLy()) + " new",
      "clear 0 0 0 0", "make current", "draw", "get raster"};
  EXPECT_EQ(g_events, expected);
  ASSERT_TRUE(ras);
  EXPECT_EQ(ras->pixels(0)[0], TPixel32::Red);
}

// An icon is drawn in the shared context of its size, cleared to white.
TEST(GlSeams, AStyleIconIsDrawnInTheStockContextClearedToWhite) {
  std::unique_ptr<TColorStyle> rope(TColorStyle::create(108));
  ASSERT_NE(rope, nullptr);

  RecordingOffline offline;
  rope->getIcon(TDimension(52, 52));
  ASSERT_EQ(g_events.size(), 4u);
  EXPECT_EQ(g_events[0].rfind("open ", 0), 0u);
  EXPECT_NE(g_events[0].find(" stock"), std::string::npos);
  EXPECT_EQ(g_events[1], "clear 255 255 255 255");
  EXPECT_EQ(g_events[2], "draw");
  EXPECT_EQ(g_events[3], "get raster");
}

// A raster pattern made from a vector level: one context for the whole
// level, made when the first frame needs it and never cleared, so that each
// frame is drawn over the one before it. That is what the old code did (a
// TOfflineGL created once, drawn into frame after frame); it shows in the
// textures, and so it stays.
TEST(GlSeams, ARasterPatternOfAVectorLevelReusesOneContextUncleared) {
  // The patterns are looked for in "custom styles" under the library.
  const TFilePath library = nexttoonz_test::makeScratchDir("gl_seams_pattern");
  TSystem::mkDir(library + "custom styles");
  TSystem::copyFile(library + "custom styles" + "seams.pli",
                    nexttoonz_test::fixturePath("drawings/vec_circle.pli"));
  const TFilePath previous = TRasterImagePatternStrokeStyle::getRootDir().getParentDir();
  TRasterImagePatternStrokeStyle::setRootDir(library);

  {
    RecordingOffline offline;
    TRasterImagePatternStrokeStyle style("seams");
    std::vector<std::string> expected = {"open 256x256 new"};
    for (int frame = 0; frame < 8; ++frame) {
      expected.push_back("draw");
      expected.push_back("get raster");
    }
    EXPECT_EQ(g_events, expected);
  }

  TRasterImagePatternStrokeStyle::setRootDir(previous);
}

// The icon of a vector pattern: its first stamp, in the shared context of
// the icon's size, cleared to white.
TEST(GlSeams, AVectorPatternIconIsDrawnInTheStockContextClearedToWhite) {
  // The patterns are looked for in "custom styles" under the library.
  const TFilePath library = nexttoonz_test::makeScratchDir("gl_seams_vpattern");
  TSystem::mkDir(library + "custom styles");
  TSystem::copyFile(library + "custom styles" + "seams.pli",
                    nexttoonz_test::fixturePath("drawings/vec_circle.pli"));
  const TFilePath previous = TVectorImagePatternStrokeStyle::getRootDir().getParentDir();
  TVectorImagePatternStrokeStyle::setRootDir(library);

  {
    TVectorImagePatternStrokeStyle style("seams");
    RecordingOffline offline;
    style.getIcon(TDimension(40, 30));
    const std::vector<std::string> expected = {
        "open 40x30 stock", "clear 255 255 255 255", "draw", "get raster"};
    EXPECT_EQ(g_events, expected);
  }

  TVectorImagePatternStrokeStyle::setRootDir(previous);
}
