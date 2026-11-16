// The Skia canvas: meshes, images, shaders, and whole pictures.

#include "canvas_pixel_test_util.h"

#include "canvas/tskiacanvas.h"

#include "canvas/tcanvasrecorder.h"

#include <cstdio>

//-----------------------------------------------------------------------------
// Meshes of one colour

// What makes a stroke outline one shape: its quads overlap where the stroke
// bends or crosses itself, every second triangle of a strip is wound the
// other way, and the edges between triangles run through pixels.
TEST(SkiaCanvasMesh, OneColourFillsTheUnionOfTheTriangles) {
  TCanvasMesh strip;
  strip.mode = TCanvasMesh::Mode::TriangleStrip;
  // From x = 2 to x = 10, then back over itself to x = 7.
  strip.positions = {{2, 2}, {2, 6}, {6, 2},  {6, 6},
                     {10, 2}, {10, 6}, {7, 2}, {7, 6}};

  Target target(12, 8);
  TSkiaCanvas canvas(target.pixels(), 12, 8, target.rowBytes());
  canvas.drawMesh(strip, paintOf(0.f, 0.f, 0.f, 0.5f));

  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 12; ++x) {
      const bool inside = x >= 2 && x < 10 && y >= 2 && y < 6;
      EXPECT_NEAR(target.alpha(x, y), inside ? 128 : 0, 1)
          << "at (" << x << ", " << y << ")";
    }
}

TEST(SkiaCanvasMesh, TakesTrianglesAndFans) {
  TCanvasMesh triangles;
  triangles.positions = {{1, 1}, {5, 1}, {1, 5}, {5, 1}, {5, 5}, {1, 5}};

  TCanvasMesh fan;
  fan.mode      = TCanvasMesh::Mode::TriangleFan;
  fan.positions = {{9, 3}, {7, 1}, {11, 1}, {11, 5}, {7, 5}, {7, 1}};

  Target target(12, 6);
  TSkiaCanvas canvas(target.pixels(), 12, 6, target.rowBytes());
  canvas.drawMesh(triangles, paintOf(0.f, 0.f, 0.f));
  canvas.drawMesh(fan, paintOf(0.f, 0.f, 0.f));

  for (int y = 0; y < 6; ++y)
    for (int x = 0; x < 12; ++x) {
      const bool inside =
          y >= 1 && y < 5 && ((x >= 1 && x < 5) || (x >= 7 && x < 11));
      // Within one step: where an edge between two triangles cuts a pixel
      // in two, the halves add up to 254.
      EXPECT_NEAR(target.alpha(x, y), inside ? 255 : 0, 1)
          << "at (" << x << ", " << y << ")";
    }
}

// Quads come four vertices at a time, going round each.
TEST(SkiaCanvasMesh, TakesQuads) {
  TCanvasMesh quads;
  quads.mode      = TCanvasMesh::Mode::Quads;
  quads.positions = {{1, 1}, {5, 1}, {5, 5}, {1, 5},
                     {7, 1}, {7, 5}, {11, 5}, {11, 1}};  // the other way round

  Target target(12, 6);
  TSkiaCanvas canvas(target.pixels(), 12, 6, target.rowBytes());
  canvas.drawMesh(quads, paintOf(0.f, 0.f, 0.f));

  for (int y = 0; y < 6; ++y)
    for (int x = 0; x < 12; ++x) {
      const bool inside =
          y >= 1 && y < 5 && ((x >= 1 && x < 5) || (x >= 7 && x < 11));
      EXPECT_NEAR(target.alpha(x, y), inside ? 255 : 0, 1)
          << "at (" << x << ", " << y << ")";
    }
}

// A polygon comes all the way round, like a quad with more corners.
TEST(SkiaCanvasMesh, TakesPolygons) {
  TCanvasMesh hexagon;
  hexagon.mode      = TCanvasMesh::Mode::Polygon;
  hexagon.positions = {{2, 4}, {4, 1}, {8, 1}, {10, 4}, {8, 7}, {4, 7}};
  TCanvasPaint hard = paintOf(0.f, 0.f, 0.f);
  hard.antialias    = false;

  Target target(12, 8);
  TSkiaCanvas canvas(target.pixels(), 12, 8, target.rowBytes());
  canvas.drawMesh(hexagon, hard);

  EXPECT_EQ(target.alpha(6, 4), 255);
  EXPECT_EQ(target.alpha(3, 4), 255);
  EXPECT_EQ(target.alpha(7, 6), 255);
  EXPECT_EQ(target.alpha(2, 1), 0) << "outside the slanting edge";
  EXPECT_EQ(target.alpha(9, 6), 0);
  EXPECT_EQ(target.alpha(6, 7), 0);
}

