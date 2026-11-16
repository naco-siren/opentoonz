// The same calls through the Skia canvas and through the OpenGL canvas.
//
// The two are not meant to give the same pixels: one computes how much of a
// pixel a shape covers, the other fills pixels whole and softens outlines
// with a line or a band one pixel wide. What they have to agree on is where
// things are and what colour they have, so each picture is compared by how
// many pixels differ noticeably. The limits are about twice what an AMD
// Radeon Pro 5700 on macOS gives; the numbers are printed so that another
// driver's can be read off.
//
// The OpenGL canvas's line or band lies half outside the shape, whatever
// the paint says. The pictures here ask for that explicitly (a paint that
// grows by half a pixel), as code that wants the old look on both canvases
// would.

#ifdef NEXTTOONZ_HAVE_SKIA

#include "canvas_gl_test_util.h"

#include "canvas/tcanvasrecorder.h"
#include "canvas/tskiacanvas.h"

#include <cstdio>
#include <functional>

namespace {

struct Difference {
  double mean = 0.0;       // of the largest channel difference per pixel
  double noticeable = 0.0; // share of pixels that differ by more than 32
  double gross = 0.0;      // share of pixels that differ by more than 96
  int largest = 0;
};

Difference compare(const Target &a, const Target &b) {
  Difference d;
  const std::size_t pixels = a.bytes.size() / 4;
  for (std::size_t i = 0; i < pixels; ++i) {
    int worst = 0;
    for (int c = 0; c < 4; ++c)
      worst = std::max(worst,
                       std::abs(a.bytes[i * 4 + c] - b.bytes[i * 4 + c]));
    d.mean += worst;
    d.noticeable += worst > 32;
    d.gross += worst > 96;
    d.largest = std::max(d.largest, worst);
  }
  d.mean /= pixels, d.noticeable /= pixels, d.gross /= pixels;
  return d;
}

// Draws the picture through both canvases, from one recording of it.
Difference bothWays(const char *name, int width, int height,
                    const std::function<void(TCanvas &)> &draw) {
  TCanvasRecorder recorder(TCanvasIRect(0, 0, width, height));
  draw(recorder);
  const TCanvasDisplayList &list = recorder.displayList();

  Target skia(width, height);
  {
    TSkiaCanvas canvas(skia.pixels(), width, height, skia.rowBytes());
    list.replay(canvas);
  }
  const Target gl = drawWithGl(width, height,
                               [&](TCanvas &canvas) { list.replay(canvas); });

  const Difference d = compare(skia, gl);
  std::printf("%-22s mean %5.2f  over 32: %5.2f%%  over 96: %5.2f%%  "
              "largest %3d\n",
              name, d.mean, 100.0 * d.noticeable, 100.0 * d.gross, d.largest);

  // NEXTTOONZ_CANVAS_GL_DUMP=<directory> keeps both pictures (BGRA, bottom
  // row first).
  if (const char *directory = std::getenv("NEXTTOONZ_CANVAS_GL_DUMP")) {
    const auto keep = [&](const Target &target, const char *canvas) {
      const std::string path = std::string(directory) + "/" + name + canvas +
                               std::to_string(width) + "x" +
                               std::to_string(height) + ".bgra";
      if (std::FILE *file = std::fopen(path.c_str(), "wb")) {
        std::fwrite(target.bytes.data(), 1, target.bytes.size(), file);
        std::fclose(file);
      }
    };
    keep(skia, "_skia_");
    keep(gl, "_gl_");
  }
  return d;
}

// A paint with the half pixel of growth the OpenGL canvas always has.
TCanvasPaint grown(TCanvasPaint paint) {
  paint.growDevice = 0.5;
  return paint;
}

TCanvasPath blob(double cx, double cy, double r) {
  TCanvasPath path;
  path.moveTo({cx, cy - r}).quadTo({cx + r, cy - r}, {cx + r, cy});
  path.quadTo({cx + r, cy + r}, {cx, cy + r});
  path.quadTo({cx - r, cy + r}, {cx - r, cy});
  path.quadTo({cx - r, cy - r}, {cx, cy - r});
  return path.close();
}

// A stroke outline the way a stroke gives it: a strip along a curve.
TCanvasMesh ribbon(double halfWidth) {
  TCanvasMesh mesh;
  mesh.mode = TCanvasMesh::Mode::TriangleStrip;
  for (int i = 0; i <= 48; ++i) {
    const double t = i / 48.0;
    const double x = 20 + 200 * t, y = 60 + 40 * std::sin(6.0 * t);
    const double dx = 200, dy = 240 * std::cos(6.0 * t);
    const double n  = halfWidth / std::hypot(dx, dy);
    mesh.positions.push_back({x - dy * n, y + dx * n});
    mesh.positions.push_back({x + dy * n, y - dx * n});
  }
  return mesh;
}

}  // namespace

