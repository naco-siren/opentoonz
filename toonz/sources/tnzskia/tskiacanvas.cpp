#include "canvas/tskiacanvas.h"

#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPathUtils.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkSpan.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkVertices.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/pathops/SkPathOps.h"
#include "include/utils/SkNoDrawCanvas.h"

// Skia promises no stable API. Gradients moved from SkGradientShader to
// SkShaders between milestones 144 and 148; both are supported, because the
// prebuilt Skia and the one built through vcpkg are on either side.
#if __has_include("include/effects/SkGradient.h")
#include "include/effects/SkGradient.h"
#define TNZSKIA_HAS_SKGRADIENT 1
#else
#include "include/effects/SkGradientShader.h"
#define TNZSKIA_HAS_SKGRADIENT 0
#endif

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

//=============================================================================
// Conversions

namespace {

SkPoint toSk(const TCanvasPoint &p) {
  return SkPoint::Make((float)p.x, (float)p.y);
}

SkMatrix toSk(const TCanvasAffine &m) {
  return SkMatrix::MakeAll((float)m.a11, (float)m.a12, (float)m.a13,
                           (float)m.a21, (float)m.a22, (float)m.a23, 0.f, 0.f,
                           1.f);
}

SkColor4f toSk(const TCanvasColor &c) { return {c.r, c.g, c.b, c.a}; }

int toByte(float channel) {
  return (int)std::lround(std::min(std::max(channel, 0.f), 1.f) * 255.f);
}

// The 8-bit colour a vertex carries.
SkColor toSkColor(const TCanvasColor &c) {
  return SkColorSetARGB(toByte(c.a), toByte(c.r), toByte(c.g), toByte(c.b));
}

SkSamplingOptions toSk(TCanvasFilter filter) {
  switch (filter) {
  case TCanvasFilter::Nearest:
    return SkSamplingOptions(SkFilterMode::kNearest);
  case TCanvasFilter::Linear:
    break;
  case TCanvasFilter::Mipmap:
    return SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear);
  }
  return SkSamplingOptions(SkFilterMode::kLinear);
}

// The points go through \p map (if any) in double precision, before they
// are rounded to the single precision Skia works in.
//
// With \p largestFirst the contour that encloses the largest area is put
// first. The order of contours means nothing to a fill, but Skia's "stroke
// and fill" reads the direction of the first contour as the direction of
// the whole shape, which is true of an outer contour and false of a hole.
SkPath makePath(const TCanvasPath &path, TCanvasFillRule rule,
                const TCanvasAffine *map, bool largestFirst = false) {
  const std::vector<TCanvasPath::Verb> &verbs = path.verbs();
  const std::vector<TCanvasPoint> &points     = path.points();

  struct Contour {
    std::size_t verb, point;  // where it starts
  };
  std::vector<Contour> contours;
  std::size_t p = 0;
  for (std::size_t v = 0; v < verbs.size(); ++v) {
    if (verbs[v] == TCanvasPath::MoveTo) contours.push_back({v, p});
    p += verbs[v] == TCanvasPath::QuadTo ? 2 : verbs[v] != TCanvasPath::Close;
  }
  contours.push_back({verbs.size(), points.size()});  // the end

  std::size_t first = 0;
  if (largestFirst) {
    double largest = -1.0;
    for (std::size_t c = 0; c + 1 < contours.size(); ++c) {
      // Twice the area of the polygon through the contour's points, control
      // points included: close enough to tell an outline from its holes.
      const std::size_t begin = contours[c].point, end = contours[c + 1].point;
      double area = 0.0;
      for (std::size_t i = begin; i < end; ++i) {
        const TCanvasPoint &a = points[i];
        const TCanvasPoint &b = points[i + 1 < end ? i + 1 : begin];
        area += a.x * b.y - a.y * b.x;
      }
      if (std::fabs(area) > largest) largest = std::fabs(area), first = c;
    }
  }

  SkPathBuilder builder(rule == TCanvasFillRule::EvenOdd
                            ? SkPathFillType::kEvenOdd
                            : SkPathFillType::kWinding);
  const auto at = [&](std::size_t i) {
    return toSk(map ? *map * points[i] : points[i]);
  };
  const auto addContour = [&](std::size_t c) {
    std::size_t i = contours[c].point;
    // A contour of one point is drawn as a line of no length: stroked with
    // round caps that is a dot, and Skia draws nothing for a bare point.
    if (contours[c + 1].verb - contours[c].verb == 1) {
      builder.moveTo(at(i));
      builder.lineTo(at(i));
      return;
    }
    for (std::size_t v = contours[c].verb; v < contours[c + 1].verb; ++v) {
      switch (verbs[v]) {
      case TCanvasPath::MoveTo:
        builder.moveTo(at(i++));
        break;
      case TCanvasPath::LineTo:
        builder.lineTo(at(i++));
        break;
      case TCanvasPath::QuadTo:
        builder.quadTo(at(i), at(i + 1));
        i += 2;
        break;
      case TCanvasPath::Close:
        builder.close();
        break;
      }
    }
  };
  if (contours.size() > 1) addContour(first);
  for (std::size_t c = 0; c + 1 < contours.size(); ++c)
    if (c != first) addContour(c);
  return builder.detach();
}

