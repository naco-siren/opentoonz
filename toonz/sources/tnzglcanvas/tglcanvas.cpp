#include "canvas/tglcanvas.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX  // or windows.h turns std::min and std::max into macros
#endif
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

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
// Texture coordinates

// Where in the texture a point of a shape is looked up. The coordinate is
// worked out here, in double precision, and sent with the vertex; OpenGL's
// own texture matrix is left alone. (It would do the same sum in single
// precision, and the old renderer, which sent coordinates it had computed
// itself, could then not be matched to the bit.)
struct Lookup {
  bool textured = false;  // without a texture nothing is sent
  bool identity = true;   // the point is the coordinate
  TCanvasAffine map;

  void send(double x, double y) const {
    if (!textured) return;
    if (identity)
      glTexCoord2d(x, y);
    else
      glTexCoord2d(map.a11 * x + map.a12 * y + map.a13,
                   map.a21 * x + map.a22 * y + map.a23);
  }
};

//-----------------------------------------------------------------------------
// GLU tessellation. Every vertex is sent as position and, when the paint has
// a shader, as the texture coordinate of that position: a shader is looked
// up where the shape is.

struct Tessellation {
  // A deque: GLU keeps pointers into it.
  std::deque<std::array<GLdouble, 3>> vertices;
  const Lookup *lookup = nullptr;
};

extern "C" {
static void CALLBACK tessBegin(GLenum type) { glBegin(type); }
static void CALLBACK tessEnd() { glEnd(); }
static void CALLBACK tessVertex(void *vertex, void *polygonData) {
  const GLdouble *v = (const GLdouble *)vertex;
  ((const Tessellation *)polygonData)->lookup->send(v[0], v[1]);
  glVertex2dv(v);
}
// Where contours cross, GLU asks for a vertex of its own.
static void CALLBACK tessCombine(GLdouble coords[3], void *[4], GLfloat[4],
                                 void **out, void *polygonData) {
  Tessellation &tessellation = *(Tessellation *)polygonData;
  tessellation.vertices.push_back({coords[0], coords[1], 0.0});
  *out = tessellation.vertices.back().data();
}
}

#ifdef _WIN32
typedef GLvoid(CALLBACK *GluCallback)(void);
#else
typedef GLvoid (*GluCallback)();
#endif

// With \p positive, the non-zero rule fills only where the winding number
// is above zero: see TGlCanvas::State::Callers.
void tessellate(const std::vector<Contour> &contours, TCanvasFillRule rule,
                const Lookup &lookup, bool positive) {
  GLUtesselator *tess = gluNewTess();
  if (!tess) return;

  Tessellation tessellation;
  tessellation.lookup = &lookup;
  gluTessCallback(tess, GLU_TESS_BEGIN, (GluCallback)tessBegin);
  gluTessCallback(tess, GLU_TESS_END, (GluCallback)tessEnd);
  gluTessCallback(tess, GLU_TESS_VERTEX_DATA, (GluCallback)tessVertex);
  gluTessCallback(tess, GLU_TESS_COMBINE_DATA, (GluCallback)tessCombine);
  gluTessProperty(tess, GLU_TESS_WINDING_RULE,
                  rule == TCanvasFillRule::EvenOdd
                      ? GLU_TESS_WINDING_ODD
                      : positive ? GLU_TESS_WINDING_POSITIVE
                                 : GLU_TESS_WINDING_NONZERO);

  gluTessBeginPolygon(tess, &tessellation);
  for (const Contour &contour : contours) {
    gluTessBeginContour(tess);
    for (const TCanvasPoint &point : contour.points) {
      tessellation.vertices.push_back({point.x, point.y, 0.0});
      gluTessVertex(tess, tessellation.vertices.back().data(),
                    tessellation.vertices.back().data());
    }
    gluTessEndContour(tess);
  }
  gluTessEndPolygon(tess);
  gluDeleteTess(tess);
}

