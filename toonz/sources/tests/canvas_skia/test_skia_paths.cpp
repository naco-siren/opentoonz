// The Skia canvas: where pixels land, fills, clips and strokes.

#include "canvas_skia_test_util.h"

//-----------------------------------------------------------------------------
// The target

TEST(SkiaCanvasTarget, DrawsIntoBgraRowsBottomUp) {
  Target target(8, 8);
  TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());
  ASSERT_TRUE(canvas.isValid());
  EXPECT_EQ(canvas.deviceBounds().width(), 8);
  EXPECT_EQ(canvas.deviceBounds().height(), 8);

  TCanvasPaint paint = paintOf(1.f, 0.5f, 0.f);
  paint.antialias    = false;
  canvas.fillPath(rectPath(0, 0, 4, 2), TCanvasFillRule::NonZero, paint);

  // The first pixel in memory is the one at the bottom left.
  EXPECT_EQ(target.bytes[0], 0) << "blue";
  EXPECT_NEAR(target.bytes[1], 128, 1) << "green";
  EXPECT_EQ(target.bytes[2], 255) << "red";
  EXPECT_EQ(target.bytes[3], 255) << "alpha";

  EXPECT_TRUE(pixelIs(target, 3, 1, 255, 128, 0, 255));
  EXPECT_EQ(target.alpha(4, 1), 0);
  EXPECT_EQ(target.alpha(0, 2), 0);
}

TEST(SkiaCanvasTarget, StoresPremultipliedColourAndBlendsOverWhatIsThere) {
  Target target(4, 4);
  {
    TSkiaCanvas canvas(target.pixels(), 4, 4, target.rowBytes());
    canvas.fillPath(rectPath(0, 0, 4, 4), TCanvasFillRule::NonZero,
                    paintOf(1.f, 0.f, 0.f, 0.5f));
  }
  EXPECT_TRUE(pixelIs(target, 1, 1, 128, 0, 0, 128));

  target.fill(0, 0, 255, 255);
  {
    TSkiaCanvas canvas(target.pixels(), 4, 4, target.rowBytes());
    canvas.fillPath(rectPath(0, 0, 4, 4), TCanvasFillRule::NonZero,
                    paintOf(1.f, 0.f, 0.f, 0.5f));
  }
  EXPECT_TRUE(pixelIs(target, 1, 1, 128, 0, 127, 255));
}

TEST(SkiaCanvasTarget, RowsMayBeFurtherApartThanTheyAreWide) {
  // Like a TRaster that is a window on a wider one.
  const int rowBytes = 6 * 4;
  std::vector<unsigned char> bytes(rowBytes * 4);
  TSkiaCanvas canvas(bytes.data(), 4, 4, rowBytes);
  ASSERT_TRUE(canvas.isValid());

  canvas.fillPath(rectPath(0, 0, 4, 4), TCanvasFillRule::NonZero,
                  paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(bytes[1 * rowBytes + 3 * 4 + 3], 255) << "inside the window";
  EXPECT_EQ(bytes[1 * rowBytes + 4 * 4 + 3], 0) << "beside the window";
}

TEST(SkiaCanvasTarget, DrawsNothingIntoPixelsItCannotUse) {
  std::vector<unsigned char> bytes(4 * 4 * 4);
  const TCanvasPath all = rectPath(0, 0, 4, 4);

  TSkiaCanvas noPixels(nullptr, 4, 4, 16);
  EXPECT_FALSE(noPixels.isValid());
  noPixels.fillPath(all, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(noPixels.deviceBounds().width(), 4);

  TSkiaCanvas narrowRows(bytes.data(), 4, 4, 8);
  EXPECT_FALSE(narrowRows.isValid());
  TSkiaCanvas topDown(bytes.data() + 3 * 16, 4, 4, -16);
  EXPECT_FALSE(topDown.isValid());
  TSkiaCanvas noSize(bytes.data(), 0, 4, 16);
  EXPECT_FALSE(noSize.isValid());

  narrowRows.save();
  narrowRows.concat(TCanvasAffine::scale(2, 2));
  narrowRows.fillPath(all, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));
  narrowRows.restore();
  for (unsigned char byte : bytes) ASSERT_EQ(byte, 0);
}

//-----------------------------------------------------------------------------
// Fills

TEST(SkiaCanvasFill, AntialiasingIsAreaCoverage) {
  Target target(8, 8);
  TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());

  // The right edge cuts the pixels of column 5 in half.
  canvas.fillPath(rectPath(2, 2, 5.5, 6), TCanvasFillRule::NonZero,
                  paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(target.alpha(4, 3), 255);
  EXPECT_NEAR(target.alpha(5, 3), 128, 1);
  EXPECT_EQ(target.alpha(6, 3), 0);
  EXPECT_EQ(target.alpha(1, 3), 0);
}

TEST(SkiaCanvasFill, WithoutAntialiasingAPixelIsInOrOut) {
  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f);
  paint.antialias    = false;

  Target less(8, 8);
  TSkiaCanvas lessCanvas(less.pixels(), 8, 8, less.rowBytes());
  lessCanvas.fillPath(rectPath(2, 2, 5.25, 6), TCanvasFillRule::NonZero, paint);
  EXPECT_EQ(less.alpha(5, 3), 0) << "the pixel's centre is outside";

  Target more(8, 8);
  TSkiaCanvas moreCanvas(more.pixels(), 8, 8, more.rowBytes());
  moreCanvas.fillPath(rectPath(2, 2, 5.75, 6), TCanvasFillRule::NonZero, paint);
  EXPECT_EQ(more.alpha(5, 3), 255) << "the pixel's centre is inside";
}

