// The OpenGL canvas on its own. What is checked is what any driver should
// agree on: the inside of shapes, and that their edges are somewhere
// between nothing and everything.

#include "canvas_gl_test_util.h"

#include <cstring>

TEST(GlCanvas, FillsWithPremultipliedColour) {
  TCanvasPaint paint = paintOf(1.f, 0.f, 0.f, 0.5f);
  paint.antialias    = false;
  const Target target = drawWithGl(12, 12, [&](TCanvas &canvas) {
    canvas.fillPath(rectPath(2, 2, 10, 10), TCanvasFillRule::NonZero, paint);
  });

  EXPECT_TRUE(pixelIs(target, 5, 5, 128, 0, 0, 128, 2));
  EXPECT_TRUE(pixelIs(target, 2, 2, 128, 0, 0, 128, 2));
  EXPECT_TRUE(pixelIs(target, 9, 9, 128, 0, 0, 128, 2));
  EXPECT_EQ(target.alpha(1, 5), 0);
  EXPECT_EQ(target.alpha(10, 5), 0);
}

// The smooth line around an antialiased fill lies half outside the shape.
TEST(GlCanvas, AnAntialiasedFillHasAFringe) {
  const Target target = drawWithGl(12, 12, [&](TCanvas &canvas) {
    canvas.fillPath(rectPath(3, 3, 9, 9), TCanvasFillRule::NonZero,
                    paintOf(0.f, 0.f, 0.f));
  });

  EXPECT_EQ(target.alpha(5, 5), 255);
  EXPECT_GT(target.alpha(2, 5), 30) << "half a pixel of fringe";
  EXPECT_LT(target.alpha(2, 5), 225);
  EXPECT_EQ(target.alpha(0, 5), 0);
}

TEST(GlCanvas, FillRuleDecidesWhatANestedContourDoes) {
  TCanvasPath path = rectPath(1, 1, 11, 11);
  const TCanvasPoint inner[4] = {{4, 4}, {8, 4}, {8, 8}, {4, 8}};
  path.addPolygon(inner, 4);
  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f);
  paint.antialias    = false;

  const Target nonZero = drawWithGl(12, 12, [&](TCanvas &canvas) {
    canvas.fillPath(path, TCanvasFillRule::NonZero, paint);
  });
  EXPECT_EQ(nonZero.alpha(6, 6), 255);

  const Target evenOdd = drawWithGl(12, 12, [&](TCanvas &canvas) {
    canvas.fillPath(path, TCanvasFillRule::EvenOdd, paint);
  });
  EXPECT_EQ(evenOdd.alpha(6, 6), 0);
  EXPECT_EQ(evenOdd.alpha(2, 6), 255);
}

TEST(GlCanvas, TransformsNestAndComeBack) {
  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f);
  paint.antialias    = false;
  const Target target = drawWithGl(16, 8, [&](TCanvas &canvas) {
    canvas.save();
    canvas.concat(TCanvasAffine::translation(4, 0));
    canvas.concat(TCanvasAffine::scale(2, 2));
    canvas.fillPath(rectPath(0, 0, 1, 1), TCanvasFillRule::NonZero, paint);
    canvas.restore();
    canvas.fillPath(rectPath(12, 4, 14, 6), TCanvasFillRule::NonZero, paint);
  });

  EXPECT_EQ(target.alpha(4, 0), 255);
  EXPECT_EQ(target.alpha(5, 1), 255);
  EXPECT_EQ(target.alpha(3, 0), 0);
  EXPECT_EQ(target.alpha(6, 0), 0);
  EXPECT_EQ(target.alpha(4, 2), 0);
  EXPECT_EQ(target.alpha(12, 4), 255) << "drawn after restore()";
  EXPECT_EQ(target.alpha(13, 5), 255);
  EXPECT_EQ(target.alpha(14, 5), 0);
}

