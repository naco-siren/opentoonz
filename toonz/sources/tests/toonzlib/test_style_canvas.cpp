// The styles that draw with a picture or with another vector image (raster
// and vector patterns, vector brushes, textures, MyPaint brushes), drawn on
// a canvas: which calls they make, read as text from a recording. No
// rasteriser and no OpenGL is involved.
//
// The styles are the ones of the reference project's vec_styles level,
// loaded the way a scene loads them.

#include <gtest/gtest.h>

#include "toonzlib_test_env.h"

#include "canvas/tcanvasrecorder.h"

#include "toonz/levelset.h"
#include "toonz/toonzscene.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"
#include "tpalette.h"
#include "tstroke.h"
#include "tvectorcanvas.h"
#include "tvectorimage.h"
#include "tvectorrenderdata.h"

#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using nexttoonz_test::fixturePath;

// The palette of vec_styles, which has one style of every registered kind.
class StyleCanvas : public ::testing::Test {
protected:
  std::unique_ptr<ToonzScene> m_scene;
  TPalette *m_palette = nullptr;

  void SetUp() override {
    m_scene.reset(new ToonzScene());
    m_scene->load(fixturePath("scenes/vector_styles.tnz"));
    TLevelSet *levels = m_scene->getLevelSet();
    for (int l = 0; l < levels->getLevelCount(); ++l) {
      TXshSimpleLevel *sl = levels->getLevel(l)->getSimpleLevel();
      if (sl && sl->getType() == PLI_XSHLEVEL && sl->getName() == L"vec_styles")
        m_palette = sl->getPalette();
    }
    ASSERT_NE(m_palette, nullptr);
  }

  // The id of the palette's style of this kind.
  int styleWithTag(int tag) const {
    for (int i = 0; i < m_palette->getStyleCount(); ++i)
      if (m_palette->getStyle(i) && m_palette->getStyle(i)->getTagId() == tag)
        return i;
    ADD_FAILURE() << "no style with tag " << tag;
    return 0;
  }

  // A wavy stroke five units thick and, for a style that fills, a filled
  // circle, all in the one style.
  TVectorImageP imageIn(int styleId) const {
    TVectorImageP vi = new TVectorImage();
    vi->setPalette(m_palette);
    std::vector<TThickPoint> wave;
    for (int i = 0; i < 9; ++i)
      wave.push_back(
          TThickPoint(-90 + 22.5 * i, 35 + ((i % 2) ? 12 : -12), 5.0));
    TStroke *stroke = new TStroke(wave);
    stroke->setStyle(styleId);
    vi->addStroke(stroke);

    if (m_palette->getStyle(styleId)->isRegionStyle()) {
      std::vector<TThickPoint> ring;
      const int n       = 8;
      const double step = 2.0 * std::acos(-1.0) / n;
      for (int i = 0; i < n; ++i) {
        const double a = i * step, am = a + step / 2, r = 28;
        const double cr = r / std::cos(step / 2);
        ring.push_back(
            TThickPoint(-45 + r * std::cos(a), -20 + r * std::sin(a), 0.0));
        ring.push_back(
            TThickPoint(-45 + cr * std::cos(am), -20 + cr * std::sin(am), 0.0));
      }
      ring.push_back(ring[0]);
      TStroke *circle = new TStroke(ring);
      circle->setSelfLoop(true);
      circle->setStyle(0);
      vi->addStroke(circle);
      vi->findRegions();
      vi->fill(TPointD(-45, -20), styleId);
    }
    return vi;
  }

  TVectorRenderData production(double scale = 1.0) const {
    return TVectorRenderData(TVectorRenderData::ProductionSettings(),
                             TTranslation(200, 100) * TScale(scale),
                             TRect(0, 0, 399, 199), m_palette);
  }

