// Unit tests for TStroke, TVectorImage and TRegion (tnzcore,
// common/tvectorimage/).
//
// These are the geometry behind every .pli level: strokes are chains of
// quadratic chunks with a thickness per control point, and regions are
// computed from stroke intersections. Phase 1 replaces how they are drawn,
// not what they are, so their geometry is pinned down here. Nothing below
// needs OpenGL: no tglDraw, no TOfflineGL, no render().

#include <gtest/gtest.h>

#include "tgeometry.h"
#include "tpalette.h"
#include "tregion.h"
#include "tstroke.h"
#include "tvectorimage.h"

#include <cmath>
#include <memory>
#include <vector>

namespace {

constexpr double kEps = 1e-9;
const double kPi      = std::acos(-1.0);

void expectPointNear(const TPointD &a, const TPointD &b, double eps = kEps) {
  EXPECT_NEAR(a.x, b.x, eps);
  EXPECT_NEAR(a.y, b.y, eps);
}

// Straight stroke from a to b made of `chunks` quadratic chunks with
// evenly spaced control points.
TStroke *makeLine(const TPointD &a, const TPointD &b, double thick,
                  int chunks = 1) {
  std::vector<TThickPoint> pts;
  const int n = 2 * chunks;
  for (int i = 0; i <= n; ++i) {
    const double t = double(i) / n;
    pts.push_back(TThickPoint(a + t * (b - a), thick));
  }
  return new TStroke(pts);
}

// Closed circle made of 8 quadratic chunks (same construction as the
// reference-project generator, tests/fixtures/gen_reference_project.cpp).
TStroke *makeCircle(const TPointD &c, double r, double thick) {
  std::vector<TThickPoint> pts;
  const int n       = 8;
  const double step = 2.0 * kPi / n;
  const double cr   = r / std::cos(step / 2.0);  // control-point radius
  for (int i = 0; i < n; ++i) {
    const double a  = i * step;
    const double am = (i + 0.5) * step;
    pts.push_back(
        TThickPoint(c.x + r * std::cos(a), c.y + r * std::sin(a), thick));
    pts.push_back(
        TThickPoint(c.x + cr * std::cos(am), c.y + cr * std::sin(am), thick));
  }
  pts.push_back(pts[0]);
  TStroke *s = new TStroke(pts);
  s->setSelfLoop(true);
  return s;
}

}  // namespace

//-----------------------------------------------------------------------------
// TStroke

TEST(TStroke, SingleChunkLine) {
  std::unique_ptr<TStroke> s(makeLine(TPointD(0, 0), TPointD(100, 0), 2.0));
  EXPECT_EQ(s->getChunkCount(), 1);
  EXPECT_EQ(s->getControlPointCount(), 3);
  EXPECT_FALSE(s->isSelfLoop());
  EXPECT_NEAR(s->getLength(), 100.0, kEps);
  EXPECT_NEAR(s->getLength(0.0, 0.5), 50.0, kEps);

  expectPointNear(s->getPoint(0.0), TPointD(0, 0));
  expectPointNear(s->getPoint(0.5), TPointD(50, 0));
  expectPointNear(s->getPoint(1.0), TPointD(100, 0));
  EXPECT_NEAR(s->getThickPoint(0.5).thick, 2.0, kEps);

  // Parameters outside [0, 1] clamp to the end points.
  expectPointNear(s->getPoint(-1.0), TPointD(0, 0));
  expectPointNear(s->getPoint(2.0), TPointD(100, 0));

  const TRectD cl = s->getCenterlineBBox();
  EXPECT_NEAR(cl.x0, 0.0, kEps);
  EXPECT_NEAR(cl.x1, 100.0, kEps);
  EXPECT_NEAR(cl.y0, 0.0, kEps);
  EXPECT_NEAR(cl.y1, 0.0, kEps);
}

