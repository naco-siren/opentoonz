

//#include "tcolorstyles.h"
#include "tsimplecolorstyles.h"
//#include "tstrokeoutline.h"
#include "tstrokeprop.h"
#include "tgl.h"
//#include "tcolorfunctions.h"
#include "tvectorrenderdata.h"
#include "tmathutil.h"
#include "tcanvasutil.h"
//#include "tcurves.h"
//#include "tstrokeutil.h"

//#include "tstroke.h"

//=============================================================================

TSimpleStrokeProp::TSimpleStrokeProp(const TStroke *stroke,
                                     TSimpleStrokeStyle *style)
    : TStrokeProp(stroke)
    , m_colorStyle(style)

{
  m_styleVersionNumber = m_colorStyle->getVersionNumber();
  m_colorStyle->addRef();
}

//-----------------------------------------------------------------------------

TSimpleStrokeProp::~TSimpleStrokeProp() { m_colorStyle->release(); }

//-----------------------------------------------------------------------------

const TColorStyle *TSimpleStrokeProp::getColorStyle() const {
  return m_colorStyle;
}

//-----------------------------------------------------------------------------

TStrokeProp *TSimpleStrokeProp::clone(const TStroke *stroke) const {
  TSimpleStrokeProp *prop = new TSimpleStrokeProp(stroke, m_colorStyle);
  prop->m_strokeChanged   = m_strokeChanged;
  return prop;
}

//-----------------------------------------------------------------------------

void TSimpleStrokeProp::draw(
    const TVectorRenderData
        &rd) /*assenza di const non e' una dimenticanza! Alcune sottoclassi
                devono ridefinire questo metodo e serve che non sia const*/
{
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_stroke->getBBox()).overlaps(rd.m_clippingRect))
    return;

  if (!rd.m_show0ThickStrokes) {
    // >:(  This is not an implementation detail of TCenterlineStrokeStyle
    // because the drawStroke()
    //      function does not have access to rd - should modify the interface...
    //      it would be best.

    const TCenterLineStrokeStyle *cs =
        dynamic_cast<const TCenterLineStrokeStyle *>(m_colorStyle);
    if (cs && cs->getParamValue(TColorStyle::double_tag(), 0) == 0) return;
  }

  glPushMatrix();
  tglMultMatrix(rd.m_aff);
  m_colorStyle->drawStroke(rd.m_cf, m_stroke);

  glPopMatrix();
}

//-----------------------------------------------------------------------------

// draw() without OpenGL. Keep the two in step.
void TSimpleStrokeProp::drawOnCanvas(TCanvas &canvas,
                                     const TVectorRenderData &rd) {
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_stroke->getBBox()).overlaps(rd.m_clippingRect))
    return;

  if (!rd.m_show0ThickStrokes) {
    const TCenterLineStrokeStyle *cs =
        dynamic_cast<const TCenterLineStrokeStyle *>(m_colorStyle);
    if (cs && cs->getParamValue(TColorStyle::double_tag(), 0) == 0) return;
  }

  canvas.save();
  canvas.concat(toCanvas(rd.m_aff));
  m_colorStyle->drawStrokeOnCanvas(canvas, rd.m_cf, m_stroke,
                                   rd.m_antiAliasing);
  canvas.restore();
}

//=============================================================================

TRasterImagePatternStrokeProp::TRasterImagePatternStrokeProp(
    const TStroke *stroke, TRasterImagePatternStrokeStyle *style)
    : TStrokeProp(stroke), m_colorStyle(style) {
  m_styleVersionNumber = style->getVersionNumber();
  m_colorStyle->addRef();
}

//-----------------------------------------------------------------------------

TRasterImagePatternStrokeProp::~TRasterImagePatternStrokeProp() {
  m_colorStyle->release();
}

//-----------------------------------------------------------------------------

const TColorStyle *TRasterImagePatternStrokeProp::getColorStyle() const {
  return m_colorStyle;
}

//-----------------------------------------------------------------------------

TStrokeProp *TRasterImagePatternStrokeProp::clone(const TStroke *stroke) const {
  TRasterImagePatternStrokeProp *prop =
      new TRasterImagePatternStrokeProp(stroke, m_colorStyle);
  prop->m_strokeChanged   = m_strokeChanged;
  prop->m_transformations = m_transformations;
  return prop;
}

//-----------------------------------------------------------------------------

void TRasterImagePatternStrokeProp::draw(
    const TVectorRenderData &rd) /*assenza di const non e' una
                                    dimenticanza! Alcune
                                    sottoclassi devono
                                    ridefinire questo metodo e
                                    serbve che non sia const*/
{
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_colorStyle->getStrokeBBox(m_stroke))
           .overlaps(rd.m_clippingRect))
    return;

  if (m_strokeChanged ||
      m_styleVersionNumber != m_colorStyle->getVersionNumber()) {
    m_strokeChanged      = false;
    m_styleVersionNumber = m_colorStyle->getVersionNumber();
    m_colorStyle->computeTransformations(m_transformations, m_stroke);
  }
  m_colorStyle->drawStroke(rd, m_transformations, m_stroke);
}

//-----------------------------------------------------------------------------
//=============================================================================

TVectorImagePatternStrokeProp::TVectorImagePatternStrokeProp(
    const TStroke *stroke, TVectorImagePatternStrokeStyle *style)
    : TStrokeProp(stroke), m_colorStyle(style) {
  m_styleVersionNumber = style->getVersionNumber();
  m_colorStyle->addRef();
}

//-----------------------------------------------------------------------------

