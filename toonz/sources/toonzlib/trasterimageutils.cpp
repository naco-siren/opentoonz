

#include "toonz/trasterimageutils.h"
#include "toonz/ttileset.h"
#include "tstroke.h"
#include "tofflinegl.h"
#include "tpalette.h"
#include "tvectorrenderdata.h"
#include "ttzpimagefx.h"
#include "tvectorgl.h"
#include "tvectorrasterizer.h"
#include "trop.h"
#include "tregion.h"
#include "tcolorstyles.h"

#include <QPainter>
#include <QColor>
#include <QImage>

//--------------------------------------------------------------------------

namespace {
QImage rasterToQImage(const TRasterP &ras, bool premultiplied, bool mirrored) {
  if (TRaster32P ras32 = ras) {
    QImage image(ras->getRawData(), ras->getLx(), ras->getLy(),
                 premultiplied ? QImage::Format_ARGB32_Premultiplied
                               : QImage::Format_ARGB32);
    if (mirrored) return image.mirrored();
    return image;
  } else if (TRasterGR8P ras8 = ras) {
    QImage image(ras->getRawData(), ras->getLx(), ras->getLy(), ras->getWrap(),
                 QImage::Format_Indexed8);
    static QVector<QRgb> colorTable;
    if (colorTable.size() == 0) {
      int i;
      for (i = 0; i < 256; i++) colorTable.append(QColor(i, i, i).rgb());
    }
    image.setColorTable(colorTable);
    if (mirrored) return image.mirrored();
    return image;
  }
  return QImage();
}

//--------------------------------------------------------------------------

// The whole stroke in its own colours over transparent pixels, its bounding
// box enlarged by a pixel. On the CPU when renders are to be done there and
// the style can be drawn that way (TVectorRasterizer); otherwise, as before,
// through an offscreen OpenGL context.
TRaster32P rasterizeWholeStroke(TStroke *stroke, TPalette *palette,
                                bool doAnialias) {
  TRectD bbox = stroke->getBBox();
  TRect rect  = convert(bbox).enlarge(1);

  if (TVectorRasterizer::isSelected()) {
    TPaletteP plt = palette->clone();
    TTranslation affine(-convert(rect.getP00()));
    TVectorRenderData rd(affine, TRect(rect.getSize()), plt.getPointer(), 0,
                         true, doAnialias);
    TRaster32P ras(rect.getSize());
    ras->clear();
    if (TVectorRasterizer::draw(ras, stroke, rd)) return ras;
  }

  TOfflineGL *gl = new TOfflineGL(rect.getSize());
  gl->makeCurrent();
  gl->clear(TPixel32(0, 0, 0, 0));
  glEnable(GL_ALPHA_TEST);
  glAlphaFunc(GL_GREATER, 0);

  TPaletteP plt      = palette->clone();
  int styleId        = stroke->getStyle();
  TColorStyleP style = plt->getStyle(styleId);
  TTranslation affine(-convert(rect.getP00()));
  TVectorRenderData rd(affine, gl->getBounds(), plt.getPointer(), 0, true,
                       true);
  if (doAnialias)
    tglDraw(rd, stroke);
  else {
    TStrokeProp *prop = stroke->getProp();
    if (prop) prop->getMutex()->lock();

    if (!style->isStrokeStyle() || style->isEnabled() == false) {
      if (prop) prop->getMutex()->unlock();

      prop = 0;
    } else {
      if (!prop || style.getPointer() != prop->getColorStyle()) {
        if (prop) prop->getMutex()->unlock();

        stroke->setProp(style->makeStrokeProp(stroke));
        prop = stroke->getProp();
        if (prop) prop->getMutex()->lock();
      }
    }

    if (!prop) {
      // The old code returned here leaving the context to its caller,
      // which laid its pixels, all transparent, over the image: nothing.
      delete gl;
      return TRaster32P();
    }
    prop->getMutex()->lock();
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    prop->draw(rd);
    glPopAttrib();
  }
  glDisable(GL_ALPHA_TEST);
  glFinish();
  TRaster32P ras = gl->getRaster();
  gl->doneCurrent();
  delete gl;
  return ras;
}

//--------------------------------------------------------------------------

TRect fastAddInkStroke(const TRasterImageP &ri, TStroke *stroke, TRectD clip,
                       double opacity, bool doAntialiasing) {
  TRaster32P strokeRas;
  TRectD bbox      = stroke->getBBox();
  TRect sBBox      = convert(bbox).enlarge(1);
  TRect rectRender = sBBox * ri->getRaster()->getBounds();

  if (!rectRender.isEmpty()) {
    if (opacity < 1.0) {
      int styleId    = stroke->getStyle();
      TPalette *plt  = ri->getPalette();
      TPixel32 color = plt->getStyle(styleId)->getMainColor();
      color.m        = 255 * opacity;
      TPaletteP newPlt(plt);
      newPlt->getStyle(styleId)->setMainColor(color);
      strokeRas = rasterizeWholeStroke(stroke, newPlt.getPointer(),
                                       doAntialiasing);
    } else
      strokeRas =
          rasterizeWholeStroke(stroke, ri->getPalette(), doAntialiasing);
    // A stroke whose style draws nothing gives nothing to put over.
    if (!strokeRas) return rectRender;
    TRect tmp        = rectRender - sBBox.getP00();
    TRaster32P glRas = strokeRas->extract(tmp);
    TRop::over(ri->getRaster(), glRas, rectRender.getP00());
  }
  return rectRender;
}

//-------------------------------------------------------------------

// The region and the regions inside it, in their own colours over
// transparent pixels. \p rect is set to where in \p rasBounds the result
// goes; an empty one draws nothing. Where it is drawn: as above.
TRaster32P rasterizeRegion(TRect &rect, TRect rasBounds, TRegion *region,
                           TPalette *palette, TRectD clip) {
  TRectD regionBBox = region->getBBox();
  if (!clip.isEmpty()) regionBBox = regionBBox * clip;
  rect = convert(regionBBox) * rasBounds;
  if (rect.isEmpty()) return TRaster32P();

  TTranslation affine(-convert(rect.getP00()));
  TVectorRenderData rd(affine, TRect(rect.getSize()), palette, 0, true, true);

  if (TVectorRasterizer::isSelected()) {
    TRaster32P ras(rect.getSize());
    ras->clear();
    if (TVectorRasterizer::draw(ras, region, rd)) return ras;
  }

  TOfflineGL *gl = new TOfflineGL(rect.getSize());
  gl->makeCurrent();
  gl->clear(TPixel32::Transparent);
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glEnable(GL_ALPHA_TEST);
  glAlphaFunc(GL_GREATER, 0);
  tglDraw(rd, region);
  glDisable(GL_ALPHA_TEST);
  glPopAttrib();
  glFinish();
  TRaster32P ras = gl->getRaster();
  gl->doneCurrent();
  delete gl;
  return ras;
}

//-------------------------------------------------------------------

void fastAddPaintRegion(const TRasterImageP &ri, TRegion *region,
                        int newPaintId, int maxStyleId,
                        TRectD clip = TRectD()) {
  TRaster32P ras = ri->getRaster();
  TRect rect;
  TRaster32P glRas =
      rasterizeRegion(rect, ras->getBounds(), region, ri->getPalette(), clip);
  if (rect.isEmpty()) return;

  assert(TPixelCM32::getMaxTone() == 255);
  TRop::over(ri->getRaster(), glRas, rect.getP00());

  TRegion *subregion;
  UINT i = 0;
  for (; i < region->getSubregionCount(); ++i) {
    subregion = region->getSubregion(i);
    fastAddPaintRegion(ri, subregion,
                       std::min(maxStyleId, subregion->getStyle()), maxStyleId);
  }
}
}  // namespace