void drawLines(const std::vector<Contour> &contours, const Lookup &lookup) {
  for (const Contour &contour : contours) {
    glBegin(contour.closed ? GL_LINE_LOOP : GL_LINE_STRIP);
    for (const TCanvasPoint &point : contour.points) {
      lookup.send(point.x, point.y);
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
  // A plain scale, the usual way of placing a texture, without the detour
  // through the determinant and its rounding.
  if (m.a12 == 0.0 && m.a21 == 0.0 && m.a13 == 0.0 && m.a23 == 0.0 &&
      m.a11 != 0.0 && m.a22 != 0.0)
    return TCanvasAffine::scale(1.0 / m.a11, 1.0 / m.a22);
  const double det = m.det();
  if (det == 0.0) return TCanvasAffine();
  const double a = m.a22 / det, b = -m.a12 / det;
  const double c = -m.a21 / det, d = m.a11 / det;
  return TCanvasAffine(a, b, -(a * m.a13 + b * m.a23), c, d,
                       -(c * m.a13 + d * m.a23));
}

//=============================================================================
// Paint

// With its own state, everything a call changes is put back when the call
// is over (except the stencil state, which the masks keep track of). With
// the caller's state nothing is saved: the old style code did not either,
// and what it left changed is part of what the caller expects.
struct ScopedAttributes {
  const bool active;
  explicit ScopedAttributes(TGlCanvas::State state)
      : active(state == TGlCanvas::State::Own) {
    if (!active) return;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
  }
  ~ScopedAttributes() {
    if (!active) return;
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
  struct ResetRowLength {  // the rest of the tree expects it to be 0
    ~ResetRowLength() { glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); }
  } resetRowLength;
  // "4" for four components, the way the old code asks for its textures:
  // which sized format that becomes is the driver's choice, and stays it.
  glTexImage2D(GL_TEXTURE_2D, 0, 4, width, height, 0, GL_BGRA,
               GL_UNSIGNED_BYTE, bgra);
  return texture;
}

}  // namespace

// The texture of the image drawn last, kept for as long as the canvas lives
// in case the next call draws the same image: a pattern stroke puts the
// same picture down hundreds of times. Only an image that says what keeps
// its pixels alive is kept; for any other, the same address may by now hold
// other pixels.
struct TGlCanvas::Textures {
  std::shared_ptr<const void> owner;
  const void *pixels      = nullptr;
  int width = 0, height = 0;
  std::ptrdiff_t rowBytes = 0;
  TCanvasFilter filter    = TCanvasFilter::Linear;
  TCanvasTile tile        = TCanvasTile::Repeat;
  GLuint texture          = 0;

  ~Textures() { clear(); }

  void clear() {
    if (texture) glDeleteTextures(1, &texture);
    texture = 0;
    owner.reset();
  }

  // The texture for the image, bound; \p kept says whether it stays here
  // or is the caller's to delete.
  GLuint get(const TCanvasImage &image, TCanvasFilter imageFilter,
             TCanvasTile imageTile, bool &kept);
};

GLuint TGlCanvas::Textures::get(const TCanvasImage &image,
                                TCanvasFilter imageFilter,
                                TCanvasTile imageTile, bool &kept) {
  kept = image.keepAlive != nullptr;
  if (kept && texture && owner == image.keepAlive && pixels == image.pixels &&
      width == image.width && height == image.height &&
      rowBytes == image.rowBytes && filter == imageFilter &&
      tile == imageTile) {
    glBindTexture(GL_TEXTURE_2D, texture);
    return texture;
  }
  const GLuint made = makeTexture(image.width, image.height, image.pixels,
                                  image.rowBytes, imageFilter, imageTile);
  if (!kept) return made;
  clear();
  owner    = image.keepAlive;
  pixels   = image.pixels;
  width    = image.width;
  height   = image.height;
  rowBytes = image.rowBytes;
  filter   = imageFilter;
  tile     = imageTile;
  texture  = made;
  return made;
}

namespace {

// A colour whose components are whole bytes goes to OpenGL as bytes, the
// way the old code sent a TPixel32 (glColor4ub). How a byte becomes the
// float OpenGL works with is then the driver's affair, as it always was;
// a float worked out here may be the next one along, which is enough to
// round a blended or interpolated pixel the other way now and then.
bool wholeBytes(const TCanvasColor &color, GLubyte bytes[4]) {
  const float v[4] = {color.r, color.g, color.b, color.a};
  for (int i = 0; i < 4; ++i) {
    const long k = std::lround(v[i] * 255.f);
    if (k < 0 || k > 255 || (float)k / 255.f != v[i]) return false;
    bytes[i] = (GLubyte)k;
  }
  return true;
}

void setColor(const TCanvasColor &color) {
  GLubyte bytes[4];
  if (wholeBytes(color, bytes))
    glColor4ubv(bytes);
  else
    glColor4f(color.r, color.g, color.b, color.a);
}

// Sets up OpenGL for a paint: the colour and, for a shader, a texture, with
// the map from the point a vertex is looked up at to where in the texture
// that is.
class BoundPaint {
public:
  BoundPaint(const TCanvasPaint &paint, TGlCanvas::Textures &textures) {
    TCanvasColor color          = paint.color;
    const TCanvasShader &shader = paint.shader;
    TCanvasAffine &lookup       = m_lookup.map;

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
      m_texture = textures.get(image, shader.filter, shader.tile, m_kept);
      m_premultiplied = image.premultiplied;
      // An image laid over the unit square is looked up at the point
      // itself: exactly, not by a product that only comes close to 1.
      const TCanvasAffine unit =
          TCanvasAffine::scale(1.0 / image.width, 1.0 / image.height);
      m_lookup.identity = shader.imageToLocal == unit;
      lookup            = unit * inverse(shader.imageToLocal);
      break;
    }
    }

    if (!m_texture) {
      setColor(color);
      return;
    }
    // The texture gives the colour, the paint its opacity.
    const float a = paint.color.a;
    if (m_premultiplied)
      setColor(TCanvasColor(a, a, a, a));
    else
      setColor(TCanvasColor(1.f, 1.f, 1.f, a));
    m_textureWasOn = glIsEnabled(GL_TEXTURE_2D);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    m_lookup.textured = true;
  }

  ~BoundPaint() {
    if (!m_texture) return;
    if (!m_textureWasOn) glDisable(GL_TEXTURE_2D);
    if (!m_kept) glDeleteTextures(1, &m_texture);
  }

  BoundPaint(const BoundPaint &)            = delete;
  BoundPaint &operator=(const BoundPaint &) = delete;

  bool textured() const { return m_texture != 0; }
  const Lookup &lookup() const { return m_lookup; }
  //! Whether what comes out of the texture already has its alpha in it.
  bool premultiplied() const { return m_premultiplied; }

private:
  GLuint m_texture     = 0;
  Lookup m_lookup;
  bool m_premultiplied = false;
  bool m_kept          = false;  // the canvas keeps the texture
  GLboolean m_textureWasOn = GL_FALSE;

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
    m_lookup.identity = false;
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
    m_lookup.identity = false;
    const double k = 0.5 / shader.radius;
    lookup         = toTexels() * TCanvasAffine(k, 0.0, 0.5 - shader.p0.x * k,
                                                0.0, k, 0.5 - shader.p0.y * k);
  }
};

