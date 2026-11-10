#pragma once

#ifndef TCANVASTYPES_H
#define TCANVASTYPES_H

// Value types of the path-level canvas IR (doc/nexttoonz_canvas_ir.md).
//
// This library, tnzcanvas, must build where the rest of the tree does not
// yet: it uses the C++ standard library and nothing else. No Qt, no OpenGL
// and no header of tnzcore, whose geometry and raster types pull in both.
// The types here are deliberately plain; code that has a TAffine, a TPixel32
// or a TRaster converts at the call site.

#include <cstddef>
#include <memory>
#include <vector>

#if defined(_WIN32)
#if defined(TNZCANVAS_EXPORTS)
#define TNZCANVAS_API __declspec(dllexport)
#elif defined(TNZCANVAS_STATIC)
#define TNZCANVAS_API
#else
#define TNZCANVAS_API __declspec(dllimport)
#endif
#else
#define TNZCANVAS_API __attribute__((visibility("default")))
#endif

//-----------------------------------------------------------------------------
// Geometry

struct TCanvasPoint {
  double x = 0.0, y = 0.0;

  TCanvasPoint() {}
  TCanvasPoint(double x_, double y_) : x(x_), y(y_) {}
};

inline bool operator==(const TCanvasPoint &a, const TCanvasPoint &b) {
  return a.x == b.x && a.y == b.y;
}

//! A 2D affine transform, laid out like TAffine:
//!   x' = a11 x + a12 y + a13
//!   y' = a21 x + a22 y + a23
struct TNZCANVAS_API TCanvasAffine {
  double a11 = 1.0, a12 = 0.0, a13 = 0.0;
  double a21 = 0.0, a22 = 1.0, a23 = 0.0;

  TCanvasAffine() {}
  TCanvasAffine(double a11_, double a12_, double a13_, double a21_,
                double a22_, double a23_)
      : a11(a11_), a12(a12_), a13(a13_), a21(a21_), a22(a22_), a23(a23_) {}

  static TCanvasAffine translation(double dx, double dy);
  static TCanvasAffine scale(double sx, double sy);
  static TCanvasAffine rotation(double degrees);

  //! (a * b)(p) == a(b(p)), like TAffine and like glMultMatrix.
  TCanvasAffine operator*(const TCanvasAffine &other) const;
  TCanvasPoint operator*(const TCanvasPoint &p) const;

  double det() const { return a11 * a22 - a12 * a21; }
  bool isIdentity() const;
};

inline bool operator==(const TCanvasAffine &a, const TCanvasAffine &b) {
  return a.a11 == b.a11 && a.a12 == b.a12 && a.a13 == b.a13 &&
         a.a21 == b.a21 && a.a22 == b.a22 && a.a23 == b.a23;
}

//! An integer rectangle in device pixels, half open: x0 <= x < x1.
struct TCanvasIRect {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

  TCanvasIRect() {}
  TCanvasIRect(int x0_, int y0_, int x1_, int y1_)
      : x0(x0_), y0(y0_), x1(x1_), y1(y1_) {}

  int width() const { return x1 - x0; }
  int height() const { return y1 - y0; }
  bool isEmpty() const { return x1 <= x0 || y1 <= y0; }
};

//-----------------------------------------------------------------------------
// Colour

//! Straight (not premultiplied) RGBA, each channel in 0..1. Styles compute
//! 8-bit colours today; float leaves room for deeper targets later.
struct TCanvasColor {
  float r = 0.f, g = 0.f, b = 0.f, a = 1.f;

  TCanvasColor() {}
  TCanvasColor(float r_, float g_, float b_, float a_ = 1.f)
      : r(r_), g(g_), b(b_), a(a_) {}

  static TCanvasColor fromBytes(int r, int g, int b, int a = 255) {
    return TCanvasColor(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
  }
};

inline bool operator==(const TCanvasColor &x, const TCanvasColor &y) {
  return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
}

//-----------------------------------------------------------------------------
// Path: contours whose outline is the data itself (regions, centrelines)

enum class TCanvasFillRule { NonZero, EvenOdd };

class TNZCANVAS_API TCanvasPath {
public:
  enum Verb : unsigned char { MoveTo, LineTo, QuadTo, Close };

  TCanvasPath &moveTo(const TCanvasPoint &p);
  TCanvasPath &lineTo(const TCanvasPoint &p);
  //! A quadratic Bezier from the current point: region edges are pieces of
  //! stroke centrelines, which are quadratics, so they need no flattening.
  TCanvasPath &quadTo(const TCanvasPoint &control, const TCanvasPoint &end);
  TCanvasPath &close();

  //! One contour through \p count points.
  TCanvasPath &addPolygon(const TCanvasPoint *points, std::size_t count,
                          bool closed = true);