// The styles that draw their own primitives give every vertex the colour:
// that keeps the OpenGL canvas from treating the mesh as a stroke outline.
// Here it is the same thing as the colour in the paint.
TEST(SkiaCanvasMesh, AMeshOfOneColourIsAShape) {
  TCanvasMesh plain;
  plain.mode        = TCanvasMesh::Mode::Quads;
  plain.smoothEdges = false;
  // Two quads that overlap from x = 4 to x = 6, with edges through pixels.
  plain.positions = {{1.5, 1.25}, {6, 1.25}, {6, 6.75}, {1.5, 6.75},
                     {4, 3},      {10.5, 3}, {10.5, 5}, {4, 5}};
  TCanvasMesh coloured = plain;
  coloured.colors.assign(8, TCanvasColor(0.f, 0.f, 1.f, 0.5f));

  TCanvasPaint blue = paintOf(0.f, 0.f, 1.f, 0.5f);
  blue.antialias    = false;
  TCanvasPaint white = paintOf(1.f, 1.f, 1.f);
  white.antialias    = false;

  Target a(12, 8), b(12, 8);
  TSkiaCanvas first(a.pixels(), 12, 8, a.rowBytes());
  first.drawMesh(plain, blue);
  TSkiaCanvas second(b.pixels(), 12, 8, b.rowBytes());
  second.drawMesh(coloured, white);

  EXPECT_EQ(a.bytes, b.bytes);
  EXPECT_TRUE(pixelIs(b, 5, 4, 0, 0, 128, 128)) << "painted once";
  EXPECT_EQ(b.alpha(1, 3), 0) << "hard edges: more than half outside";
  EXPECT_EQ(b.alpha(10, 3), 128);

  // The paint's opacity counts as well.
  Target c(12, 8);
  TSkiaCanvas third(c.pixels(), 12, 8, c.rowBytes());
  coloured.colors.assign(8, TCanvasColor(0.f, 0.f, 1.f));
  TCanvasPaint half = paintOf(1.f, 1.f, 1.f, 0.5f);
  half.antialias    = false;
  third.drawMesh(coloured, half);
  EXPECT_EQ(a.bytes, c.bytes);
}

TEST(SkiaCanvasMesh, EdgesAreSmoothUnlessTheMeshOrThePaintSaysOtherwise) {
  TCanvasMesh mesh = rectMesh(2, 2, 5.25, 6);
  {
    Target target(8, 8);
    TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());
    canvas.drawMesh(mesh, paintOf(0.f, 0.f, 0.f));
    EXPECT_NEAR(target.alpha(5, 3), 64, 1);
  }
  {
    TCanvasPaint aliased = paintOf(0.f, 0.f, 0.f);
    aliased.antialias    = false;
    Target target(8, 8);
    TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());
    canvas.drawMesh(mesh, aliased);
    EXPECT_EQ(target.alpha(5, 3), 0);
    EXPECT_EQ(target.alpha(4, 3), 255);
  }
  mesh.smoothEdges = false;
  {
    Target target(8, 8);
    TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());
    canvas.drawMesh(mesh, paintOf(0.f, 0.f, 0.f));
    EXPECT_EQ(target.alpha(5, 3), 0);
    EXPECT_EQ(target.alpha(4, 3), 255);
  }
}

// Question Q2 of the design: a stroke that keeps the weight the old
// renderer's fringe gave it is the same mesh with a paint that grows.
TEST(SkiaCanvasMesh, OneColourCanGrow) {
  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f, 0.5f);
  paint.growDevice   = 1.0;

  Target target(12, 8);
  TSkiaCanvas canvas(target.pixels(), 12, 8, target.rowBytes());
  canvas.drawMesh(rectMesh(3, 2, 9, 6), paint);

  // Within one step everywhere: the rounded corners of the band are curves
  // Skia approximates.
  EXPECT_NEAR(target.alpha(1, 4), 0, 1);
  for (int x = 2; x < 10; ++x) EXPECT_NEAR(target.alpha(x, 4), 128, 1) << x;
  EXPECT_NEAR(target.alpha(10, 4), 0, 1);
  EXPECT_NEAR(target.alpha(6, 0), 0, 1);
  EXPECT_NEAR(target.alpha(6, 1), 128, 1);
  EXPECT_NEAR(target.alpha(6, 6), 128, 1);
  EXPECT_NEAR(target.alpha(6, 7), 0, 1);
}

//-----------------------------------------------------------------------------
// Meshes with a colour per vertex

TEST(SkiaCanvasShadedMesh, ColoursAreInterpolatedAcrossTheTriangles) {
  TCanvasMesh mesh = rectMesh(0, 0, 16, 4);
  const TCanvasColor red(1.f, 0.f, 0.f), blue(0.f, 0.f, 1.f);
  mesh.colors = {red, red, blue, blue};

  Target target(16, 4);
  TSkiaCanvas canvas(target.pixels(), 16, 4, target.rowBytes());
  canvas.drawMesh(mesh, TCanvasPaint());

  // The centre of column x is at (x + 0.5) / 16 of the way.
  EXPECT_TRUE(pixelIs(target, 0, 1, 247, 0, 8, 255, 3));
  EXPECT_TRUE(pixelIs(target, 8, 2, 120, 0, 135, 255, 3));
  EXPECT_TRUE(pixelIs(target, 15, 1, 8, 0, 247, 255, 3));
}

