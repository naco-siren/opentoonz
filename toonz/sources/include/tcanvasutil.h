#pragma once

#ifndef TCANVASUTIL_H
#define TCANVASUTIL_H

// From the types of tnzcore to those of the canvas interface. The canvas
// libraries cannot use TAffine, TPointD or TPixel32 (see
// canvas/tcanvastypes.h), so code that draws converts where it calls.

#include "canvas/tcanvas.h"

#include "tgeometry.h"
#include "tpixel.h"

inline TCanvasPoint toCanvas(const TPointD &p) { return TCanvasPoint(p.x, p.y); }

inline TCanvasAffine toCanvas(const TAffine &aff) {
  return TCanvasAffine(aff.a11, aff.a12, aff.a13, aff.a21, aff.a22, aff.a23);
}

//! Not premultiplied, like the TPixel32 of a style.
inline TCanvasColor toCanvas(const TPixel32 &color) {
  return TCanvasColor::fromBytes(color.r, color.g, color.b, color.m);
}

#endif  // TCANVASUTIL_H
