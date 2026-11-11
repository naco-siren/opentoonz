// Value types of the canvas IR: transform, path, mesh.

#include <gtest/gtest.h>

#include "canvas/tcanvastypes.h"

#include <cmath>

//-----------------------------------------------------------------------------

TEST(CanvasAffine, DefaultIsIdentity) {
  TCanvasAffine aff;
  EXPECT_TRUE(aff.isIdentity());
  EXPECT_EQ(aff * TCanvasPoint(3, -4), TCanvasPoint(3, -4));
  EXPECT_DOUBLE_EQ(aff.det(), 1.0);
}

// (a * b)(p) is a(b(p)): the convention of TAffine, so that porting
// "tglMultMatrix(rd.m_aff)" to "canvas.concat(aff)" changes nothing.
TEST(CanvasAffine, ProductAppliesTheRightFactorFirst) {
  const TCanvasAffine move  = TCanvasAffine::translation(10, 20);
  const TCanvasAffine twice = TCanvasAffine::scale(2, 2);

  EXPECT_EQ((move * twice) * TCanvasPoint(1, 1), TCanvasPoint(12, 22));
  EXPECT_EQ((twice * move) * TCanvasPoint(1, 1), TCanvasPoint(22, 42));
  EXPECT_EQ(move * (twice * TCanvasPoint(1, 1)), TCanvasPoint(12, 22));
}

TEST(CanvasAffine, RotationIsCounterClockwiseInDegrees) {
  const TCanvasPoint p = TCanvasAffine::rotation(90) * TCanvasPoint(1, 0);
  EXPECT_NEAR(p.x, 0.0, 1e-12);
  EXPECT_NEAR(p.y, 1.0, 1e-12);
  EXPECT_NEAR(TCanvasAffine::rotation(30).det(), 1.0, 1e-12);
}

TEST(CanvasAffine, DeterminantIsTheAreaScale) {
  EXPECT_DOUBLE_EQ(TCanvasAffine::scale(2, 3).det(), 6.0);
  EXPECT_DOUBLE_EQ(TCanvasAffine::scale(-1, 1).det(), -1.0);
}

//-----------------------------------------------------------------------------

TEST(CanvasColor, FromBytesIsStraightAlphaInZeroToOne) {
  const TCanvasColor c = TCanvasColor::fromBytes(255, 0, 51, 128);
  EXPECT_FLOAT_EQ(c.r, 1.f);
  EXPECT_FLOAT_EQ(c.g, 0.f);
  EXPECT_FLOAT_EQ(c.b, 0.2f);
  EXPECT_FLOAT_EQ(c.a, 128 / 255.f);
  EXPECT_FLOAT_EQ(TCanvasColor::fromBytes(1, 2, 3).a, 1.f);
}

//-----------------------------------------------------------------------------

TEST(CanvasPath, RecordsVerbsAndTheirPoints) {
  TCanvasPath path;
  EXPECT_TRUE(path.isEmpty());

  path.moveTo({0, 0}).lineTo({10, 0}).quadTo({10, 10}, {0, 10}).close();

  const std::vector<TCanvasPath::Verb> verbs = {
      TCanvasPath::MoveTo, TCanvasPath::LineTo, TCanvasPath::QuadTo,
      TCanvasPath::Close};
  EXPECT_EQ(path.verbs(), verbs);
  ASSERT_EQ(path.points().size(), 4u);  // a quadratic takes two
  EXPECT_EQ(path.points()[2], TCanvasPoint(10, 10));
  EXPECT_EQ(path.points()[3], TCanvasPoint(0, 10));
  EXPECT_FALSE(path.isEmpty());

  path.clear();
  EXPECT_TRUE(path.isEmpty());
  EXPECT_TRUE(path.verbs().empty());
}