TVectorImagePatternStrokeProp::~TVectorImagePatternStrokeProp() {
  m_colorStyle->release();
}

//-----------------------------------------------------------------------------

const TColorStyle *TVectorImagePatternStrokeProp::getColorStyle() const {
  return m_colorStyle;
}

//-----------------------------------------------------------------------------

TStrokeProp *TVectorImagePatternStrokeProp::clone(const TStroke *stroke) const {
  TVectorImagePatternStrokeProp *prop =
      new TVectorImagePatternStrokeProp(stroke, m_colorStyle);
  prop->m_strokeChanged   = m_strokeChanged;
  prop->m_transformations = m_transformations;
  return prop;
}

//-----------------------------------------------------------------------------

void TVectorImagePatternStrokeProp::draw(
    const TVectorRenderData &rd) /*assenza di const non e' una
                                    dimenticanza! Alcune
                                    sottoclassi devono
                                    ridefinire questo metodo e
                                    serbve che non sia const*/
{
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_colorStyle->getStrokeBBox(m_stroke))
           .overlaps(rd.m_clippingRect))
    return;

  if (m_strokeChanged ||
      m_styleVersionNumber != m_colorStyle->getVersionNumber()) {
    m_strokeChanged      = false;
    m_styleVersionNumber = m_colorStyle->getVersionNumber();
    m_colorStyle->computeTransformations(m_transformations, m_stroke);
  }
  m_colorStyle->drawStroke(rd, m_transformations, m_stroke);
}

//=============================================================================

OutlineStrokeProp::OutlineStrokeProp(const TStroke *stroke,
                                     const TOutlineStyleP style)
    : TStrokeProp(stroke)
    , m_colorStyle(style)
    , m_outline()
    , m_outlinePixelSize(0) {
  m_styleVersionNumber = m_colorStyle->getVersionNumber();
}

//-----------------------------------------------------------------------------

TStrokeProp *OutlineStrokeProp::clone(const TStroke *stroke) const {
  OutlineStrokeProp *prop  = new OutlineStrokeProp(stroke, m_colorStyle);
  prop->m_strokeChanged    = m_strokeChanged;
  prop->m_outline          = m_outline;
  prop->m_outlinePixelSize = m_outlinePixelSize;
  return prop;
}

//-----------------------------------------------------------------------------

const TColorStyle *OutlineStrokeProp::getColorStyle() const {
  return m_colorStyle.getPointer();
}

//-----------------------------------------------------------------------------

void OutlineStrokeProp::draw(const TVectorRenderData &rd) {
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_stroke->getBBox()).overlaps(rd.m_clippingRect))
    return;

  glPushMatrix();
  tglMultMatrix(rd.m_aff);

  double pixelSize = sqrt(tglGetPixelSize2());

#ifdef _DEBUG
  if (m_stroke->isCenterLine() && m_colorStyle->getTagId() != 99)
#else
  if (m_stroke->isCenterLine())
#endif
  {
    TCenterLineStrokeStyle *appStyle =
        new TCenterLineStrokeStyle(m_colorStyle->getAverageColor(), 0, 0);
    appStyle->drawStroke(rd.m_cf, m_stroke);
    delete appStyle;
  } else {
    updateOutline(pixelSize);

    if (rd.m_antiAliasing)
      m_colorStyle->drawStroke(rd.m_cf, &m_outline, m_stroke);
    else
      m_colorStyle->drawAliasedStroke(rd.m_cf, &m_outline, m_stroke);
  }

  glPopMatrix();
}

//-----------------------------------------------------------------------------

void OutlineStrokeProp::updateOutline(double pixelSize) {
  if (!isAlmostZero(pixelSize - m_outlinePixelSize, 1e-5) || m_strokeChanged ||
      m_styleVersionNumber != m_colorStyle->getVersionNumber()) {
    m_strokeChanged    = false;
    m_outlinePixelSize = pixelSize;
    // The pixel size goes along: computing the outline used to read it
    // from OpenGL once more, and on a canvas there may be none.
    TOutlineUtil::OutlineParameter param(0, pixelSize);

    m_outline.getArray().clear();
    m_colorStyle->computeOutline(m_stroke, m_outline, param);

    // TOutlineStyle::StrokeOutlineModifier *modifier =
    // m_colorStyle->getStrokeOutlineModifier();
    // if(modifier)
    //  modifier->modify(m_outline);

    m_styleVersionNumber = m_colorStyle->getVersionNumber();
  }
}

//-----------------------------------------------------------------------------

// draw() without OpenGL. Keep the two in step.
void OutlineStrokeProp::drawOnCanvas(TCanvas &canvas,
                                     const TVectorRenderData &rd) {
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_stroke->getBBox()).overlaps(rd.m_clippingRect))
    return;

  canvas.save();
  canvas.concat(toCanvas(rd.m_aff));

  const double pixelSize = canvas.pixelSize();

#ifdef _DEBUG
  if (m_stroke->isCenterLine() && m_colorStyle->getTagId() != 99)
#else
  if (m_stroke->isCenterLine())
#endif
  {
    TCenterLineStrokeStyle *appStyle =
        new TCenterLineStrokeStyle(m_colorStyle->getAverageColor(), 0, 0);
    appStyle->drawStrokeOnCanvas(canvas, rd.m_cf, m_stroke, rd.m_antiAliasing);
    delete appStyle;
  } else {
    updateOutline(pixelSize);
    m_colorStyle->drawStrokeOnCanvas(canvas, rd.m_cf, &m_outline, m_stroke,
                                     rd.m_antiAliasing);
  }

  canvas.restore();
}

//=============================================================================
