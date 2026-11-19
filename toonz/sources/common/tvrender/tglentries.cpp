// The three seams between the core's drawing and desktop OpenGL
// (tglentries.h, doc/nexttoonz_core_noqt.md steps 8b and 8c). No OpenGL
// here: the OpenGL implementations are in tnzglview, which a program
// installs with TGLView::install() (tglview.h).

#include "tglentries.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace {

std::atomic<const TGLEntries::ViewerCanvas *> currentCanvas{nullptr};
std::atomic<TGLEntries::OfflineFactory> currentFactory{nullptr};

std::mutex drawersMutex;
// std::less<> finds an entry by its const char * without making a string:
// a drawer is looked up every time its style draws.
typedef std::map<std::string, TGLEntries::AnyDrawer, std::less<>> Drawers;
Drawers &drawers() {
  static Drawers map;
  return map;
}
const TGLEntries::ViewerCanvas *canvas() { return currentCanvas.load(); }

// A seam asked for and not installed: say so, once, in the build with Qt
// (where a program that draws with OpenGL forgot TGLView::install()).
void missing(const char *what) {
#ifdef QT_CORE_LIB
  static std::atomic<bool> told{false};
  if (!told.exchange(true))
    std::fprintf(stderr,
                 "NextToonz: %s needs the OpenGL view (TGLView::install()), "
                 "which this program has not installed; nothing is drawn\n",
                 what);
  assert(!"an OpenGL seam is not installed");
#else
  (void)what;
#endif
}

}  // namespace

namespace TGLEntries {

const ViewerCanvas *setViewerCanvas(const ViewerCanvas *hooks) {
  return currentCanvas.exchange(hooks);
}

void paintOnViewerCanvas(Painter painter, void *what, bool saveAllAttributes) {
  if (const ViewerCanvas *c = canvas())
    c->paint(painter, what, saveAllAttributes);
  else
    missing("drawing on the viewer");
}

void pushTransform(const TAffine &aff) {
  if (const ViewerCanvas *c = canvas())
    c->pushTransform(aff);
  else
    missing("drawing on the viewer");
}

void popTransform() {
  if (const ViewerCanvas *c = canvas()) c->popTransform();
}

double pixelSize2() {
  const ViewerCanvas *c = canvas();
  return c ? c->pixelSize2() : 1.0;
}

AnyDrawer setDrawer(const char *entry, AnyDrawer drawer) {
  std::lock_guard<std::mutex> lock(drawersMutex);
  auto &map         = drawers();
  auto it           = map.find(entry);
  AnyDrawer previous = it == map.end() ? nullptr : it->second;
  if (drawer)
    map[entry] = drawer;
  else if (it != map.end())
    map.erase(it);
  return previous;
}

AnyDrawer findDrawer(const char *entry) {
  AnyDrawer d = nullptr;
  {
    std::lock_guard<std::mutex> lock(drawersMutex);
    auto it = drawers().find(entry);
    if (it != drawers().end()) d = it->second;
  }
  if (!d) missing(entry);
  return d;
}

OfflineContext::~OfflineContext() {}

OfflineFactory setOfflineFactory(OfflineFactory factory) {
  return currentFactory.exchange(factory);
}

std::unique_ptr<OfflineContext> openOffline(const TDimension &size,
                                            bool stock) {
  OfflineFactory f = currentFactory.load();
  if (!f) {
    missing("an offline OpenGL render");
    return nullptr;
  }
  return f(size, stock);
}

}  // namespace TGLEntries
