// The OpenGL side of the seams of tglentries.h, in tnzglview: what
// TGLView::install() puts in them (doc/nexttoonz_core_noqt.md, steps 8b and
// 8c). The bodies of the drawers are the OpenGL entry points of tnzcore's
// styles as they were, moved here.

#include "tglentries.h"
#include "tglview.h"

#include "../../colorfx/colorfx_gl.h"

#include "tgl.h"
#include "tglcanvasbridge.h"
#include "tofflinegl.h"
#include "tvectorgl.h"
#include "tvectorimage.h"
#include "tvectorrenderdata.h"
#include "tlevel.h"
#include "tstroke.h"
#include "tcolorfunctions.h"
#include "tsimplecolorstyles.h"

#include <cmath>

//=============================================================================
//  1. The viewer canvas

namespace {

void paintWithGl(TGLEntries::Painter painter, void *what,
                 bool saveAllAttributes) {
  if (saveAllAttributes) glPushAttrib(GL_ALL_ATTRIB_BITS);
  {
    TLegacyGlCanvas gl;
    painter(gl.canvas(), glIsEnabled(GL_LINE_SMOOTH) != GL_FALSE, what);
  }
  if (saveAllAttributes) glPopAttrib();
}

void pushGlTransform(const TAffine &aff) {
  glPushMatrix();
  tglMultMatrix(aff);
}

void popGlTransform() { glPopMatrix(); }

double glPixelSize2() { return tglGetPixelSize2(); }

const TGLEntries::ViewerCanvas glViewerCanvas = {
    &paintWithGl, &pushGlTransform, &popGlTransform, &glPixelSize2};

//=============================================================================
//  3. Offline contexts

class GlOfflineContext final : public TGLEntries::OfflineContext {
  std::unique_ptr<TOfflineGL> m_owned;
  TOfflineGL *m_gl;

public:
  GlOfflineContext(const TDimension &size, bool stock)
      : m_owned(stock ? nullptr : new TOfflineGL(size))
      , m_gl(stock ? TOfflineGL::getStock(size) : m_owned.get()) {}

  void clear(TPixel32 color) override { m_gl->clear(color); }
  void makeCurrent() override { m_gl->makeCurrent(); }
  void draw(TVectorImage *image, const TVectorRenderData &rd,
            bool doInitMatrix) override {
    m_gl->draw(image, rd, doInitMatrix);
  }
  TRaster32P getRaster() override { return m_gl->getRaster(); }
};

std::unique_ptr<TGLEntries::OfflineContext> openGlOffline(
    const TDimension &size, bool stock) {
  return std::unique_ptr<TGLEntries::OfflineContext>(
      new GlOfflineContext(size, stock));
}

}  // namespace

//=============================================================================
//  2. The drawers of tnzcore's styles

struct TGLEntriesAccess {
  static void centerLineStroke(const TColorStyle &s, const TColorFunction *cf,
                               const TStroke *stroke) {
    const TCenterLineStrokeStyle &style =
        static_cast<const TCenterLineStrokeStyle &>(s);

    // The old function touched no OpenGL state when it had nothing to draw.
    if (!stroke || stroke->getChunkCount() == 0) return;
    TPixel32 color = style.m_color;
    if (cf) color = (*cf)(style.m_color);
    if (color.m == 0) return;

    // Through OpenGL the width goes by what the driver says it can draw, as
    // it always has, and smoothing by what the caller has set.
    GLfloat range[2];
    glGetFloatv(GL_LINE_WIDTH_RANGE, &range[0]);

    // The old function switched the stipple on here, before it drew
    // anything, the ribbon included. The canvas does it for the lines alone,
    // which by the OpenGL specification draws the same thing. Mesa's
    // software rasteriser does not agree: with a dashed line that starts
    // outside the picture, where the dashes fall came out different (seen in
    // a before-and-after comparison; why, I did not find out). With the
    // stipple switched on here as well, it draws what it drew.
    if (style.m_stipple != 0) {
      glEnable(GL_LINE_STIPPLE);
      glLineStipple(1, style.m_stipple);
    }

    // It also set the width of a thin line whatever the width already was,
    // and put it back to 1 at the end. The canvas leaves the width alone
    // when it is what it needs (see tglcanvas.cpp: on one driver even a call
    // that changes nothing moves the dashes of the next stippled line), so
    // those two calls are made here, where the old code made them.
    float width = float(style.m_width);
    if (width < range[0])
      width = range[0];
    else if (width > range[1])
      width = range[1];
    width = 2 * width;
    if (width <= 1.0) {
      if (style.m_width != 0.0) width /= sqrt(tglGetPixelSize2());
      if (width == 0.F) width = 0.00001f;
      glLineWidth(width);
    }

    {
      TLegacyGlCanvas gl;
      style.drawStrokeOnCanvas(gl.canvas(), cf, stroke,
                               glIsEnabled(GL_LINE_SMOOTH), range[0], range[1],
                               0.00001f);
    }

    glLineWidth(1.0);
  }