// Calls f(a, b, c, sharesFirstEdge) with the vertex indices of each
// triangle. In a strip or a fan the edge a-b of every triangle but the first
// is also an edge of the triangle before it.
template <class F>
void forEachTriangle(const TCanvasMesh &mesh, F f) {
  const std::size_t n = mesh.positions.size();
  switch (mesh.mode) {
  case TCanvasMesh::Mode::Triangles:
    for (std::size_t i = 0; i + 2 < n; i += 3) f(i, i + 1, i + 2, false);
    break;
  case TCanvasMesh::Mode::TriangleStrip:
    for (std::size_t i = 0; i + 2 < n; ++i) f(i, i + 1, i + 2, i > 0);
    break;
  case TCanvasMesh::Mode::TriangleFan:
  case TCanvasMesh::Mode::Polygon:
    for (std::size_t i = 1; i + 1 < n; ++i) f(0, i, i + 1, i > 1);
    break;
  case TCanvasMesh::Mode::Quads:
    // The second triangle of a quad shares the diagonal 0-2 with the first.
    for (std::size_t i = 0; i + 3 < n; i += 4) {
      f(i, i + 1, i + 2, false);
      f(i + 2, i + 3, i, false);
    }
    break;
  }
}

double signedArea2(const TCanvasPoint &a, const TCanvasPoint &b,
                   const TCanvasPoint &c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// One contour per triangle, all wound the same way, so that under the
// non-zero rule they fill as their union. A strip turns some of its
// triangles over where it bends sharply; left as they are, those would
// cancel their neighbours and leave holes.
SkPath unionOfTriangles(const TCanvasMesh &mesh,
                        const std::vector<TCanvasPoint> &positions) {
  SkPathBuilder builder(SkPathFillType::kWinding);
  forEachTriangle(mesh,
                  [&](std::size_t a, std::size_t b, std::size_t c, bool) {
                    const double area =
                        signedArea2(positions[a], positions[b], positions[c]);
                    if (area == 0.0) return;
                    if (area < 0.0) std::swap(b, c);
                    builder.moveTo(toSk(positions[a]));
                    builder.lineTo(toSk(positions[b]));
                    builder.lineTo(toSk(positions[c]));
                    builder.close();
                  });
  return builder.detach();
}

sk_sp<SkImage> makeImage(const TCanvasImage &image) {
  if (image.isEmpty() || image.rowBytes < (std::ptrdiff_t)image.width * 4)
    return nullptr;
  const SkImageInfo info = SkImageInfo::Make(
      image.width, image.height, kBGRA_8888_SkColorType,
      image.premultiplied ? kPremul_SkAlphaType : kUnpremul_SkAlphaType);
  // Not copied: the image is used up before the call that got it returns.
  return SkImages::RasterFromPixmap(
      SkPixmap(info, image.pixels, (size_t)image.rowBytes), nullptr, nullptr);
}

sk_sp<SkShader> makeGradient(const TCanvasShader &shader,
                             const SkMatrix *localMatrix) {
  std::vector<SkColor4f> colors;
  std::vector<float> offsets;
  for (const TCanvasGradientStop &stop : shader.stops) {
    colors.push_back(toSk(stop.color));
    offsets.push_back((float)stop.offset);
  }
  const bool linear    = shader.kind == TCanvasShader::Kind::LinearGradient;
  const SkPoint ends[] = {toSk(shader.p0), toSk(shader.p1)};
  const float radius   = (float)shader.radius;
  if (!linear && !(radius > 0.f)) return nullptr;

#if TNZSKIA_HAS_SKGRADIENT
  const SkGradient gradient(
      SkGradient::Colors(SkSpan<const SkColor4f>(colors),
                         SkSpan<const float>(offsets), SkTileMode::kClamp),
      SkGradient::Interpolation());
  return linear
             ? SkShaders::LinearGradient(ends, gradient, localMatrix)
             : SkShaders::RadialGradient(ends[0], radius, gradient,
                                         localMatrix);
#else
  const int count = (int)colors.size();
  return linear ? SkGradientShader::MakeLinear(
                      ends, colors.data(), nullptr, offsets.data(), count,
                      SkTileMode::kClamp, 0, localMatrix)
                : SkGradientShader::MakeRadial(
                      ends[0], radius, colors.data(), nullptr, offsets.data(),
                      count, SkTileMode::kClamp, 0, localMatrix);
#endif
}

// \p shaderSpace maps the coordinates the shader is given in to the ones the
// shape is drawn in. It is null when those are the same, and the current
// transform when the shape has been taken to device coordinates.
sk_sp<SkShader> makeShader(const TCanvasShader &shader,
                           const TCanvasAffine *shaderSpace) {
  const std::optional<SkMatrix> space =
      shaderSpace ? std::optional<SkMatrix>(toSk(*shaderSpace)) : std::nullopt;

  switch (shader.kind) {
  case TCanvasShader::Kind::None:
    return nullptr;
  case TCanvasShader::Kind::LinearGradient:
  case TCanvasShader::Kind::RadialGradient:
    // Fewer than two stops is not a gradient; makePaint() handles those.
    if (shader.stops.size() < 2) return nullptr;
    return makeGradient(shader, space ? &*space : nullptr);
  case TCanvasShader::Kind::Image: {
    const sk_sp<SkImage> image = makeImage(shader.image);
    if (!image) return nullptr;
    const SkTileMode tile = shader.tile == TCanvasTile::Repeat
                                ? SkTileMode::kRepeat
                                : SkTileMode::kClamp;
    SkMatrix imageToShape = toSk(shader.imageToLocal);
    if (space) imageToShape.postConcat(*space);
    return image->makeShader(tile, tile, toSk(shader.filter), &imageToShape);
  }
  }
  return nullptr;
}

SkPaint makePaint(const TCanvasPaint &paint, bool antialias,
                  const TCanvasAffine *shaderSpace) {
  SkPaint skPaint;
  skPaint.setAntiAlias(antialias);

  TCanvasColor color = paint.color;
  const TCanvasShader &shader = paint.shader;
  const bool gradient = shader.kind == TCanvasShader::Kind::LinearGradient ||
                        shader.kind == TCanvasShader::Kind::RadialGradient;
  if (gradient && shader.stops.size() == 1) {
    // A gradient of one colour is that colour.
    color   = shader.stops[0].color;
    color.a = color.a * paint.color.a;
  } else if (sk_sp<SkShader> skShader = makeShader(shader, shaderSpace)) {
    // With a shader the paint contributes its alpha only.
    skPaint.setShader(std::move(skShader));
    color = TCanvasColor(1.f, 1.f, 1.f, paint.color.a);
  }
  skPaint.setColor4f(toSk(color), nullptr);
  return skPaint;
}

}  // namespace