TEST(SkiaCanvasFill, FillRuleDecidesWhatANestedContourDoes) {
  // Two squares wound the same way, one inside the other.
  TCanvasPath path = rectPath(1, 1, 11, 11);
  const TCanvasPoint inner[4] = {{4, 4}, {8, 4}, {8, 8}, {4, 8}};
  path.addPolygon(inner, 4);

  Target nonZero(12, 12);
  TSkiaCanvas a(nonZero.pixels(), 12, 12, nonZero.rowBytes());
  a.fillPath(path, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(nonZero.alpha(6, 6), 255);

  Target evenOdd(12, 12);
  TSkiaCanvas b(evenOdd.pixels(), 12, 12, evenOdd.rowBytes());
  b.fillPath(path, TCanvasFillRule::EvenOdd, paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(evenOdd.alpha(6, 6), 0);
  EXPECT_EQ(evenOdd.alpha(2, 6), 255);
}

TEST(SkiaCanvasFill, CurvesAreNotFlattenedIntoTheirControlPolygon) {
  // A quarter disc of radius 16 around the origin, its arc one quadratic.
  TCanvasPath path;
  path.moveTo({0, 0}).lineTo({16, 0}).quadTo({16, 16}, {0, 16}).close();

  Target target(16, 16);
  TSkiaCanvas canvas(target.pixels(), 16, 16, target.rowBytes());
  canvas.fillPath(path, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));

  EXPECT_EQ(target.alpha(4, 4), 255);
  // The curve passes through (12, 12); the control polygon would reach
  // the corner.
  EXPECT_EQ(target.alpha(10, 10), 255);
  EXPECT_EQ(target.alpha(13, 13), 0);
  EXPECT_EQ(target.alpha(15, 15), 0);
}

//-----------------------------------------------------------------------------
// Transform

TEST(SkiaCanvasTransform, ConcatAppliesBeforeTheCurrentTransform) {
  Target target(8, 8);
  TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());

  canvas.concat(TCanvasAffine::translation(4, 0));
  canvas.concat(TCanvasAffine::scale(2, 2));
  // The unit square is doubled, then moved: it lands on (4, 0) to (6, 2).
  canvas.fillPath(rectPath(0, 0, 1, 1), TCanvasFillRule::NonZero,
                  paintOf(0.f, 0.f, 0.f));

  EXPECT_EQ(target.alpha(4, 0), 255);
  EXPECT_EQ(target.alpha(5, 1), 255);
  EXPECT_EQ(target.alpha(3, 0), 0);
  EXPECT_EQ(target.alpha(6, 0), 0);
  EXPECT_EQ(target.alpha(4, 2), 0);
  EXPECT_DOUBLE_EQ(canvas.pixelSize(), 0.5);
}

