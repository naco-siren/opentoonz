#pragma once

#ifndef TGLCANVAS_H
#define TGLCANVAS_H

#include "canvas/tcanvas.h"

#include <vector>

#if defined(_WIN32)
#if defined(TNZGLCANVAS_EXPORTS)
#define TNZGLCANVAS_API __declspec(dllexport)
#else
#define TNZGLCANVAS_API __declspec(dllimport)
#endif
#else
#define TNZGLCANVAS_API __attribute__((visibility("default")))
#endif

//! The canvas that draws with the fixed-function OpenGL the tree has always
//! used: the bridge that keeps OpenGL targets working while the code that
//! draws moves to the canvas interface.
/*!
  It carries each call out the way the old renderer did, not the way a
  rasteriser with real antialiasing would:

  - A filled path is tessellated with GLU and drawn without antialiasing. A
    paint that antialiases adds a smooth line of one pixel along every
    contour, half of which lies outside the shape. That fringe is all the
    growing there is: TCanvasPaint::growDevice is not looked at.
  - A mesh of one colour gets a fringe one pixel wide that fades outwards,
    if it is a triangle strip (whose even and odd vertices are its two
    sides). A translucent one is drawn through the stencil buffer so that
    it covers each pixel once.
  - A mesh with colours per vertex or texture coordinates is drawn triangle
    by triangle: no fringe, and blended twice where triangles overlap.
  - Lines are OpenGL lines: no caps, no joins, a width the driver may
    clamp, and dashes rounded to a 16-bit stipple pattern.
  - A clip is a stencil mask, without antialiasing. Clips nest through
    TStencilControl, like the masks of the code around this canvas.
  - Gradients are textures of 256 steps.
  - The target is premultiplied, which takes two passes per call: colour,
    then alpha.

  The canvas draws into the OpenGL context that is current when it is
  created, and that context has to be current for every call. When the
  canvas is created, the modelview and projection matrices must map the
  canvas's device coordinates (one unit per pixel, origin at the bottom
  left) to the viewport; TOfflineGL::initMatrix() sets that up. The canvas
  loads its own modelview matrix as transforms are applied and puts the
  first one back when it is destroyed.

  This library (tnzglcanvas) links OpenGL and tnzcore. Nothing that has to
  build without them may depend on it.
*/
class TNZGLCANVAS_API TGlCanvas final : public TCanvas {
public:
  explicit TGlCanvas(const TCanvasIRect &deviceBounds);
  ~TGlCanvas() override;

  TCanvasIRect deviceBounds() const override { return m_bounds; }

protected:
  void onSave() override;
  void onRestore() override;
  void onConcat(const TCanvasAffine &aff) override;
  void onClipPath(const TCanvasPath &path, TCanvasFillRule rule,
                  TCanvasClipOp op, bool antialias) override;
  void onFillPath(const TCanvasPath &path, TCanvasFillRule rule,
                  const TCanvasPaint &paint) override;
  void onStrokePath(const TCanvasPath &path, const TCanvasPen &pen,
                    const TCanvasPaint &paint) override;
  void onDrawMesh(const TCanvasMesh &mesh, const TCanvasPaint &paint) override;
  void onDrawImage(const TCanvasImage &image,
                   const TCanvasAffine &imageToLocal,
                   const TCanvasImageOptions &options) override;

private:
  TCanvasIRect m_bounds;
  // For each save() not yet restored, and for the canvas itself at the
  // front: how many clips have been set since.
  std::vector<int> m_clips;
  double m_base[16];  // the modelview matrix the canvas was created under

  void dropClips(int count);
  void loadTransform();
};

#endif  // TGLCANVAS_H