//=============================================================================
// TSkiaCanvas::Imp

struct TSkiaCanvas::Imp {
  sk_sp<SkSurface> surface;
  std::unique_ptr<SkCanvas> nothing;  // what is drawn on without a surface
  SkCanvas *canvas = nullptr;
  int width = 0, height = 0;

  // A fill that is also a clip (fillPathAndClip) is a layer: filled whole
  // with the paint, drawn on, and cut to the shape of the path when the
  // clip ends. This is what is kept of each until then, for every save()
  // not yet restored and, at the front, for the canvas itself.
  struct Cutout {
    TCanvasPath path;
    TCanvasFillRule rule;
    bool antialias;
    double growDevice;
    TCanvasAffine transform;
  };
  std::vector<std::vector<Cutout>> cutouts{1};
  void beginCutout(const TCanvasPath &path, TCanvasFillRule rule,
                   const TCanvasPaint &paint, const TCanvasAffine &transform);
  void endCutouts(std::vector<Cutout> &ended);

  template <class MakePath>
  void fill(MakePath makeShape, bool evenOdd, const TCanvasPaint &paint,
            bool antialias, const TCanvasAffine &transform);

  void drawShadedMesh(const TCanvasMesh &mesh, const TCanvasPaint &paint,
                      const TCanvasAffine &transform);
};