TEST(SkiaCanvasTransform, RestoreBringsBackTheTransformAndTheClip) {
  Target target(8, 8);
  TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());

  canvas.save();
  canvas.concat(TCanvasAffine::translation(100, 100));
  canvas.clipPath(rectPath(0, 0, 1, 1), TCanvasFillRule::NonZero,
                  TCanvasClipOp::Intersect, false);
  canvas.restore();
  canvas.fillPath(rectPath(0, 0, 8, 8), TCanvasFillRule::NonZero,
                  paintOf(0.f, 0.f, 0.f));

  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) ASSERT_EQ(target.alpha(x, y), 255);
}

//-----------------------------------------------------------------------------
// Clip

TEST(SkiaCanvasClip, KeepsTheInsideOrTheOutsideOfAPath) {
  const TCanvasPath all = rectPath(0, 0, 8, 8);

  Target inside(8, 8);
  TSkiaCanvas a(inside.pixels(), 8, 8, inside.rowBytes());
  a.clipPath(rectPath(2, 2, 6, 6), TCanvasFillRule::NonZero,
             TCanvasClipOp::Intersect, true);
  a.fillPath(all, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(inside.alpha(3, 3), 255);
  EXPECT_EQ(inside.alpha(1, 3), 0);
  EXPECT_EQ(inside.alpha(6, 3), 0);

  Target outside(8, 8);
  TSkiaCanvas b(outside.pixels(), 8, 8, outside.rowBytes());
  b.clipPath(rectPath(2, 2, 6, 6), TCanvasFillRule::NonZero,
             TCanvasClipOp::Difference, true);
  b.fillPath(all, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(outside.alpha(3, 3), 0);
  EXPECT_EQ(outside.alpha(1, 3), 255);
  EXPECT_EQ(outside.alpha(6, 3), 255);
}

TEST(SkiaCanvasClip, IsSmoothOnlyWhenAskedTo) {
  const TCanvasPath all = rectPath(0, 0, 8, 8);

  Target smooth(8, 8);
  TSkiaCanvas a(smooth.pixels(), 8, 8, smooth.rowBytes());
  a.clipPath(rectPath(0, 0, 4.5, 8), TCanvasFillRule::NonZero,
             TCanvasClipOp::Intersect, true);
  a.fillPath(all, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));
  EXPECT_NEAR(smooth.alpha(4, 3), 128, 1);

  Target hard(8, 8);
  TSkiaCanvas b(hard.pixels(), 8, 8, hard.rowBytes());
  b.clipPath(rectPath(0, 0, 4.25, 8), TCanvasFillRule::NonZero,
             TCanvasClipOp::Intersect, false);
  b.fillPath(all, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(hard.alpha(3, 3), 255);
  EXPECT_EQ(hard.alpha(4, 3), 0);
}

TEST(SkiaCanvasClip, AnEmptyPathLeavesNothingToDrawOn) {
  Target target(8, 8);
  TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());

  canvas.save();
  canvas.clipPath(TCanvasPath(), TCanvasFillRule::NonZero,
                  TCanvasClipOp::Intersect, true);
  canvas.fillPath(rectPath(0, 0, 8, 8), TCanvasFillRule::NonZero,
                  paintOf(0.f, 0.f, 0.f));
  canvas.restore();
  for (unsigned char byte : target.bytes) ASSERT_EQ(byte, 0);
}

TEST(SkiaCanvasClip, IsSetInTheCoordinatesOfItsOwnCall) {
  Target target(8, 8);
  TSkiaCanvas canvas(target.pixels(), 8, 8, target.rowBytes());

  canvas.concat(TCanvasAffine::scale(2, 2));
  canvas.clipPath(rectPath(1, 1, 3, 3), TCanvasFillRule::NonZero,
                  TCanvasClipOp::Intersect, false);  // device (2, 2)-(6, 6)
  canvas.concat(TCanvasAffine::translation(100, 0));
  canvas.fillPath(rectPath(-100, 0, -96, 4), TCanvasFillRule::NonZero,
                  paintOf(0.f, 0.f, 0.f));

  EXPECT_EQ(target.alpha(2, 2), 255);
  EXPECT_EQ(target.alpha(5, 5), 255);
  EXPECT_EQ(target.alpha(1, 2), 0);
  EXPECT_EQ(target.alpha(6, 5), 0);
}

//-----------------------------------------------------------------------------
// Growing a shape

// The reason the paint can grow a shape (question Q1 of the design): two
// regions that share an edge are two fills, and each covers half of the
// pixels the edge runs through.
TEST(SkiaCanvasGrow, ClosesTheSeamBetweenAbuttingShapes) {
  const TCanvasPath left = rectPath(2, 0, 8.5, 4), right = rectPath(8.5, 0, 14, 4);

  Target plain(16, 4);
  TSkiaCanvas a(plain.pixels(), 16, 4, plain.rowBytes());
  a.fillPath(left, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));
  a.fillPath(right, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));
  EXPECT_NEAR(plain.alpha(8, 1), 191, 1) << "a quarter of what is behind shows";

  TCanvasPaint grown = paintOf(0.f, 0.f, 0.f);
  grown.growDevice   = 0.5;
  Target closed(16, 4);
  TSkiaCanvas b(closed.pixels(), 16, 4, closed.rowBytes());
  b.fillPath(left, TCanvasFillRule::NonZero, grown);
  b.fillPath(right, TCanvasFillRule::NonZero, paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(closed.alpha(8, 1), 255);
  // The price: the first shape is half a pixel wider on its free side too.
  EXPECT_NEAR(closed.alpha(1, 1), 128, 1);
  EXPECT_EQ(closed.alpha(0, 1), 0);
}