TEST(GlCanvas, LeavesTheModelviewMatrixAsItFoundIt) {
  GLdouble before[16], after[16];
  drawWithGl(8, 8, [&](TCanvas &) {});  // makes a context current
  TOfflineGL gl(TDimension(8, 8));
  gl.makeCurrent();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glTranslated(3.0, 5.0, 0.0);
  glGetDoublev(GL_MODELVIEW_MATRIX, before);
  {
    TGlStencilMasks masks;
    TGlCanvas canvas(TCanvasIRect(0, 0, 8, 8), masks);
    canvas.save();
    canvas.concat(TCanvasAffine::scale(2, 3));
    canvas.clipPath(rectPath(0, 0, 1, 1), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Intersect, false);
    // Destroyed without restore().
  }
  glGetDoublev(GL_MODELVIEW_MATRIX, after);
  EXPECT_EQ(std::memcmp(before, after, sizeof(before)), 0);
  EXPECT_FALSE(glIsEnabled(GL_STENCIL_TEST)) << "the clip is gone as well";
  gl.doneCurrent();
}

TEST(GlCanvas, ClipsToTheInsideOrTheOutsideUntilRestore) {
  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f);
  paint.antialias    = false;
  const TCanvasPath all = rectPath(0, 0, 8, 8);

  const Target inside = drawWithGl(8, 8, [&](TCanvas &canvas) {
    canvas.clipPath(rectPath(2, 2, 6, 6), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Intersect, false);
    canvas.fillPath(all, TCanvasFillRule::NonZero, paint);
  });
  EXPECT_EQ(inside.alpha(3, 3), 255);
  EXPECT_EQ(inside.alpha(1, 3), 0);
  EXPECT_EQ(inside.alpha(6, 3), 0);

  const Target outside = drawWithGl(8, 8, [&](TCanvas &canvas) {
    canvas.clipPath(rectPath(2, 2, 6, 6), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Difference, false);
    canvas.fillPath(all, TCanvasFillRule::NonZero, paint);
  });
  EXPECT_EQ(outside.alpha(3, 3), 0);
  EXPECT_EQ(outside.alpha(1, 3), 255);

  // Two clips, one inside the other's save: both hold, then only the first.
  const Target nested = drawWithGl(8, 8, [&](TCanvas &canvas) {
    canvas.clipPath(rectPath(0, 0, 6, 8), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Intersect, false);
    canvas.save();
    canvas.clipPath(rectPath(0, 0, 8, 4), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Intersect, false);
    canvas.fillPath(all, TCanvasFillRule::NonZero, paintOf(1.f, 0.f, 0.f));
    canvas.restore();
    TCanvasPaint blue = paintOf(0.f, 0.f, 1.f, 0.5f);
    blue.antialias    = false;
    canvas.fillPath(all, TCanvasFillRule::NonZero, blue);
  });
  EXPECT_TRUE(pixelIs(nested, 2, 2, 128, 0, 128, 255, 2)) << "both fills";
  EXPECT_TRUE(pixelIs(nested, 2, 6, 0, 0, 128, 128, 2)) << "the second only";
  EXPECT_EQ(nested.alpha(7, 2), 0) << "outside the first clip";

  const Target nothing = drawWithGl(8, 8, [&](TCanvas &canvas) {
    canvas.clipPath(TCanvasPath(), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Intersect, false);
    canvas.fillPath(all, TCanvasFillRule::NonZero, paint);
  });
  EXPECT_EQ(nothing.alpha(3, 3), 0);
}

TEST(GlCanvas, ADevicePenKeepsItsWidthUnderATransform) {
  TCanvasPen pen;
  pen.width = 3.0;
  TCanvasPath line;
  line.moveTo({1, 2.125}).lineTo({5, 2.125});  // device y = 8.5

  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f);
  paint.antialias    = false;
  const Target target = drawWithGl(24, 16, [&](TCanvas &canvas) {
    canvas.concat(TCanvasAffine::scale(4, 4));
    canvas.strokePath(line, pen, paint);
  });

  int rows = 0;
  for (int y = 0; y < 16; ++y) rows += target.alpha(10, y) == 255;
  EXPECT_EQ(rows, 3);
  EXPECT_EQ(target.alpha(10, 8), 255);
  EXPECT_EQ(target.alpha(2, 8), 0);
  EXPECT_EQ(target.alpha(22, 8), 0);
}

