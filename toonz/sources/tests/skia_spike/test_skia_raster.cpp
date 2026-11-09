// Phase 1 spike (doc/nexttoonz_phase1_skia.md): proves that a CPU-only Skia
// obtained through vcpkg links into the tree and rasterises paths into
// memory this codebase owns. Nothing in the render path uses it yet.
//
// The checks are on pixel values rather than on a stored image so that they
// say what is expected: exact colours inside and outside a shape, and the
// coverage value of an edge that cuts a pixel in half.

#include <gtest/gtest.h>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMilestone.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"
#include "include/core/SkVertices.h"

#include "traster.h"
#include "tpixel.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

const int kSize = 64;

// BGRA is spelled out because Skia's "native" 32-bit format is not the same
// everywhere: RGBA on macOS, BGRA on Linux. TPixel32 is BGRA on both.
sk_sp<SkSurface> makeSurface() {
  sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::Make(
      kSize, kSize, kBGRA_8888_SkColorType, kPremul_SkAlphaType));
  if (surface) surface->getCanvas()->clear(SK_ColorTRANSPARENT);
  return surface;
}

SkColor pixelAt(SkSurface *surface, int x, int y) {
  SkPixmap pixmap;
  if (!surface->peekPixels(&pixmap)) return 0xDEADBEEF;
  return pixmap.getColor(x, y);  // unpremultiplied ARGB
}

// A square with a square hole, as one path with two contours.
SkPath squareWithHole() {
  SkPathBuilder builder(SkPathFillType::kEvenOdd);
  builder.addRect(SkRect::MakeLTRB(8, 8, 56, 56));
  builder.addRect(SkRect::MakeLTRB(24, 24, 40, 40));
  return builder.detach();
}

void drawScene(SkCanvas *canvas) {
  SkPaint fill;
  fill.setAntiAlias(true);
  fill.setColor(SK_ColorRED);
  canvas->drawPath(squareWithHole(), fill);

  SkPaint stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(SkPaint::kStroke_Style);
  stroke.setStrokeWidth(3);
  stroke.setStrokeJoin(SkPaint::kRound_Join);
  stroke.setStrokeCap(SkPaint::kRound_Cap);
  stroke.setColor(SkColorSetARGB(128, 0, 0, 255));
  SkPathBuilder curve;
  curve.moveTo(4, 60).quadTo(32, -20, 60, 60).cubicTo(50, 30, 14, 30, 4, 60);
  canvas->drawPath(curve.detach(), stroke);
}

std::uint64_t fnv1a(const SkPixmap &pixmap) {
  std::uint64_t hash = 14695981039346656037ull;
  for (int y = 0; y < pixmap.height(); ++y) {
    const std::uint8_t *row =
        static_cast<const std::uint8_t *>(pixmap.addr(0, y));
    for (int i = 0; i < pixmap.width() * 4; ++i) {
      hash ^= row[i];
      hash *= 1099511628211ull;
    }
  }
  return hash;
}

}  // namespace

//-----------------------------------------------------------------------------

TEST(SkiaRaster, FilledPathCoversItsInteriorOnly) {
  sk_sp<SkSurface> surface = makeSurface();
  ASSERT_TRUE(surface);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(SK_ColorRED);
  surface->getCanvas()->drawPath(squareWithHole(), paint);

  EXPECT_EQ(pixelAt(surface.get(), 16, 16), SK_ColorRED) << "inside the ring";
  EXPECT_EQ(pixelAt(surface.get(), 8, 8), SK_ColorRED) << "first covered pixel";
  EXPECT_EQ(pixelAt(surface.get(), 55, 55), SK_ColorRED)
      << "last covered pixel";
  EXPECT_EQ(pixelAt(surface.get(), 32, 32), SK_ColorTRANSPARENT) << "the hole";
  EXPECT_EQ(pixelAt(surface.get(), 7, 30), SK_ColorTRANSPARENT) << "outside";
  EXPECT_EQ(pixelAt(surface.get(), 56, 30), SK_ColorTRANSPARENT) << "outside";
}

//-----------------------------------------------------------------------------