namespace {

// A square ring from (2, 2) to (18, 18) with a hole from (6, 6) to (14, 14),
// grown by one pixel with a paint of half opacity.
void expectGrownRing(const TCanvasPath &ring, TCanvasFillRule rule,
                     const char *what) {
  SCOPED_TRACE(what);
  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f, 0.5f);
  paint.growDevice   = 1.0;

  Target target(20, 20);
  TSkiaCanvas canvas(target.pixels(), 20, 20, target.rowBytes());
  canvas.fillPath(ring, rule, paint);

  // Outwards by one pixel.
  EXPECT_EQ(target.alpha(0, 10), 0);
  EXPECT_NEAR(target.alpha(1, 10), 128, 1);
  EXPECT_NEAR(target.alpha(18, 10), 128, 1);
  EXPECT_EQ(target.alpha(19, 10), 0);
  // Into the hole by one pixel.
  EXPECT_NEAR(target.alpha(6, 10), 128, 1);
  EXPECT_EQ(target.alpha(7, 10), 0);
  EXPECT_EQ(target.alpha(12, 10), 0);
  EXPECT_NEAR(target.alpha(13, 10), 128, 1);
  // The paint is applied once: no pixel of the ring is more opaque than
  // the paint, and none is left out next to an outline.
  for (int x = 1; x <= 6; ++x) EXPECT_NEAR(target.alpha(x, 10), 128, 1) << x;
  for (int y = 13; y <= 18; ++y) EXPECT_NEAR(target.alpha(10, y), 128, 1) << y;
}

}  // namespace

TEST(SkiaCanvasGrow, GrowsIntoHolesWhicheverWayAndOrderTheContoursCome) {
  const TCanvasPoint outer[4] = {{2, 2}, {18, 2}, {18, 18}, {2, 18}};
  const TCanvasPoint outerCw[4] = {{2, 2}, {2, 18}, {18, 18}, {18, 2}};
  const TCanvasPoint hole[4]    = {{6, 6}, {6, 14}, {14, 14}, {14, 6}};
  const TCanvasPoint holeCcw[4] = {{6, 6}, {14, 6}, {14, 14}, {6, 14}};

  TCanvasPath path;
  path.addPolygon(outer, 4).addPolygon(hole, 4);
  expectGrownRing(path, TCanvasFillRule::NonZero, "outline first");

  path.clear();
  path.addPolygon(hole, 4).addPolygon(outer, 4);
  expectGrownRing(path, TCanvasFillRule::NonZero, "hole first");

  path.clear();
  path.addPolygon(outerCw, 4).addPolygon(holeCcw, 4);
  expectGrownRing(path, TCanvasFillRule::NonZero, "clockwise outline");

  path.clear();
  path.addPolygon(holeCcw, 4).addPolygon(outerCw, 4);
  expectGrownRing(path, TCanvasFillRule::NonZero, "clockwise, hole first");

  // Under the even-odd rule the hole needs no direction of its own.
  path.clear();
  path.addPolygon(outer, 4).addPolygon(holeCcw, 4);
  expectGrownRing(path, TCanvasFillRule::EvenOdd, "even-odd");

  path.clear();
  path.addPolygon(holeCcw, 4).addPolygon(outer, 4);
  expectGrownRing(path, TCanvasFillRule::EvenOdd, "even-odd, hole first");
}

