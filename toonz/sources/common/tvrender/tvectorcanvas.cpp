// What tvectorcanvas.h declares: a vector image, stroke or region drawn on a
// canvas. Split from tglregions.cpp, whose OpenGL drawing went to tnzglview
// with step 8c of doc/nexttoonz_core_noqt.md.

#include "tvectorcanvas.h"

#include "canvas/tcanvas.h"
#include "tcolorfunctions.h"
#include "tpalette.h"
#include "tregion.h"
#include "tregionprop.h"
#include "tsimplecolorstyles.h"
#include "tstroke.h"
#include "tstrokeprop.h"
#include "tthreadmessage.h"
#include "tvectorimage.h"
#include "tvectorrenderdata.h"

//=============================================================================
// The traversal of tglregions.cpp on a canvas (tvectorcanvas.h).
//
// The functions below mirror tglDraw() and its helpers in tglregions.cpp
// (in tnzglview since step 8c of doc/nexttoonz_core_noqt.md) with the
// OpenGL taken out: no
// attributes to save, no two passes for the alpha channel (a canvas blends
// into a premultiplied target by itself), and the prop is asked to draw on
// the canvas. Which style draws what, and when nothing is drawn, has to
// stay the same in both until the OpenGL ones are gone.

namespace {

// As in tglregions.cpp.
bool isOThick(const TStroke *s) {
  int i;
  for (i = 0; i < s->getControlPointCount(); i++)
    if (s->getControlPoint(i).thick != 0) return false;
  return true;
}


// The style a region is drawn with: its own, or the plain colour of a check
// mode.
TColorStyleP regionStyleFor(const TVectorRenderData &rd, const TRegion *r) {
  if (rd.m_paintCheckEnabled && r->getStyle() == rd.m_paintIndex) {
    TSolidColorStyle *colorStyle = new TSolidColorStyle();
    colorStyle->setMainColor(rd.m_paintCheckColor);
    return colorStyle;
  }
  if (rd.m_tcheckEnabled) {
    TSolidColorStyle *colorStyle = new TSolidColorStyle();
    colorStyle->setMainColor(rd.m_tCheckPaint);
    return colorStyle;
  }
  return rd.m_palette->getStyle(r->getStyle());
}

TColorStyleP strokeStyleFor(const TVectorRenderData &rd, const TStroke *s) {
  TPixel32 checkColor;
  if (rd.m_inkCheckEnabled && s->getStyle() == rd.m_colorCheckIndex)
    checkColor = rd.m_inkCheckColor;
  else if (rd.m_ink1CheckEnabled && s->getStyle() == 1)
    checkColor = rd.m_ink1CheckColor;
  else if (rd.m_tcheckEnabled)
    checkColor = rd.m_tCheckInk;
  else
    return rd.m_palette->getStyle(s->getStyle());

  TSolidColorStyle *colorStyle = new TSolidColorStyle();
  colorStyle->setMainColor(checkColor);
  return colorStyle;
}

// A style none of whose colours shows is not drawn at all.
bool showsAnything(const TColorStyleP &style, const TVectorRenderData &rd) {
  const int colorCount = style->getColorParamCount();
  if (colorCount == 0) return true;  // for example texture
  for (int j = 0; j < colorCount; j++) {
    TPixel32 color = style->getColorParamValue(j);
    if (rd.m_cf) color = (*(rd.m_cf))(color);
    if (color.m != 0) return true;
  }
  return false;
}

bool skipsThinStroke(const TVectorRenderData &rd, const TStroke *s,
                     const TColorStyleP &style) {
  return !rd.m_show0ThickStrokes && isOThick(s) &&
         dynamic_cast<TSolidColorStyle *>(style.getPointer()) &&
         !rd.m_tcheckEnabled;
}

//-----------------------------------------------------------------------------

// tglDraw(rd, region)
void drawRegionOnCanvas(TCanvas &canvas, const TVectorRenderData &rd,
                        TRegion *r) {
  if (!r) return;

  const TColorStyleP style = regionStyleFor(rd, r);
  if (showsAnything(style, rd)) {
    TRegionProp *prop = r->getProp();
    if (r->getStyle()) {
      if (!style->isRegionStyle() || style->isEnabled() == false) {
        prop = 0;
      } else if (!prop || style.getPointer() != prop->getColorStyle()) {
        r->setProp(style->makeRegionProp(r));
        prop = r->getProp();
      }
    }
    if (prop) prop->drawOnCanvas(canvas, rd);
  }

  for (UINT i = 0; i < r->getSubregionCount(); i++)
    drawRegionOnCanvas(canvas, rd, r->getSubregion(i));
}

// tglDoDraw(rd, region)
void doDrawRegionOnCanvas(TCanvas &canvas, const TVectorRenderData &rd,
                          TRegion *r) {
  if (!r) return;
  if (showsAnything(rd.m_palette->getStyle(r->getStyle()), rd))
    drawRegionOnCanvas(canvas, rd, r);
  else
    for (UINT j = 0; j < r->getSubregionCount(); j++)
      drawRegionOnCanvas(canvas, rd, r->getSubregion(j));
}

// tglDraw(rd, stroke)
void drawStrokeOnCanvas(TCanvas &canvas, const TVectorRenderData &rd,
                        const TStroke *s) {
  TStrokeProp *prop = 0;
  try {
    TStroke *stroke          = const_cast<TStroke *>(s);
    const TColorStyleP style = strokeStyleFor(rd, s);
    if (skipsThinStroke(rd, s, style)) return;

    prop = s->getProp();
    if (prop) prop->getMutex()->lock();

    if (!style->isStrokeStyle() || style->isEnabled() == false) {
      if (prop) prop->getMutex()->unlock();
      prop = 0;
    } else if (!prop || style.getPointer() != prop->getColorStyle()) {
      if (prop) prop->getMutex()->unlock();
      stroke->setProp(style->makeStrokeProp(stroke));
      prop = stroke->getProp();
      if (prop) prop->getMutex()->lock();
    }
    if (!prop) return;

    prop->drawOnCanvas(canvas, rd);
    prop->getMutex()->unlock();
  } catch (...) {
    if (prop) prop->getMutex()->unlock();
  }
}

// doDraw(vim, rd, drawEnteredGroup)
void doDrawOnCanvas(TCanvas &canvas, const TVectorImage *vim,
                    const TVectorRenderData &_rd, bool drawEnteredGroup) {
  static TOnionFader *fade = new TOnionFader(TPixel::White, 0.5);

  TVectorRenderData rd(_rd);
  if (!drawEnteredGroup && !rd.m_isIcon && vim->isInsideGroup() > 0)
    rd.m_cf = fade;

  UINT strokeIndex = 0;
  while (strokeIndex < vim->getStrokeCount())  // each while loop draws a group
  {
    int currStrokeIndex = strokeIndex;
    if (!rd.m_isIcon && vim->isInsideGroup() > 0 &&
        ((drawEnteredGroup && !vim->isEnteredGroupStroke(strokeIndex)) ||
         (!drawEnteredGroup && vim->isEnteredGroupStroke(strokeIndex)))) {
      while (strokeIndex < vim->getStrokeCount() &&
             vim->sameGroup(strokeIndex, currStrokeIndex))
        strokeIndex++;
      continue;
    }

    if (rd.m_drawRegions)
      for (UINT regionIndex = 0; regionIndex < vim->getRegionCount();
           regionIndex++)
        if (vim->sameGroupStrokeAndRegion(currStrokeIndex, regionIndex))
          doDrawRegionOnCanvas(canvas, rd, vim->getRegion(regionIndex));
    while (strokeIndex < vim->getStrokeCount() &&
           vim->sameGroup(strokeIndex, currStrokeIndex)) {
      const TStroke *s = vim->getStroke(strokeIndex);
      if (showsAnything(rd.m_palette->getStyle(s->getStyle()), rd))
        drawStrokeOnCanvas(canvas, rd, s);
      strokeIndex++;
    }
  }
}

//-----------------------------------------------------------------------------

bool regionCanBeDrawnOnCanvas(const TVectorRenderData &rd, TRegion *r) {
  // A check mode draws with a plain colour, whatever the region's style.
  const bool checked = (rd.m_paintCheckEnabled &&
                        r->getStyle() == rd.m_paintIndex) ||
                       rd.m_tcheckEnabled;
  if (!checked) {
    // What draws a region is its style or, for style 0, whatever prop it
    // was last given.
    const TColorStyle *style = 0;
    if (r->getStyle())
      style = rd.m_palette->getStyle(r->getStyle());
    else if (TRegionProp *prop = r->getProp())
      style = prop->getColorStyle();
    if (style && style->isRegionStyle() && style->isEnabled() &&
        !(style->canvasSupport() & TColorStyle::RegionsOnCanvas))
      return false;
  }
  for (UINT i = 0; i < r->getSubregionCount(); i++)
    if (!regionCanBeDrawnOnCanvas(rd, r->getSubregion(i))) return false;
  return true;
}

}  // namespace