  // The calls drawing the image makes, one per element, without geometry.
  std::vector<std::string> calls(const TVectorImageP &vi,
                                 const TVectorRenderData &rd) const {
    TCanvasRecorder recorder(TCanvasIRect(0, 0, 400, 200));
    tcanvasDraw(recorder, rd, vi.getPointer());

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

}  // namespace

//-----------------------------------------------------------------------------

// Every stamp of a raster pattern is its picture on a quad: a mesh that
// looks an image up at its corners, with hard edges (the picture's own
// alpha is its shape).
TEST_F(StyleCanvas, ARasterPatternIsAPictureOnAQuadPerStamp) {
  const TVectorImageP vi = imageIn(styleWithTag(2000));
  ASSERT_TRUE(tcanvasCanDraw(production(), vi.getPointer()));

  const std::vector<std::string> lines = calls(vi, production());
  const int stamps =
      count(lines, "drawMesh strip hardedges color=(1 1 1 1) shader=image(");
  EXPECT_GT(stamps, 3);
  EXPECT_EQ(count(lines, "drawMesh"), stamps) << "nothing but stamps";
  EXPECT_EQ(countContaining(lines, " repeat linear "), stamps);
  // Each under a transform of its own.
  EXPECT_EQ(count(lines, "save"), stamps);
  EXPECT_EQ(count(lines, "concat"), stamps);
  EXPECT_EQ(count(lines, "restore"), stamps);
}

// The stamps of a vector pattern are vector images, drawn by the same
// traversal one level down; when they would come out smaller than two
// pixels, a dashed line stands in for them.
TEST_F(StyleCanvas, AVectorPatternDrawsItsStampsOrADashedLine) {
  const TVectorImageP vi = imageIn(styleWithTag(2800));
  ASSERT_TRUE(tcanvasCanDraw(production(), vi.getPointer()));

  const std::vector<std::string> stamps = calls(vi, production());
  EXPECT_GT(count(stamps, "drawMesh") + count(stamps, "fillPath"), 3);
  EXPECT_EQ(countContaining(stamps, "shader="), 0);
  EXPECT_EQ(count(stamps, "strokePath"), 0);
  EXPECT_EQ(count(stamps, "save"), count(stamps, "restore"));

  const std::vector<std::string> small = calls(vi, production(0.02));
  EXPECT_EQ(count(small, "drawMesh") + count(small, "fillPath"), 0);
  EXPECT_EQ(count(small, "strokePath width=1px dash=[0 2 2 2 2 "), 1);
}

// A vector brush is its image bent along the stroke, every part of it in a
// plain colour: regions without smooth edges, strokes with.
TEST_F(StyleCanvas, AVectorBrushIsPlainColouredOutlines) {
  const TVectorImageP vi = imageIn(styleWithTag(3000));
  ASSERT_TRUE(tcanvasCanDraw(production(), vi.getPointer()));

  const std::vector<std::string> lines = calls(vi, production());
  EXPECT_GT(count(lines, "fillPath nonzero color=(") +
                count(lines, "drawMesh strip color=("),
            0);
  EXPECT_EQ(countContaining(lines, "shader="), 0);
  EXPECT_EQ(count(lines, "fillPath"), countContaining(lines, " noaa"));
  // One transform for the whole stroke.
  EXPECT_EQ(count(lines, "save"), 1);
}

// On a vector level a MyPaint brush is a stroke of its colour.
TEST_F(StyleCanvas, AMyPaintBrushIsASolidStroke) {
  const TVectorImageP vi = imageIn(styleWithTag(4001));
  ASSERT_TRUE(tcanvasCanDraw(production(), vi.getPointer()));

  const std::vector<std::string> lines = calls(vi, production());
  ASSERT_EQ(lines.size(), 4u);
  EXPECT_EQ(lines[0], "save");
  EXPECT_EQ(lines[2].rfind("drawMesh strip color=(", 0), 0u) << lines[2];
  EXPECT_EQ(lines[3], "restore");
}

// A texture fills a region as a repeating image with hard edges, and runs
// along a stroke as a strip that looks the image up by its own coordinates.
TEST_F(StyleCanvas, ATextureIsAnImageShaderOnRegionsAndStrokes) {
  const TVectorImageP vi = imageIn(styleWithTag(4));
  ASSERT_TRUE(tcanvasCanDraw(production(), vi.getPointer()));

  const std::vector<std::string> lines = calls(vi, production());
  EXPECT_EQ(count(lines, "fillPath nonzero color=(1 1 1 1) shader=image("), 1);
  EXPECT_EQ(count(lines, "drawMesh strip color=(1 1 1 1) shader=image("), 1);
  EXPECT_EQ(countContaining(lines, " straight repeat linear "), 2);
  // The region has hard edges; the stroke's are left to the canvas.
  for (const std::string &line : lines) {
    if (line.rfind("fillPath", 0) == 0)
      EXPECT_NE(line.find(" noaa"), std::string::npos) << line;
    if (line.rfind("drawMesh", 0) == 0)
      EXPECT_EQ(line.find(" noaa"), std::string::npos) << line;
  }
}

// One style that is not on the canvas yet keeps the whole image off it.
TEST_F(StyleCanvas, AnImageWithAnyOtherSpecialStyleIsNotDrawnOnACanvas) {
  const TVectorImageP vi = imageIn(styleWithTag(2000));
  TStroke *extra         = new TStroke(*vi->getStroke(0));
  extra->setStyle(styleWithTag(108));  // "Rope", one of colorfx's
  vi->addStroke(extra);
  EXPECT_FALSE(tcanvasCanDraw(production(), vi.getPointer()));
}