// Fills the path makeShape(map) returns: with a null map in the coordinates
// the shape was given in, otherwise taken through the map.
template <class MakePath>
void TSkiaCanvas::Imp::fill(MakePath makeShape, bool evenOdd,
                            const TCanvasPaint &paint, bool antialias,
                            const TCanvasAffine &transform) {
  if (!(paint.growDevice > 0.0)) {
    canvas->drawPath(makeShape(nullptr), makePaint(paint, antialias, nullptr));
    return;
  }

  // Growing by a number of pixels is done in device coordinates, where it
  // is the same in every direction whatever the transform.
  const SkPath shape = makeShape(&transform);
  SkPaint skPaint    = makePaint(paint, antialias, &transform);
  skPaint.setStrokeWidth((float)(2.0 * paint.growDevice));
  skPaint.setStrokeJoin(SkPaint::kRound_Join);

  SkAutoCanvasRestore restore(canvas, true);
  canvas->resetMatrix();

  // The shape and the band around its outline have to be covered as one
  // area, or a translucent paint is applied twice where they overlap.
  if (!evenOdd) {
    // "Stroke and fill" does that, for a path filled by winding number
    // whose first contour is an outer one.
    skPaint.setStyle(SkPaint::kStrokeAndFill_Style);
    canvas->drawPath(shape, skPaint);
    return;
  }

  // It does not for the even-odd rule. There the band is made into a path
  // of its own and united with the shape.
  skPaint.setStyle(SkPaint::kStroke_Style);
  const SkPath band = skpathutils::FillPathWithPaint(shape, skPaint);
  skPaint.setStyle(SkPaint::kFill_Style);
  if (const std::optional<SkPath> grown = Op(shape, band, kUnion_SkPathOp)) {
    canvas->drawPath(*grown, skPaint);
  } else {
    // Skia could not unite them. Drawn one after the other they are
    // slightly too opaque where both cover part of a pixel.
    canvas->drawPath(shape, skPaint);
    canvas->drawPath(band, skPaint);
  }
}

void TSkiaCanvas::Imp::beginCutout(const TCanvasPath &path,
                                   TCanvasFillRule rule,
                                   const TCanvasPaint &paint,
                                   const TCanvasAffine &transform) {
  // No larger than it has to be: the path in device coordinates, control
  // points and all, with room for the growth and for smooth edges.
  SkRect bounds = SkRect::MakeEmpty();
  bool first    = true;
  for (const TCanvasPoint &p : path.points()) {
    const SkPoint at = toSk(transform * p);
    if (first) bounds.setLTRB(at.x(), at.y(), at.x(), at.y()), first = false;
    bounds.setLTRB(std::min(bounds.left(), at.x()),
                   std::min(bounds.top(), at.y()),
                   std::max(bounds.right(), at.x()),
                   std::max(bounds.bottom(), at.y()));
  }
  const float room = (float)std::max(paint.growDevice, 0.0) + 2.f;
  bounds.outset(room, room);

  const SkMatrix matrix = canvas->getTotalMatrix();
  canvas->save();
  canvas->resetMatrix();
  canvas->saveLayer(&bounds, nullptr);
  canvas->setMatrix(matrix);
  // What is set from here on (transforms, further clips) ends before the
  // layer is cut.
  canvas->save();

  // The fill, all over: its shape comes from the cut.
  TCanvasPaint whole = paint;
  whole.growDevice   = 0.0;
  canvas->drawPaint(makePaint(whole, false, nullptr));

  cutouts.back().push_back(
      {path, rule, paint.antialias, paint.growDevice, transform});
}

void TSkiaCanvas::Imp::endCutouts(std::vector<Cutout> &ended) {
  for (auto cutout = ended.rbegin(); cutout != ended.rend(); ++cutout) {
    canvas->restore();

    // Kept: where the shape is, by as much as it covers each pixel.
    SkPaint keep;
    keep.setBlendMode(SkBlendMode::kDstIn);
    canvas->saveLayer(nullptr, &keep);
    canvas->setMatrix(toSk(cutout->transform));
    TCanvasPaint shape(TCanvasColor(0.f, 0.f, 0.f, 1.f));
    shape.growDevice = cutout->growDevice;
    fill(
        [&](const TCanvasAffine *map) {
          return makePath(cutout->path, cutout->rule, map, map != nullptr);
        },
        cutout->rule == TCanvasFillRule::EvenOdd, shape, cutout->antialias,
        cutout->transform);
    canvas->restore();

    canvas->restore();  // the layer, onto what is underneath
    canvas->restore();
  }
  ended.clear();
}

// The affine map that takes the corners \p from to the corners \p to,
// or false if \p from are on one line. (SkMatrix::setPolyToPoly() does
// this too, but is not called the same in every Skia this builds with.)
bool mapTriangle(const SkPoint from[3], const SkPoint to[3], SkMatrix &map) {
  const double ax = from[1].fX - from[0].fX, ay = from[1].fY - from[0].fY;
  const double bx = from[2].fX - from[0].fX, by = from[2].fY - from[0].fY;
  const double det = ax * by - ay * bx;
  if (!(std::abs(det) > 1e-12)) return false;
  // The inverse of the columns (a, b), times the corners' offsets.
  const double i11 = by / det, i12 = -bx / det, i21 = -ay / det,
               i22 = ax / det;
  const double cx = to[1].fX - to[0].fX, cy = to[1].fY - to[0].fY;
  const double dx = to[2].fX - to[0].fX, dy = to[2].fY - to[0].fY;
  const double m11 = cx * i11 + dx * i21, m12 = cx * i12 + dx * i22;
  const double m21 = cy * i11 + dy * i21, m22 = cy * i12 + dy * i22;
  const double tx = to[0].fX - (m11 * from[0].fX + m12 * from[0].fY);
  const double ty = to[0].fY - (m21 * from[0].fX + m22 * from[0].fY);
  map.setAll((float)m11, (float)m12, (float)tx, (float)m21, (float)m22,
             (float)ty, 0.f, 0.f, 1.f);
  return true;
}