TEST(TStroke, BBoxIncludesThickness) {
  // getBBox() is the bounding box of the outline, which is offset from the
  // centerline by `thick` on each side: TThickPoint::thick is a half width,
  // so a stroke of thickness 2 is 4 units wide. getCenterlineBBox() ignores
  // the thickness.
  std::unique_ptr<TStroke> s(makeLine(TPointD(0, 0), TPointD(100, 0), 2.0));
  const TRectD bb = s->getBBox();
  EXPECT_NEAR(bb.x0, -2.0, 1e-6);
  EXPECT_NEAR(bb.x1, 102.0, 1e-6);
  EXPECT_NEAR(bb.y0, -2.0, 1e-6);
  EXPECT_NEAR(bb.y1, 2.0, 1e-6);
}

TEST(TStroke, MultiChunkParametrization) {
  std::unique_ptr<TStroke> s(makeLine(TPointD(0, 0), TPointD(0, 100), 1.0, 4));
  EXPECT_EQ(s->getChunkCount(), 4);
  EXPECT_EQ(s->getControlPointCount(), 9);
  EXPECT_NEAR(s->getLength(), 100.0, kEps);

  // With evenly spaced chunks w, length and control points line up.
  expectPointNear(s->getPoint(0.5), TPointD(0, 50));
  expectPointNear(s->getPoint(0.25), TPointD(0, 25));
  EXPECT_NEAR(s->getParameterAtControlPoint(4), 0.5, kEps);
  EXPECT_NEAR(s->getLengthAtControlPoint(4), 50.0, kEps);
  EXPECT_NEAR(s->getParameterAtLength(75.0), 0.75, kEps);
  expectPointNear(s->getPointAtLength(10.0), TPointD(0, 10));

  int chunk = -1;
  double t  = -1;
  EXPECT_FALSE(s->getChunkAndT(0.6, chunk, t));  // false means success
  EXPECT_EQ(chunk, 2);
  EXPECT_NEAR(t, 0.4, kEps);

  expectPointNear(convert(s->getControlPoint(2)), TPointD(0, 25));
}

TEST(TStroke, ThicknessIsInterpolatedAlongTheStroke) {
  std::vector<TThickPoint> pts = {TThickPoint(0, 0, 0), TThickPoint(5, 0, 2),
                                  TThickPoint(10, 0, 4)};
  TStroke s(pts);
  EXPECT_NEAR(s.getThickPoint(0.0).thick, 0.0, kEps);
  EXPECT_NEAR(s.getThickPoint(0.5).thick, 2.0, kEps);
  EXPECT_NEAR(s.getThickPoint(1.0).thick, 4.0, kEps);
  EXPECT_NEAR(s.getMaxThickness(), 4.0, kEps);
  // getAverageThickness() is not computed from the points: it is a plain
  // attribute (set by tools through setAverageThickness()) and 0 otherwise.
  EXPECT_EQ(s.getAverageThickness(), 0.0);
}

TEST(TStroke, ClosedCircle) {
  const TPointD c(10, 20);
  const double r = 50;
  std::unique_ptr<TStroke> s(makeCircle(c, r, 1.0));

  EXPECT_TRUE(s->isSelfLoop());
  EXPECT_EQ(s->getChunkCount(), 8);
  EXPECT_EQ(s->getControlPointCount(), 17);

  expectPointNear(s->getPoint(0.0), TPointD(c.x + r, c.y), 1e-9);
  expectPointNear(s->getPoint(0.25), TPointD(c.x, c.y + r), 1e-9);
  expectPointNear(s->getPoint(0.5), TPointD(c.x - r, c.y), 1e-9);
  expectPointNear(s->getPoint(1.0), s->getPoint(0.0), 1e-9);

  // Eight quadratic arcs overestimate the circumference by ~0.18%.
  EXPECT_NEAR(s->getLength(), 2 * kPi * r, 0.6);
  EXPECT_GT(s->getLength(), 2 * kPi * r);

  // The on-curve points are the extremes, so the centerline bbox is exact.
  const TRectD cl = s->getCenterlineBBox();
  EXPECT_NEAR(cl.x0, c.x - r, 1e-9);
  EXPECT_NEAR(cl.x1, c.x + r, 1e-9);
  EXPECT_NEAR(cl.y0, c.y - r, 1e-9);
  EXPECT_NEAR(cl.y1, c.y + r, 1e-9);
}

