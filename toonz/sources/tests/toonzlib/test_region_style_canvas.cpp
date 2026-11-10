// The region styles of colorfx (gradients, hatching, dots, stripes, mosaics
// and the like), drawn on a canvas: which calls they make, read as text from
// a recording, and that a rasteriser other than OpenGL can carry them out.
//
// The styles are made by their tags, the way a palette that is loaded makes
// them, and set through the generic parameter interface.

#include <gtest/gtest.h>

#include "canvas/tcanvasrecorder.h"

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
  kMoving       = 1125,
  kHatching     = 1127,  // ShadowStyle
  kRubber       = 1128,
  kPointShadow  = 1129,
  kDotted       = 1130,
  kChecked      = 1131,
  kArtistic     = 1132,
  kShading      = 1135,  // ShadowStyle2
  kChess        = 1136,
  kStripe       = 1137,
  kLinGrad      = 1138,
  kRadGrad      = 1139,
  kCircleStripe = 1140,
  kMosaic       = 1141,
  kPatch        = 1142,
  kChalk        = 1143
};

const int kEveryTag[] = {kMoving,  kHatching,     kRubber, kPointShadow,
                         kDotted,  kChecked,      kArtistic, kShading,
                         kChess,   kStripe,       kLinGrad,  kRadGrad,
                         kCircleStripe, kMosaic,  kPatch,    kChalk};

TStroke *loop(double cx, double cy, double r, int n) {
  std::vector<TThickPoint> points;
  const double step = 2.0 * std::acos(-1.0) / n;
  for (int i = 0; i < n; ++i) {
    const double a = i * step, am = a + step / 2;
    const double cr = r / std::cos(step / 2);
    points.push_back(
        TThickPoint(cx + r * std::cos(a), cy + r * std::sin(a), 0.0));
    points.push_back(
        TThickPoint(cx + cr * std::cos(am), cy + cr * std::sin(am), 0.0));
  }
  points.push_back(points[0]);
  TStroke *stroke = new TStroke(points);
  stroke->setSelfLoop(true);
  return stroke;
}

class RegionStyleCanvas : public ::testing::Test {
protected:
  TPaletteP m_palette;

  void SetUp() override { m_palette = new TPalette(); }

  TColorStyle *make(int tag) const {
    TColorStyle *style = TColorStyle::create(tag);
    EXPECT_NE(style, nullptr) << "no style with tag " << tag;
    return style;
  }

  // A disc 60 units across filled with the style, or a ring of it around a
  // hole 20 units across. The outlines themselves are not drawn.
  TVectorImageP imageOf(TColorStyle *style, bool withHole = false) const {
    const int id = m_palette->getPage(0)->addStyle(style);

    TVectorImageP vi = new TVectorImage();
    vi->setPalette(m_palette.getPointer());
    TStroke *outer = loop(0, 0, 30, 8);
    outer->setStyle(0);
    vi->addStroke(outer);
    if (withHole) {
      TStroke *inner = loop(4, -3, 10, 6);
      inner->setStyle(0);
      vi->addStroke(inner);
    }
    vi->findRegions();
    vi->fill(TPointD(-22, 0), id);
    return vi;
  }

  TVectorRenderData production() const {
    return TVectorRenderData(TVectorRenderData::ProductionSettings(),
                             TTranslation(50, 40), TRect(0, 0, 99, 79),
                             m_palette.getPointer());
  }

  // The calls drawing the image makes, without their geometry and without
  // the save, concat and restore that put the image in its place.
  std::vector<std::string> calls(const TVectorImageP &vi) const {
    TCanvasRecorder recorder(TCanvasIRect(0, 0, 100, 80));
    tcanvasDraw(recorder, production(), vi.getPointer());

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
      ADD_FAILURE() << "the image is not drawn under a transform of its own";
    }
    return out;
  }
};

int count(const std::vector<std::string> &lines, const std::string &prefix) {
  int n = 0;
  for (const std::string &line : lines) n += line.rfind(prefix, 0) == 0;
  return n;
}

bool contains(const std::string &line, const std::string &part) {
  return line.find(part) != std::string::npos;
}

std::string joined(const std::vector<std::string> &lines) {
  std::string all;
  for (const std::string &line : lines) all += line + "\n";
  return all;
}

}  // namespace

//-----------------------------------------------------------------------------

