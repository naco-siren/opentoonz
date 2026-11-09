#include "canvas/tcanvas.h"

#include <cmath>

//=============================================================================
// TCanvasAffine

TCanvasAffine TCanvasAffine::translation(double dx, double dy) {
  return TCanvasAffine(1.0, 0.0, dx, 0.0, 1.0, dy);
}

TCanvasAffine TCanvasAffine::scale(double sx, double sy) {
  return TCanvasAffine(sx, 0.0, 0.0, 0.0, sy, 0.0);
}

TCanvasAffine TCanvasAffine::rotation(double degrees) {
  const double radians = degrees * 3.14159265358979323846 / 180.0;
  const double c = std::cos(radians), s = std::sin(radians);
  return TCanvasAffine(c, -s, 0.0, s, c, 0.0);
}

TCanvasAffine TCanvasAffine::operator*(const TCanvasAffine &b) const {
  return TCanvasAffine(
      a11 * b.a11 + a12 * b.a21, a11 * b.a12 + a12 * b.a22,
      a11 * b.a13 + a12 * b.a23 + a13, a21 * b.a11 + a22 * b.a21,
      a21 * b.a12 + a22 * b.a22, a21 * b.a13 + a22 * b.a23 + a23);
}

TCanvasPoint TCanvasAffine::operator*(const TCanvasPoint &p) const {
  return TCanvasPoint(a11 * p.x + a12 * p.y + a13, a21 * p.x + a22 * p.y + a23);
}

bool TCanvasAffine::isIdentity() const { return *this == TCanvasAffine(); }

//=============================================================================
// TCanvasPath

namespace {

// A drawing verb needs a current point. After close(), or in a new path,
// there is none, and the verb starts a contour at its own end point.
bool hasCurrentPoint(const std::vector<TCanvasPath::Verb> &verbs) {
  return !verbs.empty() && verbs.back() != TCanvasPath::Close;
}

}  // namespace

TCanvasPath &TCanvasPath::moveTo(const TCanvasPoint &p) {
  m_verbs.push_back(MoveTo);
  m_points.push_back(p);
  return *this;
}

TCanvasPath &TCanvasPath::lineTo(const TCanvasPoint &p) {
  if (!hasCurrentPoint(m_verbs)) return moveTo(p);
  m_verbs.push_back(LineTo);
  m_points.push_back(p);
  return *this;
}

TCanvasPath &TCanvasPath::quadTo(const TCanvasPoint &control,
                                 const TCanvasPoint &end) {
  if (!hasCurrentPoint(m_verbs)) return moveTo(end);
  m_verbs.push_back(QuadTo);
  m_points.push_back(control);
  m_points.push_back(end);
  return *this;
}

TCanvasPath &TCanvasPath::close() {
  if (hasCurrentPoint(m_verbs)) m_verbs.push_back(Close);
  return *this;
}

TCanvasPath &TCanvasPath::addPolygon(const TCanvasPoint *points,
                                     std::size_t count, bool closed) {
  if (!points || count == 0) return *this;
  moveTo(points[0]);
  for (std::size_t i = 1; i < count; ++i) lineTo(points[i]);
  if (closed) close();
  return *this;
}

void TCanvasPath::clear() {
  m_verbs.clear();
  m_points.clear();
}

//=============================================================================
// TCanvasMesh

std::size_t TCanvasMesh::triangleCount() const {
  const std::size_t n = positions.size();
  if (mode == Mode::Triangles) return n / 3;
  return n >= 3 ? n - 2 : 0;
}

bool TCanvasMesh::isValid() const {
  const std::size_t n = positions.size();
  if (triangleCount() == 0) return false;
  if (mode == Mode::Triangles && n % 3 != 0) return false;
  if (!colors.empty() && colors.size() != n) return false;
  if (!uvs.empty() && uvs.size() != n) return false;
  return true;
}

//=============================================================================
// TCanvas

TCanvas::TCanvas() : m_transforms(1) {}

TCanvas::~TCanvas() {}

void TCanvas::save() {
  m_transforms.push_back(m_transforms.back());
  onSave();
}

void TCanvas::restore() {
  if (m_transforms.size() <= 1) return;
  m_transforms.pop_back();
  onRestore();
}

void TCanvas::concat(const TCanvasAffine &aff) {
  if (aff.isIdentity()) return;
  m_transforms.back() = m_transforms.back() * aff;
  onConcat(aff);
}

void TCanvas::clipPath(const TCanvasPath &path, TCanvasFillRule rule,
                       TCanvasClipOp op, bool antialias) {
  // Never dropped: clipping to an empty path leaves nothing to draw on,
  // which is not the same as not clipping.
  onClipPath(path, rule, op, antialias);
}

void TCanvas::fillPath(const TCanvasPath &path, TCanvasFillRule rule,
                       const TCanvasPaint &paint) {
  if (path.isEmpty() || paint.color.a <= 0.f) return;
  onFillPath(path, rule, paint);
}

void TCanvas::strokePath(const TCanvasPath &path, const TCanvasPen &pen,
                         const TCanvasPaint &paint) {
  if (path.isEmpty() || paint.color.a <= 0.f || !(pen.width > 0.0)) return;
  onStrokePath(path, pen, paint);
}

void TCanvas::drawMesh(const TCanvasMesh &mesh, const TCanvasPaint &paint) {
  if (!mesh.isValid() || paint.color.a <= 0.f) return;
  onDrawMesh(mesh, paint);
}

void TCanvas::drawImage(const TCanvasImage &image,
                        const TCanvasAffine &imageToLocal,
                        const TCanvasImageOptions &options) {
  if (image.isEmpty() || !(options.opacity > 0.0)) return;
  onDrawImage(image, imageToLocal, options);
}

double TCanvas::pixelSize() const {
  // Same definition as tglGetPixelSize2(): the square root of the area one
  // device pixel covers in current coordinates.
  double det = std::fabs(transform().det());
  if (det < 1e-8) det = 1e-8;
  return std::sqrt(1.0 / det);
}
