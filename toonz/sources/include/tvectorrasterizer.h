#pragma once

#ifndef TVECTORRASTERIZER_H
#define TVECTORRASTERIZER_H

// Vector images rasterised on the CPU, without an OpenGL context: the
// canvas traversal of tvectorcanvas.h drawn with the Skia canvas of
// tnzskia. The alternative to TOfflineGL::draw() for renders.

#include "traster.h"
#include "tvectorimage.h"

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
