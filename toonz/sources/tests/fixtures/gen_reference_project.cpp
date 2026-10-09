// Generates the NextToonz reference project used by the golden-render tests.
//
// Every scene is built programmatically through the toonzlib API, so this
// tool doubles as a characterisation test of scene construction: if a
// refactor changes how levels, columns, pegbars or fx are wired, the
// regenerated .tnz files change and the golden renders flag it.
//
// Scenes are 320x180 pixels (camera 16 x 9 inches at 20 dpi), 8 frames.
//
//   nexttoonz_gen_fixtures <stuff dir> <output project dir>

#include "tenv.h"
#include "tsystem.h"
#include "tfilepath.h"
#include "tiio_std.h"
#include "tnzimage.h"
#include "colorfx.h"
#include "tvectorimage.h"
#include "tstroke.h"
#include "tregion.h"
#include "tpalette.h"
#include "trasterimage.h"
#include "ttoonzimage.h"
#include "trastercm.h"
#include "tpixelcm.h"
#include "tfxutil.h"
#include "tfx.h"
#include "tdoubleparam.h"
#include "toutputproperties.h"
#include "toonz/toonzscene.h"
#include "toonz/tproject.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshlevel.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshchildlevel.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshcolumn.h"
#include "toonz/txshzeraryfxcolumn.h"
#include "toonz/tcolumnfx.h"
#include "toonz/fxdag.h"
#include "toonz/tcolumnfxset.h"
#include "toonz/tcamera.h"
#include "toonz/sceneproperties.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjectid.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/levelproperties.h"
#include "toonz/preferences.h"
#include "toonz/stage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

DV_IMPORT_API void initStdFx();

