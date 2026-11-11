// The stroke styles of colorfx (ropes, braids, fur, chalk, blends and the
// like), drawn on a canvas: which calls they make, read as text from a
// recording, and that a rasteriser other than OpenGL can carry them out.
//
// The styles are made by their tags, the way a palette that is loaded makes
// them, and set through the generic parameter interface.

#include <gtest/gtest.h>

#include "canvas/tcanvasrecorder.h"

#include "tcolorfunctions.h"
#include "tcolorstyles.h"
#include "tpalette.h"
#include "traster.h"
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

enum Tag {
  kFur         = 103,
  kChain       = 104,
  kSpray       = 106,
  kGraphicPen  = 107,
  kRope        = 108,
  kCrystallize = 109,
  kDottedLine  = 111,
  kSketch      = 113,
  kBubble      = 114,
  kTissue      = 117,
  kBump        = 120,  // TNormal2StrokeStyle
  kChalk       = 123,
  kBlend       = 125,
  kTwirl       = 126,
  kSawTooth    = 127,
  kZigzag      = 129,
  kSin         = 130,
  kStriped     = 132,  // TDualColorStrokeStyle2
  kFrieze      = 133,
  kLongBlend   = 134,
  kBiColor     = 135,
  kBraid       = 136,
  kMultiLine   = 138,
  kMatrioska   = 141,
  kFlowLine    = 201
};

const int kEveryTag[] = {kFur,       kChain,     kSpray,     kGraphicPen,
                         kRope,      kCrystallize, kDottedLine, kSketch,
                         kBubble,    kTissue,    kBump,      kChalk,
                         kBlend,     kTwirl,     kSawTooth,  kZigzag,
                         kSin,       kStriped,   kFrieze,    kLongBlend,
                         kBiColor,   kBraid,     kMultiLine, kMatrioska,
                         kFlowLine};

class StrokeStyleCanvas : public ::testing::Test {
protected:
  TPaletteP m_palette;

  void SetUp() override { m_palette = new TPalette(); }

  TColorStyle *make(int tag) const {
    TColorStyle *style = TColorStyle::create(tag);
    EXPECT_NE(style, nullptr) << "no style with tag " << tag;
    return style;
  }

  // A wave 160 units long in the style, as thick as \p thickness says.
  TVectorImageP imageOf(TColorStyle *style, double thickness = 5.0) const {
    const int id = m_palette->getPage(0)->addStyle(style);

    TVectorImageP vi = new TVectorImage();
    vi->setPalette(m_palette.getPointer());
    std::vector<TThickPoint> wave;
    for (int i = 0; i < 9; ++i)
      wave.push_back(
          TThickPoint(-80 + 20.0 * i, (i % 2) ? 12 : -12, thickness));
    TStroke *stroke = new TStroke(wave);
    stroke->setStyle(id);
    vi->addStroke(stroke);
    return vi;
  }

  TVectorRenderData production(const TColorFunction *cf = nullptr) const {
    return TVectorRenderData(TVectorRenderData::ProductionSettings(),
                             TTranslation(100, 40), TRect(0, 0, 199, 79),
                             m_palette.getPointer(), cf);
  }

  // The calls drawing the image makes, without their geometry and without
  // the save, concat and restore that put the stroke in its place.
  std::vector<std::string> calls(const TVectorImageP &vi,
                                 const TColorFunction *cf = nullptr) const {
    TCanvasRecorder recorder(TCanvasIRect(0, 0, 200, 80));
    tcanvasDraw(recorder, production(cf), vi.getPointer());

    std::vector<std::string> out;
    std::istringstream text(recorder.displayList().dump());
    for (std::string line; std::getline(text, line);) {
      const std::size_t start = line.find_first_not_of(' ');
      if (start == std::string::npos) continue;
      line = line.substr(start);
      if (line[0] == 'M' || line.rfind("positions:", 0) == 0 ||
          line.rfind("colors:", 0) == 0 || line.rfind("uvs:", 0) == 0)
        continue;
      out.push_back(line);
    }
    if (out.size() >= 3 && out.front() == "save" &&
        out[1].rfind("concat", 0) == 0 && out.back() == "restore") {
      out.erase(out.begin(), out.begin() + 2);
      out.pop_back();
    } else {
      ADD_FAILURE() << "the stroke is not drawn under a transform of its own";
    }
    return out;
  }
};

