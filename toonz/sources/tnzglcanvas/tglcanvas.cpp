#include "canvas/tglcanvas.h"

#include "tgl.h"
#include "tstencilcontrol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>

#ifndef CALLBACK
#define CALLBACK
#endif

// Part of OpenGL since 1.2, which the header of Windows predates.
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

//=============================================================================
// Geometry

namespace {

struct Contour {
  std::vector<TCanvasPoint> points;
  bool closed = false;
};

// The path as polylines. A curve is cut into as many pieces as it takes to
// stay within \p tolerance of it.
std::vector<Contour> flatten(const TCanvasPath &path, double tolerance) {
  std::vector<Contour> contours;
  const std::vector<TCanvasPoint> &points = path.points();
  std::size_t p                           = 0;
  for (TCanvasPath::Verb verb : path.verbs()) {
    switch (verb) {
    case TCanvasPath::MoveTo:
      contours.push_back(Contour());
      contours.back().points.push_back(points[p++]);
      break;
    case TCanvasPath::LineTo:
      contours.back().points.push_back(points[p++]);
      break;
    case TCanvasPath::QuadTo: {
      const TCanvasPoint a = contours.back().points.back();
      const TCanvasPoint b = points[p], c = points[p + 1];
      p += 2;
      // A piece 1/n of the curve long strays from its chord by at most
      // |a - 2b + c| / (4 n^2).
      const double bend =
          std::hypot(a.x - 2.0 * b.x + c.x, a.y - 2.0 * b.y + c.y);
      const int pieces = (int)std::min(
          std::max(std::ceil(std::sqrt(bend / (4.0 * tolerance))), 1.0), 256.0);
      for (int i = 1; i <= pieces; ++i) {
        const double t = (double)i / pieces, s = 1.0 - t;
        contours.back().points.push_back(
            {s * s * a.x + 2.0 * s * t * b.x + t * t * c.x,
             s * s * a.y + 2.0 * s * t * b.y + t * t * c.y});
      }
      break;
    }
    case TCanvasPath::Close:
      contours.back().closed = true;
      break;
    }
  }
  return contours;
}

//-----------------------------------------------------------------------------
// GLU tessellation. Every vertex is sent as position and as texture
// coordinate: a paint with a shader looks the shader up where the shape is.

typedef std::deque<std::array<GLdouble, 3>> TessVertices;

extern "C" {
static void CALLBACK tessBegin(GLenum type) { glBegin(type); }
static void CALLBACK tessEnd() { glEnd(); }
static void CALLBACK tessVertex(void *vertex) {
  glTexCoord2dv((const GLdouble *)vertex);
  glVertex2dv((const GLdouble *)vertex);
}
// Where contours cross, GLU asks for a vertex of its own.
static void CALLBACK tessCombine(GLdouble coords[3], void *[4], GLfloat[4],
                                 void **out, void *polygonData) {
  TessVertices &vertices = *(TessVertices *)polygonData;
  vertices.push_back({coords[0], coords[1], 0.0});
  *out = vertices.back().data();
}
}

#ifdef _WIN32
typedef GLvoid(CALLBACK *GluCallback)(void);
#else
typedef GLvoid (*GluCallback)();
#endif

void tessellate(const std::vector<Contour> &contours, TCanvasFillRule rule) {
  GLUtesselator *tess = gluNewTess();
  if (!tess) return;

  TessVertices vertices;  // a deque: GLU keeps pointers into it
  gluTessCallback(tess, GLU_TESS_BEGIN, (GluCallback)tessBegin);
  gluTessCallback(tess, GLU_TESS_END, (GluCallback)tessEnd);
  gluTessCallback(tess, GLU_TESS_VERTEX, (GluCallback)tessVertex);
  gluTessCallback(tess, GLU_TESS_COMBINE_DATA, (GluCallback)tessCombine);
  gluTessProperty(tess, GLU_TESS_WINDING_RULE,
                  rule == TCanvasFillRule::NonZero ? GLU_TESS_WINDING_NONZERO
                                                   : GLU_TESS_WINDING_ODD);

  gluTessBeginPolygon(tess, &vertices);
  for (const Contour &contour : contours) {
    gluTessBeginContour(tess);
    for (const TCanvasPoint &point : contour.points) {
      vertices.push_back({point.x, point.y, 0.0});
      gluTessVertex(tess, vertices.back().data(), vertices.back().data());
    }
    gluTessEndContour(tess);
  }
  gluTessEndPolygon(tess);
  gluDeleteTess(tess);
}

void drawLines(const std::vector<Contour> &contours) {
  for (const Contour &contour : contours) {
    glBegin(contour.closed ? GL_LINE_LOOP : GL_LINE_STRIP);
    for (const TCanvasPoint &point : contour.points) {
      glTexCoord2d(point.x, point.y);
      glVertex2d(point.x, point.y);
    }
    glEnd();
  }
}

//-----------------------------------------------------------------------------
// Matrices, column by column as OpenGL keeps them

void toGl(const TCanvasAffine &aff, GLdouble m[16]) {
  const GLdouble values[16] = {aff.a11, aff.a21, 0.0, 0.0, aff.a12, aff.a22,
                               0.0,     0.0,     0.0, 0.0, 1.0,     0.0,
                               aff.a13, aff.a23, 0.0, 1.0};
  std::memcpy(m, values, sizeof(values));
}

void multiply(GLdouble out[16], const GLdouble a[16], const GLdouble b[16]) {
  GLdouble result[16];
  for (int column = 0; column < 4; ++column)
    for (int row = 0; row < 4; ++row) {
      double sum = 0.0;
      for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[column * 4 + k];
      result[column * 4 + row] = sum;
    }
  std::memcpy(out, result, sizeof(result));
}

TCanvasAffine inverse(const TCanvasAffine &m) {
  const double det = m.det();
  if (det == 0.0) return TCanvasAffine();
  const double a = m.a22 / det, b = -m.a12 / det;
  const double c = -m.a21 / det, d = m.a11 / det;
  return TCanvasAffine(a, b, -(a * m.a13 + b * m.a23), c, d,
                       -(c * m.a13 + d * m.a23));
}

//=============================================================================
// Paint

// Everything a call changes is put back when the call is over, except what
// TStencilControl keeps track of itself.
struct ScopedAttributes {
  ScopedAttributes() {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
  }
  ~ScopedAttributes() {
    glPopClientAttrib();
    glPopAttrib();
  }
};

TCanvasColor colorAt(const std::vector<TCanvasGradientStop> &stops,
                     double offset) {
  if (offset <= stops.front().offset) return stops.front().color;
  for (std::size_t i = 1; i < stops.size(); ++i) {
    if (offset > stops[i].offset) continue;
    const TCanvasGradientStop &a = stops[i - 1], &b = stops[i];
    const double span = b.offset - a.offset;
    const float t     = span > 0.0 ? (float)((offset - a.offset) / span) : 1.f;
    return TCanvasColor(
        a.color.r + (b.color.r - a.color.r) * t,
        a.color.g + (b.color.g - a.color.g) * t,
        a.color.b + (b.color.b - a.color.b) * t,
        a.color.a + (b.color.a - a.color.a) * t);
  }
  return stops.back().color;
}

void putBgra(unsigned char *pixel, const TCanvasColor &color) {
  const auto byte = [](float channel) {
    return (unsigned char)std::lround(std::min(std::max(channel, 0.f), 1.f) *
                                      255.f);
  };
  pixel[0] = byte(color.b), pixel[1] = byte(color.g);
  pixel[2] = byte(color.r), pixel[3] = byte(color.a);
}

GLuint makeTexture(int width, int height, const void *bgra,
                   std::ptrdiff_t rowBytes, TCanvasFilter filter,
                   TCanvasTile tile) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  const GLint glFilter = filter == TCanvasFilter::Nearest ? GL_NEAREST
                                                          : GL_LINEAR;
  const GLint wrap = tile == TCanvasTile::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