// A mesh whose colour changes from vertex to vertex. Skia's rasteriser
// draws such triangles without antialiasing and blends each one separately,
// while the mesh is meant to be one shape: smooth at its silhouette and
// painted once where its triangles overlap. So:
//
// - the triangles go into a layer, each replacing what is there, and the
//   layer is blended once;
// - for smooth edges the layer is clipped to the antialiased union of the
//   triangles. A pixel the union covers in part may have its centre outside
//   every triangle, and would then be left empty in the layer. Underneath
//   the triangles the layer is therefore first filled along every edge and
//   around every vertex with the colours found there, two pixels wide.
void TSkiaCanvas::Imp::drawShadedMesh(const TCanvasMesh &mesh,
                                      const TCanvasPaint &paint,
                                      const TCanvasAffine &transform) {
  const std::size_t count = mesh.positions.size();
  std::vector<TCanvasPoint> device(count);
  for (std::size_t i = 0; i < count; ++i)
    device[i] = transform * mesh.positions[i];

  // The shader is looked up at the texture coordinates, or at the position
  // the vertex had before the transform; either way not at where the vertex
  // is drawn, so no shader space is needed.
  const sk_sp<SkShader> shader = makeShader(paint.shader, nullptr);
  const bool hasColors         = !mesh.colors.empty();
  const std::vector<TCanvasPoint> &lookups =
      mesh.uvs.empty() ? mesh.positions : mesh.uvs;
  const bool smooth = paint.antialias && mesh.smoothEdges;

  // Skia blends vertex colours after multiplying them by their alpha.
  // Between a colour and a more transparent one of another hue that gives
  // a different blend from the one the old renderer drew, which blended
  // colour and alpha each on its own. Where the alphas differ, the two are
  // therefore drawn apart: the colours opaque, the alphas as a mask on
  // top.
  bool apart = false;
  if (hasColors && !shader)
    for (const TCanvasColor &color : mesh.colors)
      if (color.a != mesh.colors[0].a) {
        apart = true;
        break;
      }

  std::vector<SkPoint> positions, texs;
  std::vector<SkColor> colors, alphas;
  const auto add = [&](const TCanvasPoint &at, std::size_t source) {
    positions.push_back(toSk(at));
    if (shader) texs.push_back(toSk(lookups[source]));
    if (!hasColors) return;
    const SkColor color = toSkColor(mesh.colors[source]);
    if (apart) {
      colors.push_back(SkColorSetA(color, 0xFF));
      alphas.push_back(SkColorSetARGB(SkColorGetA(color), 0, 0, 0));
    } else {
      colors.push_back(color);
    }
  };
  const auto addQuad = [&](const TCanvasPoint &p0, std::size_t s0,
                           const TCanvasPoint &p1, std::size_t s1,
                           const TCanvasPoint &p2, std::size_t s2,
                           const TCanvasPoint &p3, std::size_t s3) {
    add(p0, s0), add(p1, s1), add(p2, s2);
    add(p0, s0), add(p2, s2), add(p3, s3);
  };

  // A triangle that is transparent at all three corners draws nothing. It
  // must not get as far as the layer, where it would replace what is under
  // it with nothing: drawing code written for OpenGL joins the pieces of a
  // strip with such triangles, across pieces that can be seen.
  const auto drawsNothing = [&](std::size_t a, std::size_t b, std::size_t c) {
    if (signedArea2(device[a], device[b], device[c]) == 0.0) return true;
    return hasColors && !(mesh.colors[a].a > 0.f) &&
           !(mesh.colors[b].a > 0.f) && !(mesh.colors[c].a > 0.f);
  };

  if (smooth) {
    const double reach = 1.0;  // pixels; more than half a pixel's diagonal
    std::vector<char> vertexDone(count, 0);
    const auto addVertex = [&](std::size_t v) {
      if (vertexDone[v]) return;
      vertexDone[v]         = 1;
      const TCanvasPoint &p = device[v];
      addQuad({p.x - reach, p.y - reach}, v, {p.x + reach, p.y - reach}, v,
              {p.x + reach, p.y + reach}, v, {p.x - reach, p.y + reach}, v);
    };
    forEachTriangle(mesh,
                    [&](std::size_t a, std::size_t b, std::size_t c, bool) {
                      if (drawsNothing(a, b, c)) return;
                      addVertex(a), addVertex(b), addVertex(c);
                    });

    const auto addEdge = [&](std::size_t v0, std::size_t v1) {
      const TCanvasPoint &p0 = device[v0], &p1 = device[v1];
      const double dx = p1.x - p0.x, dy = p1.y - p0.y;
      const double length = std::sqrt(dx * dx + dy * dy);
      if (!(length > 0.0)) return;
      const double nx = -dy / length * reach, ny = dx / length * reach;
      addQuad({p0.x + nx, p0.y + ny}, v0, {p0.x - nx, p0.y - ny}, v0,
              {p1.x - nx, p1.y - ny}, v1, {p1.x + nx, p1.y + ny}, v1);
    };
    forEachTriangle(mesh, [&](std::size_t a, std::size_t b, std::size_t c,
                              bool sharesFirstEdge) {
      if (drawsNothing(a, b, c)) return;
      if (!sharesFirstEdge) addEdge(a, b);
      addEdge(b, c);
      addEdge(c, a);
    });
  }

  forEachTriangle(mesh, [&](std::size_t a, std::size_t b, std::size_t c, bool) {
    if (drawsNothing(a, b, c)) return;
    add(device[a], a), add(device[b], b), add(device[c], c);
  });
  if (positions.empty()) return;

  const sk_sp<SkVertices> vertices = SkVertices::MakeCopy(
      SkVertices::kTriangles_VertexMode, (int)positions.size(),
      positions.data(), shader ? texs.data() : nullptr,
      hasColors ? colors.data() : nullptr);
  if (!vertices) return;

  // Inside the layer: opaque, replacing. The paint's alpha is applied when
  // the layer is blended.
  SkPaint inside;
  inside.setBlendMode(SkBlendMode::kSrc);
  inside.setColor4f({paint.color.r, paint.color.g, paint.color.b, 1.f},
                    nullptr);
  if (shader) inside.setShader(shader);
  // How a vertex colour meets the shader: multiplied with it. Without a
  // shader the vertex colour is used as it is.
  const SkBlendMode mix = shader ? SkBlendMode::kModulate : SkBlendMode::kDst;

  SkPaint layer;
  layer.setAlphaf(std::min(std::max(paint.color.a, 0.f), 1.f));

  SkRect bounds = SkRect::MakeEmpty();
  for (const TCanvasPoint &p : device) {
    const SkRect around = SkRect::MakeLTRB(
        (float)p.x - 2.f, (float)p.y - 2.f, (float)p.x + 2.f, (float)p.y + 2.f);
    bounds.join(around);
  }

  SkAutoCanvasRestore restore(canvas, true);
  canvas->resetMatrix();
  if (smooth)
    canvas->clipPath(unionOfTriangles(mesh, device), SkClipOp::kIntersect,
                     true);
  canvas->saveLayer(&bounds, &layer);

  // Skia's rasteriser picks the mipmap level of an image once for a whole
  // drawVertices() call, from the canvas transform: not from how each
  // triangle maps the image, which for a mesh is what its texture
  // coordinates say. It would sample the image at full size however small
  // the mesh shows it. A mesh that asks for mipmaps is therefore drawn a
  // triangle at a time, each with the image placed where that triangle
  // puts it - when its vertices are all of one colour, which can then be
  // applied as a colour filter. A triangle that shows a line or a point of
  // the image (the margin under smooth edges) has no such placement and is
  // drawn with drawVertices() as before.
  bool sameColour = true;
  for (const SkColor color : colors)
    if (color != colors[0]) {
      sameColour = false;
      break;
    }
  if (shader && paint.shader.filter == TCanvasFilter::Mipmap && sameColour) {
    SkPaint each = inside;
    each.setAntiAlias(false);
    if (!colors.empty())
      each.setColorFilter(
          SkColorFilters::Blend(colors[0], SkBlendMode::kModulate));
    // Those first: they are the margin under smooth edges, which the
    // triangles are drawn over.
    std::vector<SkPoint> restPositions, restTexs;
    std::vector<SkColor> restColors;
    std::vector<std::pair<std::size_t, SkMatrix>> placed;
    for (std::size_t i = 0; i + 2 < positions.size(); i += 3) {
      SkMatrix texToDevice;
      if (mapTriangle(&texs[i], &positions[i], texToDevice)) {
        placed.emplace_back(i, texToDevice);
        continue;
      }
      for (std::size_t k = i; k < i + 3; ++k) {
        restPositions.push_back(positions[k]);
        restTexs.push_back(texs[k]);
        if (!colors.empty()) restColors.push_back(colors[k]);
      }
    }
    if (!restPositions.empty())
      if (const sk_sp<SkVertices> rest = SkVertices::MakeCopy(
              SkVertices::kTriangles_VertexMode, (int)restPositions.size(),
              restPositions.data(), restTexs.data(),
              restColors.empty() ? nullptr : restColors.data()))
        canvas->drawVertices(rest, mix, inside);
    for (const auto &triangle : placed) {
      const std::size_t i = triangle.first;
      each.setShader(shader->makeWithLocalMatrix(triangle.second));
      SkPathBuilder path;
      path.moveTo(positions[i]);
      path.lineTo(positions[i + 1]);
      path.lineTo(positions[i + 2]);
      path.close();
      canvas->drawPath(path.detach(), each);
    }
    return;
  }

  canvas->drawVertices(vertices, mix, inside);
  if (!apart) return;

  // The alphas, in a layer of their own that then keeps of the colours
  // what it is opaque for.
  const sk_sp<SkVertices> mask = SkVertices::MakeCopy(
      SkVertices::kTriangles_VertexMode, (int)positions.size(),
      positions.data(), nullptr, alphas.data());
  if (!mask) return;
  SkPaint keep;
  keep.setBlendMode(SkBlendMode::kDstIn);
  canvas->saveLayer(&bounds, &keep);
  canvas->drawVertices(mask, mix, inside);
}

