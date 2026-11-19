
#include <cstring>
#include <limits>
#include <QStringList>
#include <QDir>

// TnzCore includes
#include "tsystem.h"
#include "tthread.h"
#include "tstopwatch.h"

#include "tvectorgl.h"
#include "tvectorrenderdata.h"
#include "tvectorimage.h"
#include "trasterimage.h"
#include "tlevel_io.h"
#include "tstrokeprop.h"
#include "tcolorfunctions.h"
#include "tofflinegl.h"
#include "drawutil.h"
#include "trop.h"
#include "tropcm.h"
#include "ttoonzimage.h"
#include "tstencilcontrol.h"
#include "tpalette.h"
#include "tpixelutils.h"
#include "tregionprop.h"
#include "tcurves.h"
#include "tcurveutil.h"
#include "tstrokeoutline.h"
#include "tregion.h"

#include "tsimplecolorstyles.h"
#include "trailscale.h"

#include "tcanvasutil.h"
#include "tglcanvasbridge.h"
#include "tvectorrasterizer.h"
#include "tvectorcanvas.h"

#include <memory>
#include <typeinfo>

#ifndef _WIN32
#define CALLBACK
#endif

//-----------------------------------------------------------------------------

TFilePath TRasterImagePatternStrokeStyle::m_rootDir = TFilePath();
TFilePath TVectorImagePatternStrokeStyle::m_rootDir = TFilePath();

//-----------------------------------------------------------------------------

#ifndef NDEBUG
static void checkGlError() {
  GLenum err = glGetError();
  assert(err != GL_INVALID_ENUM);
  assert(err != GL_INVALID_VALUE);
  assert(err != GL_INVALID_OPERATION);
  assert(err != GL_STACK_OVERFLOW);
  assert(err != GL_STACK_UNDERFLOW);
  assert(err != GL_OUT_OF_MEMORY);
  assert(err == GL_NO_ERROR);
}

#define CHECK_GL_ERROR checkGlError();
#else
#define CHECK_GL_ERROR
#endif

// Use GL_LINE_STRIP to debug the centerline-to-outline algorithm
#define QUAD_PRIMITIVE GL_QUAD_STRIP

//*************************************************************************************
//    Local namespace  stuff
//*************************************************************************************

namespace {

//-----------------------------------------------------------------------------

// se ras ha le dimensioni giuste (potenze di due) ritorna ras. Altrimenti
// crea il piu' piccolo raster con le dimensioni giuste che contenga ras e
// copia il contenuto (scalandolo)
// se il raster di partenza e' vuoto, non e' ras32 o e' piu' piccolo di 2x2
// ritorna un raster vuoto
TRaster32P makeTexture(const TRaster32P &ras) {
  if (!ras || ras->getLx() < 2 || ras->getLy() < 2) return TRaster32P();
  TRaster32P ras32 = ras;
  if (!ras32) return TRaster32P();
  TDimension d(2, 2);
  while (d.lx < 256 && d.lx * 2 <= ras32->getLx()) d.lx *= 2;
  while (d.ly < 256 && d.ly * 2 <= ras32->getLy()) d.ly *= 2;
  if (d == ras32->getSize())
    return ras32;
  else {
    TRaster32P texture(d);
    TScale sc((double)d.lx / ras32->getLx(), (double)d.ly / ras32->getLy());
    TRop::resample(texture, ras32, sc);
    return texture;
  }
}

//-----------------------------------------------------------------------------

double maximumPatternThickness(const TStroke *stroke) {
  double thickness = 0.0;
  for (int i = 0; i < stroke->getControlPointCount(); ++i)
    thickness = (std::max)(thickness, stroke->getControlPoint(i).thick);
  return thickness;
}

int findPatternSourceFrameIndex(const TLevelP &level, int frame) {
  if (!level || frame <= 0) return -1;
  const TFrameId fid(frame);
  int index = 0;
  for (auto it = level->begin(); it != level->end(); ++it, ++index) {
    if (it->first == fid) return it->second ? index : -1;
  }
  return -1;
}

TLevel::Iterator getPatternFrameIterator(const TLevelP &level,
                                         const TStroke *stroke);
void advancePatternFrameIterator(TLevel::Iterator &it, const TLevelP &level,
                                 int step);

}  // namespace

namespace {

// The 16 bits of an OpenGL line stipple as lengths of dashes and gaps in
// pixels, starting with a dash (of no length if the pattern starts with a
// gap).
std::vector<double> stippleToDash(USHORT stipple) {
  std::vector<double> dash;
  if (stipple == 0) return dash;
  bool on = true;
  dash.push_back(0.0);
  for (int bit = 0; bit < 16; ++bit) {
    const bool set = (stipple >> bit) & 1;
    if (set != on) dash.push_back(0.0), on = set;
    dash.back() += 1.0;
  }
  if (dash.size() % 2 != 0) dash.push_back(0.0);
  return dash;
}

}  // namespace

//*************************************************************************************
//    DisplayListManager  definition
//*************************************************************************************

#ifdef _WIN32

namespace {

class DisplayListManager  // singleton
{
public:
  TThread::Mutex m_mutex;
  typedef HGLRC T_hGlContext;

  //----------------------------------------------

  struct Key {
    std::string m_name;
    TFrameId m_fid;
    T_hGlContext m_glContext;
    Key(std::string name, TFrameId fid, T_hGlContext glContext)
        : m_name(name), m_fid(fid), m_glContext(glContext) {}
  };

  //----------------------------------------------
  struct Info {
    GLuint m_displayListId;
    TColorFunction::Parameters m_parameters;
    HDC m_hdc;
    Info()
        : m_displayListId(0)
        , m_hdc(0)
        , m_parameters(TColorFunction::Parameters()) {}

    Info(GLuint displayListId, HDC hdc, TColorFunction::Parameters parameters)
        : m_displayListId(displayListId)
        , m_hdc(hdc)
        , m_parameters(parameters) {}

    // se ad una stessa chiave corrispondono due displayInfo diversi (second
    // questo metodo),
    // si aggiorna la displayList secondo l'ultima chiamata.
    // Serve per non duplicare troppe liste, per parametri che non cambiano
    // spesso, come le cf.
    bool isDrawnAs(const Info &linfo) {
      return (this->m_parameters.m_mR == linfo.m_parameters.m_mR &&
              this->m_parameters.m_mG == linfo.m_parameters.m_mG &&
              this->m_parameters.m_mB == linfo.m_parameters.m_mB &&
              this->m_parameters.m_mM == linfo.m_parameters.m_mM &&
              this->m_parameters.m_cR == linfo.m_parameters.m_cR &&
              this->m_parameters.m_cG == linfo.m_parameters.m_cG &&
              this->m_parameters.m_cB == linfo.m_parameters.m_cB &&
              this->m_parameters.m_cM == linfo.m_parameters.m_cM);
    }
  };
  //----------------------------------------------

  struct KeyLess final {
    bool operator()(const Key &d1, const Key &d2) const {
        return d1.m_glContext < d2.m_glContext ||
               (d1.m_glContext == d2.m_glContext &&
                (d1.m_fid < d2.m_fid ||
                 d1.m_fid == d2.m_fid && d1.m_name < d2.m_name));
    }
};

  //----------------------------------------------

  typedef std::map<Key, Info, KeyLess> DisplayListMap;

private:
  DisplayListMap m_displayListMap;

  DisplayListManager() {}

  void recordList(GLuint listId, TVectorImage *image,
                  const TVectorRenderData &rd) {
    TThread::MutexLocker sl(&m_mutex);
    TRectD bbox = image->getBBox();
    double ry   = bbox.getLy() * 0.5;
    if (ry < 1e-5) ry = 1e-5;
    TAffine identity;
    TRect rect;
    glPushMatrix();

    glLoadIdentity();
    // forzo pixelSize come se si disegnasse su una stroke con thickness=10
    // tglMultMatrix(TScale(10.0/ry));

    // forzo pixelSize a 20.
    // In questo modo, un'immagine piu' grande (nel pli
    // che definisce il custom style) viene piu' definita di una piu' piccola,
    // anche se appaiono della stessa dimensione (che dipende esclusivamente
    // dalla thickness)
    // tglMultMatrix(TScale(0.50));

    TPalette *vPalette = image->getPalette();
    assert(vPalette);

    TVectorRenderData rd2(identity, rect, vPalette, rd.m_cf, true, true);
    rd2.m_isOfflineRender = rd.m_isOfflineRender;
    rd2.m_isImagePattern  = true;

    CHECK_GL_ERROR
    glNewList(listId, GL_COMPILE);
    CHECK_GL_ERROR
    tglDraw(rd2, image);
    CHECK_GL_ERROR
    glEndList();
    CHECK_GL_ERROR
    glPopMatrix();
    CHECK_GL_ERROR
  }

public:
  static DisplayListManager *instance() {
    static DisplayListManager singleton;
    return &singleton;
  }
  //--------------------------------------------