  // Rows bottom up is also what OpenGL takes: no flip.
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(rowBytes / 4));
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA,
               GL_UNSIGNED_BYTE, bgra);
  return texture;
}

// Sets up OpenGL for a paint: the colour and, for a shader, a texture with
// a texture matrix that takes the point a vertex is looked up at (sent as
// its texture coordinate) to where in the texture that is.
class BoundPaint {
public:
  explicit BoundPaint(const TCanvasPaint &paint) {
    TCanvasColor color          = paint.color;
    const TCanvasShader &shader = paint.shader;
    TCanvasAffine lookup;

    switch (shader.kind) {
    case TCanvasShader::Kind::None:
      break;
    case TCanvasShader::Kind::LinearGradient:
    case TCanvasShader::Kind::RadialGradient:
      if (shader.stops.size() == 1) {
        color   = shader.stops[0].color;
        color.a = color.a * paint.color.a;
      } else if (shader.stops.size() >= 2) {
        if (shader.kind == TCanvasShader::Kind::LinearGradient)
          makeLinearGradient(shader, lookup);
        else
          makeRadialGradient(shader, lookup);
      }
      break;
    case TCanvasShader::Kind::Image: {
      const TCanvasImage &image = shader.image;
      if (image.isEmpty() || image.rowBytes < (std::ptrdiff_t)image.width * 4)
        break;
      m_texture = makeTexture(image.width, image.height, image.pixels,
                              image.rowBytes, shader.filter, shader.tile);
      m_premultiplied = image.premultiplied;
      lookup = TCanvasAffine::scale(1.0 / image.width, 1.0 / image.height) *
               inverse(shader.imageToLocal);
      break;
    }
    }

    if (!m_texture) {
      glColor4f(color.r, color.g, color.b, color.a);
      return;
    }
    // The texture gives the colour, the paint its opacity.
    const float a = paint.color.a;
    if (m_premultiplied)
      glColor4f(a, a, a, a);
    else
      glColor4f(1.f, 1.f, 1.f, a);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    GLdouble matrix[16];
    toGl(lookup, matrix);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadMatrixd(matrix);
    glMatrixMode(GL_MODELVIEW);
  }