// The target holds premultiplied colour, the paint is not premultiplied.
// Fixed-function blending cannot do that at once, so everything is drawn
// twice: the colour channels blended by alpha, then the alpha channel
// added up. (A source that is premultiplied already needs one pass.)
//
// None of this with the caller's state: the old drawing code does the two
// passes itself, around the styles.
template <class Draw>
void blendOver(TGlCanvas::State state, bool sourceIsPremultiplied, Draw draw) {
  if (state == TGlCanvas::State::Callers) {
    draw();
    return;
  }
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

namespace {

// The part of the modelview matrix that moves things about in the plane.
TCanvasAffine modelviewAffine() {
  GLdouble m[16];
  glGetDoublev(GL_MODELVIEW_MATRIX, m);
  return TCanvasAffine(m[0], m[4], m[12], m[1], m[5], m[13]);
}

// What a mesh is sent as. An even strip is a strip of quads, and goes out
// as one (see the header).
GLenum primitive(const TCanvasMesh &mesh) {
  switch (mesh.mode) {
  case TCanvasMesh::Mode::Triangles:
    return GL_TRIANGLES;
  case TCanvasMesh::Mode::Quads:
    return GL_QUADS;
  case TCanvasMesh::Mode::TriangleFan:
    return GL_TRIANGLE_FAN;
  case TCanvasMesh::Mode::TriangleStrip:
    break;
  }
  return mesh.positions.size() % 2 == 0 ? GL_QUAD_STRIP : GL_TRIANGLE_STRIP;
}

void smoothLines() {
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_LINE_SMOOTH);
  glLineWidth(1.f);
}

}  // namespace

