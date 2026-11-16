#pragma once

#ifndef TVECTORRASTERIZER_H
#define TVECTORRASTERIZER_H

// Vector images rasterised on the CPU, without an OpenGL context: the
// canvas traversal of tvectorcanvas.h drawn with the Skia canvas of
// tnzskia. The alternative to TOfflineGL::draw() for renders.

#include "traster.h"
#include "tvectorimage.h"

#include <functional>

#undef DVAPI
#undef DVVAR
#ifdef TVRENDER_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

class TVectorRenderData;
class TStroke;
class TRegion;
class TCanvas;

namespace TVectorRasterizer {

//! Whether this build can rasterise on the CPU at all: it has to be built
//! with Skia (WITH_SKIA).
DVAPI bool isAvailable();

//! Whether renders are to try rasterize() before they fall back on
//! TOfflineGL. Off unless the environment has
//! NEXTTOONZ_VECTOR_RASTERIZER=skia when this is first asked, or select()
//! says otherwise. Always false in a build without Skia.
DVAPI bool isSelected();
DVAPI void select(bool on);

//! Replaces what is in \p raster with \p image, drawn over transparent
//! pixels as \p rd says. A 32-bit raster is drawn into directly, any other
//! by way of one.
/*!
  Returns false, and leaves the raster alone, when it cannot: in a build
  without Skia, or when the image uses a style that does not draw on a
  canvas yet (tcanvasCanDraw()). The caller then renders the image the old
  way.
*/
DVAPI bool rasterize(const TRasterP &raster, const TVectorImageP &image,
                     const TVectorRenderData &rd);

//! Draws \p image over what is in \p raster, as \p rd says: what an
//! offscreen OpenGL context does that has been cleared to some colour and
//! is drawn on with tglDraw(). Returns false, and leaves the raster alone,
//! when it cannot (see rasterize()).
/*!
  The raster's pixels are premultiplied, and so is the result, whatever
  TVectorRenderData::m_alphaChannel says.
*/
//! (A plain pointer: an image that is not reference counted, one on the
//! stack, can be drawn without being taken over and deleted.)
DVAPI bool draw(const TRaster32P &raster, const TVectorImage *image,
                const TVectorRenderData &rd);
//! One stroke, as tglDraw(rd, stroke) draws it.
DVAPI bool draw(const TRaster32P &raster, const TStroke *stroke,
                const TVectorRenderData &rd);
//! One region and the regions inside it, as tglDraw(rd, region) draws
//! them.
DVAPI bool draw(const TRaster32P &raster, TRegion *region,
                const TVectorRenderData &rd);

//! Calls \p draw with a canvas that draws over what is in \p raster, on the
//! CPU, in the raster's pixels (one unit a pixel, the origin at its bottom
//! left corner): for what is not a vector image, such as a deformed mesh or
//! brush strokes. Returns false, without calling it, in a build that has no
//! such canvas.
DVAPI bool drawOnCanvas(const TRaster32P &raster,
                        const std::function<void(TCanvas &)> &draw);

//! By how many pixels solid strokes are grown when they are antialiased
//! on a canvas. The old renderer's fringe added about half a pixel on each
//! side of a stroke; a rasteriser that computes coverage draws strokes at
//! their true width unless told to grow them.
/*!
  Half a pixel, the weight strokes have always had: the owner's decision of
  2026-11-09 on question Q2 of doc/nexttoonz_canvas_ir.md, taken from
  pictures of both. NEXTTOONZ_STROKE_GROW=<pixels> in the environment (0 for
  the true width) or setStrokeGrow() changes it, to make such pictures
  again. The OpenGL canvas ignores it; its fringe is what this imitates.
*/
DVAPI double strokeGrow();
DVAPI void setStrokeGrow(double pixels);
DVAPI double defaultStrokeGrow();

}  // namespace TVectorRasterizer

#endif  // TVECTORRASTERIZER_H
