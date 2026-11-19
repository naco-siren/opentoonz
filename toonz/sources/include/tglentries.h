#pragma once

#ifndef TGLENTRIES_H
#define TGLENTRIES_H

#include "tcommon.h"
#include "tgeometry.h"
#include "traster.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#undef DVAPI
#undef DVVAR
#ifdef TVRENDER_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

class TCanvas;
class TColorStyle;
class TColorFunction;
class TStroke;
class TStrokeOutline;
class TRegionOutline;
class TVectorImage;
class TVectorRenderData;

//! Where the core's drawing meets desktop OpenGL (doc/nexttoonz_core_noqt.md,
//! step 8 and Q8-A). The core no longer calls OpenGL itself: the styles' and
//! props' OpenGL entry points, and the offline renders that fall back on
//! OpenGL, go through three seams, each settable in one place. The OpenGL
//! implementations are in tnzglview; a program that draws with OpenGL
//! installs them with TGLView::install() (tglview.h). With a seam missing
//! nothing is drawn; the build with Qt says so on the standard error, once
//! (and asserts in Debug).
namespace TGLEntries {

//------------------------------------------------------------------
//  1. The viewer canvas: the OpenGL context current where the viewer draws.
//  These hooks are the whole set (decided in step 8b): adding one is a
//  decision, so that this does not grow into a table of OpenGL calls.

typedef void (*Painter)(TCanvas &canvas, bool lineSmooth, void *what);

struct ViewerCanvas {
  //! Runs painter with a canvas bound to the current OpenGL context and
  //! whether line smoothing is on there; with saveAllAttributes, between
  //! glPushAttrib(GL_ALL_ATTRIB_BITS) and glPopAttrib().
  void (*paint)(Painter painter, void *what, bool saveAllAttributes);
  void (*pushTransform)(const TAffine &aff);  //!< glPushMatrix, then aff
  void (*popTransform)();                     //!< glPopMatrix
  double (*pixelSize2)();  //!< the square of the current pixel's size
};

//! Sets the hooks and returns the ones they replace (nullptr: none).
DVAPI const ViewerCanvas *setViewerCanvas(const ViewerCanvas *hooks);

DVAPI void paintOnViewerCanvas(Painter painter, void *what,
                               bool saveAllAttributes = false);
DVAPI void pushTransform(const TAffine &aff);
DVAPI void popTransform();
DVAPI double pixelSize2();

//! paintOnViewerCanvas with a callable taking (TCanvas &, bool lineSmooth).
template <class F>
void onViewerCanvas(F &&f, bool saveAllAttributes = false) {
  paintOnViewerCanvas(
      [](TCanvas &canvas, bool lineSmooth, void *what) {
        (*static_cast<typename std::remove_reference<F>::type *>(what))(
            canvas, lineSmooth);
      },
      &f, saveAllAttributes);
}

//------------------------------------------------------------------
//  2. OpenGL drawings by class: the styles whose OpenGL entry point sets
//  OpenGL state of its own. Each is an entry named after the member it
//  implements; its drawer is a function of the style and the member's
//  arguments.

typedef void (*AnyDrawer)();

typedef void (*StrokeDrawer)(const TColorStyle &style,
                             const TColorFunction *cf, const TStroke *stroke);
typedef void (*OutlineStrokeDrawer)(const TColorStyle &style,
                                    const TColorFunction *cf,
                                    TStrokeOutline *outline,
                                    const TStroke *stroke);
typedef void (*RegionDrawer)(const TColorStyle &style,
                             const TColorFunction *cf, bool antiAliasing,
                             TRegionOutline &boundary);
typedef void (*PatternStrokeDrawer)(const TColorStyle &style,
                                    const TVectorRenderData &rd,
                                    const std::vector<TAffine> &transformations,
                                    const TStroke *stroke);

//! The one place drawers come from: sets the entry's drawer (nullptr removes
//! it) and returns the one it replaces.
DVAPI AnyDrawer setDrawer(const char *entry, AnyDrawer drawer);
//! The entry's drawer, or nullptr (and the missing-seam notice).
DVAPI AnyDrawer findDrawer(const char *entry);

template <class Drawer, class... Args>
void draw(const char *entry, Args &&...args) {
  if (AnyDrawer d = findDrawer(entry))
    reinterpret_cast<Drawer>(d)(std::forward<Args>(args)...);
}

//------------------------------------------------------------------
//  3. Offline contexts: what the renders that fall back on OpenGL draw
//  into (TOfflineGL on the desktop).

class DVAPI OfflineContext {
public:
  virtual ~OfflineContext();
  virtual void clear(TPixel32 color) = 0;
  virtual void makeCurrent()         = 0;
  virtual void draw(TVectorImage *image, const TVectorRenderData &rd,
                    bool doInitMatrix = false) = 0;
  virtual TRaster32P getRaster()             = 0;  //!< a copy
};

//! stock: TOfflineGL::getStock's shared context of that size, which the
//! returned object does not own; otherwise a new one.
typedef std::unique_ptr<OfflineContext> (*OfflineFactory)(
    const TDimension &size, bool stock);

//! The one place offline contexts come from: sets the factory and returns
//! the one it replaces (nullptr: none).
DVAPI OfflineFactory setOfflineFactory(OfflineFactory factory);
//! A context, or nullptr when no factory is installed.
DVAPI std::unique_ptr<OfflineContext> openOffline(const TDimension &size,
                                                  bool stock);

}  // namespace TGLEntries

//! The OpenGL drawers of tnzcore's own styles, friends of those styles: an
//! implementation detail of tnzglview (tglentries_gl.cpp).
struct TGLEntriesAccess;

#endif