  GLuint getDisplayListId(TVectorImage *image, std::string name, TFrameId fid,
                          const TVectorRenderData &rd) {
    TThread::MutexLocker sl(&m_mutex);
    TColorFunction::Parameters parameters;
    if (rd.m_cf) rd.m_cf->getParameters(parameters);
    CHECK_GL_ERROR

    HDC hDC               = wglGetCurrentDC();
    T_hGlContext hContext = wglGetCurrentContext();
    Info listInfo(0, hDC, parameters);
    Key key(name, fid, hContext);
    DisplayListMap::iterator it = m_displayListMap.find(key);
    if (it != m_displayListMap.end()) {
      if (listInfo.isDrawnAs(it->second)) {
        // lascia tutto com'e' ed utilizza la displaliList
        listInfo.m_displayListId = it->second.m_displayListId;
      } else {  // aggiorna la displayList con le nuove info (attualmente solo
                // la cf)
        listInfo.m_displayListId = it->second.m_displayListId;
        recordList(listInfo.m_displayListId, image, rd);
        m_displayListMap[key] = listInfo;
      }
    } else  // se non ho mai disegnato l'immagine creo la displayList
    {
      CHECK_GL_ERROR
      listInfo.m_displayListId = glGenLists(1);

      if (listInfo.m_displayListId != 0)
        recordList(listInfo.m_displayListId, image, rd);
      else
        std::cout << "list of " << name << fid << " is 0." << std::endl;

      CHECK_GL_ERROR
      m_displayListMap[key] = listInfo;
    }
    return listInfo.m_displayListId;
  }

  void clearLists() {
    // save current context and others
    HDC hCurrentDC               = wglGetCurrentDC();
    T_hGlContext hCurrentContext = wglGetCurrentContext();
    // free all display lists
    DisplayListMap::iterator it = m_displayListMap.begin();
    for (; it != m_displayListMap.end(); ++it) {
      wglMakeCurrent(it->second.m_hdc, it->first.m_glContext);
      glDeleteLists(it->second.m_displayListId, 1);
    }
    m_displayListMap.clear();
    // restore current context

    wglMakeCurrent(hCurrentDC, hCurrentContext);
  }
};

}  // namespace

#endif  // _WIN32

//*************************************************************************************
//    TSimpleStrokeStyle  implementation
//*************************************************************************************

TStrokeProp *TSimpleStrokeStyle::makeStrokeProp(const TStroke *stroke) {
  return new TSimpleStrokeProp(stroke, this);
}

//*************************************************************************************
//    TOutlineStyle  implementation
//*************************************************************************************

TOutlineStyle::TOutlineStyle() : m_regionOutlineModifier(0) {}

//-----------------------------------------------------------------------------

TOutlineStyle::TOutlineStyle(const TOutlineStyle &other)
    : TColorStyle(other)
    , m_regionOutlineModifier(other.m_regionOutlineModifier
                                  ? other.m_regionOutlineModifier->clone()
                                  : 0) {}

//-----------------------------------------------------------------------------

TOutlineStyle::~TOutlineStyle() { delete m_regionOutlineModifier; }

//-----------------------------------------------------------------------------

void TOutlineStyle::setRegionOutlineModifier(RegionOutlineModifier *modifier) {
  delete m_regionOutlineModifier;
  m_regionOutlineModifier = modifier;
}

//-----------------------------------------------------------------------------

TStrokeProp *TOutlineStyle::makeStrokeProp(const TStroke *stroke) {
  return new OutlineStrokeProp(stroke, this);
}

//-----------------------------------------------------------------------------

TRegionProp *TOutlineStyle::makeRegionProp(const TRegion *region) {
  return new OutlineRegionProp(region, this);
}

//-----------------------------------------------------------------------------

void TOutlineStyle::computeOutline(const TStroke *stroke,
                                   TStrokeOutline &outline,
                                   TOutlineUtil::OutlineParameter param) const {
  TOutlineUtil::makeOutline(*stroke, outline, param);
}

//*************************************************************************************
//    TSolidColorStyle  implementation
//*************************************************************************************

TSolidColorStyle::TSolidColorStyle(const TPixel32 &color) : m_color(color) {}

//-----------------------------------------------------------------------------

TSolidColorStyle::TSolidColorStyle(const TSolidColorStyle &other)
    : TOutlineStyle(other), m_color(other.m_color) {}

//-----------------------------------------------------------------------------

TSolidColorStyle::~TSolidColorStyle() {}

//-----------------------------------------------------------------------------

TColorStyle *TSolidColorStyle::clone() const {
  return new TSolidColorStyle(*this);
}

//-----------------------------------------------------------------------------

void TSolidColorStyle::makeIcon(const TDimension &size) {
  /*-- TSolidColorStyle, TColorCleanupStyle, TBlackCleanupStyle --*/
  if (getTagId() != 3 && getTagId() != 2001 &&
      getTagId() !=
          2002)  // e' un'istanza di una classe derivata da TSolidColorStyle
  {
    TColorStyle::makeIcon(size);
  } else {
    if (!m_icon || m_icon->getSize() != size) {
      TRaster32P ras(size);
      m_icon = ras;
    }
    TPixel32 col = m_color;
    if (col.m == 255)
      m_icon->fill(col);
    else {
      TRaster32P fg(size);
      fg->fill(premultiply(col));
      TRop::checkBoard(m_icon, TPixel32::Black, TPixel32::White,
                       TDimensionD(6, 6), TPointD());
      TRop::over(m_icon, fg);
    }
  }
}

//-----------------------------------------------------------------------------

int TSolidColorStyle::canvasSupport() const {
  // The many styles that derive from this one override the OpenGL
  // functions for the parts that make them what they are. Until they are
  // moved to the canvas one by one, only the plain colour itself is.
  return typeid(*this) == typeid(TSolidColorStyle)
             ? (StrokesOnCanvas | RegionsOnCanvas)
             : NoCanvas;
}

//-----------------------------------------------------------------------------

void TSolidColorStyle::drawRegion(const TColorFunction *cf,
                                  const bool antiAliasing,
                                  TRegionOutline &boundary) const {
  TLegacyGlCanvas gl;
  drawRegionOnCanvas(gl.canvas(), cf, antiAliasing, boundary);
}

//-----------------------------------------------------------------------------

namespace {

// What a region is filled with.
TCanvasPaint regionPaint(const TPixel32 &color, bool antiAliasing,
                         const TRegionOutline &outline) {
  TCanvasPaint paint(toCanvas(color));
  paint.antialias = antiAliasing && outline.m_doAntialiasing;
  // The smooth line the old renderer drew around a region lay half outside
  // it, which is what kept regions that share an edge from showing a seam.
  // A rasteriser that computes coverage is asked for the same half pixel
  // (doc/nexttoonz_canvas_ir.md, Q1).
  if (paint.antialias) paint.growDevice = 0.5;
  return paint;
}

}  // namespace

void TSolidColorStyle::drawRegionOnCanvas(TCanvas &canvas,
                                          const TColorFunction *cf,
                                          bool antiAliasing,
                                          TRegionOutline &outline) const {
  TPixel32 color = m_color;
  if (cf) color = (*cf)(color);
  if (color.m == 0) return;

  canvas.fillPath(toCanvasPath(outline), TCanvasFillRule::NonZero,
                  regionPaint(color, antiAliasing, outline));
}

//-----------------------------------------------------------------------------

void TSolidColorStyle::drawRegionAndClipOnCanvas(TCanvas &canvas,
                                                 const TColorFunction *cf,
                                                 bool antiAliasing,
                                                 TRegionOutline &outline) const {
  TPixel32 backgroundColor = m_color;
  if (cf) backgroundColor = (*cf)(backgroundColor);
  drawRegionAndClipOnCanvas(canvas, cf, antiAliasing, outline,
                            backgroundColor.m != 0);
}

//-----------------------------------------------------------------------------

void TSolidColorStyle::drawRegionAndClipOnCanvas(TCanvas &canvas,
                                                 const TColorFunction *cf,
                                                 bool antiAliasing,
                                                 TRegionOutline &outline,
                                                 bool regionShows) const {
  canvas.save();
  if (!regionShows) {
    // Nothing to see of the region itself: only the clip, with hard edges.
    canvas.clipPath(toCanvasPath(outline), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Intersect, false);
    return;
  }

  // The old styles drew the region to the screen and to the stencil buffer
  // in one go, so that the pattern lay on the region as it was drawn, its
  // smooth edge and all; and on nothing at all, if the region's colour
  // could not be seen.
  TPixel32 color = m_color;
  if (cf) color = (*cf)(color);
  if (color.m == 0) {
    canvas.clipPath(TCanvasPath(), TCanvasFillRule::NonZero,
                    TCanvasClipOp::Intersect, false);
    return;
  }
  canvas.fillPathAndClip(toCanvasPath(outline), TCanvasFillRule::NonZero,
                         regionPaint(color, antiAliasing, outline));
}

//-----------------------------------------------------------------------------

//=============================================================================

void TSolidColorStyle::doDrawStroke(const TColorFunction *cf,
                                    TStrokeOutline *outline,
                                    const TStroke *stroke,
                                    bool antialias) const {
  TLegacyGlCanvas gl;
  drawStrokeOnCanvas(gl.canvas(), cf, outline, stroke, antialias);
}

//-----------------------------------------------------------------------------