  ~BoundPaint() {
    if (!m_texture) return;
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glDeleteTextures(1, &m_texture);
  }

  BoundPaint(const BoundPaint &)            = delete;
  BoundPaint &operator=(const BoundPaint &) = delete;

  bool textured() const { return m_texture != 0; }
  //! Whether what comes out of the texture already has its alpha in it.
  bool premultiplied() const { return m_premultiplied; }

private:
  GLuint m_texture     = 0;
  bool m_premultiplied = false;

  enum { kSteps = 256 };

  // Offset u of the gradient is at the centre of texel u * (kSteps - 1).
  static TCanvasAffine toTexels() {
    return TCanvasAffine((kSteps - 1.0) / kSteps, 0.0, 0.5 / kSteps, 0.0,
                         (kSteps - 1.0) / kSteps, 0.5 / kSteps);
  }

  void makeLinearGradient(const TCanvasShader &shader, TCanvasAffine &lookup) {
    const double dx = shader.p1.x - shader.p0.x, dy = shader.p1.y - shader.p0.y;
    const double length2 = dx * dx + dy * dy;
    if (!(length2 > 0.0)) return;

    std::vector<unsigned char> row(kSteps * 4);
    for (int i = 0; i < kSteps; ++i)
      putBgra(&row[i * 4], colorAt(shader.stops, i / (kSteps - 1.0)));
    m_texture = makeTexture(kSteps, 1, row.data(), kSteps * 4,
                            TCanvasFilter::Linear, TCanvasTile::Clamp);
    // How far along the gradient a point is; the texture has one row.
    lookup = toTexels() *
             TCanvasAffine(dx / length2, dy / length2,
                           -(shader.p0.x * dx + shader.p0.y * dy) / length2,
                           0.0, 0.0, 0.0);
  }

