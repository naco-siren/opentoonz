

// TnzCore includes
#include "tcolorfunctions.h"
#include "trandom.h"
#include "tcurves.h"
#include "tvectorrenderdata.h"
#include "tmathutil.h"
#include "colorfxutils.h"
#include "tpixelutils.h"
#include "tconvert.h"

// tcg includes
#include "tcg/tcg_misc.h"

#include "strokestyles.h"

using namespace std;

#define MINTHICK 1.0

//=============================================================================

#include "colorfxcanvas.h"
#include "tglentries.h"
#ifdef _DEBUG
#include "tgl.h"  // OutlineViewerStyle, a style for debugging
#endif

using colorfxcanvas::addPolyline;
using colorfxcanvas::addSegment;
using colorfxcanvas::addVertex;
using colorfxcanvas::drawCircle;
using colorfxcanvas::drawQuad;
using colorfxcanvas::drawQuadStrip;
using colorfxcanvas::floatColor;
using colorfxcanvas::linePaint;
using colorfxcanvas::plainMesh;
using colorfxcanvas::plainPaint;
using colorfxcanvas::strokeColoredPolyline;
using colorfxcanvas::strokeLines;
using colorfxcanvas::thinPen;

namespace {

template <class T>
class TOptimizedStrokePropT final : public TStrokeProp {
protected:
  double m_pixelSize;

  TOptimizedStrokeStyleT<T> *m_colorStyle;
  T m_data;

public:
  TOptimizedStrokePropT(const TStroke *stroke,
                        TOptimizedStrokeStyleT<T> *style);
  ~TOptimizedStrokePropT() { m_colorStyle->release(); }

  const TColorStyle *getColorStyle() const override;

  TStrokeProp *clone(const TStroke *stroke) const override;
  void draw(const TVectorRenderData &rd) override;
  void drawOnCanvas(TCanvas &canvas, const TVectorRenderData &rd) override;

private:
  void updateData(double pixelSize, const TColorFunction *cf);
};

//-----------------------------------------------------------------------------

template <class T>
TOptimizedStrokePropT<T>::TOptimizedStrokePropT(
    const TStroke *stroke, TOptimizedStrokeStyleT<T> *style)
    : TStrokeProp(stroke), m_colorStyle(style), m_pixelSize(0) {
  m_styleVersionNumber = style->getVersionNumber();
  m_colorStyle->addRef();
}

//-----------------------------------------------------------------------------

template <class T>
const TColorStyle *TOptimizedStrokePropT<T>::getColorStyle() const {
  return m_colorStyle;
}

//-----------------------------------------------------------------------------

template <class T>
TStrokeProp *TOptimizedStrokePropT<T>::clone(const TStroke *stroke) const {
  TOptimizedStrokePropT<T> *prop =
      new TOptimizedStrokePropT<T>(stroke, m_colorStyle);
  prop->m_strokeChanged = m_strokeChanged;
  prop->m_data          = m_data;
  return prop;
}

//-----------------------------------------------------------------------------

template <class T>
void TOptimizedStrokePropT<T>::draw(
    const TVectorRenderData
        &rd) /*assenza di const non e' una dimenticanza! Alcune sottoclassi
                devono ridefinire questo metodo e serbve che non sia const*/
{
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_stroke->getBBox()).overlaps(rd.m_clippingRect))
    return;

  TGLEntries::pushTransform(rd.m_aff);

  updateData(sqrt(TGLEntries::pixelSize2()), rd.m_cf);

  m_colorStyle->drawStroke(rd.m_cf, m_data, m_stroke);

  TGLEntries::popTransform();
}

//-----------------------------------------------------------------------------

template <class T>
void TOptimizedStrokePropT<T>::updateData(double pixelSize,
                                          const TColorFunction *cf) {
  if (m_strokeChanged ||
      m_styleVersionNumber != m_colorStyle->getVersionNumber() ||
      !isAlmostZero(pixelSize - m_pixelSize, 1e-5)) {
    m_strokeChanged      = false;
    m_pixelSize          = pixelSize;
    m_styleVersionNumber = m_colorStyle->getVersionNumber();
    // The pixel size goes along: some styles used to read it from OpenGL
    // once more in here, and on a canvas there may be no OpenGL.
    m_colorStyle->computeData(m_data, m_stroke, cf, pixelSize);
  }
}

//-----------------------------------------------------------------------------

// draw() without OpenGL. Keep the two in step.
template <class T>
void TOptimizedStrokePropT<T>::drawOnCanvas(TCanvas &canvas,
                                            const TVectorRenderData &rd) {
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_stroke->getBBox()).overlaps(rd.m_clippingRect))
    return;

  canvas.save();
  canvas.concat(toCanvas(rd.m_aff));

  updateData(canvas.pixelSize(), rd.m_cf);

  m_colorStyle->drawStrokeOnCanvas(canvas, rd.m_cf, m_data, m_stroke,
                                   rd.m_antiAliasing);

  canvas.restore();
}
}

//=============================================================================

template <class T>
TStrokeProp *TOptimizedStrokeStyleT<T>::makeStrokeProp(const TStroke *stroke) {
  return new TOptimizedStrokePropT<T>(stroke, this);
}

//=============================================================================

TFurStrokeStyle::TFurStrokeStyle()
    : m_color(TPixel32::Black)
    , m_angle(120.0)
    , m_length(1.0)
    , m_cs(0.0)
    , m_sn(0.0) {
  double rad = m_angle * M_PI_180;
  m_cs       = cos(rad);
  m_sn       = sin(rad);
}

//-----------------------------------------------------------------------------

TColorStyle *TFurStrokeStyle::clone() const {
  return new TFurStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TFurStrokeStyle::getParamCount() const { return 2; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TFurStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TFurStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < getParamCount());
  return index == 0
             ? TTranslatableText("TFurStrokeStyle",
                                 QT_TRANSLATE_NOOP("TFurStrokeStyle", "Angle"))
             : TTranslatableText("TFurStrokeStyle",
                                 QT_TRANSLATE_NOOP("TFurStrokeStyle", "Size"));
}

//-----------------------------------------------------------------------------

void TFurStrokeStyle::getParamRange(int index, double &min, double &max) const {
  assert(0 <= index && index < getParamCount());
  if (index == 0) {
    min = 0.0;
    max = 180.0;
  } else {
    min = 0.0;
    max = 1.0;
  }
}

//-----------------------------------------------------------------------------

double TFurStrokeStyle::getParamValue(TColorStyle::double_tag,
                                      int index) const {
  assert(0 <= index && index < getParamCount());
  return index == 0 ? m_angle : m_length;
}

//-----------------------------------------------------------------------------

void TFurStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < getParamCount());
  if (index == 0) {
    m_angle    = value;
    double rad = m_angle * M_PI_180;
    m_cs       = cos(rad);
    m_sn       = sin(rad);
  } else
    m_length = value;

  updateVersionNumber();
}

//-----------------------------------------------------------------------------
void TFurStrokeStyle::computeData(Points &positions, const TStroke *stroke,
                                  const TColorFunction *cf,
    double pixelSize) const {
  double length = stroke->getLength();

  double s  = 0.0;
  double ds = 4;
  double vs = 1;
  TRandom rnd;

  positions.clear();
  positions.reserve(tceil(length / ds) + 1);

  while (s <= length) {
    double w        = stroke->getParameterAtLength(s);
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    if (norm2(u) == 0.0) {
      s += 0.5;
      continue;
    }
    u         = normalize(u);
    TPointD v = rotate90(u);

    double length = 0;
    if (pos.thick)
      length = m_length * pos.thick;
    else
      length = 1.0;

    vs = -vs;

    positions.push_back(pos);
    double q = 0.01 * (rnd.getFloat() * 2 - 1);
    positions.push_back(pos + length * ((m_cs + q) * u + (vs * m_sn) * v));

    s += ds;
  }
}

//-----------------------------------------------------------------------------
void TFurStrokeStyle::drawStroke(const TColorFunction *cf, Points &positions,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, positions, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TFurStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    Points &positions, const TStroke *stroke, bool antialias) const {
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  // Each hair is a line from the stroke outwards that fades to nothing,
  // by way of white.
  const std::vector<TCanvasColor> colors = {toCanvas(color),
                                            TCanvasColor(1.f, 1.f, 1.f, 0.f)};
  for (UINT i = 0; i + 1 < positions.size(); i += 2)
    strokeColoredPolyline(canvas, {positions[i], positions[i + 1]}, colors,
                          antialias);
}

//=============================================================================

TChainStrokeStyle::TChainStrokeStyle(const TPixel32 &color) : m_color(color) {}

//-----------------------------------------------------------------------------

TChainStrokeStyle::TChainStrokeStyle() : m_color(TPixel32(20, 10, 0)) {}

//-----------------------------------------------------------------------------

TColorStyle *TChainStrokeStyle::clone() const {
  return new TChainStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

void TChainStrokeStyle::computeData(Points &data, const TStroke *stroke,
                                    const TColorFunction *cf,
    double pixelSize) const {
  // TStroke *stroke = getStroke();
  double length = stroke->getLength();

  // spessore della catena = spessore "medio" dello stroke
  double thickness =
      0.25 *
      (stroke->getThickPoint(0).thick + stroke->getThickPoint(1.0 / 3.0).thick +
       stroke->getThickPoint(2.0 / 3.0).thick + stroke->getThickPoint(1).thick);

  if (thickness == 0) thickness = 0.1;
  double ringHeight             = thickness;
  double ringWidth              = 1.5 * ringHeight;

  // distanza fra i centri degli anelli
  double ringDistance = 2 * 1.2 * ringWidth;

  // distanza fra il centro dell'anello e il punto di attacco dell'anello
  // trasversale
  // double joinPos = 0.45 * ringWidth;

  TPointD oldPos;
  // bool firstRing = true;
  double s = 0;

  data.clear();
  data.reserve(tceil(length / ringDistance) * 2 + 2);

  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    // if(w<0) {s+=0.1; continue;} // per tamponare il baco della
    // getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    if (norm2(u) == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u = normalize(u);

    data.push_back(pos);
    data.push_back(u);

    // se e' il caso unisco la catena alla precedente
    s += ringDistance;
  }
}

//-----------------------------------------------------------------------------

void TChainStrokeStyle::drawStroke(const TColorFunction *cf, Points &data,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, data, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TChainStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    Points &data, const TStroke *stroke, bool antialias) const {
  // spessore della catena = spessore "medio" dello stroke
  double thickness =
      0.25 *
      (stroke->getThickPoint(0).thick + stroke->getThickPoint(1.0 / 3.0).thick +
       stroke->getThickPoint(2.0 / 3.0).thick + stroke->getThickPoint(1).thick);

  const double pixelSize = canvas.pixelSize();
  if (thickness * thickness < 4 * pixelSize * pixelSize) {
    TCenterLineStrokeStyle *appStyle =
        new TCenterLineStrokeStyle(m_color, 0x0, thickness);
    appStyle->drawStrokeOnCanvas(canvas, cf, stroke, antialias);
    delete appStyle;
    return;
  }

  assert(thickness);
  double ringHeight = thickness;
  double ringWidth  = 1.5 * ringHeight;

  // distanza fra il centro dell'anello e il punto di attacco dell'anello
  // trasversale
  double joinPos = 0.45 * ringWidth;

  // definisco la forma dell'anello della catena
  double a = .6, b = .6;
  TCanvasPath ring;
  addPolyline(ring, {TPointD(1, b), TPointD(a, 1), TPointD(-a, 1),
                     TPointD(-1, b), TPointD(-1, -b), TPointD(-a, -1),
                     TPointD(a, -1), TPointD(1, -b), TPointD(1, b)});

  // disegno la catena
  const TPixel32 color = cf ? (*cf)(m_color) : m_color;

  TPointD oldPos;
  for (UINT i = 0; i < data.size(); i += 2) {
    TPointD pos = data[i];
    TPointD u   = data[i + 1];
    TPointD v   = rotate90(u);

    // disegno un anello della catena
    canvas.save();
    TAffine aff(u.x, v.x, pos.x, u.y, v.y, pos.y);
    canvas.concat(toCanvas(aff));
    canvas.concat(TCanvasAffine::scale(ringWidth, ringHeight));
    strokeLines(canvas, ring, color, antialias);
    canvas.restore();

    // se e' il caso unisco la catena alla precedente
    if (i != 0) {
      TPointD q = pos - u * joinPos;
      TCanvasPath link;
      addSegment(link, oldPos, q);
      strokeLines(canvas, link, color, antialias);
    }
    oldPos = pos + u * joinPos;
  }
}

//=============================================================================

TSprayStrokeStyle::TSprayStrokeStyle()
    : m_color(TPixel32::Blue), m_blend(0.5), m_intensity(10.0), m_radius(0.3) {}

//-----------------------------------------------------------------------------

