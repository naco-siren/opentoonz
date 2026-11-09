// The canvas interface as seen through the recorder: which calls reach a
// backend, what state the canvas keeps, how a recording is printed and
// replayed. Nothing here rasterises.

#include <gtest/gtest.h>

#include "canvas/tcanvasrecorder.h"

#include <clocale>
#include <cstdio>
#include <string>

namespace {

const TCanvasIRect kBounds(0, 0, 320, 180);

TCanvasPath square(double x0, double y0, double x1, double y1) {
  const TCanvasPoint corners[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
  TCanvasPath path;
  path.addPolygon(corners, 4);
  return path;
}

// Two quads of a stroke outline, as the pairs of left and right points
// TStrokeOutline holds.
TCanvasMesh ribbon() {
  TCanvasMesh mesh;
  mesh.mode      = TCanvasMesh::Mode::TriangleStrip;
  mesh.positions = {{0, 1}, {0, -1}, {5, 2}, {5, -2}, {10, 1}, {10, -1}};
  return mesh;
}

}  // namespace

//-----------------------------------------------------------------------------
// State

TEST(Canvas, TransformIsScopedBySaveAndRestore) {
  TCanvasRecorder canvas(kBounds);
  EXPECT_TRUE(canvas.transform().isIdentity());
  EXPECT_EQ(canvas.saveCount(), 0);

  canvas.concat(TCanvasAffine::translation(100, 50));
  canvas.save();
  canvas.concat(TCanvasAffine::scale(2, 2));
  EXPECT_EQ(canvas.saveCount(), 1);
  EXPECT_EQ(canvas.transform() * TCanvasPoint(1, 1), TCanvasPoint(102, 52));

  canvas.restore();
  EXPECT_EQ(canvas.saveCount(), 0);
  EXPECT_EQ(canvas.transform() * TCanvasPoint(1, 1), TCanvasPoint(101, 51));
}

TEST(Canvas, PixelSizeFollowsTheTransform) {
  TCanvasRecorder canvas(kBounds);
  EXPECT_DOUBLE_EQ(canvas.pixelSize(), 1.0);

  canvas.concat(TCanvasAffine::scale(4, 4));
  EXPECT_DOUBLE_EQ(canvas.pixelSize(), 0.25) << "zoomed in four times";

  canvas.concat(TCanvasAffine::rotation(30));
  EXPECT_NEAR(canvas.pixelSize(), 0.25, 1e-12) << "rotation does not count";

  canvas.concat(TCanvasAffine::scale(0, 0));
  EXPECT_GT(canvas.pixelSize(), 1000.0) << "degenerate, but finite";
  EXPECT_LT(canvas.pixelSize(), 1e9);
}

TEST(Canvas, RestoreWithoutSaveDoesNothing) {
  TCanvasRecorder canvas(kBounds);
  canvas.concat(TCanvasAffine::translation(1, 2));
  canvas.restore();
  EXPECT_EQ(canvas.saveCount(), 0);
  EXPECT_EQ(canvas.transform(), TCanvasAffine::translation(1, 2));
  EXPECT_EQ(canvas.displayList().ops.size(), 1u) << "only the concat";
}

TEST(Canvas, ReportsItsDeviceBounds) {
  TCanvasRecorder canvas(kBounds);
  EXPECT_EQ(canvas.deviceBounds().width(), 320);
  EXPECT_EQ(canvas.deviceBounds().height(), 180);
  EXPECT_FALSE(canvas.deviceBounds().isEmpty());
}

//-----------------------------------------------------------------------------
// What reaches a backend

TEST(Canvas, CallsThatCannotDrawAreNotPassedOn) {
  TCanvasRecorder canvas(kBounds);
  const TCanvasPaint red(TCanvasColor(1, 0, 0));
  const TCanvasPaint invisible(TCanvasColor(1, 0, 0, 0));

  canvas.fillPath(TCanvasPath(), TCanvasFillRule::NonZero, red);
  canvas.fillPath(square(0, 0, 1, 1), TCanvasFillRule::NonZero, invisible);
  canvas.strokePath(TCanvasPath(), TCanvasPen(), red);
  TCanvasPen noWidth;
  noWidth.width = 0;
  canvas.strokePath(square(0, 0, 1, 1), noWidth, red);
  canvas.drawMesh(TCanvasMesh(), red);
  canvas.drawMesh(ribbon(), invisible);
  canvas.drawImage(TCanvasImage(), TCanvasAffine(), TCanvasImageOptions());
  canvas.concat(TCanvasAffine());

  EXPECT_TRUE(canvas.displayList().isEmpty());
}

// Clipping to nothing is not the same as not clipping.
TEST(Canvas, AnEmptyClipIsPassedOn) {
  TCanvasRecorder canvas(kBounds);
  canvas.clipPath(TCanvasPath(), TCanvasFillRule::NonZero,
                  TCanvasClipOp::Intersect, true);
  EXPECT_EQ(canvas.displayList().ops.size(), 1u);
}

//-----------------------------------------------------------------------------
// Printing

// What the vector renderer is expected to emit for one painted region with
// a hole and the stroke around it, under the affine of the render: a filled
// path grown by half a pixel, then the stroke outline as a mesh.
TEST(CanvasDisplayList, DumpsOneLinePerCallWithGeometryBelow) {
  TCanvasRecorder canvas(kBounds);

  canvas.save();
  canvas.concat(TCanvasAffine::translation(160, 90));

  TCanvasPath region = square(-10, -10, 10, 10);
  const TCanvasPoint hole[3] = {{-2, -2}, {0, 4}, {2, -2}};
  region.addPolygon(hole, 3);
  TCanvasPaint paint(TCanvasColor::fromBytes(255, 0, 0));
  paint.growDevice = 0.5;
  canvas.fillPath(region, TCanvasFillRule::NonZero, paint);

  canvas.drawMesh(ribbon(), TCanvasPaint(TCanvasColor(0, 0, 0, 0.5f)));
  canvas.restore();

  EXPECT_EQ(canvas.displayList().dump(),
            "save\n"
            "  concat [1 0 160; 0 1 90]\n"
            "  fillPath nonzero color=(1 0 0 1) grow=0.5px\n"
            "    M -10 -10 L 10 -10 L 10 10 L -10 10 Z\n"
            "    M -2 -2 L 0 4 L 2 -2 Z\n"
            "  drawMesh strip color=(0 0 0 0.5)\n"
            "    positions: (0 1) (0 -1) (5 2) (5 -2) (10 1) (10 -1)\n"
            "restore\n");
}

TEST(CanvasDisplayList, DumpsPensClipsShadersAndImages) {
  TCanvasRecorder canvas(kBounds);

  // A dashed centreline, one device pixel wide.
  TCanvasPath curve;
  curve.moveTo({0, 0}).quadTo({5, 10}, {10, 0});
  TCanvasPen pen;
  pen.dash = {4, 2};
  pen.cap  = TCanvasPen::Cap::Round;
  TCanvasPaint ink(TCanvasColor(0, 0, 0));
  ink.antialias = false;
  canvas.strokePath(curve, pen, ink);

  // The same curve with a width in its own units.
  TCanvasPen wide;
  wide.width = 2.5;
  wide.unit  = TCanvasPen::Unit::Local;
  wide.join  = TCanvasPen::Join::Round;
  canvas.strokePath(curve, wide, TCanvasPaint(TCanvasColor(0, 0, 0)));

  // A pattern inside a region: clip, then a gradient and a mesh with a
  // colour per vertex.
  canvas.save();
  canvas.clipPath(square(0, 0, 8, 8), TCanvasFillRule::EvenOdd,
                  TCanvasClipOp::Intersect, true);
  TCanvasPaint gradient;
  gradient.shader.kind  = TCanvasShader::Kind::LinearGradient;
  gradient.shader.p0    = {0, 0};
  gradient.shader.p1    = {8, 0};
  gradient.shader.stops = {{0, TCanvasColor(1, 0, 0)},
                           {1, TCanvasColor(0, 0, 1, 0)}};
  canvas.fillPath(square(0, 0, 8, 8), TCanvasFillRule::EvenOdd, gradient);

  TCanvasMesh fade;
  fade.mode        = TCanvasMesh::Mode::Triangles;
  fade.positions   = {{0, 0}, {8, 0}, {0, 8}};
  fade.colors      = {TCanvasColor(0, 0, 0), TCanvasColor(0, 0, 0, 0),
                      TCanvasColor(0, 0, 0, 0)};
  fade.smoothEdges = false;
  canvas.drawMesh(fade, TCanvasPaint());
  canvas.clipPath(square(2, 2, 4, 4), TCanvasFillRule::NonZero,
                  TCanvasClipOp::Difference, false);
  canvas.restore();

  // An image stamp and an image used as a fill.
  static const unsigned char pixels[2 * 2 * 4] = {};
  TCanvasImage image;
  image.pixels        = pixels;
  image.width         = 2;
  image.height        = 2;
  image.rowBytes      = 8;
  image.premultiplied = false;
  TCanvasImageOptions options;
  options.filter  = TCanvasFilter::Nearest;
  options.opacity = 0.75;
  canvas.drawImage(image, TCanvasAffine::scale(4, 4), options);

  TCanvasPaint textured;
  textured.shader.kind         = TCanvasShader::Kind::Image;
  textured.shader.image        = image;
  textured.shader.imageToLocal = TCanvasAffine::translation(1, 1);
  canvas.fillPath(square(0, 0, 8, 8), TCanvasFillRule::NonZero, textured);

  TCanvasPaint radial;
  radial.shader.kind   = TCanvasShader::Kind::RadialGradient;
  radial.shader.p0     = {4, 4};
  radial.shader.radius = 3;
  radial.shader.stops  = {{0.25, TCanvasColor(1, 1, 1)},
                          {1, TCanvasColor(0, 0, 0)}};
  canvas.fillPath(square(0, 0, 8, 8), TCanvasFillRule::NonZero, radial);

  EXPECT_EQ(
      canvas.displayList().dump(),
      "strokePath width=1px cap=round dash=[4 2] color=(0 0 0 1) noaa\n"
      "  M 0 0 Q 5 10 10 0\n"
      "strokePath width=2.5 join=round color=(0 0 0 1)\n"
      "  M 0 0 Q 5 10 10 0\n"
      "save\n"
      "  clipPath intersect evenodd\n"
      "    M 0 0 L 8 0 L 8 8 L 0 8 Z\n"
      "  fillPath evenodd color=(0 0 0 1) "
      "shader=linear((0 0)->(8 0) [0:(1 0 0 1) 1:(0 0 1 0)])\n"
      "    M 0 0 L 8 0 L 8 8 L 0 8 Z\n"
      "  drawMesh triangles hardedges color=(0 0 0 1)\n"
      "    positions: (0 0) (8 0) (0 8)\n"
      "    colors: (0 0 0 1) (0 0 0 0) (0 0 0 0)\n"
      "  clipPath difference nonzero noaa\n"
      "    M 2 2 L 4 2 L 4 4 L 2 4 Z\n"
      "restore\n"
      "drawImage 2x2 straight nearest opacity=0.75 [4 0 0; 0 4 0]\n"
      "fillPath nonzero color=(0 0 0 1) "
      "shader=image(2x2 straight repeat linear [1 0 1; 0 1 1])\n"
      "  M 0 0 L 8 0 L 8 8 L 0 8 Z\n"
      "fillPath nonzero color=(0 0 0 1) "
      "shader=radial((4 4) r=3 [0.25:(1 1 1 1) 1:(0 0 0 1)])\n"
      "  M 0 0 L 8 0 L 8 8 L 0 8 Z\n");
}

TEST(CanvasDisplayList, DumpsSixSignificantDigitsAndNoNegativeZero) {
  TCanvasRecorder canvas(kBounds);
  TCanvasPath path;
  path.moveTo({1.0 / 3.0, -0.0}).lineTo({123456.789, 1e-9});
  canvas.strokePath(path, TCanvasPen(), TCanvasPaint(TCanvasColor(0, 0, 0)));

  EXPECT_EQ(canvas.displayList().dump(),
            "strokePath width=1px color=(0 0 0 1)\n"
            "  M 0.333333 0 L 123457 1e-09\n");
}

// Qt sets the C locale from the environment, and in many locales the
// decimal separator is a comma. The dump must not depend on it.
TEST(CanvasDisplayList, DumpDoesNotDependOnTheLocale) {
  TCanvasRecorder canvas(kBounds);
  canvas.concat(TCanvasAffine::translation(0.5, 1.25));
  const std::string expected = "concat [1 0 0.5; 0 1 1.25]\n";
  ASSERT_EQ(canvas.displayList().dump(), expected);

  const std::string before = std::setlocale(LC_ALL, nullptr);
  const char *names[]      = {"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE", "fr_FR"};
  bool switched            = false;
  for (const char *name : names)
    if (std::setlocale(LC_ALL, name)) {
      switched = true;
      break;
    }
  if (!switched) GTEST_SKIP() << "no locale with a decimal comma installed";

  char probe[16];
  std::snprintf(probe, sizeof(probe), "%.1f", 0.5);
  const std::string dump = canvas.displayList().dump();
  std::setlocale(LC_ALL, before.c_str());

  ASSERT_STREQ(probe, "0,5") << "the locale did not take effect";
  EXPECT_EQ(dump, expected);
}

//-----------------------------------------------------------------------------
// Replaying

TEST(CanvasDisplayList, ReplayMakesTheSameCallsOnAnotherCanvas) {
  TCanvasRecorder first(kBounds);
  first.save();
  first.concat(TCanvasAffine::scale(2, 2));
  first.clipPath(square(0, 0, 50, 50), TCanvasFillRule::NonZero,
                 TCanvasClipOp::Intersect, true);
  first.fillPath(square(1, 1, 9, 9), TCanvasFillRule::EvenOdd,
                 TCanvasPaint(TCanvasColor(0, 1, 0)));
  first.drawMesh(ribbon(), TCanvasPaint(TCanvasColor(0, 0, 1)));
  first.restore();
  first.strokePath(square(0, 0, 3, 3), TCanvasPen(),
                   TCanvasPaint(TCanvasColor(0, 0, 0)));

  TCanvasRecorder second(kBounds);
  first.displayList().replay(second);

  EXPECT_EQ(second.displayList().ops.size(), 7u);
  EXPECT_EQ(second.displayList().dump(), first.displayList().dump());
  EXPECT_EQ(second.saveCount(), 0);
  EXPECT_TRUE(second.transform().isIdentity());
}

// A backend sees a replay under whatever transform is current, which is how
// a cached recording will be drawn at a new position.
TEST(CanvasDisplayList, ReplayHappensUnderTheCurrentTransform) {
  TCanvasRecorder recording(kBounds);
  recording.concat(TCanvasAffine::scale(3, 3));

  TCanvasRecorder target(kBounds);
  target.concat(TCanvasAffine::translation(7, 0));
  recording.displayList().replay(target);

  EXPECT_EQ(target.transform() * TCanvasPoint(1, 1), TCanvasPoint(10, 3));
}

TEST(CanvasRecorder, TakeDisplayListStartsAnEmptyRecording) {
  TCanvasRecorder canvas(kBounds);
  canvas.concat(TCanvasAffine::translation(5, 5));
  canvas.fillPath(square(0, 0, 1, 1), TCanvasFillRule::NonZero,
                  TCanvasPaint(TCanvasColor(0, 0, 0)));

  const TCanvasDisplayList taken = canvas.takeDisplayList();
  EXPECT_EQ(taken.ops.size(), 2u);
  EXPECT_TRUE(canvas.displayList().isEmpty());
  EXPECT_EQ(canvas.transform(), TCanvasAffine::translation(5, 5))
      << "the canvas keeps its state";
}

// A recording owns what it needs: the caller's geometry can go away.
TEST(CanvasRecorder, KeepsItsOwnCopyOfTheGeometry) {
  TCanvasRecorder canvas(kBounds);
  {
    TCanvasPath path = square(0, 0, 2, 2);
    canvas.fillPath(path, TCanvasFillRule::NonZero,
                    TCanvasPaint(TCanvasColor(0, 0, 0)));
    path.clear();
  }
  EXPECT_EQ(canvas.displayList().dump(),
            "fillPath nonzero color=(0 0 0 1)\n"
            "  M 0 0 L 2 0 L 2 2 L 0 2 Z\n");
}