TEST(GlCanvas, DashesBecomeAStipplePattern) {
  TCanvasPen pen;
  pen.width = 1.0;
  pen.dash  = {4, 4};
  TCanvasPath line;
  line.moveTo({0, 2.5}).lineTo({16, 2.5});

  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f);
  paint.antialias    = false;
  const Target target = drawWithGl(16, 5, [&](TCanvas &canvas) {
    canvas.strokePath(line, pen, paint);
  });

  std::string columns;
  for (int x = 0; x < 16; ++x) columns += target.alpha(x, 2) ? '#' : '.';
  EXPECT_EQ(columns, "####....####....");
}

// A stroke outline that folds back over itself, translucent: the stencil
// buffer keeps the overlap from being painted twice.
TEST(GlCanvas, ATranslucentMeshOfOneColourCoversEachPixelOnce) {
  TCanvasMesh strip;
  strip.mode      = TCanvasMesh::Mode::TriangleStrip;
  strip.positions = {{2, 2}, {2, 6}, {6, 2},  {6, 6},
                     {10, 2}, {10, 6}, {7, 2}, {7, 6}};
  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f, 0.5f);

  for (bool antialias : {false, true}) {
    SCOPED_TRACE(antialias ? "with the fringe" : "without antialiasing");
    paint.antialias     = antialias;
    const Target target = drawWithGl(12, 8, [&](TCanvas &canvas) {
      canvas.drawMesh(strip, paint);
    });
    for (int y = 3; y < 5; ++y)
      for (int x = 3; x < 9; ++x)
        EXPECT_NEAR(target.alpha(x, y), 128, 2)
            << "at (" << x << ", " << y << ")";
    EXPECT_EQ(target.alpha(0, 4), 0);
    EXPECT_EQ(target.alpha(5, 0), 0);
  }
}

TEST(GlCanvas, AStripOfOneColourHasAFringeThatFadesOutwards) {
  const Target target = drawWithGl(12, 12, [&](TCanvas &canvas) {
    canvas.drawMesh(rectMesh(3, 3, 9, 9), paintOf(0.f, 0.f, 0.f));
  });
  EXPECT_EQ(target.alpha(5, 5), 255);
  EXPECT_GT(target.alpha(2, 5), 30);
  EXPECT_LT(target.alpha(2, 5), 225);
  EXPECT_EQ(target.alpha(1, 5), 0);

  TCanvasMesh hard = rectMesh(3, 3, 9, 9);
  hard.smoothEdges = false;
  const Target without = drawWithGl(12, 12, [&](TCanvas &canvas) {
    canvas.drawMesh(hard, paintOf(0.f, 0.f, 0.f));
  });
  EXPECT_EQ(without.alpha(5, 5), 255);
  EXPECT_EQ(without.alpha(2, 5), 0);
}

TEST(GlCanvas, VertexColoursAreInterpolated) {
  TCanvasMesh mesh = rectMesh(0, 0, 16, 4);
  const TCanvasColor red(1.f, 0.f, 0.f), blue(0.f, 0.f, 1.f);
  mesh.colors = {red, red, blue, blue};

  const Target target = drawWithGl(16, 4, [&](TCanvas &canvas) {
    canvas.drawMesh(mesh, TCanvasPaint());
  });
  EXPECT_TRUE(pixelIs(target, 0, 1, 247, 0, 8, 255, 4));
  EXPECT_TRUE(pixelIs(target, 8, 2, 120, 0, 135, 255, 4));
  EXPECT_TRUE(pixelIs(target, 15, 1, 8, 0, 247, 255, 4));
}