TEST_F(RegionStyleCanvas, EveryRegionStyleOfColorfxDrawsOnACanvas) {
  for (int tag : kEveryTag) {
    SCOPED_TRACE("style " + std::to_string(tag));
    TColorStyle *style = make(tag);
    ASSERT_NE(style, nullptr);
    EXPECT_TRUE(style->canvasSupport() & TColorStyle::RegionsOnCanvas);

    const TVectorImageP vi = imageOf(style, true);
    ASSERT_TRUE(tcanvasCanDraw(production(), vi.getPointer()));

    const std::vector<std::string> lines = calls(vi);
    EXPECT_EQ(count(lines, "save"), count(lines, "restore"));
    EXPECT_GT(count(lines, "fillPath") + count(lines, "drawMesh") +
                  count(lines, "strokePath"),
              0);
    // The pattern is kept to the region one way or the other.
    if (tag != kMoving && tag != kRubber && tag != kArtistic)
      EXPECT_EQ(count(lines, "fillPathAndClip") +
                    count(lines, "clipPath intersect"),
                1)
          << joined(lines);
  }
}

// What all the styles with a pattern do: the region in the style's main
// colour, as a fill that is also a clip, and the pattern on it.
TEST_F(RegionStyleCanvas, APatternIsDrawnOverTheRegionAndClippedToIt) {
  TColorStyle *checked = make(kChecked);
  ASSERT_NE(checked, nullptr);
  checked->setMainColor(TPixel32(255, 255, 0, 255));
  checked->setColorParamValue(0, TPixel32(0, 0, 255, 255));

  const std::vector<std::string> lines = calls(imageOf(checked));
  ASSERT_EQ(lines.size(), 5u) << joined(lines);
  EXPECT_EQ(lines[0], "save");
  EXPECT_EQ(lines[1], "fillPathAndClip nonzero color=(1 1 0 1) grow=0.5px");
  // The lines one way, then the lines the other way.
  EXPECT_EQ(lines[2], "drawMesh quads hardedges color=(1 1 1 1) noaa");
  EXPECT_EQ(lines[3], "drawMesh quads hardedges color=(1 1 1 1) noaa");
  EXPECT_EQ(lines[4], "restore");
}

// A main colour that cannot be seen leaves only the clip, with hard edges.
TEST_F(RegionStyleCanvas, ARegionThatDoesNotShowIsOnlyAClip) {
  TColorStyle *checked = make(kChecked);
  ASSERT_NE(checked, nullptr);
  checked->setMainColor(TPixel32(255, 255, 0, 0));
  checked->setColorParamValue(0, TPixel32(0, 0, 255, 255));

  const std::vector<std::string> lines = calls(imageOf(checked));
  ASSERT_EQ(lines.size(), 5u) << joined(lines);
  EXPECT_EQ(lines[1], "clipPath intersect nonzero noaa");
  EXPECT_EQ(count(lines, "fillPath"), 0);
}

// The two gradients paint the whole region themselves: quads, a fan and
// strips with a colour at every corner, and no region underneath.
TEST_F(RegionStyleCanvas, GradientsAreMeshesWithAColourPerCorner) {
  const std::vector<std::string> linear = calls(imageOf(make(kLinGrad)));
  ASSERT_EQ(linear.size(), 4u) << joined(linear);
  EXPECT_EQ(linear[1], "clipPath intersect nonzero noaa");
  EXPECT_EQ(linear[2], "drawMesh quads hardedges color=(1 1 1 1) noaa");

  const std::vector<std::string> radial = calls(imageOf(make(kRadGrad)));
  EXPECT_EQ(radial[1], "clipPath intersect nonzero noaa") << joined(radial);
  EXPECT_EQ(count(radial, "drawMesh fan hardedges"), 1);
  EXPECT_GE(count(radial, "drawMesh strip hardedges"), 1);
  EXPECT_EQ(count(radial, "fillPath"), 0);
}