  void makeRadialGradient(const TCanvasShader &shader, TCanvasAffine &lookup) {
    if (!(shader.radius > 0.0)) return;

    // A square from -1 to 1 around the centre, the radius being 1.
    std::vector<unsigned char> square((std::size_t)kSteps * kSteps * 4);
    for (int j = 0; j < kSteps; ++j)
      for (int i = 0; i < kSteps; ++i) {
        const double x = 2.0 * i / (kSteps - 1.0) - 1.0;
        const double y = 2.0 * j / (kSteps - 1.0) - 1.0;
        putBgra(&square[((std::size_t)j * kSteps + i) * 4],
                colorAt(shader.stops, std::hypot(x, y)));
      }
    m_texture = makeTexture(kSteps, kSteps, square.data(), kSteps * 4,
                            TCanvasFilter::Linear, TCanvasTile::Clamp);
    const double k = 0.5 / shader.radius;
    lookup         = toTexels() * TCanvasAffine(k, 0.0, 0.5 - shader.p0.x * k,
                                                0.0, k, 0.5 - shader.p0.y * k);
  }
};

// The target holds premultiplied colour, the paint is not premultiplied.
// Fixed-function blending cannot do that at once, so everything is drawn
// twice: the colour channels blended by alpha, then the alpha channel
// added up. (A source that is premultiplied already needs one pass.)
template <class Draw>
void blendOver(bool sourceIsPremultiplied, Draw draw) {
  glEnable(GL_BLEND);
  if (sourceIsPremultiplied) {
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    draw();
    return;
  }
  GLboolean mask[4];
  glGetBooleanv(GL_COLOR_WRITEMASK, mask);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColorMask(mask[0], mask[1], mask[2], GL_FALSE);
  draw();
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, mask[3]);
  draw();
  glColorMask(mask[0], mask[1], mask[2], mask[3]);
}

//=============================================================================
// The fringe of a stroke outline
//
// A band one pixel wide along a closed polyline, opaque on the polyline and
// transparent at its far edge. This is what the old renderer put around
// every solid stroke (OutlineBuilder and AntialiasingOutlinePainter in
// common/tvrender/tsimplecolorstyles.cpp); the corner rules are taken from
// there unchanged, so that strokes keep their look through this canvas.

class FringeBuilder {
public:
  // Pairs of points: one on the polyline, one \p width away from it.
  std::vector<GLdouble> vertices;

  explicit FringeBuilder(double width) : m_width(width) {}

  void add(double x, double y) {
    const double maxMiter = 4.0;

    if (m_stage == 0) {
      remember(x, y);
    } else {
      double bx       = x - m_px, by = y - m_py;
      const double lb = std::sqrt(bx * bx + by * by);
      if (lb < 1e-9) return;
      bx = m_width * bx / lb;
      by = m_width * by / lb;

      if (m_stage == 1) {
        remember(x, y);
      } else {
        if (m_stage == 2) remember(x, y);
        const double l = std::fabs(m_ax + bx) > 1e-9
                             ? -(m_ay - by) / (m_ax + bx)
                             : std::fabs(m_ay + by) > 1e-9
                                   ? (m_ax - bx) / (m_ay + by)
                                   : 0.0;
        if (std::fabs(l) > maxMiter || std::fabs(l) < 1.0 || l > 0.0) {
          push(m_px, m_py, m_px + m_ay, m_py - m_ax);
          push(m_px, m_py, m_px + by, m_py - bx);
        } else {
          push(m_px, m_py, m_px - l * bx + by, m_py - l * by - bx);
        }
      }
      m_ax = bx;
      m_ay = by;
    }
    m_px = x;
    m_py = y;
  }

  // Closes the band by going over the first points again.
  void finish() {
    const int stages = m_stage;
    for (int i = 0; i < stages; ++i) add(m_first[i][0], m_first[i][1]);
    m_stage = 0;
  }

private:
  double m_width;
  int m_stage = 0;
  double m_first[3][2] = {};
  double m_px = 0.0, m_py = 0.0, m_ax = 0.0, m_ay = 0.0;

