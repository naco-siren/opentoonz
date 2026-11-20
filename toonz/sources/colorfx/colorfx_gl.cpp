// The OpenGL side of colorfx's styles (tglentries.h): the OpenGL entry
// points of the styles that set OpenGL state of their own, as they were,
// moved here. Built into tnzglview and registered by TGLView::install()
// (doc/nexttoonz_core_noqt.md, steps 8b and 8c).

#include "colorfx_gl.h"

#include "strokestyles.h"
#include "regionstyles.h"
#include "flowlinestrokestyle.h"

#include "tgl.h"
#include "tglcanvasbridge.h"
#include "tglentries.h"
#include "tcolorfunctions.h"
#include "tpixelutils.h"
#include "tstrokeoutline.h"

#include <vector>

namespace {

inline void tglNormal(const T3DPointD &p) { glNormal3d(p.x, p.y, p.z); }

inline void tglVertex(const T3DPointD &p) { glVertex3d(p.x, p.y, p.z); }

// What the regions drew through before the seams: a canvas over OpenGL,
// between the push and pop of all of OpenGL's attributes.
template <class Draw>
void drawThroughGl(Draw draw) {
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  {
    TLegacyGlCanvas gl;
    draw(gl.canvas());
  }
  glPopAttrib();
}

}  // namespace

struct TColorFxGLAccess {
  static void sketchStroke(const TColorStyle &s, const TColorFunction *cf,
                           const TStroke *stroke) {
    // through the virtual function of tnzcore's base class: colorfx does
    // not export its style classes, so on Windows tnzglview cannot call
    // their members by name
    const TSimpleStrokeStyle &style = static_cast<const TSimpleStrokeStyle &>(s);
    {
      TLegacyGlCanvas gl;
      style.drawStrokeOnCanvas(gl.canvas(), cf, stroke,
                               glIsEnabled(GL_LINE_SMOOTH) != GL_FALSE);
    }
    // The old code left this colour behind it.
    glColor4d(0, 0, 0, 1);
  }

  static void flowLineStroke(const TColorStyle &s, const TColorFunction *cf,
                             const TStroke *stroke) {
    // (virtually, as above)
    const TSimpleStrokeStyle &style = static_cast<const TSimpleStrokeStyle &>(s);
    {
      TLegacyGlCanvas gl;
      style.drawStrokeOnCanvas(gl.canvas(), cf, stroke,
                               glIsEnabled(GL_LINE_SMOOTH) != GL_FALSE);
    }
    // The old code left this colour behind it.
    glColor4d(0.0, 0.0, 0.0, 1.0);
  }

  static void normal2Stroke(const TColorStyle &s, const TColorFunction *cf,
                            TStrokeOutline *outline, const TStroke *stroke) {
    const TNormal2StrokeStyle &style =
        static_cast<const TNormal2StrokeStyle &>(s);
    TPixel32 color;
    if (cf)
      color = (*(cf))(style.m_color);
    else
      color = style.m_color;
    TPixelD dcolor;
    dcolor = toPixelD(color);
    UINT i;
    double bend                         = 2 * style.m_bend;
    const std::vector<TOutlinePoint> &v = outline->getArray();
    if (v.empty()) return;
    std::vector<T3DPointD> normal;

    GLfloat light_position[] = {(float)(style.m_lightx), (float)(style.m_lighty), 100.0, 0.0};
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    GLfloat mat_ambient[] = {(float)dcolor.r, (float)dcolor.g, (float)dcolor.b,
                             1.0};
    GLfloat mat_specular[] = {(float)(style.m_metal * (1 - dcolor.r) + dcolor.r),
                              (float)(style.m_metal * (1 - dcolor.g) + dcolor.g),
                              (float)(style.m_metal * (1 - dcolor.b) + dcolor.b), 1.0};
    GLfloat mat_shininess[] = {(float)style.m_shininess};

    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_ambient);

    // glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

    // outline with antialiasing
    glBegin(GL_LINE_STRIP);
    for (i = 0; i < v.size(); i += 2) {
      T3DPointD pointa(v[i].x, v[i].y, 0);
      T3DPointD pointb(v[i + 1].x, v[i + 1].y, 0);
      T3DPointD d           = pointb - pointa;
      if (norm2(d) > 0.0) d = normalize(d);
      normal.push_back(d);
      T3DPointD pointaNormal = T3DPointD(0, 0, 1) - bend * d;
      tglNormal(pointaNormal);
      tglVertex(pointa);
    }
    glEnd();

    int normalcounter = 0;
    glBegin(GL_LINE_STRIP);
    for (i = 1; i < v.size(); i += 2) {
      T3DPointD pointa(v[i].x, v[i].y, 0);
      T3DPointD pointaNormal =
          T3DPointD(0, 0, 1) + bend * normal[normalcounter++];
      tglNormal(pointaNormal);
      tglVertex(pointa);
    }
    glEnd();