// Regions: the inside is the same; the outline is half a pixel of fringe
// on one side and half a pixel of growth on the other.
TEST(CanvasBackends, AgreeOnFilledRegions) {
  const Difference d = bothWays("regions", 240, 120, [](TCanvas &canvas) {
    const TCanvasPaint orange = grown(paintOf(0.9f, 0.6f, 0.1f));
    const TCanvasPaint blue   = grown(paintOf(0.2f, 0.5f, 0.8f, 0.7f));

    TCanvasPath left, right;
    left.moveTo({10, 10}).lineTo({110, 10}).quadTo({150, 60}, {110, 110});
    left.lineTo({10, 110}).close();
    right.moveTo({110, 10}).lineTo({230, 10}).lineTo({230, 110});
    right.lineTo({110, 110}).quadTo({150, 60}, {110, 10}).close();
    canvas.fillPath(left, TCanvasFillRule::NonZero, orange);
    canvas.fillPath(right, TCanvasFillRule::NonZero, blue);

    TCanvasPath ring = blob(60, 60, 30);
    const TCanvasPoint hole[4] = {{50, 50}, {50, 70}, {70, 70}, {70, 50}};
    ring.addPolygon(hole, 4);
    canvas.fillPath(ring, TCanvasFillRule::NonZero, blue);
  });
  EXPECT_LT(d.noticeable, 0.01);
  EXPECT_LT(d.gross, 0.002);
  EXPECT_LT(d.mean, 3.0);
}

TEST(CanvasBackends, AgreeOnStrokeOutlines) {
  for (float alpha : {1.f, 0.5f}) {
    const Difference d = bothWays(
        alpha == 1.f ? "outline_opaque" : "outline_translucent", 240, 120,
        [&](TCanvas &canvas) {
          canvas.drawMesh(ribbon(6.0),
                          grown(paintOf(0.1f, 0.1f, 0.1f, alpha)));
          canvas.save();
          canvas.concat(TCanvasAffine::translation(0, -35));
          canvas.drawMesh(ribbon(1.2), grown(paintOf(0.6f, 0.f, 0.2f, alpha)));
          canvas.restore();
        });
    EXPECT_LT(d.noticeable, 0.005);
    EXPECT_LT(d.gross, 0.002);
    EXPECT_LT(d.mean, 1.5);
  }
}

TEST(CanvasBackends, AgreeOnLines) {
  const Difference d = bothWays("lines", 240, 120, [](TCanvas &canvas) {
    canvas.concat(TCanvasAffine::scale(2, 2));
    TCanvasPath curve;
    curve.moveTo({10, 10}).quadTo({60, 70}, {110, 10});
    TCanvasPen one;
    canvas.strokePath(curve, one, paintOf(0.f, 0.f, 0.f));

    TCanvasPen three;
    three.width = 3.0;
    canvas.concat(TCanvasAffine::translation(0, 10));
    canvas.strokePath(curve, three, paintOf(0.f, 0.3f, 0.8f, 0.8f));

    TCanvasPen dashed;
    dashed.width = 2.0;
    dashed.dash  = {8, 8};
    TCanvasPath straight;
    straight.moveTo({10, 44.25}).lineTo({110, 44.25});
    canvas.strokePath(straight, dashed, paintOf(0.8f, 0.f, 0.f));
  });
  EXPECT_LT(d.noticeable, 0.025);
  EXPECT_LT(d.gross, 0.003);
  EXPECT_LT(d.mean, 2.0);
}

