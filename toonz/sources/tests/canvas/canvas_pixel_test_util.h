#pragma once

#ifndef CANVAS_PIXEL_TEST_UTIL_H
#define CANVAS_PIXEL_TEST_UTIL_H

// What the pixel tests of the canvas backends share (../canvas_skia,
// ../canvas_gl): a block of pixels, a way to read one back, shapes on whole
// pixels, and a picture that uses every call of the interface.

#include <gtest/gtest.h>

#include "canvas/tcanvas.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

//! Pixels laid out like a TRaster32: BGRA, premultiplied, rows bottom up.
struct Target {
  int width, height;
  std::vector<unsigned char> bytes;

  Target(int w, int h) : width(w), height(h), bytes((std::size_t)w * h * 4) {}

  void *pixels() { return bytes.data(); }
  std::ptrdiff_t rowBytes() const { return (std::ptrdiff_t)width * 4; }

  const unsigned char *at(int x, int y) const {
    return &bytes[((std::size_t)y * width + x) * 4];
  }
  int alpha(int x, int y) const { return at(x, y)[3]; }

  void fill(int r, int g, int b, int a) {
    for (std::size_t i = 0; i < bytes.size(); i += 4)
      bytes[i] = b, bytes[i + 1] = g, bytes[i + 2] = r, bytes[i + 3] = a;
  }

  //! FNV-1a, to compare whole pictures across runs and platforms.
  std::uint64_t hash() const {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char byte : bytes) h = (h ^ byte) * 1099511628211ull;
    return h;
  }
};

//! Whether the pixel at (x, y) holds these premultiplied values, give or
//! take \p tolerance per channel.
inline ::testing::AssertionResult pixelIs(const Target &target, int x, int y,
                                          int r, int g, int b, int a,
                                          int tolerance = 1) {
  const unsigned char *p = target.at(x, y);
  if (std::abs(p[2] - r) <= tolerance && std::abs(p[1] - g) <= tolerance &&
      std::abs(p[0] - b) <= tolerance && std::abs(p[3] - a) <= tolerance)
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "pixel (" << x << ", " << y << ") is rgba(" << (int)p[2] << ", "
         << (int)p[1] << ", " << (int)p[0] << ", " << (int)p[3]
         << "), expected rgba(" << r << ", " << g << ", " << b << ", " << a
         << ")";
}

//! Counter-clockwise, seen with y up.
inline TCanvasPath rectPath(double x0, double y0, double x1, double y1) {
  TCanvasPath path;
  path.moveTo({x0, y0}).lineTo({x1, y0}).lineTo({x1, y1}).lineTo({x0, y1});
  return path.close();
}

inline TCanvasPath clockwiseRectPath(double x0, double y0, double x1,
                                     double y1) {
  TCanvasPath path;
  path.moveTo({x0, y0}).lineTo({x0, y1}).lineTo({x1, y1}).lineTo({x1, y0});
  return path.close();
}

//! The rectangle as a strip of two triangles.
inline TCanvasMesh rectMesh(double x0, double y0, double x1, double y1) {
  TCanvasMesh mesh;
  mesh.mode      = TCanvasMesh::Mode::TriangleStrip;
  mesh.positions = {{x0, y0}, {x0, y1}, {x1, y0}, {x1, y1}};
  return mesh;
}

inline TCanvasPaint paintOf(float r, float g, float b, float a = 1.f) {
  return TCanvasPaint(TCanvasColor(r, g, b, a));
}

//! Two by two opaque pixels: red and green in the bottom row, blue and
//! white above them.
struct FourColours {
  unsigned char bytes[16] = {0,   0, 255, 255, 0,   255, 0,   255,
                             255, 0, 0,   255, 255, 255, 255, 255};

  TCanvasImage image() const {
    TCanvasImage image;
    image.pixels   = bytes;
    image.width    = 2;
    image.height   = 2;
    image.rowBytes = 8;
    return image;
  }
};