    normalcounter = 0;
    for (i = 0; i <= v.size() - 4; i += 2) {
      glBegin(GL_QUAD_STRIP);
      T3DPointD olda(v[i].x, v[i].y, 0);
      T3DPointD oldb(v[i + 1].x, v[i + 1].y, 0);
      T3DPointD oldcenter = 0.5 * (olda + oldb);
      T3DPointD oldcenterNormal(0, 0, 1);
      T3DPointD oldaNormal = T3DPointD(0, 0, 1) - bend * normal[normalcounter];
      T3DPointD oldbNormal = T3DPointD(0, 0, 1) + bend * normal[normalcounter];
      T3DPointD a(v[i + 2].x, v[i + 2].y, 0);
      T3DPointD b(v[i + 3].x, v[i + 3].y, 0);
      T3DPointD center = 0.5 * (a + b);
      T3DPointD centerNormal(0, 0, 1);
      T3DPointD aNormal = T3DPointD(0, 0, 1) - bend * normal[normalcounter++];
      T3DPointD bNormal = T3DPointD(0, 0, 1) + bend * normal[normalcounter];
      tglNormal(oldaNormal);
      tglVertex(olda);
      tglNormal(aNormal);
      tglVertex(a);
      tglNormal(oldcenterNormal);
      tglVertex(oldcenter);
      tglNormal(centerNormal);
      tglVertex(center);
      tglNormal(oldbNormal);
      tglVertex(oldb);
      tglNormal(bNormal);
      tglVertex(b);
      glEnd();
    }

    glDisable(GL_NORMALIZE);
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
}

  static void pointShadowRegion(const TColorStyle &s, const TColorFunction *cf,
                                bool antiAliasing, TRegionOutline &boundary) {
    const TPointShadowFillStyle &style =
        static_cast<const TPointShadowFillStyle &>(s);
    drawThroughGl([&](TCanvas &canvas) {
      // The points are as large as this driver can make them, times the
      // style's parameter: that is what the style has always meant by a
      // size.
      GLfloat sizes[2];
      glGetFloatv(GL_POINT_SIZE_RANGE, sizes);
      style.drawOnCanvas(canvas, cf, antiAliasing, boundary, sizes[0],
                         sizes[1]);
    });
  }

  static void stripeRegion(const TColorStyle &s, const TColorFunction *cf,
                           bool antiAliasing, TRegionOutline &boundary) {
    const TStripeFillStyle &style = static_cast<const TStripeFillStyle &>(s);
    drawThroughGl([&](TCanvas &canvas) {
      // The lines along the stripes are smooth whatever the caller has set.
      tglEnableLineSmooth();
      style.drawRegionOnCanvas(canvas, cf, antiAliasing, boundary);
    });
  }

  static void circleStripeRegion(const TColorStyle &s,
                                 const TColorFunction *cf, bool antiAliasing,
                                 TRegionOutline &boundary) {
    const TCircleStripeFillStyle &style =
        static_cast<const TCircleStripeFillStyle &>(s);
    drawThroughGl([&](TCanvas &canvas) {
      // The lines along the stripes are smooth whatever the caller has set.
      tglEnableLineSmooth();
      style.drawRegionOnCanvas(canvas, cf, antiAliasing, boundary);
    });
  }
};

void registerColorFxGlDrawers() {
  using namespace TGLEntries;
  setDrawer("TSketchStrokeStyle::drawStroke",
            reinterpret_cast<AnyDrawer>(
                static_cast<StrokeDrawer>(&TColorFxGLAccess::sketchStroke)));
  setDrawer("FlowLineStrokeStyle::drawStroke",
            reinterpret_cast<AnyDrawer>(
                static_cast<StrokeDrawer>(&TColorFxGLAccess::flowLineStroke)));
  setDrawer("TNormal2StrokeStyle::drawStroke",
            reinterpret_cast<AnyDrawer>(static_cast<OutlineStrokeDrawer>(
                &TColorFxGLAccess::normal2Stroke)));
  setDrawer("TPointShadowFillStyle::drawRegion",
            reinterpret_cast<AnyDrawer>(static_cast<RegionDrawer>(
                &TColorFxGLAccess::pointShadowRegion)));
  setDrawer("TStripeFillStyle::drawRegion",
            reinterpret_cast<AnyDrawer>(static_cast<RegionDrawer>(
                &TColorFxGLAccess::stripeRegion)));
  setDrawer("TCircleStripeFillStyle::drawRegion",
            reinterpret_cast<AnyDrawer>(static_cast<RegionDrawer>(
                &TColorFxGLAccess::circleStripeRegion)));
}