namespace {

//-----------------------------------------------------------------------------
// Constants

const int kWidth        = 320;
const int kHeight       = 180;
const int kFrames       = 8;
const double kCameraInW = 16.0;  // camera size in inches
const double kCameraInH = 9.0;
const double kDpi       = kWidth / kCameraInW;  // 20 dpi

// Vector images live in stage units: Stage::inch units per inch, origin at
// the camera centre. The camera therefore spans +-kHalfW x +-kHalfH.
const double kHalfW = kCameraInW * Stage::inch / 2.0;
const double kHalfH = kCameraInH * Stage::inch / 2.0;

//-----------------------------------------------------------------------------
// Palette helpers

int addSolidStyle(TPalette *plt, const TPixel32 &color) {
  int styleId = plt->addStyle(color);
  plt->getPage(0)->addStyle(styleId);
  return styleId;
}

//-----------------------------------------------------------------------------
// Vector drawing helpers

// Closed circle made of 8 quadratic chunks.
TStroke *makeCircle(const TPointD &c, double r, double thick, int styleId) {
  std::vector<TThickPoint> pts;
  const int n       = 8;
  const double step = 2.0 * M_PI / n;
  const double cr   = r / std::cos(step / 2.0);  // control-point radius
  for (int i = 0; i < n; ++i) {
    double a  = i * step;
    double am = (i + 0.5) * step;
    pts.push_back(
        TThickPoint(c.x + r * std::cos(a), c.y + r * std::sin(a), thick));
    pts.push_back(
        TThickPoint(c.x + cr * std::cos(am), c.y + cr * std::sin(am), thick));
  }
  pts.push_back(pts[0]);
  TStroke *s = new TStroke(pts);
  s->setSelfLoop(true);
  s->setStyle(styleId);
  return s;
}

TStroke *makeLine(const TPointD &a, const TPointD &b, double thick,
                  int styleId) {
  std::vector<TThickPoint> pts;
  pts.push_back(TThickPoint(a.x, a.y, thick));
  pts.push_back(TThickPoint((a.x + b.x) / 2.0, (a.y + b.y) / 2.0, thick));
  pts.push_back(TThickPoint(b.x, b.y, thick));
  TStroke *s = new TStroke(pts);
  s->setStyle(styleId);
  return s;
}

// Axis-aligned rectangle as a closed stroke.
TStroke *makeRect(const TRectD &r, double thick, int styleId) {
  std::vector<TThickPoint> pts;
  auto corner = [&](double x, double y) {
    pts.push_back(TThickPoint(x, y, thick));
  };
  auto mid = [&](double x, double y) {
    pts.push_back(TThickPoint(x, y, thick));
  };
  corner(r.x0, r.y0);
  mid((r.x0 + r.x1) / 2, r.y0);
  corner(r.x1, r.y0);
  mid(r.x1, (r.y0 + r.y1) / 2);
  corner(r.x1, r.y1);
  mid((r.x0 + r.x1) / 2, r.y1);
  corner(r.x0, r.y1);
  mid(r.x0, (r.y0 + r.y1) / 2);
  corner(r.x0, r.y0);
  TStroke *s = new TStroke(pts);
  s->setSelfLoop(true);
  s->setStyle(styleId);
  return s;
}

void fillAllRegions(TVectorImageP vi, int styleId) {
  vi->findRegions();
  for (UINT i = 0; i < vi->getRegionCount(); ++i)
    vi->getRegion(i)->setStyle(styleId);
}

//-----------------------------------------------------------------------------
// Level factories

using VectorDrawer = std::function<void(TVectorImageP, TPalette *, int frame)>;
using CmDrawer     = std::function<void(TRasterCM32P, TPalette *, int frame)>;
using RgbDrawer    = std::function<void(TRaster32P, int frame)>;

TXshSimpleLevel *makeVectorLevel(ToonzScene *scene, const std::wstring &name,
                                 int frames, const VectorDrawer &draw) {
  TXshLevel *xl       = scene->createNewLevel(PLI_XSHLEVEL, name);
  TXshSimpleLevel *sl = xl->getSimpleLevel();
  TPalette *plt       = sl->getPalette();
  for (int f = 1; f <= frames; ++f) {
    TVectorImageP vi = new TVectorImage();
    vi->setPalette(plt);
    draw(vi, plt, f);
    sl->setFrame(TFrameId(f), vi);
  }
  sl->getProperties()->setDirtyFlag(true);
  plt->setDirtyFlag(true);
  sl->save();
  return sl;
}

TXshSimpleLevel *makeToonzRasterLevel(ToonzScene *scene,
                                      const std::wstring &name, int frames,
                                      const CmDrawer &draw) {
  TXshLevel *xl       = scene->createNewLevel(TZP_XSHLEVEL, name,
                                              TDimension(kWidth, kHeight), kDpi);
  TXshSimpleLevel *sl = xl->getSimpleLevel();
  TPalette *plt       = sl->getPalette();
  for (int f = 1; f <= frames; ++f) {
    TRasterCM32P ras(kWidth, kHeight);
    ras->fill(TPixelCM32());  // ink 0, paint 0, tone 255: transparent
    draw(ras, plt, f);
    TToonzImageP ti = new TToonzImage(ras, ras->getBounds());
    ti->setPalette(plt);
    ti->setDpi(kDpi, kDpi);
    sl->setFrame(TFrameId(f), ti);
  }
  sl->getProperties()->setDirtyFlag(true);
  plt->setDirtyFlag(true);
  sl->save();
  return sl;
}

TXshSimpleLevel *makeRasterLevel(ToonzScene *scene, const std::wstring &name,
                                 int frames, const RgbDrawer &draw) {
  TFilePath fp        = TFilePath("+drawings") + (name + L"..png");
  TXshLevel *xl       = scene->createNewLevel(OVL_XSHLEVEL, name,
                                              TDimension(kWidth, kHeight), kDpi, fp);
  TXshSimpleLevel *sl = xl->getSimpleLevel();
  for (int f = 1; f <= frames; ++f) {
    TRaster32P ras(kWidth, kHeight);
    ras->fill(TPixel32::Transparent);
    draw(ras, f);
    TRasterImageP ri = new TRasterImage(ras);
    ri->setDpi(kDpi, kDpi);
    sl->setFrame(TFrameId(f), ri);
  }
  sl->getProperties()->setDirtyFlag(true);
  sl->save();
  return sl;
}

//-----------------------------------------------------------------------------
// Xsheet helpers

void fillColumn(TXsheet *xsh, int col, TXshLevel *level, int frames,
                int frameOffset = 0) {
  for (int r = 0; r < frames; ++r) {
    int f = ((r + frameOffset) % frames) + 1;
    xsh->setCell(r, col, TXshCell(level, TFrameId(f)));
  }
}

// Puts fx between the column's fx and the xsheet node.
void insertFxAboveColumn(TXsheet *xsh, int col, TFx *fx) {
  TXshColumn *column = xsh->getColumn(col);
  TFx *columnFx      = column->getFx();
  FxDag *dag         = xsh->getFxDag();
  dag->assignUniqueId(fx);
  dag->getInternalFxs()->addFx(fx);
  fx->getInputPort(0)->setFx(columnFx);
  dag->removeFromXsheet(columnFx);
  dag->addToXsheet(fx);
}

void addZeraryColumn(TXsheet *xsh, int col, const TFxP &zeraryFx, int frames) {
  TXshZeraryFxColumn *column = new TXshZeraryFxColumn(frames);
  column->getZeraryColumnFx()->setZeraryFx(zeraryFx.getPointer());
  xsh->insertColumn(col, column);
  xsh->getFxDag()->assignUniqueId(zeraryFx.getPointer());
}

void setKey(TXsheet *xsh, const TStageObjectId &id,
            TStageObject::Channel channel, int frame, double value) {
  xsh->getStageObject(id)->getParam(channel)->setValue(frame, value);
}

//-----------------------------------------------------------------------------
// Scene scaffolding

struct SceneBuild {
  std::unique_ptr<ToonzScene> scene;
  TXsheet *xsh = nullptr;
};

SceneBuild newScene(const std::string &name) {
  SceneBuild b;
  b.scene.reset(new ToonzScene());
  TProjectManager::instance()->initializeScene(b.scene.get());
  b.xsh = b.scene->getXsheet();

  TCamera *camera = b.scene->getCurrentCamera();
  camera->setSize(TDimensionD(kCameraInW, kCameraInH));
  camera->setRes(TDimension(kWidth, kHeight));

  TOutputProperties *out = b.scene->getProperties()->getOutputProperties();
  out->setPath(TFilePath("+outputs") + (name + ".png"));
  out->setRange(0, kFrames - 1, 1);
  out->setFrameRate(24.0);
  return b;
}

void saveScene(SceneBuild &b, const TFilePath &projectFolder,
               const std::string &name) {
  TFilePath scenePath = projectFolder + "scenes" + (name + ".tnz");
  b.scene->save(scenePath, nullptr, /*saveSceneIcon=*/false);
  std::cout << "  wrote " << ::to_string(scenePath) << std::endl;
}

//-----------------------------------------------------------------------------
// Scene definitions

// A filled circle with an outline, moving left to right, plus a fixed
// diagonal thick stroke. Exercises region fill, stroke rendering and
// per-frame vector images.
void sceneVectorBasic(const TFilePath &projectFolder) {
  SceneBuild b        = newScene("vector_basic");
  TXshSimpleLevel *sl = makeVectorLevel(
      b.scene.get(), L"vec_circle", kFrames,
      [](TVectorImageP vi, TPalette *plt, int f) {
        static int fill = -1, ink = -1;
        if (fill < 0) {
          fill = addSolidStyle(plt, TPixel32(220, 40, 40, 255));
          ink  = addSolidStyle(plt, TPixel32(20, 20, 60, 255));
        }
        double t = (f - 1) / double(kFrames - 1);
        TPointD c(-kHalfW * 0.6 + t * kHalfW * 1.2, 0.0);
        vi->addStroke(makeCircle(c, kHalfH * 0.5, 4.0, ink));
        fillAllRegions(vi, fill);
        vi->addStroke(makeLine(TPointD(-kHalfW * 0.9, -kHalfH * 0.8),
                               TPointD(kHalfW * 0.9, kHalfH * 0.8), 9.0, ink));
      });
  fillColumn(b.xsh, 0, sl, kFrames);
  saveScene(b, projectFolder, "vector_basic");
}

// A Toonz raster (tlv) level: a paint-filled rectangle with an ink border
// that grows frame by frame, plus an antialiased ink diagonal.
void sceneToonzRasterBasic(const TFilePath &projectFolder) {
  SceneBuild b        = newScene("toonz_raster_basic");
  TXshSimpleLevel *sl = makeToonzRasterLevel(
      b.scene.get(), L"tz_rect", kFrames,
      [](TRasterCM32P ras, TPalette *plt, int f) {
        static int paint = -1, ink = -1;
        if (paint < 0) {
          paint = addSolidStyle(plt, TPixel32(60, 160, 220, 255));
          ink   = addSolidStyle(plt, TPixel32(10, 10, 10, 255));
        }
        int grow = (f - 1) * 4;
        int x0 = 60 - grow, y0 = 40 - grow, x1 = 200 + grow, y1 = 130 + grow;
        x0 = std::max(x0, 0), y0 = std::max(y0, 0);
        x1 = std::min(x1, kWidth - 1), y1 = std::min(y1, kHeight - 1);
        for (int y = y0; y <= y1; ++y) {
          TPixelCM32 *row = ras->pixels(y);
          for (int x = x0; x <= x1; ++x) {
            bool border =
                (x < x0 + 3 || x > x1 - 3 || y < y0 + 3 || y > y1 - 3);
            row[x] =
                border ? TPixelCM32(ink, paint, 0) : TPixelCM32(0, paint, 255);
          }
        }
        // Diagonal with a tone ramp so antialiased ink is exercised too.
        for (int x = 20; x < kWidth - 20; ++x) {
          int y = 20 + (x - 20) * (kHeight - 40) / (kWidth - 40);
          for (int d = -2; d <= 2; ++d) {
            int yy = y + d;
            if (yy < 0 || yy >= kHeight) continue;
            int tone      = std::abs(d) * 100;
            TPixelCM32 &p = ras->pixels(yy)[x];
            p = TPixelCM32(ink, p.getPaint(), std::min(tone, p.getTone()));
          }
        }
      });
  fillColumn(b.xsh, 0, sl, kFrames);
  saveScene(b, projectFolder, "toonz_raster_basic");
}

// A full-colour raster (png) level: horizontal gradient with a moving
// opaque square and a half-transparent band, to pin down premultiplied
// compositing over the transparent background.
void sceneRasterPngBasic(const TFilePath &projectFolder) {
  SceneBuild b        = newScene("raster_png_basic");
  TXshSimpleLevel *sl = makeRasterLevel(
      b.scene.get(), L"bg_gradient", kFrames, [](TRaster32P ras, int f) {
        for (int y = 0; y < kHeight; ++y) {
          TPixel32 *row = ras->pixels(y);
          for (int x = 0; x < kWidth; ++x) {
            int r  = x * 255 / (kWidth - 1);
            int g  = y * 255 / (kHeight - 1);
            row[x] = TPixel32(r, g, 128, 255);
          }
        }
        int sx = 20 + (f - 1) * 30;
        for (int y = 60; y < 120; ++y)
          for (int x = sx; x < sx + 50 && x < kWidth; ++x)
            ras->pixels(y)[x] = TPixel32(255, 255, 255, 255);
        for (int y = 140; y < 170; ++y)
          for (int x = 0; x < kWidth; ++x)
            ras->pixels(y)[x] = TPixel32(0, 0, 128, 128);
      });
  fillColumn(b.xsh, 0, sl, kFrames);
  saveScene(b, projectFolder, "raster_png_basic");
}

// Two vector columns parented to one pegbar; the pegbar translates and
// rotates, the camera zooms. Exercises the stage object tree and keyframe
// interpolation.
void scenePegbarHierarchy(const TFilePath &projectFolder) {
  SceneBuild b            = newScene("pegbar_hierarchy");
  TXshSimpleLevel *square = makeVectorLevel(
      b.scene.get(), L"vec_square", 1,
      [](TVectorImageP vi, TPalette *plt, int) {
        int fill = addSolidStyle(plt, TPixel32(40, 180, 90, 255));
        int ink  = addSolidStyle(plt, TPixel32(0, 60, 20, 255));
        vi->addStroke(makeRect(TRectD(-60, -60, 60, 60), 3.0, ink));
        fillAllRegions(vi, fill);
      });
  TXshSimpleLevel *dot = makeVectorLevel(
      b.scene.get(), L"vec_dot", 1, [](TVectorImageP vi, TPalette *plt, int) {
        int fill = addSolidStyle(plt, TPixel32(250, 200, 30, 255));
        vi->addStroke(makeCircle(TPointD(0, 0), 25, 0.0, fill));
        fillAllRegions(vi, fill);
      });
  for (int r = 0; r < kFrames; ++r) {
    b.xsh->setCell(r, 0, TXshCell(square, TFrameId(1)));
    b.xsh->setCell(r, 1, TXshCell(dot, TFrameId(1)));
  }
  TStageObjectId peg = TStageObjectId::PegbarId(0);
  b.xsh->getStageObject(peg);  // create it
  b.xsh->getStageObject(TStageObjectId::ColumnId(0))->setParent(peg);
  b.xsh->getStageObject(TStageObjectId::ColumnId(1))->setParent(peg);
  // Dot offset from the pegbar origin so rotation is visible.
  setKey(b.xsh, TStageObjectId::ColumnId(1), TStageObject::T_X, 0, 3.0);

  setKey(b.xsh, peg, TStageObject::T_X, 0, -4.0);
  setKey(b.xsh, peg, TStageObject::T_X, kFrames - 1, 4.0);
  setKey(b.xsh, peg, TStageObject::T_Angle, 0, 0.0);
  setKey(b.xsh, peg, TStageObject::T_Angle, kFrames - 1, 90.0);

  TStageObjectId cam = b.xsh->getStageObjectTree()->getCurrentCameraId();
  // Camera scale is a factor (1.0 = 100%); zooming in to 60% of the view.
  setKey(b.xsh, cam, TStageObject::T_Scale, 0, 1.0);
  setKey(b.xsh, cam, TStageObject::T_Scale, kFrames - 1, 0.6);
  saveScene(b, projectFolder, "pegbar_hierarchy");
}

// Colour card background (zerary column) under a blurred vector column.
// Exercises zerary fx columns and fx insertion in the dag.
void sceneFxBlurOverColorCard(const TFilePath &projectFolder) {
  SceneBuild b = newScene("fx_blur_over_colorcard");
  addZeraryColumn(b.xsh, 0, TFxUtil::makeColorCard(TPixel32(30, 50, 110, 255)),
                  kFrames);
  TXshSimpleLevel *sl = makeVectorLevel(
      b.scene.get(), L"vec_blurred", kFrames,
      [](TVectorImageP vi, TPalette *plt, int f) {
        static int fill = -1, ink = -1;
        if (fill < 0) {
          fill = addSolidStyle(plt, TPixel32(255, 240, 200, 255));
          ink  = addSolidStyle(plt, TPixel32(200, 60, 20, 255));
        }
        double t = (f - 1) / double(kFrames - 1);
        vi->addStroke(
            makeRect(TRectD(-120 + t * 100, -70, 20 + t * 100, 70), 5.0, ink));
        fillAllRegions(vi, fill);
      });
  fillColumn(b.xsh, 1, sl, kFrames);
  // Hold the fx in a smart pointer: TFx::create() returns a refcount-0
  // object, and a temporary TFxP would delete it on destruction.
  TFxP blur = TFx::create("STD_blurFx");
  TFxUtil::setParam(blur, "value", 12.0);
  insertFxAboveColumn(b.xsh, 1, blur.getPointer());
  saveScene(b, projectFolder, "fx_blur_over_colorcard");
}

// A sub-xsheet holding a vector level, exposed in the parent twice with
// different frame offsets. Exercises child levels and nested xsheets.
void sceneSubXsheet(const TFilePath &projectFolder) {
  SceneBuild b        = newScene("subxsheet");
  TXshSimpleLevel *sl = makeVectorLevel(
      b.scene.get(), L"vec_sub", kFrames,
      [](TVectorImageP vi, TPalette *plt, int f) {
        static int fill = -1, ink = -1;
        if (fill < 0) {
          fill = addSolidStyle(plt, TPixel32(120, 60, 200, 255));
          ink  = addSolidStyle(plt, TPixel32(40, 0, 80, 255));
        }
        double r = 20 + f * 8;
        vi->addStroke(makeCircle(TPointD(0, 0), r, 3.0, ink));
        fillAllRegions(vi, fill);
      });
  TXshLevel *childLevel = b.scene->createNewLevel(CHILD_XSHLEVEL, L"sub01");
  TXsheet *sub          = childLevel->getChildLevel()->getXsheet();
  fillColumn(sub, 0, sl, kFrames);

  fillColumn(b.xsh, 0, childLevel, kFrames);
  fillColumn(b.xsh, 1, childLevel, kFrames, kFrames / 2);
  setKey(b.xsh, TStageObjectId::ColumnId(0), TStageObject::T_X, 0, -4.0);
  setKey(b.xsh, TStageObjectId::ColumnId(1), TStageObject::T_X, 0, 4.0);
  saveScene(b, projectFolder, "subxsheet");
}

// Asset-free scene: a checkerboard generator under a panning camera.
void sceneCheckerboardCamera(const TFilePath &projectFolder) {
  SceneBuild b = newScene("checkerboard_camera");
  addZeraryColumn(b.xsh, 0,
                  TFxUtil::makeCheckboard(TPixel32(240, 240, 240, 255),
                                          TPixel32(70, 70, 70, 255), 40.0),
                  kFrames);
  TStageObjectId cam = b.xsh->getStageObjectTree()->getCurrentCameraId();
  setKey(b.xsh, cam, TStageObject::T_X, 0, 0.0);
  setKey(b.xsh, cam, TStageObject::T_X, kFrames - 1, 2.0);
  setKey(b.xsh, cam, TStageObject::T_Angle, 0, 0.0);
  setKey(b.xsh, cam, TStageObject::T_Angle, kFrames - 1, 15.0);
  saveScene(b, projectFolder, "checkerboard_camera");
}

//-----------------------------------------------------------------------------

TFilePath absolutePath(const char *arg) {
  return TFilePath(
      QFileInfo(QString::fromLocal8Bit(arg)).absoluteFilePath().toStdWString());
}

}  // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <stuff dir> <output project dir>"
              << std::endl;
    return 1;
  }

  const TFilePath stuffSource   = absolutePath(argv[1]);
  const TFilePath projectFolder = absolutePath(argv[2]);

  // Work on a scratch copy of stuff: Preferences and the project manager
  // write into it.
  QTemporaryDir scratch;
  if (!scratch.isValid()) {
    std::cerr << "cannot create a temporary directory" << std::endl;
    return 1;
  }
  const TFilePath stuff(scratch.path().toStdWString() + L"/stuff");
  TSystem::copyDir(stuff, stuffSource);

  TEnv::setRootVarName("TOONZROOT");
  TEnv::setSystemVarPrefix("TOONZ");
  TEnv::setApplicationFileName(argv[0]);
  TEnv::setStuffDir(stuff);

  Tiio::defineStd();
  initImageIo();
  initStdFx();
  initColorFx();
  Preferences::instance();

  try {
    // Start from a clean project folder so regeneration is deterministic.
    if (TFileStatus(projectFolder).doesExist())
      TSystem::rmDirTree(projectFolder);
    TSystem::mkDir(projectFolder);

    TProjectManager *pm = TProjectManager::instance();
    pm->addProjectsRoot(projectFolder.getParentDir());
    TFilePath projectPath = pm->projectFolderToProjectPath(projectFolder);
    std::shared_ptr<TProject> project = pm->createStandardProject();
    project->save(projectPath);
    pm->setCurrentProjectPath(projectPath);
    std::cout << "project " << ::to_string(projectPath) << std::endl;

    sceneVectorBasic(projectFolder);
    sceneToonzRasterBasic(projectFolder);
    sceneRasterPngBasic(projectFolder);
    scenePegbarHierarchy(projectFolder);
    sceneFxBlurOverColorCard(projectFolder);
    sceneSubXsheet(projectFolder);
    sceneCheckerboardCamera(projectFolder);
  } catch (const TException &e) {
    std::cerr << "error: " << ::to_string(e.getMessage()) << std::endl;
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
