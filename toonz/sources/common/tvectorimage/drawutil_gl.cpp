// The OpenGL drawing of drawutil.h (drawStrokeCenterline) and its texture
// helper, moved from drawutil.cpp to tnzglview with step 8c of
// doc/nexttoonz_core_noqt.md.

#include "drawutil.h"
#include "tstroke.h"
#include "tmathutil.h"
#include "tcurveutil.h"
#include "tcurves.h"

namespace {
void drawQuadraticCenterline(const TQuadratic &inQuad, double pixelSize,
                             double from, double to) {
  assert(0.0 <= from && from <= to && to <= 1.0);

  to   = (std::max)(0.0, (std::min)(to, 1.0));
  from = (std::max)(0.0, (std::min)(from, to));

  TQuadratic tmp(inQuad), s1, s2;

  TQuadratic *quad = &tmp;

  double newFrom = from;
  if (to != 1.0) {
    tmp.split(to, s1, s2);
    quad    = &s1;
    newFrom = from / to;
  }

  if (from != 0.0) {
    tmp = *quad;
    tmp.split(newFrom, s1, s2);
    quad = &s2;
  }

  // glColor( TPixel32::Black );
  double step = computeStep(*quad, pixelSize);

  // It draws the curve as a linear piecewise approximation

  double invSqrtScale = 1.0;

  // First of all, it computes the control circles of the curve in screen
  // coordinates
  TPointD scP0 = quad->getP0();
  TPointD scP1 = quad->getP1();
  TPointD scP2 = quad->getP2();

  TPointD A = scP0 - 2 * scP1 + scP2;
  TPointD B = scP0 - scP1;

  double h;
  h         = invSqrtScale * step;
  double h2 = h * h;

  TPointD P = scP0, D2 = 2 * h2 * A, D1 = A * h2 - 2 * B * h;

  if (h < 0 || isAlmostZero(h)) return;

  //    if ( h < from )
  //      h = from;

  // It draws the whole curve, using forward differencing
  glBegin(GL_LINE_STRIP);  // The curve starts from scP0

  // scP0 = quad.getPoint(from);
  glVertex2d(scP0.x, scP0.y);

  for (double t = from + h; t < to; t = t + h) {
    P  = P + D1;
    D1 = D1 + D2;
    glVertex2d(P.x, P.y);
  }

  // scP2 = quad.getPoint(to);
  glVertex2d(scP2.x, scP2.y);  // The curve ends in scP2
  glEnd();
}
}

//-----------------------------------------------------------------------------

#if defined(MACOSX)
void lefttRotateBits(UCHAR *buf, int bufferSize) {
  UINT *buffer = (UINT *)buf;
  UINT app;
  for (int i = 0; i < bufferSize; i++, buffer++) {
    app     = *buffer;
    *buffer = app << 8 | app >> 24;
  }
}
#endif

TRasterP prepareTexture(const TRasterP &ras, TextureInfoForGL &texinfo) {
  TDimension size = ras->getSize();

  texinfo.width          = size.lx;
  texinfo.height         = size.ly;
  texinfo.internalformat = ras->getPixelSize();
  texinfo.format         = GL_UNSIGNED_BYTE;
  texinfo.pixels         = ras->getRawData();

  switch (texinfo.internalformat) {
  case 1:
    texinfo.type = GL_LUMINANCE;
    break;
  case 2:
    texinfo.type = GL_LUMINANCE_ALPHA;
    break;
  }

  if (texinfo.internalformat > 2) {
    switch (texinfo.internalformat) {
    case 3:
      texinfo.type = GL_RGB;
      break;
    case 4:
      texinfo.type = GL_RGBA;
      break;  // GL_RGBA;  break;
    }

#ifdef TNZ_MACHINE_CHANNEL_ORDER_BGRM  // under win32 pixel are in reverse order

#ifdef GL_EXT_bgra  // if extension exists...

    // and it's  present at run time all okay
    // if(TGLArea::isBGRASupported())
    {
      switch (texinfo.internalformat) {
      case 3:
        texinfo.type = GL_BGR_EXT;
        break;
      case 4:
        texinfo.type = GL_BGRA_EXT;
        break;
      }
      return ras;
    }
#else

    TDimension size = ras->getSize();
    TRasterP outRas = ras->clone();
    outRas->lock();
    int pixelSize  = ras->getPixelSize();
    texinfo.pixels = outRas->getRawData();

    UCHAR *p1, *p2;

    for (int i = 0; i < size.lx; ++i)
      for (int j = 0; j < size.ly; ++j) {
        p1 = outRas->getRawData(i, j);
        p2 = p1 + 2;
        std::swap(*p1, *p2);
      }
    outRas->unlock();
    return outRas;
#endif

#elif defined(TNZ_MACHINE_CHANNEL_ORDER_MRGB)
// mrgb

#warning "ottimizzare in qualche modo"

    TDimension size = ras->getSize();
    TRasterP outRas = ras->clone();

    texinfo.pixels = outRas->getRawData();

    lefttRotateBits((UCHAR *)texinfo.pixels, size.lx * size.ly);

    return outRas;

#endif
  }

  texinfo.pixels = ras->getRawData();
  return ras;
}

void drawStrokeCenterline(const TStroke &stroke, double pixelSize, double from,
                          double to) {
  int c1 = 0, c2 = 0;
  double t1 = 1.0, t2 = 0.0;

  if (stroke.getChunkCount() == 0) return;

  stroke.getChunkAndT(from, c1, t1);
  stroke.getChunkAndT(to, c2, t2);

  if (c1 == c2) {
    if (from == to) return;

    drawQuadraticCenterline(*stroke.getChunk(c1), pixelSize, t1, t2);
  } else {
    // partial first chunk
    drawQuadraticCenterline(*stroke.getChunk(c1), pixelSize, t1, 1.0);
    // next chunk
    ++c1;
    if (c1 < c2) {
      for (int i = c1; i < c2; ++i)
        drawQuadraticCenterline(*stroke.getChunk(i), pixelSize, 0.0, 1.0);
    }

    // partial last chunk
    drawQuadraticCenterline(*stroke.getChunk(c2), pixelSize, 0.0, t2);
  }
}
