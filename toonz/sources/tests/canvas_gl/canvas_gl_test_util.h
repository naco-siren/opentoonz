#pragma once

#ifndef CANVAS_GL_TEST_UTIL_H
#define CANVAS_GL_TEST_UTIL_H

#include "canvas_pixel_test_util.h"

#include "canvas/tglcanvas.h"

#include "tglcanvasbridge.h"
#include "tgl.h"
#include "tofflinegl.h"
#include "traster.h"

//! Calls draw(canvas) with a TGlCanvas over an offscreen OpenGL buffer of
//! transparent pixels, and returns what is in the buffer afterwards.
//!
//! With State::Callers the canvas draws with the state it finds, as it
//! does for the old drawing code; that state is here what the old code
//! sets up around a region: blending by alpha, smooth lines.
template <class Draw>
Target drawWithGl(int width, int height, Draw draw,
                  TGlCanvas::State state = TGlCanvas::State::Own) {
  TOfflineGL gl(TDimension(width, height));
  gl.makeCurrent();
  gl.initMatrix();
  gl.clear(TPixel32(0, 0, 0, 0));
  glClearStencil(0);
  glClear(GL_STENCIL_BUFFER_BIT);
  if (state == TGlCanvas::State::Callers) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    tglEnableLineSmooth(true);
  }
  {
    TGlStencilMasks masks;
    TGlCanvas canvas(TCanvasIRect(0, 0, width, height), masks, state);
    draw(canvas);
  }
  const TRaster32P raster = gl.getRaster();
  gl.doneCurrent();

  Target target(width, height);
  raster->lock();
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const TPixel32 &pixel = raster->pixels(y)[x];
      unsigned char *bytes  = &target.bytes[((std::size_t)y * width + x) * 4];
      bytes[0] = pixel.b, bytes[1] = pixel.g;
      bytes[2] = pixel.r, bytes[3] = pixel.m;
    }
  raster->unlock();
  return target;
}

#endif  // CANVAS_GL_TEST_UTIL_H
