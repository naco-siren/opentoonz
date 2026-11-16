#include "tvectorrasterizer.h"

#include "tvectorcanvas.h"
#include "tvectorrenderdata.h"
#include "tmachine.h"
#include "trop.h"

#ifdef NEXTTOONZ_WITH_SKIA
#include "canvas/tskiacanvas.h"
#endif

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace {

// -1: not decided yet, the environment is asked on first use.
std::atomic<int> s_selected(-1);
std::atomic<bool> s_growRead(false);
const double kDefaultStrokeGrow = 0.5;
std::atomic<double> s_strokeGrow(kDefaultStrokeGrow);

}  // namespace

//-----------------------------------------------------------------------------

bool TVectorRasterizer::isAvailable() {
// The Skia canvas writes B, G, R, A bytes, which is a TPixel32 only where
// the channels are laid out in that order.
#if defined(NEXTTOONZ_WITH_SKIA) && defined(TNZ_MACHINE_CHANNEL_ORDER_BGRM)
  return true;
#else
  return false;
#endif
}

//-----------------------------------------------------------------------------

bool TVectorRasterizer::isSelected() {
  if (!isAvailable()) return false;
  int selected = s_selected.load();
  if (selected < 0) {
    // The CPU is the default (migration step 9 of doc/nexttoonz_canvas_ir.md);
    // NEXTTOONZ_VECTOR_RASTERIZER=gl goes back to TOfflineGL.
    const char *value = std::getenv("NEXTTOONZ_VECTOR_RASTERIZER");
    selected          = (value && std::strcmp(value, "gl") == 0) ? 0 : 1;
    s_selected.store(selected);
  }
  return selected == 1;
}

void TVectorRasterizer::select(bool on) { s_selected.store(on ? 1 : 0); }

//-----------------------------------------------------------------------------

double TVectorRasterizer::strokeGrow() {
  if (!s_growRead.exchange(true)) {
    const char *value = std::getenv("NEXTTOONZ_STROKE_GROW");
    if (value && *value) {
      char *end           = 0;
      const double pixels = std::strtod(value, &end);
      // Anything that is not a number, or less than nothing, is not heard.
      if (end != value && pixels >= 0.0) s_strokeGrow.store(pixels);
    }
  }
  return s_strokeGrow.load();
}

void TVectorRasterizer::setStrokeGrow(double pixels) {
  s_growRead.store(true);
  s_strokeGrow.store(pixels > 0.0 ? pixels : 0.0);
}

double TVectorRasterizer::defaultStrokeGrow() { return kDefaultStrokeGrow; }

//-----------------------------------------------------------------------------

namespace {

#if defined(NEXTTOONZ_WITH_SKIA) && defined(TNZ_MACHINE_CHANNEL_ORDER_BGRM)
// Calls draw(canvas) with a Skia canvas over the raster's pixels. A TRaster32
// is what the Skia canvas draws into: rows bottom up, and as far apart as the
// raster says (it may be a window on a larger one).
template <class Draw>
void onCanvas(const TRaster32P &raster, Draw draw) {
  raster->lock();
  {
    TSkiaCanvas canvas(raster->getRawData(), raster->getLx(), raster->getLy(),
                       (std::ptrdiff_t)raster->getWrap() * 4);
    draw(canvas);
  }
  raster->unlock();
}
#endif

}  // namespace

//-----------------------------------------------------------------------------

bool TVectorRasterizer::rasterize(const TRasterP &raster,
                                  const TVectorImageP &image,
                                  const TVectorRenderData &rd) {
#if defined(NEXTTOONZ_WITH_SKIA) && defined(TNZ_MACHINE_CHANNEL_ORDER_BGRM)
  if (!raster || !image) return false;
  if (!tcanvasCanDraw(rd, image.getPointer())) return false;

  TRaster32P ras32 = raster;
  if (!ras32) ras32 = TRaster32P(raster->getSize());

  ras32->clear();
  onCanvas(ras32, [&](TCanvas &canvas) {
    tcanvasDraw(canvas, rd, image.getPointer());
  });

  if (ras32.getPointer() != raster.getPointer()) TRop::copy(raster, ras32);
  return true;
#else
  return false;
#endif
}

//-----------------------------------------------------------------------------

bool TVectorRasterizer::draw(const TRaster32P &raster,
                             const TVectorImage *image,
                             const TVectorRenderData &rd) {
#if defined(NEXTTOONZ_WITH_SKIA) && defined(TNZ_MACHINE_CHANNEL_ORDER_BGRM)
  if (!raster || !image) return false;
  if (!tcanvasCanDraw(rd, image)) return false;
  onCanvas(raster, [&](TCanvas &canvas) { tcanvasDraw(canvas, rd, image); });
  return true;
#else
  return false;
#endif
}

bool TVectorRasterizer::draw(const TRaster32P &raster, const TStroke *stroke,
                             const TVectorRenderData &rd) {
#if defined(NEXTTOONZ_WITH_SKIA) && defined(TNZ_MACHINE_CHANNEL_ORDER_BGRM)
  if (!raster || !stroke) return false;
  if (!tcanvasCanDraw(rd, stroke)) return false;
  onCanvas(raster,
           [&](TCanvas &canvas) { tcanvasDraw(canvas, rd, stroke); });
  return true;
#else
  return false;
#endif
}

bool TVectorRasterizer::draw(const TRaster32P &raster, TRegion *region,
                             const TVectorRenderData &rd) {
#if defined(NEXTTOONZ_WITH_SKIA) && defined(TNZ_MACHINE_CHANNEL_ORDER_BGRM)
  if (!raster || !region) return false;
  if (!tcanvasCanDraw(rd, region)) return false;
  onCanvas(raster,
           [&](TCanvas &canvas) { tcanvasDraw(canvas, rd, region); });
  return true;
#else
  return false;
#endif
}

//-----------------------------------------------------------------------------

bool TVectorRasterizer::drawOnCanvas(
    const TRaster32P &raster, const std::function<void(TCanvas &)> &draw) {
#if defined(NEXTTOONZ_WITH_SKIA) && defined(TNZ_MACHINE_CHANNEL_ORDER_BGRM)
  if (!raster || !draw) return false;
  onCanvas(raster, draw);
  return true;
#else
  return false;
#endif
}
