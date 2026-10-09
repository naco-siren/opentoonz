// Unit tests for the geometry primitives in tnzcore (include/tgeometry.h).
//
// These classes are the foundation of every transform in the scene model and
// the renderer, so they are the first thing pinned down before any refactor.

#include <gtest/gtest.h>

#include "tgeometry.h"

#include <cmath>

namespace {

constexpr double kEps = 1e-9;

void expectPointNear(const TPointD &a, const TPointD &b, double eps = kEps) {
  EXPECT_NEAR(a.x, b.x, eps);
  EXPECT_NEAR(a.y, b.y, eps);
}

void expectAffineNear(const TAffine &a, const TAffine &b, double eps = kEps) {
  EXPECT_NEAR(a.a11, b.a11, eps);
  EXPECT_NEAR(a.a12, b.a12, eps);
  EXPECT_NEAR(a.a13, b.a13, eps);
  EXPECT_NEAR(a.a21, b.a21, eps);
  EXPECT_NEAR(a.a22, b.a22, eps);
  EXPECT_NEAR(a.a23, b.a23, eps);
}

}  // namespace

//-----------------------------------------------------------------------------
// TPointD

TEST(TPointD, ArithmeticAndNorm) {
  TPointD a(3.0, 4.0);
  TPointD b(1.0, -2.0);

  expectPointNear(a + b, TPointD(4.0, 2.0));
  expectPointNear(a - b, TPointD(2.0, 6.0));
  expectPointNear(a * 2.0, TPointD(6.0, 8.0));
  EXPECT_NEAR(norm(a), 5.0, kEps);
  EXPECT_NEAR(norm2(a), 25.0, kEps);
  EXPECT_NEAR(a * b, 3.0 - 8.0, kEps);  // dot product
}

TEST(TPointD, Equality) {
  EXPECT_TRUE(TPointD(1.5, 2.5) == TPointD(1.5, 2.5));
  EXPECT_TRUE(TPointD(1.5, 2.5) != TPointD(2.5, 1.5));
}

//-----------------------------------------------------------------------------
// TRectD

TEST(TRectD, UnionAndIntersection) {
  TRectD r1(0.0, 0.0, 10.0, 10.0);
  TRectD r2(5.0, 5.0, 20.0, 20.0);

  TRectD u = r1 + r2;
  EXPECT_NEAR(u.x0, 0.0, kEps);
  EXPECT_NEAR(u.y0, 0.0, kEps);
  EXPECT_NEAR(u.x1, 20.0, kEps);
  EXPECT_NEAR(u.y1, 20.0, kEps);

  TRectD i = r1 * r2;
  EXPECT_NEAR(i.x0, 5.0, kEps);
  EXPECT_NEAR(i.y0, 5.0, kEps);
  EXPECT_NEAR(i.x1, 10.0, kEps);
  EXPECT_NEAR(i.y1, 10.0, kEps);
}

TEST(TRectD, DisjointIntersectionIsEmpty) {
  TRectD r1(0.0, 0.0, 1.0, 1.0);
  TRectD r2(5.0, 5.0, 6.0, 6.0);
  EXPECT_TRUE((r1 * r2).isEmpty());
  EXPECT_FALSE(r1.isEmpty());
}

TEST(TRectD, Contains) {
  TRectD r(0.0, 0.0, 10.0, 10.0);
  EXPECT_TRUE(r.contains(TPointD(5.0, 5.0)));
  EXPECT_FALSE(r.contains(TPointD(11.0, 5.0)));
}

TEST(TRectI, IntegerExtents) {
  // Integer rects are inclusive: a rect from 0 to 9 is 10 pixels wide.
  TRect r(0, 0, 9, 9);
  EXPECT_EQ(r.getLx(), 10);
  EXPECT_EQ(r.getLy(), 10);
}

//-----------------------------------------------------------------------------
// TAffine

TEST(TAffine, DefaultIsIdentity) {
  TAffine id;
  EXPECT_TRUE(id.isIdentity());
  expectPointNear(id * TPointD(3.0, -7.0), TPointD(3.0, -7.0));
  EXPECT_NEAR(id.det(), 1.0, kEps);
}

TEST(TAffine, TranslationScaleRotation) {
  expectPointNear(TTranslation(2.0, 3.0) * TPointD(1.0, 1.0),
                  TPointD(3.0, 4.0));
  expectPointNear(TScale(2.0, 3.0) * TPointD(1.0, 1.0), TPointD(2.0, 3.0));
  // TRotation takes degrees, counter-clockwise.
  expectPointNear(TRotation(90.0) * TPointD(1.0, 0.0), TPointD(0.0, 1.0));
  expectPointNear(TRotation(180.0) * TPointD(1.0, 0.0), TPointD(-1.0, 0.0));
}

TEST(TAffine, CompositionOrder) {
  // (A * B) * p == A * (B * p): B is applied first.
  TAffine a = TTranslation(10.0, 0.0);
  TAffine b = TScale(2.0);
  TPointD p(1.0, 1.0);
  expectPointNear((a * b) * p, a * (b * p));
  expectPointNear((a * b) * p, TPointD(12.0, 2.0));
  expectPointNear((b * a) * p, TPointD(22.0, 2.0));
}

TEST(TAffine, InverseRoundTrip) {
  TAffine m = TTranslation(5.0, -3.0) * TRotation(37.0) * TScale(2.0, 0.5) *
              TShear(0.3, 0.0);
  EXPECT_FALSE(m.isIdentity());
  expectAffineNear(m * m.inv(), TAffine());
  expectAffineNear(m.inv() * m, TAffine());

  TPointD p(1.25, -4.5);
  expectPointNear(m.inv() * (m * p), p);
}

TEST(TAffine, Determinant) {
  EXPECT_NEAR(TScale(2.0, 3.0).det(), 6.0, kEps);
  EXPECT_NEAR(TRotation(45.0).det(), 1.0, kEps);
  EXPECT_NEAR((TScale(2.0, 3.0) * TRotation(45.0)).det(), 6.0, kEps);
}

TEST(TAffine, RectTransformIsBoundingBox) {
  TRectD r(0.0, 0.0, 2.0, 2.0);
  TRectD rotated = TRotation(45.0) * r;
  // The bounding box of a 2x2 square rotated by 45 degrees around the
  // origin has half-diagonal sqrt(2) on each side.
  const double d = std::sqrt(2.0);
  EXPECT_NEAR(rotated.x0, -d, kEps);
  EXPECT_NEAR(rotated.x1, d, kEps);
  EXPECT_NEAR(rotated.y0, 0.0, kEps);
  EXPECT_NEAR(rotated.y1, 2.0 * d, kEps);
}

TEST(TAffine, Place) {
  // place(pIn, pOut) returns the affine that maps pIn to pOut while keeping
  // the linear part of *this.
  TAffine m      = TScale(2.0);
  TAffine placed = m.place(TPointD(1.0, 1.0), TPointD(10.0, 10.0));
  expectPointNear(placed * TPointD(1.0, 1.0), TPointD(10.0, 10.0));
  EXPECT_NEAR(placed.a11, 2.0, kEps);
  EXPECT_NEAR(placed.a22, 2.0, kEps);
}