// An edge through the middle of a pixel column must give half coverage: this
// is the analytic antialiasing Phase 1 wants in place of GL_LINE_SMOOTH.
TEST(SkiaRaster, FillAntialiasingIsAreaCoverage) {
  sk_sp<SkSurface> surface = makeSurface();
  ASSERT_TRUE(surface);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(SK_ColorBLACK);
  SkPathBuilder builder;
  builder.moveTo(10.5f, 10).lineTo(30, 10).lineTo(30, 30).lineTo(10.5f, 30);
  builder.close();
  surface->getCanvas()->drawPath(builder.detach(), paint);

  EXPECT_EQ(SkColorGetA(pixelAt(surface.get(), 9, 20)), 0u);
  EXPECT_NEAR(SkColorGetA(pixelAt(surface.get(), 10, 20)), 128, 1);
  EXPECT_EQ(SkColorGetA(pixelAt(surface.get(), 11, 20)), 255u);

  // Without antialiasing the same edge is all or nothing.
  sk_sp<SkSurface> aliased = makeSurface();
  ASSERT_TRUE(aliased);
  paint.setAntiAlias(false);
  SkPathBuilder again;
  again.moveTo(10.5f, 10).lineTo(30, 10).lineTo(30, 30).lineTo(10.5f, 30);
  again.close();
  aliased->getCanvas()->drawPath(again.detach(), paint);
  for (int x = 8; x < 13; ++x) {
    unsigned alpha = SkColorGetA(pixelAt(aliased.get(), x, 20));
    EXPECT_TRUE(alpha == 0u || alpha == 255u) << "x=" << x << " a=" << alpha;
  }
}

//-----------------------------------------------------------------------------

TEST(SkiaRaster, StrokedPathCoversTheOutlineNotTheInterior) {
  sk_sp<SkSurface> surface = makeSurface();
  ASSERT_TRUE(surface);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(8);
  paint.setStrokeCap(SkPaint::kRound_Cap);
  paint.setColor(SK_ColorBLUE);

  // An open segment: 8 wide, so rows 28..35 are covered between the ends.
  SkPathBuilder segment;
  segment.moveTo(16, 32).lineTo(48, 32);
  surface->getCanvas()->drawPath(segment.detach(), paint);

  EXPECT_EQ(pixelAt(surface.get(), 32, 32), SK_ColorBLUE);
  EXPECT_EQ(pixelAt(surface.get(), 32, 28), SK_ColorBLUE);
  EXPECT_EQ(pixelAt(surface.get(), 32, 35), SK_ColorBLUE);
  EXPECT_EQ(pixelAt(surface.get(), 32, 27), SK_ColorTRANSPARENT);
  EXPECT_EQ(pixelAt(surface.get(), 32, 36), SK_ColorTRANSPARENT);
  // The round cap reaches 4 units past the end point and no further.
  EXPECT_GE(SkColorGetA(pixelAt(surface.get(), 13, 32)), 250u);
  EXPECT_EQ(pixelAt(surface.get(), 10, 32), SK_ColorTRANSPARENT);

  // A closed contour, stroked: the ring is painted, the inside is not.
  sk_sp<SkSurface> ring = makeSurface();
  ASSERT_TRUE(ring);
  paint.setStrokeWidth(4);
  SkPathBuilder circle;
  circle.addCircle(32, 32, 20);
  ring->getCanvas()->drawPath(circle.detach(), paint);

  EXPECT_EQ(pixelAt(ring.get(), 32, 32), SK_ColorTRANSPARENT) << "centre";
  EXPECT_EQ(pixelAt(ring.get(), 32, 20), SK_ColorTRANSPARENT) << "inside";
  EXPECT_EQ(pixelAt(ring.get(), 51, 32), SK_ColorBLUE) << "on the ring";
  EXPECT_EQ(pixelAt(ring.get(), 32, 12), SK_ColorBLUE) << "on the ring";
  EXPECT_EQ(pixelAt(ring.get(), 58, 32), SK_ColorTRANSPARENT) << "outside";
}

//-----------------------------------------------------------------------------

// Stroke outlines reach the renderer as quad strips, and a translucent
// stroke must not get darker where its quads abut or where the stroke
// crosses itself (the GL code uses the stencil buffer for that). Put into
// one path with every piece wound the same way and filled non-zero, the
// pieces behave as their union: no seam on shared edges that cut through a
// pixel, no double blending in overlaps. A piece wound the other way cancels
// what it overlaps, which is why the pieces have to be oriented first.
TEST(SkiaRaster, PiecesOfOnePathFillAsTheirUnion) {
  const SkPoint left[4]  = {{8.3f, 10.2f}, {20.7f, 12.4f}, {33.1f, 10.9f},
                            {45.6f, 13.3f}};
  const SkPoint right[4] = {{8.3f, 20.2f}, {20.7f, 22.4f}, {33.1f, 20.9f},
                            {45.6f, 23.3f}};

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(SkColorSetARGB(128, 0, 0, 0));

  SkPathBuilder ribbon(SkPathFillType::kWinding);
  for (int i = 0; i < 3; ++i)
    ribbon.moveTo(left[i])
        .lineTo(left[i + 1])
        .lineTo(right[i + 1])
        .lineTo(right[i])
        .close();
  // One more piece on top of the middle of the ribbon, wound like the rest.
  ribbon.moveTo(15, 14).lineTo(28, 14).lineTo(28, 19).lineTo(15, 19).close();

  sk_sp<SkSurface> surface = makeSurface();
  ASSERT_TRUE(surface);
  surface->getCanvas()->drawPath(ribbon.detach(), paint);
  for (int x = 9; x < 45; ++x)
    EXPECT_EQ(SkColorGetA(pixelAt(surface.get(), x, 16)), 128u) << "x=" << x;

  // The same extra piece wound the other way punches a hole instead.
  SkPathBuilder cancelled(SkPathFillType::kWinding);
  cancelled.moveTo(left[0]).lineTo(left[1]).lineTo(right[1]).lineTo(right[0]);
  cancelled.close();
  cancelled.moveTo(12, 14).lineTo(12, 19).lineTo(18, 19).lineTo(18, 14).close();
  sk_sp<SkSurface> holed = makeSurface();
  ASSERT_TRUE(holed);
  holed->getCanvas()->drawPath(cancelled.detach(), paint);
  EXPECT_EQ(SkColorGetA(pixelAt(holed.get(), 15, 16)), 0u);
  EXPECT_EQ(SkColorGetA(pixelAt(holed.get(), 10, 16)), 128u);
}