// Stripes that are not level get a smooth line down each edge. The lines
// may only show next to the stripes, so they are drawn inside a clip that
// keeps them off the mesh the stripes were drawn as.
TEST_F(RegionStyleCanvas, SmoothLinesAlongStripesAreKeptOffTheStripes) {
  TColorStyle *slanted = make(kStripe);
  ASSERT_NE(slanted, nullptr);
  slanted->setParamValue(1, 30.0);  // the angle
  slanted->setColorParamValue(0, TPixel32(0, 0, 255, 255));
  std::vector<std::string> lines = calls(imageOf(slanted));
  EXPECT_EQ(count(lines, "drawMesh quads hardedges"), 1) << joined(lines);
  EXPECT_EQ(count(lines, "clipMesh difference quads"), 1);
  EXPECT_EQ(count(lines, "strokePath width=1px color=(0 0 1 1)"), 1);
  EXPECT_EQ(count(lines, "save"), 2);

  for (double angle : {0.0, 90.0}) {
    SCOPED_TRACE(angle);
    TColorStyle *straight = make(kStripe);
    straight->setParamValue(1, angle);
    straight->setColorParamValue(0, TPixel32(0, 0, 255, 255));
    lines = calls(imageOf(straight));
    EXPECT_EQ(count(lines, "drawMesh quads hardedges"), 1) << joined(lines);
    EXPECT_EQ(count(lines, "clipMesh"), 0);
    EXPECT_EQ(count(lines, "strokePath"), 0);
  }

  // Rings: each a strip, and each with its own two lines.
  lines           = calls(imageOf(make(kCircleStripe)));
  const int rings = count(lines, "drawMesh strip hardedges");
  EXPECT_GT(rings, 1) << joined(lines);
  EXPECT_EQ(count(lines, "clipMesh difference strip"), rings);
  EXPECT_EQ(count(lines, "strokePath width=1px color=("), rings);
}

// A disc is a point under a round pen as wide as the disc, in the units of
// the drawing.
TEST_F(RegionStyleCanvas, DiscsAreDotsUnderARoundPen) {
  TColorStyle *dotted = make(kDotted);
  ASSERT_NE(dotted, nullptr);
  dotted->setParamValue(0, 2.5);  // the radius of a dot
  dotted->setColorParamValue(0, TPixel32(255, 0, 0, 255));
  std::vector<std::string> lines = calls(imageOf(dotted));
  // All the dots in one call.
  EXPECT_EQ(count(lines, "strokePath width=5 cap=round color=(1 0 0 1) noaa"),
            1)
      << joined(lines);
  EXPECT_EQ(count(lines, "strokePath"), 1);

  // The cells of a beehive, the walls between them, and a disc at each
  // end of every wall.
  TColorStyle *beehive = make(kPatch);
  ASSERT_NE(beehive, nullptr);
  beehive->setMainColor(TPixel32(0, 0, 0, 255));  // the colour of the walls
  beehive->setParamValue(2, 40.0);                // and how thick they are
  lines = calls(imageOf(beehive));
  EXPECT_EQ(count(lines, "drawMesh triangles hardedges"), 1) << joined(lines);
  EXPECT_EQ(count(lines, "drawMesh quads hardedges"), 1);
  EXPECT_EQ(count(lines, "strokePath width="), 1);
  for (const std::string &line : lines)
    if (line.rfind("strokePath", 0) == 0) {
      EXPECT_TRUE(contains(line, " cap=round ")) << line;
      EXPECT_FALSE(contains(line, "px")) << line;
    }
}

// A canvas paints what one call covers once. These patterns never did:
// where two of their pieces overlapped, a translucent colour was laid on
// twice, and it showed. So translucent pieces go out one call each.
TEST_F(RegionStyleCanvas, ATranslucentPatternIsDrawnPieceByPiece) {
  const TPixel32 opaque(0, 0, 255, 255), translucent(0, 0, 255, 128);
  const auto piecesOf = [&](int tag, const TPixel32 &color,
                            const char *call) {
    TColorStyle *style = make(tag);
    style->setColorParamValue(0, color);
    return count(calls(imageOf(style)), call);
  };

  EXPECT_EQ(piecesOf(kChecked, opaque, "drawMesh quads"), 2);
  EXPECT_GT(piecesOf(kChecked, translucent, "drawMesh quads"), 4);
  EXPECT_EQ(piecesOf(kStripe, opaque, "drawMesh quads"), 1);
  EXPECT_GT(piecesOf(kStripe, translucent, "drawMesh quads"), 2);
  EXPECT_EQ(piecesOf(kDotted, opaque, "strokePath"), 1);
  EXPECT_GT(piecesOf(kDotted, translucent, "strokePath"), 4);
}