TEST(CanvasBackends, AgreeOnShadersAndImages) {
  const FourColours colours;
  const Difference d = bothWays("shaders", 240, 120, [&](TCanvas &canvas) {
    TCanvasPaint linear;
    linear.shader.kind  = TCanvasShader::Kind::LinearGradient;
    linear.shader.p0    = {10, 10};
    linear.shader.p1    = {70, 50};
    linear.shader.stops = {{0.0, TCanvasColor(1.f, 0.9f, 0.2f)},
                           {0.6, TCanvasColor(0.9f, 0.2f, 0.3f)},
                           {1.0, TCanvasColor(0.3f, 0.1f, 0.5f, 0.5f)}};
    canvas.fillPath(rectPath(10, 10, 70, 50), TCanvasFillRule::NonZero,
                    grown(linear));

    TCanvasPaint radial;
    radial.shader.kind   = TCanvasShader::Kind::RadialGradient;
    radial.shader.p0     = {120, 30};
    radial.shader.radius = 25;
    radial.shader.stops  = {{0.0, TCanvasColor(1.f, 1.f, 1.f)},
                            {1.0, TCanvasColor(0.f, 0.2f, 0.4f)}};
    canvas.fillPath(blob(120, 30, 22), TCanvasFillRule::NonZero,
                    grown(radial));

    TCanvasPaint tiles;
    tiles.color.a             = 0.8f;
    tiles.shader.kind         = TCanvasShader::Kind::Image;
    tiles.shader.image        = colours.image();
    tiles.shader.imageToLocal = TCanvasAffine::translation(160, 10) *
                                TCanvasAffine::scale(10, 10);
    tiles.shader.filter       = TCanvasFilter::Nearest;
    canvas.fillPath(rectPath(160, 10, 230, 50), TCanvasFillRule::NonZero,
                    grown(tiles));

    TCanvasImageOptions options;
    options.opacity = 0.75;
    canvas.drawImage(colours.image(),
                     TCanvasAffine::translation(30, 65) *
                         TCanvasAffine::rotation(15) *
                         TCanvasAffine::scale(20, 20),
                     options);

    TCanvasMesh fan;
    fan.mode      = TCanvasMesh::Mode::TriangleFan;
    fan.positions = {{160, 85}, {120, 65}, {200, 62},
                     {205, 110}, {125, 112}, {120, 65}};
    fan.colors    = {TCanvasColor(1.f, 1.f, 1.f),
                     TCanvasColor(0.f, 0.6f, 0.3f),
                     TCanvasColor(0.f, 0.3f, 0.6f),
                     TCanvasColor(0.5f, 0.f, 0.5f),
                     TCanvasColor(0.6f, 0.3f, 0.f),
                     TCanvasColor(0.f, 0.6f, 0.3f)};
    canvas.drawMesh(fan, paintOf(1.f, 1.f, 1.f, 0.8f));
  });
  EXPECT_LT(d.noticeable, 0.05);
  EXPECT_LT(d.gross, 0.025);
  EXPECT_LT(d.mean, 6.0);
}