  void remember(double x, double y) {
    m_first[m_stage][0] = x;
    m_first[m_stage][1] = y;
    ++m_stage;
  }
  void push(double x0, double y0, double x1, double y1) {
    const GLdouble pair[4] = {x0, y0, x1, y1};
    vertices.insert(vertices.end(), pair, pair + 4);
  }
};

// \p loop is in the coordinates of the current modelview matrix; the band
// is built and drawn in window coordinates, where a pixel is a unit.
void drawFringe(const std::vector<TCanvasPoint> &loop,
                const TCanvasColor &color) {
  if (loop.size() < 2) return;

  GLdouble viewport[4], projection[16], modelview[16];
  glGetDoublev(GL_VIEWPORT, viewport);
  glGetDoublev(GL_PROJECTION_MATRIX, projection);
  glGetDoublev(GL_MODELVIEW_MATRIX, modelview);

  // From clip coordinates (-1 to 1) to pixels of the viewport, and back.
  GLdouble toWindow[16] = {}, fromWindow[16] = {};
  toWindow[0] = toWindow[12] = 0.5 * viewport[2];
  toWindow[5] = toWindow[13] = 0.5 * viewport[3];
  toWindow[10] = toWindow[15] = 1.0;
  fromWindow[0]               = 2.0 / viewport[2];
  fromWindow[5]               = 2.0 / viewport[3];
  fromWindow[12] = fromWindow[13] = -1.0;
  fromWindow[10] = fromWindow[15] = 1.0;

  GLdouble matrix[16];
  multiply(matrix, projection, modelview);
  multiply(matrix, toWindow, matrix);

  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixd(fromWindow);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  for (double width : {1.0, -1.0}) {  // on either side
    FringeBuilder builder(width);
    for (const TCanvasPoint &p : loop) {
      const double w = matrix[3] * p.x + matrix[7] * p.y + matrix[15];
      const double k = std::fabs(w) > 1e-9 ? 1.0 / w : 0.0;
      builder.add(k * (matrix[0] * p.x + matrix[4] * p.y + matrix[12]),
                  k * (matrix[1] * p.x + matrix[5] * p.y + matrix[13]));
    }
    builder.finish();

    glBegin(GL_TRIANGLE_STRIP);
    for (std::size_t i = 0; i + 3 < builder.vertices.size(); i += 4) {
      glColor4f(color.r, color.g, color.b, color.a);
      glVertex2dv(&builder.vertices[i]);
      glColor4f(color.r, color.g, color.b, 0.f);
      glVertex2dv(&builder.vertices[i + 2]);
    }
    glEnd();
  }

  glLoadMatrixd(projection);
  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixd(modelview);
  glColor4f(color.r, color.g, color.b, color.a);
}

// A 16-bit stipple pattern and its repeat factor for dashes and gaps given
// in pixels. Exact when they add up to 16 pixels or a multiple of it.
void setStipple(const std::vector<double> &dash) {
  std::vector<double> lengths;
  for (double length : dash) lengths.push_back(std::max(length, 0.0));
  if (lengths.size() % 2 != 0) {
    const std::vector<double> once = lengths;
    lengths.insert(lengths.end(), once.begin(), once.end());
  }
  double period = 0.0;
  for (double length : lengths) period += length;
  if (!(period > 0.0)) return;

  const int factor =
      (int)std::min(std::max(std::lround(period / 16.0), 1L), 256L);
  GLushort pattern = 0;
  for (int bit = 0; bit < 16; ++bit) {
    double position = std::fmod((bit + 0.5) * factor, period);
    for (std::size_t i = 0; i < lengths.size(); ++i) {
      if (position < lengths[i]) {
        if (i % 2 == 0) pattern |= (GLushort)(1 << bit);
        break;
      }
      position -= lengths[i];
    }
  }
  glEnable(GL_LINE_STIPPLE);
  glLineStipple(factor, pattern);
}

}  // namespace

//=============================================================================
// TGlCanvas

TGlCanvas::TGlCanvas(const TCanvasIRect &deviceBounds)
    : m_bounds(deviceBounds), m_clips(1, 0) {
  glGetDoublev(GL_MODELVIEW_MATRIX, m_base);
}