//-----------------------------------------------------------------------------

// Two regions that share a boundary are two draw calls. With coverage
// antialiasing each one covers half of the boundary pixels and the second
// is blended over the first, so a quarter of the background shows through:
// a seam. The GL code never had it, because it fills without antialiasing
// and then draws a smooth one-pixel line over the boundary. Growing the
// first shape by half a pixel (fill and hairline stroke in one coverage
// pass) closes the seam the same way; the canvas IR draft calls this "grow".
TEST(SkiaRaster, AbuttingShapesLeaveASeamUnlessTheFirstIsGrown) {
  const SkRect leftRect  = SkRect::MakeLTRB(10, 10, 20.5f, 30);
  const SkRect rightRect = SkRect::MakeLTRB(20.5f, 10, 30, 30);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(SK_ColorBLACK);

  sk_sp<SkSurface> plain = makeSurface();
  ASSERT_TRUE(plain);
  plain->getCanvas()->drawPath(SkPathBuilder().addRect(leftRect).detach(),
                               paint);
  plain->getCanvas()->drawPath(SkPathBuilder().addRect(rightRect).detach(),
                               paint);
  EXPECT_EQ(SkColorGetA(pixelAt(plain.get(), 19, 20)), 255u);
  EXPECT_NEAR(SkColorGetA(pixelAt(plain.get(), 20, 20)), 191, 2) << "seam";
  EXPECT_EQ(SkColorGetA(pixelAt(plain.get(), 21, 20)), 255u);

  sk_sp<SkSurface> grown = makeSurface();
  ASSERT_TRUE(grown);
  SkPaint growPaint = paint;
  growPaint.setStyle(SkPaint::kStrokeAndFill_Style);
  growPaint.setStrokeWidth(1);  // half a pixel on each side of the boundary
  growPaint.setStrokeJoin(SkPaint::kRound_Join);
  grown->getCanvas()->drawPath(SkPathBuilder().addRect(leftRect).detach(),
                               growPaint);
  grown->getCanvas()->drawPath(SkPathBuilder().addRect(rightRect).detach(),
                               paint);
  for (int x = 12; x < 29; ++x)
    EXPECT_EQ(SkColorGetA(pixelAt(grown.get(), x, 20)), 255u) << "x=" << x;
}

//-----------------------------------------------------------------------------

// Many colour styles draw triangle strips with a colour per vertex. Skia's
// CPU backend interpolates the colours but does not antialias the edges of a
// mesh, whatever the paint says. The canvas IR draft relies on this fact: a
// style that needs smooth edges has to be expressed as a path with a
// gradient, not as a mesh.
TEST(SkiaRaster, MeshesInterpolateColoursButAreNotAntialiased) {
  sk_sp<SkSurface> surface = makeSurface();
  ASSERT_TRUE(surface);

  const SkPoint positions[4] = {{10.5f, 10}, {10.5f, 30}, {42.5f, 10},
                                {42.5f, 30}};
  const SkColor colors[4]    = {SK_ColorBLACK, SK_ColorBLACK, SK_ColorWHITE,
                                SK_ColorWHITE};
  sk_sp<SkVertices> strip    = SkVertices::MakeCopy(
      SkVertices::kTriangleStrip_VertexMode, 4, positions, nullptr, colors);
  SkPaint paint;
  paint.setAntiAlias(true);
  surface->getCanvas()->drawVertices(strip, SkBlendMode::kDst, paint);

  // The left edge cuts column 10 in half: a path gives alpha 128 there (see
  // FillAntialiasingIsAreaCoverage), a mesh gives all or nothing.
  unsigned edge = SkColorGetA(pixelAt(surface.get(), 10, 20));
  EXPECT_TRUE(edge == 0u || edge == 255u) << "alpha=" << edge;

  // Colours run from black to white across 32 pixels.
  EXPECT_LT(SkColorGetR(pixelAt(surface.get(), 12, 20)), 24u);
  EXPECT_NEAR(SkColorGetR(pixelAt(surface.get(), 26, 20)), 128, 8);
  EXPECT_GT(SkColorGetR(pixelAt(surface.get(), 40, 20)), 232u);
  EXPECT_EQ(SkColorGetA(pixelAt(surface.get(), 26, 20)), 255u);
}