TEST(TStroke, StyleIdAndTransform) {
  std::unique_ptr<TStroke> s(makeLine(TPointD(0, 0), TPointD(10, 0), 1.0));
  EXPECT_EQ(s->getStyle(), 1);  // new strokes use style 1, not 0
  s->setStyle(3);
  EXPECT_EQ(s->getStyle(), 3);

  s->transform(TTranslation(5, 7));
  expectPointNear(s->getPoint(0.0), TPointD(5, 7));
  expectPointNear(s->getPoint(1.0), TPointD(15, 7));
  EXPECT_NEAR(s->getThickPoint(0.0).thick, 1.0, kEps);

  // Scaling changes thickness only when asked to.
  s->transform(TScale(2), false);
  EXPECT_NEAR(s->getLength(), 20.0, kEps);
  EXPECT_NEAR(s->getThickPoint(0.0).thick, 1.0, kEps);
  s->transform(TScale(2), true);
  EXPECT_NEAR(s->getThickPoint(0.0).thick, 2.0, kEps);
}

TEST(TStroke, CopyIsDeep) {
  std::unique_ptr<TStroke> a(makeLine(TPointD(0, 0), TPointD(10, 0), 1.0));
  a->setStyle(4);
  TStroke b(*a);
  EXPECT_EQ(b.getStyle(), 4);
  EXPECT_EQ(b.getControlPointCount(), 3);
  b.setControlPoint(2, TThickPoint(10, 10, 1.0));
  expectPointNear(a->getPoint(1.0), TPointD(10, 0));
  expectPointNear(b.getPoint(1.0), TPointD(10, 10));
}

//-----------------------------------------------------------------------------
// TVectorImage: strokes

TEST(TVectorImage, AddGetRemoveStrokes) {
  TVectorImageP vi = new TVectorImage();
  EXPECT_EQ(vi->getStrokeCount(), 0u);
  EXPECT_EQ(vi->getType(), TImage::VECTOR);

  TStroke *line   = makeLine(TPointD(0, 0), TPointD(100, 0), 1.0);
  TStroke *circle = makeCircle(TPointD(0, 0), 30, 1.0);
  EXPECT_EQ(vi->addStroke(line), 0);
  EXPECT_EQ(vi->addStroke(circle), 1);
  EXPECT_EQ(vi->getStrokeCount(), 2u);
  EXPECT_EQ(vi->getStroke(0), line);
  EXPECT_EQ(vi->getStroke(1), circle);
  EXPECT_EQ(vi->getStrokeIndex(circle), 1);

  // removeStroke() hands the stroke back to the caller, who owns it.
  std::unique_ptr<TStroke> removed(vi->removeStroke(0));
  EXPECT_EQ(removed.get(), line);
  EXPECT_EQ(vi->getStrokeCount(), 1u);
  EXPECT_EQ(vi->getStroke(0), circle);
  EXPECT_EQ(vi->getStrokeIndex(line), -1);
}

TEST(TVectorImage, DegenerateStrokesAreDiscarded) {
  TVectorImageP vi = new TVectorImage();
  std::unique_ptr<TStroke> dot(makeLine(TPointD(5, 5), TPointD(5, 5), 0.0));
  // With discardPoints (the default) a stroke whose bbox is a single point
  // is rejected: -1, and the caller keeps ownership.
  EXPECT_EQ(vi->addStroke(dot.get()), -1);
  EXPECT_EQ(vi->getStrokeCount(), 0u);
}

TEST(TVectorImage, BBoxNeedsAPalette) {
  TVectorImageP vi = new TVectorImage();
  EXPECT_TRUE(vi->getBBox().isEmpty());
  TStroke *s = makeLine(TPointD(0, 0), TPointD(100, 50), 2.0);
  s->setStyle(1);
  vi->addStroke(s);

  // TVectorImage::getBBox() asks each stroke's color style for its bbox, so
  // without a palette every stroke is skipped and the result is empty.
  EXPECT_TRUE(vi->getBBox().isEmpty());

  // A default palette has styles 0 and 1; a plain style's bbox is the
  // stroke's outline bbox.
  vi->setPalette(new TPalette());
  const TRectD bb = vi->getBBox();
  const TRectD sb = s->getBBox();
  EXPECT_FALSE(bb.isEmpty());
  EXPECT_NEAR(bb.x0, sb.x0, kEps);
  EXPECT_NEAR(bb.y0, sb.y0, kEps);
  EXPECT_NEAR(bb.x1, sb.x1, kEps);
  EXPECT_NEAR(bb.y1, sb.y1, kEps);
}