TColorStyle *TSprayStrokeStyle::clone() const {
  return new TSprayStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TSprayStrokeStyle::getParamCount() const { return 3; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TSprayStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TSprayStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 3);
  TTranslatableText value;
  switch (index) {
  case 0:
    value = TTranslatableText(
        "TSprayStrokeStyle",
        QT_TRANSLATE_NOOP("TSprayStrokeStyle", "Border Fade"));
    break;
  case 1:
    value = TTranslatableText(
        "TSprayStrokeStyle", QT_TRANSLATE_NOOP("TSprayStrokeStyle", "Density"));
    break;
  case 2:
    value = TTranslatableText("TSprayStrokeStyle",
                              QT_TRANSLATE_NOOP("TSprayStrokeStyle", "Size"));
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TSprayStrokeStyle::getParamRange(int index, double &min,
                                      double &max) const {
  assert(0 <= index && index < 3);
  switch (index) {
  case 0:
    min = 0.0;
    max = 1.0;
    break;
  case 1:
    min = 0.0;
    max = 100.0;
    break;
  case 2:
    min = 0.0;
    max = 1.0;
    break;
  }
}

//-----------------------------------------------------------------------------

double TSprayStrokeStyle::getParamValue(TColorStyle::double_tag,
                                        int index) const {
  assert(0 <= index && index < 3);
  double value = 0;
  switch (index) {
  case 0:
    value = m_blend;
    break;
  case 1:
    value = m_intensity;
    break;
  case 2:
    value = m_radius;
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TSprayStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 3);
  switch (index) {
  case 0:
    m_blend = value;
    break;
  case 1:
    m_intensity = value;
    break;
  case 2:
    m_radius = value;
    break;
  }

  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TSprayStrokeStyle::drawStroke(const TColorFunction *cf,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TSprayStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    const TStroke *stroke, bool antialias) const {
  //  TStroke *stroke = getStroke();
  double length = stroke->getLength();
  double step   = 4;

  double blend = m_blend;          // distanza che controlla da dove il gessetto
                                   // comincia il fade out  (0, 1)
  double intensity = m_intensity;  // quanti punti vengono disegnati ad ogni
                                   // step
  double radius = m_radius;
  double decay  = 1 - blend;
  TPointD pos1;
  TRandom rnd;
  TPixelD dcolor;
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color             = m_color;
  dcolor              = toPixelD(color);
  double s            = 0;
  double minthickness = MINTHICK * canvas.pixelSize();
  double thickness    = 0;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    // if(w<0) {s+=0.1; continue;} // per tamponare il baco della
    // getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    double normu    = norm2(u);
    if (normu == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u         = normalize(u);
    TPointD v = rotate90(u);
    TPointD shift;
    if (pos.thick < MINTHICK)
      thickness = minthickness;
    else
      thickness = pos.thick;
    for (int i = 0; i < intensity; i++) {
      double vrandnorm = (0.5 - rnd.getFloat()) * 2;
      double randomv   = vrandnorm * pos.thick;
      double randomu   = (0.5 - rnd.getFloat()) * step;
      shift            = u * randomu + v * randomv;
      pos1             = pos + shift;
      double mod       = fabs(vrandnorm);
      TCanvasColor dot;
      if (mod < decay)
        dot = floatColor(dcolor, rnd.getFloat() * dcolor.m);
      else
        dot = floatColor(dcolor, rnd.getFloat() * (1 - mod) * dcolor.m);
      // Rings, not discs: "fill" has always been off.
      drawCircle(canvas, pos1, radius * thickness * rnd.getFloat(), dot,
                 antialias);
    }
    s += step;
  }
}

//=============================================================================

TGraphicPenStrokeStyle::TGraphicPenStrokeStyle()
    : m_color(TPixel32::Black), m_intensity(10.0) {}

//-----------------------------------------------------------------------------

TColorStyle *TGraphicPenStrokeStyle::clone() const {
  return new TGraphicPenStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TGraphicPenStrokeStyle::getParamCount() const { return 1; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TGraphicPenStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TGraphicPenStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 1);

  return TTranslatableText(
      "TGraphicPenStrokeStyle",
      QT_TRANSLATE_NOOP("TGraphicPenStrokeStyle", "Density"));
}

//-----------------------------------------------------------------------------

void TGraphicPenStrokeStyle::getParamRange(int index, double &min,
                                           double &max) const {
  assert(0 <= index && index < 1);
  min = 0.0;
  max = 10.0;
}

//-----------------------------------------------------------------------------

double TGraphicPenStrokeStyle::getParamValue(TColorStyle::double_tag,
                                             int index) const {
  assert(0 <= index && index < 1);
  return m_intensity;
}

//-----------------------------------------------------------------------------

void TGraphicPenStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 1);
  m_intensity = value;
  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TGraphicPenStrokeStyle::computeData(DrawmodePointsMatrix &data,
                                         const TStroke *stroke,
                                         const TColorFunction *cf,
    double pixelSize) const {
  data.clear();
  double length = stroke->getLength();
  double step   = 10;
  TPointD pos1, pos2;
  TRandom rnd;
  double intensity = m_intensity;
  data.reserve(tceil(length / 10.0));
  double s = 0;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    // if(w<0) {s+=0.1; continue;} // per tamponare il baco della
    // getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    double normu    = norm2(u);
    if (normu == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u         = normalize(u);
    TPointD v = rotate90(u);
    TPointD shift;
    Points tmpPoints;
    tmpPoints.clear();
    unsigned int drawMode;
    for (int i = 0; i < intensity; i++) {
      if (pos.thick) {
        drawMode = DrawLines;
        tmpPoints.reserve((int)(intensity * 2 + 1));
        double randomv = (0.5 - rnd.getFloat()) * pos.thick;
        double randomu = (0.5 - rnd.getFloat()) * step;
        shift          = randomu * u + randomv * v;
        pos1           = pos + shift + v * (pos.thick);
        pos2           = pos + shift - v * (pos.thick);
        tmpPoints.push_back(pos1);
        tmpPoints.push_back(pos2);
      } else {
        drawMode = DrawPoints;
        tmpPoints.reserve((int)(intensity + 1));
        tmpPoints.push_back((TPointD)pos);
      }
    }
    if (!tmpPoints.empty()) {
      assert(drawMode == DrawPoints || drawMode == DrawLines);
      data.push_back(make_pair(drawMode, tmpPoints));
    }
    s += step;
  }
}

//-----------------------------------------------------------------------------

void TGraphicPenStrokeStyle::drawStroke(const TColorFunction *cf, DrawmodePointsMatrix &data,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, data, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TGraphicPenStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    DrawmodePointsMatrix &data, const TStroke *stroke, bool antialias) const {
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  DrawmodePointsMatrix::iterator it1 = data.begin();
  for (; it1 != data.end(); ++it1) {
    const Points &points = it1->second;
    TCanvasPath path;
    if (it1->first == DrawLines) {
      // Short lines across the stroke.
      for (std::size_t i = 0; i + 1 < points.size(); i += 2)
        addSegment(path, points[i], points[i + 1]);
      strokeLines(canvas, path, color, antialias);
    } else {
      assert(it1->first == DrawPoints);
      // Where the stroke has no thickness: points of one pixel.
      for (const TPointD &point : points) path.moveTo(toCanvas(point));
      TCanvasPen pen = thinPen();
      pen.cap        = TCanvasPen::Cap::Round;
      canvas.strokePath(path, pen, linePaint(toCanvas(color), false));
    }
  }
}

//=============================================================================

namespace {
double inline get_line_slope(double meter, double inmax, double linemax,
                             double outmax) {
  if (meter <= inmax)
    return meter / inmax;
  else if (meter <= inmax + linemax)
    return 1;
  else if (meter <= inmax + linemax + outmax)
    return (inmax + linemax - meter) / outmax + 1;
  else
    return 0;
}
}

//-----------------------------------------------------------------------------

TDottedLineStrokeStyle::TDottedLineStrokeStyle()
    : m_color(TPixel32::Black)
    , m_in(10.0)
    , m_line(50.0)
    , m_out(10.0)
    , m_blank(10.0) {}

//-----------------------------------------------------------------------------

TColorStyle *TDottedLineStrokeStyle::clone() const {
  return new TDottedLineStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TDottedLineStrokeStyle::getParamCount() const { return 4; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TDottedLineStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TDottedLineStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 4);
  TTranslatableText value;
  switch (index) {
  case 0:
    value = TTranslatableText(
        "TDottedLineStrokeStyle",
        QT_TRANSLATE_NOOP("TDottedLineStrokeStyle", "Fade In"));
    break;
  case 1:
    value =
        TTranslatableText("TDottedLineStrokeStyle",
                          QT_TRANSLATE_NOOP("TDottedLineStrokeStyle", "Dash"));
    break;
  case 2:
    value = TTranslatableText(
        "TDottedLineStrokeStyle",
        QT_TRANSLATE_NOOP("TDottedLineStrokeStyle", "Fade Out"));
    break;
  case 3:
    value =
        TTranslatableText("TDottedLineStrokeStyle",
                          QT_TRANSLATE_NOOP("TDottedLineStrokeStyle", "Gap"));
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TDottedLineStrokeStyle::getParamRange(int index, double &min,
                                           double &max) const {
  assert(0 <= index && index < 4);
  switch (index) {
  case 0:
    min = 1.0;
    max = 100.0;
    break;
  case 1:
    min = 1.0;
    max = 100.0;
    break;
  case 2:
    min = 1.;
    max = 100.0;
    break;
  case 3:
    min = 0.0;
    max = 100.0;
    break;
  }
}

//-----------------------------------------------------------------------------

double TDottedLineStrokeStyle::getParamValue(TColorStyle::double_tag,
                                             int index) const {
  assert(0 <= index && index < 4);
  double value = 0;
  switch (index) {
  case 0:
    value = m_in;
    break;
  case 1:
    value = m_line;
    break;
  case 2:
    value = m_out;
    break;
  case 3:
    value = m_blank;
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TDottedLineStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 4);
  switch (index) {
  case 0:
    m_in = value;
    break;
  case 1:
    m_line = value;
    break;
  case 2:
    m_out = value;
    break;
  case 3:
    m_blank = value;
    break;
  }
  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TDottedLineStrokeStyle::computeData(Points &positions,
                                         const TStroke *stroke,
                                         const TColorFunction *cf,
    double pixelSize) const {
  double length = stroke->getLength();

  double step     = 5.0;
  double linemax  = m_line;
  double inmax    = (m_in / 100);
  double outmax   = (m_out / 100);
  double blankmax = m_blank;
  double total    = 0;
  TRandom rnd;

  positions.clear();
  positions.reserve(tceil(length / step) + 1);

  TPointD oldPos1, oldPos2, oldPos3, oldPos4, pos1, pos2, pos3, pos4;
  // bool firstRing = true;
  double s            = 0;
  double meter        = 0;
  double center       = 0;
  double slopetmp     = 0;
  double minthickness = MINTHICK * pixelSize;
  double thickness    = 0;
  double line = 0, in = 0, out = 0, blank = 0;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    if (w < 0) {
      s += 0.1;
      continue;
    }  // per tamponare il baco della getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);

    if (pos.thick < MINTHICK)
      thickness = minthickness;
    else
      thickness = pos.thick;
    if (meter >= total) {
      meter = 0;

      line                        = linemax * (1 + rnd.getFloat()) * thickness;
      if (line > length - s) line = length - s;
      in                          = inmax * line;
      out                         = outmax * line;
      line                        = line - in - out;
      blank                       = blankmax * (1 + rnd.getFloat()) * thickness;
      if (in + out > length) {
        in   = rnd.getFloat() * (length / 2);
        out  = length - in;
        line = 0;
      }
      total = in + line + out + blank;
    } else if (meter > in + line + out + step) {
      s += step;
      meter += step;
      continue;
    }
    TPointD u = stroke->getSpeed(w);
    if (norm2(u) == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u            = normalize(u);
    double slope = 0;
    slope        = get_line_slope(meter, in, line, out);
    slopetmp     = slope;
    TPointD v    = rotate90(u) * (thickness)*slope;
    if (pos.thick * slope < 1)
      center = 0.0;
    else
      center = 0.5;
    pos1     = pos + v;
    pos2     = pos + v * 0.5;
    pos3     = pos - v * 0.5;
    pos4     = pos - v;

    positions.push_back(pos1);
    positions.push_back(pos2);
    positions.push_back(pos3);
    positions.push_back(pos4);

    s += step;
    meter += step;
  }
}

//-----------------------------------------------------------------------------

void TDottedLineStrokeStyle::drawStroke(const TColorFunction *cf, Points &positions,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, positions, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TDottedLineStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    Points &positions, const TStroke *stroke, bool antialias) const {
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  // Each piece is three bands side by side: the colour in the middle one,
  // fading to nothing across the outer two.
  const TPixel32 faded(color.r, color.g, color.b, 0);
  for (UINT i = 4; i + 3 < positions.size(); i += 4) {
    TCanvasMesh strip = plainMesh(TCanvasMesh::Mode::TriangleStrip);
    addVertex(strip, positions[i - 4], faded);
    addVertex(strip, positions[i], faded);
    addVertex(strip, positions[i - 3], color);
    addVertex(strip, positions[i + 1], color);
    addVertex(strip, positions[i - 2], color);
    addVertex(strip, positions[i + 2], color);
    addVertex(strip, positions[i - 1], faded);
    addVertex(strip, positions[i + 3], faded);
    canvas.drawMesh(strip, plainPaint());
  }
}

//=============================================================================

TRopeStrokeStyle::TRopeStrokeStyle()
    : m_color(TPixel32(255, 135, 0)), m_bend(0.4) {}

//-----------------------------------------------------------------------------

TColorStyle *TRopeStrokeStyle::clone() const {
  return new TRopeStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TRopeStrokeStyle::getParamCount() const { return 1; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TRopeStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TRopeStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 1);
  return TTranslatableText("TRopeStrokeStyle",
                           QT_TRANSLATE_NOOP("TRopeStrokeStyle", "Tilt"));
}

//-----------------------------------------------------------------------------

void TRopeStrokeStyle::getParamRange(int index, double &min,
                                     double &max) const {
  assert(0 <= index && index < 1);
  min = -1.0;
  max = 1.0;
}

//-----------------------------------------------------------------------------

double TRopeStrokeStyle::getParamValue(TColorStyle::double_tag,
                                       int index) const {
  assert(0 <= index && index < 1);
  return m_bend;
}

//-----------------------------------------------------------------------------

void TRopeStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 1);
  m_bend = value;
  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TRopeStrokeStyle::computeData(Points &positions, const TStroke *stroke,
                                   const TColorFunction *cf,
    double pixelSize) const {
  double length = stroke->getLength();
  // spessore della catena = spessore "medio" dello stroke
  double step = 10.0;
  double bend;
  double bump;
  double bump_max = step / 4;

  positions.clear();
  positions.reserve(tceil(length / step) + 1);

  TPointD oldPos1, oldPos2;
  bool firstRing = true;
  double s       = 0;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    if (w < 0) {
      s += 0.1;
      continue;
    }  // per tamponare il baco della getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    if (norm2(u) == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u                          = normalize(u);
    bend                       = pos.thick * m_bend;
    bump                       = pos.thick * 0.3;
    if (bump >= bump_max) bump = bump_max;
    TPointD v                  = rotate90(u) * pos.thick;
    TPointD v1                 = v * 0.2;
    if (firstRing) {
      firstRing = false;
    } else {
      positions.push_back(pos + (bend + bump) * u + v - v1);
      positions.push_back(pos + (bend)*u + v);
      positions.push_back(oldPos1 + (bump)*u + v1);
      positions.push_back(oldPos1);
      positions.push_back(oldPos2);
      positions.push_back(oldPos2 + bump * u - v1);
      positions.push_back(pos + u * (-bend) - v);
      positions.push_back(pos + u * (bump - bend) - v + v1);
    }
    oldPos1 = pos + (bend + bump) * u + v - v1;
    oldPos2 = pos + u * (bump - bend) - v + v1;
    s += step;
  }
  positions.push_back(oldPos1);
  positions.push_back(oldPos2);
}

//-----------------------------------------------------------------------------

void TRopeStrokeStyle::drawStroke(const TColorFunction *cf, Points &positions,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, positions, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TRopeStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    Points &positions, const TStroke *stroke, bool antialias) const {
  if (positions.size() <= 1) return;
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  TPixel32 blackcolor(TPixel32::Black);
  if (cf) blackcolor = (*(cf))(blackcolor);

  // Each strand is a polygon of eight points with a black line round it.
  UINT i = 0;
  for (; i < positions.size() - 2; i += 8) {
    TCanvasMesh strand = plainMesh(TCanvasMesh::Mode::Polygon);
    TCanvasPath outline;
    for (UINT j = 0; j < 8; j++) addVertex(strand, positions[i + j], color);
    addPolyline(outline,
                std::vector<TPointD>(positions.begin() + i,
                                     positions.begin() + i + 8));
    canvas.drawMesh(strand, plainPaint());
    strokeLines(canvas, outline, blackcolor, antialias);
  }

  TCanvasPath last;
  addSegment(last, positions[i], positions[i + 1]);
  strokeLines(canvas, last, blackcolor, antialias);
}

//=============================================================================

TCrystallizeStrokeStyle::TCrystallizeStrokeStyle()
    : m_color(TPixel32(255, 150, 150, 255)), m_period(10.0), m_opacity(0.5) {}

//-----------------------------------------------------------------------------

TColorStyle *TCrystallizeStrokeStyle::clone() const {
  return new TCrystallizeStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TCrystallizeStrokeStyle::getParamCount() const { return 2; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TCrystallizeStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TCrystallizeStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 2);
  return index == 0
             ? TTranslatableText(
                   "TCrystallizeStrokeStyle",
                   QT_TRANSLATE_NOOP("TCrystallizeStrokeStyle", "Crease"))
             : TTranslatableText(
                   "TCrystallizeStrokeStyle",
                   QT_TRANSLATE_NOOP("TCrystallizeStrokeStyle", "Opacity"));
}

//-----------------------------------------------------------------------------

void TCrystallizeStrokeStyle::getParamRange(int index, double &min,
                                            double &max) const {
  assert(0 <= index && index < 2);
  if (index == 0) {
    min = 1.0;
    max = 100.0;
  } else {
    min = 0.0;
    max = 1.0;
  }
}

//-----------------------------------------------------------------------------

double TCrystallizeStrokeStyle::getParamValue(TColorStyle::double_tag,
                                              int index) const {
  assert(0 <= index && index < 2);
  return index == 0 ? m_period : m_opacity;
}

//-----------------------------------------------------------------------------

void TCrystallizeStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 2);
  if (index == 0)
    m_period = value;
  else
    m_opacity = value;

  // updateVersionNumber(); non serve perche' i parametri vengono sfrutttati
  // direttamente nella draw
}

//-----------------------------------------------------------------------------

void TCrystallizeStrokeStyle::computeData(Points &positions,
                                          const TStroke *stroke,
                                          const TColorFunction *cf,
    double pixelSize) const {
  double length = stroke->getLength();
  double step   = 10.0;
  TRandom rnd;
  positions.clear();
  positions.reserve(tceil((length + 1) / step));
  double s = 0;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    if (w < 0) {
      s += 0.1;
      continue;
    }  // per tamponare il baco della getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    if (norm2(u) == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u         = normalize(u);
    TPointD v = rotate90(u) * (pos.thick / 2);
    // One random draw per statement: the order in which the operands of an
    // expression are evaluated is unspecified, and GCC and Clang disagreed
    // here, so the same drawing rendered differently in their builds.
    const double across1 = 1 + rnd.getFloat();
    const double along1  = 2 * rnd.getFloat();
    positions.push_back(pos + v * across1 + u * along1);
    const double across2 = 1 + rnd.getFloat();
    const double along2  = 2 * rnd.getFloat();
    positions.push_back(pos - v * across2 - u * along2);
    s += step;
  }
}

//-----------------------------------------------------------------------------

void TCrystallizeStrokeStyle::drawStroke(const TColorFunction *cf, Points &positions,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, positions, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TCrystallizeStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    Points &positions, const TStroke *stroke, bool antialias) const {
  double step    = 10.0;
  double period  = (101 - m_period) * step;
  double counter = 0;
  double opacity = m_opacity;
  TRandom rnd;

  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  TPixelD dcolor = toPixelD(color);

  // A band whose opacity changes along the stroke, a little at random...
  TCanvasMesh band = plainMesh(TCanvasMesh::Mode::TriangleStrip);
  for (int i = 0; i < (int)positions.size() / 2; i++) {
    if (counter > period) counter = 0;
    const TCanvasColor shade = floatColor(
        dcolor, (opacity + (counter / period) * rnd.getFloat()) * dcolor.m);
    addVertex(band, positions[i * 2], shade);
    addVertex(band, positions[i * 2 + 1], shade);
    counter += step;
  }
  canvas.drawMesh(band, plainPaint());

  // ...and a line in the full colour down each side.
  TCanvasPath sides;
  for (int j = 1; j < (int)positions.size() / 2; j++) {
    addSegment(sides, positions[(j - 1) * 2], positions[j * 2]);
    addSegment(sides, positions[(j - 1) * 2 + 1], positions[j * 2 + 1]);
  }
  strokeLines(canvas, sides, floatColor(dcolor, dcolor.m), antialias);
}