TGlCanvas::TGlCanvas(const TCanvasIRect &deviceBounds, TGlCanvasMasks &masks,
                     State state)
    : TCanvas(modelviewAffine())
    , m_bounds(deviceBounds)
    , m_masks(masks)
    , m_state(state)
    , m_clips(1, 0)
    , m_textures(new Textures) {
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
}

TGlCanvas::~TGlCanvas() {
  delete m_textures;
  glMatrixMode(GL_MODELVIEW);
  for (int count : m_clips) {
    dropClips(count);
    glPopMatrix();
  }
}

// The same number tglGetPixelSize2() gives, from the same place: OpenGL
// keeps the matrix in single precision, and styles decide how finely to cut
// curves by this value.
double TGlCanvas::pixelSize() const {
  GLdouble m[16];
  glGetDoublev(GL_MODELVIEW_MATRIX, m);
  double det = std::fabs(m[0] * m[5] - m[1] * m[4]);
  if (det < 1e-8) det = 1e-8;
  return std::sqrt(1.0 / det);
}

void TGlCanvas::dropClips(int count) {
  for (int i = 0; i < count; ++i) m_masks.disableMask();
}

void TGlCanvas::onSave() {
  m_clips.push_back(0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
}

void TGlCanvas::onRestore() {
  dropClips(m_clips.back());
  m_clips.pop_back();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
}

void TGlCanvas::onConcat(const TCanvasAffine &aff) {
  GLdouble matrix[16];
  toGl(aff, matrix);
  glMatrixMode(GL_MODELVIEW);
  glMultMatrixd(matrix);
}

void TGlCanvas::onClipPath(const TCanvasPath &path, TCanvasFillRule rule,
                           TCanvasClipOp op, bool antialias) {
  const std::vector<Contour> contours = flatten(path, 0.1 * pixelSize());

  // The mask is the path filled in white, the way a region is filled:
  // "antialiased" means with the smooth line along every contour, whose
  // outer half then belongs to the mask as well. That is the mask the old
  // styles got when they drew a region to the screen and to the stencil
  // buffer in one go.
  m_masks.beginMask(false);
  glColor4f(1.f, 1.f, 1.f, 1.f);
  if (antialias) smoothLines();
  tessellate(contours, rule, Lookup(), m_state == State::Callers);
  if (antialias) {
    for (const Contour &contour : contours) {
      glBegin(GL_LINE_LOOP);
      for (const TCanvasPoint &point : contour.points)
        glVertex2d(point.x, point.y);
      glEnd();
    }
  }
  m_masks.endMask();
  m_masks.enableMask(op == TCanvasClipOp::Intersect);
  ++m_clips.back();
}

// The mesh goes into the mask as the primitive it is drawn as, so that the
// mask is to the pixel what drawing it covers.
void TGlCanvas::onClipMesh(const TCanvasMesh &mesh, TCanvasClipOp op) {
  m_masks.beginMask(false);
  glColor4f(1.f, 1.f, 1.f, 1.f);
  glBegin(primitive(mesh));
  for (const TCanvasPoint &p : mesh.positions) glVertex2d(p.x, p.y);
  glEnd();
  m_masks.endMask();
  m_masks.enableMask(op == TCanvasClipOp::Intersect);
  ++m_clips.back();
}

void TGlCanvas::onFillPath(const TCanvasPath &path, TCanvasFillRule rule,
                           const TCanvasPaint &paint) {
  const std::vector<Contour> contours = flatten(path, 0.1 * pixelSize());

  ScopedAttributes attributes(m_state);
  BoundPaint bound(paint, *m_textures);
  if (m_state == State::Own) glDisable(GL_POLYGON_SMOOTH);
  if (paint.antialias) smoothLines();

  blendOver(m_state, bound.premultiplied(), [&] {
    tessellate(contours, rule, bound.lookup(), m_state == State::Callers);
    if (!paint.antialias) return;
    // The fringe: every contour once more, as a smooth closed line.
    for (const Contour &contour : contours) {
      glBegin(GL_LINE_LOOP);
      for (const TCanvasPoint &point : contour.points) {
        bound.lookup().send(point.x, point.y);
        glVertex2d(point.x, point.y);
      }
      glEnd();
    }
  });
}

// The fill, and a mask of the same fragments: with antialiasing, the smooth
// line along the contours included. The old styles drew a region to the
// screen and to the stencil buffer in one go; this comes to the same.
void TGlCanvas::onFillPathAndClip(const TCanvasPath &path, TCanvasFillRule rule,
                                  const TCanvasPaint &paint) {
  onFillPath(path, rule, paint);
  onClipPath(path, rule, TCanvasClipOp::Intersect, paint.antialias);
}

namespace {

// The colours of a linear gradient at the vertices of polylines, when that
// is all there is to it: no stop of the gradient falls between two
// neighbouring vertices, so that OpenGL's own interpolation from vertex to
// vertex is the gradient. A line that fades in and out along its length,
// which the old styles drew with a colour per vertex, is such a gradient.
bool gradientAtVertices(const std::vector<Contour> &contours,
                        const TCanvasPaint &paint,
                        std::vector<std::vector<TCanvasColor>> &colors) {
  const TCanvasShader &shader = paint.shader;
  if (shader.kind != TCanvasShader::Kind::LinearGradient ||
      shader.stops.size() < 2)
    return false;
  const double dx = shader.p1.x - shader.p0.x, dy = shader.p1.y - shader.p0.y;
  const double length2 = dx * dx + dy * dy;
  if (!(length2 > 0.0)) return false;

  // How close to a stop counts as on it: a billionth of the gradient's
  // length, or for a gradient shorter than a pixel, where that is less
  // than the arithmetic can tell apart, a ten-millionth of a unit.
  const double eps = std::max(1e-9, 1e-7 / std::sqrt(length2));
  colors.clear();
  for (const Contour &contour : contours) {
    std::vector<TCanvasColor> row;
    double previous = 0.0;
    for (std::size_t i = 0; i < contour.points.size(); ++i) {
      const TCanvasPoint &p = contour.points[i];
      double t = ((p.x - shader.p0.x) * dx + (p.y - shader.p0.y) * dy) / length2;
      // A vertex that is on a stop has that stop's colour, to the bit.
      TCanvasColor color;
      bool onStop = false;
      for (const TCanvasGradientStop &stop : shader.stops)
        if (std::fabs(t - stop.offset) < eps) {
          color = stop.color, t = stop.offset, onStop = true;
          break;
        }
      if (!onStop) color = colorAt(shader.stops, t);
      color.a *= paint.color.a;
      row.push_back(color);

      if (i > 0) {
        const double lo = std::min(previous, t), hi = std::max(previous, t);
        for (const TCanvasGradientStop &stop : shader.stops)
          if (stop.offset > lo + eps && stop.offset < hi - eps) return false;
      }
      previous = t;
    }
    colors.push_back(row);
  }
  return true;
}

}  // namespace

void TGlCanvas::onStrokePath(const TCanvasPath &path, const TCanvasPen &pen,
                             const TCanvasPaint &paint) {
  std::vector<Contour> contours = flatten(path, 0.1 * pixelSize());

  ScopedAttributes attributes(m_state);

  // A contour that is one point, under a pen with round caps, is a dot.
  std::vector<TCanvasPoint> dots;
  if (pen.cap == TCanvasPen::Cap::Round) {
    std::vector<Contour> lines;
    for (const Contour &contour : contours) {
      if (contour.points.size() == 1)
        dots.push_back(contour.points[0]);
      else
        lines.push_back(contour);
    }
    contours.swap(lines);
  }
  if (!dots.empty()) drawDots(dots, pen, paint);
  if (contours.empty()) return;

  // The same for as long as the context lives; asking is not free, and a
  // style may draw thousands of lines.
  if (m_lineWidthRange[1] < m_lineWidthRange[0]) {
    m_lineWidthRange[0] = m_lineWidthRange[1] = 1.f;
    glGetFloatv(GL_LINE_WIDTH_RANGE, m_lineWidthRange);
  }
  const float *range = m_lineWidthRange;
  const double pixels =
      pen.unit == TCanvasPen::Unit::Device ? pen.width
                                           : pen.width / pixelSize();
  glLineWidth((GLfloat)std::min(std::max(pixels, (double)range[0]),
                                (double)range[1]));
  // Whether lines are smooth is the caller's business when the state is.
  if (m_state == State::Own) {
    if (paint.antialias) {
      glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
      glEnable(GL_LINE_SMOOTH);
    } else {
      glDisable(GL_LINE_SMOOTH);
    }
  }
  if (!pen.dash.empty()) setStipple(pen.dash);

  std::vector<std::vector<TCanvasColor>> vertexColors;
  if (gradientAtVertices(contours, paint, vertexColors)) {
    // No texture: the gradient is a colour at each vertex.
    blendOver(m_state, false, [&] {
      for (std::size_t c = 0; c < contours.size(); ++c) {
        glBegin(contours[c].closed ? GL_LINE_LOOP : GL_LINE_STRIP);
        for (std::size_t i = 0; i < contours[c].points.size(); ++i) {
          setColor(vertexColors[c][i]);
          glVertex2d(contours[c].points[i].x, contours[c].points[i].y);
        }
        glEnd();
      }
    });
  } else {
    BoundPaint bound(paint, *m_textures);
    blendOver(m_state, bound.premultiplied(),
              [&] { drawLines(contours, bound.lookup()); });
  }

  if (m_state == State::Callers) {
    // What the old line styles left behind them.
    if (!pen.dash.empty()) glDisable(GL_LINE_STIPPLE);
    glLineWidth(1.f);
  }
}

// Dots. With a pen in device units they are OpenGL points of the pen's
// width; with a pen in local units, discs of half that radius, in the 60
// slices the old tglDrawDisk() cut every disc into.
void TGlCanvas::drawDots(const std::vector<TCanvasPoint> &dots,
                         const TCanvasPen &pen, const TCanvasPaint &paint) {
  BoundPaint bound(paint, *m_textures);

  if (pen.unit == TCanvasPen::Unit::Local) {
    blendOver(m_state, bound.premultiplied(), [&] {
      for (const TCanvasPoint &dot : dots) {
        glPushMatrix();
        glTranslated(dot.x, dot.y, 0.0);
        GLUquadric *quadric = gluNewQuadric();
        gluDisk(quadric, 0, 0.5 * pen.width, 60, 1);
        gluDeleteQuadric(quadric);
        glPopMatrix();
      }
    });
    return;
  }

  GLfloat sizeBefore = 1.f;
  glGetFloatv(GL_POINT_SIZE, &sizeBefore);
  if (paint.antialias) {
    // As tglEnablePointSmooth() did, the blending it switched on included.
    glEnable(GL_BLEND);
    glEnable(GL_POINT_SMOOTH);
    glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
  } else if (m_state == State::Own) {
    glDisable(GL_POINT_SMOOTH);
  }
  glPointSize((GLfloat)pen.width);
  blendOver(m_state, bound.premultiplied(), [&] {
    glBegin(GL_POINTS);
    for (const TCanvasPoint &dot : dots) {
      bound.lookup().send(dot.x, dot.y);
      glVertex2d(dot.x, dot.y);
    }
    glEnd();
  });
  if (m_state == State::Callers) glPointSize(sizeBefore);
}

void TGlCanvas::onDrawMesh(const TCanvasMesh &mesh, const TCanvasPaint &paint) {
  ScopedAttributes attributes(m_state);
  BoundPaint bound(paint, *m_textures);

  const GLsizei count = (GLsizei)mesh.positions.size();
  const bool strip    = mesh.mode == TCanvasMesh::Mode::TriangleStrip;
  const GLenum mode   = primitive(mesh);

  // The paint's opacity applies to the vertex colours too.
  std::vector<TCanvasColor> colors = mesh.colors;
  for (TCanvasColor &color : colors) {
    color.a *= paint.color.a;
    if (bound.premultiplied())
      color.r *= color.a, color.g *= color.a, color.b *= color.a;
  }

  // As bytes, if that is what they all are (see wholeBytes()).
  std::vector<GLubyte> byteColors(colors.size() * 4);
  for (std::size_t i = 0; i < colors.size(); ++i)
    if (!wholeBytes(colors[i], &byteColors[i * 4])) {
      byteColors.clear();
      break;
    }

  // The texture coordinate of each vertex: where it is looked up (its own
  // coordinates, or the position), taken through the paint's lookup.
  const Lookup &lookup = bound.lookup();
  const std::vector<TCanvasPoint> &lookedUpAt =
      mesh.uvs.empty() ? mesh.positions : mesh.uvs;
  std::vector<TCanvasPoint> mapped;
  if (bound.textured() && !lookup.identity) {
    mapped.reserve(lookedUpAt.size());
    for (const TCanvasPoint &p : lookedUpAt) mapped.push_back(lookup.map * p);
  }
  const std::vector<TCanvasPoint> &texCoords =
      mapped.empty() ? lookedUpAt : mapped;

  const auto drawTriangles = [&] {
    if (!colors.empty() && !bound.textured()) {
      // Vertex by vertex, the way the old styles sent the primitives they
      // coloured themselves.
      glBegin(mode);
      for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        if (!byteColors.empty())
          glColor4ubv(&byteColors[i * 4]);
        else
          glColor4f(colors[i].r, colors[i].g, colors[i].b, colors[i].a);
        glVertex2d(mesh.positions[i].x, mesh.positions[i].y);
      }
      glEnd();
      return;
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_DOUBLE, sizeof(TCanvasPoint), &mesh.positions[0].x);
    if (bound.textured()) {
      glEnableClientState(GL_TEXTURE_COORD_ARRAY);
      glTexCoordPointer(2, GL_DOUBLE, sizeof(TCanvasPoint), &texCoords[0].x);
    }
    if (!byteColors.empty()) {
      glEnableClientState(GL_COLOR_ARRAY);
      glColorPointer(4, GL_UNSIGNED_BYTE, 0, byteColors.data());
    } else if (!colors.empty()) {
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
    // A textured strip that wants smooth edges gets what the old renderer
    // gave a textured stroke: a line down each of its two sides, textured
    // like the strip, drawn first.
    const bool sides =
        strip && mesh.smoothEdges && colors.empty() && bound.textured();
    if (sides && m_state == State::Own) {
      if (paint.antialias)
        smoothLines();
      else
        glDisable(GL_LINE_SMOOTH), glLineWidth(1.f);
    }
    blendOver(m_state, bound.premultiplied(), [&] {
      if (sides) {
        for (std::size_t side = 0; side < 2; ++side) {
          glBegin(GL_LINE_STRIP);
          for (std::size_t i = side; i < mesh.positions.size(); i += 2) {
            glTexCoord2d(texCoords[i].x, texCoords[i].y);
            glVertex2d(mesh.positions[i].x, mesh.positions[i].y);
          }
          glEnd();
        }
      }
      drawTriangles();
    });
    return;
  }

  // The two sides of a strip are its even and its odd vertices. Along them
  // goes the fringe: a band around both, or without antialiasing a plain
  // line down each.
  const bool outlined = strip && mesh.smoothEdges && count >= 4;
  const auto drawOutline = [&] {
    if (!outlined) return;
    const std::size_t n = mesh.positions.size();
    if (paint.antialias) {
      std::vector<TCanvasPoint> loop;
      for (std::size_t i = 0; i < n; i += 2) loop.push_back(mesh.positions[i]);
      for (std::size_t i = (n / 2) * 2 - 1;; i -= 2) {
        loop.push_back(mesh.positions[i]);
        if (i == 1) break;
      }
      drawFringe(loop, paint.color);
      return;
    }
    if (m_state == State::Own) {
      glDisable(GL_LINE_SMOOTH);
      glLineWidth(1.f);
    }
    for (std::size_t side = 0; side < 2; ++side) {
      glBegin(GL_LINE_STRIP);
      for (std::size_t i = side; i < n; i += 2)
        glVertex2d(mesh.positions[i].x, mesh.positions[i].y);
      glEnd();
    }
  };

  blendOver(m_state, false, [&] {
    if (paint.color.a < 1.f) {
      // The triangles overlap where the stroke bends or crosses itself.
      // The stencil buffer lets each pixel through once, and afterwards
      // keeps the fringe off what the triangles covered.
      m_masks.beginMask(true);
      drawTriangles();
      m_masks.endMask();
      m_masks.enableMask(false);
      drawOutline();
      m_masks.disableMask();
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

  ScopedAttributes attributes(m_state);
  BoundPaint bound(paint, *m_textures);
  const TCanvasPoint corners[4] = {
      imageToLocal * TCanvasPoint(0, 0),
      imageToLocal * TCanvasPoint(image.width, 0),
      imageToLocal * TCanvasPoint(image.width, image.height),
      imageToLocal * TCanvasPoint(0, image.height)};
  // The corners of the image are the corners of the texture: said outright
  // rather than looked up.
  const double texCoords[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

  blendOver(m_state, bound.premultiplied(), [&] {
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i < 4; ++i) {
      glTexCoord2dv(texCoords[i]);
      glVertex2d(corners[i].x, corners[i].y);
    }
    glEnd();
  });
}
