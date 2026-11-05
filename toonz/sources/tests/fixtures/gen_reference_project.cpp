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
//
// The generator itself needs no OpenGL context and no display: shader fx are
// only declared here (their GLSL runs in tcomposer), and the plastic mesh is
// built from a CPU rasterisation of its texture.

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
#include "tparamset.h"
#include "tspectrumparam.h"
#include "tnotanimatableparam.h"
#include "tparamcontainer.h"
#include "ttonecurveparam.h"
#include "toutputproperties.h"
#include "tsound.h"
#include "tsound_io.h"
#include "tfiletype.h"
#include "tmeshimage.h"
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
#include "toonz/txshsoundlevel.h"
#include "toonz/txshsoundcolumn.h"
#include "toonz/txshmeshcolumn.h"
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
#include "ext/meshbuilder.h"
#include "ext/meshutils.h"
#include "ext/plasticskeleton.h"
#include "ext/plasticskeletondeformation.h"
#include "stdfx/shaderfx.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QFile>
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

// Open stroke through the points of f(x) sampled on [x0, x1]: n quadratic
// chunks whose control points lie on the curve too.
TStroke *makeCurve(double x0, double x1, int n,
                   const std::function<double(double)> &f, double thick,
                   int styleId) {
  std::vector<TThickPoint> pts;
  for (int i = 0; i <= 2 * n; ++i) {
    double x = x0 + (x1 - x0) * i / (2.0 * n);
    pts.push_back(TThickPoint(x, f(x), thick));
  }
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

// Puts fx between `below` (currently connected to the xsheet node) and the
// xsheet node, feeding it into fx's port 0.
void insertFxAbove(TXsheet *xsh, TFx *below, TFx *fx) {
  FxDag *dag = xsh->getFxDag();
  dag->assignUniqueId(fx);
  dag->getInternalFxs()->addFx(fx);
  fx->getInputPort(0)->setFx(below);
  dag->removeFromXsheet(below);
  dag->addToXsheet(fx);
}

// Puts fx between the column's fx and the xsheet node.
void insertFxAboveColumn(TXsheet *xsh, int col, TFx *fx) {
  insertFxAbove(xsh, xsh->getColumn(col)->getFx(), fx);
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
// Fx parameter helpers. They look parameters up by the name the fx binds
// them to (bindParam) and fail loudly on a typo or a type mismatch.

template <class ParamP>
ParamP fxParam(const TFxP &fx, const std::string &name) {
  ParamP param = TParamP(fx->getParams()->getParam(name));
  if (!param)
    throw TException(fx->getFxType() + ": no parameter " + name +
                     " of the expected type");
  return param;
}

void setDouble(const TFxP &fx, const std::string &name, double value) {
  fxParam<TDoubleParamP>(fx, name)->setDefaultValue(value);
}

void setDoubleKey(const TFxP &fx, const std::string &name, int frame,
                  double value) {
  fxParam<TDoubleParamP>(fx, name)->setValue(frame, value);
}

void setRange(const TFxP &fx, const std::string &name, double a, double b) {
  fxParam<TRangeParamP>(fx, name)->setDefaultValue(DoublePair(a, b));
}

void setPoint(const TFxP &fx, const std::string &name, const TPointD &p) {
  fxParam<TPointParamP>(fx, name)->setDefaultValue(p);
}

void setPixel(const TFxP &fx, const std::string &name, const TPixel32 &c) {
  fxParam<TPixelParamP>(fx, name)->setDefaultValue(c);
}

void setSpectrum(const TFxP &fx, const std::string &name, const TPixel32 &c0,
                 const TPixel32 &c1) {
  fxParam<TSpectrumParamP>(fx, name)->setDefaultValue(TSpectrum(c0, c1));
}

// Not animatable: the value, not the default, is what renders and saves.
void setInt(const TFxP &fx, const std::string &name, int value) {
  fxParam<TIntParamP>(fx, name)->setValue(value);
}

TFxP createFx(const std::string &id) {
  TFxP fx = TFx::create(id);
  if (!fx) throw TException("unknown fx " + id);
  return fx;
}

//-----------------------------------------------------------------------------
// Sound
//
// The generator does not link the sound library (initSoundIo lives there),
// so it writes canonical 16-bit PCM WAV files itself and registers a reader
// for exactly that layout. tcomposer and the application load the files with
// the full reader from the sound library.

const int kSoundRate = 22050;

void put16(QByteArray &b, int v) {
  b.append(char(v & 0xff));
  b.append(char((v >> 8) & 0xff));
}

void put32(QByteArray &b, quint32 v) {
  put16(b, int(v & 0xffff));
  put16(b, int(v >> 16));
}

int get16(const QByteArray &b, int pos) {
  return short((uchar(b[pos + 1]) << 8) | uchar(b[pos]));
}

quint32 get32(const QByteArray &b, int pos) {
  return quint32(get16(b, pos) & 0xffff) |
         (quint32(get16(b, pos + 2) & 0xffff) << 16);
}

// Mono 16-bit sine, `seconds` long, at half of full scale.
void writeSineWav(const TFilePath &fp, double freq, double seconds) {
  const int count     = int(seconds * kSoundRate);
  const quint32 bytes = quint32(count) * 2;
  QByteArray b;
  b.append("RIFF");
  put32(b, 36 + bytes);
  b.append("WAVE");
  b.append("fmt ");
  put32(b, 16);              // fmt chunk size
  put16(b, 1);               // PCM
  put16(b, 1);               // mono
  put32(b, kSoundRate);      // sample rate
  put32(b, kSoundRate * 2);  // byte rate
  put16(b, 2);               // block align
  put16(b, 16);              // bits per sample
  b.append("data");
  put32(b, bytes);
  for (int i = 0; i < count; ++i)
    put16(b, int(std::lround(16384.0 *
                             std::sin(2.0 * M_PI * freq * i / kSoundRate))));

  TSystem::touchParentDir(fp);
  QFile f(fp.getQString());
  if (!f.open(QIODevice::WriteOnly) || f.write(b) != b.size())
    throw TException(L"cannot write " + fp.getWideString());
}

class FixtureWavReader final : public TSoundTrackReader {
public:
  FixtureWavReader(const TFilePath &fp) : TSoundTrackReader(fp) {}

  static TSoundTrackReader *create(const TFilePath &fp) {
    return new FixtureWavReader(fp);
  }

  TSoundTrackP load() override {
    QFile f(m_path.getQString());
    if (!f.open(QIODevice::ReadOnly)) return TSoundTrackP();
    const QByteArray b = f.readAll();
    if (b.size() < 44 || !b.startsWith("RIFF") || b.mid(8, 8) != "WAVEfmt " ||
        get16(b, 20) != 1 || get16(b, 34) != 16 || b.mid(36, 4) != "data")
      throw TException(L"unsupported wav layout: " + m_path.getWideString());
    const int channels = get16(b, 22);
    const int rate     = int(get32(b, 24));
    const int count    = int(get32(b, 40)) / (2 * channels);
    if (44 + count * 2 * channels > b.size())
      throw TException(L"truncated wav: " + m_path.getWideString());
    TSoundTrackP st =
        TSoundTrack::create(rate, 16, channels, count, TSound::INT);
    short *samples = (short *)st->getRawData();
    for (int i = 0; i < count * channels; ++i)
      samples[i] = short(get16(b, 44 + 2 * i));
    return st;
  }
};

//-----------------------------------------------------------------------------
// Scene scaffolding

struct SceneBuild {
  std::unique_ptr<ToonzScene> scene;
  TXsheet *xsh = nullptr;
};

SceneBuild newScene(const std::string &name, int frames = kFrames) {
  SceneBuild b;
  b.scene.reset(new ToonzScene());
  TProjectManager::instance()->initializeScene(b.scene.get());
  b.xsh = b.scene->getXsheet();

  TCamera *camera = b.scene->getCurrentCamera();
  camera->setSize(TDimensionD(kCameraInW, kCameraInH));
  camera->setRes(TDimension(kWidth, kHeight));

  TOutputProperties *out = b.scene->getProperties()->getOutputProperties();
  out->setPath(TFilePath("+outputs") + (name + ".png"));
  out->setRange(0, frames - 1, 1);
  out->setFrameRate(24.0);
  return b;
}

void saveScene(SceneBuild &b, const TFilePath &projectFolder,
               const std::string &name) {
  TFilePath scenePath = projectFolder + "scenes" + (name + ".tnz");
  // insertColumn()/removeColumn() do not refresh the xsheet frame count, so
  // without this the <tnz framecount> header of scenes built from zerary
  // columns is written as 0 (see doc/nexttoonz_known_issues.md).
  b.xsh->updateFrameCount();
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

// A sound column next to a vector level. The 1-second 440 Hz tone is
// loaded through ToonzScene::loadLevel, like File > Load Level does, and
// exposed for 8 frames. tcomposer ignores audio when writing images, so the
// frames show only the vector level: the test proves that a scene with a
// sound column loads and renders.
void sceneSoundColumn(const TFilePath &projectFolder) {
  SceneBuild b        = newScene("sound_column");
  TXshSimpleLevel *sl = makeVectorLevel(
      b.scene.get(), L"vec_wave", kFrames,
      [](TVectorImageP vi, TPalette *plt, int f) {
        static int ink = -1, fill = -1;
        if (ink < 0) {
          ink  = addSolidStyle(plt, TPixel32(30, 90, 200, 255));
          fill = addSolidStyle(plt, TPixel32(250, 180, 40, 255));
        }
        // A scrolling waveform and a pulsing dot.
        const double phase = (f - 1) * M_PI / 4.0;
        vi->addStroke(makeCurve(
            -kHalfW * 0.8, kHalfW * 0.8, 32,
            [=](double x) {
              return kHalfH * 0.4 * std::sin(x / kHalfW * 3.0 * M_PI + phase);
            },
            5.0, ink));
        const double r = kHalfH * (0.12 + 0.06 * std::sin(phase));
        vi->addStroke(makeCircle(TPointD(0, -kHalfH * 0.7), r, 0.0, fill));
        fillAllRegions(vi, fill);
      });
  fillColumn(b.xsh, 0, sl, kFrames);

  const TFilePath wav = projectFolder + "extras" + "tone_440hz.wav";
  writeSineWav(wav, 440.0, 1.0);
  TXshLevel *xl       = b.scene->loadLevel(wav);
  TXshSoundLevel *snd = xl ? xl->getSoundLevel() : nullptr;
  if (!snd || snd->getFrameCount() != 24)  // 1 s at 24 fps
    throw TException(L"cannot load " + wav.getWideString());
  // Sound cells are numbered from 0; cell r plays the r-th 1/24 s.
  for (int r = 0; r < kFrames; ++r)
    b.xsh->setCell(r, 1, TXshCell(snd, TFrameId(r)));
  if (!b.xsh->getColumn(1)->getSoundColumn())
    throw TException("column 1 is not a sound column");
  saveScene(b, projectFolder, "sound_column");
}

// A zerary particles column over a colour card. No texture is connected, so
// ParticlesFx draws its default sprite, a small ring rasterised through
// TOfflineGL. A fountain of a few dozen particles rises from the bottom.
void sceneParticlesBasic(const TFilePath &projectFolder) {
  SceneBuild b = newScene("particles_basic");
  addZeraryColumn(b.xsh, 0, TFxUtil::makeColorCard(TPixel32(20, 30, 70, 255)),
                  kFrames);
  TFxP particles = createFx("STD_particlesFx");
  // Lengths and speeds are in stage units (Stage::inch per inch).
  setPoint(particles, "center", TPointD(0.0, -kHalfH * 0.7));
  setDouble(particles, "length", kHalfW * 0.5);
  setDouble(particles, "height", 20.0);
  setDouble(particles, "birth_rate", 4.0);
  setRange(particles, "lifetime", 100.0, 100.0);
  setRange(particles, "speed", 40.0, 70.0);
  setRange(particles, "speed_angle", 160.0, 200.0);  // 180 = straight up
  setDouble(particles, "gravity", 20.0);
  // With the default sprite only the relative scale matters: ParticlesFx
  // normalises the 10x10 sprite by the largest particle scale.
  setRange(particles, "scale", 50.0, 100.0);
  setSpectrum(particles, "birth_color", TPixel32(255, 230, 80, 255),
              TPixel32(255, 120, 40, 255));
  setDouble(particles, "birth_color_fade", 100.0);
  addZeraryColumn(b.xsh, 1, particles, kFrames);
  saveScene(b, projectFolder, "particles_basic");
}

// Several CPU fx families in one dag: a linear gradient (generator) as
// background, a glow (light) followed by an ino blur (ino_ family) on one
// vector column, and a radial blur on the other.
//
//   col 0  linearGradientFx (zerary)                    -> xsheet
//   col 1  vec_glow -> glowFx (Light + Source) -> inoBlurFx -> xsheet
//   col 2  vec_bar  -> radialBlurFx                     -> xsheet
void sceneFxGalleryCpu(const TFilePath &projectFolder) {
  SceneBuild b = newScene("fx_gallery_cpu");

  TFxP gradient = createFx("STD_linearGradientFx");
  setDouble(gradient, "period", kHalfW * 2.0);
  setPixel(gradient, "color1", TPixel32(10, 20, 60, 255));
  setPixel(gradient, "color2", TPixel32(60, 150, 160, 255));
  addZeraryColumn(b.xsh, 0, gradient, kFrames);

  TXshSimpleLevel *disc = makeVectorLevel(
      b.scene.get(), L"vec_glow", kFrames,
      [](TVectorImageP vi, TPalette *plt, int f) {
        static int fill = -1;
        if (fill < 0) fill = addSolidStyle(plt, TPixel32(255, 250, 220, 255));
        const double y = -kHalfH * 0.3 + (f - 1) * kHalfH * 0.08;
        vi->addStroke(
            makeCircle(TPointD(-kHalfW * 0.45, y), kHalfH * 0.25, 0.0, fill));
        fillAllRegions(vi, fill);
      });
  fillColumn(b.xsh, 1, disc, kFrames);

  TXshSimpleLevel *bar = makeVectorLevel(
      b.scene.get(), L"vec_bar", kFrames,
      [](TVectorImageP vi, TPalette *plt, int f) {
        static int fill = -1, ink = -1;
        if (fill < 0) {
          fill = addSolidStyle(plt, TPixel32(230, 60, 60, 255));
          ink  = addSolidStyle(plt, TPixel32(255, 255, 255, 255));
        }
        const double x = kHalfW * 0.25 + (f - 1) * kHalfW * 0.04;
        vi->addStroke(
            makeRect(TRectD(x, -kHalfH * 0.5, x + kHalfW * 0.3, kHalfH * 0.5),
                     6.0, ink));
        fillAllRegions(vi, fill);
      });
  fillColumn(b.xsh, 2, bar, kFrames);

  // Glow: the column lights itself (port 0, Light) and is also the lit
  // source (port 1), so the output is the drawing plus its halo.
  TFxP glow = createFx("STD_glowFx");
  setDouble(glow, "value", 60.0);
  setDouble(glow, "brightness", 150.0);
  setPixel(glow, "color", TPixel32(255, 170, 40, 255));
  setDouble(glow, "fade", 100.0);  // tint the light fully with the colour
  insertFxAboveColumn(b.xsh, 1, glow.getPointer());
  glow->getInputPort(1)->setFx(b.xsh->getColumn(1)->getFx());

  TFxP inoBlur = createFx("STD_inoBlurFx");
  setDouble(inoBlur, "radius", 6.0);
  insertFxAbove(b.xsh, glow.getPointer(), inoBlur.getPointer());

  TFxP radialBlur = createFx("STD_radialBlurFx");
  setPoint(radialBlur, "point", TPointD(0.0, 0.0));
  setDouble(radialBlur, "blur", 15.0);
  insertFxAboveColumn(b.xsh, 2, radialBlur.getPointer());

  saveScene(b, projectFolder, "fx_gallery_cpu");
}

// A GLSL shader fx (stuff/library/shaders/sunflare.xml) as a zerary column
// over a colour card; the rays rotate through an animated parameter.
// tcomposer runs the shader on the GPU, under xvfb with Mesa's llvmpipe.
void sceneShaderFx(const TFilePath &projectFolder) {
  SceneBuild b = newScene("shader_fx");
  addZeraryColumn(b.xsh, 0, TFxUtil::makeColorCard(TPixel32(10, 10, 30, 255)),
                  kFrames);
  TFxP shader = createFx("SHADER_sunflare");
  setPixel(shader, "color", TPixel32(255, 170, 75, 255));
  setInt(shader, "blades", 6);
  setDouble(shader, "intensity", 1.0);
  // pow(sin(a) + bias, sharpness) is undefined in GLSL for a negative base;
  // bias 100 (= +1.0) keeps the base non-negative on every driver.
  setDouble(shader, "bias", 100.0);
  setDouble(shader, "sharpness", 4.0);
  setDoubleKey(shader, "angle", 0, 0.0);
  setDoubleKey(shader, "angle", kFrames - 1, 50.0);
  addZeraryColumn(b.xsh, 1, shader, kFrames);
  saveScene(b, projectFolder, "shader_fx");
}

// CPU coverage mask of a vector image, for meshification: a pixel is opaque
// when its centre lies in a filled region or within a stroke's thickness.
// `bbox` is in pixels of the image scaled by `scale`.
TRaster32P coverageMask(const TVectorImageP &vi, double scale,
                        const TRect &bbox) {
  TRaster32P ras(bbox.getLx(), bbox.getLy());
  ras->fill(TPixel32::Transparent);
  std::vector<TRegion *> regions;
  for (UINT i = 0; i < vi->getRegionCount(); ++i)
    if (vi->getRegion(i)->getStyle() != 0) regions.push_back(vi->getRegion(i));
  for (int y = 0; y < ras->getLy(); ++y) {
    TPixel32 *row = ras->pixels(y);
    for (int x = 0; x < ras->getLx(); ++x) {
      const TPointD p((bbox.x0 + x + 0.5) / scale, (bbox.y0 + y + 0.5) / scale);
      bool inside = false;
      for (TRegion *r : regions)
        if ((inside = r->contains(p))) break;
      for (UINT i = 0; !inside && i < vi->getStrokeCount(); ++i) {
        const TStroke *s = vi->getStroke(i);
        double w, dist2;
        if (!s->getBBox().contains(p) || !s->getNearestW(p, w, dist2, false))
          continue;
        const double thick = s->getThickPoint(w).thick;
        inside             = dist2 <= thick * thick;
      }
      if (inside) row[x] = TPixel32::Black;
    }
  }
  return ras;
}

// Plastic deformation: a vector "arm" bent by an animated skeleton.
//
// The scene is built the way Level > Create Mesh and the Plastic tool build
// it: the vector level is meshified into a mesh level with buildMesh, the
// mesh column is inserted next to the texture column and becomes its
// parent, and a three-vertex skeleton on the mesh column's stage object
// bends the arm.
// At render time FxBuilder inserts the PlasticDeformerFx for the texture.
//
// MeshifyPopup rasterises vector images with TOfflineGL before meshing;
// every vector rasteriser in the code base goes through OpenGL, and the
// generator has no GL context. It therefore builds the coverage mask that
// buildMesh needs with coverageMask() below, at the same resolution and
// placement MeshifyPopup uses.
void scenePlasticBasic(const TFilePath &projectFolder) {
  SceneBuild b = newScene("plastic_basic");

  const double armHalfLen = kHalfW * 0.55, armHalfThick = kHalfH * 0.18;
  TXshSimpleLevel *tex = makeVectorLevel(
      b.scene.get(), L"arm", 1, [=](TVectorImageP vi, TPalette *plt, int) {
        int fill   = addSolidStyle(plt, TPixel32(90, 170, 240, 255));
        int ink    = addSolidStyle(plt, TPixel32(20, 40, 90, 255));
        int stripe = addSolidStyle(plt, TPixel32(250, 210, 60, 255));
        vi->addStroke(makeRect(
            TRectD(-armHalfLen, -armHalfThick, armHalfLen, armHalfThick), 4.0,
            ink));
        fillAllRegions(vi, fill);
        // Cross stripes make the bending visible along the arm.
        for (int i = 1; i < 6; ++i) {
          double x = -armHalfLen + i * armHalfLen / 3.0;
          vi->addStroke(makeLine(TPointD(x, -armHalfThick + 4),
                                 TPointD(x, armHalfThick - 4), 6.0, stripe));
        }
      });
  for (int r = 0; r < kFrames; ++r)
    b.xsh->setCell(r, 0, TXshCell(tex, TFrameId(1)));

  // Meshify (MeshifyPopup defaults: 0.2 inch edges, 5 px margin; 100 dpi
  // instead of 300 keeps the mesh small).
  const double rasDpi = 100.0, scale = rasDpi / Stage::inch;
  const int margin = 5;
  TVectorImageP vi = tex->getFrame(TFrameId(1), false);
  TRectD bboxD     = (TScale(scale) * vi->getBBox()).enlarge(margin + 1);
  TRect bbox(tfloor(bboxD.x0), tfloor(bboxD.y0), tceil(bboxD.x1) - 1,
             tceil(bboxD.y1) - 1);
  const TPointD rasOrigin = convert(bbox.getP00());
  TRaster32P coverage     = coverageMask(vi, scale, bbox);

  MeshBuilderOptions opts;
  opts.m_margin                 = margin;
  opts.m_targetEdgeLength       = 0.2 * rasDpi;
  opts.m_targetMaxVerticesCount = 1000;
  opts.m_transparentColor       = TPixel64::Transparent;
  TMeshImageP mesh              = buildMesh(coverage, opts);
  if (!mesh || mesh->meshes().empty())
    throw TException("meshification produced no mesh");
  // From raster pixels to the mesh level's reference: origin at the world
  // origin, Stage::inch dpi (so mesh units are stage units).
  transform(mesh, TScale(Stage::inch / rasDpi) * TTranslation(rasOrigin));
  mesh->setDpi(Stage::inch, Stage::inch);

  TXshSimpleLevel *ml =
      b.scene->createNewLevel(MESH_XSHLEVEL, L"arm_mesh")->getSimpleLevel();
  ml->setPath(TFilePath("+drawings/arm..mesh"));
  ml->getProperties()->setDpiPolicy(LevelProperties::DP_ImageDpi);
  ml->getProperties()->setDpi(TPointD(Stage::inch, Stage::inch));
  ml->setFrame(TFrameId(1), mesh);
  ml->setDirtyFlag(true);
  ml->save();

  // Mesh column right after the texture column, parent of the texture.
  b.xsh->insertColumn(1, new TXshMeshColumn);
  TStageObject *texObj  = b.xsh->getStageObject(TStageObjectId::ColumnId(0));
  TStageObject *meshObj = b.xsh->getStageObject(TStageObjectId::ColumnId(1));
  meshObj->setParent(texObj->getParent());
  meshObj->setParentHandle(texObj->getParentHandle());
  meshObj->setName(texObj->getName() + "_mesh");
  texObj->setParent(TStageObjectId::ColumnId(1));
  // Lower the mesh column (and with it the texture) so the bent arm stays
  // inside the camera.
  setKey(b.xsh, TStageObjectId::ColumnId(1), TStageObject::T_Y, 0, -1.5);
  for (int r = 0; r < kFrames; ++r)
    b.xsh->setCell(r, 1, TXshCell(ml, TFrameId(1)));

  // Skeleton root -> elbow -> tip along the arm; the elbow and the tip
  // rotate (angles are deltas relative to the parent edge, in degrees).
  PlasticSkeletonDeformationP sd(new PlasticSkeletonDeformation);
  meshObj->setPlasticSkeletonDeformation(sd);
  PlasticSkeletonP skel(new PlasticSkeleton);
  const int skelId = 1;
  sd->attach(skelId, skel.getPointer());
  // The vertices sit slightly off the arm's axis: the mesh is symmetric
  // and has edges along y = 0, and PlasticDeformer silently drops a handle
  // lying exactly on a mesh edge (TTextureMesh::faceContaining finds no
  // face for it).
  const double x0 = -armHalfLen * 0.9, y0 = 3.3;
  const TPointD joints[3] = {TPointD(x0, y0), TPointD(1.7, y0),
                             TPointD(-x0, y0)};
  for (const TPointD &p : joints)
    if (mesh->meshes()[0]->faceContaining(p) < 0)
      throw TException("skeleton vertex outside every mesh face");
  int root      = skel->addVertex(PlasticSkeletonVertex(joints[0]), -1);
  int elbow     = skel->addVertex(PlasticSkeletonVertex(joints[1]), root);
  int tip       = skel->addVertex(PlasticSkeletonVertex(joints[2]), elbow);
  SkVD *elbowVd = sd->vertexDeformation(skelId, elbow);
  SkVD *tipVd   = sd->vertexDeformation(skelId, tip);
  if (!elbowVd || !tipVd)
    throw TException("skeleton vertices have no deformation channels");
  elbowVd->m_params[SkVD::ANGLE]->setValue(0, 0.0);
  elbowVd->m_params[SkVD::ANGLE]->setValue(kFrames - 1, 20.0);
  tipVd->m_params[SkVD::ANGLE]->setValue(0, 0.0);
  tipVd->m_params[SkVD::ANGLE]->setValue(kFrames - 1, 35.0);

  saveScene(b, projectFolder, "plastic_basic");
}

//-----------------------------------------------------------------------------
// Test cards
//
// Full-colour 320x180 rasters that pack many independent features into one
// frame, so that whatever an fx does to hue, luminance, alpha, hard edges,
// fine detail or soft falloff shows up somewhere. Pixels are premultiplied,
// like every TRaster32 in the render pipeline.

// Composites the straight colour (r, g, b) with opacity a (0..1) over p.
void over(TPixel32 &p, double r, double g, double b, double a) {
  a      = std::max(0.0, std::min(1.0, a));
  auto c = [&](double src, int dst) {
    return (TPixel32::Channel)std::lround(
        std::max(0.0, std::min(255.0, src * a + dst * (1.0 - a))));
  };
  p = TPixel32(c(r, p.r), c(g, p.g), c(b, p.b), c(255.0, p.m));
}

// Fills the pixels of `box` with the coverage of `inside` (4x4 supersampled).
void fillShape(const TRaster32P &ras, const TRect &box,
               const std::function<bool(double, double)> &inside, double r,
               double g, double b, double a = 1.0) {
  TRect clip = box * ras->getBounds();
  for (int y = clip.y0; y <= clip.y1; ++y)
    for (int x = clip.x0; x <= clip.x1; ++x) {
      int hits = 0;
      for (int sy = 0; sy < 4; ++sy)
        for (int sx = 0; sx < 4; ++sx)
          if (inside(x + (sx + 0.5) / 4.0, y + (sy + 0.5) / 4.0)) ++hits;
      if (hits) over(ras->pixels(y)[x], r, g, b, a * hits / 16.0);
    }
}

void fillRect(const TRaster32P &ras, const TRect &box, double r, double g,
              double b, double a = 1.0) {
  fillShape(ras, box, [](double, double) { return true; }, r, g, b, a);
}

void fillDisc(const TRaster32P &ras, double cx, double cy, double radius,
              double r, double g, double b, double a = 1.0,
              double innerRadius = 0.0) {
  TRect box(int(cx - radius) - 1, int(cy - radius) - 1, int(cx + radius) + 1,
            int(cy + radius) + 1);
  fillShape(
      ras, box,
      [=](double x, double y) {
        double d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
        return d2 <= radius * radius && d2 >= innerRadius * innerRadius;
      },
      r, g, b, a);
}

void hsvToRgb(double h, double s, double v, double &r, double &g, double &b) {
  h        = std::fmod(h, 360.0) / 60.0;
  int i    = int(h);
  double f = h - i, p = v * (1 - s), q = v * (1 - s * f),
         t = v * (1 - s * (1 - f));
  switch (i) {
  case 0: r = v, g = t, b = p; break;
  case 1: r = q, g = v, b = p; break;
  case 2: r = p, g = v, b = t; break;
  case 3: r = p, g = q, b = v; break;
  case 4: r = t, g = p, b = v; break;
  default: r = v, g = p, b = q; break;
  }
  r *= 255, g *= 255, b *= 255;
}

// The main source: transparent margin and background; from top to bottom
//   - hue ramp, luminance ramp, alpha ramp (three strips)
//   - hard-edged primaries: disc, square, triangle; a white square with a
//     1 px grid; a half-transparent disc overlapping an opaque square
//   - fine detail: 1 px checker, 1 px lines at growing pitch, thin diagonals;
//     a soft gaussian blob; a thin ring
void drawCardMain(TRaster32P ras, int) {
  const int x0 = 16, x1 = 303;
  for (int x = x0; x <= x1; ++x) {
    double t = (x - x0) / double(x1 - x0), r, g, b;
    hsvToRgb(t * 359.0, 1.0, 1.0, r, g, b);
    for (int y = 152; y <= 167; ++y) over(ras->pixels(y)[x], r, g, b, 1.0);
    for (int y = 135; y <= 148; ++y)
      over(ras->pixels(y)[x], t * 255, t * 255, t * 255, 1.0);
    for (int y = 118; y <= 131; ++y) over(ras->pixels(y)[x], 255, 140, 0, t);
  }

  fillDisc(ras, 46, 86, 22, 230, 30, 30);
  fillRect(ras, TRect(82, 66, 121, 105), 30, 180, 60);
  fillShape(
      ras, TRect(134, 66, 176, 106),
      [](double x, double y) {
        return y >= 66 && y <= 106 && std::abs(x - 155) <= (106 - y) * 0.5;
      },
      40, 70, 230);
  fillRect(ras, TRect(188, 66, 227, 105), 255, 255, 255);
  for (int i = 188; i <= 227; i += 8) {
    fillRect(ras, TRect(i, 66, i, 105), 0, 0, 0);
    fillRect(ras, TRect(188, i - 122, 227, i - 122), 0, 0, 0);
  }
  fillRect(ras, TRect(240, 70, 275, 105), 0, 200, 220);
  fillDisc(ras, 278, 80, 19, 230, 0, 200, 0.5);

  for (int y = 14; y <= 49; ++y)
    for (int x = 16; x <= 55; ++x) {
      double v = ((x + y) & 1) ? 255 : 0;
      over(ras->pixels(y)[x], v, v, v, 1.0);
    }
  fillRect(ras, TRect(64, 14, 119, 49), 255, 255, 255);
  for (int x = 66, pitch = 2; x <= 117; x += pitch, ++pitch)
    fillRect(ras, TRect(x, 14, x, 49), 0, 0, 0);
  for (int k = 0; k < 5; ++k)
    fillShape(
        ras, TRect(128, 14, 183, 49),
        [=](double x, double y) {
          double d = (y - 14) - (x - 128 - k * 9) * 0.9;
          return std::abs(d) <= 0.6 + 0.3 * k;
        },
        20, 20, 20);
  for (int y = 12; y <= 52; ++y)
    for (int x = 194; x <= 236; ++x) {
      double d2 = (x - 215.0) * (x - 215.0) + (y - 32.0) * (y - 32.0);
      over(ras->pixels(y)[x], 255, 255, 255, std::exp(-d2 / (2 * 8.0 * 8.0)));
    }
  fillDisc(ras, 276, 32, 15, 250, 210, 30, 1.0, 12);
}

// The second source of two-input fx (Back, Down, Source2): the whole frame
// in opaque diagonal stripes under a radial light, fading to transparent
// over the right quarter.
void drawCardBack(TRaster32P ras, int) {
  for (int y = 0; y < kHeight; ++y)
    for (int x = 0; x < kWidth; ++x) {
      int band   = ((x + y) / 20) % 3;
      double r   = band == 0 ? 20 : band == 1 ? 30 : 120;
      double g   = band == 0 ? 110 : band == 1 ? 40 : 60;
      double b   = band == 0 ? 120 : band == 1 ? 110 : 170;
      double d   = std::hypot(x - 110.0, y - 100.0) / 140.0;
      double lit = std::max(0.35, 1.25 - d);
      double a   = x < 240 ? 1.0 : std::max(0.0, (kWidth - 1 - x) / 79.0);
      over(ras->pixels(y)[x], r * lit, g * lit, b * lit, a);
    }
}

// The reference input (Reference, Matte, Controller, Depth, Noise, Flow,
// Warper, Iris...): a grey ripple pattern on a ramp, inside a soft-edged
// ellipse.
void drawCardRef(TRaster32P ras, int) {
  for (int y = 0; y < kHeight; ++y)
    for (int x = 0; x < kWidth; ++x) {
      double v = 0.5 + 0.3 * std::sin(x / 12.0) * std::cos(y / 9.0) +
                 0.2 * (x / double(kWidth - 1) - 0.5) * 2.0;
      double e = std::hypot((x - 160.0) / 130.0, (y - 90.0) / 70.0);
      double a = std::max(0.0, std::min(1.0, (1.0 - e) * 8.0));
      v        = std::max(0.0, std::min(1.0, v)) * 255;
      over(ras->pixels(y)[x], v, v, v, a);
    }
}

// A one-frame card level, shared by all the scenes that use it: the first
// scene draws and saves it, the others load the saved file.
// (createNewLevel() looks for a free name when the file already exists,
// which would give every scene its own renamed copy.)
TXshSimpleLevel *cardLevel(ToonzScene *scene, const TFilePath &projectFolder,
                           const std::wstring &name, const RgbDrawer &draw) {
  const TFilePath frame =
      projectFolder + "drawings" + (name + L".0001.png");
  if (!TFileStatus(frame).doesExist())
    return makeRasterLevel(scene, name, 1, draw);
  TXshLevel *xl = scene->loadLevel(projectFolder + "drawings" + (name + L"..png"));
  TXshSimpleLevel *sl = xl ? xl->getSimpleLevel() : nullptr;
  if (!sl) throw TException(L"cannot load " + frame.getWideString());
  sl->getProperties()->setDpiPolicy(LevelProperties::DP_CustomDpi);
  sl->getProperties()->setDpi(TPointD(kDpi, kDpi));
  return sl;
}

//-----------------------------------------------------------------------------
// Fx sweep
//
// Every registered fx, one per frame, applied to the test cards with its
// default parameters. A source column has a cell only on the frame of the fx
// it feeds, so each frame shows exactly one fx.

enum CardKind { CardMain, CardBack, CardRef };

// Which card feeds an input port, from the port's name.
CardKind cardForPort(const std::string &portName, int index) {
  QString n = QString::fromStdString(portName).toLower();
  for (const char *ref :
       {"reference", "matte", "control", "depth", "noise", "flow", "warper",
        "hori", "vert", "displacement", "thickness", "height", "iris",
        "source3", "area"})
    if (n.startsWith(ref)) return CardRef;
  for (const char *back : {"back", "down", "basesrc", "source2", "source4"})
    if (n.startsWith(back)) return CardBack;
  if (index == 0) return CardMain;
  for (const char *main_ :
       {"source", "fore", "up", "blendsrc", "light", "shape", "color",
        "texture", "brush"})
    if (n.startsWith(main_)) return CardMain;
  return CardBack;
}

// Fxs that are not swept, and why.
const char *sweepExclusion(const TFxInfo &info) {
  if (info.m_isHidden) return "internal fx";
  if (info.m_name == "macroFx") return "container, no behaviour of its own";
  if (info.m_name == "externalProgramFx") return "runs an external program";
  return nullptr;
}

struct SweepEntry {
  std::string scene;
  int frame;  // 1-based, as in the output file names
  std::string fxId;
  std::string inputs;
};

void sceneFxSweep(const TFilePath &projectFolder, const std::string &name,
                  const std::vector<std::string> &fxIds,
                  std::vector<SweepEntry> &manifest) {
  const int frames = int(fxIds.size());
  SceneBuild b     = newScene(name, frames);
  TXshSimpleLevel *cards[3] = {
      cardLevel(b.scene.get(), projectFolder, L"card_main", drawCardMain),
      cardLevel(b.scene.get(), projectFolder, L"card_back", drawCardBack),
      cardLevel(b.scene.get(), projectFolder, L"card_ref", drawCardRef)};
  const char *cardNames[3] = {"main", "back", "ref"};

  FxDag *dag = b.xsh->getFxDag();
  int col    = 0;
  for (int row = 0; row < frames; ++row) {
    TFxP fx = createFx(fxIds[row]);
    SweepEntry entry{name, row + 1, fxIds[row], ""};
    if (fx->getInputPortCount() == 0) {
      // Zerary fx: its own column, exposed on this frame only.
      TXshZeraryFxColumn *column = new TXshZeraryFxColumn(row + 1);
      if (row > 0) column->clearCells(0, row);
      column->getZeraryColumnFx()->setZeraryFx(fx.getPointer());
      b.xsh->insertColumn(col++, column);
      dag->assignUniqueId(fx.getPointer());
    } else {
      dag->assignUniqueId(fx.getPointer());
      dag->getInternalFxs()->addFx(fx.getPointer());
      for (int i = 0; i < fx->getInputPortCount(); ++i) {
        const std::string portName = fx->getInputPortName(i);
        const CardKind kind        = cardForPort(portName, i);
        b.xsh->setCell(row, col, TXshCell(cards[kind], TFrameId(1)));
        TFx *columnFx = b.xsh->getColumn(col)->getFx();
        fx->getInputPort(i)->setFx(columnFx);
        dag->removeFromXsheet(columnFx);
        ++col;
        entry.inputs += (i ? " " : "") + portName + "=" + cardNames[kind];
      }
      dag->addToXsheet(fx.getPointer());
    }
    manifest.push_back(entry);
  }
  saveScene(b, projectFolder, name);
}

// Builds the fx_sweep_* scenes: base compositing fxs, then the standard fxs
// by family, 16 per scene. GPU shader fxs get scenes of their own
// (fx_sweep_gpu_*), since their output depends on the driver.
void scenesFxSweep(const TFilePath &projectFolder) {
  std::vector<TFxInfo> infos;
  TFx::listFxs(infos);
  std::vector<std::string> cpu, gpu;
  std::vector<std::pair<std::string, std::string>> excluded;
  for (const TFxInfo &info : infos) {
    if (const char *why = sweepExclusion(info))
      excluded.push_back({info.m_name, why});
    else if (info.m_name.compare(0, 7, "SHADER_") == 0)
      gpu.push_back(info.m_name);
    else
      cpu.push_back(info.m_name);
  }
  // Base fxs first, then STD_ classic, STD_ino*, STD_iwa_*; alphabetical
  // within each group, so that adding an fx moves as few frames as possible.
  auto family = [](const std::string &id) {
    if (id.compare(0, 4, "STD_") != 0) return 0;
    if (id.compare(0, 7, "STD_ino") == 0) return 2;
    if (id.compare(0, 8, "STD_iwa_") == 0) return 3;
    return 1;
  };
  auto byFamily = [&](const std::string &a, const std::string &b) {
    return std::make_pair(family(a), a) < std::make_pair(family(b), b);
  };
  std::sort(cpu.begin(), cpu.end(), byFamily);
  std::sort(gpu.begin(), gpu.end());

  std::vector<SweepEntry> manifest;
  const size_t perScene = 16;
  auto emitScenes = [&](const std::vector<std::string> &ids, const char *prefix) {
    for (size_t i = 0, n = 1; i < ids.size(); i += perScene, ++n) {
      std::vector<std::string> chunk(
          ids.begin() + i, ids.begin() + std::min(ids.size(), i + perScene));
      // A letter suffix: a scene name ending in _<digits> would be read as
      // a frame number when tcomposer builds the output file names.
      const std::string sceneName =
          std::string(prefix) + "_" + char('a' + int(n) - 1);
      sceneFxSweep(projectFolder, sceneName, chunk, manifest);
    }
  };
  emitScenes(cpu, "fx_sweep");
  emitScenes(gpu, "fx_sweep_gpu");

  // The manifest maps every output frame to its fx.
  const TFilePath manifestPath = projectFolder + "extras" + "fx_sweep.txt";
  TSystem::touchParentDir(manifestPath);
  QFile f(manifestPath.getQString());
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    throw TException(L"cannot write " + manifestPath.getWideString());
  QByteArray text("# scene frame fx inputs\n");
  for (const SweepEntry &e : manifest) {
    char frame[16];
    snprintf(frame, sizeof frame, "%04d", e.frame);
    text += (e.scene + " " + frame + " " + e.fxId + " " + e.inputs + "\n")
                .c_str();
  }
  for (const auto &x : excluded)
    text += ("# not swept: " + x.first + " (" + x.second + ")\n").c_str();
  f.write(text);
  std::cout << "  wrote " << ::to_string(manifestPath) << " (" << manifest.size()
            << " fxs)" << std::endl;
}

//-----------------------------------------------------------------------------
// Fx inventory

std::string paramKind(const TParamP &param) {
  if (TDoubleParamP(param)) return "double";
  if (TRangeParamP(param)) return "range";
  if (TPointParamP(param)) return "point";
  if (TPixelParamP(param)) return "pixel";
  if (TSpectrumParamP(param)) return "spectrum";
  if (TIntEnumParamP(param)) return "enum";
  if (TIntParamP(param)) return "int";
  if (TBoolParamP(param)) return "bool";
  if (TStringParamP(param)) return "string";
  if (TFilePathParamP(param)) return "path";
  if (TToneCurveParamP(param)) return "tonecurve";
  return "other";
}

// One line per registered fx: id, hidden flag, input ports, parameters.
void listFxs() {
  std::vector<TFxInfo> infos;
  TFx::listFxs(infos);
  std::sort(infos.begin(), infos.end(),
            [](const TFxInfo &a, const TFxInfo &b) { return a.m_name < b.m_name; });
  for (const TFxInfo &info : infos) {
    TFxP fx = TFx::create(info.m_name);
    std::cout << info.m_name << (info.m_isHidden ? " [hidden]" : "");
    if (!fx) {
      std::cout << " [not creatable]" << std::endl;
      continue;
    }
    std::cout << " ports(";
    for (int i = 0; i < fx->getInputPortCount(); ++i)
      std::cout << (i ? "," : "") << fx->getInputPortName(i);
    std::cout << ")" << (fx->hasDynamicPortGroups() ? " [dynamic ports]" : "");
    std::cout << " params(";
    TParamContainer *params = fx->getParams();
    for (int i = 0; i < params->getParamCount(); ++i)
      std::cout << (i ? "," : "") << params->getParamName(i) << ":"
                << paramKind(params->getParam(i));
    std::cout << ")" << std::endl;
  }
}

//-----------------------------------------------------------------------------

TFilePath absolutePath(const char *arg) {
  return TFilePath(
      QFileInfo(QString::fromLocal8Bit(arg)).absoluteFilePath().toStdWString());
}

}  // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  const bool listOnly = argc == 3 && std::string(argv[1]) == "--list-fx";
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <stuff dir> <output project dir>\n"
              << "       " << argv[0] << " --list-fx <stuff dir>" << std::endl;
    return 1;
  }

  const TFilePath stuffSource   = absolutePath(argv[listOnly ? 2 : 1]);
  const TFilePath projectFolder = listOnly ? TFilePath() : absolutePath(argv[2]);

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
  // Shader fx are declared from their interface files, as tcomposer does.
  loadShaderInterfaces(stuff + "library" + "shaders");
  TSoundTrackReader::define("wav", FixtureWavReader::create);
  TFileType::declare("wav", TFileType::AUDIO_LEVEL);
  Preferences::instance();

  if (listOnly) {
    listFxs();
    return 0;
  }

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
    sceneSoundColumn(projectFolder);
    sceneParticlesBasic(projectFolder);
    sceneFxGalleryCpu(projectFolder);
    sceneShaderFx(projectFolder);
    scenePlasticBasic(projectFolder);
    scenesFxSweep(projectFolder);
  } catch (const TException &e) {
    std::cerr << "error: " << ::to_string(e.getMessage()) << std::endl;
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