// Hatching: lines one pixel wide that are transparent at both ends and the
// colour in the middle, all the lines of an edge under one gradient.
TEST_F(RegionStyleCanvas, HatchingIsThinLinesThatFadeAtBothEnds) {
  TColorStyle *hatched = make(kHatching);
  ASSERT_NE(hatched, nullptr);
  hatched->setColorParamValue(0, TPixel32(0, 255, 0, 255));
  hatched->setMainColor(TPixel32(255, 255, 255, 255));

  const std::vector<std::string> disc = calls(imageOf(hatched->clone()));
  const int lines = count(disc, "strokePath width=1px color=(1 1 1 1) "
                                "shader=linear(");
  EXPECT_GT(lines, 0) << joined(disc);
  EXPECT_EQ(count(disc, "strokePath"), lines);
  for (const std::string &line : disc)
    if (line.rfind("strokePath", 0) == 0)
      EXPECT_TRUE(
          contains(line, "[0:(0 1 0 0) 0.5:(0 1 0 1) 1:(0 1 0 0)]"))
          << line;
  EXPECT_EQ(count(disc, "fillPathAndClip"), 1);
  EXPECT_EQ(count(disc, "clipPath"), 0);

  // A hole is hatched too, from its edge inwards: outside the region.
  const std::vector<std::string> ring = calls(imageOf(hatched, true));
  EXPECT_EQ(count(ring, "fillPathAndClip"), 1) << joined(ring);
  EXPECT_EQ(count(ring, "clipPath difference nonzero"), 1);
  EXPECT_GT(count(ring, "strokePath"), lines);
}

// The other shading is a band along the edges that face the light, a strip
// per edge with the colour changing across it.
TEST_F(RegionStyleCanvas, ShadingIsAStripAlongEachEdgeThatFacesTheLight) {
  const std::vector<std::string> lines = calls(imageOf(make(kShading)));
  EXPECT_GT(count(lines, "drawMesh strip hardedges color=(1 1 1 1) noaa"), 3)
      << joined(lines);
  EXPECT_EQ(count(lines, "strokePath"), 0);
}

// The points of the point shadow are OpenGL points, whose size the style
// gives as a share of what the driver can do. Away from OpenGL the range
// is taken to be 1 to 64 pixels.
TEST_F(RegionStyleCanvas, PointsAreDotsOfASizeInPixels) {
  TColorStyle *points = make(kPointShadow);
  ASSERT_NE(points, nullptr);
  points->setParamValue(3, 50.0);  // the point size, in percent
  const std::vector<std::string> lines = calls(imageOf(points));
  const int dots = count(lines, "strokePath width=32.5px cap=round color=(");
  EXPECT_GT(dots, 10) << joined(lines);
  EXPECT_EQ(count(lines, "strokePath"), dots);
}

// Squares scattered or laid out in a grid are each moved into place by a
// transform of their own.
TEST_F(RegionStyleCanvas, SquaresAreEachMovedIntoPlace) {
  for (int tag : {kChalk, kChess}) {
    SCOPED_TRACE("style " + std::to_string(tag));
    const std::vector<std::string> lines = calls(imageOf(make(tag)));
    const int squares = count(lines, "drawMesh quads hardedges");
    EXPECT_GT(squares, 3) << joined(lines);
    EXPECT_EQ(count(lines, "concat"), squares);
    // One save for each, and one for the region's clip.
    EXPECT_EQ(count(lines, "save"), squares + 1);
  }
}

//-----------------------------------------------------------------------------

// None of it needs OpenGL: the styles come out of the rasteriser a build
// has, which runs without a context. (A build without one skips this.)
TEST_F(RegionStyleCanvas, TheStylesAreRasterisedWithoutOpenGl) {
  if (!TVectorRasterizer::isAvailable())
    GTEST_SKIP() << "this build has no rasteriser but OpenGL";

  for (int tag : kEveryTag) {
    SCOPED_TRACE("style " + std::to_string(tag));
    TColorStyle *style = make(tag);
    ASSERT_NE(style, nullptr);
    // Opaque colours all round, so that the region is covered whatever
    // the pattern leaves open.
    style->setMainColor(TPixel32(200, 160, 40, 255));
    for (int i = 0; i < style->getColorParamCount(); ++i)
      if (style->getColorParamValue(i).m == 0)
        style->setColorParamValue(i, TPixel32(30, 60, 200, 255));

    TRaster32P raster(100, 80);
    ASSERT_TRUE(TVectorRasterizer::rasterize(raster, imageOf(style, true),
                                             production()));
    int covered = 0;
    raster->lock();
    for (int y = 0; y < raster->getLy(); ++y)
      for (int x = 0; x < raster->getLx(); ++x)
        covered += raster->pixels(y)[x].m > 0;
    raster->unlock();
    // The ring is about 2500 pixels; nothing is drawn far outside it.
    EXPECT_GT(covered, 1500);
    EXPECT_LT(covered, 4200);
    EXPECT_EQ(raster->pixels(2)[2].m, 0);
  }
}
