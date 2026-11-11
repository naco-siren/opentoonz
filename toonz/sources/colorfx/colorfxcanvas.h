#pragma once

#ifndef COLORFXCANVAS_H
#define COLORFXCANVAS_H

// What the styles of colorfx share when they draw on a canvas: the
// primitives they used to hand to OpenGL (a quad, a fan, a strip, each
// corner with its colour, hard edges, blended on its own), said as meshes.

#include "tcanvasutil.h"
#include "tmathutil.h"
#include "tpixel.h"

#include <algorithm>
#include <cmath>

#include <vector>

namespace colorfxcanvas {

//! The paint of a mesh that carries its colours itself.
inline TCanvasPaint plainPaint() {
  TCanvasPaint paint(TCanvasColor(1.f, 1.f, 1.f, 1.f));
  paint.antialias = false;
  return paint;
}

//! A mesh of this mode with hard edges and a colour at every vertex. A
//! mesh without colours of its own is treated as one shape, painted once
//! where its triangles overlap; these primitives never were.
inline TCanvasMesh plainMesh(TCanvasMesh::Mode mode) {
  TCanvasMesh mesh;
  mesh.mode        = mode;
  mesh.smoothEdges = false;
  return mesh;
}

inline void addVertex(TCanvasMesh &mesh, const TPointD &p,
                      const TCanvasColor &color) {
  mesh.positions.push_back(toCanvas(p));
  mesh.colors.push_back(color);
}

inline void addVertex(TCanvasMesh &mesh, const TPointD &p,
                      const TPixel32 &color) {
  addVertex(mesh, p, toCanvas(color));
}

//! A colour the styles hand to OpenGL as doubles (glColor4d): the colour of
//! \p color with an opacity of its own. OpenGL keeps single precision.
inline TCanvasColor floatColor(const TPixelD &color, double alpha) {
  return TCanvasColor((float)color.r, (float)color.g, (float)color.b,
                      (float)alpha);
}

//! Draws a strip the styles sent as a quad strip: without a last vertex
//! that has no partner, which OpenGL ignored.
inline void drawQuadStrip(TCanvas &canvas, TCanvasMesh &strip,
                          const TCanvasPaint &paint) {
  if (strip.positions.size() % 2 != 0) {
    strip.positions.pop_back();
    if (!strip.colors.empty()) strip.colors.pop_back();
  }
  canvas.drawMesh(strip, paint);
}

//! Adds the quad p0 p1 p2 p3, a colour at each corner, to a mesh of quads.
inline void addQuad(TCanvasMesh &mesh, const TPointD &p0, const TPointD &p1,
                    const TPointD &p2, const TPointD &p3, const TPixel32 &c0,
                    const TPixel32 &c1, const TPixel32 &c2,
                    const TPixel32 &c3) {
  addVertex(mesh, p0, c0);
  addVertex(mesh, p1, c1);
  addVertex(mesh, p2, c2);
  addVertex(mesh, p3, c3);
}

inline void addQuad(TCanvasMesh &mesh, const TPointD &p0, const TPointD &p1,
                    const TPointD &p2, const TPointD &p3,
                    const TPixel32 &color) {
  addQuad(mesh, p0, p1, p2, p3, color, color, color, color);
}

//! The quad p0 p1 p2 p3, a colour at each corner.
inline void drawQuad(TCanvas &canvas, const TPointD &p0, const TPointD &p1,
                     const TPointD &p2, const TPointD &p3, const TPixel32 &c0,
                     const TPixel32 &c1, const TPixel32 &c2,
                     const TPixel32 &c3) {
  TCanvasMesh mesh = plainMesh(TCanvasMesh::Mode::Quads);
  addQuad(mesh, p0, p1, p2, p3, c0, c1, c2, c3);
  canvas.drawMesh(mesh, plainPaint());
}

inline void drawQuad(TCanvas &canvas, const TPointD &p0, const TPointD &p1,
                     const TPointD &p2, const TPointD &p3,
                     const TPixel32 &color) {
  drawQuad(canvas, p0, p1, p2, p3, color, color, color, color);
}

inline void drawQuad(TCanvas &canvas, const TPointD &p0, const TPointD &p1,
                     const TPointD &p2, const TPointD &p3,
                     const TCanvasColor &color) {
  TCanvasMesh mesh = plainMesh(TCanvasMesh::Mode::Quads);
  addVertex(mesh, p0, color);
  addVertex(mesh, p1, color);
  addVertex(mesh, p2, color);
  addVertex(mesh, p3, color);
  canvas.drawMesh(mesh, plainPaint());
}

//-----------------------------------------------------------------------------
// Lines one pixel wide: what the styles drew with GL_LINES and GL_LINE_STRIP

inline TCanvasPen thinPen() {
  TCanvasPen pen;
  pen.width = 1.0;
  pen.unit  = TCanvasPen::Unit::Device;
  return pen;
}

inline TCanvasPaint linePaint(const TCanvasColor &color, bool antialias) {
  TCanvasPaint paint(color);
  paint.antialias = antialias;
  return paint;
}

//! The contours of \p lines in one colour.
inline void strokeLines(TCanvas &canvas, const TCanvasPath &lines,
                        const TCanvasColor &color, bool antialias) {
  canvas.strokePath(lines, thinPen(), linePaint(color, antialias));
}

inline void strokeLines(TCanvas &canvas, const TCanvasPath &lines,
                        const TPixel32 &color, bool antialias) {
  strokeLines(canvas, lines, toCanvas(color), antialias);
}

//! Adds the segment a b to \p lines as a contour of its own.
inline void addSegment(TCanvasPath &lines, const TPointD &a, const TPointD &b) {
  lines.moveTo(toCanvas(a));
  lines.lineTo(toCanvas(b));
}

//! Adds the points as one open contour.
inline void addPolyline(TCanvasPath &lines, const std::vector<TPointD> &points) {
  for (std::size_t i = 0; i < points.size(); ++i) {
    if (i == 0)
      lines.moveTo(toCanvas(points[i]));
    else
      lines.lineTo(toCanvas(points[i]));
  }
}

//! A line strip with a colour at every vertex, which is how OpenGL takes
//! one: between two vertices of different colours the colour changes
//! evenly. Stretches in one colour go out as they are; a segment between
//! two colours goes out on its own, under a gradient from one end to the
//! other.
inline void strokeColoredPolyline(TCanvas &canvas,
                                  const std::vector<TPointD> &points,
                                  const std::vector<TCanvasColor> &colors,
                                  bool antialias) {
  const std::size_t n = points.size();
  std::size_t i       = 0;
  while (i + 1 < n) {
    if (colors[i] == colors[i + 1]) {
      TCanvasPath run;
      run.moveTo(toCanvas(points[i]));
      std::size_t j = i;
      while (j + 1 < n && colors[j + 1] == colors[i])
        run.lineTo(toCanvas(points[++j]));
      strokeLines(canvas, run, colors[i], antialias);
      i = j;
      continue;
    }
    // Two colours at one place are a change of colour, not a line.
    if (!(points[i] == points[i + 1])) {
      TCanvasPath segment;
      addSegment(segment, points[i], points[i + 1]);
      TCanvasPaint paint =
          linePaint(TCanvasColor(1.f, 1.f, 1.f, 1.f), antialias);
      paint.shader.kind  = TCanvasShader::Kind::LinearGradient;
      paint.shader.p0    = toCanvas(points[i]);
      paint.shader.p1    = toCanvas(points[i + 1]);
      paint.shader.stops = {{0.0, colors[i]}, {1.0, colors[i + 1]}};
      canvas.strokePath(segment, thinPen(), paint);
    }
    ++i;
  }
}

//! How many pieces tglDrawCircle() cut a circle of this radius into.
inline int circleSlices(double radius, double pixelSize) {
  if (radius < 0) return 2;

  double thetaStep;
  double temp = pixelSize * 0.5 / radius;

  if (fabs(1.0 - temp) <= 1)
    thetaStep = acos(1.0 - temp);
  else
    thetaStep = M_PI_4;

  int numberOfSlices = (int)(M_2PI / thetaStep);

  return numberOfSlices != 0 ? numberOfSlices : 2;
}

//! The outline of a circle as tglDrawCircle() drew it: a line through as
//! many points as the size of a pixel calls for, moved into place by the
//! transform.
inline void drawCircle(TCanvas &canvas, const TPointD &center, double radius,
                       const TCanvasColor &color, bool antialias) {
  if (radius <= 0) return;

  int slices = std::max(3, circleSlices(radius, canvas.pixelSize()));

  double step = M_2PI / (double)slices;
  double c = cos(step), s = sin(step);

  TCanvasPath circle;
  double x = radius, y = 0.0;
  circle.moveTo(TCanvasPoint(x, y));
  for (int i = slices - 1; i; --i) {
    double xx = x;
    x         = c * xx - s * y;
    y         = s * xx + c * y;
    circle.lineTo(TCanvasPoint(x, y));
  }
  circle.lineTo(TCanvasPoint(radius, 0.0));

  canvas.save();
  canvas.concat(TCanvasAffine::translation(center.x, center.y));
  strokeLines(canvas, circle, color, antialias);
  canvas.restore();
}

//! Draws a mesh of quads that all have one colour. A translucent one goes
//! out quad by quad: where two of them overlap the colour is then laid on
//! twice, which is how these patterns have always looked (a canvas paints
//! what one call covers once). For an opaque colour that makes no
//! difference, and the quads go out together.
inline void drawQuads(TCanvas &canvas, const TCanvasMesh &quads) {
  if (quads.colors.empty() || quads.colors[0].a >= 1.f) {
    canvas.drawMesh(quads, plainPaint());
    return;
  }
  for (std::size_t i = 0; i + 3 < quads.positions.size(); i += 4) {
    TCanvasMesh one = plainMesh(TCanvasMesh::Mode::Quads);
    one.positions.assign(quads.positions.begin() + i,
                         quads.positions.begin() + i + 4);
    one.colors.assign(quads.colors.begin() + i, quads.colors.begin() + i + 4);
    canvas.drawMesh(one, plainPaint());
  }
}

}  // namespace colorfxcanvas

#endif  // COLORFXCANVAS_H