//==========================================================================

TRect TRasterImageUtils::addStroke(const TRasterImageP &ri, TStroke *stroke,
                                   TRectD clip, double opacity,
                                   bool doAntialiasing) {
  TStroke *s      = new TStroke(*stroke);
  TPoint riCenter = ri->getRaster()->getCenter();
  s->transform(TTranslation(riCenter.x, riCenter.y));
  TRect rect = fastAddInkStroke(ri, s, clip, opacity, doAntialiasing);
  rect -= riCenter;
  delete s;
  return rect;
}

//==========================================================================

TRect TRasterImageUtils::convertWorldToRaster(const TRectD &area,
                                              const TRasterImageP ri) {
  if (area.isEmpty()) return TRect();
  if (!ri || !ri->getRaster())
    return TRect(tfloor(area.x0), tfloor(area.y0), tfloor(area.x1) - 1,
                 tfloor(area.y1) - 1);
  TRasterP ras = ri->getRaster();
  TRectD rect(area + ras->getCenterD());
  return TRect(tfloor(rect.x0), tfloor(rect.y0), tceil(rect.x1) - 1,
               tceil(rect.y1) - 1);
}

//==========================================================================

TRectD TRasterImageUtils::convertRasterToWorld(const TRect &area,
                                               const TRasterImageP ri) {
  if (area.isEmpty()) return TRectD();

  TRectD rect(area.x0, area.y0, area.x1 + 1, area.y1 + 1);
  if (ri && ri->getRaster()) rect = rect - ri->getRaster()->getCenterD();
  return rect;
}