// Skia itself draws such triangles with hard edges. The canvas has to make
// them smooth on both sides of a pixel's centre: where the centre is inside
// the mesh, and where it is outside and no triangle gives the pixel a colour.
// Between a colour and a more transparent one of another hue, the hue and
// the opacity change evenly and independently: halfway is half of each
// colour at half the opacity, not mostly the opaque one.
TEST(SkiaCanvasShadedMesh, ColoursAndAlphasAreBlendedEachOnItsOwn) {
  TCanvasMesh mesh = rectMesh(0, 0, 8, 4);
  mesh.smoothEdges = false;
  // Opaque red on the left, transparent blue on the right.
  mesh.colors = {TCanvasColor(1.f, 0.f, 0.f, 1.f), TCanvasColor(1.f, 0.f, 0.f, 1.f),
                 TCanvasColor(0.f, 0.f, 1.f, 0.f), TCanvasColor(0.f, 0.f, 1.f, 0.f)};

  Target target(8, 4);
  TSkiaCanvas canvas(target.pixels(), 8, 4, target.rowBytes());
  canvas.drawMesh(mesh, TCanvasPaint());

  // The middle of pixel 3 is 7/16 of the way: (9/16 red, 7/16 blue) at an
  // opacity of 9/16, stored premultiplied.
  EXPECT_TRUE(pixelIs(target, 3, 2, 81, 0, 63, 143, 3));
  EXPECT_TRUE(pixelIs(target, 0, 2, 225, 0, 15, 239, 3));
  EXPECT_TRUE(pixelIs(target, 7, 2, 1, 0, 15, 16, 3));

  // The paint's opacity on top of it all.
  Target faint(8, 4);
  TSkiaCanvas second(faint.pixels(), 8, 4, faint.rowBytes());
  second.drawMesh(mesh, paintOf(1.f, 1.f, 1.f, 0.5f));
  EXPECT_TRUE(pixelIs(faint, 3, 2, 40, 0, 31, 72, 3));
}

// A strip made for OpenGL may run its pieces together with triangles that
// are transparent at every corner, right across pieces that show. Those
// draw nothing; they do not rub out what is under them.
TEST(SkiaCanvasShadedMesh, ATransparentTriangleDoesNotEraseWhatItCrosses) {
  TCanvasMesh mesh;
  mesh.smoothEdges = false;
  // An opaque square from (2, 2) to (6, 6), then a transparent triangle
  // over its lower half.
  mesh.positions = {{2, 2}, {6, 2}, {6, 6}, {2, 2}, {6, 6}, {2, 6},
                    {0, 0}, {8, 0}, {4, 4}};
  mesh.colors.assign(6, TCanvasColor(1.f, 0.f, 0.f, 1.f));
  mesh.colors.insert(mesh.colors.end(), 3, TCanvasColor(1.f, 0.f, 0.f, 0.f));

  Target target(8, 8);
  TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());
  canvas.drawMesh(mesh, TCanvasPaint());

  EXPECT_TRUE(pixelIs(target, 4, 2, 255, 0, 0, 255));
  EXPECT_TRUE(pixelIs(target, 3, 5, 255, 0, 0, 255));
  EXPECT_EQ(target.alpha(4, 0), 0);
}

TEST(SkiaCanvasShadedMesh, EdgesAreSmooth) {
  const TCanvasColor green(0.f, 1.f, 0.f);
  {
    TCanvasMesh mesh = rectMesh(2, 2, 5.75, 6);
    mesh.colors.assign(4, green);
    Target target(8, 8);
    TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());
    canvas.drawMesh(mesh, TCanvasPaint());
    EXPECT_TRUE(pixelIs(target, 4, 3, 0, 255, 0, 255));
    EXPECT_TRUE(pixelIs(target, 5, 3, 0, 191, 0, 191, 2)) << "centre inside";
    EXPECT_EQ(target.alpha(6, 3), 0);
  }
  {
    TCanvasMesh mesh = rectMesh(2, 2, 5.25, 6.25);
    mesh.colors.assign(4, green);
    Target target(8, 8);
    TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());
    canvas.drawMesh(mesh, TCanvasPaint());
    EXPECT_TRUE(pixelIs(target, 4, 3, 0, 255, 0, 255));
    EXPECT_TRUE(pixelIs(target, 5, 3, 0, 64, 0, 64, 2)) << "centre outside";
    EXPECT_TRUE(pixelIs(target, 3, 6, 0, 64, 0, 64, 2)) << "centre outside";
    EXPECT_TRUE(pixelIs(target, 5, 6, 0, 16, 0, 16, 2)) << "at the corner";
    EXPECT_EQ(target.alpha(6, 3), 0);
    EXPECT_EQ(target.alpha(1, 3), 0);
    EXPECT_EQ(target.alpha(3, 1), 0);
  }
}