// Something of everything, on 64 by 48 pixels.
inline void drawScene(TCanvas &canvas, const FourColours &colours) {
  canvas.fillPath(rectPath(0, 0, 64, 48), TCanvasFillRule::NonZero,
                  paintOf(1.f, 1.f, 1.f));

  // Two regions that share a curved edge, the first one grown.
  TCanvasPath left, right;
  left.moveTo({4, 4}).lineTo({20, 4}).quadTo({28, 14}, {20, 24});
  left.lineTo({4, 24}).close();
  right.moveTo({20, 4}).lineTo({36, 4}).lineTo({36, 24}).lineTo({20, 24});
  right.quadTo({28, 14}, {20, 4}).close();
  TCanvasPaint grown = paintOf(0.9f, 0.6f, 0.1f);
  grown.growDevice   = 0.5;
  canvas.fillPath(left, TCanvasFillRule::NonZero, grown);
  canvas.fillPath(right, TCanvasFillRule::NonZero, paintOf(0.2f, 0.5f, 0.8f));

  // A translucent ribbon that crosses itself, turned a little.
  canvas.save();
  canvas.concat(TCanvasAffine::translation(30, 30) *
                TCanvasAffine::rotation(20));
  TCanvasMesh ribbon;
  ribbon.mode      = TCanvasMesh::Mode::TriangleStrip;
  ribbon.positions = {{-20, -3}, {-20, 3}, {0, -3}, {0, 3},  {14, 2},
                      {10, 8},   {4, 12},  {-2, 8}, {-6, -8}, {-12, -6}};
  canvas.drawMesh(ribbon, paintOf(0.1f, 0.1f, 0.1f, 0.6f));

  // A dashed line one pixel wide and a wide one with round ends.
  TCanvasPath curve;
  curve.moveTo({-24, 10}).quadTo({0, 30}, {24, 6});
  TCanvasPen thin;
  thin.dash = {5, 3};
  canvas.strokePath(curve, thin, paintOf(0.f, 0.f, 0.f));
  TCanvasPen wide;
  wide.width = 2.5;
  wide.unit  = TCanvasPen::Unit::Local;
  wide.cap   = TCanvasPen::Cap::Round;
  wide.join  = TCanvasPen::Join::Round;
  TCanvasPath angle;
  angle.moveTo({-26, -14}).lineTo({-16, -20}).lineTo({-8, -13});
  canvas.strokePath(angle, wide, paintOf(0.7f, 0.f, 0.2f, 0.8f));
  canvas.restore();

  // A gradient seen through a round clip.
  canvas.save();
  TCanvasPath disc;
  disc.moveTo({52, 4}).quadTo({62, 4}, {62, 14}).quadTo({62, 24}, {52, 24});
  disc.quadTo({42, 24}, {42, 14}).quadTo({42, 4}, {52, 4}).close();
  canvas.clipPath(disc, TCanvasFillRule::NonZero, TCanvasClipOp::Intersect,
                  true);
  TCanvasPaint gradient;
  gradient.shader.kind  = TCanvasShader::Kind::LinearGradient;
  gradient.shader.p0    = {42, 4};
  gradient.shader.p1    = {62, 24};
  gradient.shader.stops = {{0.0, TCanvasColor(1.f, 0.9f, 0.2f)},
                           {0.6, TCanvasColor(0.9f, 0.2f, 0.3f)},
                           {1.0, TCanvasColor(0.3f, 0.1f, 0.5f, 0.5f)}};
  canvas.fillPath(rectPath(40, 0, 64, 28), TCanvasFillRule::NonZero, gradient);
  canvas.restore();

  // A fan whose colours fade from its hub.
  TCanvasMesh fan;
  fan.mode      = TCanvasMesh::Mode::TriangleFan;
  fan.positions = {{52, 37}, {44.5, 31.5}, {60.5, 30.5},
                   {61.5, 44.5}, {45.5, 45.5}, {44.5, 31.5}};
  fan.colors    = {TCanvasColor(1.f, 1.f, 1.f),
                   TCanvasColor(0.f, 0.6f, 0.3f),
                   TCanvasColor(0.f, 0.3f, 0.6f),
                   TCanvasColor(0.5f, 0.f, 0.5f, 0.5f),
                   TCanvasColor(0.6f, 0.3f, 0.f),
                   TCanvasColor(0.f, 0.6f, 0.3f)};
  canvas.drawMesh(fan, TCanvasPaint());

  // An image, enlarged and turned, at three quarters opacity.
  TCanvasImageOptions options;
  options.opacity = 0.75;
  canvas.drawImage(colours.image(),
                   TCanvasAffine::translation(10, 30) *
                       TCanvasAffine::rotation(15) * TCanvasAffine::scale(6, 6),
                   options);
}

#endif  // CANVAS_PIXEL_TEST_UTIL_H
