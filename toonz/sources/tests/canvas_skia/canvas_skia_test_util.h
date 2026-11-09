#pragma once

#ifndef CANVAS_SKIA_TEST_UTIL_H
#define CANVAS_SKIA_TEST_UTIL_H

// What the pixel tests of the Skia canvas share: a block of pixels to draw
// into, a way to read one back, and shapes on whole pixels.

#include <gtest/gtest.h>

#include "canvas/tskiacanvas.h"

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

#endif  // CANVAS_SKIA_TEST_UTIL_H
