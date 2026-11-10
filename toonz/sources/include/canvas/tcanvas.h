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
  //! The same with what \p mesh covers, the union of its triangles, and
  //! hard edges: for drawing next to a mesh that has just been drawn, or on
  //! top of it, and nowhere else. A path with the same corners would not
  //! do: a backend is free to cut a quad into triangles its own way.
  void clipMesh(const TCanvasMesh &mesh, TCanvasClipOp op);

  void fillPath(const TCanvasPath &path, TCanvasFillRule rule,
                const TCanvasPaint &paint);
  //! Fills \p path and, until the enclosing restore(), keeps drawing to
  //! where that fill is: what is drawn afterwards lies on the fill like
  //! print on a shape cut out of paper. This is not the same as a fill
  //! followed by a clip to the same path. There, a smooth edge is worked
  //! out twice, once for the fill and once for what is drawn on it, and
  //! the fill shows through along it; here the edge belongs to the two
  //! together. (A backend that cannot put them together first draws the
  //! fill and clips to it.)
  void fillPathAndClip(const TCanvasPath &path, TCanvasFillRule rule,
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
  //! read back from the OpenGL modelview matrix (tglGetPixelSize2). Virtual
  //! for the one backend that still has to read it back there, so that
  //! drawing through it is rounded exactly as it always was.
  virtual double pixelSize() const;

  //! The pixels this canvas can draw to; useful for culling.
  virtual TCanvasIRect deviceBounds() const = 0;

protected:
  //! For a backend whose device coordinates are not where drawing starts:
  //! \p baseToDevice is the transform in force before the first concat().
  explicit TCanvas(const TCanvasAffine &baseToDevice);

  virtual void onSave()    = 0;
  virtual void onRestore() = 0;
  //! transform() already includes \p aff when this is called.
  virtual void onConcat(const TCanvasAffine &aff) = 0;
  virtual void onClipPath(const TCanvasPath &path, TCanvasFillRule rule,
                          TCanvasClipOp op, bool antialias) = 0;
  virtual void onClipMesh(const TCanvasMesh &mesh, TCanvasClipOp op) = 0;
  virtual void onFillPath(const TCanvasPath &path, TCanvasFillRule rule,
                          const TCanvasPaint &paint)        = 0;
  virtual void onFillPathAndClip(const TCanvasPath &path, TCanvasFillRule rule,
                                 const TCanvasPaint &paint) = 0;
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
