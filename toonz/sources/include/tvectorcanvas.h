#pragma once

#ifndef TVECTORCANVAS_H
#define TVECTORCANVAS_H

// Drawing a vector image on a canvas (doc/nexttoonz_canvas_ir.md): what
// tglDraw() in tvectorgl.h does, without OpenGL.

#include "tcommon.h"

#undef DVAPI
#undef DVVAR
#ifdef TVRENDER_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

class TCanvas;
class TRegion;
class TStroke;
class TVectorImage;
class TVectorRenderData;

//! Whether tcanvasDraw() can draw this image with these settings: every
//! style the image uses has to draw on a canvas what the image uses it for
//! (TColorStyle::canvasSupport), and the settings must not ask for what only
//! the OpenGL viewers have (the 3D view, guided drawing).
/*!
  While styles move to the canvas one by one, an image that fails this is
  drawn with tglDraw() as a whole, so that no image is ever rasterised half
  by one renderer and half by another.
*/
DVAPI bool tcanvasCanDraw(const TVectorRenderData &rd,
                          const TVectorImage *vim);

//! Draws the image the way tglDraw() does: group by group, the regions of a
//! group under its strokes, with the check modes of \p rd. The result on
//! the canvas is premultiplied whatever TVectorRenderData::m_alphaChannel
//! says; that flag is about how OpenGL has to be driven.
DVAPI void tcanvasDraw(TCanvas &canvas, const TVectorRenderData &rd,
                       const TVectorImage *vim);

//! The same for one stroke, drawn as tglDraw(rd, stroke) draws it: with
//! the style \p rd's palette gives it (or the colour of a check mode),
//! whether or not that shows.
DVAPI bool tcanvasCanDraw(const TVectorRenderData &rd, const TStroke *stroke);
DVAPI void tcanvasDraw(TCanvas &canvas, const TVectorRenderData &rd,
                       const TStroke *stroke);

//! The same for one region and the regions inside it, drawn as
//! tglDraw(rd, region) draws them.
DVAPI bool tcanvasCanDraw(const TVectorRenderData &rd, TRegion *region);
DVAPI void tcanvasDraw(TCanvas &canvas, const TVectorRenderData &rd,
                       TRegion *region);

#endif  // TVECTORCANVAS_H