TGlCanvas::~TGlCanvas() {
  for (int count : m_clips) dropClips(count);
  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixd(m_base);
}

void TGlCanvas::dropClips(int count) {
  for (int i = 0; i < count; ++i) TStencilControl::instance()->disableMask();
}

void TGlCanvas::loadTransform() {
  GLdouble matrix[16];
  toGl(transform(), matrix);
  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixd(m_base);
  glMultMatrixd(matrix);
}

void TGlCanvas::onSave() { m_clips.push_back(0); }

void TGlCanvas::onRestore() {
  dropClips(m_clips.back());
  m_clips.pop_back();
  loadTransform();
}

void TGlCanvas::onConcat(const TCanvasAffine &) { loadTransform(); }

void TGlCanvas::onClipPath(const TCanvasPath &path, TCanvasFillRule rule,
                           TCanvasClipOp op, bool) {
  TStencilControl *stencil = TStencilControl::instance();
  stencil->beginMask();
  tessellate(flatten(path, 0.1 * pixelSize()), rule);
  stencil->endMask();
  stencil->enableMask(op == TCanvasClipOp::Intersect
                          ? TStencilControl::SHOW_INSIDE
                          : TStencilControl::SHOW_OUTSIDE);
  ++m_clips.back();
}

void TGlCanvas::onFillPath(const TCanvasPath &path, TCanvasFillRule rule,
                           const TCanvasPaint &paint) {
  const std::vector<Contour> contours = flatten(path, 0.1 * pixelSize());

  ScopedAttributes attributes;
  BoundPaint bound(paint);
  glDisable(GL_POLYGON_SMOOTH);
  if (paint.antialias) tglEnableLineSmooth(true, 1.0);

  blendOver(bound.premultiplied(), [&] {
    tessellate(contours, rule);
    if (!paint.antialias) return;
    // The fringe: every contour once more, as a smooth closed line.
    for (const Contour &contour : contours) {
      glBegin(GL_LINE_LOOP);
      for (const TCanvasPoint &point : contour.points) {
        glTexCoord2d(point.x, point.y);
        glVertex2d(point.x, point.y);
      }
      glEnd();
    }
  });
}

void TGlCanvas::onStrokePath(const TCanvasPath &path, const TCanvasPen &pen,
                             const TCanvasPaint &paint) {
  const std::vector<Contour> contours = flatten(path, 0.1 * pixelSize());

  ScopedAttributes attributes;
  BoundPaint bound(paint);

  GLfloat range[2] = {1.f, 1.f};
  glGetFloatv(GL_LINE_WIDTH_RANGE, range);
  const double pixels =
      pen.unit == TCanvasPen::Unit::Device ? pen.width
                                           : pen.width / pixelSize();
  glLineWidth((GLfloat)std::min(std::max(pixels, (double)range[0]),
                                (double)range[1]));
  if (paint.antialias) {
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_LINE_SMOOTH);
  } else {
    glDisable(GL_LINE_SMOOTH);
  }
  if (!pen.dash.empty()) setStipple(pen.dash);

  blendOver(bound.premultiplied(), [&] { drawLines(contours); });
}