int count(const std::vector<std::string> &lines, const std::string &prefix) {
  int n = 0;
  for (const std::string &line : lines) n += line.rfind(prefix, 0) == 0;
  return n;
}

int countContaining(const std::vector<std::string> &lines,
                    const std::string &part) {
  int n = 0;
  for (const std::string &line : lines)
    n += line.find(part) != std::string::npos;
  return n;
}

std::string joined(const std::vector<std::string> &lines) {
  std::string all;
  for (std::size_t i = 0; i < lines.size() && i < 40; ++i)
    all += lines[i] + "\n";
  return all;
}

}  // namespace

//-----------------------------------------------------------------------------

TEST_F(StrokeStyleCanvas, EveryStrokeStyleOfColorfxDrawsOnACanvas) {
  for (int tag : kEveryTag) {
    SCOPED_TRACE("style " + std::to_string(tag));
    TColorStyle *style = make(tag);
    ASSERT_NE(style, nullptr);
    EXPECT_TRUE(style->canvasSupport() & TColorStyle::StrokesOnCanvas);

    const TVectorImageP vi = imageOf(style);
    ASSERT_TRUE(tcanvasCanDraw(production(), vi.getPointer()));

    const std::vector<std::string> lines = calls(vi);
    EXPECT_EQ(count(lines, "save"), count(lines, "restore"));
    EXPECT_GT(count(lines, "drawMesh") + count(lines, "strokePath"), 0);
    // Nothing here fills a path or clips.
    EXPECT_EQ(count(lines, "fillPath") + count(lines, "clip"), 0)
        << joined(lines);
  }
}

// A band with a line of one pixel down each side: the band is a strip with
// the colour at every vertex and hard edges, the lines soften its sides.
TEST_F(StrokeStyleCanvas, ABandIsAHardEdgedStripWithALineDownEachSide) {
  for (int tag : {kSin, kFrieze}) {
    SCOPED_TRACE("style " + std::to_string(tag));
    TColorStyle *style = make(tag);
    style->setMainColor(TPixel32(255, 0, 0, 255));
    const std::vector<std::string> lines = calls(imageOf(style));
    ASSERT_EQ(lines.size(), 3u) << joined(lines);
    EXPECT_EQ(lines[0], "drawMesh strip hardedges color=(1 1 1 1) noaa");
    EXPECT_EQ(lines[1], "strokePath width=1px color=(1 0 0 1)");
    EXPECT_EQ(lines[2], "strokePath width=1px color=(1 0 0 1)");
  }

  // The zigzag adds the rungs between the two sides.
  const std::vector<std::string> zigzag = calls(imageOf(make(kZigzag)));
  EXPECT_EQ(count(zigzag, "drawMesh strip hardedges"), 1) << joined(zigzag);
  EXPECT_EQ(count(zigzag, "strokePath width=1px color=("), 3);
}

// Polygons go out as polygons: a strand of rope has eight corners.
TEST_F(StrokeStyleCanvas, StrandsOfRopeArePolygonsWithALineRoundThem) {
  const std::vector<std::string> lines = calls(imageOf(make(kRope)));
  const int strands = count(lines, "drawMesh polygon hardedges");
  EXPECT_GT(strands, 3) << joined(lines);
  // A black line round each, and one more at the end.
  EXPECT_EQ(count(lines, "strokePath width=1px color=(0 0 0 1)"), strands + 1);

  const std::vector<std::string> braid = calls(imageOf(make(kBraid)));
  EXPECT_GT(count(braid, "drawMesh polygon hardedges"), 9) << joined(braid);
  EXPECT_EQ(count(braid, "strokePath"), count(braid, "drawMesh"));
}

// A line whose colour changes from one end to the other is stroked under a
// gradient between its ends: a hair of fur fades out by way of white.
TEST_F(StrokeStyleCanvas, ALineThatFadesIsStrokedUnderAGradient) {
  TColorStyle *fur = make(kFur);
  fur->setMainColor(TPixel32(0, 0, 255, 255));
  const std::vector<std::string> lines = calls(imageOf(fur));
  const int hairs = count(lines, "strokePath width=1px color=(1 1 1 1) "
                                 "shader=linear(");
  EXPECT_GT(hairs, 10) << joined(lines);
  EXPECT_EQ(count(lines, "strokePath"), hairs);
  EXPECT_EQ(countContaining(lines, "[0:(0 0 1 1) 1:(1 1 1 0)]"), hairs);
}

