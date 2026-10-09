// Unit tests for TRaster / TPixel32 and a few TRop operations.
//
// TRop is the CPU raster-ops library that every raster effect and the
// compositing path are built on. It is pure C++ and must stay bit-exact
// across platforms, so a handful of exact-value tests guard it.

#include <gtest/gtest.h>

#include "traster.h"
#include "trop.h"
#include "tpixel.h"

TEST(TPixel32, Constants) {
  EXPECT_EQ(TPixel32::Red, TPixel32(255, 0, 0, 255));
  EXPECT_EQ(TPixel32::Transparent, TPixel32(0, 0, 0, 0));
  EXPECT_EQ(TPixel32::White.m, 255);
}

TEST(TRaster32, FillAndPixelAccess) {
  TRaster32P ras(8, 6);
  ASSERT_TRUE(ras);
  EXPECT_EQ(ras->getLx(), 8);
  EXPECT_EQ(ras->getLy(), 6);

  const TPixel32 color(10, 20, 30, 255);
  ras->fill(color);
  EXPECT_EQ(ras->pixels(0)[0], color);
  EXPECT_EQ(ras->pixels(5)[7], color);

  ras->pixels(2)[3] = TPixel32::Blue;
  EXPECT_EQ(ras->pixels(2)[3], TPixel32::Blue);
  EXPECT_EQ(ras->pixels(2)[4], color);
}

TEST(TRaster32, ExtractSharesPixels) {
  TRaster32P ras(10, 10);
  ras->fill(TPixel32::Transparent);

  TRaster32P sub = ras->extract(2, 3, 5, 6);
  ASSERT_TRUE(sub);
  EXPECT_EQ(sub->getLx(), 4);
  EXPECT_EQ(sub->getLy(), 4);

  sub->fill(TPixel32::Green);
  EXPECT_EQ(ras->pixels(3)[2], TPixel32::Green);
  EXPECT_EQ(ras->pixels(6)[5], TPixel32::Green);
  EXPECT_EQ(ras->pixels(2)[2], TPixel32::Transparent);
  EXPECT_EQ(ras->pixels(3)[1], TPixel32::Transparent);
}

TEST(TRop, CopyIsExact) {
  TRaster32P src(4, 4), dst(4, 4);
  src->fill(TPixel32(1, 2, 3, 4));
  dst->fill(TPixel32::White);
  TRop::copy(dst, src);
  EXPECT_EQ(dst->pixels(0)[0], TPixel32(1, 2, 3, 4));
  EXPECT_EQ(dst->pixels(3)[3], TPixel32(1, 2, 3, 4));
}

TEST(TRop, OverComposesPremultiplied) {
  // Pixels are premultiplied: a 50% blue over opaque red yields a mix.
  TRaster32P up(4, 4), down(4, 4);
  down->fill(TPixel32(255, 0, 0, 255));
  up->fill(TPixel32(0, 0, 128, 128));

  TRop::over(down, up);

  const TPixel32 &p = down->pixels(1)[1];
  EXPECT_NEAR(p.r, 127, 1);
  EXPECT_EQ(p.g, 0);
  EXPECT_NEAR(p.b, 128, 1);
  EXPECT_EQ(p.m, 255);
}

TEST(TRop, OverWithTransparentUpIsNoOp) {
  TRaster32P up(4, 4), down(4, 4);
  down->fill(TPixel32(10, 20, 30, 255));
  up->fill(TPixel32::Transparent);
  TRop::over(down, up);
  EXPECT_EQ(down->pixels(2)[2], TPixel32(10, 20, 30, 255));
}