void TSolidColorStyle::drawStrokeOnCanvas(TCanvas &canvas,
                                          const TColorFunction *cf,
                                          TStrokeOutline *outline,
                                          const TStroke *stroke,
                                          bool antialias) const {
  TPixel32 color = m_color;
  if (cf) color = (*cf)(color);
  if (color.m == 0) return;

  const std::vector<TOutlinePoint> &v = outline->getArray();
  if (v.empty()) return;

  // The outline is pairs of points, one on each side of the stroke: a
  // strip. Where the stroke bends or crosses itself its quads overlap, and
  // it is still one shape, painted once: that is what a mesh means.
  TCanvasMesh mesh;
  mesh.mode = TCanvasMesh::Mode::TriangleStrip;
  mesh.positions.reserve(v.size());
  for (const TOutlinePoint &p : v)
    mesh.positions.push_back(TCanvasPoint(p.x, p.y));

  TCanvasPaint paint(toCanvas(color));
  paint.antialias = antialias;
  // Grown, so that strokes keep the weight the old renderer gave them
  // (doc/nexttoonz_canvas_ir.md, Q2): with antialiasing a smooth band one
  // pixel wide along both sides, without it a line one pixel wide down each
  // side. Either way about half a pixel more on each side, and without it a
  // stroke thinner than a pixel would come out in pieces, or not at all.
  paint.growDevice = TVectorRasterizer::strokeGrow();
  canvas.drawMesh(mesh, paint);
}

//-----------------------------------------------------------------------------

QString TSolidColorStyle::getDescription() const { return "SolidColorStyle"; }

//-----------------------------------------------------------------------------

std::string TSolidColorStyle::getBrushIdName() const {
  return "SolidColorStyle";
}

//-----------------------------------------------------------------------------

std::size_t TSolidColorStyle::staticBrushIdHash() {
  static std::size_t bidHash = TColorStyle::generateHash("SolidColorStyle");
  return bidHash;
}

//-----------------------------------------------------------------------------

int TSolidColorStyle::getTagId() const { return 3; }

//-----------------------------------------------------------------------------

void TSolidColorStyle::loadData(TInputStreamInterface &is) {
  TPixel32 color;
  is >> color;
  m_color = color;
}

//-----------------------------------------------------------------------------

void TSolidColorStyle::saveData(TOutputStreamInterface &os) const {
  os << m_color;
}

//*************************************************************************************
//    TCenterLineStrokeStyle  implementation
//*************************************************************************************

TCenterLineStrokeStyle::TCenterLineStrokeStyle(const TPixel32 &color,
                                               USHORT stipple, double width)
    : m_color(color), m_stipple(stipple), m_width(width) {}

//-----------------------------------------------------------------------------

TColorStyle *TCenterLineStrokeStyle::clone() const {
  return new TCenterLineStrokeStyle(*this);
}

//------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------

void TCenterLineStrokeStyle::drawStroke(const TColorFunction *cf,
                                        const TStroke *stroke) const {
  // The old function touched no OpenGL state when it had nothing to draw.
  if (!stroke || stroke->getChunkCount() == 0) return;
  TPixel32 color = m_color;
  if (cf) color = (*cf)(m_color);
  if (color.m == 0) return;

  // Through OpenGL the width goes by what the driver says it can draw, as
  // it always has, and smoothing by what the caller has set.
  GLfloat range[2];
  glGetFloatv(GL_LINE_WIDTH_RANGE, &range[0]);

  // The old function switched the stipple on here, before it drew anything,
  // the ribbon included. The canvas does it for the lines alone, which by
  // the OpenGL specification draws the same thing. Mesa's software
  // rasteriser does not agree: with a dashed line that starts outside the
  // picture, where the dashes fall came out different (seen in a
  // before-and-after comparison; why, I did not find out). With the stipple
  // switched on here as well, it draws what it drew.
  if (m_stipple != 0) {
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1, m_stipple);
  }

  // It also set the width of a thin line whatever the width already was,
  // and put it back to 1 at the end. The canvas leaves the width alone when
  // it is what it needs (see tglcanvas.cpp: on one driver even a call that
  // changes nothing moves the dashes of the next stippled line), so those
  // two calls are made here, where the old code made them.
  float width = float(m_width);
  if (width < range[0])
    width = range[0];
  else if (width > range[1])
    width = range[1];
  width = 2 * width;
  if (width <= 1.0) {
    if (m_width != 0.0) width /= sqrt(tglGetPixelSize2());
    if (width == 0.F) width = 0.00001f;
    glLineWidth(width);
  }

  {
    TLegacyGlCanvas gl;
    drawStrokeOnCanvas(gl.canvas(), cf, stroke, glIsEnabled(GL_LINE_SMOOTH),
                       range[0], range[1], 0.00001f);
  }

  glLineWidth(1.0);
}

//------------------------------------------------------------------------------------

void TCenterLineStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas,
                                                const TColorFunction *cf,
                                                const TStroke *stroke,
                                                bool antialias) const {
  // No driver to ask. The narrowest line is taken to be one pixel, which is
  // what both drivers the renders have been checked on report (Mesa's
  // software rasteriser and AMD's on macOS); with it this style comes out
  // as it does through them. There is no widest line.
  drawStrokeOnCanvas(canvas, cf, stroke, antialias, 1.f,
                     std::numeric_limits<float>::max(), 1.f);
}

//------------------------------------------------------------------------------------

// \p minWidth and \p maxWidth are the line widths that can be drawn, and
// \p hairline the width to ask for when the style's is nothing. The
// arithmetic on the width is the old function's, single precision included.
void TCenterLineStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas,
                                                const TColorFunction *cf,
                                                const TStroke *stroke,
                                                bool antialias, float minWidth,
                                                float maxWidth,
                                                float hairline) const {
  if (!stroke) return;

  int i, n = stroke->getChunkCount();
  if (n == 0) return;
  TPixel32 color = m_color;
  if (cf) color = (*cf)(m_color);
  if (color.m == 0) return;
  const double pixelSize = canvas.pixelSize();

  TCanvasPaint paint(toCanvas(color));
  paint.antialias = antialias;

  TCanvasPen pen;
  pen.unit = TCanvasPen::Unit::Device;
  pen.dash = stippleToDash(m_stipple);

  const TThickQuadratic *q = stroke->getChunk(0);

  float width = float(m_width);

  if (width < minWidth)
    width = minWidth;
  else if (width > maxWidth)
    width = maxWidth;

  width = 2 * width;

  if (width <= 1.0) {
    // A line: the style's width in pixels, twice over.
    if (m_width != 0.0) width /= pixelSize;

    if (width == 0.F) width = hairline;

    TCanvasPath path;
    for (i = 0; i < n; i++) {
      q           = stroke->getChunk(i);
      double step = computeStep(*q, pixelSize);
      for (double t = 0; t < 1; t += step)
        path.lineTo(toCanvas(q->getPoint(t)));
    }
    assert(q);
    path.lineTo(toCanvas(q->getP2()));

    pen.width = width;
    canvas.strokePath(path, pen, paint);
  } else {
    // A ribbon of that half width, with a line of one pixel down each side.
    double s      = 0;
    double length = stroke->getLength();
    TPointD pos1, pos4;
    std::vector<TPointD> pv;
    width = width / 2.;
    while (s <= length) {
      double step = 1.0;
      double w    = stroke->getParameterAtLength(s);
      if (w <= 0) {
        s += 0.01;
        continue;
      }  // per tamponare il baco della getParameterAtLength()
      TPointD u = stroke->getSpeed(w);
      if (norm2(u) == 0) {
        s += 0.1;
        continue;
      }  // non dovrebbe succedere mai, ma per prudenza....
      TThickPoint pos = stroke->getThickPoint(w);
      u               = normalize(u);
      TPointD v       = rotate90(u) * (width);
      pos1            = pos + v;
      pos4            = pos - v;
      pv.push_back(pos1);
      pv.push_back(pos4);
      s += step;
    }
    double w = stroke->getParameterAtLength(length);
    if (w > 0) {
      TPointD u = stroke->getSpeed(w);
      if (norm2(u) != 0) {
        TThickPoint pos = stroke->getThickPoint(w);
        u               = normalize(u);
        TPointD v       = rotate90(u) * (width);
        pos1            = pos + v;
        pos4            = pos - v;
        pv.push_back(pos1);
        pv.push_back(pos4);
      }
    }

    if (pv.size() > 2) {
      // The ribbon was drawn quad by quad, each blended on its own and with
      // hard edges. A mesh with a colour at every vertex is drawn that way;
      // one without would be treated as a single shape (as solid strokes
      // are), which this style never was.
      TCanvasMesh ribbon;
      ribbon.mode        = TCanvasMesh::Mode::TriangleStrip;
      ribbon.smoothEdges = false;
      for (const TPointD &p : pv) ribbon.positions.push_back(toCanvas(p));
      ribbon.colors.assign(pv.size(), paint.color);
      canvas.drawMesh(ribbon, TCanvasPaint(TCanvasColor(1.f, 1.f, 1.f)));

      // The two lines, exactly the points the old loops visited: every odd
      // one, and every even one but the first. They are two contours of one
      // path, so that through OpenGL the stipple stays switched on from the
      // first to the second, as it did.
      pen.width = 1.0;
      TCanvasPath sides;
      for (std::size_t k = 1; k < pv.size(); k += 2)
        k == 1 ? sides.moveTo(toCanvas(pv[k])) : sides.lineTo(toCanvas(pv[k]));
      for (std::size_t k = 2; k < pv.size(); k += 2)
        k == 2 ? sides.moveTo(toCanvas(pv[k])) : sides.lineTo(toCanvas(pv[k]));
      canvas.strokePath(sides, pen, paint);
    }
  }
}