// Stretches of one colour go out as plain lines, and a change of colour at
// one place is not a line at all.
TEST_F(StrokeStyleCanvas, StripesChangeColourWithoutALineBetween) {
  TColorStyle *striped = make(kStriped);
  striped->setColorParamValue(0, TPixel32(0, 0, 255, 255));
  striped->setColorParamValue(1, TPixel32(255, 255, 0, 255));
  const std::vector<std::string> lines = calls(imageOf(striped));
  EXPECT_EQ(countContaining(lines, "shader="), 0) << joined(lines);
  EXPECT_GT(count(lines, "strokePath width=1px color=(0 0 1 1)"), 2);
  EXPECT_GT(count(lines, "strokePath width=1px color=(1 1 0 1)"), 1);
  EXPECT_EQ(count(lines, "drawMesh strip hardedges"), 1);
}

// Circles are closed lines round the origin, moved into place by the
// transform: that is how they have always been rounded to pixels.
TEST_F(StrokeStyleCanvas, CirclesAreLinesMovedIntoPlace) {
  for (int tag : {kSpray, kBubble}) {
    SCOPED_TRACE("style " + std::to_string(tag));
    const std::vector<std::string> lines = calls(imageOf(make(tag), 9.0));
    const int circles = count(lines, "strokePath width=1px color=(");
    EXPECT_GT(circles, 3) << joined(lines);
    EXPECT_EQ(count(lines, "save"), circles);
    EXPECT_EQ(count(lines, "concat"), circles);
    EXPECT_EQ(count(lines, "drawMesh"), 0);
  }
}

// A chain is drawn link by link; one too thin to show its links is a plain
// line.
TEST_F(StrokeStyleCanvas, AChainTooThinForLinksIsALine) {
  const std::vector<std::string> links = calls(imageOf(make(kChain), 6.0));
  const int rings = count(links, "save");
  EXPECT_GT(rings, 2) << joined(links);
  EXPECT_EQ(count(links, "concat"), 2 * rings) << "placed, then scaled";
  // The rings, and a short line from each to the next.
  EXPECT_EQ(count(links, "strokePath width=1px"), 2 * rings - 1);

  const std::vector<std::string> thin = calls(imageOf(make(kChain), 0.5));
  EXPECT_EQ(count(thin, "save"), 0) << joined(thin);
  EXPECT_GT(count(thin, "strokePath") + count(thin, "drawMesh"), 0);
}

// Where a stroke has no thickness the graphic pen puts down points: dots of
// one pixel.
TEST_F(StrokeStyleCanvas, PointsAreDotsOfOnePixel) {
  const std::vector<std::string> lines = calls(imageOf(make(kGraphicPen), 0.0));
  EXPECT_GT(count(lines, "strokePath width=1px cap=round color=("), 3)
      << joined(lines);
  EXPECT_EQ(countContaining(lines, " noaa"), count(lines, "strokePath"));

  const std::vector<std::string> thick =
      calls(imageOf(make(kGraphicPen), 5.0));
  EXPECT_EQ(countContaining(thick, "cap=round"), 0) << joined(thick);
}

// The one style whose bands are asked to have smooth edges, whatever the
// render says.
TEST_F(StrokeStyleCanvas, TheLinesOfTheMultiLineStyleHaveSmoothEdges) {
  const std::vector<std::string> lines = calls(imageOf(make(kMultiLine)));
  const int bands = count(lines, "drawMesh strip color=(1 1 1 1)");
  EXPECT_GT(bands, 1) << joined(lines);
  EXPECT_EQ(count(lines, "drawMesh"), bands);
  EXPECT_EQ(countContaining(lines, "hardedges"), 0);
  EXPECT_EQ(countContaining(lines, " noaa"), 0);
}