TEST(SkiaCanvasShadedMesh, EdgesStayHardWhenTheMeshSaysSo) {
  TCanvasMesh mesh = rectMesh(2, 2, 5.25, 6);
  mesh.colors.assign(4, TCanvasColor(0.f, 1.f, 0.f));
  mesh.smoothEdges = false;

  Target target(8, 8);
  TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());
  canvas.drawMesh(mesh, TCanvasPaint());
  EXPECT_EQ(target.alpha(4, 3), 255);
  EXPECT_EQ(target.alpha(5, 3), 0);
}

TEST(SkiaCanvasShadedMesh, IsPaintedOnceWhereItsTrianglesOverlap) {
  // Two squares that share the pixels from (5, 5) to (8, 8).
  TCanvasMesh mesh;
  mesh.positions = {{2, 2}, {8, 2}, {8, 8}, {2, 2}, {8, 8},   {2, 8},
                    {5, 5}, {11, 5}, {11, 11}, {5, 5}, {11, 11}, {5, 11}};
  for (bool smooth : {false, true}) {
    SCOPED_TRACE(smooth ? "smooth edges" : "hard edges");
    mesh.smoothEdges = smooth;

    // Translucent through the vertices...
    mesh.colors.assign(6, TCanvasColor(1.f, 0.f, 0.f, 0.5f));
    mesh.colors.insert(mesh.colors.end(), 6, TCanvasColor(0.f, 0.f, 1.f, 0.5f));
    {
      Target target(12, 12);
      TSkiaCanvas canvas(target.pixels(), 12, 12, target.rowBytes());
      canvas.drawMesh(mesh, TCanvasPaint());
      EXPECT_TRUE(pixelIs(target, 3, 3, 128, 0, 0, 128));
      EXPECT_TRUE(pixelIs(target, 6, 6, 0, 0, 128, 128)) << "the later one";
      EXPECT_TRUE(pixelIs(target, 9, 9, 0, 0, 128, 128));
    }

    // ...or through the paint.
    mesh.colors.assign(6, TCanvasColor(1.f, 0.f, 0.f));
    mesh.colors.insert(mesh.colors.end(), 6, TCanvasColor(0.f, 0.f, 1.f));
    {
      Target target(12, 12);
      TSkiaCanvas canvas(target.pixels(), 12, 12, target.rowBytes());
      canvas.drawMesh(mesh, paintOf(1.f, 1.f, 1.f, 0.5f));
      EXPECT_TRUE(pixelIs(target, 3, 3, 128, 0, 0, 128));
      EXPECT_TRUE(pixelIs(target, 6, 6, 0, 0, 128, 128));
      EXPECT_TRUE(pixelIs(target, 9, 9, 0, 0, 128, 128));
    }
  }
}

TEST(SkiaCanvasShadedMesh, IsDrawnWhereTheTransformPutsIt) {
  TCanvasMesh mesh = rectMesh(1, 1, 3, 2);
  // Two colours: with one the mesh would be a plain shape (see
  // AMeshOfOneColourIsAShape below).
  mesh.colors.assign(4, TCanvasColor(0.f, 1.f, 0.f));
  mesh.colors[0] = TCanvasColor(0.f, 0.5f, 0.f);

  Target target(16, 8);
  TSkiaCanvas canvas(target.pixels(), 16, 8, target.rowBytes());
  canvas.concat(TCanvasAffine::translation(2, 0) * TCanvasAffine::scale(4, 2));
  canvas.drawMesh(mesh, TCanvasPaint());  // device (6, 2) to (14, 4)

  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 16; ++x) {
      const bool inside = x >= 6 && x < 14 && y >= 2 && y < 4;
      EXPECT_EQ(target.alpha(x, y), inside ? 255 : 0)
          << "at (" << x << ", " << y << ")";
    }
}

//-----------------------------------------------------------------------------
// Images

TEST(SkiaCanvasImage, IsDrawnBottomRowFirst) {
  const FourColours colours;

  Target target(8, 8);
  TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());
  TCanvasImageOptions options;
  options.filter = TCanvasFilter::Nearest;
  canvas.drawImage(colours.image(), TCanvasAffine::translation(3, 1), options);

  EXPECT_TRUE(pixelIs(target, 3, 1, 255, 0, 0, 255)) << "red";
  EXPECT_TRUE(pixelIs(target, 4, 1, 0, 255, 0, 255)) << "green";
  EXPECT_TRUE(pixelIs(target, 3, 2, 0, 0, 255, 255)) << "blue";
  EXPECT_TRUE(pixelIs(target, 4, 2, 255, 255, 255, 255)) << "white";
  EXPECT_EQ(target.alpha(2, 1), 0);
  EXPECT_EQ(target.alpha(5, 1), 0);
  EXPECT_EQ(target.alpha(3, 0), 0);
  EXPECT_EQ(target.alpha(3, 3), 0);
}