// A region is an outer contour plus one contour per hole.
TEST(CanvasPath, AddPolygonMakesOneContourPerCall) {
  const TCanvasPoint outer[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  const TCanvasPoint hole[3]  = {{2, 2}, {4, 8}, {8, 2}};

  TCanvasPath path;
  path.addPolygon(outer, 4).addPolygon(hole, 3);

  int moves = 0, closes = 0;
  for (TCanvasPath::Verb verb : path.verbs()) {
    moves += verb == TCanvasPath::MoveTo;
    closes += verb == TCanvasPath::Close;
  }
  EXPECT_EQ(moves, 2);
  EXPECT_EQ(closes, 2);
  EXPECT_EQ(path.points().size(), 7u);

  TCanvasPath open;
  open.addPolygon(outer, 4, false);
  EXPECT_EQ(open.verbs().back(), TCanvasPath::LineTo);

  TCanvasPath none;
  none.addPolygon(outer, 0).addPolygon(nullptr, 3);
  EXPECT_TRUE(none.isEmpty());
}

// There is no current point in a new path or after close(); a drawing verb
// then starts a contour instead of reading a point that does not exist.
TEST(CanvasPath, DrawingWithoutACurrentPointStartsAContour) {
  TCanvasPath path;
  path.lineTo({1, 1});
  EXPECT_EQ(path.verbs().back(), TCanvasPath::MoveTo);

  path.lineTo({2, 2}).close().quadTo({5, 5}, {6, 6});
  EXPECT_EQ(path.verbs().back(), TCanvasPath::MoveTo);
  EXPECT_EQ(path.points().back(), TCanvasPoint(6, 6));
  EXPECT_EQ(path.points().size(), 3u);

  TCanvasPath closedTwice;
  closedTwice.moveTo({0, 0}).lineTo({1, 0}).close().close();
  EXPECT_EQ(closedTwice.verbs().size(), 3u);
}

//-----------------------------------------------------------------------------

TEST(CanvasMesh, CountsTrianglesByMode) {
  TCanvasMesh mesh;
  mesh.positions.resize(6);

  mesh.mode = TCanvasMesh::Mode::Triangles;
  EXPECT_EQ(mesh.triangleCount(), 2u);
  mesh.mode = TCanvasMesh::Mode::TriangleStrip;
  EXPECT_EQ(mesh.triangleCount(), 4u);
  mesh.mode = TCanvasMesh::Mode::TriangleFan;
  EXPECT_EQ(mesh.triangleCount(), 4u);

  mesh.positions.resize(2);
  EXPECT_EQ(mesh.triangleCount(), 0u);
}

// A polygon is the triangles of a fan.
TEST(CanvasMesh, APolygonIsAFan) {
  TCanvasMesh mesh;
  mesh.mode = TCanvasMesh::Mode::Polygon;
  mesh.positions.resize(8);
  EXPECT_EQ(mesh.triangleCount(), 6u);
  EXPECT_TRUE(mesh.isValid());
  mesh.positions.resize(2);
  EXPECT_FALSE(mesh.isValid());
}

// Every four vertices are one quad, and a quad is two triangles.
TEST(CanvasMesh, QuadsAreFourVerticesEach) {
  TCanvasMesh mesh;
  mesh.mode = TCanvasMesh::Mode::Quads;

  mesh.positions.resize(8);
  EXPECT_EQ(mesh.triangleCount(), 4u);
  EXPECT_TRUE(mesh.isValid());

  mesh.positions.resize(6);
  EXPECT_FALSE(mesh.isValid()) << "a quad and a half";
  mesh.positions.resize(3);
  EXPECT_FALSE(mesh.isValid()) << "not one whole quad";
}

TEST(CanvasMesh, IsValidOnlyWithWholeTrianglesAndMatchingArrays) {
  TCanvasMesh mesh;
  EXPECT_FALSE(mesh.isValid()) << "no vertices";

  mesh.positions.resize(4);
  EXPECT_FALSE(mesh.isValid()) << "four vertices are not whole triangles";
  mesh.mode = TCanvasMesh::Mode::TriangleStrip;
  EXPECT_TRUE(mesh.isValid());

  mesh.colors.resize(3);
  EXPECT_FALSE(mesh.isValid()) << "a colour for some vertices only";
  mesh.colors.resize(4);
  EXPECT_TRUE(mesh.isValid());

  mesh.uvs.resize(5);
  EXPECT_FALSE(mesh.isValid());
  mesh.uvs.resize(4);
  EXPECT_TRUE(mesh.isValid());
}
