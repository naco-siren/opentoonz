#pragma once

#ifndef TCANVASRECORDER_H
#define TCANVASRECORDER_H

#include "canvas/tcanvas.h"

#include <string>
#include <variant>

//-----------------------------------------------------------------------------
// One recorded call each

struct TCanvasOpSave {};
struct TCanvasOpRestore {};
struct TCanvasOpConcat {
  TCanvasAffine aff;
};
struct TCanvasOpClipPath {
  TCanvasPath path;
  TCanvasFillRule rule;
  TCanvasClipOp op;
  bool antialias;
};
struct TCanvasOpClipMesh {
  TCanvasMesh mesh;
  TCanvasClipOp op;
};
struct TCanvasOpFillPath {
  TCanvasPath path;
  TCanvasFillRule rule;
  TCanvasPaint paint;
};
struct TCanvasOpFillPathAndClip {
  TCanvasPath path;
  TCanvasFillRule rule;
  TCanvasPaint paint;
};
struct TCanvasOpStrokePath {
  TCanvasPath path;
  TCanvasPen pen;
  TCanvasPaint paint;
};
struct TCanvasOpDrawMesh {
  TCanvasMesh mesh;
  TCanvasPaint paint;
};
struct TCanvasOpDrawImage {
  TCanvasImage image;
  TCanvasAffine imageToLocal;
  TCanvasImageOptions options;
};

typedef std::variant<TCanvasOpSave, TCanvasOpRestore, TCanvasOpConcat,
                     TCanvasOpClipPath, TCanvasOpClipMesh, TCanvasOpFillPath,
                     TCanvasOpFillPathAndClip, TCanvasOpStrokePath,
                     TCanvasOpDrawMesh, TCanvasOpDrawImage>
    TCanvasOp;

//-----------------------------------------------------------------------------

//! A sequence of canvas calls that can be played into any canvas, or
//! printed.
class TNZCANVAS_API TCanvasDisplayList {
public:
  std::vector<TCanvasOp> ops;

  bool isEmpty() const { return ops.empty(); }

  //! Makes the same calls on \p canvas, in order.
  void replay(TCanvas &canvas) const;

  //! One line per call, nested calls indented, geometry on the lines below
  //! the call it belongs to. The text is the same on every platform and in
  //! every locale, so tests can compare it to say which operations a piece
  //! of drawing code emits without rasterising anything.
  std::string dump() const;
};

//-----------------------------------------------------------------------------

//! A canvas that draws nothing and keeps the calls.
class TNZCANVAS_API TCanvasRecorder final : public TCanvas {
public:
  explicit TCanvasRecorder(const TCanvasIRect &deviceBounds);

  const TCanvasDisplayList &displayList() const { return m_list; }
  //! Hands the recording over and starts an empty one. The transform stack
  //! is left as it is.
  TCanvasDisplayList takeDisplayList();

  TCanvasIRect deviceBounds() const override { return m_bounds; }

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
  void onDrawImage(const TCanvasImage &image, const TCanvasAffine &imageToLocal,
                   const TCanvasImageOptions &options) override;

private:
  TCanvasIRect m_bounds;
  TCanvasDisplayList m_list;
};

#endif  // TCANVASRECORDER_H