//-----------------------------------------------------------------------------

QString TCenterLineStrokeStyle::getDescription() const {
  return QCoreApplication::translate("TCenterLineStrokeStyle", "Constant");
}

//-----------------------------------------------------------------------------

std::string TCenterLineStrokeStyle::getBrushIdName() const {
  return "CenterLineStrokeStyle";
}

//-----------------------------------------------------------------------------

int TCenterLineStrokeStyle::getTagId() const { return 2; }

//-----------------------------------------------------------------------------

void TCenterLineStrokeStyle::loadData(TInputStreamInterface &is) {
  is >> m_color >> m_stipple >> m_width;
}

//-----------------------------------------------------------------------------

void TCenterLineStrokeStyle::saveData(TOutputStreamInterface &os) const {
  os << m_color << m_stipple << m_width;
}

//-----------------------------------------------------------------------------

int TCenterLineStrokeStyle::getParamCount() const { return 1; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TCenterLineStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

QString TCenterLineStrokeStyle::getParamNames(int index) const {
  return QCoreApplication::translate("TCenterLineStrokeStyle", "Thickness");
}

//-----------------------------------------------------------------------------

void TCenterLineStrokeStyle::getParamRange(int index, double &min,
                                           double &max) const {
  assert(0 <= index && index < getParamCount());
  min = 0.0, max = 10.0;
}

//-----------------------------------------------------------------------------

double TCenterLineStrokeStyle::getParamValue(TColorStyle::double_tag,
                                             int index) const {
  assert(0 <= index && index < getParamCount());
  return m_width;
}

//-----------------------------------------------------------------------------

void TCenterLineStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < getParamCount());
  m_width = value;
}

/*
TPixel32 TTextureStyle::getMainColor() const
{return m_averageColor; }*/

//*************************************************************************************
//    TRasterImagePatternStrokeStyle  implementation
//*************************************************************************************

TRasterImagePatternStrokeStyle::TRasterImagePatternStrokeStyle()
    : m_level(), m_name(""), m_space(0), m_rotation(0) {}

//-----------------------------------------------------------------------------

TRasterImagePatternStrokeStyle::TRasterImagePatternStrokeStyle(
    const std::string &patternName)
    : m_level(), m_name(patternName), m_space(20), m_rotation(0) {
  if (m_name != "") loadLevel(m_name);
}

//-----------------------------------------------------------------------------