//=============================================================================
// TSkiaCanvas

TSkiaCanvas::TSkiaCanvas(void *pixels, int width, int height,
                         std::ptrdiff_t rowBytes)
    : m_imp(new Imp) {
  m_imp->width  = std::max(width, 0);
  m_imp->height = std::max(height, 0);

  if (pixels && width > 0 && height > 0 &&
      rowBytes >= (std::ptrdiff_t)width * 4) {
    // No colour space: colours are numbers that go into the pixels as they
    // are, like everywhere else in the renderer today.
    //
    // No flip either. Skia calls the first row in memory y = 0 and so does
    // a TRaster, which only happens to show that row at the bottom.
    const SkImageInfo info = SkImageInfo::Make(
        width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    m_imp->surface = SkSurfaces::WrapPixels(info, pixels, (size_t)rowBytes);
  }

  if (m_imp->surface) {
    m_imp->canvas = m_imp->surface->getCanvas();
  } else {
    m_imp->nothing.reset(new SkNoDrawCanvas(m_imp->width, m_imp->height));
    m_imp->canvas = m_imp->nothing.get();
  }
}

TSkiaCanvas::~TSkiaCanvas() {
  // What was begun and never restored ends here.
  while (!m_imp->cutouts.empty()) {
    m_imp->endCutouts(m_imp->cutouts.back());
    m_imp->cutouts.pop_back();
  }
}

bool TSkiaCanvas::isValid() const { return m_imp->surface != nullptr; }

TCanvasIRect TSkiaCanvas::deviceBounds() const {
  return TCanvasIRect(0, 0, m_imp->width, m_imp->height);
}

void TSkiaCanvas::onSave() {
  m_imp->canvas->save();
  m_imp->cutouts.emplace_back();
}

void TSkiaCanvas::onRestore() {
  m_imp->endCutouts(m_imp->cutouts.back());
  m_imp->cutouts.pop_back();
  m_imp->canvas->restore();
}

void TSkiaCanvas::onConcat(const TCanvasAffine &aff) {
  m_imp->canvas->concat(toSk(aff));
}

void TSkiaCanvas::onClipPath(const TCanvasPath &path, TCanvasFillRule rule,
                             TCanvasClipOp op, bool antialias) {
  m_imp->canvas->clipPath(makePath(path, rule, nullptr),
                          op == TCanvasClipOp::Intersect
                              ? SkClipOp::kIntersect
                              : SkClipOp::kDifference,
                          antialias);
}

void TSkiaCanvas::onClipMesh(const TCanvasMesh &mesh, TCanvasClipOp op) {
  m_imp->canvas->clipPath(unionOfTriangles(mesh, mesh.positions),
                          op == TCanvasClipOp::Intersect
                              ? SkClipOp::kIntersect
                              : SkClipOp::kDifference,
                          false);
}

void TSkiaCanvas::onFillPath(const TCanvasPath &path, TCanvasFillRule rule,
                             const TCanvasPaint &paint) {
  m_imp->fill(
      [&](const TCanvasAffine *map) {
        return makePath(path, rule, map, map != nullptr);
      },
      rule == TCanvasFillRule::EvenOdd, paint, paint.antialias, transform());
}

void TSkiaCanvas::onFillPathAndClip(const TCanvasPath &path,
                                    TCanvasFillRule rule,
                                    const TCanvasPaint &paint) {
  m_imp->beginCutout(path, rule, paint, transform());
}

void TSkiaCanvas::onStrokePath(const TCanvasPath &path, const TCanvasPen &pen,
                               const TCanvasPaint &paint) {
  SkCanvas *canvas = m_imp->canvas;
  // A width in pixels is a width in device coordinates: the path is taken
  // there, and stroked without a transform that could distort the line.
  const bool device           = pen.unit == TCanvasPen::Unit::Device;
  const TCanvasAffine *toDevice = device ? &transform() : nullptr;

  SkPaint skPaint = makePaint(paint, paint.antialias, toDevice);
  skPaint.setStyle(SkPaint::kStroke_Style);
  skPaint.setStrokeWidth((float)pen.width);
  skPaint.setStrokeCap(pen.cap == TCanvasPen::Cap::Round ? SkPaint::kRound_Cap
                                                         : SkPaint::kButt_Cap);
  skPaint.setStrokeJoin(pen.join == TCanvasPen::Join::Round
                            ? SkPaint::kRound_Join
                            : SkPaint::kMiter_Join);

  if (!pen.dash.empty()) {
    const double unit = device ? 1.0 : pixelSize();
    std::vector<float> intervals;
    for (double length : pen.dash)
      intervals.push_back((float)(std::max(length, 0.0) * unit));
    // Dash, gap, dash, gap: an odd list is gone through twice.
    if (intervals.size() % 2 != 0) {
      const std::vector<float> once = intervals;
      intervals.insert(intervals.end(), once.begin(), once.end());
    }
    // Null (and then a solid line) when the lengths add up to nothing.
    skPaint.setPathEffect(
        SkDashPathEffect::Make(SkSpan<const float>(intervals), 0.f));
  }

  const SkPath skPath = makePath(path, TCanvasFillRule::NonZero, toDevice);
  if (device) {
    SkAutoCanvasRestore restore(canvas, true);
    canvas->resetMatrix();
    canvas->drawPath(skPath, skPaint);
  } else {
    canvas->drawPath(skPath, skPaint);
  }
}

void TSkiaCanvas::onDrawMesh(const TCanvasMesh &mesh,
                             const TCanvasPaint &paint) {
  const bool shaded =
      !mesh.colors.empty() ||
      (!mesh.uvs.empty() && paint.shader.kind != TCanvasShader::Kind::None);
  // The same colour at every vertex is one colour, said at length: what a
  // style does that wants its mesh hard-edged and blended on its own.
  TCanvasPaint flat = paint;
  bool isFlat       = paint.shader.kind == TCanvasShader::Kind::None;
  if (isFlat && !mesh.colors.empty()) {
    const TCanvasColor &first = mesh.colors[0];
    for (const TCanvasColor &color : mesh.colors)
      if (color.r != first.r || color.g != first.g || color.b != first.b ||
          color.a != first.a) {
        isFlat = false;
        break;
      }
    if (isFlat) {
      flat.color   = first;
      flat.color.a = first.a * paint.color.a;
    }
  }
  if (shaded && !isFlat) {
    m_imp->drawShadedMesh(mesh, paint, transform());
    return;
  }
  const TCanvasPaint &shapePaint = isFlat ? flat : paint;

  // One colour, or a shader looked up where the mesh is: the mesh is just a
  // shape, the union of its triangles.
  m_imp->fill(
      [&](const TCanvasAffine *map) {
        if (!map) return unionOfTriangles(mesh, mesh.positions);
        std::vector<TCanvasPoint> mapped;
        for (const TCanvasPoint &p : mesh.positions)
          mapped.push_back(*map * p);
        return unionOfTriangles(mesh, mapped);
      },
      false, shapePaint, paint.antialias && mesh.smoothEdges, transform());
}

void TSkiaCanvas::onDrawImage(const TCanvasImage &image,
                              const TCanvasAffine &imageToLocal,
                              const TCanvasImageOptions &options) {
  const sk_sp<SkImage> skImage = makeImage(image);
  if (!skImage) return;

  SkPaint skPaint;
  skPaint.setAntiAlias(true);  // the edges of an image that is turned
  skPaint.setAlphaf((float)std::min(options.opacity, 1.0));

  SkCanvas *canvas = m_imp->canvas;
  SkAutoCanvasRestore restore(canvas, true);
  canvas->concat(toSk(imageToLocal));
  canvas->drawImage(skImage, 0.f, 0.f, toSk(options.filter), &skPaint);
}
