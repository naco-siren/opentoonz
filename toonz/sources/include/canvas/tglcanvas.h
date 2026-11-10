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

//! How a TGlCanvas reaches the stencil buffer: for clips, and to paint a
//! translucent shape once where its pieces overlap.
/*!
  The stencil buffer of an OpenGL context is shared with whatever else draws
  into that context, and the tree has one manager for it (TStencilControl,
  in tnzcore). The canvas library does not link tnzcore, so it is handed
  this interface instead; the four functions are those of TStencilControl.
*/
class TNZGLCANVAS_API TGlCanvasMasks {
public:
  virtual ~TGlCanvasMasks() {}

  //! What happens on the screen while a mask is being drawn: nothing; the
  //! same drawing as without a mask; or that, but each pixel at most once.
  enum class Screen { Untouched, DrawnToo, DrawnOnce };

  //! Starts a mask: what is drawn until endMask() goes into it.
  virtual void beginMask(Screen screen) = 0;
  virtual void endMask()                = 0;
  //! From here to disableMask(), drawing is kept inside the last mask or,
  //! with \p inside false, outside it.
  virtual void enableMask(bool inside) = 0;
  virtual void disableMask()           = 0;
};

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
    by triangle, blended twice where triangles overlap. A textured strip
    that wants smooth edges gets a textured line down each side first, as
    the old renderer gave its textured strokes; other such meshes get no
    fringe.
  - A strip with an even number of vertices is sent as a quad strip, which
    is what the old renderer sent: how a quad that is not convex gets
    split is the driver's choice, and stays the one it has always made.
  - Lines are OpenGL lines: no caps, no joins, a width the driver may
    clamp, and dashes rounded to a 16-bit stipple pattern. A line whose
    paint is a linear gradient with no stop between two neighbouring
    vertices is drawn with a colour per vertex, not through a texture.
  - A contour of one point under a pen with round caps is a dot: an OpenGL
    point of the pen's width for a pen in device units, a disc of 60
    slices for one in local units.
  - A clip is a stencil mask: the path filled as a region is, which with
    antialiasing includes the smooth line along its contours. A clip to a
    mesh is the mesh drawn into the mask as the primitive it is drawn as.
    A fill that is also a clip is the fill followed by a clip to it: every
    pixel the fill touched takes what is drawn on it afterwards at full
    strength, so the edge of the whole is as jagged as a mask is.
  - Gradients are textures of 256 steps. Texture coordinates are computed
    here and sent with the vertices; the texture matrix is not used.
  - The texture of an image that names what keeps its pixels alive
    (TCanvasImage::keepAlive) is kept for the next call, so that a picture
    stamped many times is uploaded once.

  The canvas draws into the OpenGL context that is current when it is
  created, and that context has to be current for every call. Coordinates
  are those of the modelview matrix at that moment; the canvas multiplies
  onto that matrix as transforms are applied and puts it back when it is
  destroyed. pixelSize() is read back from the modelview matrix, the way
  tglGetPixelSize2() reads it: it is right when the projection maps one
  unit of the untransformed modelview space to one pixel, which
  TOfflineGL::initMatrix() and the viewers set up.

  There are two ways to use it, chosen by State:

  - Own. The canvas takes care of everything: it blends for a target that
    holds premultiplied colour (two passes per call: colour, then alpha),
    sets line smoothing from each paint, and leaves every OpenGL attribute
    as it found it.
  - Callers. For the old drawing code, which sets up blending, colour masks
    and line smoothing around the styles it draws and draws them twice
    itself. The canvas then draws with that state, and changes only what
    the code it replaces changed. This is what keeps a style that has
    moved to the canvas interface pixel for pixel what it was when it is
    reached through tglDraw(). For the same reason a path filled by the
    non-zero rule is then filled by the rule the old tessellator had,
    which depends on the platform: positive winding numbers on Windows
    and macOS (positive being the way most of the path goes round), odd
    ones on Linux. The rules part ways where an outline crosses or
    overlaps itself: a loop wound the other way was left empty on macOS,
    a part covered twice was left empty on Linux.

  This library (tnzglcanvas) links OpenGL and tnzcanvas, nothing else.
*/
class TNZGLCANVAS_API TGlCanvas final : public TCanvas {
public:
  enum class State { Own, Callers };

  //! \p deviceBounds is only what deviceBounds() returns.
  TGlCanvas(const TCanvasIRect &deviceBounds, TGlCanvasMasks &masks,
            State state = State::Own);
  //! With the current viewport as the device bounds.
  explicit TGlCanvas(TGlCanvasMasks &masks, State state = State::Own);
  ~TGlCanvas() override;

  TCanvasIRect deviceBounds() const override { return m_bounds; }
  double pixelSize() const override;

  struct Textures;  //!< Not for callers: what the canvas keeps of images.

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
  TCanvasIRect m_bounds;
  TGlCanvasMasks &m_masks;
  State m_state;
  Textures *m_textures;
  // For each save() not yet restored, and for the canvas itself at the
  // front: how many clips have been set since.
  std::vector<int> m_clips;
  // The driver's GL_LINE_WIDTH_RANGE, once it has been asked for.
  float m_lineWidthRange[2] = {1.f, 0.f};

  void dropClips(int count);
  void drawDots(const std::vector<TCanvasPoint> &dots, const TCanvasPen &pen,
                const TCanvasPaint &paint);
};

#endif  // TGLCANVAS_H