TEST(SkiaCanvasGrow, IsMeasuredInDevicePixelsWhateverTheTransform) {
  Target target(16, 8);
  TSkiaCanvas canvas(target.pixels(), 16, 8, target.rowBytes());

  TCanvasPaint paint = paintOf(0.f, 0.f, 0.f);
  paint.growDevice   = 1.0;
  canvas.concat(TCanvasAffine::scale(4, 1));
  // Device (4, 2) to (12, 6) before it grows.
  canvas.fillPath(rectPath(1, 2, 3, 6), TCanvasFillRule::NonZero, paint);

  EXPECT_EQ(target.alpha(2, 4), 0);
  EXPECT_EQ(target.alpha(3, 4), 255) << "one pixel, not four";
  EXPECT_EQ(target.alpha(12, 4), 255);
  EXPECT_EQ(target.alpha(13, 4), 0);
  EXPECT_EQ(target.alpha(8, 0), 0);
  EXPECT_EQ(target.alpha(8, 1), 255);
  EXPECT_EQ(target.alpha(8, 6), 255);
  EXPECT_EQ(target.alpha(8, 7), 0);
}

//-----------------------------------------------------------------------------
// Strokes

TEST(SkiaCanvasStroke, ADevicePenKeepsItsWidthUnderATransform) {
  TCanvasPen pen;
  pen.width = 2.0;
  pen.unit  = TCanvasPen::Unit::Device;

  TCanvasPath line;
  line.moveTo({1, 2}).lineTo({5, 2});  // device (4, 8) to (20, 8)

  Target target(24, 16);
  TSkiaCanvas canvas(target.pixels(), 24, 16, target.rowBytes());
  canvas.concat(TCanvasAffine::scale(4, 4));
  canvas.strokePath(line, pen, paintOf(0.f, 0.f, 0.f));

  EXPECT_EQ(target.alpha(10, 6), 0);
  EXPECT_EQ(target.alpha(10, 7), 255);
  EXPECT_EQ(target.alpha(10, 8), 255);
  EXPECT_EQ(target.alpha(10, 9), 0);
  // Butt caps: the line ends where the path does.
  EXPECT_EQ(target.alpha(3, 7), 0);
  EXPECT_EQ(target.alpha(4, 7), 255);
  EXPECT_EQ(target.alpha(19, 7), 255);
  EXPECT_EQ(target.alpha(20, 7), 0);
}

TEST(SkiaCanvasStroke, ADevicePenIsNotDistortedByAnUnevenScale) {
  TCanvasPen pen;
  pen.width = 2.0;
  pen.unit  = TCanvasPen::Unit::Device;

  TCanvasPath line;
  line.moveTo({2, 1}).lineTo({2, 7});  // device x = 8

  Target target(16, 8);
  TSkiaCanvas canvas(target.pixels(), 16, 8, target.rowBytes());
  canvas.concat(TCanvasAffine::scale(4, 1));
  canvas.strokePath(line, pen, paintOf(0.f, 0.f, 0.f));

  EXPECT_EQ(target.alpha(6, 4), 0);
  EXPECT_EQ(target.alpha(7, 4), 255);
  EXPECT_EQ(target.alpha(8, 4), 255);
  EXPECT_EQ(target.alpha(9, 4), 0) << "two pixels wide, not eight";
}