TEST(GlCanvas, DrawsImagesBottomRowFirst) {
  const FourColours colours;
  TCanvasImageOptions options;
  options.filter = TCanvasFilter::Nearest;

  const Target target = drawWithGl(16, 16, [&](TCanvas &canvas) {
    canvas.concat(TCanvasAffine::translation(4, 0));
    canvas.drawImage(colours.image(), TCanvasAffine::scale(4, 4), options);
  });
  EXPECT_TRUE(pixelIs(target, 5, 1, 255, 0, 0, 255));
  EXPECT_TRUE(pixelIs(target, 8, 3, 0, 255, 0, 255));
  EXPECT_TRUE(pixelIs(target, 5, 4, 0, 0, 255, 255));
  EXPECT_TRUE(pixelIs(target, 11, 7, 255, 255, 255, 255));
  EXPECT_EQ(target.alpha(3, 1), 0);
  EXPECT_EQ(target.alpha(12, 1), 0);
}

TEST(GlCanvas, ImagesAreStraightOrPremultipliedAsTheySay) {
  const unsigned char straight[4]      = {0, 0, 255, 128};
  const unsigned char premultiplied[4] = {0, 0, 128, 128};
  TCanvasImage image;
  image.width = image.height = 1;
  image.rowBytes             = 4;
  TCanvasImageOptions half;
  half.opacity = 0.5;

  const Target target = drawWithGl(4, 1, [&](TCanvas &canvas) {
    image.pixels        = straight;
    image.premultiplied = false;
    canvas.drawImage(image, TCanvasAffine(), TCanvasImageOptions());
    canvas.drawImage(image, TCanvasAffine::translation(2, 0), half);
    image.pixels        = premultiplied;
    image.premultiplied = true;
    canvas.drawImage(image, TCanvasAffine::translation(1, 0),
                     TCanvasImageOptions());
    canvas.drawImage(image, TCanvasAffine::translation(3, 0), half);
  });
  EXPECT_TRUE(pixelIs(target, 0, 0, 128, 0, 0, 128, 2));
  EXPECT_TRUE(pixelIs(target, 1, 0, 128, 0, 0, 128, 2));
  EXPECT_TRUE(pixelIs(target, 2, 0, 64, 0, 0, 64, 2));
  EXPECT_TRUE(pixelIs(target, 3, 0, 64, 0, 0, 64, 2));
}

TEST(GlCanvas, GradientsRunWhereTheirPointsSay) {
  TCanvasPaint linear;
  linear.antialias    = false;
  linear.shader.kind  = TCanvasShader::Kind::LinearGradient;
  linear.shader.p0    = {0, 0};
  linear.shader.p1    = {8, 0};
  linear.shader.stops = {{0.0, TCanvasColor(1.f, 0.f, 0.f)},
                         {1.0, TCanvasColor(0.f, 0.f, 1.f)}};
  const Target across = drawWithGl(16, 4, [&](TCanvas &canvas) {
    canvas.concat(TCanvasAffine::scale(2, 1));
    canvas.fillPath(rectPath(0, 0, 8, 4), TCanvasFillRule::NonZero, linear);
  });
  EXPECT_TRUE(pixelIs(across, 0, 1, 247, 0, 8, 255, 4));
  EXPECT_TRUE(pixelIs(across, 8, 2, 120, 0, 135, 255, 4));
  EXPECT_TRUE(pixelIs(across, 15, 1, 8, 0, 247, 255, 4));

  TCanvasPaint radial;
  radial.antialias     = false;
  radial.shader.kind   = TCanvasShader::Kind::RadialGradient;
  radial.shader.p0     = {8, 8};
  radial.shader.radius = 8;
  radial.shader.stops  = {{0.0, TCanvasColor(1.f, 1.f, 1.f)},
                          {1.0, TCanvasColor(0.f, 0.f, 0.f)}};
  const Target around = drawWithGl(16, 16, [&](TCanvas &canvas) {
    canvas.fillPath(rectPath(0, 0, 16, 16), TCanvasFillRule::NonZero, radial);
  });
  EXPECT_TRUE(pixelIs(around, 8, 8, 232, 232, 232, 255, 6));
  EXPECT_TRUE(pixelIs(around, 15, 8, 15, 15, 15, 255, 6));
  EXPECT_TRUE(pixelIs(around, 15, 15, 0, 0, 0, 255, 2));
}