//=============================================================================

namespace {

class Stripe {
public:
  TPointD oldpos1;
  TPointD oldpos2;
  TPointD pos1;
  TPointD pos2;
  int phase;
  TPixel32 color;
  Stripe();
  void drawpolygon(TCanvas &canvas);
  void drawlines(TCanvas &canvas, TPixel32 blackcolor, bool antialias);
  void addToSegment(vector<TSegment> *sv, vector<TSegment> &scontour,
                    TPixel32 *colors);
};

Stripe::Stripe() {
  oldpos1 = TPointD(0, 0);
  oldpos2 = TPointD(0, 0);
  pos1    = TPointD(0, 0);
  pos2    = TPointD(0, 0);
  color   = TPixel32(0, 0, 0);
  phase   = 0;
}

void Stripe::drawpolygon(TCanvas &canvas) {
  TCanvasMesh polygon = plainMesh(TCanvasMesh::Mode::Polygon);
  addVertex(polygon, oldpos1, color);
  addVertex(polygon, pos1, color);
  addVertex(polygon, pos2, color);
  addVertex(polygon, oldpos2, color);
  canvas.drawMesh(polygon, plainPaint());
}

void Stripe::addToSegment(vector<TSegment> *sv, vector<TSegment> &scontour,
                          TPixel32 *colors) {
  TPointD p0 = (oldpos1 + oldpos2) * 0.5;
  TPointD p1 = (pos1 + pos2) * 0.5;
  //	TPointD p0=oldpos1;
  //	TPointD p1=pos1;
  if (color == colors[0]) sv[0].push_back(TSegment(p0, p1));
  if (color == colors[1]) sv[1].push_back(TSegment(p0, p1));
  if (color == colors[2]) sv[2].push_back(TSegment(p0, p1));

  scontour.push_back(TSegment(oldpos1, pos1));
  scontour.push_back(TSegment(oldpos2, pos2));
}

void Stripe::drawlines(TCanvas &canvas, TPixel32 blackcolor, bool antialias) {
  TCanvasPath sides;
  addSegment(sides, oldpos1, pos1);
  addSegment(sides, pos2, oldpos2);
  strokeLines(canvas, sides, blackcolor, antialias);
}
}

//-----------------------------------------------------------------------------

TBraidStrokeStyle::TBraidStrokeStyle()
    : m_period(80.0)

{
  m_colors[0] = TPixel32::Red;
  m_colors[1] = TPixel32::Green;
  m_colors[2] = TPixel32::Blue;
}

//-----------------------------------------------------------------------------

TColorStyle *TBraidStrokeStyle::clone() const {
  return new TBraidStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TBraidStrokeStyle::getParamCount() const { return 1; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TBraidStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TBraidStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 1);

  return TTranslatableText("TBraidStrokeStyle",
                           QT_TRANSLATE_NOOP("TBraidStrokeStyle", "Twirl"));
}

//-----------------------------------------------------------------------------

void TBraidStrokeStyle::getParamRange(int index, double &min,
                                      double &max) const {
  assert(0 <= index && index < 1);
  min = 1.0;
  max = 100.0;
}

//-----------------------------------------------------------------------------

double TBraidStrokeStyle::getParamValue(TColorStyle::double_tag,
                                        int index) const {
  assert(0 <= index && index < 1);
  return m_period;
}

//-----------------------------------------------------------------------------

void TBraidStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 1);
  m_period = value;

  updateVersionNumber();
}

//-----------------------------------------------------------------------------

TPixel32 TBraidStrokeStyle::getColorParamValue(int index) const {
  TPixel32 tmp;
  switch (index) {
  case (0):
    tmp = m_colors[0];
    break;
  case (1):
    tmp = m_colors[1];
    break;
  case (2):
    tmp = m_colors[2];
    break;
  }
  return tmp;
}

//-----------------------------------------------------------------------------

void TBraidStrokeStyle::setColorParamValue(int index, const TPixel32 &color) {
  switch (index) {
  case (0):
    m_colors[0] = color;
    break;
  case (1):
    m_colors[1] = color;
    break;
  case (2):
    m_colors[2] = color;
    break;
  }
}

//-----------------------------------------------------------------------------

void TBraidStrokeStyle::loadData(int ids, TInputStreamInterface &is) {
  if (ids != 112)
    throw TException("Braid stroke style: unknown obsolete format");

  is >> m_colors[0] >> m_period;

  m_period /= 10.0;
  m_colors[0] = TPixel32::Red;
  m_colors[1] = TPixel32::Green;
  m_colors[2] = TPixel32::Blue;
}

//-----------------------------------------------------------------------------

void TBraidStrokeStyle::drawStroke(const TColorFunction *cf,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TBraidStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    const TStroke *stroke, bool antialias) const {
  double length                = stroke->getLength();
  const int ntick              = 81;
  const double stripethickness = 0.3;
  int period                   = (int)(101 - m_period) * 20;
  double step                  = period / (double)ntick;
  double freq                  = M_2PI / ntick;
  int swapcount                = 0;
  int count                    = 0;
  bool firstRing               = true;
  double s                     = 0;
  double swap;
  vector<Stripe> braid;
  vector<double> ssin;
  int k = 0;
  TPixel32 colors[3];
  for (k = 0; k < 3; k++) {
    if (cf)
      colors[k] = (*(cf))(m_colors[k]);
    else
      colors[k] = m_colors[k];
  }
  TPixel32 blackcolor = TPixel32::Black;
  if (cf) blackcolor  = (*(cf))(blackcolor);

  for (k = 0; k < 3; k++) {
    Stripe tmp;
    tmp.phase = (ntick * k) / 3;
    tmp.color = colors[k];
    braid.push_back(tmp);
  }

  for (int z = 0; z < ntick; z++) {
    double tmpsin = sin(z * freq);
    ssin.push_back(tmpsin);
  }
  while (s <= length) {
    count++;
    double w = stroke->getParameterAtLength(s);
    if (w < 0) {
      s += 0.1;
      continue;
    }  // per tamponare il baco della getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    if (norm2(u) == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u          = normalize(u);
    TPointD v  = rotate90(u) * pos.thick;
    TPointD v1 = v * stripethickness;
    v          = v * 0.5;
    // int modper=(int)s%(int)period;
    if (firstRing) {
      firstRing = false;
      swap      = 0;
      for (int j = 0; j < (int)braid.size(); j++) {
        int tmp          = (count + braid[j].phase) % ntick;
        braid[j].oldpos1 = pos + v * ssin[tmp];
        braid[j].oldpos2 = pos + v * ssin[tmp] + v1;
      }
    } else {
      for (int i = 0; i < (int)braid.size(); i++) {
        int tmp       = (count + braid[i].phase) % ntick;
        braid[i].pos1 = pos + v * ssin[tmp];
        braid[i].pos2 = pos + v * ssin[tmp] + v1;
        braid[i].drawpolygon(canvas);
        braid[i].drawlines(canvas, blackcolor, antialias);
        braid[i].oldpos1 = pos + v * ssin[tmp];
        braid[i].oldpos2 = pos + v * ssin[tmp] + v1;
      }
    }
    s += step;
    swap += step;
    if (swap > (period / 3.0)) {
      swapcount++;
      std::swap(braid[0], braid[1 + (swapcount & 1)]);
      swap -= period / 3.0;
    }
  }
}

//=============================================================================

TSketchStrokeStyle::TSketchStrokeStyle()
    : m_color(TPixel32(100, 100, 150, 127)), m_density(0.4) {}

//-----------------------------------------------------------------------------

TColorStyle *TSketchStrokeStyle::clone() const {
  return new TSketchStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TSketchStrokeStyle::getParamCount() const { return 1; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TSketchStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TSketchStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 1);

  return TTranslatableText("TSketchStrokeStyle",
                           QT_TRANSLATE_NOOP("TSketchStrokeStyle", "Density"));
}

//-----------------------------------------------------------------------------

void TSketchStrokeStyle::getParamRange(int index, double &min,
                                       double &max) const {
  assert(0 <= index && index < 1);
  min = 0.0;
  max = 1.0;
}

//-----------------------------------------------------------------------------

double TSketchStrokeStyle::getParamValue(TColorStyle::double_tag,
                                         int index) const {
  assert(0 <= index && index < 1);
  return m_density;
}

//-----------------------------------------------------------------------------

void TSketchStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 1);
  m_density = value;

  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TSketchStrokeStyle::drawStroke(const TColorFunction *cf,
    const TStroke *stroke) const {
  // Sets OpenGL state of its own: drawn on the OpenGL side (colorfx_gl.cpp).
  TGLEntries::draw<TGLEntries::StrokeDrawer>("TSketchStrokeStyle::drawStroke",
                                             *this, cf, stroke);
}

//-----------------------------------------------------------------------------

void TSketchStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    const TStroke *stroke, bool antialias) const {
  double length = stroke->getLength();
  if (length <= 0) return;

  int count = (int)(length * m_density);

  double maxDw = std::min(1.0, 20.0 / length);
  double minDw = 1.0 / length;

  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;


  TRandom rnd;

  for (int i = 0; i < count; i++) {
    double r    = rnd.getFloat();
    double dw   = (1 - r) * minDw + r * maxDw;
    double wmin = dw, wmax = 1 - dw;
    if (wmin >= wmax) continue;
    r        = rnd.getFloat();
    double w = (1 - r) * wmin + r * wmax;

    double w0 = w - dw;
    double w1 = w + dw;

    TThickPoint p0 = stroke->getThickPoint(w0);
    TThickPoint p1 = stroke->getThickPoint(w1);
    double d01     = tdistance(p0, p1);
    if (d01 == 0) continue;

    int count = (int)(d01);

    TPointD v0 = stroke->getSpeed(w0);
    TPointD v1 = stroke->getSpeed(w1);

    if (norm2(v0) == 0 || norm2(v1) == 0)
      continue;  // non dovrebbe succedere mai, ma....
    v0 = rotate90(normalize(v0));
    v1 = rotate90(normalize(v1));

    double delta  = 0.5 * (rnd.getFloat() - 0.5) * (p0.thick + p1.thick);
    double d      = 0.1 * d01;
    double delta0 = delta - d;
    double delta1 = delta + d;

    std::vector<TPointD> line;
    line.push_back(p0 + v0 * delta0);
    for (int j = 1; j < count; j++) {
      double t  = j / (double)count;
      w         = (1 - t) * w0 + t * w1;
      TPointD v = rotate90(normalize(stroke->getSpeed(w)));
      assert(0 <= w && w <= 1);
      TPointD p      = stroke->getPoint(w);
      double delta_t = (1 - t) * delta0 + t * delta1;
      line.push_back(p + v * delta_t);
    }
    line.push_back(p1 + v1 * delta1);
    TCanvasPath path;
    addPolyline(path, line);
    strokeLines(canvas, path, color, antialias);
  }
}

//=============================================================================

TBubbleStrokeStyle::TBubbleStrokeStyle()
    : m_color0(TPixel32::Red), m_color1(TPixel32::Green) {}

//-----------------------------------------------------------------------------

TColorStyle *TBubbleStrokeStyle::clone() const {
  return new TBubbleStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

void TBubbleStrokeStyle::loadData(int ids, TInputStreamInterface &is) {
  if (ids != 114)
    throw TException("Bubble  stroke style: unknown obsolete format");

  m_color0 = TPixel32::Red;
  m_color1 = TPixel32::Green;
}

//-----------------------------------------------------------------------------

void TBubbleStrokeStyle::drawStroke(const TColorFunction *cf,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TBubbleStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    const TStroke *stroke, bool antialias) const {
  double length = stroke->getLength();
  if (length <= 0) return;

  TRandom rnd(0);

  TPixel32 color0, color1;
  if (cf) {
    color0 = (*(cf))(m_color0);
    color1 = (*(cf))(m_color1);
  } else {
    color0 = m_color0;
    color1 = m_color1;
  }

  double minthickness = MINTHICK * canvas.pixelSize();
  double thickness    = 0;
  for (double s = 0; s < length; s += 5) {
    TPointD p = stroke->getPointAtLength(s);
    double w  = stroke->getParameterAtLength(s);
    if (w < 0) {
      s += 0.1;
      continue;
    }  // per tamponare il baco della getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    int toff        = rnd.getInt(0, 999);
    int t           = (m_currentFrame + toff) % 1000;
    TRandom rnd2(t >> 2);
    // x first, then y, in separate statements (as in
    // TCrystallizeStrokeStyle::computeData above: the
    // evaluation order of function arguments is unspecified).
    const double dx = -0.5 + rnd2.getFloat();
    const double dy = -0.5 + rnd2.getFloat();
    p += 2 * TPointD(dx, dy);
    if (pos.thick < MINTHICK)
      thickness = minthickness;
    else
      thickness = pos.thick;
    const TPixel32 color = blend(color0, color1, rnd.getFloat());
    double radius        = (t & ((int)(thickness)));
    drawCircle(canvas, p, radius, toCanvas(color), antialias);
  }
}

//=============================================================================

TTissueStrokeStyle::TTissueStrokeStyle()
    : m_color(TPixel32::Black), m_density(3.0), m_border(1.0) {}

//-----------------------------------------------------------------------------

TColorStyle *TTissueStrokeStyle::clone() const {
  return new TTissueStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TTissueStrokeStyle::getParamCount() const { return 2; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TTissueStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TTissueStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 2);
  return index == 0 ? TTranslatableText(
                          "TTissueStrokeStyle",
                          QT_TRANSLATE_NOOP("TTissueStrokeStyle", "Density"))
                    : TTranslatableText("TTissueStrokeStyle",
                                        QT_TRANSLATE_NOOP("TTissueStrokeStyle",
                                                          "Border Size"));
}

//-----------------------------------------------------------------------------

void TTissueStrokeStyle::getParamRange(int index, double &min,
                                       double &max) const {
  assert(0 <= index && index < 2);
  if (index == 0) {
    min = 2.0;
    max = 10.0;
  } else {
    min = 0.0;
    max = 1.0;
  }
}

//-----------------------------------------------------------------------------

double TTissueStrokeStyle::getParamValue(TColorStyle::double_tag,
                                         int index) const {
  assert(0 <= index && index < 2);
  return index == 0 ? m_density : m_border;
}

//-----------------------------------------------------------------------------

void TTissueStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 2);
  if (index == 0)
    m_density = value;
  else
    m_border = value;

  updateVersionNumber();
}
//-----------------------------------------------------------------------------

void TTissueStrokeStyle::computeData(PointMatrix &data, const TStroke *stroke,
                                     const TColorFunction *cf,
    double pixelSize) const {
  data.clear();
  double length = stroke->getLength();
  double step   = 5.0;
  double border = m_border;
  TPointD pos1, oldPos1;
  TRandom rnd;
  double increment = 0.0;
  int intensity    = (int)m_density + 2;
  vector<TPointD> points;
  vector<TPointD> oldpoints;
  double s       = 0;
  bool firstRing = true;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    if (w < 0) {
      s += 0.1;
      continue;
    }  // per tamponare il baco della getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    if (norm2(u) == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u         = normalize(u);
    TPointD v = rotate90(u);
    increment = (2 * pos.thick) / (intensity - 1);
    for (int i = 1; i < intensity - 1; i++) {
      pos1 = pos + v * (-pos.thick + i * increment);
      points.push_back(pos1);
    }
    if (firstRing) {
      firstRing = false;
    } else {
      Points tmpPoints1;
      tmpPoints1.clear();
      tmpPoints1.reserve(intensity);

      for (int i = 1; i < intensity - 1; i++) {
        pos1    = points[i - 1];
        oldPos1 = oldpoints[i - 1];
        tmpPoints1.push_back(oldPos1);
        tmpPoints1.push_back(pos1);
      }
      data.push_back(tmpPoints1);
      if (increment > 1) {
        int count = tceil(step / increment + 1);
        Points tmpPoints2;
        tmpPoints2.clear();
        tmpPoints2.reserve(count);
        double startpoint = -step - increment / 2.0;
        for (int j = 1; j < step / increment + 1; j++) {
          tmpPoints2.push_back(points[0] -
                               v * border * increment * rnd.getFloat() +
                               u * (startpoint + j * (increment)));
          tmpPoints2.push_back(points[intensity - 3] +
                               v * border * increment * rnd.getFloat() +
                               u * (startpoint + j * (increment)));
        }
        data.push_back(tmpPoints2);
      }
    }
    oldpoints = points;
    points.clear();
    s += step;
  }
}