//-----------------------------------------------------------------------------
// TVectorImage: regions

TEST(TVectorImage, ClosedStrokeMakesOneRegion) {
  const TPointD c(10, -5);
  TVectorImageP vi = new TVectorImage();
  vi->addStroke(makeCircle(c, 40, 2.0));
  EXPECT_EQ(vi->getRegionCount(), 0u);  // regions are computed on demand

  vi->findRegions();
  ASSERT_EQ(vi->getRegionCount(), 1u);
  TRegion *region = vi->getRegion(0u);
  ASSERT_NE(region, nullptr);

  EXPECT_EQ(region->getStyle(), 0);  // unfilled
  region->setStyle(5);
  EXPECT_EQ(region->getStyle(), 5);

  EXPECT_TRUE(region->contains(c));
  EXPECT_TRUE(region->contains(c + TPointD(30, 0)));
  EXPECT_FALSE(region->contains(c + TPointD(45, 0)));
  EXPECT_FALSE(region->contains(c + TPointD(100, 100)));

  EXPECT_EQ(vi->getRegion(c), region);
  EXPECT_EQ(vi->getRegion(c + TPointD(100, 100)), nullptr);

  // The region's bbox is the bbox of its edges' outlines, so it includes the
  // stroke thickness: radius 40 + thickness 2.
  const TRectD rb = region->getBBox();
  const TRectD sb = vi->getStroke(0)->getBBox();
  EXPECT_NEAR(rb.x0, sb.x0, 1e-6);
  EXPECT_NEAR(rb.x1, sb.x1, 1e-6);
  EXPECT_NEAR(rb.y0, sb.y0, 1e-6);
  EXPECT_NEAR(rb.y1, sb.y1, 1e-6);
  EXPECT_NEAR(rb.x0, c.x - 42, 1e-6);
  EXPECT_NEAR(rb.x1, c.x + 42, 1e-6);
  EXPECT_NEAR(rb.y0, c.y - 42, 1e-6);
  EXPECT_NEAR(rb.y1, c.y + 42, 1e-6);
}

TEST(TVectorImage, FillSetsTheRegionStyle) {
  TVectorImageP vi = new TVectorImage();
  vi->addStroke(makeCircle(TPointD(0, 0), 40, 2.0));
  vi->findRegions();
  ASSERT_EQ(vi->getRegionCount(), 1u);

  vi->fill(TPointD(0, 0), 7);
  EXPECT_EQ(vi->getRegion(0u)->getStyle(), 7);
  // Filling outside every region changes nothing.
  vi->fill(TPointD(500, 500), 9);
  EXPECT_EQ(vi->getRegion(0u)->getStyle(), 7);
}

TEST(TVectorImage, OpenStrokeMakesNoRegion) {
  TVectorImageP vi = new TVectorImage();
  vi->addStroke(makeLine(TPointD(0, 0), TPointD(100, 0), 2.0));
  vi->findRegions();
  EXPECT_EQ(vi->getRegionCount(), 0u);
  EXPECT_EQ(vi->getRegion(TPointD(50, 0)), nullptr);
}

TEST(TVectorImage, DisjointClosedStrokesMakeOneRegionEach) {
  TVectorImageP vi = new TVectorImage();
  vi->addStroke(makeCircle(TPointD(-100, 0), 30, 1.0));
  vi->addStroke(makeCircle(TPointD(100, 0), 30, 1.0));
  vi->findRegions();
  ASSERT_EQ(vi->getRegionCount(), 2u);
  TRegion *left  = vi->getRegion(TPointD(-100, 0));
  TRegion *right = vi->getRegion(TPointD(100, 0));
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_NE(left, right);
}