TEST(SkiaCanvasStroke, ALocalPenScalesWithTheTransform) {
  TCanvasPen pen;
  pen.width = 1.0;
  pen.unit  = TCanvasPen::Unit::Local;

  TCanvasPath line;
  line.moveTo({1, 2}).lineTo({5, 2});

  Target target(24, 16);
  TSkiaCanvas canvas(target.pixels(), 24, 16, target.rowBytes());
  canvas.concat(TCanvasAffine::scale(4, 4));
  canvas.strokePath(line, pen, paintOf(0.f, 0.f, 0.f));

  // One unit is four pixels here: rows 6 to 9.
  EXPECT_EQ(target.alpha(10, 5), 0);
  EXPECT_EQ(target.alpha(10, 6), 255);
  EXPECT_EQ(target.alpha(10, 9), 255);
  EXPECT_EQ(target.alpha(10, 10), 0);
}

TEST(SkiaCanvasStroke, RoundCapsReachPastTheEndsOfThePath) {
  TCanvasPen pen;
  pen.width = 4.0;
  TCanvasPath line;
  line.moveTo({4, 4}).lineTo({10, 4});

  Target butt(16, 8);
  TSkiaCanvas a(butt.pixels(), 16, 8, butt.rowBytes());
  a.strokePath(line, pen, paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(butt.alpha(3, 4), 0);
  EXPECT_EQ(butt.alpha(4, 4), 255);

  pen.cap = TCanvasPen::Cap::Round;
  Target round(16, 8);
  TSkiaCanvas b(round.pixels(), 16, 8, round.rowBytes());
  b.strokePath(line, pen, paintOf(0.f, 0.f, 0.f));
  EXPECT_EQ(round.alpha(3, 4), 255);
  EXPECT_EQ(round.alpha(1, 4), 0);
  EXPECT_EQ(round.alpha(10, 4), 255);
  EXPECT_EQ(round.alpha(12, 4), 0);
}

namespace {

// Which of the first sixteen pixels of row 2 a line along y = 3 covers.
std::string coveredColumns(const Target &target) {
  std::string columns;
  for (int x = 0; x < 16; ++x) {
    const int alpha = target.alpha(x, 2);
    columns += alpha == 255 ? '#' : alpha == 0 ? '.' : '?';
  }
  return columns;
}

}  // namespace

TEST(SkiaCanvasStroke, DashesAreMeasuredInDevicePixels) {
  TCanvasPath line;
  line.moveTo({0, 1.5}).lineTo({8, 1.5});  // device (0, 3) to (16, 3)

  TCanvasPen pen;
  pen.width = 2.0;
  pen.unit  = TCanvasPen::Unit::Device;
  pen.dash  = {4, 2};
  {
    Target target(16, 6);
    TSkiaCanvas canvas(target.pixels(), 16, 6, target.rowBytes());
    canvas.concat(TCanvasAffine::scale(2, 2));
    canvas.strokePath(line, pen, paintOf(0.f, 0.f, 0.f));
    EXPECT_EQ(coveredColumns(target), "####..####..####");
  }

  // The same dashes with a pen whose width is in local units.
  pen.width = 1.0;
  pen.unit  = TCanvasPen::Unit::Local;
  {
    Target target(16, 6);
    TSkiaCanvas canvas(target.pixels(), 16, 6, target.rowBytes());
    canvas.concat(TCanvasAffine::scale(2, 2));
    canvas.strokePath(line, pen, paintOf(0.f, 0.f, 0.f));
    EXPECT_EQ(coveredColumns(target), "####..####..####");
  }

  // One length: dashes and gaps of that length.
  pen.dash = {3};
  {
    Target target(16, 6);
    TSkiaCanvas canvas(target.pixels(), 16, 6, target.rowBytes());
    canvas.concat(TCanvasAffine::scale(2, 2));
    canvas.strokePath(line, pen, paintOf(0.f, 0.f, 0.f));
    EXPECT_EQ(coveredColumns(target), "###...###...###.");
  }

  // Lengths that add up to nothing: a solid line.
  pen.dash = {0, 0};
  {
    Target target(16, 6);
    TSkiaCanvas canvas(target.pixels(), 16, 6, target.rowBytes());
    canvas.concat(TCanvasAffine::scale(2, 2));
    canvas.strokePath(line, pen, paintOf(0.f, 0.f, 0.f));
    EXPECT_EQ(coveredColumns(target), "################");
  }
}
