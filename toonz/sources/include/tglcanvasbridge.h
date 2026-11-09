#pragma once

#ifndef TGLCANVASBRIDGE_H
#define TGLCANVASBRIDGE_H

// What the old OpenGL drawing code needs to reach a style that draws on a
// canvas: a TGlCanvas over the current context. It goes away with that code.

#include "canvas/tglcanvas.h"

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

//! The stencil masks of a TGlCanvas, kept by the TStencilControl of the
//! current context, so that they nest with the masks drawn around it.
class DVAPI TGlStencilMasks final : public TGlCanvasMasks {
public:
  void beginMask(bool drawOnce) override;
  void endMask() override;
  void enableMask(bool inside) override;
  void disableMask() override;
};

//! A canvas for style code that the old renderer calls in the middle of its
//! own OpenGL drawing (tglDraw() and what it reaches): it draws with the
//! blending, colour mask and line smoothing set up around it.
class TLegacyGlCanvas {
  TGlStencilMasks m_masks;
  TGlCanvas m_canvas;

public:
  TLegacyGlCanvas()
      : m_canvas(TCanvasIRect(), m_masks, TGlCanvas::State::Callers) {}

  TCanvas &canvas() { return m_canvas; }
};

#endif  // TGLCANVASBRIDGE_H