//-----------------------------------------------------------------------------

// Skia can draw straight into a TRaster32P: same channel order on the
// platforms we build and premultiplied alpha on both sides. No flip is
// needed either: TRaster stores its bottom row first, Skia addresses the
// first row in memory as y = 0, so Skia's device y is TRaster's y (up) as it
// is. A Skia backend therefore needs no copy and no conversion to hand a
// frame to the fx tree. (Only things with an intrinsic "up", text and images
// that are not themselves TRasters, would come out upside down.)
TEST(SkiaRaster, DrawsIntoTRaster32WithoutACopy) {
#ifndef TNZ_MACHINE_CHANNEL_ORDER_BGRM
  GTEST_SKIP() << "TPixel32 is not BGRM on this platform";
#else
  TRaster32P raster(kSize, kSize);
  raster->clear();
  raster->lock();

  SkImageInfo info = SkImageInfo::Make(kSize, kSize, kBGRA_8888_SkColorType,
                                       kPremul_SkAlphaType);
  sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(
      info, raster->getRawData(), raster->getWrap() * sizeof(TPixel32));
  ASSERT_TRUE(surface);

  SkCanvas *canvas = surface->getCanvas();

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(SkColorSetARGB(128, 255, 0, 0));
  SkPathBuilder builder;
  builder.addRect(SkRect::MakeLTRB(4, 4, 20, 12));  // near the bottom-left
  canvas->drawPath(builder.detach(), paint);
  surface.reset();
  raster->unlock();

  TPixel32 inside = raster->pixels(8)[10];  // y = 8 counted from the bottom
  EXPECT_EQ(inside.m, 128);
  EXPECT_EQ(inside.r, 128) << "premultiplied: 255 * 128 / 255";
  EXPECT_EQ(inside.g, 0);
  EXPECT_EQ(inside.b, 0);

  TPixel32 mirrored = raster->pixels(kSize - 1 - 8)[10];
  EXPECT_EQ(mirrored.m, 0) << "nothing near the top-left";

  // Half coverage on the lower edge of a second rectangle, to pin the
  // direction of y down with an antialiased edge as well.
  EXPECT_EQ(raster->pixels(3)[10].m, 0);
  EXPECT_EQ(raster->pixels(4)[10].m, 128);
#endif
}

//-----------------------------------------------------------------------------

// Two renders of the same scene in one process must be identical. The hash
// is printed so that runs on other platforms can be compared by hand; it is
// not asserted, because it is not the same everywhere. Measured on x86-64
// with milestone 148: Ubuntu 24.04 (Skia built by GCC 13) gives
// 22f238120169907e and macOS (Apple clang) gives 5653731d3fe1721d. The two
// images differ by 1 in one channel of 5 pixels out of 4096, all where the
// translucent stroke crosses the fill. BGRA is Skia's native format on Linux
// but not on macOS, where it takes a different blending path; rendered into
// each platform's native format the two images are identical. arm64 has not
// been run.
TEST(SkiaRaster, RenderingIsRepeatable) {
  sk_sp<SkSurface> first = makeSurface(), second = makeSurface();
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  drawScene(first->getCanvas());
  drawScene(second->getCanvas());

  SkPixmap a, b;
  ASSERT_TRUE(first->peekPixels(&a));
  ASSERT_TRUE(second->peekPixels(&b));
  const std::uint64_t hashA = fnv1a(a), hashB = fnv1a(b);
  EXPECT_EQ(hashA, hashB);

  // NEXTTOONZ_SKIA_SPIKE_DUMP=<file> writes the raw pixels (64 x 64, four
  // bytes each, premultiplied BGRA) for a closer comparison than the hash
  // allows.
  if (const char *dumpPath = std::getenv("NEXTTOONZ_SKIA_SPIKE_DUMP")) {
    if (FILE *file = std::fopen(dumpPath, "wb")) {
      for (int y = 0; y < a.height(); ++y)
        std::fwrite(a.addr(0, y), 4, a.width(), file);
      std::fclose(file);
    }
  }

  char text[32];
  std::snprintf(text, sizeof(text), "%016llx",
                static_cast<unsigned long long>(hashA));
  RecordProperty("skia_milestone", SK_MILESTONE);
  RecordProperty("pixel_hash", text);
  std::printf("Skia m%d scene hash: %s\n", SK_MILESTONE, text);
}