//-----------------------------------------------------------------------------

namespace {

// What every canvas drawing needs before it can start: no 3D view or
// guided drawing, and a palette.
bool canvasSettingsAllow(const TVectorRenderData &rd) {
  return !rd.m_is3dView && !rd.m_showGuidedDrawing;
}

bool strokeCanBeDrawnOnCanvas(const TVectorRenderData &rd, const TStroke *s) {
  const bool checked =
      (rd.m_inkCheckEnabled && s->getStyle() == rd.m_colorCheckIndex) ||
      (rd.m_ink1CheckEnabled && s->getStyle() == 1) || rd.m_tcheckEnabled;
  if (checked) return true;
  const TColorStyle *style = rd.m_palette->getStyle(s->getStyle());
  return !(style->isStrokeStyle() && style->isEnabled() &&
           !(style->canvasSupport() & TColorStyle::StrokesOnCanvas));
}

}  // namespace

//-----------------------------------------------------------------------------

bool tcanvasCanDraw(const TVectorRenderData &_rd, const TVectorImage *vim) {
  if (!vim) return true;
  if (!canvasSettingsAllow(_rd)) return false;

  TThread::MutexLocker sl(vim->getMutex());

  TVectorRenderData rd(_rd);
  if (!rd.m_palette) rd.m_palette = vim->getPalette();
  if (!rd.m_palette) return true;  // nothing is drawn without one

  for (UINT i = 0; i < vim->getStrokeCount(); i++)
    if (!strokeCanBeDrawnOnCanvas(rd, vim->getStroke(i))) return false;

  if (rd.m_drawRegions)
    for (UINT i = 0; i < vim->getRegionCount(); i++)
      if (!regionCanBeDrawnOnCanvas(rd, vim->getRegion(i))) return false;

  return true;
}