// The stroke that looks lit: one mesh for the whole stroke, lit vertex by
// vertex, with smooth edges. (Through OpenGL it is drawn by other code,
// which has OpenGL do the lighting.)
TEST_F(StrokeStyleCanvas, TheLitStrokeIsOneMeshLitVertexByVertex) {
  TColorStyle *bump = make(kBump);
  bump->setMainColor(TPixel32(255, 255, 0, 255));

  TCanvasRecorder recorder(TCanvasIRect(0, 0, 200, 80));
  const TVectorImageP vi = imageOf(bump);
  tcanvasDraw(recorder, production(), vi.getPointer());

  const TCanvasOpDrawMesh *mesh = nullptr;
  int meshes                   = 0;
  for (const TCanvasOp &op : recorder.displayList().ops)
    if (const TCanvasOpDrawMesh *m = std::get_if<TCanvasOpDrawMesh>(&op))
      mesh = m, ++meshes;
  ASSERT_EQ(meshes, 1);
  EXPECT_EQ(mesh->mesh.mode, TCanvasMesh::Mode::Triangles);
  EXPECT_TRUE(mesh->mesh.smoothEdges);
  ASSERT_EQ(mesh->mesh.colors.size(), mesh->mesh.positions.size());

  // Yellow in different lights: no blue anywhere, never transparent, and
  // not the same everywhere.
  float darkest = 1.f, brightest = 0.f;
  for (const TCanvasColor &color : mesh->mesh.colors) {
    EXPECT_EQ(color.a, 1.f);
    EXPECT_LT(color.b, color.r);
    darkest   = std::min(darkest, color.r);
    brightest = std::max(brightest, color.r);
  }
  EXPECT_GT(brightest - darkest, 0.2f);
  EXPECT_GE(darkest, 0.19f) << "the ambient light";
}

// The matrioska is solid strokes one inside the other.
TEST_F(StrokeStyleCanvas, TheMatrioskaIsStrokesInsideStrokes) {
  TColorStyle *matrioska = make(kMatrioska);
  matrioska->setParamValue(0, 4.0);  // how many
  const std::vector<std::string> lines = calls(imageOf(matrioska));
  EXPECT_EQ(count(lines, "drawMesh strip color=("), 4) << joined(lines);
  EXPECT_EQ(count(lines, "strokePath"), 0);
}

// The colour function reaches the styles on a canvas as it does through
// OpenGL: a fader that hides everything leaves nothing to draw.
TEST_F(StrokeStyleCanvas, TheColourFunctionIsApplied) {
  const TTranspFader hide(0.0);
  for (int tag : {kSin, kTissue, kSketch, kChain}) {
    SCOPED_TRACE("style " + std::to_string(tag));
    const TVectorImageP vi = imageOf(make(tag));
    TCanvasRecorder recorder(TCanvasIRect(0, 0, 200, 80));
    tcanvasDraw(recorder, production(&hide), vi.getPointer());
    int strokes = 0;
    for (const TCanvasOp &op : recorder.displayList().ops)
      strokes += std::holds_alternative<TCanvasOpStrokePath>(op);
    EXPECT_EQ(strokes, 0) << "lines in a colour that cannot be seen";
  }
}

//-----------------------------------------------------------------------------

// None of it needs OpenGL: the styles come out of the rasteriser a build
// has, which runs without a context. (A build without one skips this.)
TEST_F(StrokeStyleCanvas, TheStylesAreRasterisedWithoutOpenGl) {
  if (!TVectorRasterizer::isAvailable())
    GTEST_SKIP() << "this build has no rasteriser but OpenGL";

  for (int tag : kEveryTag) {
    SCOPED_TRACE("style " + std::to_string(tag));
    TColorStyle *style = make(tag);
    ASSERT_NE(style, nullptr);

    TRaster32P raster(200, 80);
    ASSERT_TRUE(
        TVectorRasterizer::rasterize(raster, imageOf(style), production()));
    int covered = 0;
    raster->lock();
    for (int y = 0; y < raster->getLy(); ++y)
      for (int x = 0; x < raster->getLx(); ++x)
        covered += raster->pixels(y)[x].m > 0;
    raster->unlock();
    // Something along the wave, and not the whole picture. (The flow lines
    // spread far past the stroke; the others stay near it.)
    EXPECT_GT(covered, 150);
    EXPECT_LT(covered, tag == kFlowLine ? 12000 : 6000);
  }
}
