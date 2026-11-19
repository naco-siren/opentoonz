

#include "tstroke.h"
#include "tvectorrenderdata.h"
#include "tglentries.h"
#include "tstrokeoutline.h"
#include "tsimplecolorstyles.h"
#include "tpalette.h"
#include "tregion.h"
#include "tregionoutline.h"
#include "tmathutil.h"
#include "tlevel_io.h"
#include "tenv.h"

#include "tvectorbrushstyle.h"
#include "tcanvasutil.h"

//**********************************************************************
//    Vector Brush Style  static members
//**********************************************************************

TFilePath TVectorBrushStyle::m_rootDir = TFilePath();

//**********************************************************************
//    Vector Brush Prop  declaration
//**********************************************************************

class VectorBrushProp final : public TStrokeProp {
  TVectorBrushStyle *m_style;
  TVectorImageP m_brush;
  TRectD m_brushBox;

  std::vector<TStrokeOutline> m_strokeOutlines;
  std::vector<TRegionOutline> m_regionOutlines;
  double m_pixelSize;

public:
  VectorBrushProp(const TStroke *stroke, TVectorBrushStyle *style);

  TStrokeProp *clone(const TStroke *stroke) const override;

  void draw(const TVectorRenderData &rd) override;
  void drawOnCanvas(TCanvas &canvas, const TVectorRenderData &rd) override;

  const TColorStyle *getColorStyle() const override;

private:
  void buildOutlines(double pixelSize);
  void drawOutlines(TCanvas *canvas);

  // not implemented
  VectorBrushProp(const VectorBrushProp &);
  VectorBrushProp &operator=(const VectorBrushProp &);
};

//**********************************************************************
//    Vector Brush Prop  implementation
//**********************************************************************

VectorBrushProp::VectorBrushProp(const TStroke *stroke,
                                 TVectorBrushStyle *style)
    : TStrokeProp(stroke)
    , m_style(style)
    , m_brush(style->getImage())
    , m_brushBox(m_brush->getBBox())
    , m_pixelSize(.0) {}

//-----------------------------------------------------------------

TStrokeProp *VectorBrushProp::clone(const TStroke *stroke) const {
  return new VectorBrushProp(stroke, m_style);
}

//-----------------------------------------------------------------

void VectorBrushProp::draw(const TVectorRenderData &rd) {
  // Ensure that the stroke overlaps our clipping rect
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_stroke->getBBox()).overlaps(rd.m_clippingRect))
    return;

  if (!m_brush->getPalette()) return;

  // Push the specified rd affine before drawing
  TGLEntries::pushTransform(rd.m_aff);

  buildOutlines(sqrt(TGLEntries::pixelSize2()));
  drawOutlines(0);

  TGLEntries::popTransform();
}

//-----------------------------------------------------------------

// draw() without OpenGL. Keep the two in step.
void VectorBrushProp::drawOnCanvas(TCanvas &canvas,
                                   const TVectorRenderData &rd) {
  if (rd.m_clippingRect != TRect() && !rd.m_is3dView &&
      !convert(rd.m_aff * m_stroke->getBBox()).overlaps(rd.m_clippingRect))
    return;

  if (!m_brush->getPalette()) return;

  canvas.save();
  canvas.concat(toCanvas(rd.m_aff));

  buildOutlines(canvas.pixelSize());
  drawOutlines(&canvas);

  canvas.restore();
}

//-----------------------------------------------------------------

// 1. If necessary, build the outlines: the brush image bent along the stroke.
void VectorBrushProp::buildOutlines(double currentPixelSize) {
  bool differentPixelSize = !isAlmostZero(currentPixelSize - m_pixelSize, 1e-5);
  m_pixelSize             = currentPixelSize;
  if (!differentPixelSize && !m_strokeChanged) return;
  m_strokeChanged = false;

  // The pixel size goes along: the outline builders used to read it from
  // OpenGL themselves, and on a canvas there may be none.
  const TOutlineUtil::OutlineParameter param(0, currentPixelSize);

  int i, viRegionsCount = m_brush->getRegionCount(),
         viStrokesCount = m_brush->getStrokeCount();

  // 1a. First, the regions
  m_regionOutlines.resize(viRegionsCount);

  for (i = 0; i < viRegionsCount; ++i) {
    TRegionOutline &outline    = m_regionOutlines[i];
    const TRegion *brushRegion = m_brush->getRegion(i);

    // Build the outline
    outline.clear();
    TOutlineUtil::makeOutline(*getStroke(), *brushRegion, m_brushBox, outline,
                              param);
  }

  // 1b. Then, the strokes
  m_strokeOutlines.resize(viStrokesCount);

  for (i = 0; i < viStrokesCount; ++i) {
    TStrokeOutline &outline    = m_strokeOutlines[i];
    const TStroke *brushStroke = m_brush->getStroke(i);

    outline.getArray().clear();
    TOutlineUtil::makeOutline(*getStroke(), *brushStroke, m_brushBox, outline,
                              param);
  }
}

