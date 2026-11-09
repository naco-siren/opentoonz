// The Skia canvas: meshes, images, shaders, and whole pictures.

#include "canvas_skia_test_util.h"

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
  mesh.colors.assign(4, TCanvasColor(0.f, 1.f, 0.f));

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

//-----------------------------------------------------------------------------
// Whole pictures

namespace {

// Something of everything, on 64 by 48 pixels.
void drawScene(TCanvas &canvas, const FourColours &colours) {
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

}  // namespace

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
