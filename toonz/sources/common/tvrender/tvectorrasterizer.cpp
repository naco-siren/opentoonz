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
std::atomic<double> s_strokeGrow(0.0);

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
    const char *value = std::getenv("NEXTTOONZ_VECTOR_RASTERIZER");
    selected          = (value && std::strcmp(value, "skia") == 0) ? 1 : 0;
    s_selected.store(selected);
  }
  return selected == 1;
}

void TVectorRasterizer::select(bool on) { s_selected.store(on ? 1 : 0); }

//-----------------------------------------------------------------------------

double TVectorRasterizer::strokeGrow() {
  if (!s_growRead.exchange(true)) {
    if (const char *value = std::getenv("NEXTTOONZ_STROKE_GROW")) {
      const double pixels = std::atof(value);
      if (pixels > 0.0) s_strokeGrow.store(pixels);
    }
  }
  return s_strokeGrow.load();
}

void TVectorRasterizer::setStrokeGrow(double pixels) {
  s_growRead.store(true);
  s_strokeGrow.store(pixels > 0.0 ? pixels : 0.0);
}

//-----------------------------------------------------------------------------

bool TVectorRasterizer::rasterize(const TRasterP &raster,
                                  const TVectorImageP &image,
                                  const TVectorRenderData &rd) {
#if defined(NEXTTOONZ_WITH_SKIA) && defined(TNZ_MACHINE_CHANNEL_ORDER_BGRM)
  if (!raster || !image) return false;
  if (!tcanvasCanDraw(rd, image.getPointer())) return false;

  TRaster32P ras32 = raster;
  if (!ras32) ras32 = TRaster32P(raster->getSize());

  ras32->lock();
  ras32->clear();
  {
    // A TRaster32 is what the Skia canvas draws into: rows bottom up, and
    // as far apart as the raster says (it may be a window on a larger one).
    TSkiaCanvas canvas(ras32->getRawData(), ras32->getLx(), ras32->getLy(),
                       (std::ptrdiff_t)ras32->getWrap() * 4);
    tcanvasDraw(canvas, rd, image.getPointer());
  }
  ras32->unlock();

  if (ras32.getPointer() != raster.getPointer()) TRop::copy(raster, ras32);
  return true;
#else
  return false;
#endif
}
