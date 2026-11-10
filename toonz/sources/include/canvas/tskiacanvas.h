#pragma once

#ifndef TSKIACANVAS_H
#define TSKIACANVAS_H

#include "canvas/tcanvas.h"

#include <cstddef>
#include <memory>

#if defined(_WIN32)
#if defined(TNZSKIA_EXPORTS)
#define TNZSKIA_API __declspec(dllexport)
#elif defined(TNZSKIA_STATIC)
#define TNZSKIA_API
#else
#define TNZSKIA_API __declspec(dllimport)
#endif
#else
#define TNZSKIA_API __attribute__((visibility("default")))
#endif

//! The canvas that rasterises on the CPU with Skia.
/*!
  It draws straight into pixels the caller owns: 32 bits per pixel, bytes in
  the order B G R A, premultiplied, rows bottom up. That is the layout of a
  TRaster32, so a raster can be drawn into without a copy:

      raster->lock();
      TSkiaCanvas canvas(raster->getRawData(), raster->getLx(),
                         raster->getLy(), raster->getWrap() * 4);

  The pixels must outlive the canvas. What is already in them is drawn over,
  not cleared.

  This header shows nothing of Skia, and the library behind it (tnzskia) is
  the only code of the tree that includes Skia's headers. Like tnzcanvas it
  uses neither Qt nor tnzcore.

  How the calls of TCanvas are carried out where the choice is not obvious:

  - A paint that grows a shape (TCanvasPaint::growDevice) fills the shape and
    a band of that many device pixels around its outline as one area, so the
    paint is applied once. The outline's corners are rounded. A path filled
    by winding number is expected to be wound one way where it is filled,
    its holes the other way; one that winds some filled parts clockwise and
    others counter-clockwise loses the inner half of the band around the
    minority. The even-odd rule has no such condition, and costs more.
  - A pen in device units draws lines of that many pixels whatever the
    transform, and its dashes are measured in device pixels. With a pen in
    local units the dashes are still in device pixels, converted with
    pixelSize(), which is exact only while the transform keeps shapes
    similar.
  - A mesh without colours of its own is filled as the union of its
    triangles, however each one is wound.
  - A mesh with a colour per vertex, or with texture coordinates, is drawn
    triangle by triangle into a layer; where triangles overlap the later
    one replaces the earlier one, and the layer is then blended once. Its
    smooth edges come from clipping to the union of the triangles. Such a
    mesh is not grown.
*/
class TNZSKIA_API TSkiaCanvas final : public TCanvas {
public:
  //! \p rowBytes is the distance in bytes from one row to the next one up,
  //! at least 4 * \p width.
  TSkiaCanvas(void *pixels, int width, int height, std::ptrdiff_t rowBytes);
  ~TSkiaCanvas() override;

  //! False when the constructor was given pixels it cannot draw into; such
  //! a canvas accepts every call and draws nothing.
  bool isValid() const;

  TCanvasIRect deviceBounds() const override;

protected:
  void onSave() override;
  void onRestore() override;
  void onConcat(const TCanvasAffine &aff) override;
  void onClipPath(const TCanvasPath &path, TCanvasFillRule rule,
                  TCanvasClipOp op, bool antialias) override;
  void onClipMesh(const TCanvasMesh &mesh, TCanvasClipOp op) override;
  void onFillPath(const TCanvasPath &path, TCanvasFillRule rule,
                  const TCanvasPaint &paint) override;
  void onFillPathAndClip(const TCanvasPath &path, TCanvasFillRule rule,
                         const TCanvasPaint &paint) override;
  void onStrokePath(const TCanvasPath &path, const TCanvasPen &pen,
                    const TCanvasPaint &paint) override;
  void onDrawMesh(const TCanvasMesh &mesh, const TCanvasPaint &paint) override;
  void onDrawImage(const TCanvasImage &image,
                   const TCanvasAffine &imageToLocal,
                   const TCanvasImageOptions &options) override;

private:
  struct Imp;
  std::unique_ptr<Imp> m_imp;
};

#endif  // TSKIACANVAS_H