  const std::vector<Verb> &verbs() const { return m_verbs; }
  const std::vector<TCanvasPoint> &points() const { return m_points; }

  bool isEmpty() const { return m_points.empty(); }
  void clear();

private:
  std::vector<Verb> m_verbs;
  std::vector<TCanvasPoint> m_points;
};

//-----------------------------------------------------------------------------
// Mesh: shapes put together from many small pieces (stroke outlines, ribbons,
// pattern tiles). Coverage is the union of the triangles: where they overlap
// the paint is applied once, whichever way each triangle is wound.

struct TNZCANVAS_API TCanvasMesh {
  //! Quads: every four vertices one quad, given all the way round; it is
  //! the two triangles 0 1 2 and 0 2 3. The mode exists because OpenGL has
  //! it and blends colours across a quad in its own way, which drawing
  //! code written for OpenGL counts on while OpenGL still draws it.
  enum class Mode { Triangles, TriangleStrip, TriangleFan, Quads };

  Mode mode = Mode::Triangles;
  std::vector<TCanvasPoint> positions;
  std::vector<TCanvasColor> colors;  //!< Per vertex; empty: the paint's.
  std::vector<TCanvasPoint> uvs;     //!< Per vertex, in the image shader's
                                     //!< space; empty: positions are used.
  //! Whether the silhouette is antialiased when the paint asks for it. Off
  //! for meshes whose edge colours already fade to transparent.
  bool smoothEdges = true;

  std::size_t triangleCount() const;
  //! Enough vertices for one triangle, and per-vertex arrays that match.
  bool isValid() const;
};

//-----------------------------------------------------------------------------
// Images

//! A view of 32-bit pixels laid out like TRaster32: BGRA bytes, rows bottom
//! up (the first row in memory is the lowest one on screen).
struct TCanvasImage {
  std::shared_ptr<const void> keepAlive;  //!< Whatever owns the pixels.
  const void *pixels      = nullptr;
  int width               = 0;
  int height              = 0;
  std::ptrdiff_t rowBytes = 0;
  bool premultiplied      = true;

  bool isEmpty() const { return !pixels || width <= 0 || height <= 0; }
};

enum class TCanvasFilter { Nearest, Linear };
enum class TCanvasTile { Clamp, Repeat };

struct TCanvasImageOptions {
  TCanvasFilter filter = TCanvasFilter::Linear;
  double opacity       = 1.0;
};

//-----------------------------------------------------------------------------
// Paint

struct TCanvasGradientStop {
  double offset = 0.0;  //!< 0 at the start of the gradient, 1 at its end.
  TCanvasColor color;
};

//! What a shape is filled with instead of the paint's flat colour.
//! Coordinates are in the space the shape is drawn in.
struct TCanvasShader {
  enum class Kind { None, LinearGradient, RadialGradient, Image };

  Kind kind = Kind::None;

  // Gradients: from p0 to p1 (linear), or from p0 outwards to radius.
  TCanvasPoint p0, p1;
  double radius = 0.0;
  std::vector<TCanvasGradientStop> stops;

  // Image: placed by imageToLocal, repeated or clamped outside its bounds.
  TCanvasImage image;
  TCanvasAffine imageToLocal;
  TCanvasTile tile     = TCanvasTile::Repeat;
  TCanvasFilter filter = TCanvasFilter::Linear;
};

enum class TCanvasBlend { SrcOver };

struct TCanvasPaint {
  TCanvasColor color;    //!< With a shader, only its alpha is used.
  TCanvasShader shader;
  bool antialias = true;
  //! Grows a filled shape outwards by this many device pixels before it is
  //! filled. Half a pixel reproduces what the smooth-line fringe of the
  //! OpenGL renderer did, and closes the seam between abutting regions.
  double growDevice  = 0.0;
  TCanvasBlend blend = TCanvasBlend::SrcOver;

  TCanvasPaint() {}
  explicit TCanvasPaint(const TCanvasColor &c) : color(c) {}
};

//! How a path is stroked. Only strokes of constant width are expressed this
//! way; a stroke of varying thickness is a mesh.
struct TCanvasPen {
  enum class Unit { Local, Device };
  enum class Cap { Butt, Round };
  enum class Join { Miter, Round };

  double width = 1.0;
  //! Device: a width in pixels whatever the transform, which is what every
  //! one-pixel line of the old renderer was.
  Unit unit = Unit::Device;
  Cap cap   = Cap::Butt;
  Join join = Join::Miter;
  //! Lengths of dashes and gaps in device pixels, alternating; empty: solid.
  std::vector<double> dash;
};

enum class TCanvasClipOp { Intersect, Difference };

#endif  // TCANVASTYPES_H
