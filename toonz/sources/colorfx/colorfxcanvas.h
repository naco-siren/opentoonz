#pragma once

#ifndef COLORFXCANVAS_H
#define COLORFXCANVAS_H

// What the styles of colorfx share when they draw on a canvas: the
// primitives they used to hand to OpenGL (a quad, a fan, a strip, each
// corner with its colour, hard edges, blended on its own), said as meshes.

#include "tcanvasutil.h"

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
                      const TPixel32 &color) {
  mesh.positions.push_back(toCanvas(p));
  mesh.colors.push_back(toCanvas(color));
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