TEST(GlCanvas, AnImageShaderRepeatsAndFollowsTextureCoordinates) {
  const FourColours colours;
  TCanvasPaint paint;
  paint.antialias           = false;
  paint.shader.kind         = TCanvasShader::Kind::Image;
  paint.shader.image        = colours.image();
  paint.shader.imageToLocal = TCanvasAffine::scale(2, 2);
  paint.shader.filter       = TCanvasFilter::Nearest;

  const Target repeated = drawWithGl(12, 12, [&](TCanvas &canvas) {
    canvas.fillPath(rectPath(0, 0, 12, 12), TCanvasFillRule::NonZero, paint);
  });
  EXPECT_TRUE(pixelIs(repeated, 1, 1, 255, 0, 0, 255));
  EXPECT_TRUE(pixelIs(repeated, 3, 1, 0, 255, 0, 255));
  EXPECT_TRUE(pixelIs(repeated, 1, 3, 0, 0, 255, 255));
  EXPECT_TRUE(pixelIs(repeated, 5, 1, 255, 0, 0, 255));

  TCanvasMesh mesh          = rectMesh(4, 4, 12, 12);
  mesh.uvs                  = {{0, 0}, {0, 2}, {2, 0}, {2, 2}};
  mesh.smoothEdges          = false;  // or its sides get a textured line
  paint.shader.imageToLocal = TCanvasAffine();
  paint.shader.tile         = TCanvasTile::Clamp;
  const Target mapped = drawWithGl(16, 16, [&](TCanvas &canvas) {
    canvas.drawMesh(mesh, paint);
  });
  EXPECT_TRUE(pixelIs(mapped, 5, 5, 255, 0, 0, 255));
  EXPECT_TRUE(pixelIs(mapped, 10, 5, 0, 255, 0, 255));
  EXPECT_TRUE(pixelIs(mapped, 5, 10, 0, 0, 255, 255));
  EXPECT_TRUE(pixelIs(mapped, 10, 10, 255, 255, 255, 255));
  EXPECT_EQ(mapped.alpha(3, 5), 0);
}

//-----------------------------------------------------------------------------
// What the styles that draw their own primitives use

TEST(GlCanvas, TakesQuads) {
  TCanvasMesh quads;
  quads.mode        = TCanvasMesh::Mode::Quads;
  quads.smoothEdges = false;
  quads.positions   = {{1, 1}, {5, 1}, {5, 5}, {1, 5},
                       {7, 1}, {7, 5}, {11, 5}, {11, 1}};
  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f);
  paint.antialias    = false;
  const Target target = drawWithGl(
      12, 6, [&](TCanvas &canvas) { canvas.drawMesh(quads, paint); });

  for (int y = 0; y < 6; ++y)
    for (int x = 0; x < 12; ++x) {
      const bool inside =
          y >= 1 && y < 5 && ((x >= 1 && x < 5) || (x >= 7 && x < 11));
      EXPECT_EQ(target.alpha(x, y), inside ? 255 : 0)
          << "at (" << x << ", " << y << ")";
    }
}

TEST(GlCanvas, TakesPolygons) {
  TCanvasMesh hexagon;
  hexagon.mode        = TCanvasMesh::Mode::Polygon;
  hexagon.smoothEdges = false;
  hexagon.positions   = {{2, 4}, {4, 1}, {8, 1}, {10, 4}, {8, 7}, {4, 7}};
  hexagon.colors.assign(6, TCanvasColor(0.f, 0.f, 0.f));
  TCanvasPaint paint = paintOf(1.f, 1.f, 1.f);
  paint.antialias    = false;
  const Target target = drawWithGl(
      12, 8, [&](TCanvas &canvas) { canvas.drawMesh(hexagon, paint); });

  EXPECT_EQ(target.alpha(6, 4), 255);
  EXPECT_EQ(target.alpha(3, 4), 255);
  EXPECT_EQ(target.alpha(7, 6), 255);
  EXPECT_EQ(target.alpha(2, 1), 0);
  EXPECT_EQ(target.alpha(9, 6), 0);
  EXPECT_EQ(target.alpha(6, 7), 0);
}