TEST(SkiaCanvasImage, IsPlacedByItsOwnTransformAndTheCanvass) {
  const FourColours colours;

  Target target(16, 16);
  TSkiaCanvas canvas(target.pixels(), 16, 16, target.rowBytes());
  TCanvasImageOptions options;
  options.filter = TCanvasFilter::Nearest;
  canvas.concat(TCanvasAffine::translation(4, 0));
  canvas.drawImage(colours.image(), TCanvasAffine::scale(4, 4), options);

  EXPECT_TRUE(pixelIs(target, 5, 1, 255, 0, 0, 255));
  EXPECT_TRUE(pixelIs(target, 7, 3, 255, 0, 0, 255));
  EXPECT_TRUE(pixelIs(target, 8, 3, 0, 255, 0, 255));
  EXPECT_TRUE(pixelIs(target, 5, 4, 0, 0, 255, 255));
  EXPECT_TRUE(pixelIs(target, 11, 7, 255, 255, 255, 255));
  EXPECT_EQ(target.alpha(3, 1), 0);
  EXPECT_EQ(target.alpha(12, 1), 0);
}

TEST(SkiaCanvasImage, PixelsAreStraightOrPremultipliedAsTheImageSays) {
  // The same translucent red, written both ways.
  const unsigned char straight[4]      = {0, 0, 255, 128};
  const unsigned char premultiplied[4] = {0, 0, 128, 128};
  TCanvasImage image;
  image.width = image.height = 1;
  image.rowBytes             = 4;

  Target target(2, 1);
  TSkiaCanvas canvas(target.pixels(), 2, 1, target.rowBytes());
  image.pixels        = straight;
  image.premultiplied = false;
  canvas.drawImage(image, TCanvasAffine(), TCanvasImageOptions());
  image.pixels        = premultiplied;
  image.premultiplied = true;
  canvas.drawImage(image, TCanvasAffine::translation(1, 0),
                   TCanvasImageOptions());

  EXPECT_TRUE(pixelIs(target, 0, 0, 128, 0, 0, 128));
  EXPECT_TRUE(pixelIs(target, 1, 0, 128, 0, 0, 128));
}

TEST(SkiaCanvasImage, OpacityScalesTheWholeImage) {
  const FourColours colours;
  TCanvasImageOptions options;
  options.filter  = TCanvasFilter::Nearest;
  options.opacity = 0.5;

  Target target(2, 2);
  TSkiaCanvas canvas(target.pixels(), 2, 2, target.rowBytes());
  canvas.drawImage(colours.image(), TCanvasAffine(), options);
  EXPECT_TRUE(pixelIs(target, 0, 0, 128, 0, 0, 128));
  EXPECT_TRUE(pixelIs(target, 1, 1, 128, 128, 128, 128));
}

TEST(SkiaCanvasImage, LinearFilteringBlendsNeighbours) {
  // Black beside white, eight times as large.
  const unsigned char bytes[8] = {0, 0, 0, 255, 255, 255, 255, 255};
  TCanvasImage image;
  image.pixels   = bytes;
  image.width    = 2;
  image.height   = 1;
  image.rowBytes = 8;

  Target target(16, 8);
  TSkiaCanvas canvas(target.pixels(), 16, 8, target.rowBytes());
  canvas.drawImage(image, TCanvasAffine::scale(8, 8), TCanvasImageOptions());

  EXPECT_TRUE(pixelIs(target, 1, 4, 0, 0, 0, 255, 2));
  EXPECT_TRUE(pixelIs(target, 14, 4, 255, 255, 255, 255, 2));
  const int before = target.at(7, 4)[2], after = target.at(8, 4)[2];
  EXPECT_GT(before, 64);
  EXPECT_LT(before, 128);
  EXPECT_GT(after, 128);
  EXPECT_LT(after, 192);
}

//-----------------------------------------------------------------------------
// Shaders

namespace {

TCanvasPaint redToBlue(const TCanvasPoint &from, const TCanvasPoint &to) {
  TCanvasPaint paint;
  paint.shader.kind  = TCanvasShader::Kind::LinearGradient;
  paint.shader.p0    = from;
  paint.shader.p1    = to;
  paint.shader.stops = {{0.0, TCanvasColor(1.f, 0.f, 0.f)},
                        {1.0, TCanvasColor(0.f, 0.f, 1.f)}};
  return paint;
}

void expectRedToBlueAcrossSixteenPixels(const Target &target) {
  EXPECT_TRUE(pixelIs(target, 0, 1, 247, 0, 8, 255, 3));
  EXPECT_TRUE(pixelIs(target, 8, 2, 120, 0, 135, 255, 3));
  EXPECT_TRUE(pixelIs(target, 15, 1, 8, 0, 247, 255, 3));
}

}  // namespace