// Brush tips: an image drawn far smaller than it is, tinted by the
// vertices' colour, sampled from its mipmaps on both canvases. Columns one
// pixel wide come out as the grey in between, wherever each pixel lands.
TEST(CanvasBackends, AgreeOnImagesShrunkWithMipmaps) {
  const int side = 64;
  std::vector<unsigned char> stripes((std::size_t)side * side * 4);
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x) {
      unsigned char *p = &stripes[((std::size_t)y * side + x) * 4];
      p[0] = p[1] = p[2] = (x % 2) ? 255 : 0;
      p[3]                = 255;
    }
  TCanvasImage image;
  image.pixels   = stripes.data();
  image.width    = side;
  image.height   = side;
  image.rowBytes = side * 4;

  const Difference d = bothWays("mipmaps", 60, 40, [&](TCanvas &canvas) {
    TCanvasPaint paint     = paintOf(1.f, 1.f, 1.f);
    paint.antialias        = false;
    paint.shader.kind      = TCanvasShader::Kind::Image;
    paint.shader.image     = image;
    paint.shader.tile      = TCanvasTile::Clamp;
    paint.shader.filter    = TCanvasFilter::Mipmap;
    paint.shader.imageToLocal =
        TCanvasAffine::scale(1.0 / side, 1.0 / side);

    // A square sixteen pixels across, and a long thin tip turned a little.
    TCanvasMesh square = rectMesh(4, 4, 20, 20);
    square.uvs         = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
    square.colors.assign(4, TCanvasColor(0.9f, 0.6f, 0.3f));
    square.smoothEdges = false;
    canvas.drawMesh(square, paint);

    TCanvasMesh tip;
    tip.mode      = TCanvasMesh::Mode::TriangleStrip;
    tip.positions = {{26, 10}, {28, 18}, {54, 4}, {56, 12}};
    tip.uvs       = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
    tip.colors.assign(4, TCanvasColor(0.3f, 0.6f, 0.9f));
    tip.smoothEdges = false;
    canvas.drawMesh(tip, paint);
  });
  EXPECT_LT(d.noticeable, 0.05);
  EXPECT_LT(d.gross, 0.025);
  EXPECT_LT(d.mean, 6.0);
}

TEST(CanvasBackends, AgreeOnClips) {
  const Difference d = bothWays("clips", 240, 120, [](TCanvas &canvas) {
    canvas.save();
    canvas.clipPath(blob(70, 60, 45), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Intersect, true);
    canvas.fillPath(rectPath(0, 0, 240, 120), TCanvasFillRule::NonZero,
                    grown(paintOf(0.2f, 0.6f, 0.3f)));
    canvas.save();
    canvas.clipPath(rectPath(60, 40, 100, 80), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Difference, true);
    canvas.fillPath(rectPath(40, 20, 240, 100), TCanvasFillRule::NonZero,
                    grown(paintOf(0.8f, 0.8f, 0.1f, 0.6f)));
    canvas.restore();
    canvas.restore();
    canvas.fillPath(rectPath(150, 30, 220, 90), TCanvasFillRule::NonZero,
                    grown(paintOf(0.5f, 0.1f, 0.6f)));
  });
  // An antialiased clip is the widest gap between the two: through OpenGL
  // it is a stencil mask that takes in every pixel the smooth line along
  // the path touches, so what is drawn inside reaches a pixel further out,
  // at full strength. That is the mask the old styles had.
  EXPECT_LT(d.noticeable, 0.035);
  EXPECT_LT(d.gross, 0.02);
  EXPECT_LT(d.mean, 6.5);
}