// A mesh that brings its colours is not taken for a stroke outline: its
// primitives are blended one by one, also where they overlap. (A mesh of
// one colour in the paint covers each pixel once: see above.)
TEST(GlCanvas, AMeshWithItsOwnColoursIsBlendedPrimitiveByPrimitive) {
  TCanvasMesh quads;
  quads.mode        = TCanvasMesh::Mode::Quads;
  quads.smoothEdges = false;
  // Two quads that share the pixels from x = 4 to x = 6.
  quads.positions = {{1, 1}, {6, 1}, {6, 5}, {1, 5},
                     {4, 1}, {11, 1}, {11, 5}, {4, 5}};
  quads.colors.assign(8, TCanvasColor(0.f, 0.f, 1.f, 0.5f));
  TCanvasPaint paint = paintOf(1.f, 1.f, 1.f);
  paint.antialias    = false;
  const Target target = drawWithGl(
      12, 6, [&](TCanvas &canvas) { canvas.drawMesh(quads, paint); });

  EXPECT_NEAR(target.alpha(2, 3), 128, 2);
  EXPECT_NEAR(target.alpha(5, 3), 191, 3) << "half over a half";
  EXPECT_NEAR(target.alpha(8, 3), 128, 2);
  EXPECT_EQ(target.alpha(0, 3), 0) << "no fringe";
}

// The mask of an antialiased clip is the shape as an antialiased fill
// draws it: the pixels of the fringe belong to it, whole.
TEST(GlCanvas, AnAntialiasedClipTakesInTheFringe) {
  TCanvasPaint flat = paintOf(0.f, 0.f, 0.f);
  flat.antialias    = false;
  const auto clipped = [&](bool antialias) {
    return drawWithGl(12, 12, [&](TCanvas &canvas) {
      canvas.clipPath(rectPath(3, 3, 9, 9), TCanvasFillRule::NonZero,
                      TCanvasClipOp::Intersect, antialias);
      canvas.fillPath(rectPath(0, 0, 12, 12), TCanvasFillRule::NonZero, flat);
    });
  };

  const Target smooth = clipped(true);
  EXPECT_EQ(smooth.alpha(5, 5), 255);
  EXPECT_EQ(smooth.alpha(2, 5), 255) << "the pixel the fringe is in";
  EXPECT_EQ(smooth.alpha(0, 5), 0);

  const Target hard = clipped(false);
  EXPECT_EQ(hard.alpha(3, 5), 255);
  EXPECT_EQ(hard.alpha(2, 5), 0);
}

// Through OpenGL a fill that is also a clip is the fill and a mask of every
// pixel it touched.
TEST(GlCanvas, AFillThatIsAlsoAClipKeepsWhatIsDrawnOnItToTheFill) {
  TCanvasPaint blue = paintOf(0.f, 0.f, 1.f);
  blue.antialias    = false;
  const Target target = drawWithGl(12, 12, [&](TCanvas &canvas) {
    canvas.save();
    canvas.fillPathAndClip(rectPath(3, 3, 9, 9), TCanvasFillRule::NonZero,
                           paintOf(1.f, 0.f, 0.f));
    canvas.fillPath(rectPath(0, 0, 12, 6), TCanvasFillRule::NonZero, blue);
    canvas.restore();
    canvas.fillPath(rectPath(11, 11, 12, 12), TCanvasFillRule::NonZero, blue);
  });

  EXPECT_TRUE(pixelIs(target, 5, 7, 255, 0, 0, 255, 2)) << "the fill";
  EXPECT_TRUE(pixelIs(target, 5, 4, 0, 0, 255, 255, 2)) << "what is on it";
  EXPECT_TRUE(pixelIs(target, 2, 4, 0, 0, 255, 255, 2))
      << "at full strength where the fringe is";
  EXPECT_GT(target.alpha(2, 7), 30);
  EXPECT_LT(target.alpha(2, 7), 225);
  EXPECT_EQ(target.alpha(0, 4), 0);
  EXPECT_EQ(target.alpha(5, 1), 0) << "kept to the fill";
  EXPECT_EQ(target.alpha(11, 11), 255) << "the clip has ended";
}