TEST(SkiaCanvasShader, ALinearGradientRunsBetweenItsPoints) {
  Target target(16, 4);
  TSkiaCanvas canvas(target.pixels(), 16, 4, target.rowBytes());
  canvas.fillPath(rectPath(0, 0, 16, 4), TCanvasFillRule::NonZero,
                  redToBlue({0, 0}, {16, 0}));
  expectRedToBlueAcrossSixteenPixels(target);
}

// The points of a gradient are in the coordinates of the shape, on each of
// the ways a shape can reach the rasteriser.
TEST(SkiaCanvasShader, IsInTheCoordinatesOfTheShape) {
  const TCanvasPaint gradient = redToBlue({0, 0}, {8, 0});
  {
    SCOPED_TRACE("a path");
    Target target(16, 4);
    TSkiaCanvas canvas(target.pixels(), 16, 4, target.rowBytes());
    canvas.concat(TCanvasAffine::scale(2, 1));
    canvas.fillPath(rectPath(0, 0, 8, 4), TCanvasFillRule::NonZero, gradient);
    expectRedToBlueAcrossSixteenPixels(target);
  }
  {
    SCOPED_TRACE("a path that grows");
    TCanvasPaint grown = gradient;
    grown.growDevice   = 1.0;
    Target target(16, 4);
    TSkiaCanvas canvas(target.pixels(), 16, 4, target.rowBytes());
    canvas.concat(TCanvasAffine::scale(2, 1));
    canvas.fillPath(rectPath(0, 0, 8, 4), TCanvasFillRule::NonZero, grown);
    expectRedToBlueAcrossSixteenPixels(target);
  }
  {
    SCOPED_TRACE("a line of a width in pixels");
    TCanvasPen pen;
    pen.width = 4.0;
    TCanvasPath line;
    line.moveTo({0, 2}).lineTo({8, 2});
    Target target(16, 4);
    TSkiaCanvas canvas(target.pixels(), 16, 4, target.rowBytes());
    canvas.concat(TCanvasAffine::scale(2, 1));
    canvas.strokePath(line, pen, gradient);
    expectRedToBlueAcrossSixteenPixels(target);
  }
  {
    SCOPED_TRACE("a mesh");
    Target target(16, 4);
    TSkiaCanvas canvas(target.pixels(), 16, 4, target.rowBytes());
    canvas.concat(TCanvasAffine::scale(2, 1));
    canvas.drawMesh(rectMesh(0, 0, 8, 4), gradient);
    expectRedToBlueAcrossSixteenPixels(target);
  }
}

TEST(SkiaCanvasShader, ThePaintGivesAShaderItsOpacity) {
  TCanvasPaint paint = redToBlue({0, 0}, {16, 0});
  paint.color        = TCanvasColor(0.f, 1.f, 0.f, 0.5f);  // green is not used

  Target target(16, 4);
  TSkiaCanvas canvas(target.pixels(), 16, 4, target.rowBytes());
  canvas.fillPath(rectPath(0, 0, 16, 4), TCanvasFillRule::NonZero, paint);
  EXPECT_TRUE(pixelIs(target, 0, 1, 124, 0, 4, 128, 3));
  EXPECT_TRUE(pixelIs(target, 15, 1, 4, 0, 124, 128, 3));
}

TEST(SkiaCanvasShader, ARadialGradientRunsFromItsCentreToItsRadius) {
  TCanvasPaint paint;
  paint.shader.kind   = TCanvasShader::Kind::RadialGradient;
  paint.shader.p0     = {8, 8};
  paint.shader.radius = 8;
  paint.shader.stops  = {{0.0, TCanvasColor(1.f, 1.f, 1.f)},
                         {1.0, TCanvasColor(0.f, 0.f, 0.f)}};

  Target target(16, 16);
  TSkiaCanvas canvas(target.pixels(), 16, 16, target.rowBytes());
  canvas.fillPath(rectPath(0, 0, 16, 16), TCanvasFillRule::NonZero, paint);

  // Half a pixel's diagonal from the centre; half a pixel short of the
  // radius; past the radius, where the last colour goes on.
  EXPECT_TRUE(pixelIs(target, 8, 8, 232, 232, 232, 255, 4));
  EXPECT_TRUE(pixelIs(target, 15, 8, 15, 15, 15, 255, 4));
  EXPECT_TRUE(pixelIs(target, 15, 15, 0, 0, 0, 255, 1));
}