void TGlCanvas::onDrawMesh(const TCanvasMesh &mesh, const TCanvasPaint &paint) {
  ScopedAttributes attributes;
  BoundPaint bound(paint);

  const GLenum mode = mesh.mode == TCanvasMesh::Mode::Triangles
                          ? GL_TRIANGLES
                          : mesh.mode == TCanvasMesh::Mode::TriangleStrip
                                ? GL_TRIANGLE_STRIP
                                : GL_TRIANGLE_FAN;
  const GLsizei count = (GLsizei)mesh.positions.size();

  // The paint's opacity applies to the vertex colours too.
  std::vector<TCanvasColor> colors = mesh.colors;
  for (TCanvasColor &color : colors) {
    color.a *= paint.color.a;
    if (bound.premultiplied())
      color.r *= color.a, color.g *= color.a, color.b *= color.a;
  }

  const std::vector<TCanvasPoint> &lookups =
      mesh.uvs.empty() ? mesh.positions : mesh.uvs;
  const auto drawTriangles = [&] {
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_DOUBLE, sizeof(TCanvasPoint), &mesh.positions[0].x);
    if (bound.textured()) {
      glEnableClientState(GL_TEXTURE_COORD_ARRAY);
      glTexCoordPointer(2, GL_DOUBLE, sizeof(TCanvasPoint), &lookups[0].x);
    }
    if (!colors.empty()) {
      glEnableClientState(GL_COLOR_ARRAY);
      glColorPointer(4, GL_FLOAT, sizeof(TCanvasColor), &colors[0].r);
    }
    glDrawArrays(mode, 0, count);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
  };

  // One flat colour: what a solid stroke is, and it is drawn the way the old
  // renderer drew those.
  const bool flat = colors.empty() && !bound.textured();
  if (!flat) {
    blendOver(bound.premultiplied(), drawTriangles);
    return;
  }

  // The two sides of a strip are its even and its odd vertices; around
  // them goes the fringe, or without antialiasing a plain line.
  const bool strip = mesh.mode == TCanvasMesh::Mode::TriangleStrip;
  std::vector<TCanvasPoint> loop;
  if (strip && mesh.smoothEdges) {
    for (std::size_t i = 0; i < mesh.positions.size(); i += 2)
      loop.push_back(mesh.positions[i]);
    const std::size_t lastOdd = (mesh.positions.size() / 2) * 2 - 1;
    for (std::size_t i = lastOdd;; i -= 2) {
      loop.push_back(mesh.positions[i]);
      if (i == 1) break;
    }
  }
  const auto drawOutline = [&] {
    if (loop.empty()) return;
    if (paint.antialias) {
      drawFringe(loop, paint.color);
    } else {
      glDisable(GL_LINE_SMOOTH);
      glLineWidth(1.f);
      glBegin(GL_LINE_LOOP);
      for (const TCanvasPoint &point : loop) glVertex2d(point.x, point.y);
      glEnd();
    }
  };

  blendOver(false, [&] {
    if (paint.color.a < 1.f) {
      // The triangles overlap where the stroke bends or crosses itself.
      // The stencil buffer lets each pixel through once, and afterwards
      // keeps the fringe off what the triangles covered.
      TStencilControl *stencil = TStencilControl::instance();
      stencil->beginMask(TStencilControl::DRAW_ON_SCREEN_ONLY_ONCE);
      drawTriangles();
      stencil->endMask();
      stencil->enableMask(TStencilControl::SHOW_OUTSIDE);
      drawOutline();
      stencil->disableMask();
    } else {
      drawOutline();
      drawTriangles();
    }
  });
}

void TGlCanvas::onDrawImage(const TCanvasImage &image,
                            const TCanvasAffine &imageToLocal,
                            const TCanvasImageOptions &options) {
  if (image.rowBytes < (std::ptrdiff_t)image.width * 4) return;

  // The image as a paint whose shader is looked up at the corners of the
  // image itself.
  TCanvasPaint paint;
  paint.color.a             = (float)std::min(options.opacity, 1.0);
  paint.shader.kind         = TCanvasShader::Kind::Image;
  paint.shader.image        = image;
  paint.shader.imageToLocal = imageToLocal;
  paint.shader.tile         = TCanvasTile::Clamp;
  paint.shader.filter       = options.filter;

  ScopedAttributes attributes;
  BoundPaint bound(paint);
  const TCanvasPoint corners[4] = {
      imageToLocal * TCanvasPoint(0, 0),
      imageToLocal * TCanvasPoint(image.width, 0),
      imageToLocal * TCanvasPoint(image.width, image.height),
      imageToLocal * TCanvasPoint(0, image.height)};

  blendOver(bound.premultiplied(), [&] {
    glBegin(GL_TRIANGLE_FAN);
    for (const TCanvasPoint &corner : corners) {
      glTexCoord2d(corner.x, corner.y);
      glVertex2d(corner.x, corner.y);
    }
    glEnd();
  });
}