TEST(GlCanvas, AMeshKeepsDrawingOffOrOnWhatItCovers) {
  TCanvasMesh quads;
  quads.mode      = TCanvasMesh::Mode::Quads;
  quads.positions = {{2, 2}, {6, 2}, {6, 6}, {2, 6},
                     {5, 3}, {5, 5}, {10, 5}, {10, 3}};
  TCanvasPaint flat = paintOf(0.f, 0.f, 0.f);
  flat.antialias    = false;
  const auto clipped = [&](TCanvasClipOp op) {
    return drawWithGl(12, 8, [&](TCanvas &canvas) {
      canvas.save();
      canvas.clipMesh(quads, op);
      canvas.fillPath(rectPath(0, 0, 12, 8), TCanvasFillRule::NonZero, flat);
      canvas.restore();
      // After the restore the clip is gone.
      canvas.fillPath(rectPath(0, 0, 1, 1), TCanvasFillRule::NonZero, flat);
    });
  };
  const Target outside = clipped(TCanvasClipOp::Difference);
  const Target inside  = clipped(TCanvasClipOp::Intersect);

  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 12; ++x) {
      if (x == 0 && y == 0) continue;
      const bool covered = (x >= 2 && x < 6 && y >= 2 && y < 6) ||
                           (x >= 5 && x < 10 && y >= 3 && y < 5);
      EXPECT_EQ(outside.alpha(x, y), covered ? 0 : 255)
          << "at (" << x << ", " << y << ")";
      EXPECT_EQ(inside.alpha(x, y), covered ? 255 : 0)
          << "at (" << x << ", " << y << ")";
    }
  EXPECT_EQ(inside.alpha(0, 0), 255);
}

// A point under a round pen: a disc in the units of the drawing, or a point
// of OpenGL's in pixels.
TEST(GlCanvas, APointUnderARoundPenIsADot) {
  TCanvasPath points;
  points.moveTo({4, 4}).moveTo({12, 4});
  TCanvasPaint flat = paintOf(0.f, 0.f, 0.f);
  flat.antialias    = false;

  TCanvasPen pen;
  pen.width = 3.0;
  pen.cap   = TCanvasPen::Cap::Round;
  for (bool device : {false, true}) {
    SCOPED_TRACE(device ? "device pen" : "local pen");
    pen.unit = device ? TCanvasPen::Unit::Device : TCanvasPen::Unit::Local;

    const Target target = drawWithGl(16, 8, [&](TCanvas &canvas) {
      canvas.strokePath(points, pen, flat);
    });
    for (int cx : {4, 12}) {
      EXPECT_EQ(target.alpha(cx, 4), 255);
      EXPECT_EQ(target.alpha(cx - 1, 4), 255);
      EXPECT_EQ(target.alpha(cx - 3, 4), 0);
      EXPECT_EQ(target.alpha(cx, 7), 0);
    }
    EXPECT_EQ(target.alpha(8, 4), 0) << "the points are not joined";

    // Under twice the transform the disc is twice the size, the point is
    // not.
    const Target scaled = drawWithGl(16, 16, [&](TCanvas &canvas) {
      canvas.concat(TCanvasAffine::scale(2, 2));
      TCanvasPath one;
      one.moveTo({4, 4});
      canvas.strokePath(one, pen, flat);
    });
    EXPECT_EQ(scaled.alpha(8, 8), 255);
    EXPECT_EQ(scaled.alpha(5, 8), device ? 0 : 255) << "centre 2.5 away";
    EXPECT_EQ(scaled.alpha(3, 8), 0);
  }

  // With flat caps a point is nothing.
  pen.cap = TCanvasPen::Cap::Butt;
  const Target none = drawWithGl(16, 8, [&](TCanvas &canvas) {
    canvas.strokePath(points, pen, flat);
  });
  for (unsigned char byte : none.bytes) ASSERT_EQ(byte, 0);
}