//-----------------------------------------------------------------------------

void TTissueStrokeStyle::drawStroke(const TColorFunction *cf, PointMatrix &data,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, data, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TTissueStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    PointMatrix &data, const TStroke *stroke, bool antialias) const {
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  // Threads: each row of the data is a set of separate segments.
  PointMatrix::iterator it1 = data.begin();
  for (; it1 != data.end(); ++it1) {
    const Points &points = *it1;
    TCanvasPath threads;
    for (std::size_t i = 0; i + 1 < points.size(); i += 2)
      addSegment(threads, points[i], points[i + 1]);
    strokeLines(canvas, threads, color, antialias);
  }
}

//=============================================================================

TBiColorStrokeStyle::TBiColorStrokeStyle()
    : m_color0(TPixel32::Red), m_color1(TPixel32::Black) {}

//-----------------------------------------------------------------------------

TColorStyle *TBiColorStrokeStyle::clone() const {
  return new TBiColorStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

void TBiColorStrokeStyle::loadData(TInputStreamInterface &is) {
  is >> m_color0 >> m_color1 >> m_parameter;
}

//-----------------------------------------------------------------------------

void TBiColorStrokeStyle::saveData(TOutputStreamInterface &os) const {
  os << m_color0 << m_color1 << m_parameter;
}

//-----------------------------------------------------------------------------

void TBiColorStrokeStyle::drawRegion(const TColorFunction *cf,
                                     const bool antiAliasing,
                                     TRegionOutline &boundary) const {}

//-----------------------------------------------------------------------------

void TBiColorStrokeStyle::loadData(int ids, TInputStreamInterface &is) {
  if (ids != 115 && ids != 119)
    throw TException("Bicolor stroke style: unknown obsolete format");

  is >> m_color0 >> m_parameter;

  m_color1 = TPixel32::Black;
}

//-----------------------------------------------------------------------------

void TBiColorStrokeStyle::drawStroke(const TColorFunction *cf,
    TStrokeOutline *outline, const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, outline, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TBiColorStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    TStrokeOutline *outline, const TStroke *stroke, bool antialias) const {
  TPixel32 color0 = m_color0;
  TPixel32 color1 = m_color1;
  if (cf) {
    color0 = (*(cf))(m_color0);
    color1 = (*(cf))(m_color1);
  }

  UINT i;

  const std::vector<TOutlinePoint> &v = outline->getArray();
  if (v.empty()) return;

  // A line down each side in the colour of that side, then the stroke
  // itself, blending from the one colour to the other across its width.
  std::vector<TPointD> side;
  for (i = 0; i < v.size(); i += 2) side.push_back(TPointD(v[i].x, v[i].y));
  TCanvasPath left;
  addPolyline(left, side);
  strokeLines(canvas, left, color0, antialias);

  side.clear();
  for (i = 1; i < v.size(); i += 2) side.push_back(TPointD(v[i].x, v[i].y));
  TCanvasPath right;
  addPolyline(right, side);
  strokeLines(canvas, right, color1, antialias);

  TCanvasMesh strip = plainMesh(TCanvasMesh::Mode::TriangleStrip);
  for (i = 0; i + 1 < v.size(); i += 2) {
    addVertex(strip, TPointD(v[i].x, v[i].y), color0);
    addVertex(strip, TPointD(v[i + 1].x, v[i + 1].y), color1);
  }
  canvas.drawMesh(strip, plainPaint());
}

//=============================================================================

TNormal2StrokeStyle::TNormal2StrokeStyle()
    : m_color(TPixel32::Yellow)
    , m_lightx(45.0)
    , m_lighty(200.0)
    , m_shininess(50.0)
    , m_metal(0.5)
    , m_bend(1.0) {}

//-----------------------------------------------------------------------------

TColorStyle *TNormal2StrokeStyle::clone() const {
  return new TNormal2StrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TNormal2StrokeStyle::getParamCount() const { return 5; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TNormal2StrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TNormal2StrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 5);
  TTranslatableText value;
  switch (index) {
  case 0:
    value = TTranslatableText(
        "TNormal2StrokeStyle",
        QT_TRANSLATE_NOOP("TNormal2StrokeStyle", "Light X Pos"));
    break;
  case 1:
    value = TTranslatableText(
        "TNormal2StrokeStyle",
        QT_TRANSLATE_NOOP("TNormal2StrokeStyle", "Light Y Pos"));
    break;
  case 2:
    value = TTranslatableText(
        "TNormal2StrokeStyle",
        QT_TRANSLATE_NOOP("TNormal2StrokeStyle", "Shininess"));
    break;
  case 3:
    value =
        TTranslatableText("TNormal2StrokeStyle",
                          QT_TRANSLATE_NOOP("TNormal2StrokeStyle", "Plastic"));
    break;
  case 4:
    value = TTranslatableText("TNormal2StrokeStyle",
                              QT_TRANSLATE_NOOP("TNormal2StrokeStyle", "Bump"));
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TNormal2StrokeStyle::getParamRange(int index, double &min,
                                        double &max) const {
  assert(0 <= index && index < 5);
  switch (index) {
  case 0:
    min = -100.0;
    max = 100.0;
    break;
  case 1:
    min = -100.0;
    max = 100.0;
    break;
  case 2:
    min = 0.1;
    max = 128.0;
    break;
  case 3:
    min = 0.0;
    max = 1.0;
    break;
  case 4:
    min = 0.0;
    max = 1.0;
    break;
  }
}

//-----------------------------------------------------------------------------

double TNormal2StrokeStyle::getParamValue(TColorStyle::double_tag,
                                          int index) const {
  assert(0 <= index && index < 5);
  double value;
  switch (index) {
  case 0:
    value = m_lightx;
    break;
  case 1:
    value = m_lighty;
    break;
  case 2:
    value = m_shininess;
    break;
  case 3:
    value = m_metal;
    break;
  case 4:
    value = m_bend;
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TNormal2StrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 5);
  switch (index) {
  case 0:
    m_lightx = value;
    break;
  case 1:
    m_lighty = value;
    break;
  case 2:
    m_shininess = value;
    break;
  case 3:
    m_metal = value;
    break;
  case 4:
    m_bend = value;
    break;
  }

  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TNormal2StrokeStyle::loadData(int ids, TInputStreamInterface &is) {
  if (ids != 121)
    throw TException("Normal  stroke style: unknown obsolete format");

  is >> m_color >> m_lightx >> m_lighty >> m_shininess >> m_metal;
  m_bend = 1.0;
}

//-----------------------------------------------------------------------------

void TNormal2StrokeStyle::drawRegion(const TColorFunction *cf,
                                     const bool antiAliasing,
                                     TRegionOutline &boundary) const {}

//-----------------------------------------------------------------------------

void TNormal2StrokeStyle::drawStroke(const TColorFunction *cf,
                                     TStrokeOutline *outline,
                                     const TStroke *stroke) const {
  // Lit by OpenGL: drawn on the OpenGL side (colorfx_gl.cpp).
  TGLEntries::draw<TGLEntries::OutlineStrokeDrawer>(
      "TNormal2StrokeStyle::drawStroke", *this, cf, outline, stroke);
}
//-----------------------------------------------------------------------------

namespace {

// What OpenGL's lighting makes of TNormal2StrokeStyle's material at a
// vertex: one light at infinity, a viewer at infinity, the scene's default
// ambient light of 0.2, colours clamped to 1. Normals and the light's
// direction are taken through the transform in force, as OpenGL takes them
// through the modelview matrix.
class Normal2Light {
  double m_inv[4];  // of the transform's 2 by 2 part
  T3DPointD m_light, m_half;
  TPixelD m_color;
  double m_specular[3], m_shininess;

public:
  Normal2Light(const TCanvasAffine &aff, double lightx, double lighty,
               const TPixelD &color, double metal, double shininess)
      : m_color(color), m_shininess(std::min(std::max(shininess, 0.0), 128.0)) {
    double det = aff.a11 * aff.a22 - aff.a12 * aff.a21;
    if (det == 0.0) det = 1.0;
    m_inv[0] = aff.a22 / det, m_inv[1] = -aff.a12 / det;
    m_inv[2] = -aff.a21 / det, m_inv[3] = aff.a11 / det;

    // The light's values go to OpenGL as floats.
    const double lx = (float)lightx, ly = (float)lighty;
    m_light = T3DPointD(aff.a11 * lx + aff.a12 * ly, aff.a21 * lx + aff.a22 * ly,
                        100.0);
    m_light = normalize(m_light);
    m_half  = normalize(m_light + T3DPointD(0, 0, 1));

    m_specular[0] = metal * (1 - color.r) + color.r;
    m_specular[1] = metal * (1 - color.g) + color.g;
    m_specular[2] = metal * (1 - color.b) + color.b;
  }

  TCanvasColor at(const T3DPointD &normal) const {
    // Normals go through the inverse transposed.
    T3DPointD n(m_inv[0] * normal.x + m_inv[2] * normal.y,
                m_inv[1] * normal.x + m_inv[3] * normal.y, normal.z);
    if (norm2(n) > 0.0) n = normalize(n);

    const double diffuse = std::max(n * m_light, 0.0);
    const double shine =
        diffuse > 0.0 ? std::pow(std::max(n * m_half, 0.0), m_shininess) : 0.0;
    const double ambient = 0.2;
    const double rgb[3]  = {m_color.r, m_color.g, m_color.b};
    float out[3];
    for (int c = 0; c < 3; c++)
      out[c] = (float)std::min(
          (ambient + diffuse) * rgb[c] + shine * m_specular[c], 1.0);
    // The material's alpha is 1 whatever the colour's: it always was.
    return TCanvasColor(out[0], out[1], out[2], 1.f);
  }
};

}  // namespace

//-----------------------------------------------------------------------------

// Not what drawStroke() is seen through the OpenGL canvas: drawStroke() has
// OpenGL work out the lighting, and no two drivers do that to the same bit.
// Here the same formula is worked out per vertex and the stroke goes out as
// one mesh with those colours, with smooth edges in place of the two lit
// lines the OpenGL version draws along its sides.
void TNormal2StrokeStyle::drawStrokeOnCanvas(TCanvas &canvas,
                                             const TColorFunction *cf,
                                             TStrokeOutline *outline,
                                             const TStroke *stroke,
                                             bool antialias) const {
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  double bend                         = 2 * m_bend;
  const std::vector<TOutlinePoint> &v = outline->getArray();
  if (v.size() < 4) return;

  const Normal2Light light(canvas.transform(), m_lightx, m_lighty,
                           toPixelD(color), m_metal, m_shininess);

  // Across the stroke the normal leans from one side to the other, as on
  // a rounded ridge.
  std::vector<T3DPointD> across;
  for (UINT i = 0; i + 1 < v.size(); i += 2) {
    T3DPointD d(v[i + 1].x - v[i].x, v[i + 1].y - v[i].y, 0);
    if (norm2(d) > 0.0) d = normalize(d);
    across.push_back(d);
  }

  TCanvasMesh mesh;
  mesh.mode        = TCanvasMesh::Mode::Triangles;
  mesh.smoothEdges = true;
  const T3DPointD up(0, 0, 1);
  for (UINT k = 0; k + 1 < across.size(); k++) {
    const UINT i = 2 * k;
    const TPointD olda(v[i].x, v[i].y), oldb(v[i + 1].x, v[i + 1].y);
    const TPointD a(v[i + 2].x, v[i + 2].y), b(v[i + 3].x, v[i + 3].y);
    const TPointD oldcenter = 0.5 * (olda + oldb), center = 0.5 * (a + b);

    // The normals as the OpenGL version sets them: the far side of each
    // piece already leans the way the next piece does.
    const TCanvasColor coldA = light.at(up - bend * across[k]);
    const TCanvasColor coldB = light.at(up + bend * across[k]);
    const TCanvasColor cA    = light.at(up - bend * across[k]);
    const TCanvasColor cB    = light.at(up + bend * across[k + 1]);
    const TCanvasColor cMid  = light.at(up);

    // Two quads side by side: olda a center oldcenter, oldcenter center b
    // oldb.
    const TPointD quads[2][4]      = {{olda, a, center, oldcenter},
                                      {oldcenter, center, b, oldb}};
    const TCanvasColor shades[2][4] = {{coldA, cA, cMid, cMid},
                                       {cMid, cMid, cB, coldB}};
    for (int q = 0; q < 2; q++)
      for (int corner : {0, 1, 2, 0, 2, 3})
        addVertex(mesh, quads[q][corner], shades[q][corner]);
  }
  canvas.drawMesh(mesh, linePaint(TCanvasColor(1.f, 1.f, 1.f, 1.f), antialias));
}


//=============================================================================

namespace {
double get_inout_intensityslope(double in, double out, double t) {
  if (out < in) out = in;
  if (t < in)
    return t / in;
  else if (t > out)
    return (t - 1) / (out - 1);
  else
    return 1;
}
}

//-----------------------------------------------------------------------------

TChalkStrokeStyle2::TChalkStrokeStyle2()
    : m_color(TPixel32::Black)
    , m_blend(1.0)
    , m_intensity(50.0)
    , m_in(0.25)
    , m_out(0.25)
    , m_noise(0.0) {}

//-----------------------------------------------------------------------------

TColorStyle *TChalkStrokeStyle2::clone() const {
  return new TChalkStrokeStyle2(*this);
}

//-----------------------------------------------------------------------------

int TChalkStrokeStyle2::getParamCount() const { return 5; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TChalkStrokeStyle2::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TChalkStrokeStyle2::getParamNameText(int index) const {
  assert(0 <= index && index < 5);
  TTranslatableText value;
  switch (index) {
  case 0:
    value = TTranslatableText(
        "TChalkStrokeStyle2",
        QT_TRANSLATE_NOOP("TChalkStrokeStyle2", "Border Fade"));
    break;
  case 1:
    value =
        TTranslatableText("TChalkStrokeStyle2",
                          QT_TRANSLATE_NOOP("TChalkStrokeStyle2", "Density"));
    break;
  case 2:
    value =
        TTranslatableText("TChalkStrokeStyle2",
                          QT_TRANSLATE_NOOP("TChalkStrokeStyle2", "Fade In"));
    break;
  case 3:
    value =
        TTranslatableText("TChalkStrokeStyle2",
                          QT_TRANSLATE_NOOP("TChalkStrokeStyle2", "Fade Out"));
    break;
  case 4:
    value = TTranslatableText("TChalkStrokeStyle2",
                              QT_TRANSLATE_NOOP("TChalkStrokeStyle2", "Noise"));
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TChalkStrokeStyle2::getParamRange(int index, double &min,
                                       double &max) const {
  assert(0 <= index && index < 5);
  switch (index) {
  case 0:
    min = 0.0;
    max = 1.0;
    break;
  case 1:
    min = 0.0;
    max = 100.0;
    break;
  case 2:
    min = 0.0;
    max = 1.0;
    break;
  case 3:
    min = 0.0;
    max = 1.0;
    break;
  case 4:
    min = 0.0;
    max = 1.0;
    break;
  }
}

//-----------------------------------------------------------------------------

double TChalkStrokeStyle2::getParamValue(TColorStyle::double_tag,
                                         int index) const {
  assert(0 <= index && index < 5);
  double value = 0;
  switch (index) {
  case 0:
    value = m_blend;
    break;
  case 1:
    value = m_intensity;
    break;
  case 2:
    value = m_in;
    break;
  case 3:
    value = m_out;
    break;
  case 4:
    value = m_noise;
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TChalkStrokeStyle2::setParamValue(int index, double value) {
  assert(0 <= index && index < 5);
  switch (index) {
  case 0:
    m_blend = value;
    break;
  case 1:
    m_intensity = value;
    break;
  case 2:
    m_in = value;
    break;
  case 3:
    m_out = value;
    break;
  case 4:
    m_noise = value;
    break;
  }

  // updateVersionNumber(); non serve perche' i parametri vengono sfrutttati
  // direttamente nella draw
}

//-----------------------------------------------------------------------------

void TChalkStrokeStyle2::loadData(int ids, TInputStreamInterface &is) {
  if (ids != 105)
    throw TException("chalk stroke style: unknown obsolete format");
  m_in = 0.0, m_out = 0.0, m_noise = 0.0;

  is >> m_color >> m_blend >> m_intensity;
  m_blend = 1 - m_blend;
}

//-----------------------------------------------------------------------------

void TChalkStrokeStyle2::computeData(Doubles &data, const TStroke *stroke,
                                     const TColorFunction *cf,
    double pixelSize) const {
  double length = stroke->getLength();
  double step   = 4;
  double s      = 0;

  data.clear();
  data.reserve(tceil(length / step) * 6 + 6);

  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    // if(w<0) {s+=0.1; continue;} // per tamponare il baco della
    // getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    double normu    = norm2(u);
    if (normu == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u = normalize(u);

    data.push_back(pos.x);
    data.push_back(pos.y);
    data.push_back(pos.thick);
    data.push_back(u.x);
    data.push_back(u.y);
    data.push_back(s / length);

    s += step;
  }
}

//-----------------------------------------------------------------------------

void TChalkStrokeStyle2::drawStroke(const TColorFunction *cf, Doubles &data,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, data, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TChalkStrokeStyle2::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    Doubles &data, const TStroke *stroke, bool antialias) const {
  double step = 4;

  double blend = m_blend;  // distanza che controlla da dove il gessetto
                           // comincia il fade out  (0, 1)
  double intensitymax =
      m_intensity;  // quanti punti vengono disegnati ad ogni step

  TRandom rnd;
  TRandom rnd_noise;
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;
  TPixelD dcolor;
  dcolor             = toPixelD(color);
  double mattedcolor = 0.5 * dcolor.m;

  double noise      = 0;
  double noiseslope = 0;
  double tmpnoise   = 0;


  for (UINT i = 0; i < data.size(); i += 6) {
    TThickPoint pos;
    pos.x     = data[i];
    pos.y     = data[i + 1];
    pos.thick = data[i + 2];
    TPointD u;
    u.x       = data[i + 3];
    u.y       = data[i + 4];
    TPointD v = rotate90(u);

    TPointD shift;
    double intslope    = get_inout_intensityslope(m_in, 1 - m_out, data[i + 5]);
    double transpslope = (intslope / blend) * dcolor.m;
    if (m_noise) {
      if (tmpnoise <= 0) {
        noise    = (100 / m_noise) * rnd_noise.getFloat();
        tmpnoise = noise;
      }
      noiseslope = get_inout_intensityslope(0.5, 0.5, tmpnoise / noise);
      tmpnoise -= step;
    } else
      noiseslope = 1;

    for (int i = 0; i < intensitymax * intslope * noiseslope; i++) {
      double vrandnorm = rnd.getFloat(-1.0, 1.0);
      double randomv   = vrandnorm * pos.thick * noiseslope;
      double randomu   = (0.5 - rnd.getFloat()) * step;
      shift            = pos + u * randomu + v * randomv;
      double mod       = fabs(vrandnorm);
      TCanvasColor grain;
      if (mod > 1 - blend)
        grain = floatColor(dcolor,
                           (double)rnd.getFloat() * ((1 - mod) * transpslope));
      else
        grain = floatColor(dcolor, mattedcolor);
      // A square two units across, moved into place by the transform.
      canvas.save();
      canvas.concat(TCanvasAffine::translation(shift.x, shift.y));
      drawQuad(canvas, TPointD(1, 1), TPointD(-1, 1), TPointD(-1, -1),
               TPointD(1, -1), grain);
      canvas.restore();
    }
  }

}

//=============================================================================

TBlendStrokeStyle2::TBlendStrokeStyle2()
    : m_color(TPixel32::Red), m_blend(1.0), m_in(0.25), m_out(0.25) {}

//-----------------------------------------------------------------------------

TColorStyle *TBlendStrokeStyle2::clone() const {
  return new TBlendStrokeStyle2(*this);
}

//-----------------------------------------------------------------------------

int TBlendStrokeStyle2::getParamCount() const { return 3; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TBlendStrokeStyle2::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TBlendStrokeStyle2::getParamNameText(int index) const {
  assert(0 <= index && index < 3);
  TTranslatableText value;
  switch (index) {
  case 0:
    value = TTranslatableText(
        "TBlendStrokeStyle2",
        QT_TRANSLATE_NOOP("TBlendStrokeStyle2", "Border Fade"));
    break;
  case 1:
    value =
        TTranslatableText("TBlendStrokeStyle2",
                          QT_TRANSLATE_NOOP("TBlendStrokeStyle2", "Fade In"));
    break;
  case 2:
    value =
        TTranslatableText("TBlendStrokeStyle2",
                          QT_TRANSLATE_NOOP("TBlendStrokeStyle2", "Fade Out"));
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TBlendStrokeStyle2::getParamRange(int index, double &min,
                                       double &max) const {
  assert(0 <= index && index < 3);
  min = 0.0;
  max = 1.0;
}

//-----------------------------------------------------------------------------

double TBlendStrokeStyle2::getParamValue(TColorStyle::double_tag,
                                         int index) const {
  assert(0 <= index && index < 3);
  double value = 0;
  switch (index) {
  case 0:
    value = m_blend;
    break;
  case 1:
    value = m_in;
    break;
  case 2:
    value = m_out;
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TBlendStrokeStyle2::setParamValue(int index, double value) {
  assert(0 <= index && index < 3);
  switch (index) {
  case 0:
    m_blend = value;
    break;
  case 1:
    m_in = value;
    break;
  case 2:
    m_out = value;
    break;
  }

  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TBlendStrokeStyle2::loadData(int ids, TInputStreamInterface &is) {
  if (ids != 110)
    throw TException("Blend  stroke style: unknown obsolete format");
  m_in = 0.0, m_out = 0.0;
  is >> m_color >> m_blend;
  m_blend = 1 - m_blend;
}

//-----------------------------------------------------------------------------

void TBlendStrokeStyle2::computeData(PointsAndDoubles &data,
                                     const TStroke *stroke,
                                     const TColorFunction *cf,
    double pixelSize) const {
  data.clear();
  // TStroke *stroke = getStroke();
  double length = stroke->getLength();
  double step   = 10.0;
  TPointD pos1, pos2, pos3, pos4, oldPos1, oldPos2, oldPos3, oldPos4;
  double oldintslope;
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;
  TPixelD dcolor;
  dcolor           = toPixelD(color);
  bool firstRing   = true;
  double s         = 0;
  double maxfactor = 2 * m_blend / step;  // max definisce il numero di
                                          // intervalli in cui la regione viene
                                          // divisa
  // per evitare il problema del blend poco efficiente sui triangoli
  double minthickness = MINTHICK * pixelSize;
  double thickness    = 0;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    if (w < 0) {
      s += 0.1;
      continue;
    }  // per tamponare il baco della getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    if (norm2(u) == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u = normalize(u);
    if (pos.thick < MINTHICK)
      thickness = minthickness;
    else
      thickness = pos.thick;
    TPointD v   = rotate90(u) * thickness;

    TPointD v1 = v * (1 - m_blend);
    pos1       = pos + v;
    pos2       = pos + v1;
    pos3       = pos - v1;
    pos4       = pos - v;
    double intslope =
        get_inout_intensityslope(m_in, 1 - m_out, s / length) * dcolor.m;
    if (firstRing) {
      firstRing = false;
    } else {
      int max       = (int)(maxfactor * thickness);
      double invmax = 1.0 / max;
      data.push_back(make_pair(oldPos1, 0.0));
      data.push_back(make_pair(pos1, 0.0));
      int i;
      for (i = 1; i < max; i++) {
        data.push_back(make_pair(i * (oldPos2 - oldPos1) * invmax + oldPos1,
                                 (i * oldintslope) * invmax));
        data.push_back(make_pair(i * (pos2 - pos1) * invmax + pos1,
                                 (i * intslope) * invmax));
      }

      data.push_back(make_pair(oldPos2, oldintslope));
      data.push_back(make_pair(pos2, intslope));
      data.push_back(make_pair(oldPos3, oldintslope));
      data.push_back(make_pair(pos3, intslope));

      for (i = 0; i < max; i++) {
        data.push_back(make_pair(i * (oldPos4 - oldPos3) * invmax + oldPos3,
                                 (oldintslope * invmax) * (max - i)));
        data.push_back(make_pair(i * (pos4 - pos3) * invmax + pos3,
                                 (intslope * invmax) * (max - i)));
      }

      data.push_back(make_pair(oldPos4, 0.0));
      data.push_back(make_pair(pos4, 0.0));
    }
    oldPos1     = pos1;
    oldPos2     = pos2;
    oldPos3     = pos3;
    oldPos4     = pos4;
    oldintslope = intslope;
    s += step;
  }
}

//-----------------------------------------------------------------------------

void TBlendStrokeStyle2::drawStroke(const TColorFunction *cf, PointsAndDoubles &data,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, data, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TBlendStrokeStyle2::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    PointsAndDoubles &data, const TStroke *stroke, bool antialias) const {
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  TPixelD dcolor;
  dcolor = toPixelD(color);

  // One band, every vertex with an opacity of its own.
  TCanvasMesh band = plainMesh(TCanvasMesh::Mode::TriangleStrip);
  PointsAndDoubles::iterator it = data.begin();
  for (; it != data.end(); ++it)
    addVertex(band, it->first, floatColor(dcolor, it->second));
  drawQuadStrip(canvas, band, plainPaint());
}

//=============================================================================

TTwirlStrokeStyle::TTwirlStrokeStyle()
    : m_color(TPixel32::Green), m_period(30.0), m_blend(0.5) {}

//-----------------------------------------------------------------------------

TColorStyle *TTwirlStrokeStyle::clone() const {
  return new TTwirlStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TTwirlStrokeStyle::getParamCount() const { return 2; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TTwirlStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TTwirlStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 2);
  return index == 0 ? TTranslatableText(
                          "TTwirlStrokeStyle",
                          QT_TRANSLATE_NOOP("TTwirlStrokeStyle", "Twirl"))
                    : TTranslatableText(
                          "TTwirlStrokeStyle",
                          QT_TRANSLATE_NOOP("TTwirlStrokeStyle", "Shade"));
}

//-----------------------------------------------------------------------------

void TTwirlStrokeStyle::getParamRange(int index, double &min,
                                      double &max) const {
  assert(0 <= index && index < 2);
  if (index == 0) {
    min = 1.0;
    max = 100.0;
  } else {
    min = 0.0;
    max = 1.0;
  }
}

//-----------------------------------------------------------------------------

double TTwirlStrokeStyle::getParamValue(TColorStyle::double_tag,
                                        int index) const {
  assert(0 <= index && index < 2);
  return index == 0 ? m_period : m_blend;
}

//-----------------------------------------------------------------------------

void TTwirlStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 2);
  if (index == 0)
    m_period = value;
  else
    m_blend = value;

  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TTwirlStrokeStyle::computeData(Doubles &data, const TStroke *stroke,
                                    const TColorFunction *cf,
    double pixelSize) const {
  double length   = stroke->getLength();
  double step     = 5.0;
  double period   = 10 * (102 - m_period);
  double hperiod  = period / 2;
  double blendval = 0;
  TRandom rnd;

  data.clear();
  data.reserve(tceil(length / step) + 1);

  double s = 0;
  TPointD app;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    if (w < 0) {
      s += 0.1;
      continue;
    }  // per tamponare il baco della getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    if (norm2(u) == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u            = normalize(u);
    TPointD v    = rotate90(u) * (pos.thick);
    double shift = sin((M_PI / hperiod) * s);

    app = pos + v * shift;
    data.push_back(app.x);
    data.push_back(app.y);
    app = pos - v * shift;
    data.push_back(app.x);
    data.push_back(app.y);

    blendval = get_inout_intensityslope(
        m_blend, 1.0 - m_blend, (s - ((int)(s / hperiod) * hperiod)) / hperiod);
    data.push_back(blendval);
    s += step;
  }
}

//-----------------------------------------------------------------------------

void TTwirlStrokeStyle::drawStroke(const TColorFunction *cf, Doubles &data,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, data, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TTwirlStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    Doubles &data, const TStroke *stroke, bool antialias) const {
  TPixel32 blackcolor = TPixel32::Black;

  TPixel32 color;
  if (cf) {
    color      = (*(cf))(m_color);
    blackcolor = (*(cf))(blackcolor);
  } else {
    color = m_color;
  }
  blackcolor.m = m_color.m;

  TPointD app;
  UINT i = 0;

  // The band, shaded between the colour and black along the stroke...
  TCanvasMesh band = plainMesh(TCanvasMesh::Mode::TriangleStrip);
  for (; i + 4 < data.size(); i += 5) {
    const TPixel32 shade = blend(blackcolor, color, data[i + 4]);
    app.x                = data[i];
    app.y                = data[i + 1];
    addVertex(band, app, shade);
    app.x = data[i + 2];
    app.y = data[i + 3];
    addVertex(band, app, shade);
  }
  canvas.drawMesh(band, plainPaint());

  // ...and its two sides piece by piece, shaded the other way.
  for (i = 5; i + 4 < data.size(); i += 5) {
    TCanvasPath sides;
    addSegment(sides, TPointD(data[i - 5], data[i - 4]),
               TPointD(data[i], data[i + 1]));
    addSegment(sides, TPointD(data[i - 3], data[i - 2]),
               TPointD(data[i + 2], data[i + 3]));
    strokeLines(canvas, sides, blend(color, blackcolor, data[i + 4]),
                antialias);
  }
}

//=============================================================================

TSawToothStrokeStyle::TSawToothStrokeStyle(TPixel32 color, double parameter)
    : m_color(color), m_parameter(parameter) {}

//-----------------------------------------------------------------------------

TColorStyle *TSawToothStrokeStyle::clone() const {
  return new TSawToothStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

void TSawToothStrokeStyle::computeOutline(
    const TStroke *stroke, TStrokeOutline &outline,
    TOutlineUtil::OutlineParameter param) const {
  param.m_lengthStep = m_parameter;
  TOutlineStyle::computeOutline(stroke, outline, param);
}

//-----------------------------------------------------------------------------

void TSawToothStrokeStyle::drawStroke(const TColorFunction *cf,
    TStrokeOutline *outline, const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, outline, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TSawToothStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    TStrokeOutline *outline, const TStroke *stroke, bool antialias) const {
  UINT i;
  int counter = 0;
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  const std::vector<TOutlinePoint> &v = outline->getArray();
  if (v.empty()) return;

  // The teeth: triangles, and one line that goes round each of them in
  // turn.
  TPointD old;
  std::vector<TPointD> line;
  TCanvasMesh teeth = plainMesh(TCanvasMesh::Mode::Triangles);
  for (i = 0; i < v.size() - 2; i += 2) {
    if (0 != v[i].stepCount) {
      if (counter) {
        const TPointD a(v[i].x, v[i].y), b(v[i + 1].x, v[i + 1].y);
        line.push_back(old);
        line.push_back(a);
        line.push_back(b);
        line.push_back(old);
        addVertex(teeth, old, color);
        addVertex(teeth, a, color);
        addVertex(teeth, b, color);
      }
      old = TPointD(v[i].x, v[i].y);
      counter++;
    }
  }
  TCanvasPath path;
  addPolyline(path, line);
  strokeLines(canvas, path, color, antialias);
  canvas.drawMesh(teeth, plainPaint());
}

//-----------------------------------------------------------------------------

int TSawToothStrokeStyle::getParamCount() const { return 1; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TSawToothStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TSawToothStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 1);

  return TTranslatableText(
      "TSawToothStrokeStyle",
      QT_TRANSLATE_NOOP("TSawToothStrokeStyle", "Distance"));
}

//-----------------------------------------------------------------------------

void TSawToothStrokeStyle::getParamRange(int index, double &min,
                                         double &max) const {
  assert(0 <= index && index < 1);
  min = 0.1;
  max = 100.0;
}

//-----------------------------------------------------------------------------

double TSawToothStrokeStyle::getParamValue(TColorStyle::double_tag,
                                           int index) const {
  assert(0 <= index && index < 1);
  return m_parameter;
}

//-----------------------------------------------------------------------------

void TSawToothStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 1);
  m_parameter = value;
  updateVersionNumber();  // questo si chiama per ogno cambiamento di parametro
                          // per cui di deve ricalcolare l'outline
}

//=============================================================================

TMultiLineStrokeStyle2::TMultiLineStrokeStyle2()
    : m_color0(TPixel32(0, 255, 0))
    , m_color1(TPixel32(0, 0, 0))
    , m_intensity(0.2)
    , m_length(20.0)
    , m_thick(0.3)
    , m_noise(0.5) {}

//-----------------------------------------------------------------------------

TColorStyle *TMultiLineStrokeStyle2::clone() const {
  return new TMultiLineStrokeStyle2(*this);
}

//-----------------------------------------------------------------------------

namespace {

typedef struct {
  TPointD u, v;
  TThickPoint p;
} myLineData;
}

//-----------------------------------------------------------------------------

int TMultiLineStrokeStyle2::getParamCount() const { return 4; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TMultiLineStrokeStyle2::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TMultiLineStrokeStyle2::getParamNameText(int index) const {
  assert(0 <= index && index < 4);
  TTranslatableText value;
  switch (index) {
  case 0:
    value = TTranslatableText(
        "TMultiLineStrokeStyle2",
        QT_TRANSLATE_NOOP("TMultiLineStrokeStyle2", "Density"));
    break;
  case 1:
    value =
        TTranslatableText("TMultiLineStrokeStyle2",
                          QT_TRANSLATE_NOOP("TMultiLineStrokeStyle2", "Size"));
    break;
  case 2:
    value = TTranslatableText(
        "TMultiLineStrokeStyle2",
        QT_TRANSLATE_NOOP("TMultiLineStrokeStyle2", "Thickness"));
    break;
  case 3:
    value =
        TTranslatableText("TMultiLineStrokeStyle2",
                          QT_TRANSLATE_NOOP("TMultiLineStrokeStyle2", "Noise"));
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TMultiLineStrokeStyle2::getParamRange(int index, double &min,
                                           double &max) const {
  assert(0 <= index && index < 4);
  switch (index) {
  case 0:
    min = 0.0;
    max = 1.0;
    break;
  case 1:
    min = 0.0;
    max = 100.0;
    break;
  case 2:
    min = 0.0;
    max = 1.0;
    break;
  case 3:
    min = 0.0;
    max = 1.0;
    break;
  }
}
//-----------------------------------------------------------------------------

double TMultiLineStrokeStyle2::getParamValue(TColorStyle::double_tag,
                                             int index) const {
  assert(0 <= index && index < 4);
  double value = 0;
  switch (index) {
  case 0:
    value = m_intensity;
    break;
  case 1:
    value = m_length;
    break;
  case 2:
    value = m_thick;
    break;
  case 3:
    value = m_noise;
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TMultiLineStrokeStyle2::setParamValue(int index, double value) {
  assert(0 <= index && index < 4);
  switch (index) {
  case 0:
    m_intensity = value;
    break;
  case 1:
    m_length = value;
    break;
  case 2:
    m_thick = value;
    break;
  case 3:
    m_noise = value;
    break;
  }

  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TMultiLineStrokeStyle2::loadData(int ids, TInputStreamInterface &is) {
  if (ids != 118 && ids != 128)
    throw TException("Multi Line stroke style: unknown obsolete format");
  if (ids == 118) {
    m_length = 20.0, m_thick = 0.3, m_noise = 0.0;
    is >> m_color0 >> m_intensity;
    m_color1 = TPixel32::Black;
  } else {
    is >> m_color0 >> m_intensity >> m_length >> m_thick >> m_noise;
    m_color1 = TPixel32::Black;
  }
}

//-----------------------------------------------------------------------------

void TMultiLineStrokeStyle2::computeData(BlendAndPoints &data,
                                         const TStroke *stroke,
                                         const TColorFunction *cf,
    double pixelSize) const {
  // TStroke *stroke = getStroke();
  double length = stroke->getLength();
  double step   = 4.0;
  int maxlength = (int)m_length;
  double factor = 0;
  TRandom rnd;

  vector<myLineData> LineData;
  myLineData Data;
  double s = 0;

  double minthickness = MINTHICK * pixelSize;
  double thickness    = 0;
  double strokethick  = m_thick;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    if (w < 0) {
      s += 0.1;
      continue;
    }  // per tamponare il baco della getParameterAtLength()
    Data.p = stroke->getThickPoint(w);
    Data.u = stroke->getSpeed(w);
    if (norm2(Data.u) == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    Data.u = normalize(Data.u);
    if (Data.p.thick < MINTHICK)
      thickness = minthickness;
    else
      thickness = Data.p.thick;
    Data.v      = rotate90(Data.u) * thickness;

    LineData.push_back(Data);
    s += step;
  }

  BlendAndPoint appData;

  data.clear();
  data.reserve(LineData.size());

  for (int i = 0; i < m_intensity * LineData.size(); i++) {
    appData.points.clear();
    int start = rnd.getInt(0, LineData.size());
    int end   = start + maxlength + rnd.getInt(0, maxlength);
    if (end > (int)LineData.size()) end = LineData.size();
    double halfcount                    = (end - start) / 2.0;
    double vshift                       = (0.5 - rnd.getFloat());

    appData.blend = rnd.getFloat();

    for (int j = 0; j < (end - start); j++) {
      if (j < halfcount)
        factor = j / halfcount;
      else
        factor = 1 - (j - halfcount) / halfcount;
      appData.points.push_back(
          LineData[j + start].p +
          LineData[j + start].v *
              (vshift -
               strokethick * factor * (1 - m_noise * (1 - rnd.getFloat()))));
      appData.points.push_back(
          LineData[j + start].p +
          LineData[j + start].v *
              (vshift +
               strokethick * factor * (1 - m_noise * (1 - rnd.getFloat()))));
    }
    data.push_back(appData);
  }
}

//-----------------------------------------------------------------------------

void TMultiLineStrokeStyle2::drawStroke(const TColorFunction *cf, BlendAndPoints &data,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, data, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TMultiLineStrokeStyle2::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    BlendAndPoints &data, const TStroke *stroke, bool antialias) const {
  TPixel32 color0, color1;
  if (cf) {
    color0 = (*(cf))(m_color0);
    color1 = (*(cf))(m_color1);
  } else {
    color0 = m_color0;
    color1 = m_color1;
  }

  // Each line is a band of its own colour. They are the one thing here
  // that is asked to have smooth edges, whatever the render says: the old
  // code switched on polygon smoothing for them.
  TCanvasPaint smooth = plainPaint();
  smooth.antialias    = true;
  for (UINT i = 0; i < data.size(); i++) {
    const TPixel32 color = blend(color0, color1, data[i].blend);
    TCanvasMesh band     = plainMesh(TCanvasMesh::Mode::TriangleStrip);
    band.smoothEdges     = true;
    for (UINT j = 0; j < data[i].points.size(); j++)
      addVertex(band, data[i].points[j], color);
    drawQuadStrip(canvas, band, smooth);
  }
}

//=============================================================================

TZigzagStrokeStyle::TZigzagStrokeStyle()
    : m_color(TPixel32(0, 0, 0))
    , m_minDist(0.5)
    , m_maxDist(6.0)
    , m_minAngle(30.0)
    , m_maxAngle(60.0)
    , m_thickness(1.0) {}

//-----------------------------------------------------------------------------

TColorStyle *TZigzagStrokeStyle::clone() const {
  return new TZigzagStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TZigzagStrokeStyle::getParamCount() const { return 5; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TZigzagStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TZigzagStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 5);
  TTranslatableText value;
  switch (index) {
  case 0:
    value = TTranslatableText(
        "TZigzagStrokeStyle",
        QT_TRANSLATE_NOOP("TZigzagStrokeStyle", "Min Distance"));
    break;
  case 1:
    value = TTranslatableText(
        "TZigzagStrokeStyle",
        QT_TRANSLATE_NOOP("TZigzagStrokeStyle", "Max Distance"));
    break;
  case 2:
    value =
        TTranslatableText("TZigzagStrokeStyle",
                          QT_TRANSLATE_NOOP("TZigzagStrokeStyle", "Min Angle"));
    break;
  case 3:
    value =
        TTranslatableText("TZigzagStrokeStyle",
                          QT_TRANSLATE_NOOP("TZigzagStrokeStyle", "Max Angle"));
    break;
  case 4:
    value =
        TTranslatableText("TZigzagStrokeStyle",
                          QT_TRANSLATE_NOOP("TZigzagStrokeStyle", "Thickness"));
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TZigzagStrokeStyle::getParamRange(int index, double &min,
                                       double &max) const {
  assert(0 <= index && index < 5);
  switch (index) {
  case 0:
    min = 0.5;
    max = 50.0;
    break;
  case 1:
    min = 0.5;
    max = 50.0;
    break;
  case 2:
    min = -90.0;
    max = 90.0;
    break;
  case 3:
    min = -90.0;
    max = 90.0;
    break;
  case 4:
    min = 0.0;
    max = 3.0;
    break;
  }
}

//-----------------------------------------------------------------------------

double TZigzagStrokeStyle::getParamValue(TColorStyle::double_tag,
                                         int index) const {
  assert(0 <= index && index < 5);
  double value = 0;
  switch (index) {
  case 0:
    value = m_minDist;
    break;
  case 1:
    value = m_maxDist;
    break;
  case 2:
    value = m_minAngle;
    break;
  case 3:
    value = m_maxAngle;
    break;
  case 4:
    value = m_thickness;
    break;
  }
  return value;
}

//-----------------------------------------------------------------------------

void TZigzagStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 5);
  switch (index) {
  case 0:
    m_minDist = value;
    break;
  case 1:
    m_maxDist = value;
    break;
  case 2:
    m_minAngle = value;
    break;
  case 3:
    m_maxAngle = value;
    break;
  case 4:
    m_thickness = value;
    break;
  }
  updateVersionNumber();
}

//-----------------------------------------------------------------------------

bool TZigzagStrokeStyle::getZigZagPosition(const TStroke *stroke, TRandom &rnd,
                                           const double s, const int first,
                                           const double minTranslLength,
                                           TThickPoint &pos,
                                           TThickPoint &pos1) const {
  double w  = stroke->getParameterAtLength(s);
  pos       = stroke->getThickPoint(w);
  TPointD u = stroke->getSpeed(w);
  if (norm2(u) < TConsts::epsilon) return false;
  u          = normalize(u);
  TPointD uu = u;
  double angle =
      m_minAngle + (m_maxAngle - m_minAngle) * (double)rnd.getUInt(101) * 0.01;
  TRotation rotM(angle);
  u                      = rotM * u;
  double maxTranslLength = angle == 90 ? 1.0 : 2.0;
  if (angle > 30 && angle < 90) {
    double rta      = 1.0 / tan(degree2rad(angle));
    maxTranslLength = sqrt(sq(rta) + 1.0);
  }
  double r =
      (minTranslLength + (maxTranslLength - minTranslLength) * rnd.getFloat()) *
      pos.thick * first;
  pos  = pos + r * u;
  pos1 = pos + uu * m_thickness;
  return true;
}

//-----------------------------------------------------------------------------

void TZigzagStrokeStyle::setRealMinMax() const {
  TZigzagStrokeStyle *ncpthis = const_cast<TZigzagStrokeStyle *>(this);
  double minDist              = std::min(m_minDist, m_maxDist);
  double maxDist              = std::max(m_minDist, m_maxDist);
  double minAngle             = std::min(m_minAngle, m_maxAngle);
  double maxAngle             = std::max(m_minAngle, m_maxAngle);
  ncpthis->m_minDist          = minDist;
  ncpthis->m_maxDist          = maxDist;
  ncpthis->m_minAngle         = minAngle;
  ncpthis->m_maxAngle         = maxAngle;
}

//-----------------------------------------------------------------------------

void TZigzagStrokeStyle::computeData(Points &points, const TStroke *stroke,
                                     const TColorFunction *cf,
    double pixelSize) const {
  assert(!!stroke);
  double length = stroke->getLength();
  if (length <= 0) return;

  setRealMinMax();
  // e.g minimum translation length is the half of the thickness
  const double minTranslLength = 0.7;
  // const bool isTransparent=m_color.m<255;

  int first = 1;
  TThickPoint pos;
  TThickPoint pos1;
  TRandom rnd;

  points.clear();
  points.reserve(tceil(length / m_minDist) * 2 + 2);

  for (double s = 0.0; s <= length; first = -first) {
    if (getZigZagPosition(stroke, rnd, s, first, minTranslLength, pos, pos1)) {
      // TRectD rec(pos.x,pos.y,pos1.x,pos1.y);
      points.push_back(pos);
      points.push_back(pos1);
    }
    s += m_minDist + (m_maxDist - m_minDist) * (double)rnd.getUInt(101) * 0.01;
  }
  if (getZigZagPosition(stroke, rnd, length - TConsts::epsilon, first,
                        minTranslLength, pos, pos1)) {
    points.push_back(pos);
    points.push_back(pos1);
  }
}

//-----------------------------------------------------------------------------

void TZigzagStrokeStyle::drawStroke(const TColorFunction *cf, Points &points,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, points, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TZigzagStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    Points &points, const TStroke *stroke, bool antialias) const {
  if (points.size() <= 1) return;

  // Not the colour the colour function gives: it never was.
  const TPixel32 color = m_color;

  // The band, a line down each side, and the rungs between them.
  const std::size_t pairs = points.size() / 2;
  TCanvasMesh band        = plainMesh(TCanvasMesh::Mode::TriangleStrip);
  TCanvasPath left, right, rungs;
  for (std::size_t i = 0; i < pairs; i++) {
    const TPointD &a = points[2 * i], &b = points[2 * i + 1];
    addVertex(band, a, color);
    addVertex(band, b, color);
    if (i == 0) {
      left.moveTo(toCanvas(a));
      right.moveTo(toCanvas(b));
    } else {
      left.lineTo(toCanvas(a));
      right.lineTo(toCanvas(b));
    }
    addSegment(rungs, a, b);
  }
  canvas.drawMesh(band, plainPaint());
  strokeLines(canvas, left, color, antialias);
  strokeLines(canvas, right, color, antialias);
  strokeLines(canvas, rungs, color, antialias);
}

//=============================================================================

TSinStrokeStyle::TSinStrokeStyle()
    : m_color(TPixel32::Black), m_frequency(10.0), m_thick(0.4) {}

//-----------------------------------------------------------------------------

TColorStyle *TSinStrokeStyle::clone() const {
  return new TSinStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

int TSinStrokeStyle::getParamCount() const { return 2; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TSinStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TSinStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 2);
  return index == 0 ? TTranslatableText(
                          "TSinStrokeStyle",
                          QT_TRANSLATE_NOOP("TSinStrokeStyle", "Frequency"))
                    : TTranslatableText(
                          "TZigzTSinStrokeStyleagStrokeStyle",
                          QT_TRANSLATE_NOOP("TZigzTSinStrokeStyleagStrokeStyle",
                                            "Thickness"));
}

//-----------------------------------------------------------------------------

void TSinStrokeStyle::getParamRange(int index, double &min, double &max) const {
  assert(0 <= index && index < 2);
  if (index == 0) {
    min = 1.0;
    max = 20.0;
  } else {
    min = 0.0;
    max = 1.0;
  }
}

//-----------------------------------------------------------------------------

double TSinStrokeStyle::getParamValue(TColorStyle::double_tag,
                                      int index) const {
  assert(0 <= index && index < 2);
  return index == 0 ? m_frequency : m_thick;
}

//-----------------------------------------------------------------------------

void TSinStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 2);
  if (index == 0)
    m_frequency = value;
  else
    m_thick = value;

  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TSinStrokeStyle::computeData(Points &positions, const TStroke *stroke,
                                  const TColorFunction *cf,
    double pixelSize) const {
  double length = stroke->getLength();
  double step   = 5.0;

  positions.clear();
  positions.reserve(tceil((length + 1) / step));

  double frequency = m_frequency / 100;
  ;
  double s = 0;
  // bool firstRing = true;
  double thick = 1 - m_thick;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    // if(w<0) {s+=0.1; continue;} // per tamponare il baco della
    // getParameterAtLength()
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = stroke->getSpeed(w);
    double normu    = norm2(u);
    if (normu == 0) {
      s += 0.1;
      continue;
    }  // non dovrebbe succedere mai, ma per prudenza....
    u               = normalize(u);
    TPointD v       = rotate90(u);
    double sinvalue = sin(frequency * s);
    positions.push_back(pos + v * pos.thick * sinvalue);
    positions.push_back(pos + v * thick * pos.thick * sinvalue);
    s += step;
  }
}

//-----------------------------------------------------------------------------

void TSinStrokeStyle::drawStroke(const TColorFunction *cf, std::vector<TPointD> &positions,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, positions, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TSinStrokeStyle::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    std::vector<TPointD> &positions, const TStroke *stroke, bool antialias) const {
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  // The band, and a line down each of its sides.
  TCanvasMesh band = plainMesh(TCanvasMesh::Mode::TriangleStrip);
  std::vector<TPointD> left, right;
  int i = 0;
  for (; i + 1 < (int)positions.size(); i += 2) {
    addVertex(band, positions[i], color);
    addVertex(band, positions[i + 1], color);
  }
  canvas.drawMesh(band, plainPaint());

  for (i = 0; i < (int)positions.size(); i += 2) left.push_back(positions[i]);
  for (i = 1; i < (int)positions.size() - 1; i += 2)
    right.push_back(positions[i]);
  TCanvasPath path;
  addPolyline(path, left);
  strokeLines(canvas, path, color, antialias);
  path = TCanvasPath();
  addPolyline(path, right);
  strokeLines(canvas, path, color, antialias);
}

//=============================================================================

TFriezeStrokeStyle2::TFriezeStrokeStyle2()
    : m_color(TPixel32::Black), m_parameter(0.7), m_thick(0.3) {}

//-----------------------------------------------------------------------------

TColorStyle *TFriezeStrokeStyle2::clone() const {
  return new TFriezeStrokeStyle2(*this);
}

//-----------------------------------------------------------------------------

int TFriezeStrokeStyle2::getParamCount() const { return 2; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TFriezeStrokeStyle2::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TFriezeStrokeStyle2::getParamNameText(int index) const {
  assert(0 <= index && index < 2);
  if (index == 0)
    return TTranslatableText("TFriezeStrokeStyle2",
                             QT_TRANSLATE_NOOP("TFriezeStrokeStyle2", "Twirl"));
  else
    return TTranslatableText(
        "TFriezeStrokeStyle2",
        QT_TRANSLATE_NOOP("TFriezeStrokeStyle2", "Thickness"));
}

//-----------------------------------------------------------------------------

void TFriezeStrokeStyle2::getParamRange(int index, double &min,
                                        double &max) const {
  assert(0 <= index && index < 2);
  if (index == 0) {
    min = -1.0;
    max = 1.0;
  } else {
    min = 0.0;
    max = 1.0;
  }
}

//-----------------------------------------------------------------------------

double TFriezeStrokeStyle2::getParamValue(TColorStyle::double_tag,
                                          int index) const {
  assert(0 <= index && index < 2);
  if (index == 0)
    return m_parameter;
  else
    return m_thick;
}

//-----------------------------------------------------------------------------

void TFriezeStrokeStyle2::setParamValue(int index, double value) {
  assert(0 <= index && index < 2);
  if (index == 0)
    m_parameter = value;
  else
    m_thick = value;

  updateVersionNumber();
}

//-----------------------------------------------------------------------------

void TFriezeStrokeStyle2::loadData(int ids, TInputStreamInterface &is) {
  if (ids != 102)
    throw TException("Frieze  stroke style: unknown obsolete format");
  m_thick = 0.0;
  is >> m_color >> m_parameter;
}

//-----------------------------------------------------------------------------

void TFriezeStrokeStyle2::computeData(Points &positions, const TStroke *stroke,
                                      const TColorFunction *cf,
    double pixelSize) const {
  // TStroke *stroke = getStroke();
  double length = stroke->getLength();

  double ds = 0.5;

  positions.clear();
  positions.reserve(tceil((length + 1) / ds));

  double s     = 0.01;
  double lastS = 0;
  double phi   = 0;
  double lastW = 0;
  double thick = 1 - m_thick;
  vector<TPointD> points;
  while (s <= length) {
    double w = stroke->getParameterAtLength(s);
    if (w < lastW) {
      s += 0.1;
      continue;
    }
    lastW           = w;
    TThickPoint pos = stroke->getThickPoint(w);
    TPointD u       = normalize(stroke->getSpeed(w));
    TPointD v       = rotate90(u);

    double thickness = pos.thick;  // 5; //(1-t)*40 + t * 10;

    if (thickness > 0) {
      double omega = M_PI / thickness;

      double q        = 0.5 * (1 - cos(phi));
      double theta    = M_PI_2 - M_PI * m_parameter * q;
      double r        = thickness * sin(phi);
      double r1       = r * thick;
      double costheta = cos(theta);
      double sintheta = sin(theta);
      positions.push_back(pos + u * (r * costheta) + v * (r * sintheta));
      positions.push_back(pos + u * (r1 * costheta) + v * (r1 * sintheta));
      phi += (s - lastS) * omega;
      lastS = s;
    } else {
      positions.push_back(pos);
      positions.push_back(pos);
    }

    s += ds;
  }
}

//-----------------------------------------------------------------------------

void TFriezeStrokeStyle2::drawStroke(const TColorFunction *cf, Points &positions,
    const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, positions, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TFriezeStrokeStyle2::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    Points &positions, const TStroke *stroke, bool antialias) const {
  TPixel32 color;
  if (cf)
    color = (*(cf))(m_color);
  else
    color = m_color;

  // The band, and a line down each of its sides.
  TCanvasMesh band = plainMesh(TCanvasMesh::Mode::TriangleStrip);
  std::vector<TPointD> left, right;
  int i = 0;
  for (; i + 1 < (int)positions.size(); i += 2) {
    addVertex(band, positions[i], color);
    addVertex(band, positions[i + 1], color);
  }
  canvas.drawMesh(band, plainPaint());

  for (i = 0; i < (int)positions.size(); i += 2) left.push_back(positions[i]);
  for (i = 1; i < (int)positions.size() - 1; i += 2)
    right.push_back(positions[i]);
  TCanvasPath path;
  addPolyline(path, left);
  strokeLines(canvas, path, color, antialias);
  path = TCanvasPath();
  addPolyline(path, right);
  strokeLines(canvas, path, color, antialias);
}

//=============================================================================

TDualColorStrokeStyle2::TDualColorStrokeStyle2(TPixel32 color0, TPixel32 color1,
                                               double parameter)
    : m_color0(color0), m_color1(color1), m_parameter(parameter) {}

//-----------------------------------------------------------------------------

TColorStyle *TDualColorStrokeStyle2::clone() const {
  return new TDualColorStrokeStyle2(*this);
}

//-----------------------------------------------------------------------------

void TDualColorStrokeStyle2::computeOutline(
    const TStroke *stroke, TStrokeOutline &outline,
    TOutlineUtil::OutlineParameter param) const {
  param.m_lengthStep = m_parameter;
  TOutlineStyle::computeOutline(stroke, outline, param);
}

//-----------------------------------------------------------------------------

void TDualColorStrokeStyle2::drawStroke(const TColorFunction *cf,
    TStrokeOutline *outline, const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, outline, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TDualColorStrokeStyle2::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    TStrokeOutline *outline, const TStroke *stroke, bool antialias) const {
  UINT i;
  const std::vector<TOutlinePoint> &v = outline->getArray();
  TPixel32 colorv[2];

  if (cf) {
    colorv[0] = (*(cf))(m_color0);
    colorv[1] = (*(cf))(m_color1);
  } else {
    colorv[0] = m_color0;
    colorv[1] = m_color1;
  }
  if (v.empty()) return;

  // The stroke in stretches of the two colours in turn. Where the colour
  // changes, the points are there twice, once in each colour.
  for (int side = 0; side < 2; side++) {
    std::vector<TPointD> points;
    std::vector<TCanvasColor> colors;
    int colorindex = 0;
    for (i = side; i < v.size(); i += 2) {
      points.push_back(TPointD(v[i].x, v[i].y));
      colors.push_back(toCanvas(colorv[colorindex & 1]));
      if (0 != v[i].stepCount) {
        colorindex++;
        points.push_back(TPointD(v[i].x, v[i].y));
        colors.push_back(toCanvas(colorv[colorindex & 1]));
      }
    }
    strokeColoredPolyline(canvas, points, colors, antialias);
  }

  int colorindex    = 0;
  TCanvasMesh strip = plainMesh(TCanvasMesh::Mode::TriangleStrip);
  for (i = 0; i + 1 < v.size(); i += 2) {
    addVertex(strip, TPointD(v[i].x, v[i].y), colorv[colorindex & 1]);
    addVertex(strip, TPointD(v[i + 1].x, v[i + 1].y), colorv[colorindex & 1]);
    if (0 != v[i].stepCount) {
      colorindex++;
      addVertex(strip, TPointD(v[i].x, v[i].y), colorv[colorindex & 1]);
      addVertex(strip, TPointD(v[i + 1].x, v[i + 1].y),
                colorv[colorindex & 1]);
    }
  }
  canvas.drawMesh(strip, plainPaint());

  // A line across the stroke wherever the colour changes, always in the
  // first colour ("antialias delle linee normali", the old code says).
  TCanvasPath across;
  for (i = 0; i + 1 < v.size(); i += 2)
    if (0 != v[i].stepCount)
      addSegment(across, TPointD(v[i].x, v[i].y),
                 TPointD(v[i + 1].x, v[i + 1].y));
  strokeLines(canvas, across, colorv[0], antialias);
}

//-----------------------------------------------------------------------------

int TDualColorStrokeStyle2::getParamCount() const { return 1; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TDualColorStrokeStyle2::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TDualColorStrokeStyle2::getParamNameText(int index) const {
  assert(0 <= index && index < 1);

  return TTranslatableText(
      "TDualColorStrokeStyle2",
      QT_TRANSLATE_NOOP("TDualColorStrokeStyle2", "Distance"));
}

//-----------------------------------------------------------------------------

void TDualColorStrokeStyle2::getParamRange(int index, double &min,
                                           double &max) const {
  assert(0 <= index && index < 1);
  min = 1.0;
  max = 100.0;
}

//-----------------------------------------------------------------------------

double TDualColorStrokeStyle2::getParamValue(TColorStyle::double_tag,
                                             int index) const {
  assert(0 <= index && index < 1);
  return m_parameter;
}

//-----------------------------------------------------------------------------

void TDualColorStrokeStyle2::setParamValue(int index, double value) {
  assert(0 <= index && index < 1);
  m_parameter = value;
  updateVersionNumber();  // questo si chiama per ogno cambiamento di parametro
                          // per cui di deve ricalcolare l'outline
}

//=============================================================================

TLongBlendStrokeStyle2::TLongBlendStrokeStyle2(TPixel32 color0, TPixel32 color1,
                                               double parameter)
    : m_color0(color0), m_color1(color1), m_parameter(parameter) {}

//-----------------------------------------------------------------------------

TColorStyle *TLongBlendStrokeStyle2::clone() const {
  return new TLongBlendStrokeStyle2(*this);
}

//-----------------------------------------------------------------------------

void TLongBlendStrokeStyle2::computeOutline(
    const TStroke *stroke, TStrokeOutline &outline,
    TOutlineUtil::OutlineParameter param) const {
  param.m_lengthStep = m_parameter;
  TOutlineStyle::computeOutline(stroke, outline, param);
}

//-----------------------------------------------------------------------------

void TLongBlendStrokeStyle2::drawStroke(const TColorFunction *cf,
    TStrokeOutline *outline, const TStroke *stroke) const {
  TGLEntries::onViewerCanvas([&](TCanvas &canvas, bool lineSmooth) {
    drawStrokeOnCanvas(canvas, cf, outline, stroke, lineSmooth);
  });
}

//-----------------------------------------------------------------------------

void TLongBlendStrokeStyle2::drawStrokeOnCanvas(TCanvas &canvas, const TColorFunction *cf,
    TStrokeOutline *outline, const TStroke *stroke, bool antialias) const {
  TPixel32 color0, color1;
  if (cf) {
    color0 = (*(cf))(m_color0);
    color1 = (*(cf))(m_color1);
  } else {
    color0 = m_color0;
    color1 = m_color1;
  }

  UINT i;

  const std::vector<TOutlinePoint> &v = outline->getArray();
  if (v.empty()) return;

  double totallength = stroke->getLength();
  double ntick       = totallength / m_parameter + 1;

  // The colour goes from the first to the second along the stroke, a step
  // at every tick: on the two side lines and on the stroke itself.
  for (int side = 0; side < 2; side++) {
    std::vector<TPointD> points;
    std::vector<TCanvasColor> colors;
    int mystepCount = 0;
    TPixel32 color  = color0;
    for (i = side; i < v.size(); i += 2) {
      if (0 != v[i].stepCount) {
        color = blend(color0, color1, (double)mystepCount / ntick);
        mystepCount++;
      }
      points.push_back(TPointD(v[i].x, v[i].y));
      colors.push_back(toCanvas(color));
    }
    strokeColoredPolyline(canvas, points, colors, antialias);
  }

  int mystepCount   = 0;
  TPixel32 color    = color0;
  TCanvasMesh strip = plainMesh(TCanvasMesh::Mode::TriangleStrip);
  for (i = 0; i + 1 < v.size(); i += 2) {
    if (0 != v[i].stepCount) {
      color = blend(color0, color1, (double)mystepCount / ntick);
      mystepCount++;
    }
    addVertex(strip, TPointD(v[i].x, v[i].y), color);
    addVertex(strip, TPointD(v[i + 1].x, v[i + 1].y), color);
  }
  canvas.drawMesh(strip, plainPaint());
}

//-----------------------------------------------------------------------------

int TLongBlendStrokeStyle2::getParamCount() const { return 1; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TLongBlendStrokeStyle2::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TLongBlendStrokeStyle2::getParamNameText(int index) const {
  assert(0 <= index && index < 1);

  return TTranslatableText(
      "TLongBlendStrokeStyle2",
      QT_TRANSLATE_NOOP("TLongBlendStrokeStyle2",
                        "Distance"));  // W_Watercolor_Distance
}

//-----------------------------------------------------------------------------

void TLongBlendStrokeStyle2::getParamRange(int index, double &min,
                                           double &max) const {
  assert(0 <= index && index < 1);
  min = 0.1;
  max = 100.0;
}

//-----------------------------------------------------------------------------

double TLongBlendStrokeStyle2::getParamValue(TColorStyle::double_tag,
                                             int index) const {
  assert(0 <= index && index < 1);
  return m_parameter;
}

//-----------------------------------------------------------------------------

void TLongBlendStrokeStyle2::setParamValue(int index, double value) {
  assert(0 <= index && index < 1);
  m_parameter = value;
  updateVersionNumber();  // questo si chiama per ogno cambiamento di parametro
                          // per cui di deve ricalcolare l'outline
}

//=============================================================================

#ifdef _DEBUG

OutlineViewerStyle::OutlineViewerStyle(TPixel32 color, double parameter0,
                                       double parameter1, double parameter2,
                                       double parameter3)
    : TSolidColorStyle(color)
    , m_boolPar(false)
    , m_intPar(1)
    , m_enumPar(2)
    , m_pathPar("testPath") {
  m_parameter[0] = parameter0;
  m_parameter[1] = parameter1;
  m_parameter[2] = parameter2;
  m_parameter[3] = parameter3;
}

//-----------------------------------------------------------------------------

TColorStyle *OutlineViewerStyle::clone() const {
  return new OutlineViewerStyle(*this);
}

//-----------------------------------------------------------------------------

int OutlineViewerStyle::getParamCount() const { return 8; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType OutlineViewerStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  static const TColorStyle::ParamType types[8] = {
      TColorStyle::DOUBLE, TColorStyle::DOUBLE,  TColorStyle::DOUBLE,
      TColorStyle::DOUBLE, TColorStyle::BOOL,    TColorStyle::INT,
      TColorStyle::ENUM,   TColorStyle::FILEPATH};

  return types[index];
}

//-----------------------------------------------------------------------------

TTranslatableText OutlineViewerStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 8);

  switch (index) {
  case 0:
    return TTranslatableText(
        "OutlineViewerStyle",
        QT_TRANSLATE_NOOP("OutlineViewerStyle", "Control Point"));
  case 1:
    return TTranslatableText(
        "OutlineViewerStyle",
        QT_TRANSLATE_NOOP("OutlineViewerStyle", "Center Line"));
  case 2:
    return TTranslatableText(
        "OutlineViewerStyle",
        QT_TRANSLATE_NOOP("OutlineViewerStyle", "Outline Mode"));
  case 3:
    return TTranslatableText(
        "OutlineViewerStyle",
        QT_TRANSLATE_NOOP("OutlineViewerStyle", "Distance"));
  case 4:
    return TTranslatableText::untranslated("Bool");
  case 5:
    return TTranslatableText::untranslated("Int");
  case 6:
    return TTranslatableText::untranslated("Enum");
  case 7:
    return TTranslatableText::untranslated("Path");
  }

  assert(0);
  return TTranslatableText("OutlineViewerStyle",
                           QT_TRANSLATE_NOOP("OutlineViewerStyle", "distance"));
}

//-----------------------------------------------------------------------------

void OutlineViewerStyle::getParamRange(int index, double &min,
                                       double &max) const {
  assert(0 <= index && index < 4);
  switch (index) {
  case 0:
    min = 0.0;
    max = 2.99;
    break;
  case 1:
    min = 0.0;
    max = 1.99;
    break;
  case 2:
    min = 0.0;
    max = 3.99;
    break;
  case 3:
    min = 3.0;
    max = 100.0;
    break;
  }
}

//-----------------------------------------------------------------------------

void OutlineViewerStyle::getParamRange(int index, int &min, int &max) const {
  assert(5 <= index && index < 7);
  switch (index) {
  case 5:
    min = 0, max = 10;
    break;
  case 6:
    min = 0, max = 4;
    break;
  }
}

//-----------------------------------------------------------------------------

void OutlineViewerStyle::getParamRangeText(
    int index, std::vector<TTranslatableText>& enumItems) const {
  enumItems.push_back(TTranslatableText::untranslated("Prova 1"));
  enumItems.push_back(TTranslatableText::untranslated("Prova 2"));
  enumItems.push_back(TTranslatableText::untranslated("Prova 3"));
  enumItems.push_back(TTranslatableText::untranslated("Prova 4"));
}

//-----------------------------------------------------------------------------

bool OutlineViewerStyle::getParamValue(TColorStyle::bool_tag, int index) const {
  assert(index == 4);
  return m_boolPar;
}

//-----------------------------------------------------------------------------

int OutlineViewerStyle::getParamValue(TColorStyle::int_tag, int index) const {
  assert(5 <= index && index < 7);
  return (index == 5) ? m_intPar : m_enumPar;
}

//-----------------------------------------------------------------------------

double OutlineViewerStyle::getParamValue(TColorStyle::double_tag,
                                         int index) const {
  assert(0 <= index && index < 4);
  return m_parameter[index];
}

//-----------------------------------------------------------------------------

TFilePath OutlineViewerStyle::getParamValue(TColorStyle::TFilePath_tag,
                                            int index) const {
  assert(index == 7);
  return m_pathPar;
}

//-----------------------------------------------------------------------------

void OutlineViewerStyle::setParamValue(int index, bool value) {
  assert(index == 4);
  m_boolPar = value;
}

//-----------------------------------------------------------------------------

void OutlineViewerStyle::setParamValue(int index, int value) {
  assert(5 <= index && index < 7);
  (index == 5) ? m_intPar = value : m_enumPar = value;
}

//-----------------------------------------------------------------------------

void OutlineViewerStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 4);

  if (index >= 2 && (int)m_parameter[index] != (int)value)  // cambia l'outline
  {
    updateVersionNumber();
  }

  m_parameter[index] = value;
}

//-----------------------------------------------------------------------------

void OutlineViewerStyle::setParamValue(int index, const TFilePath &value) {
  assert(index == 7);
  m_pathPar = value;
}

//-----------------------------------------------------------------------------

void OutlineViewerStyle::loadData(TInputStreamInterface &is) {
  TPixel32 color;
  is >> color;
  TSolidColorStyle::setMainColor(color);
  is >> m_parameter[0];
  is >> m_parameter[1];
  is >> m_parameter[2];
  is >> m_parameter[3];

  int boolPar;
  is >> boolPar;
  m_boolPar = boolPar;
  is >> m_intPar;
  is >> m_enumPar;
  std::string str;
  is >> str;
  m_pathPar = TFilePath(::to_wstring(str));
}

//-----------------------------------------------------------------------------

void OutlineViewerStyle::saveData(TOutputStreamInterface &os) const {
  os << TSolidColorStyle::getMainColor();
  os << m_parameter[0];
  os << m_parameter[1];
  os << m_parameter[2];
  os << m_parameter[3];

  os << int(m_boolPar);
  os << m_intPar;
  os << m_enumPar;
  os << ::to_string(m_pathPar.getWideString());
}

//-----------------------------------------------------------------------------

void OutlineViewerStyle::computeOutline(
    const TStroke *stroke, TStrokeOutline &outline,
    TOutlineUtil::OutlineParameter param) const {
  if (m_parameter[2] >= 1.0) {
    param.m_lengthStep = (m_parameter[2] >= 3.0) ? m_parameter[3] : 0;

    TOutlineStyle::computeOutline(stroke, outline, param);
  }
}

//-----------------------------------------------------------------------------
namespace {

void drawOutline(TStrokeOutline *outline, bool cut) {
  const std::vector<TOutlinePoint> &v = outline->getArray();

  if (v.empty()) return;

  UINT i;
  // outline with antialiasing
  glBegin(GL_LINE_STRIP);
  for (i = 0; i < v.size(); i += 2) glVertex2dv(&v[i].x);
  glEnd();

  glBegin(GL_LINE_STRIP);
  for (i = 1; i < v.size(); i += 2) glVertex2dv(&v[i].x);
  glEnd();

  if (cut) {
    static const int stride = sizeof(TOutlinePoint);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_DOUBLE, stride, &v[0]);
    glDrawArrays(GL_LINES, 0, v.size());
    glDisableClientState(GL_VERTEX_ARRAY);
  }
}

void drawControlPoints(const TStroke *stroke, bool allPoints) {
  int i;
  TPointD p;
  glPointSize(4.0);
  glBegin(GL_POINTS);

  if (allPoints) {
    int n = stroke->getControlPointCount();
    for (i = 0; i < n; ++i) {
      p = stroke->getControlPoint(i);
      glColor3d((i + 1) & 1, i & 1, 0.0);
      glVertex2d(p.x, p.y);
    }
  } else {
    int n = stroke->getChunkCount();
    for (i = 0; i < n; ++i) {
      const TThickQuadratic *chunk = stroke->getChunk(i);
      p                            = chunk->getP0();
      glColor3d(1.0, 0.0, 0.0);
      glVertex2d(p.x, p.y);
    }
    const TThickQuadratic *chunk = stroke->getChunk(n - 1);
    glColor3d(1.0, 0.0, 0.0);
    p = chunk->getP2();
    glVertex2d(p.x, p.y);
  }

  glEnd();
}

void drawCenterline(const TStroke *stroke) {
  glBegin(GL_LINE_STRIP);
  int n = stroke->getChunkCount();
  int i = 0;
  for (i = 0; i < n; ++i) {
    const TThickQuadratic *chunk = stroke->getChunk(i);
    double length                = chunk->getLength(0, 1);
    int maxCount  = std::max(tceil(length / (5 * sqrt(tglGetPixelSize2()))), 1);
    double deltaT = 1.0 / maxCount;
    double t      = 0;
    for (t = 0; t < 1 + (deltaT / 2); t += deltaT) {
      TPointD point = chunk->getPoint(t);
      glVertex2d(point.x, point.y);
    }
  }
  glEnd();
  return;
}
}
//------------------------------------------------------------

void OutlineViewerStyle::drawStroke(const TColorFunction *cf,
                                    TStrokeOutline *outline,
                                    const TStroke *stroke) const {
  TPixel32 color;
  if (cf)
    color = (*(cf))(getMainColor());
  else
    color = getMainColor();

  tglColor(color);

  if (m_parameter[1] >= 1.0) drawCenterline(stroke);

  if (m_parameter[2] >= 1.0) drawOutline(outline, m_parameter[2] >= 2.0);

  if (m_parameter[0] >= 1.0) drawControlPoints(stroke, m_parameter[0] >= 2.0);
}

#endif

//=============================================================================

TMatrioskaStrokeProp::TMatrioskaStrokeProp(const TStroke *stroke,
                                           TMatrioskaStrokeStyle *style)
    : TStrokeProp(stroke)
    , m_colorStyle(style)
    , m_outline()
    , m_outlinePixelSize(0) {
  m_styleVersionNumber = m_colorStyle->getVersionNumber();
}

//-----------------------------------------------------------------------------

TStrokeProp *TMatrioskaStrokeProp::clone(const TStroke *stroke) const {
  TMatrioskaStrokeProp *prop = new TMatrioskaStrokeProp(stroke, m_colorStyle);
  prop->m_strokeChanged      = m_strokeChanged;
  prop->m_outline            = m_outline;
  prop->m_outlinePixelSize   = m_outlinePixelSize;
  return prop;
}

//-----------------------------------------------------------------------------

const TColorStyle *TMatrioskaStrokeProp::getColorStyle() const {
  return m_colorStyle;
}

//-----------------------------------------------------------------------------

namespace {
void recomputeStrokes(const TStroke *stroke, vector<TStroke *> &strokes,
                      int strokeNumber) {
  clearPointerContainer(strokes);

  strokes.resize(strokeNumber);

  int nCP = stroke->getControlPointCount();

  double reduction;

  for (int strokeId = 0; strokeId != strokeNumber; strokeId++) {
    strokes[strokeId] = new TStroke(*stroke);

    reduction = ((double)strokeId + 0.5) / (double)(strokeNumber + 0.5);

    for (int i = 0; i < nCP; i++) {
      TThickPoint tp = strokes[strokeId]->getControlPoint(i);
      tp.thick *= reduction;
      strokes[strokeId]->setControlPoint(i, tp);
    }
  }
}

// \p pixelSize 0 has the outlines read it from OpenGL, as they always did.
void recomputeOutlines(const TStroke *stroke, vector<TStroke *> &strokes,
                       vector<TStrokeOutline> &outlines,
                       const TSolidColorStyle *style, double pixelSize = 0) {
  TOutlineUtil::OutlineParameter param(0, pixelSize);
  int strokeNumber = strokes.size();
  outlines.resize(strokeNumber + 1);
  int strokeId;
  for (strokeId = 0; strokeId != strokeNumber; strokeId++) {
    outlines[strokeId].getArray().clear();
    style->computeOutline(strokes[strokeId], outlines[strokeId], param);
  }
  outlines[strokeId].getArray().clear();
  style->computeOutline(stroke, outlines[strokeId], param);
}
};

void TMatrioskaStrokeProp::draw(const TVectorRenderData &rd) {
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_stroke->getBBox()).overlaps(rd.m_clippingRect))
    return;

  int strokeId;
  TGLEntries::pushTransform(rd.m_aff);

  double pixelSize = sqrt(TGLEntries::pixelSize2());
  int strokeNumber =
      (int)(m_colorStyle->getParamValue(TColorStyle::double_tag(), 0)) - 1;

  if (m_strokeChanged || (UINT)strokeNumber != m_appStrokes.size()) {
    m_strokeChanged    = false;
    m_outlinePixelSize = pixelSize;
    recomputeStrokes(m_stroke, m_appStrokes, strokeNumber);
    recomputeOutlines(m_stroke, m_appStrokes, m_outline, m_colorStyle);
  } else if (!isAlmostZero(pixelSize - m_outlinePixelSize, 1e-5)
             //      || m_styleVersionNumber != m_colorStyle->getVersionNumber()
             ) {
    m_strokeChanged    = false;
    m_outlinePixelSize = pixelSize;
    recomputeOutlines(m_stroke, m_appStrokes, m_outline, m_colorStyle);
  }

  m_colorStyle->drawStroke(rd.m_cf, &m_outline[m_appStrokes.size()], m_stroke);

  TSolidColorStyle appStyle(m_colorStyle->getColorParamValue(1));

  //  if(m_colorStyle->isAlternate())
  //  {

  for (strokeId = strokeNumber - 1; strokeId >= 0; strokeId--) {
    if ((m_appStrokes.size() - strokeId) & 1)
      appStyle.drawStroke(rd.m_cf, &m_outline[strokeId],
                          m_appStrokes[strokeId]);
    else
      m_colorStyle->drawStroke(rd.m_cf, &m_outline[strokeId],
                               m_appStrokes[strokeId]);
  }

  //  }
  //  else
  //  {
  //    TPixel32 color0=m_colorStyle->getColorParamValue(0);
  //    TPixel32 color1=m_colorStyle->getColorParamValue(1);

  //    for(strokeId=strokeNumber-1; strokeId>=0;strokeId--)
  //    {
  //      double r2=(double)strokeId/(double)(strokeNumber);
  //      double r1=1-r2;
  //      TPixel32  color((int)(color0.r*r2+color1.r*r1),
  //                    (int)(color0.g*r2+color1.g*r1),
  //                    (int)(color0.b*r2+color1.b*r1),
  //                    (int)(color0.m*r2+color1.m*r1)
  //                    );

  //      appStyle.setMainColor(color);
  //      appStyle.drawStroke(rd.m_cf,
  //      &m_outline[strokeId],m_appStrokes[strokeId]);
  //    }

  //  }

  TGLEntries::popTransform();
}
//-----------------------------------------------------------------------------

// draw() without OpenGL. Keep the two in step.
void TMatrioskaStrokeProp::drawOnCanvas(TCanvas &canvas,
                                        const TVectorRenderData &rd) {
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_stroke->getBBox()).overlaps(rd.m_clippingRect))
    return;

  int strokeId;
  canvas.save();
  canvas.concat(toCanvas(rd.m_aff));

  double pixelSize = canvas.pixelSize();

  int strokeNumber =
      (int)(m_colorStyle->getParamValue(TColorStyle::double_tag(), 0)) - 1;

  if (m_strokeChanged || (UINT)strokeNumber != m_appStrokes.size()) {
    m_strokeChanged    = false;
    m_outlinePixelSize = pixelSize;
    recomputeStrokes(m_stroke, m_appStrokes, strokeNumber);
    recomputeOutlines(m_stroke, m_appStrokes, m_outline, m_colorStyle,
                      pixelSize);
  } else if (!isAlmostZero(pixelSize - m_outlinePixelSize, 1e-5)) {
    m_strokeChanged    = false;
    m_outlinePixelSize = pixelSize;
    recomputeOutlines(m_stroke, m_appStrokes, m_outline, m_colorStyle,
                      pixelSize);
  }

  // The stroke, and inside it thinner and thinner copies of it in the two
  // colours in turn.
  m_colorStyle->drawStrokeOnCanvas(canvas, rd.m_cf,
                                   &m_outline[m_appStrokes.size()], m_stroke,
                                   true);

  TSolidColorStyle appStyle(m_colorStyle->getColorParamValue(1));

  for (strokeId = strokeNumber - 1; strokeId >= 0; strokeId--) {
    if ((m_appStrokes.size() - strokeId) & 1)
      appStyle.drawStrokeOnCanvas(canvas, rd.m_cf, &m_outline[strokeId],
                                  m_appStrokes[strokeId], true);
    else
      m_colorStyle->drawStrokeOnCanvas(canvas, rd.m_cf, &m_outline[strokeId],
                                       m_appStrokes[strokeId], true);
  }

  canvas.restore();
}


//-----------------------------------------------------------------------------

TMatrioskaStrokeProp::~TMatrioskaStrokeProp() {
  clearPointerContainer(m_appStrokes);
}

//=============================================================================

TMatrioskaStrokeStyle::TMatrioskaStrokeStyle(TPixel32 color1, TPixel32 color2,
                                             double parameter, bool alternate)
    : TSolidColorStyle(color1), m_color2(color2), m_parameter(parameter) {}

//-----------------------------------------------------------------------------

TColorStyle *TMatrioskaStrokeStyle::clone() const {
  return new TMatrioskaStrokeStyle(*this);
}

//-----------------------------------------------------------------------------

TStrokeProp *TMatrioskaStrokeStyle::makeStrokeProp(const TStroke *stroke) {
  return new TMatrioskaStrokeProp(stroke, this);
}

//-----------------------------------------------------------------------------

int TMatrioskaStrokeStyle::getParamCount() const { return 1; }

//-----------------------------------------------------------------------------

TColorStyle::ParamType TMatrioskaStrokeStyle::getParamType(int index) const {
  assert(0 <= index && index < getParamCount());
  return TColorStyle::DOUBLE;
}

//-----------------------------------------------------------------------------

TTranslatableText TMatrioskaStrokeStyle::getParamNameText(int index) const {
  assert(0 <= index && index < 1);

  //  if(index)
  //    return TTranslatableText::untranslated("alter/gradual");
  //  else

  return TTranslatableText(
      "TMatrioskaStrokeStyle",
      QT_TRANSLATE_NOOP("TMatrioskaStrokeStyle", "Stripes"));
}

//-----------------------------------------------------------------------------

void TMatrioskaStrokeStyle::getParamRange(int index, double &min,
                                          double &max) const {
  assert(0 <= index && index < 1);

  //  if(index)
  //  {
  //    min = 0;
  //    max = 1;
  //  }
  //  else
  //  {

  min = 1.0;
  max = 10.0;
  //}
}

//-----------------------------------------------------------------------------

double TMatrioskaStrokeStyle::getParamValue(TColorStyle::double_tag,
                                            int index) const {
  assert(0 <= index && index < 1);

  //  if(index)
  //    return (m_alternate)? 0 : 1;
  //  else

  return m_parameter;
}

//-----------------------------------------------------------------------------

void TMatrioskaStrokeStyle::setParamValue(int index, double value) {
  assert(0 <= index && index < 1);

  //  if(index)
  //    m_alternate = value<0.5;
  //  else

  m_parameter = value;

  updateVersionNumber();
}

//-----------------------------------------------------------------------------

TPixel32 TMatrioskaStrokeStyle::getColorParamValue(int index) const {
  assert(0 <= index && index < 2);
  TPixel32 tmp;
  switch (index) {
  case (0):
    tmp = TSolidColorStyle::getMainColor();
    break;
  case (1):
    tmp = m_color2;
    break;
  }
  return tmp;
}

//-----------------------------------------------------------------------------

void TMatrioskaStrokeStyle::setColorParamValue(int index,
                                               const TPixel32 &color) {
  assert(0 <= index && index < 2);
  switch (index) {
  case (0):
    TSolidColorStyle::setMainColor(color);
    break;
  case (1):
    m_color2 = color;
    break;
  }
}

//-----------------------------------------------------------------------------
void TMatrioskaStrokeStyle::loadData(TInputStreamInterface &is) {
  TSolidColorStyle::loadData(is);
  is >> m_parameter;
  is >> m_color2;
}

//------------------------------------------------------------

void TMatrioskaStrokeStyle::saveData(TOutputStreamInterface &os) const {
  TSolidColorStyle::saveData(os);
  os << m_parameter;
  os << m_color2;
}
