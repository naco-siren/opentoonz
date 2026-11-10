#pragma once

#ifndef TCANVASUTIL_H
#define TCANVASUTIL_H

// From the types of tnzcore to those of the canvas interface. The canvas
// libraries cannot use TAffine, TPointD or TPixel32 (see
// canvas/tcanvastypes.h), so code that draws converts where it calls.

#include "canvas/tcanvas.h"

#include "tgeometry.h"
#include "tpixel.h"
#include "tregionoutline.h"

#include <vector>

inline TCanvasPoint toCanvas(const TPointD &p) { return TCanvasPoint(p.x, p.y); }

inline TCanvasAffine toCanvas(const TAffine &aff) {
  return TCanvasAffine(aff.a11, aff.a12, aff.a13, aff.a21, aff.a22, aff.a23);
}

//! Not premultiplied, like the TPixel32 of a style.
inline TCanvasColor toCanvas(const TPixel32 &color) {
  return TCanvasColor::fromBytes(color.r, color.g, color.b, color.m);
}

//! The outline of a region as one path: the outer contours as they come,
//! the holes turned round. The old tessellator filled where the winding
//! number was positive; for contours laid out like this, that is where it
//! is not zero.
inline TCanvasPath toCanvasPath(const TRegionOutline &outline) {
  TCanvasPath path;
  std::vector<TCanvasPoint> points;
  for (const TRegionOutline::PointVector &contour : outline.m_exterior) {
    points.clear();
    for (const T3DPointD &p : contour) points.push_back(TCanvasPoint(p.x, p.y));
    path.addPolygon(points.data(), points.size());
  }
  for (const TRegionOutline::PointVector &contour : outline.m_interior) {
    points.clear();
    for (auto p = contour.rbegin(); p != contour.rend(); ++p)
      points.push_back(TCanvasPoint(p->x, p->y));
    path.addPolygon(points.data(), points.size());
  }
  return path;
}

#endif  // TCANVASUTIL_H