// A line that fades in and out along its length: the stops of the gradient
// are on its vertices, and the line is drawn with a colour at each.
TEST(GlCanvas, AGradientWithItsStopsOnTheVerticesOfALineRunsAlongIt) {
  TCanvasPath line;
  line.moveTo({2, 4.5}).lineTo({8, 4.5}).lineTo({14, 4.5});
  TCanvasPaint paint = paintOf(1.f, 1.f, 1.f);
  paint.antialias    = false;
  paint.shader.kind  = TCanvasShader::Kind::LinearGradient;
  paint.shader.p0    = {2, 4.5};
  paint.shader.p1    = {14, 4.5};
  paint.shader.stops = {{0.0, TCanvasColor(1.f, 0.f, 0.f, 0.f)},
                        {0.5, TCanvasColor(1.f, 0.f, 0.f, 1.f)},
                        {1.0, TCanvasColor(1.f, 0.f, 0.f, 0.f)}};
  const Target target = drawWithGl(16, 8, [&](TCanvas &canvas) {
    canvas.strokePath(line, TCanvasPen(), paint);
  });

  EXPECT_LT(target.alpha(2, 4), 40);
  EXPECT_GT(target.alpha(7, 4), 215);
  EXPECT_LT(target.alpha(13, 4), 40);
  for (int x = 3; x <= 7; ++x)
    EXPECT_GT(target.alpha(x, 4), target.alpha(x - 1, 4)) << "at " << x;
  for (int x = 9; x <= 13; ++x)
    EXPECT_LT(target.alpha(x, 4), target.alpha(x - 1, 4)) << "at " << x;
  EXPECT_EQ(target.alpha(7, 6), 0);
}

//-----------------------------------------------------------------------------
// For the old drawing code

// The old tessellator did not fill by the non-zero rule, and not by the
// same rule everywhere: positive winding numbers on macOS and Windows
// (positive being the way most of the outline goes round), odd ones on
// Linux. Drawing for the old code keeps that; a canvas of its own fills by
// the rule it is given.
TEST(GlCanvas, ForTheOldCodeNonZeroIsTheOldTessellatorsRule) {
  TCanvasPaint flat = paintOf(0.f, 0.f, 0.f);
  flat.antialias    = false;
  // A large contour with a small one inside it that goes the same way, and
  // beside them a small one that goes the other way.
  TCanvasPath path             = rectPath(1, 1, 7, 7);
  const TCanvasPoint inner[4] = {{3, 3}, {5, 3}, {5, 5}, {3, 5}};
  path.addPolygon(inner, 4);
  const TCanvasPoint other[4] = {{9, 2}, {9, 5}, {12, 5}, {12, 2}};
  path.addPolygon(other, 4);
  const auto draw = [&](TCanvas &canvas) {
    canvas.fillPath(path, TCanvasFillRule::NonZero, flat);
  };

  const Target own = drawWithGl(14, 8, draw);
  EXPECT_EQ(own.alpha(2, 2), 255);
  EXPECT_EQ(own.alpha(4, 4), 255) << "winding number 2";
  EXPECT_EQ(own.alpha(10, 3), 255) << "winding number -1";

  const Target old = drawWithGl(14, 8, draw, TGlCanvas::State::Callers);
  EXPECT_EQ(old.alpha(2, 2), 255);
#if defined(__linux__) || defined(__FreeBSD__)
  EXPECT_EQ(old.alpha(4, 4), 0) << "covered twice";
  EXPECT_EQ(old.alpha(10, 3), 255);
#else
  EXPECT_EQ(old.alpha(4, 4), 255);
  EXPECT_EQ(old.alpha(10, 3), 0) << "wound the other way";
#endif
}