TEST(TVectorImage, NestedClosedStrokesMakeASubregion) {
  // A closed stroke inside another becomes a subregion of the outer region:
  // the image lists only the top-level region.
  TVectorImageP vi = new TVectorImage();
  vi->addStroke(makeCircle(TPointD(0, 0), 80, 1.0));
  vi->addStroke(makeCircle(TPointD(0, 0), 20, 1.0));
  vi->findRegions();
  ASSERT_EQ(vi->getRegionCount(), 1u);
  TRegion *outer = vi->getRegion(0u);
  ASSERT_EQ(outer->getSubregionCount(), 1u);
  TRegion *inner = outer->getSubregion(0);

  EXPECT_EQ(vi->getRegion(TPointD(0, 0)), inner);
  EXPECT_EQ(vi->getRegion(TPointD(50, 0)), outer);
  // TRegion::contains() tests the outline only, so the outer region also
  // "contains" points of its hole.
  EXPECT_TRUE(outer->contains(TPointD(0, 0)));
}

TEST(TVectorImage, OverlappingClosedStrokesSplitIntoThreeRegions) {
  TVectorImageP vi = new TVectorImage();
  vi->addStroke(makeCircle(TPointD(-25, 0), 40, 1.0));
  vi->addStroke(makeCircle(TPointD(25, 0), 40, 1.0));
  vi->findRegions();

  TRegion *left  = vi->getRegion(TPointD(-50, 0));
  TRegion *lens  = vi->getRegion(TPointD(0, 0));
  TRegion *right = vi->getRegion(TPointD(50, 0));
  ASSERT_NE(left, nullptr);
  ASSERT_NE(lens, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_NE(left, lens);
  EXPECT_NE(right, lens);
  EXPECT_NE(left, right);
  EXPECT_EQ(vi->getRegionCount(), 3u);
}

TEST(TVectorImage, RemovingTheClosedStrokeRemovesItsRegion) {
  TVectorImageP vi = new TVectorImage();
  vi->addStroke(makeLine(TPointD(200, 0), TPointD(300, 0), 1.0));
  vi->addStroke(makeCircle(TPointD(0, 0), 40, 1.0));
  vi->findRegions();
  ASSERT_EQ(vi->getRegionCount(), 1u);

  std::unique_ptr<TStroke> removed(vi->removeStroke(1));
  EXPECT_EQ(vi->getRegionCount(), 0u);
}

//-----------------------------------------------------------------------------
// TVectorImage: cloning

TEST(TVectorImage, CloneImageIsIndependent) {
  TVectorImageP vi = new TVectorImage();
  TStroke *circle  = makeCircle(TPointD(0, 0), 40, 2.0);
  circle->setStyle(1);
  vi->addStroke(circle);
  vi->addStroke(makeLine(TPointD(100, 0), TPointD(200, 0), 1.0));
  vi->findRegions();
  ASSERT_EQ(vi->getRegionCount(), 1u);
  vi->getRegion(0u)->setStyle(4);

  TVectorImageP copy = vi->cloneImage();
  ASSERT_TRUE(copy);
  ASSERT_EQ(copy->getStrokeCount(), 2u);
  EXPECT_NE(copy->getStroke(0), vi->getStroke(0));
  EXPECT_EQ(copy->getStroke(0)->getStyle(), 1);
  EXPECT_EQ(copy->getStroke(0)->getId(), vi->getStroke(0)->getId());
  ASSERT_EQ(copy->getRegionCount(), 1u);
  EXPECT_NE(copy->getRegion(0u), vi->getRegion(0u));
  EXPECT_EQ(copy->getRegion(0u)->getStyle(), 4);

  // Changing the copy leaves the original alone.
  copy->getStroke(0)->setStyle(2);
  copy->getRegion(0u)->setStyle(6);
  std::unique_ptr<TStroke> removed(copy->removeStroke(1));
  EXPECT_EQ(vi->getStroke(0)->getStyle(), 1);
  EXPECT_EQ(vi->getRegion(0u)->getStyle(), 4);
  EXPECT_EQ(vi->getStrokeCount(), 2u);
  EXPECT_EQ(copy->getStrokeCount(), 1u);
}