  static void rasterPatternStroke(const TColorStyle &s,
                                  const TVectorRenderData &rd,
                                  const std::vector<TAffine> &transformations,
                                  const TStroke *stroke) {
    const TRasterImagePatternStrokeStyle &style =
        static_cast<const TRasterImagePatternStrokeStyle &>(s);

    if (style.m_level->getFrameCount() == 0) return;

    // The blending is this style's own, in both of the passes the caller
    // makes: TRop's indexed-color conversion is premultiplied, so its alpha
    // is applied once; pre-existing full-color Trail sources keep the legacy
    // blend mode. The canvas draws with what is set here.
    glEnable(GL_BLEND);
    glBlendFunc(style.m_toonzRasterSource ? GL_ONE : GL_SRC_ALPHA,
                GL_ONE_MINUS_SRC_ALPHA);
    {
      TLegacyGlCanvas gl;
      style.drawStrokeOnCanvas(gl.canvas(), rd, transformations, stroke);
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
  }

  // How a stamp of a vector pattern gets drawn through OpenGL once the
  // transform is in place: by the old renderer.
  static void paintStampWithGl(TCanvas &, const TVectorRenderData &rd,
                               const TVectorImage *stamp) {
    tglDraw(rd, stamp);
  }

  static void vectorPatternStroke(const TColorStyle &s,
                                  const TVectorRenderData &rd,
                                  const std::vector<TAffine> &transformations,
                                  const TStroke *stroke) {
    const TVectorImagePatternStrokeStyle &style =
        static_cast<const TVectorImagePatternStrokeStyle &>(s);

    // The canvas only places the stamps; they are drawn by the old renderer,
    // which sets up OpenGL for each of their styles as it always did.
    TLegacyGlCanvas gl;
    style.drawStamps(gl.canvas(), rd, transformations, stroke,
                     &paintStampWithGl);
  }
};

//=============================================================================
//  TGLView::install()

namespace {

void registerCoreDrawers() {
  using namespace TGLEntries;
  setDrawer("TCenterLineStrokeStyle::drawStroke",
            reinterpret_cast<AnyDrawer>(static_cast<StrokeDrawer>(
                &TGLEntriesAccess::centerLineStroke)));
  setDrawer("TRasterImagePatternStrokeStyle::drawStroke",
            reinterpret_cast<AnyDrawer>(static_cast<PatternStrokeDrawer>(
                &TGLEntriesAccess::rasterPatternStroke)));
  setDrawer("TVectorImagePatternStrokeStyle::drawStroke",
            reinterpret_cast<AnyDrawer>(static_cast<PatternStrokeDrawer>(
                &TGLEntriesAccess::vectorPatternStroke)));
}

}  // namespace

void TGLView::install() {
  TGLEntries::setViewerCanvas(&glViewerCanvas);
  TGLEntries::setOfflineFactory(&openGlOffline);
  registerCoreDrawers();
  registerColorFxGlDrawers();
}