//-----------------------------------------------------------------

// 2. Draw the outlines, each with a plain colour: on \p canvas, or with a
// null canvas through OpenGL as it is set up.
void VectorBrushProp::drawOutlines(TCanvas *canvas) {
  TPaletteP palette(m_brush->getPalette());

  // Build a solid color style to draw each m_vi's stroke with.
  TSolidColorStyle colorStyle;

  UINT s, t, r, strokesCount = m_brush->getStrokeCount(),
                regionCount = m_brush->getRegionCount();

  for (s = 0; s < strokesCount; s = t)  // Each cycle draws a group
  {
    // A vector image stores group strokes with consecutive indices.

    // 2a. First, draw regions in the strokeIdx-th stroke's group
    for (r = 0; r < regionCount; ++r) {
      if (m_brush->sameGroupStrokeAndRegion(s, r)) {
        const TRegion *brushRegion = m_brush->getRegion(r);
        const TColorStyle *brushStyle =
            palette->getStyle(brushRegion->getStyle());
        assert(brushStyle);

        // Draw the outline
        colorStyle.setMainColor(brushStyle->getMainColor());
        if (canvas)
          colorStyle.drawRegionOnCanvas(*canvas, 0, false, m_regionOutlines[r]);
        else
          colorStyle.drawRegion(0, false, m_regionOutlines[r]);
      }
    }

    // 2b. Then, draw all strokes in strokeIdx-th stroke's group
    for (t = s; t < strokesCount && m_brush->sameGroup(s, t); ++t) {
      const TStroke *brushStroke = m_brush->getStroke(t);
      const TColorStyle *brushStyle =
          palette->getStyle(brushStroke->getStyle());
      if (!brushStyle) continue;

      colorStyle.setMainColor(brushStyle->getMainColor());
      // brushStroke unused but requested
      if (canvas)
        colorStyle.drawStrokeOnCanvas(*canvas, 0, &m_strokeOutlines[t],
                                      brushStroke, true);
      else
        colorStyle.drawStroke(0, &m_strokeOutlines[t], brushStroke);
    }
  }
}

//-----------------------------------------------------------------

const TColorStyle *VectorBrushProp::getColorStyle() const { return m_style; }

//**********************************************************************
//    Vector Brush Style  implementation
//**********************************************************************

TVectorBrushStyle::TVectorBrushStyle() : m_colorCount(0) {}

//-----------------------------------------------------------------

TVectorBrushStyle::TVectorBrushStyle(const std::string &brushName,
                                     TVectorImageP vi)
    : m_brush(vi) {
  loadBrush(brushName);
}

//-----------------------------------------------------------------

TVectorBrushStyle::~TVectorBrushStyle() {}

//-----------------------------------------------------------------

void TVectorBrushStyle::loadBrush(const std::string &brushName) {
  m_brushName  = brushName;
  m_colorCount = 0;

  if (brushName.empty()) return;

  if (!m_brush) {
    // Load the image associated with fp
    TFilePath fp(m_rootDir + TFilePath(brushName + ".pli"));

    TLevelReaderP lr(fp);
    TLevelP level = lr->loadInfo();

    m_brush = lr->getFrameReader(level->begin()->first)->load();
    assert(m_brush);

    TPalette *palette = level->getPalette();
    m_brush->setPalette(palette);
  }

  assert(m_brush);
  m_colorCount =
      m_brush->getPalette()->getStyleInPagesCount() - 1;  // No transparent
}

//-----------------------------------------------------------------

TColorStyle *TVectorBrushStyle::clone() const {
  TVectorImageP brush;
  if (m_brush) {
    // Clone m_brush and its palette
    brush = m_brush->clone();  // NOTE: This does NOT clone the palette, too.
    brush->setPalette(m_brush->getPalette()->clone());
  }

  TVectorBrushStyle *theClone = new TVectorBrushStyle(m_brushName, brush);
  theClone->assignNames(this);
  theClone->setFlags(getFlags());

  return theClone;
}