//==========================================================================

// DA RIFARE
// e' lenta da far schifo

//! Converts a TVectorImage into a TRasterImage. The input vector image
//! is transformed through the passed affine \b aff, and put into a
//! TRasterImage strictly covering the bounding box of the transformed
//! vector image. The output image has its lower-left position in the
//! world reference specified by the \b pos parameter, which is granted to
//! be an integer displacement of the passed value. Additional parameters
//! include an integer \b enlarge by which the output image is enlarged with
//! respect to the transformed image's bbox, and the bool \b transformThickness
//! to specify whether the transformation should involve strokes' thickensses
//! or not.
TRasterImageP TRasterImageUtils::vectorToFullColorImage(
    const TVectorImageP &vimage, const TAffine &aff, TPalette *palette,
    const TPointD &outputPos, const TDimension &outputSize,
    const std::vector<TRasterFxRenderDataP> *fxs, bool transformThickness) {
  if (!vimage || !palette) return 0;

  // Transform the vector image through aff
  TVectorImageP vi = vimage->clone();
  vi->transform(aff, transformThickness);

  // Allocate the output ToonzImage
  TRaster32P raster(outputSize.lx, outputSize.ly);
  raster->clear();
  TRasterImageP ri(raster);
  ri->setPalette(palette->clone());

  // Shift outputPos to the origin
  vi->transform(TTranslation(-outputPos));

  int strokeCount = vi->getStrokeCount();
  std::vector<int> strokeIndex(strokeCount);
  std::vector<TStroke *> strokes(strokeCount);
  int i;
  for (i = 0; i < strokeCount; ++i) {
    strokeIndex[i] = i;
    strokes[i]     = vi->getStroke(i);
  }
  vi->notifyChangedStrokes(strokeIndex, strokes);

  int maxStyleId = palette->getStyleCount() - 1;
  for (i = 0; i < (int)vi->getRegionCount(); ++i) {
    TRegion *region = vi->getRegion(i);
    fastAddPaintRegion(ri, region, std::min(maxStyleId, region->getStyle()),
                       maxStyleId);
  }

  std::set<int> colors;
  if (fxs) {
    for (i = 0; i < (int)fxs->size(); i++) {
      SandorFxRenderData *sandorData =
          dynamic_cast<SandorFxRenderData *>((*fxs)[i].getPointer());
      if (sandorData && sandorData->m_type == BlendTz) {
        std::string indexes =
            ::to_string(sandorData->m_blendParams.m_colorIndex);
        std::vector<std::string> items;
        parseIndexes(indexes, items);
        PaletteFilterFxRenderData paletteFilterData;
        insertIndexes(items, &paletteFilterData);
        colors = paletteFilterData.m_colors;
        break;
      }
    }
  }

  for (i = 0; i < strokeCount; ++i) {
    TStroke *stroke = vi->getStroke(i);

    bool visible       = false;
    int styleId        = stroke->getStyle();
    TColorStyleP style = palette->getStyle(styleId);
    assert(style);
    int colorCount = style->getColorParamCount();
    if (colorCount == 0)
      visible = true;
    else {
      visible = false;
      for (int j = 0; j < style->getColorParamCount() && !visible; j++) {
        TPixel32 color = style->getColorParamValue(j);
        if (color.m != 0) visible = true;
      }
    }
    if (visible) fastAddInkStroke(ri, stroke, TRectD(), 1, true);
  }
  return ri;
}

//-------------------------------------------------------------------

TRect TRasterImageUtils::eraseRect(const TRasterImageP &ri,
                                   const TRectD &area) {
  TRasterP ras = ri->getRaster();
  TRect rect   = convertWorldToRaster(area, ri) * ras->getBounds();
  if (rect.isEmpty()) return rect;
  ras->lock();
  TRasterP workRas = ras->extract(rect);
  if (workRas->getPixelSize() == 4)
    workRas->clear();
  else {
    TRasterGR8P rasGR8(workRas);
    if (rasGR8) rasGR8->fill(TPixelGR8::White);
  }
  ras->unlock();
  return rect;
}

//-------------------------------------------------------------------