TColorStyle *TRasterImagePatternStrokeStyle::clone() const {
  return new TRasterImagePatternStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

TColorStyle &TRasterImagePatternStrokeStyle::copy(const TColorStyle &other) {
  const auto *trail =
      dynamic_cast<const TRasterImagePatternStrokeStyle *>(&other);
  assert(trail);
  if (!trail) return *this;
  TColorStyle::copy(other);
  m_trailCycle       = trail->m_trailCycle;
  m_trailFrameOffset = trail->m_trailFrameOffset;
  return *this;
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::getParamRange(int index,
                                                   QStringList &items) const {
  assert(index == TrailStyles::cycleParam);
  items << QCoreApplication::translate("TRasterImagePatternStrokeStyle", "Off")
        << QCoreApplication::translate("TRasterImagePatternStrokeStyle",
                                       "Forward")
        << QCoreApplication::translate("TRasterImagePatternStrokeStyle",
                                       "Backward")
        << QCoreApplication::translate("TRasterImagePatternStrokeStyle",
                                       "Repeat");
}

//-----------------------------------------------------------------------------

int TRasterImagePatternStrokeStyle::getParamValue(TColorStyle::int_tag,
                                                  int index) const {
  assert(index == TrailStyles::cycleParam ||
         index == TrailStyles::frameOffsetParam);
  return index == TrailStyles::frameOffsetParam ? m_trailFrameOffset
                                                : int(m_trailCycle);
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::setParamValue(int index, int value) {
  if (index == TrailStyles::cycleParam)
    m_trailCycle = TrailCycle::modeFromValue(value);
  else if (index == TrailStyles::frameOffsetParam)
    setTrailFrameOffset(value);
  else
    setParamValue(index, double(value));
}

//-----------------------------------------------------------------------------

TColorStyle *TRasterImagePatternStrokeStyle::clone(
    std::string brushIdName) const {
  TRasterImagePatternStrokeStyle *style =
      new TRasterImagePatternStrokeStyle(*this);
  std::string patternName = getBrushIdNameParam(brushIdName);
  if (patternName != "") style->loadLevel(patternName);
  return style;
}

//-----------------------------------------------------------------------------

QString TRasterImagePatternStrokeStyle::getDescription() const {
  return "TRasterImagePatternStrokeStyle";
}

//-----------------------------------------------------------------------------

std::string TRasterImagePatternStrokeStyle::getBrushIdName() const {
  return "RasterImagePatternStrokeStyle:" + m_name;
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::makeIcon(const TDimension &size) {
  if (!m_level) loadLevel(m_name);
  m_icon                   = TRaster32P();
  TLevel::Iterator frameIt = m_level->begin();

  if (frameIt != m_level->end()) {
    TRasterImageP img = frameIt->second;
    TRaster32P src;
    if (img) src = img->getRaster();
    if (src && src->getLx() > 1 && src->getLy() > 1) {
      TRaster32P icon(size);
      icon->fill(TPixel32::White);
      double sx = (double)icon->getLx() / (double)src->getLx();
      double sy = (double)icon->getLy() / (double)src->getLy();
      double sc = 0.8 * std::min(sx, sy);
      TRop::resample(icon, src,
                     TScale(sc).place(src->getCenterD(), icon->getCenterD()));
      TRop::addBackground(icon, TPixel32::White);
      m_icon = icon;
    }
  }

  if (!m_icon) {
    TRaster32P icon(size);
    icon->fill(TPixel32::Black);
    int lx = icon->getLx();
    int ly = icon->getLy();
    for (int y = 0; y < ly; y++) {
      int x = ((lx - 1 - 10) * y / ly);
      icon->extractT(x, y, x + 5, y)->fill(TPixel32::Red);
    }
    m_icon = icon;
    return;
  }
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::setTrailSizeMultiplier(double multiplier) {
  multiplier = TrailScale::normalized(multiplier);
  if (m_trailSizeMultiplier == multiplier) return;
  m_trailSizeMultiplier = multiplier;
  updateVersionNumber();  // Rebuild cached placements and update drawing
                          // bounds.
  invalidateIcon();
}

//-----------------------------------------------------------------------------

int TRasterImagePatternStrokeStyle::getTrailStartFrameIndex() const {
  return findPatternSourceFrameIndex(m_level, m_trailFrameOffset);
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::getParamRange(int index, int &min,
                                                   int &max) const {
  assert(index == TrailStyles::frameOffsetParam);
  min = 0;
  max = (std::numeric_limits<int>::max)();
}

//-----------------------------------------------------------------------------

int TRasterImagePatternStrokeStyle::getParamCount() const { return 5; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TRasterImagePatternStrokeStyle::getParamType(
    int index) const {
  assert(0 <= index && index < getParamCount());
  if (index == TrailStyles::cycleParam) return TColorStyle::ENUM;
  if (index == TrailStyles::frameOffsetParam) return TColorStyle::INT;
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

QString TRasterImagePatternStrokeStyle::getParamNames(int index) const {
  assert(0 <= index && index < getParamCount());
  if (index == TrailStyles::sizeMultiplierParam)
    return QCoreApplication::translate("TRasterImagePatternStrokeStyle",
                                       "Size Multiplier");
  if (index == TrailStyles::cycleParam)
    return QCoreApplication::translate("TRasterImagePatternStrokeStyle",
                                       "Trail Cycle");
  if (index == TrailStyles::frameOffsetParam)
    return QCoreApplication::translate("TRasterImagePatternStrokeStyle",
                                       "Frame Offset");
  return (index == 0) ? QCoreApplication::translate(
                            "TRasterImagePatternStrokeStyle", "Distance")
                      : QCoreApplication::translate(
                            "TRasterImagePatternStrokeStyle", "Rotation");
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::getParamRange(int index, double &min,
                                                   double &max) const {
  assert(0 <= index && index < getParamCount());
  if (index == TrailStyles::sizeMultiplierParam) {
    min = TrailScale::minimum;
    max = TrailScale::maximum;
    return;
  }

  if (index == 0) {
    min = -50;
    max = 50;
  } else {
    min = -180;
    max = 180;
  }
}

//-----------------------------------------------------------------------------

double TRasterImagePatternStrokeStyle::getParamValue(TColorStyle::double_tag,
                                                     int index) const {
  assert(0 <= index && index < getParamCount());
  if (index == TrailStyles::sizeMultiplierParam) return m_trailSizeMultiplier;
  return (index == 0) ? m_space : m_rotation;
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < getParamCount());
  if (index == TrailStyles::sizeMultiplierParam) {
    setTrailSizeMultiplier(value);
    return;
  }

  if (index == 0) {
    if (m_space != value) {
      updateVersionNumber();  // aggiorna il vettore di affini
    }
    m_space = value;
  } else {
    if (m_rotation != value) {
      updateVersionNumber();  // aggiorna il vettore di affini
    }
    m_rotation = value;
  }
}

//-----------------------------------------------------------------------------

QString TrailStyles::sourceFilters() {
  return QStringLiteral(
      "*.pli *.tlv *.png *.tif *.tiff *.tga *.sgi *.rgb "
      "*.pct *.pic *.pict *.exr *.bmp *.jpg *.jpeg *.nol");
}

//-----------------------------------------------------------------------------

TFilePath TrailStyles::findSource(const TFilePath &root,
                                  const std::string &name) {
  // Keep the historical basename identity and ordering, but do not let a
  // palette sidecar, backup, or directory shadow an actual image source.
  TFilePathSet paths;
  TSystem::readDirectory(paths, root, true, true);
  const QStringList filters = sourceFilters().split(' ');
  for (const TFilePath &path : paths) {
    if (path.getName() == name &&
        QDir::match(filters, QString::fromStdWString(path.getLevelNameW())))
      return path;
  }
  return TFilePath();
}

//-----------------------------------------------------------------------------

TRaster32P TrailStyles::rasterSource(const TImageP &image,
                                     TPalette *levelPalette, int sourceFrame) {
  if (TRasterImageP ri = image) {
    TRasterP input = ri->getRaster();
    if (!input) return TRaster32P();
    if (TRaster32P rgba = input) return rgba;  // Leave existing RGBA untouched.
    if (TRasterGR16P gray = input) {
      // The generic converter has no GR16 -> RGBA path.
      TRaster32P rgba(input->getSize());
      gray->lock();
      rgba->lock();
      for (int y = 0; y < gray->getLy(); ++y) {
        const TPixelGR16 *source = gray->pixels(y);
        TPixel32 *target         = rgba->pixels(y);
        for (int x = 0; x < gray->getLx(); ++x) {
          const int value = (int(source[x].value) + 128) / 257;
          target[x]       = TPixel32(value, value, value, 255);
        }
      }
      rgba->setLinear(input->isLinear());
      rgba->unlock();
      gray->unlock();
      return rgba;
    }
    if (!TRaster64P(input) && !TRasterFP(input) && !TRasterGR8P(input))
      return TRaster32P();
    TRaster32P rgba(input->getSize());
    TRop::convert(rgba, input);
    return rgba;
  }

  TToonzImageP ti = image;
  if (!ti || !ti->getRaster()) return TRaster32P();
  TPaletteP palette = ti->getPalette() ? ti->getPalette() : levelPalette;
  if (!palette || palette->getStyleCount() == 0) return TRaster32P();
  if (sourceFrame >= 0 && palette->isAnimated()) {
    palette = palette->clone();
    palette->setFrame(sourceFrame);  // Never scrub the caller's palette.
  }

  TRasterCM32P input = ti->getRaster();
  const int count    = palette->getStyleCount();
  // A damaged/mismatched sidecar must not index past the conversion tables.
  input->lock();
  for (int y = 0; y < input->getLy(); ++y) {
    const TPixelCM32 *row = input->pixels(y);
    for (int x = 0; x < input->getLx(); ++x) {
      if ((row[x].getTone() < TPixelCM32::getMaxTone() &&
           row[x].getInk() >= count) ||
          (row[x].getTone() > 0 && row[x].getPaint() >= count)) {
        input->unlock();
        return TRaster32P();
      }
    }
  }
  input->unlock();
  TRaster32P rgba(input->getSize());
  TRop::convert(rgba, input, palette);
  return rgba;
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::loadLevel(const std::string &patternName) {
  struct locals {
    static TAffine getAffine(const TDimension &srcSize,
                             const TDimension &dstSize) {
      double scx = 1 * dstSize.lx / (double)srcSize.lx;
      double scy = 1 * dstSize.ly / (double)srcSize.ly;
      double sc  = std::min(scx, scy);
      double dx  = (dstSize.lx - srcSize.lx * sc) * 0.5;
      double dy  = (dstSize.ly - srcSize.ly * sc) * 0.5;
      return TScale(sc) * TTranslation(0.5 * TPointD(srcSize.lx, srcSize.ly) +
                                       TPointD(dx, dy));
    }
  };  // locals

  // button l'eventuale livello
  m_level = TLevelP();
  m_toonzRasterSource = false;

  // aggiorno il nome
  m_name = patternName;

  // getRootDir() e' nulla se non si e' chiamata la setRoot(..)
  assert(!getRootDir().isEmpty());

  const TFilePath fp = TrailStyles::findSource(getRootDir(), patternName);

  // se non ho trovato nulla esco
  if (fp == TFilePath() || !TSystem::doesExistFileOrLevel(fp)) return;
  // The TLV reader synthesizes fallback colors when its TPL is missing. Do not
  // bake those diagnostic colors into a Trail or silently guess a palette.
  if (fp.getType() == "tlv" &&
      !TFileStatus(fp.withNoFrame().withType("tpl")).doesExist())
    return;

  // Leggo i frames del livello e ne ricavo delle textures
  // che memorizzo in m_level come TRasterImage
  TLevelReaderP lr(fp);
  TLevelP level = lr->loadInfo();
  TLevel::Iterator frameIt;
  TOfflineGL *glContext = 0;

  for (frameIt = level->begin(); frameIt != level->end(); ++frameIt) {
    TImageP img = lr->getFrameReader(frameIt->first)->load();
    if (TRasterImageP(img) || TToonzImageP(img)) {
      TRaster32P ras = TrailStyles::rasterSource(
          img, level->getPalette(), frameIt->first.getNumber() - 1);
      if (!ras) continue;
      // aggiusta le dimensioni
      ras = makeTexture(ras);
      if (!ras) continue;
      m_level->setFrame(frameIt->first, new TRasterImage(ras));
      m_toonzRasterSource = bool(TToonzImageP(img));
    } else if (TVectorImageP vi = img) {
      // se il frame e' vettoriale
      // lo rasterizzo creando una texture 256x256 (N.B. le dimensioni
      // delle textures openGL devono essere potenze di due)
      TRaster32P ras(256, 256);
      // camera di default 640x480. probabilmente non e' la scelta
      // migliore.
      // TDimension cameraSize(768, 576);
      TDimension cameraSize(1920, 1080);

      // definisco i renderdata
      const TVectorRenderData rd(locals::getAffine(cameraSize, ras->getSize()),
                                 TRect(), level->getPalette(), 0, true, true);
      // rasterizzo: on the CPU if vectors are to be rasterised there,
      // otherwise through an offscreen OpenGL context
      if (!TVectorRasterizer::isSelected() ||
          !TVectorRasterizer::rasterize(ras, vi, rd)) {
        // se serve creo il contesto OpenGL offline (l'idea e' di crearne
        // uno solo per tutto il livello
        if (!glContext) glContext = new TOfflineGL(ras->getSize());
        glContext->draw(vi, rd);
        ras->copy(glContext->getRaster());
      }
      m_level->setFrame(frameIt->first, new TRasterImage(ras));
    }
  }
  // cancello il contesto offline (se e' stato creato)
  delete glContext;
}

//--------------------------------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::computeTransformations(
    std::vector<TAffine> &transformations, const TStroke *stroke) const {
  const int frameCount = m_level->getFrameCount();
  if (frameCount == 0) return;
  transformations.clear();
  const double length = stroke->getLength();

  std::vector<TDimensionD> images;
  assert(m_level->begin() != m_level->end());
  TLevel::Iterator lit;
  for (lit = m_level->begin(); lit != m_level->end(); ++lit) {
    TRasterImageP ri = lit->second;
    if (!ri) continue;
    TDimension d = ri->getRaster()->getSize();
    images.push_back(TDimensionD(d.lx, d.ly));
  }
  assert(!images.empty());
  if (images.empty()) return;

  double s  = 0;
  int index = 0;
  int m     = images.size();
  while (s < length) {
    double t      = stroke->getParameterAtLength(s);
    TThickPoint p = stroke->getThickPoint(t);
    TPointD v     = stroke->getSpeed(t);
    double ang    = rad2degree(atan(v)) + m_rotation;

    int ly    = std::max(1.0, images[index].ly);
    double sc = TrailScale::apply(p.thick / ly, m_trailSizeMultiplier);
    transformations.push_back(TTranslation(p) * TRotation(ang) * TScale(sc));
    double ds = std::max(2.0, sc * images[index].lx * 2 + m_space);
    s += ds;
  }
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::drawStroke(
    const TVectorRenderData &rd, const std::vector<TAffine> &transformations,
    const TStroke *stroke) const {
  if (m_level->getFrameCount() == 0) return;

  // The blending is this style's own, in both of the passes the caller
  // makes: TRop's indexed-color conversion is premultiplied, so its alpha
  // is applied once; pre-existing full-color Trail sources keep the legacy
  // blend mode. The canvas draws with what is set here.
  glEnable(GL_BLEND);
  glBlendFunc(m_toonzRasterSource ? GL_ONE : GL_SRC_ALPHA,
              GL_ONE_MINUS_SRC_ALPHA);
  {
    TLegacyGlCanvas gl;
    drawStrokeOnCanvas(gl.canvas(), rd, transformations, stroke);
  }
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::drawStrokeOnCanvas(
    TCanvas &canvas, const TVectorRenderData &rd,
    const std::vector<TAffine> &transformations, const TStroke *stroke) const {
  const int frameCount = m_level->getFrameCount();
  if (frameCount == 0) return;

  // lo stroke viene disegnato ripetendo size volte le frameCount immagini
  // contenute in level, posizionando ognuna secondo transformations[i]
  UINT size = transformations.size();

  // Each stamp is a quad twice the size of its picture, the whole picture
  // on it. As a mesh: the picture is laid over the unit square, and the
  // corners of the quad look it up at the corners of that square.
  TCanvasMesh quad;
  quad.mode        = TCanvasMesh::Mode::TriangleStrip;
  quad.smoothEdges = false;
  quad.uvs         = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

  // The paint adds nothing of its own (no colour function either: this
  // style never applied one).
  TCanvasPaint paint(TCanvasColor(1.f, 1.f, 1.f, 1.f));
  paint.shader.kind   = TCanvasShader::Kind::Image;
  paint.shader.tile   = TCanvasTile::Repeat;
  paint.shader.filter = TCanvasFilter::Linear;

  // visto che cambiare texture costa tempo il ciclo esterno e' sulle textures
  // piuttosto che sulle trasformazioni
  TLevel::Iterator frameIt     = getPatternFrameIterator(m_level, stroke);
  const int frameStep          = stroke->outlineOptions().m_patternFrameStep;
  const int distinctFrameCount = (frameStep % frameCount == 0) ? 1 : frameCount;
  for (int i = 0; i < (int)size && i < distinctFrameCount; ++i) {
    TLevel::Iterator currentFrame = frameIt;
    advancePatternFrameIterator(frameIt, m_level, frameStep);
    TRasterImageP ri = currentFrame->second;
    TRaster32P ras;
    if (ri) ras = ri->getRaster();
    if (!ras) continue;

    const double rx = ras->getLx();
    const double ry = ras->getLy();

    // The canvas may keep the picture for the stamps that follow, so it is
    // told what keeps the pixels alive.
    TCanvasImage &image = paint.shader.image;
    image.keepAlive     = std::make_shared<TRaster32P>(ras);
    image.pixels        = ras->getRawData();
    image.width         = ras->getLx();
    image.height        = ras->getLy();
    image.rowBytes      = (std::ptrdiff_t)ras->getWrap() * 4;
    // What the two blend modes of the OpenGL version amount to.
    image.premultiplied       = m_toonzRasterSource;
    paint.shader.imageToLocal = TCanvasAffine::scale(1.0 / rx, 1.0 / ry);

    quad.positions = {{-rx, -ry}, {rx, -ry}, {-rx, ry}, {rx, ry}};

    for (int j = i; j < (int)size; j += distinctFrameCount) {
      canvas.save();
      canvas.concat(toCanvas(rd.m_aff * transformations[j]));
      canvas.drawMesh(quad, paint);
      canvas.restore();
    }
  }
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::loadData(TInputStreamInterface &is) {
  m_level = TLevelP();
  m_name  = "";
  std::string name;
  is >> name >> m_space >> m_rotation;
  if (name != "") {
    try {
      loadLevel(name);
    } catch (...) {
    }
  }
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::loadData(int ids,
                                              TInputStreamInterface &is) {
  if (ids != 100)
    throw TException("image pattern stroke style: unknown obsolete format");

  m_level = TLevelP();
  m_name  = "";
  std::string name;
  is >> name;
  if (name != "") {
    try {
      loadLevel(name);
    } catch (...) {
    }
  }
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::saveData(
    TOutputStreamInterface &os) const {
  os << m_name << m_space << m_rotation;
}

//-----------------------------------------------------------------------------

TFilePath TRasterImagePatternStrokeStyle::getRootDir() { return m_rootDir; }

//-----------------------------------------------------------------------------

TStrokeProp *TRasterImagePatternStrokeStyle::makeStrokeProp(
    const TStroke *stroke) {
  return new TRasterImagePatternStrokeProp(stroke, this);
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeStyle::getObsoleteTagIds(
    std::vector<int> &ids) const {
  ids.push_back(100);
}

//-----------------------------------------------------------------------------

TRectD TRasterImagePatternStrokeStyle::getStrokeBBox(
    const TStroke *stroke) const {
  TRectD rect = TColorStyle::getStrokeBBox(stroke);
  double padding = std::max(rect.getLx() * 0.25, rect.getLy() * 0.25);
  if (!m_level || m_level->getFrameCount() == 0) return rect.enlarge(padding);

  // Legacy raster placement uses the first valid frame's height for every
  // stamp. The quad uses +/-width and +/-height, including transparent padding.
  double referenceHeight = 0.0;
  double sourceRadius    = 0.0;
  for (auto it = m_level->begin(); it != m_level->end(); ++it) {
    TRasterImageP image = it->second;
    if (!image || !image->getRaster()) continue;
    const TDimension size = image->getRaster()->getSize();
    if (referenceHeight == 0.0) referenceHeight = (std::max)(1, size.ly);
    sourceRadius =
        (std::max)(sourceRadius, TrailScale::radius(size.lx, size.ly, 1.0));
  }
  if (referenceHeight > 0.0) {
    const double scale =
        TrailScale::apply(maximumPatternThickness(stroke) / referenceHeight,
                          m_trailSizeMultiplier);
    padding = (std::max)(padding, sourceRadius * scale);
  }
  return rect.enlarge(padding);
}

//*************************************************************************************
//    TVectorImagePatternStrokeStyle  implementation
//*************************************************************************************

TVectorImagePatternStrokeStyle::TVectorImagePatternStrokeStyle()
    : m_level(), m_name(""), m_space(0), m_rotation(0) {}

//-----------------------------------------------------------------------------

TVectorImagePatternStrokeStyle::TVectorImagePatternStrokeStyle(
    const std::string &patternName)
    : m_level(), m_name(patternName), m_space(20), m_rotation(0) {
  loadLevel(patternName);
}

//-----------------------------------------------------------------------------

TColorStyle *TVectorImagePatternStrokeStyle::clone() const {
  return new TVectorImagePatternStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

TColorStyle &TVectorImagePatternStrokeStyle::copy(const TColorStyle &other) {
  const auto *trail =
      dynamic_cast<const TVectorImagePatternStrokeStyle *>(&other);
  assert(trail);
  if (!trail) return *this;
  TColorStyle::copy(other);
  m_trailCycle       = trail->m_trailCycle;
  m_trailFrameOffset = trail->m_trailFrameOffset;
  return *this;
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::getParamRange(int index,
                                                   QStringList &items) const {
  assert(index == TrailStyles::cycleParam);
  items << QCoreApplication::translate("TVectorImagePatternStrokeStyle", "Off")
        << QCoreApplication::translate("TVectorImagePatternStrokeStyle",
                                       "Forward")
        << QCoreApplication::translate("TVectorImagePatternStrokeStyle",
                                       "Backward")
        << QCoreApplication::translate("TVectorImagePatternStrokeStyle",
                                       "Repeat");
}

//-----------------------------------------------------------------------------

int TVectorImagePatternStrokeStyle::getParamValue(TColorStyle::int_tag,
                                                  int index) const {
  assert(index == TrailStyles::cycleParam ||
         index == TrailStyles::frameOffsetParam);
  return index == TrailStyles::frameOffsetParam ? m_trailFrameOffset
                                                : int(m_trailCycle);
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::setParamValue(int index, int value) {
  if (index == TrailStyles::cycleParam)
    m_trailCycle = TrailCycle::modeFromValue(value);
  else if (index == TrailStyles::frameOffsetParam)
    setTrailFrameOffset(value);
  else
    setParamValue(index, double(value));
}

//-----------------------------------------------------------------------------

TColorStyle *TVectorImagePatternStrokeStyle::clone(
    std::string brushIdName) const {
  TVectorImagePatternStrokeStyle *style =
      new TVectorImagePatternStrokeStyle(*this);
  std::string patternName = getBrushIdNameParam(brushIdName);
  if (patternName != "") style->loadLevel(patternName);
  return style;
}

//-----------------------------------------------------------------------------

QString TVectorImagePatternStrokeStyle::getDescription() const {
  return "TVectorImagePatternStrokeStyle";
}

//-----------------------------------------------------------------------------

std::string TVectorImagePatternStrokeStyle::getBrushIdName() const {
  return "VectorImagePatternStrokeStyle:" + m_name;
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::makeIcon(const TDimension &size) {
  TLevel::Iterator frameIt = m_level->begin();
  if (frameIt == m_level->end()) {
    TRaster32P icon(size);
    icon->fill(TPixel32::Black);
    int lx = icon->getLx();
    int ly = icon->getLy();
    for (int y = 0; y < ly; y++) {
      int x = ((lx - 1 - 10) * y / ly);
      icon->extractT(x, y, x + 5, y)->fill(TPixel32::Red);
    }
    m_icon = icon;
    return;
  }
  TFrameId fid      = frameIt->first;
  TVectorImageP img = m_level->frame(fid);
  if (!img) {
    TRaster32P icon(size);
    icon->fill(TPixel32::Black);
    int lx = icon->getLx();
    int ly = icon->getLy();
    for (int y = 0; y < ly; y++) {
      int x = ((lx - 1 - 10) * y / ly);
      icon->extractT(x, y, x + 5, y)->fill(TPixel32::Red);
    }
    m_icon = icon;
    return;
  }
  // img->setPalette(m_level->getPalette());
  TPalette *vPalette = m_level->getPalette();
  assert(vPalette);
  img->setPalette(vPalette);
  TRectD bbox = img->getBBox();
  double scx  = 0.8 * size.lx / bbox.getLx();
  double scy  = 0.8 * size.ly / bbox.getLy();
  double sc   = std::min(scx, scy);
  double dx   = (size.lx - bbox.getLx() * sc) * 0.5;
  double dy   = (size.ly - bbox.getLy() * sc) * 0.5;
  TAffine aff = TScale(sc) * TTranslation(-bbox.getP00() + TPointD(dx, dy));
  TVectorRenderData rd(aff, size, vPalette, 0, true);

  // On the CPU when renders are to be done there and the stamp's styles can
  // be drawn that way; otherwise, as before, with OpenGL.
  if (TVectorRasterizer::isSelected()) {
    TRaster32P icon(size);
    icon->fill(TPixel32::White);
    if (TVectorRasterizer::draw(icon, img.getPointer(), rd)) {
      m_icon = icon;
      return;
    }
  }

  TOfflineGL *glContext = TOfflineGL::getStock(size);
  glContext->clear(TPixel32::White);
  glContext->draw(img, rd);
  if (!m_icon || m_icon->getSize() != size)
    m_icon = glContext->getRaster()->clone();
  else
    m_icon->copy(glContext->getRaster());
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::setTrailSizeMultiplier(double multiplier) {
  multiplier = TrailScale::normalized(multiplier);
  if (m_trailSizeMultiplier == multiplier) return;
  m_trailSizeMultiplier = multiplier;
  updateVersionNumber();  // Rebuild cached placements and update drawing
                          // bounds.
  invalidateIcon();
}

//-----------------------------------------------------------------------------

int TVectorImagePatternStrokeStyle::getTrailStartFrameIndex() const {
  return findPatternSourceFrameIndex(m_level, m_trailFrameOffset);
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::getParamRange(int index, int &min,
                                                   int &max) const {
  assert(index == TrailStyles::frameOffsetParam);
  min = 0;
  max = (std::numeric_limits<int>::max)();
}

//-----------------------------------------------------------------------------

int TVectorImagePatternStrokeStyle::getParamCount() const { return 5; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TVectorImagePatternStrokeStyle::getParamType(
    int index) const {
  assert(0 <= index && index < getParamCount());
  if (index == TrailStyles::cycleParam) return TColorStyle::ENUM;
  if (index == TrailStyles::frameOffsetParam) return TColorStyle::INT;
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

QString TVectorImagePatternStrokeStyle::getParamNames(int index) const {
  assert(0 <= index && index < getParamCount());
  if (index == TrailStyles::sizeMultiplierParam)
    return QCoreApplication::translate("TVectorImagePatternStrokeStyle",
                                       "Size Multiplier");
  if (index == TrailStyles::cycleParam)
    return QCoreApplication::translate("TVectorImagePatternStrokeStyle",
                                       "Trail Cycle");
  if (index == TrailStyles::frameOffsetParam)
    return QCoreApplication::translate("TVectorImagePatternStrokeStyle",
                                       "Frame Offset");
  return (index == 0) ? QCoreApplication::translate(
                            "TVectorImagePatternStrokeStyle", "Distance")
                      : QCoreApplication::translate(
                            "TVectorImagePatternStrokeStyle", "Rotation");
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::getParamRange(int index, double &min,
                                                   double &max) const {
  assert(0 <= index && index < getParamCount());
  if (index == TrailStyles::sizeMultiplierParam) {
    min = TrailScale::minimum;
    max = TrailScale::maximum;
    return;
  }

  if (index == 0) {
    min = -50;
    max = 50;
  } else {
    min = -180;
    max = 180;
  }
}

//-----------------------------------------------------------------------------

double TVectorImagePatternStrokeStyle::getParamValue(TColorStyle::double_tag,
                                                     int index) const {
  assert(0 <= index && index < getParamCount());
  if (index == TrailStyles::sizeMultiplierParam) return m_trailSizeMultiplier;
  return index == 0 ? m_space : m_rotation;
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < getParamCount());
  if (index == TrailStyles::sizeMultiplierParam) {
    setTrailSizeMultiplier(value);
    return;
  }

  if (index == 0) {
    if (m_space != value) {
      updateVersionNumber();  // aggiorna il vettore di affini
    }
    m_space = value;
  } else {
    if (m_rotation != value) {
      updateVersionNumber();  // aggiorna il vettore di affini
    }
    m_rotation = value;
  }
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::loadLevel(const std::string &patternName) {
  m_level = TLevelP();
  m_name  = patternName;
  assert(!getRootDir()
              .isEmpty());  // se e' vuota, non si e' chiamata la setRoot(..)
  TFilePath fp = getRootDir() + (patternName + ".pli");
  TLevelReaderP lr(fp);
  m_level = lr->loadInfo();
  TLevel::Iterator frameIt;
  std::map<TPixel32, int> colors;
  for (frameIt = m_level->begin(); frameIt != m_level->end(); ++frameIt) {
    TVectorImageP img = lr->getFrameReader(frameIt->first)->load();
    if (img) m_level->setFrame(frameIt->first, img);
  }
}

//--------------------------------------------------------------------------------------------------

namespace {

TLevel::Iterator getPatternFrameIterator(const TLevelP &level,
                                         const TStroke *stroke) {
  const int frameCount = level->getFrameCount();
  assert(frameCount > 0);

  int offset = stroke->outlineOptions().m_patternFrameOffset % frameCount;
  if (offset < 0) offset += frameCount;

  TLevel::Iterator it = level->begin();
  while (offset-- > 0) {
    if (++it == level->end()) it = level->begin();
  }
  return it;
}

// A step of zero holds the iterator still: the stroke shows one frame along
// its whole length, which is how the brush's Repeat cycle mode freezes a
// stamp.
void advancePatternFrameIterator(TLevel::Iterator &it, const TLevelP &level,
                                 int step) {
  const int frameCount = level->getFrameCount();
  assert(frameCount > 0);

  step %= frameCount;
  if (step < 0) step += frameCount;
  if (step > frameCount / 2) step -= frameCount;

  while (step > 0) {
    if (++it == level->end()) it = level->begin();
    --step;
  }
  while (step < 0) {
    if (it == level->begin()) it = level->end();
    --it;
    ++step;
  }
}

}  // namespace

//--------------------------------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::computeTransformations(
    std::vector<TAffine> &transformations, const TStroke *stroke) const {
  const int frameCount = m_level->getFrameCount();
  if (frameCount == 0) return;
  transformations.clear();
  const double length = stroke->getLength();
  assert(m_level->begin() != m_level->end());
  TLevel::Iterator lit = getPatternFrameIterator(m_level, stroke);
  const int frameStep  = stroke->outlineOptions().m_patternFrameStep;
  double s             = 0;

  while (s < length) {
    TFrameId fid      = lit->first;
    TVectorImageP img = m_level->frame(fid);
    advancePatternFrameIterator(lit, m_level, frameStep);
    assert(img);
    if (img->getType() != TImage::VECTOR) return;
    double t       = stroke->getParameterAtLength(s);
    TThickPoint p  = stroke->getThickPoint(t);
    TPointD v      = stroke->getSpeed(t);
    double ang     = rad2degree(atan(v)) + m_rotation;
    TRectD bbox    = img->getBBox();
    TPointD center = 0.5 * (bbox.getP00() + bbox.getP11());
    double ry      = bbox.getLy() * 0.5;
    if (ry * ry < 1e-5) ry = p.thick;
    double sc = p.thick / ry;
    if (sc < 0.0001) sc = 0.0001;
    sc = TrailScale::apply(sc, m_trailSizeMultiplier);
    TAffine aff =
        TTranslation(p) * TRotation(ang) * TScale(sc) * TTranslation(-center);
    transformations.push_back(aff);
    double ds = std::max(2.0, sc * bbox.getLx() + m_space);
    s += ds;
  }
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::clearGlDisplayLists() {
#ifdef _WIN32
  DisplayListManager *pmgr = DisplayListManager::instance();
  assert(pmgr);
  pmgr->clearLists();
#endif
}

//--------------------------------------------------------------------------------------------------

namespace {

// How a stamp of a vector pattern gets drawn once the transform is in place:
// by the old renderer straight to OpenGL, or on the canvas.
void paintStampWithGl(TCanvas &, const TVectorRenderData &rd,
                      const TVectorImage *stamp) {
  tglDraw(rd, stamp);
}

void paintStampOnCanvas(TCanvas &canvas, const TVectorRenderData &rd,
                        const TVectorImage *stamp) {
  tcanvasDraw(canvas, rd, stamp);
}

// The settings every stamp is drawn with.
TVectorRenderData stampRenderData(const TVectorImage *stamp,
                                  const TColorFunction *cf) {
  return TVectorRenderData(TAffine(), TRect(), stamp->getPalette(), cf, true,
                           true);
}

}  // namespace

//--------------------------------------------------------------------------------------------------

int TVectorImagePatternStrokeStyle::canvasSupport() const {
  for (TLevel::Iterator it = m_level->begin(); it != m_level->end(); ++it) {
    const TVectorImageP stamp = it->second;
    if (!stamp) continue;
    if (!tcanvasCanDraw(stampRenderData(stamp.getPointer(), 0),
                        stamp.getPointer()))
      return NoCanvas;
  }
  return StrokesOnCanvas;
}

//--------------------------------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::drawStroke(
    const TVectorRenderData &rd, const std::vector<TAffine> &transformations,
    const TStroke *stroke) const {
  // The canvas only places the stamps; they are drawn by the old renderer,
  // which sets up OpenGL for each of their styles as it always did.
  TLegacyGlCanvas gl;
  drawStamps(gl.canvas(), rd, transformations, stroke, &paintStampWithGl);
}

//--------------------------------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::drawStrokeOnCanvas(
    TCanvas &canvas, const TVectorRenderData &rd,
    const std::vector<TAffine> &transformations, const TStroke *stroke) const {
  drawStamps(canvas, rd, transformations, stroke, &paintStampOnCanvas);
}

//--------------------------------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::drawStamps(
    TCanvas &canvas, const TVectorRenderData &rd,
    const std::vector<TAffine> &transformations, const TStroke *stroke,
    StampPainter paintStamp) const {
  const int frameCount = m_level->getFrameCount();
  if (frameCount == 0) return;
  //------------------------------------------
  // if the average thickness of the stroke, with the current pixelSize,
  // is less than 2, do not drow anything
  UINT cpCount    = stroke->getControlPointCount();
  UINT sampleStep = (cpCount < 10) ? 1 : (UINT)((double)cpCount / 10.0);
  double thickSum = 0;
  UINT count      = 0;
  for (UINT cp = 0; cp < cpCount; cp += sampleStep) {
    thickSum += stroke->getControlPoint(cp).thick;
    count++;
  }
  double averageThick =
      TrailScale::apply(thickSum / (double)count, m_trailSizeMultiplier);
  canvas.save();
  canvas.concat(toCanvas(rd.m_aff));
  const double pixelSize = canvas.pixelSize();
  double pixelSize2      = pixelSize * pixelSize;
  canvas.restore();
  // A single stamp has no useful line-strip approximation: short carriers
  // produce zero vertices in that path. Draw the source at its actual size.
  if (transformations.size() > 1 &&
      averageThick * averageThick < 4 * pixelSize2) {
    canvas.save();
    canvas.concat(toCanvas(rd.m_aff));

    TLevel::Iterator lit = getPatternFrameIterator(m_level, stroke);

    TFrameId fid      = lit->first;
    TVectorImageP img = m_level->frame(fid);

    std::set<int> styles;
    img->getUsedStyles(styles);

    std::set<int>::iterator si = styles.begin();

    // Calcolo media dei colori utilizzati

    int numStyles  = 0, styleID;
    int averageRed = 0, averageBlue = 0, averageGreen = 0;
    TPixel32 color, averageColor;
    TColorStyle *colorStyle;

    for (; si != styles.end(); si++) {
      styleID = (int)*si;

      if (styleID != 0) {
        colorStyle = m_level->getPalette()->getStyle(styleID);
        color      = colorStyle->getAverageColor();
        if (color.m != 0) {
          numStyles++;
          averageRed += color.r;
          averageGreen += color.g;
          averageBlue += color.b;
        }
      }
    }

    if (numStyles != 0) {
      averageColor.r = averageRed / numStyles;
      averageColor.g = averageGreen / numStyles;
      averageColor.b = averageBlue / numStyles;
    } else
      averageColor = TPixel32::Black;

    // Too small to show the stamps: a dashed line of their average colour,
    // one pixel wide, two pixels on and two off (the stipple 0xAAAA taken
    // twice over).
    TCanvasPaint paint(toCanvas(averageColor));
    paint.antialias = rd.m_antiAliasing;
    TCanvasPen pen;
    pen.width = 1.0;
    pen.unit  = TCanvasPen::Unit::Device;
    pen.dash  = stippleToDash(0xAAAA);
    for (double &length : pen.dash) length *= 2.0;

    UINT n = stroke->getLength() *
             0.05;  // TODO: funzione di stroke length e pixelsize
    TCanvasPath path;
    for (UINT i = 0; i < n; i++) {
      if (n != 1) {
        TThickPoint p = stroke->getPoint(i / (double)(n - 1));
        path.lineTo(TCanvasPoint(p.x, p.y));
      }
    }
    canvas.strokePath(path, pen, paint);

    canvas.restore();
  } else {
    //--------------------------------------------
    assert(m_level->begin() != m_level->end());
    TLevel::Iterator lit = getPatternFrameIterator(m_level, stroke);
    const int frameStep  = stroke->outlineOptions().m_patternFrameStep;
    UINT i, size = transformations.size();

    for (i = 0; i < size; i++) {
      TFrameId fid      = lit->first;
      TVectorImageP img = m_level->frame(fid);
      advancePatternFrameIterator(lit, m_level, frameStep);
      assert(img);
      if (!img) continue;
      if (img->getType() != TImage::VECTOR) return;
      TVectorImage *imgPointer    = img.getPointer();
      TAffine totalTransformation = rd.m_aff * transformations[i];
      if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
          !(convert(totalTransformation * imgPointer->getBBox())
                .overlaps(rd.m_clippingRect)))
        continue;
      // drawing
      assert(imgPointer->getPalette());

      canvas.save();
      canvas.concat(toCanvas(totalTransformation));
      paintStamp(canvas, stampRenderData(imgPointer, rd.m_cf), imgPointer);
      canvas.restore();
    }
  }
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::loadData(TInputStreamInterface &is) {
  m_level = TLevelP();
  m_name  = "";
  std::string name;
  is >> name >> m_space >> m_rotation;
  if (name != "") {
    try {
      loadLevel(name);
    } catch (...) {
    }
  }
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::loadData(int ids,
                                              TInputStreamInterface &is) {
  if (ids != 100)
    throw TException("image pattern stroke style: unknown obsolete format");

  m_level = TLevelP();
  m_name  = "";
  std::string name;
  is >> name;
  if (name != "") {
    try {
      loadLevel(name);
    } catch (...) {
    }
  }
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::saveData(
    TOutputStreamInterface &os) const {
  os << m_name << m_space << m_rotation;
}

//-----------------------------------------------------------------------------

TFilePath TVectorImagePatternStrokeStyle::getRootDir() { return m_rootDir; }

//-----------------------------------------------------------------------------

TStrokeProp *TVectorImagePatternStrokeStyle::makeStrokeProp(
    const TStroke *stroke) {
  return new TVectorImagePatternStrokeProp(stroke, this);
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeStyle::getObsoleteTagIds(
    std::vector<int> &ids) const {
  // ids.push_back(100);
}

//-----------------------------------------------------------------------------

TRectD TVectorImagePatternStrokeStyle::getStrokeBBox(
    const TStroke *stroke) const {
  TRectD rect = TColorStyle::getStrokeBBox(stroke);
  double padding = std::max(rect.getLx() * 0.25, rect.getLy() * 0.25);
  if (!m_level || m_level->getFrameCount() == 0) return rect.enlarge(padding);

  const double thickness = maximumPatternThickness(stroke);
  for (auto it = m_level->begin(); it != m_level->end(); ++it) {
    TVectorImageP image = it->second;
    if (!image) continue;
    const TRectD source = image->getBBox();
    if (source.isEmpty()) continue;
    const double halfHeight = source.getLy() * 0.5;
    // Match the renderer's nearly-flat source fallback and minimum scale.
    double scale = halfHeight * halfHeight < 1e-5
                       ? 1.0
                       : (std::max)(0.0001, thickness / halfHeight);
    scale        = TrailScale::apply(scale, m_trailSizeMultiplier);
    padding      = (std::max)(
        padding, TrailScale::radius(source.getLx() * 0.5, halfHeight, scale));
  }
  return rect.enlarge(padding);
}

//*************************************************************************************
//    Style declarations  instances
//*************************************************************************************

namespace {

TColorStyle::Declaration s0(new TCenterLineStrokeStyle());
TColorStyle::Declaration s1(new TSolidColorStyle());
TColorStyle::Declaration s3(new TRasterImagePatternStrokeStyle());
TColorStyle::Declaration s4(new TVectorImagePatternStrokeStyle());
}  // namespace