//-----------------------------------------------------------------

TColorStyle *TVectorBrushStyle::clone(std::string brushIdName) const {
  std::string patternName     = getBrushIdNameParam(brushIdName);
  TVectorBrushStyle *theClone = new TVectorBrushStyle(patternName);
  theClone->assignNames(this);
  theClone->setFlags(getFlags());
  return theClone;
}

//-----------------------------------------------------------------

TTranslatableText TVectorBrushStyle::getDescriptionText() const {
  return TTranslatableText::untranslated("VectorBrushStyle");
}

//-----------------------------------------------------------------

std::string TVectorBrushStyle::getBrushIdName() const {
  return "VectorBrushStyle:" + m_brushName;
}

//-----------------------------------------------------------------------------

TStrokeProp *TVectorBrushStyle::makeStrokeProp(const TStroke *stroke) {
  return new VectorBrushProp(stroke, this);
}

//-----------------------------------------------------------------

void TVectorBrushStyle::saveData(TOutputStreamInterface &osi) const {
  osi << m_brushName;

  // Save palette
  osi << m_colorCount;

  TPalette *pal = m_brush->getPalette();
  assert(pal);

  int p, pagesCount = pal->getPageCount();
  for (p = 0; p < pagesCount; ++p) {
    TPalette::Page *page = pal->getPage(p);

    int s, stylesCount = page->getStyleCount();
    for (s = 0; s < stylesCount; ++s) osi << page->getStyle(s)->getMainColor();
  }
}

//-----------------------------------------------------------------

void TVectorBrushStyle::loadData(TInputStreamInterface &isi) {
  std::string str;
  isi >> str;

  assert(!str.empty());
  loadBrush(str);

  int colorCount;
  isi >> colorCount;

  if (colorCount != m_colorCount)
    return;  // Palette mismatch! Just skip palette loading...

  // WARNING: Is it needed to read all colors nonetheless?

  // Load palette colors
  TPalette *pal = m_brush->getPalette();
  assert(pal);

  TPixel32 color;

  int p, pagesCount = pal->getPageCount();
  for (p = 0; p < pagesCount; ++p) {
    TPalette::Page *page = pal->getPage(p);

    int s, stylesCount = page->getStyleCount();
    for (s = 0; s < stylesCount; ++s) {
      isi >> color;
      page->getStyle(s)->setMainColor(color);
    }
  }
}

//-----------------------------------------------------------------

int TVectorBrushStyle::getColorStyleId(int index) const {
  if (index < 0) return 1;

  ++index;  // Skipping transparent

  TPalette *pal = m_brush->getPalette();
  assert(pal);

  int p, pagesCount = pal->getPageCount();
  for (p = 0; p < pagesCount; ++p) {
    TPalette::Page *page = pal->getPage(p);

    int stylesCount = page->getStyleCount();
    if (index - stylesCount < 0) break;

    index -= stylesCount;
  }

  if (p >= pagesCount) return -1;

  TPalette::Page *page = pal->getPage(p);
  return page->getStyleId(index);
}

//-----------------------------------------------------------------

TPixel32 TVectorBrushStyle::getMainColor() const {
  if (!m_brush) return TPixel32::Transparent;

  TPalette *pal = m_brush->getPalette();
  return pal->getStyle(1)->getMainColor();
}

//-----------------------------------------------------------------

void TVectorBrushStyle::setMainColor(const TPixel32 &color) {
  if (!m_brush) return;

  TPalette *pal = m_brush->getPalette();
  pal->getStyle(1)->setMainColor(color);
}

//-----------------------------------------------------------------

int TVectorBrushStyle::getColorParamCount() const { return m_colorCount; }

//-----------------------------------------------------------------

TPixel32 TVectorBrushStyle::getColorParamValue(int index) const {
  TPalette *pal = m_brush->getPalette();
  assert(pal);

  int styleId = getColorStyleId(index);
  if (styleId < 0) styleId = 1;

  return pal->getStyle(styleId)->getMainColor();
}

//-----------------------------------------------------------------

void TVectorBrushStyle::setColorParamValue(int index, const TPixel32 &color) {
  TPalette *pal = m_brush->getPalette();
  assert(pal);

  int styleId = getColorStyleId(index);
  if (styleId < 0) styleId = 1;

  return pal->getStyle(styleId)->setMainColor(color);
}

//**************************************************************
//    Vector Style  global declaration instance
//**************************************************************

namespace {
TColorStyle::Declaration declaration(new TVectorBrushStyle());
}
