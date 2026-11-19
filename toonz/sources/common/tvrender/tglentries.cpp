// The three seams between the core's drawing and desktop OpenGL
// (tglentries.h, doc/nexttoonz_core_noqt.md step 8b). No OpenGL here: the
// built-in OpenGL implementations are in tglentries_gl.cpp.

#include "tglentries.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#ifdef QT_CORE_LIB
// tglentries_gl.cpp, until step 8c moves OpenGL out of the core
const TGLEntries::ViewerCanvas *builtInViewerCanvas();
TGLEntries::OfflineFactory builtInOfflineFactory();
void registerBuiltInDrawers(void (*add)(const char *entry,
                                         TGLEntries::AnyDrawer drawer));
#endif

namespace {

const TGLEntries::ViewerCanvas *builtInCanvas() {
#ifdef QT_CORE_LIB
  return builtInViewerCanvas();
#else
  return nullptr;
#endif
}

TGLEntries::OfflineFactory builtInFactory() {
#ifdef QT_CORE_LIB
  return builtInOfflineFactory();
#else
  return nullptr;
#endif
}

std::atomic<const TGLEntries::ViewerCanvas *> currentCanvas{nullptr};
std::atomic<bool> canvasSet{false};
std::atomic<TGLEntries::OfflineFactory> currentFactory{nullptr};
std::atomic<bool> factorySet{false};

std::mutex drawersMutex;
// std::less<> finds an entry by its const char * without making a string:
// a drawer is looked up every time its style draws.
typedef std::map<std::string, TGLEntries::AnyDrawer, std::less<>> Drawers;
Drawers &drawers() {
  static Drawers map;
  return map;
}
// The built-in drawers go in before the first drawer is set or looked up,
// so that a drawer set later replaces them.
void builtInDrawersOnce() {
#ifdef QT_CORE_LIB
  static std::once_flag once;
  std::call_once(once, [] {
    registerBuiltInDrawers([](const char *entry, TGLEntries::AnyDrawer d) {
      std::lock_guard<std::mutex> lock(drawersMutex);
      drawers()[entry] = d;
    });
  });
#endif
}

const TGLEntries::ViewerCanvas *canvas() {
  return canvasSet.load() ? currentCanvas.load() : builtInCanvas();
}

// A seam asked for and not installed: say so, once, in the build with Qt
// (where a program that draws with OpenGL forgot to install it).
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
  const ViewerCanvas *previous = canvas();
  currentCanvas.store(hooks);
  canvasSet.store(hooks != nullptr);
  return previous;
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
  builtInDrawersOnce();
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
  builtInDrawersOnce();
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
  OfflineFactory previous = factorySet.load() ? currentFactory.load()
                                              : builtInFactory();
  currentFactory.store(factory);
  factorySet.store(factory != nullptr);
  return previous;
}

std::unique_ptr<OfflineContext> openOffline(const TDimension &size,
                                            bool stock) {
  OfflineFactory f =
      factorySet.load() ? currentFactory.load() : builtInFactory();
  if (!f) {
    missing("an offline OpenGL render");
    return nullptr;
  }
  return f(size, stock);
}

}  // namespace TGLEntries
