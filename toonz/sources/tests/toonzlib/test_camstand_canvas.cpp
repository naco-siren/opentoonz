// Camera-stand renders of reference scenes (ToonzScene::renderFrame(): scene
// icons, xsheet thumbnails, printing) with the CPU rasteriser selected
// (migration step 8 of doc/nexttoonz_canvas_ir.md). The tests run without a
// window system and so without an OpenGL context; the painter also reports
// itself whether it had to give up on its raster, which is when
// renderFrame() would start again with OpenGL.

#include <gtest/gtest.h>

#include "toonzlib_test_env.h"

#include "toonz/tcamera.h"
#include "traster.h"
#include "trasterimage.h"
#include "trop.h"
#include "tvectorrasterizer.h"

#include "toonz/stage.h"
#include "toonz/stagevisitor.h"
#include "toonz/toonzscene.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshsimplelevel.h"

#include <algorithm>
#include <memory>
#include <string>

using nexttoonz_test::fixturePath;

namespace {

std::unique_ptr<ToonzScene> loadScene(const std::string &name) {
  std::unique_ptr<ToonzScene> scene(new ToonzScene());
  scene->load(fixturePath("scenes/" + name + ".tnz"));
  return scene;
}

// Draws a frame of the scene as renderFrame() does, on a cleared raster;
// false if the painter could not draw all of it there.
bool paintOnRaster(ToonzScene &scene, int row, const TRaster32P &frame) {
  TXsheet *xsh             = scene.getXsheet();
  TStageObjectTree *tree   = xsh->getStageObjectTree();
  const TDimensionD &size  = tree->getCurrentCamera()->getSize();
  const double sc          = std::min(frame->getLx() / size.lx,
                                      frame->getLy() / size.ly);
  const TAffine &cameraAff = xsh->getPlacement(tree->getCurrentCameraId(), row);

  ImagePainter::VisualSettings vs;
  vs.m_plasticVisualSettings.m_drawMeshesWireframe = false;
  vs.m_forSceneIcon                                = true;

  frame->clear();
  Stage::RasterPainter painter(frame->getSize(),
                               TScale(sc / Stage::inch) * cameraAff.inv(),
                               frame->getBounds(), vs, false);
  painter.setTarget(frame);
  Stage::visit(painter, &scene, xsh, row);
  painter.flushRasterImages();
  return !painter.hasTargetFailed();
}

long drawnPixels(const TRaster32P &ras) {
  long n = 0;
  ras->lock();
  for (int y = 0; y < ras->getLy(); ++y)
    for (int x = 0; x < ras->getLx(); ++x) n += ras->pixels(y)[x].m != 0;
  ras->unlock();
  return n;
}

bool samePixels(const TRaster32P &a, const TRaster32P &b) {
  if (a->getSize() != b->getSize()) return false;
  for (int y = 0; y < a->getLy(); ++y)
    if (!std::equal(a->pixels(y), a->pixels(y) + a->getLx(), b->pixels(y)))
      return false;
  return true;
}

class CamstandCanvas : public ::testing::TestWithParam<const char *> {
protected:
  bool m_wasSelected = false;  // put back afterwards
  void SetUp() override {
    if (!TVectorRasterizer::isAvailable())
      GTEST_SKIP() << "this build has no rasteriser but OpenGL";
    m_wasSelected = TVectorRasterizer::isSelected();
    TVectorRasterizer::select(true);
  }
  void TearDown() override { TVectorRasterizer::select(m_wasSelected); }
};

}  // namespace

// Every image of these scenes goes on the raster, and renderFrame() puts
// that raster over what it is given.
TEST_P(CamstandCanvas, AFrameIsDrawnOnTheCpu) {
  std::unique_ptr<ToonzScene> scene = loadScene(GetParam());
  ASSERT_GT(scene->getFrameCount(), 5);

  for (int row : {0, 5}) {
    SCOPED_TRACE("row " + std::to_string(row));
    TRaster32P frame(320, 180);
    ASSERT_TRUE(paintOnRaster(*scene, row, frame));
    EXPECT_GT(drawnPixels(frame), 300);

    TRaster32P expected(320, 180), icon(320, 180);
    expected->fill(TPixel32(255, 255, 255, 255));
    icon->fill(TPixel32(255, 255, 255, 255));
    TRop::over(expected, frame);
    scene->renderFrame(icon, row, 0, false);
    EXPECT_TRUE(samePixels(icon, expected));
  }
}

INSTANTIATE_TEST_SUITE_P(ReferenceScenes, CamstandCanvas,
                         ::testing::Values("vector_basic", "vector_styles",
                                           "raster_png_basic",
                                           "toonz_raster_basic", "subxsheet",
                                           "pegbar_hierarchy", "stage_showcase",
                                           "plastic_basic"));