TEST(SkiaCanvasShader, AGradientThatIsNotOneFallsBackToAFlatColour) {
  Target target(4, 1);
  TSkiaCanvas canvas(target.pixels(), 4, 1, target.rowBytes());

  // One stop: that colour.
  TCanvasPaint oneStop     = redToBlue({0, 0}, {4, 0});
  oneStop.shader.stops     = {{0.0, TCanvasColor(0.f, 1.f, 0.f)}};
  canvas.fillPath(rectPath(0, 0, 1, 1), TCanvasFillRule::NonZero, oneStop);
  EXPECT_TRUE(pixelIs(target, 0, 0, 0, 255, 0, 255));

  // No stops, or a radius of nothing: the paint's own colour.
  TCanvasPaint noStops = redToBlue({0, 0}, {4, 0});
  noStops.shader.stops.clear();
  noStops.color = TCanvasColor(1.f, 1.f, 0.f);
  canvas.fillPath(rectPath(1, 0, 2, 1), TCanvasFillRule::NonZero, noStops);
  EXPECT_TRUE(pixelIs(target, 1, 0, 255, 255, 0, 255));

  TCanvasPaint noRadius   = redToBlue({0, 0}, {4, 0});
  noRadius.shader.kind    = TCanvasShader::Kind::RadialGradient;
  noRadius.shader.radius  = 0.0;
  noRadius.color          = TCanvasColor(0.f, 1.f, 1.f);
  canvas.fillPath(rectPath(2, 0, 3, 1), TCanvasFillRule::NonZero, noRadius);
  EXPECT_TRUE(pixelIs(target, 2, 0, 0, 255, 255, 255));
}

namespace {

TCanvasPaint fourColoursPaint(const FourColours &colours, TCanvasTile tile,
                              const TCanvasAffine &imageToLocal) {
  TCanvasPaint paint;
  paint.shader.kind         = TCanvasShader::Kind::Image;
  paint.shader.image        = colours.image();
  paint.shader.imageToLocal = imageToLocal;
  paint.shader.tile         = tile;
  paint.shader.filter       = TCanvasFilter::Nearest;
  return paint;
}

}  // namespace

TEST(SkiaCanvasShader, AnImageRepeatsOrKeepsItsEdge) {
  const FourColours colours;
  const TCanvasPath all = rectPath(0, 0, 12, 12);

  Target repeated(12, 12);
  TSkiaCanvas a(repeated.pixels(), 12, 12, repeated.rowBytes());
  a.fillPath(all, TCanvasFillRule::NonZero,
             fourColoursPaint(colours, TCanvasTile::Repeat,
                              TCanvasAffine::scale(2, 2)));
  EXPECT_TRUE(pixelIs(repeated, 1, 1, 255, 0, 0, 255));
  EXPECT_TRUE(pixelIs(repeated, 3, 1, 0, 255, 0, 255));
  EXPECT_TRUE(pixelIs(repeated, 1, 3, 0, 0, 255, 255));
  EXPECT_TRUE(pixelIs(repeated, 5, 1, 255, 0, 0, 255)) << "red again";
  EXPECT_TRUE(pixelIs(repeated, 1, 5, 255, 0, 0, 255)) << "red again";

  Target clamped(12, 12);
  TSkiaCanvas b(clamped.pixels(), 12, 12, clamped.rowBytes());
  b.fillPath(all, TCanvasFillRule::NonZero,
             fourColoursPaint(colours, TCanvasTile::Clamp,
                              TCanvasAffine::scale(2, 2)));
  EXPECT_TRUE(pixelIs(clamped, 1, 1, 255, 0, 0, 255));
  EXPECT_TRUE(pixelIs(clamped, 9, 1, 0, 255, 0, 255)) << "green goes on";
  EXPECT_TRUE(pixelIs(clamped, 1, 9, 0, 0, 255, 255)) << "blue goes on";
}

// What a deformed picture is: the image stays where it is, the mesh says
// which part of it each vertex shows.
TEST(SkiaCanvasShader, AMeshLooksItUpAtItsTextureCoordinates) {
  const FourColours colours;
  TCanvasMesh mesh = rectMesh(4, 4, 12, 12);
  mesh.uvs         = {{0, 0}, {0, 2}, {2, 0}, {2, 2}};
  const TCanvasPaint paint =
      fourColoursPaint(colours, TCanvasTile::Clamp, TCanvasAffine());

  Target target(16, 16);
  TSkiaCanvas canvas(target.pixels(), 16, 16, target.rowBytes());
  canvas.drawMesh(mesh, paint);

  EXPECT_TRUE(pixelIs(target, 5, 5, 255, 0, 0, 255));
  EXPECT_TRUE(pixelIs(target, 10, 5, 0, 255, 0, 255));
  EXPECT_TRUE(pixelIs(target, 5, 10, 0, 0, 255, 255));
  EXPECT_TRUE(pixelIs(target, 10, 10, 255, 255, 255, 255));
  EXPECT_EQ(target.alpha(3, 5), 0);
  EXPECT_EQ(target.alpha(12, 5), 0);

  // Vertex colours multiply what the shader gives.
  mesh.colors.assign(4, TCanvasColor(0.5f, 0.5f, 0.5f));
  Target darker(16, 16);
  TSkiaCanvas darkerCanvas(darker.pixels(), 16, 16, darker.rowBytes());
  darkerCanvas.drawMesh(mesh, paint);
  EXPECT_TRUE(pixelIs(darker, 5, 5, 128, 0, 0, 255, 2));
  EXPECT_TRUE(pixelIs(darker, 10, 10, 128, 128, 128, 255, 2));
}

