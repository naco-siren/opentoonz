#pragma once

#ifndef TCANVAS_H
#define TCANVAS_H

#include "canvas/tcanvastypes.h"

//! The drawing interface of the path-level canvas IR.
/*!
  Code that draws (colour styles, the vector renderer, later the viewer and
  the tools) calls a TCanvas and knows nothing about what is behind it: a
  CPU rasteriser, the legacy OpenGL bridge or a recorder.

  There is no hidden state. Colour, antialiasing and blending travel with
  each call in a TCanvasPaint. The canvas itself only carries the current
  transform and the current clip, both scoped by save() and restore().

  Device space is one unit per pixel, origin at the bottom left, y up, pixel
  centres at half-integers: the convention of TRaster.

  The public functions keep the transform stack and drop calls that cannot
  draw anything; backends implement the protected on...() functions.
*/
class TNZCANVAS_API TCanvas {
public:
  TCanvas();
  virtual ~TCanvas();

  TCanvas(const TCanvas &)            = delete;
  TCanvas &operator=(const TCanvas &) = delete;

  //! Remembers the transform and the clip until the matching restore().
  void save();
  //! Without a matching save() this does nothing.
  void restore();
  //! Number of save() calls not yet restored.
  int saveCount() const { return (int)m_transforms.size() - 1; }

  //! Applies \p aff before the current transform, like glMultMatrix: what
  //! is drawn afterwards is in the coordinates \p aff maps from.
  void concat(const TCanvasAffine &aff);

  //! Restricts drawing to the inside (Intersect) or the outside
  //! (Difference) of \p path, until the enclosing restore().
  void clipPath(const TCanvasPath &path, TCanvasFillRule rule,
                TCanvasClipOp op, bool antialias);

  void fillPath(const TCanvasPath &path, TCanvasFillRule rule,
                const TCanvasPaint &paint);
  void strokePath(const TCanvasPath &path, const TCanvasPen &pen,
                  const TCanvasPaint &paint);
  void drawMesh(const TCanvasMesh &mesh, const TCanvasPaint &paint);
  //! Draws \p image with its bottom-left pixel corner at the origin of the
  //! space \p imageToLocal maps to the current one, one unit per pixel.
  void drawImage(const TCanvasImage &image, const TCanvasAffine &imageToLocal,
                 const TCanvasImageOptions &options);

  //! From the coordinates of the next draw call to device pixels.
  const TCanvasAffine &transform() const { return m_transforms.back(); }
  //! The size of a device pixel in current coordinates: what styles used to
  //! read back from the OpenGL modelview matrix (tglGetPixelSize2).
  double pixelSize() const;

  //! The pixels this canvas can draw to; useful for culling.
  virtual TCanvasIRect deviceBounds() const = 0;

protected:
  virtual void onSave()    = 0;
  virtual void onRestore() = 0;
  //! transform() already includes \p aff when this is called.
  virtual void onConcat(const TCanvasAffine &aff) = 0;
  virtual void onClipPath(const TCanvasPath &path, TCanvasFillRule rule,
                          TCanvasClipOp op, bool antialias) = 0;
  virtual void onFillPath(const TCanvasPath &path, TCanvasFillRule rule,
                          const TCanvasPaint &paint)        = 0;
  virtual void onStrokePath(const TCanvasPath &path, const TCanvasPen &pen,
                            const TCanvasPaint &paint)      = 0;
  virtual void onDrawMesh(const TCanvasMesh &mesh,
                          const TCanvasPaint &paint)        = 0;
  virtual void onDrawImage(const TCanvasImage &image,
                           const TCanvasAffine &imageToLocal,
                           const TCanvasImageOptions &options) = 0;

private:
  std::vector<TCanvasAffine> m_transforms;  // the current one is at the back
};

#endif  // TCANVAS_H