//-----------------------------------------------------------------------------

void tcanvasDraw(TCanvas &canvas, const TVectorRenderData &_rd,
                 const TVectorImage *vim) {
  assert(vim);
  if (!vim) return;

  TThread::MutexLocker sl(vim->getMutex());

  TVectorRenderData rd(_rd);
  if (!rd.m_palette) rd.m_palette = vim->getPalette();
  if (!rd.m_palette) return;

  doDrawOnCanvas(canvas, vim, rd, false);
  if (!rd.m_isIcon && vim->isInsideGroup() > 0)
    doDrawOnCanvas(canvas, vim, rd, true);
}

//-----------------------------------------------------------------------------

bool tcanvasCanDraw(const TVectorRenderData &rd, const TStroke *stroke) {
  if (!stroke) return true;
  if (!canvasSettingsAllow(rd)) return false;
  if (!rd.m_palette) return false;  // tglDraw() would not draw it either
  return strokeCanBeDrawnOnCanvas(rd, stroke);
}

void tcanvasDraw(TCanvas &canvas, const TVectorRenderData &rd,
                 const TStroke *stroke) {
  assert(stroke && rd.m_palette);
  if (!stroke || !rd.m_palette) return;
  drawStrokeOnCanvas(canvas, rd, stroke);
}

bool tcanvasCanDraw(const TVectorRenderData &rd, TRegion *region) {
  if (!region) return true;
  if (!canvasSettingsAllow(rd)) return false;
  if (!rd.m_palette) return false;
  return regionCanBeDrawnOnCanvas(rd, region);
}

void tcanvasDraw(TCanvas &canvas, const TVectorRenderData &rd,
                 TRegion *region) {
  assert(region && rd.m_palette);
  if (!region || !rd.m_palette) return;
  drawRegionOnCanvas(canvas, rd, region);
}