std::vector<TRect> TRasterImageUtils::paste(const TRasterImageP &ri,
                                            const TTileSetFullColor *tileSet) {
  std::vector<TRect> rects;
  TRasterP raster = ri->getRaster();
  for (int i = 0; i < tileSet->getTileCount(); i++) {
    const TTileSetFullColor::Tile *tile = tileSet->getTile(i);
    TRasterP ras;
    tile->getRaster(ras);
    assert(ras);
    raster->copy(ras, tile->m_rasterBounds.getP00());
    rects.push_back(tile->m_rasterBounds);
  }
  return rects;
}
//-------------------------------------------------------------------

void TRasterImageUtils::addSceneNumbering(const TRasterImageP &ri,
                                          int globalIndex,
                                          const std::wstring &sceneName,
                                          int sceneIndex) {
  if (!ri) return;
  TRasterP raster = ri->getRaster();
  int lx = raster->getLx(), ly = raster->getLy();
  QColor greyOverlay(100, 100, 100, 140);
  QImage image = rasterToQImage(raster, true, false);
  QPainter p(&image);
  QFont numberingFont = QFont();
  numberingFont.setPixelSize(ly * 0.04);
  numberingFont.setBold(true);
  p.setFont(numberingFont);
  QTransform transform;
  p.setTransform(transform.translate(0, ly).scale(1, -1), true);
  QFontMetrics fm = p.fontMetrics();
  int fontHeight  = fm.height();
  int offset      = fontHeight * 0.2;

  // write the scenename and the scene frame
  QString sceneFrame = QString::number(sceneIndex);
  while (sceneFrame.size() < 4) sceneFrame.push_front("0");
  QString sceneNumberingString =
      QString::fromStdWString(sceneName) + ": " + sceneFrame;

  int sceneNumberingWidth = fm.horizontalAdvance(sceneNumberingString);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(255, 255, 255, 255));
  p.drawRect(offset, ly - offset - fontHeight, sceneNumberingWidth + offset * 2,
             fontHeight);
  p.setBrush(greyOverlay);
  p.drawRect(offset, ly - offset - fontHeight, sceneNumberingWidth + offset * 2,
             fontHeight);
  p.setPen(Qt::white);
  p.drawText(2 * offset, ly - 2 * offset, sceneNumberingString);

  // write the global frame
  QString globalFrame = QString::number(globalIndex);
  while (globalFrame.size() < 4) globalFrame.push_front("0");

  int gloablNumberingWidth = fm.horizontalAdvance(globalFrame);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(255, 255, 255, 255));
  p.drawRect(lx - 3 * offset - gloablNumberingWidth, ly - offset - fontHeight,
             gloablNumberingWidth + offset * 2, fontHeight);
  p.setBrush(greyOverlay);
  p.drawRect(lx - 3 * offset - gloablNumberingWidth, ly - offset - fontHeight,
             gloablNumberingWidth + offset * 2, fontHeight);
  p.setPen(Qt::white);
  p.drawText(lx - 2 * offset - gloablNumberingWidth, ly - 2 * offset,
             globalFrame);
  p.end();
}

//-------------------------------------------------------------------

void TRasterImageUtils::addGlobalNumbering(const TRasterImageP &ri,
                                           const std::wstring &sceneName,
                                           int globalIndex) {
  if (!ri) return;
  TRasterP raster = ri->getRaster();
  int lx = raster->getLx(), ly = raster->getLy();
  QColor greyOverlay(100, 100, 100, 140);
  QImage image = rasterToQImage(raster, true, false);
  QPainter p(&image);
  QFont numberingFont = QFont();
  numberingFont.setPixelSize(ly * 0.04);
  numberingFont.setBold(true);
  p.setFont(numberingFont);
  QTransform transform;
  p.setTransform(transform.translate(0, ly).scale(1, -1), true);
  QFontMetrics fm     = p.fontMetrics();
  int fontHeight      = fm.height();
  int offset          = fontHeight * 0.2;
  QString globalFrame = QString::number(globalIndex);
  while (globalFrame.size() < 4) globalFrame.push_front("0");
  QString globalNumberingString =
      QString::fromStdWString(sceneName) + ": " + globalFrame;

  int globalNumberingWidth = fm.horizontalAdvance(globalNumberingString);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(255, 255, 255, 255));
  p.drawRect(offset, ly - offset - fontHeight,
             globalNumberingWidth + offset * 2, fontHeight);
  p.setBrush(greyOverlay);
  p.drawRect(offset, ly - offset - fontHeight,
             globalNumberingWidth + offset * 2, fontHeight);
  p.setPen(Qt::white);
  p.drawText(2 * offset, ly - 2 * offset, globalNumberingString);
  p.end();
}