// A brush tip: a large image drawn a few pixels across. Sampled linearly
// it shows whichever of its pixels each pixel happens to land on; with
// mipmaps every pixel shows the average of the pixels it covers.
TEST(SkiaCanvasShader, AMeshShrunkAGreatDealIsSampledFromMipmaps) {
  // Columns one pixel wide, black and white by turns.
  const int side = 60;
  std::vector<unsigned char> stripes((std::size_t)side * side * 4);
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x) {
      unsigned char *p = &stripes[((std::size_t)y * side + x) * 4];
      p[0] = p[1] = p[2] = (x % 2) ? 255 : 0;
      p[3]                = 255;
    }

  // Four pixels show all of it: fifteen columns each. A pixel's centre
  // lands on the middle of one column.
  TCanvasMesh mesh = rectMesh(0, 0, 4, 4);
  mesh.uvs         = {{0, 0}, {0, side}, {side, 0}, {side, side}};
  TCanvasPaint paint = paintOf(1.f, 1.f, 1.f);
  paint.shader.kind           = TCanvasShader::Kind::Image;
  paint.shader.image.pixels   = stripes.data();
  paint.shader.image.width    = side;
  paint.shader.image.height   = side;
  paint.shader.image.rowBytes = side * 4;
  paint.shader.tile           = TCanvasTile::Clamp;

  for (TCanvasFilter filter : {TCanvasFilter::Linear, TCanvasFilter::Mipmap}) {
    paint.shader.filter = filter;
    Target target(4, 4);
    TSkiaCanvas canvas(target.pixels(), 4, 4, target.rowBytes());
    canvas.drawMesh(mesh, paint);
    int extremes = 0, greys = 0;
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) {
        const int red = target.bytes[((std::size_t)y * 4 + x) * 4 + 2];
        extremes += red < 30 || red > 225;
        greys += red > 90 && red < 165;
      }
    if (filter == TCanvasFilter::Linear)
      EXPECT_EQ(extremes, 16) << "linear: one column each";
    else
      EXPECT_EQ(greys, 16) << "mipmaps: the columns averaged";
  }
}

//-----------------------------------------------------------------------------
// Whole pictures

TEST(SkiaCanvasPicture, ReplayingARecordingGivesTheSamePixels) {
  const FourColours colours;

  Target direct(64, 48);
  {
    TSkiaCanvas canvas(direct.pixels(), 64, 48, direct.rowBytes());
    drawScene(canvas, colours);
  }

  TCanvasRecorder recorder(TCanvasIRect(0, 0, 64, 48));
  drawScene(recorder, colours);
  Target replayed(64, 48);
  {
    TSkiaCanvas canvas(replayed.pixels(), 64, 48, replayed.rowBytes());
    recorder.displayList().replay(canvas);
  }

  EXPECT_EQ(direct.bytes, replayed.bytes);
}

// Prints a hash of the picture, to compare by hand what different machines
// and different builds of Skia draw. NEXTTOONZ_CANVAS_SKIA_DUMP=<file>
// writes the pixels (64 x 48, BGRA, bottom row first) for a closer look.
TEST(SkiaCanvasPicture, RenderingIsRepeatable) {
  const FourColours colours;

  Target first(64, 48), second(64, 48);
  {
    TSkiaCanvas canvas(first.pixels(), 64, 48, first.rowBytes());
    drawScene(canvas, colours);
  }
  {
    TSkiaCanvas canvas(second.pixels(), 64, 48, second.rowBytes());
    drawScene(canvas, colours);
  }
  EXPECT_EQ(first.bytes, second.bytes);

  // The picture is not empty and not flat.
  EXPECT_TRUE(pixelIs(first, 1, 46, 255, 255, 255, 255));
  EXPECT_TRUE(pixelIs(first, 10, 10, 230, 153, 26, 255, 2));
  EXPECT_TRUE(pixelIs(first, 30, 10, 51, 128, 204, 255, 2));

  std::printf("canvas scene hash: %016llx\n",
              (unsigned long long)first.hash());
  if (const char *path = std::getenv("NEXTTOONZ_CANVAS_SKIA_DUMP")) {
    if (std::FILE *file = std::fopen(path, "wb")) {
      std::fwrite(first.bytes.data(), 1, first.bytes.size(), file);
      std::fclose(file);
    }
  }
}