// What the styles that fill a region with a pattern are made of: hard-edged
// meshes that bring their colours, discs, smooth lines kept off a mesh, a
// line that fades along its length. Nothing here is antialiased on one
// canvas and not on the other, so this is the closest the two get.
TEST(CanvasBackends, AgreeOnPatternsInsideARegion) {
  const Difference d = bothWays("patterns", 240, 120, [](TCanvas &canvas) {
    canvas.save();
    canvas.clipPath(blob(120, 60, 110), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Intersect, false);

    TCanvasPaint plain = paintOf(1.f, 1.f, 1.f);
    plain.antialias    = false;

    // Stripes, with a smooth line along each edge on the outside.
    TCanvasMesh stripes;
    stripes.mode        = TCanvasMesh::Mode::Quads;
    stripes.smoothEdges = false;
    TCanvasPath edges;
    for (int i = 0; i < 6; ++i) {
      const double y = 6 + 20 * i;
      stripes.positions.insert(stripes.positions.end(),
                               {{0, y}, {0, y + 8}, {110, y + 38}, {110, y + 30}});
      edges.moveTo({0, y}).lineTo({110, y + 30});
      edges.moveTo({0, y + 8}).lineTo({110, y + 38});
    }
    stripes.colors.assign(stripes.positions.size(),
                          TCanvasColor(0.1f, 0.3f, 0.8f, 0.7f));
    canvas.drawMesh(stripes, plain);
    canvas.save();
    canvas.clipMesh(stripes, TCanvasClipOp::Difference);
    canvas.strokePath(edges, TCanvasPen(), paintOf(0.1f, 0.3f, 0.8f, 0.7f));
    canvas.restore();

    // Discs.
    TCanvasPath centres;
    for (int j = 0; j < 5; ++j)
      for (int i = 0; i < 5; ++i)
        centres.moveTo({125.0 + 14 * i + (j % 2) * 7, 12.0 + 12 * j});
    TCanvasPen disc;
    disc.width = 9.0;
    disc.unit  = TCanvasPen::Unit::Local;
    disc.cap   = TCanvasPen::Cap::Round;
    TCanvasPaint red = paintOf(0.8f, 0.1f, 0.1f);
    red.antialias    = false;
    canvas.strokePath(centres, disc, red);

    // A blend across a quad, and hatching that fades at both ends.
    TCanvasMesh blend;
    blend.mode        = TCanvasMesh::Mode::Quads;
    blend.smoothEdges = false;
    blend.positions   = {{120, 70}, {230, 70}, {230, 112}, {120, 112}};
    // From an opaque colour to a translucent one of another hue.
    blend.colors      = {TCanvasColor(1.f, 0.9f, 0.2f), TCanvasColor(1.f, 0.9f, 0.2f),
                         TCanvasColor(0.2f, 0.3f, 0.9f, 0.3f), TCanvasColor(0.2f, 0.3f, 0.9f, 0.3f)};
    canvas.drawMesh(blend, plain);

    TCanvasPath hatching;
    for (int i = 0; i < 26; ++i) {
      const double x = 124 + 4 * i;
      hatching.moveTo({x, 72}).lineTo({x + 6, 90}).lineTo({x + 12, 108});
    }
    TCanvasPaint fading  = paintOf(1.f, 1.f, 1.f);
    fading.shader.kind   = TCanvasShader::Kind::LinearGradient;
    fading.shader.p0     = {0, 72};
    fading.shader.p1     = {0, 108};
    fading.shader.stops  = {{0.0, TCanvasColor(0.f, 0.f, 0.f, 0.f)},
                            {0.5, TCanvasColor(0.f, 0.f, 0.f, 1.f)},
                            {1.0, TCanvasColor(0.f, 0.f, 0.f, 0.f)}};
    canvas.strokePath(hatching, TCanvasPen(), fading);

    canvas.restore();
  });
  EXPECT_LT(d.noticeable, 0.005);
  EXPECT_LT(d.gross, 0.005);
  EXPECT_LT(d.mean, 1.5);
}

// The picture the Skia tests hash, four times as large. It asks for things
// the OpenGL canvas does not have: round caps and joins on a wide line (and
// where that line's pieces overlap, OpenGL blends them twice), and a mesh
// with a translucent vertex among opaque ones.
TEST(CanvasBackends, AgreeOnAPictureOfEverything) {
  const FourColours colours;
  const Difference d = bothWays("everything", 256, 192, [&](TCanvas &canvas) {
    canvas.concat(TCanvasAffine::scale(4, 4));
    drawScene(canvas, colours);
  });
  EXPECT_LT(d.noticeable, 0.07);
  EXPECT_LT(d.gross, 0.035);
  EXPECT_LT(d.mean, 9.0);
}

#endif  // NEXTTOONZ_HAVE_SKIA
