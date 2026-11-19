// The three seams between the core's drawing and OpenGL (tglentries.h,
// step 8b of doc/nexttoonz_core_noqt.md): each is set in one place, hands
// on what it is given, and gives back what it replaced. In the build with
// Qt the OpenGL implementations are built in; in the build without Qt
// nothing is, and a seam that is asked for draws nothing, silently.
//
// What goes through them when a style or a prop draws is tested with the
// styles (tests/toonzlib/test_gl_seams.cpp).

#include <gtest/gtest.h>

#include "tglentries.h"

#include <string>
#include <vector>

namespace {

std::vector<std::string> g_events;

void fakePaint(TGLEntries::Painter painter, void *what,
               bool saveAllAttributes) {
  g_events.push_back(std::string("paint") + (saveAllAttributes ? " all" : "") +
                     (painter ? " painter" : "") + (what ? " what" : ""));
}
void fakePush(const TAffine &aff) {
  g_events.push_back("push " + std::to_string(aff.a13) + " " +
                     std::to_string(aff.a23));
}
void fakePop() { g_events.push_back("pop"); }
double fakePixelSize2() { return 0.25; }

const TGLEntries::ViewerCanvas g_fakeCanvas = {&fakePaint, &fakePush,
                                               &fakePop, &fakePixelSize2};

void painterThatMustNotRun(TCanvas &, bool, void *) { ADD_FAILURE(); }

void fakeDrawerA() {}
void fakeDrawerB() {}

TDimension g_openedSize;
bool g_openedStock = false;

class FakeContext final : public TGLEntries::OfflineContext {
public:
  void clear(TPixel32) override {}
  void makeCurrent() override {}
  void draw(TVectorImage *, const TVectorRenderData &, bool) override {}
  TRaster32P getRaster() override { return TRaster32P(); }
};

std::unique_ptr<TGLEntries::OfflineContext> fakeFactory(const TDimension &size,
                                                        bool stock) {
  g_openedSize  = size;
  g_openedStock = stock;
  return std::unique_ptr<TGLEntries::OfflineContext>(new FakeContext);
}

}  // namespace

TEST(TGLEntries, TheViewerCanvasGoesThroughTheHooksSet) {
  g_events.clear();
  const TGLEntries::ViewerCanvas *before =
      TGLEntries::setViewerCanvas(&g_fakeCanvas);

  int what = 0;
  TGLEntries::paintOnViewerCanvas(&painterThatMustNotRun, &what);
  TGLEntries::paintOnViewerCanvas(&painterThatMustNotRun, &what, true);
  TGLEntries::onViewerCanvas([](TCanvas &, bool) { ADD_FAILURE(); }, true);
  TGLEntries::pushTransform(TTranslation(3, 4));
  EXPECT_EQ(TGLEntries::pixelSize2(), 0.25);
  TGLEntries::popTransform();

  const std::vector<std::string> expected = {
      "paint painter what", "paint all painter what", "paint all painter what",
      "push 3.000000 4.000000", "pop"};
  EXPECT_EQ(g_events, expected);

  // Setting gives back what was set; nullptr goes back to the built-in.
  EXPECT_EQ(TGLEntries::setViewerCanvas(nullptr), &g_fakeCanvas);
  EXPECT_EQ(TGLEntries::setViewerCanvas(before), before);
  TGLEntries::setViewerCanvas(nullptr);
}

TEST(TGLEntries, ADrawerIsSetFoundAndReplacedByItsEntry) {
  const char *entry = "TGLEntriesTest::draw";
  EXPECT_EQ(TGLEntries::setDrawer(entry, &fakeDrawerA), nullptr);
  EXPECT_EQ(TGLEntries::findDrawer(entry), &fakeDrawerA);
  EXPECT_EQ(TGLEntries::setDrawer(entry, &fakeDrawerB), &fakeDrawerA);
  EXPECT_EQ(TGLEntries::findDrawer(entry), &fakeDrawerB);
  EXPECT_EQ(TGLEntries::setDrawer(entry, nullptr), &fakeDrawerB);
  EXPECT_EQ(TGLEntries::setDrawer(entry, nullptr), nullptr);
}

TEST(TGLEntries, OfflineContextsComeFromTheFactorySet) {
  const TGLEntries::OfflineFactory before =
      TGLEntries::setOfflineFactory(&fakeFactory);
  std::unique_ptr<TGLEntries::OfflineContext> context =
      TGLEntries::openOffline(TDimension(64, 32), true);
  EXPECT_NE(context, nullptr);
  EXPECT_EQ(g_openedSize, TDimension(64, 32));
  EXPECT_TRUE(g_openedStock);

  EXPECT_EQ(TGLEntries::setOfflineFactory(nullptr), &fakeFactory);
  EXPECT_EQ(TGLEntries::setOfflineFactory(before), before);
  TGLEntries::setOfflineFactory(nullptr);
}

#ifdef QT_CORE_LIB

// The OpenGL implementations are built in until step 8c.
TEST(TGLEntries, TheBuildWithQtHasTheOpenGlImplementationsBuiltIn) {
  const TGLEntries::ViewerCanvas *builtIn =
      TGLEntries::setViewerCanvas(nullptr);
  EXPECT_NE(builtIn, nullptr);
  EXPECT_NE(TGLEntries::setOfflineFactory(nullptr), nullptr);
  for (const char *entry : {"TCenterLineStrokeStyle::drawStroke",
                            "TRasterImagePatternStrokeStyle::drawStroke",
                            "TVectorImagePatternStrokeStyle::drawStroke"})
    EXPECT_NE(TGLEntries::findDrawer(entry), nullptr) << entry;
}

// A drawer that is missing draws nothing and says so on the standard error,
// once in the life of the program (and asserts in a Debug build, which is
// why this is only tested in a Release one).
#ifdef NDEBUG
TEST(TGLEntries, AMissingSeamIsToldOnceInTheBuildWithQt) {
  testing::internal::CaptureStderr();
  EXPECT_EQ(TGLEntries::findDrawer("Nobody::drawStroke"), nullptr);
  EXPECT_EQ(TGLEntries::findDrawer("Nobody::drawRegion"), nullptr);
  const std::string told = testing::internal::GetCapturedStderr();
  EXPECT_EQ(told,
            "NextToonz: Nobody::drawStroke needs the OpenGL view "
            "(TGLView::install()), which this program has not installed; "
            "nothing is drawn\n");
}
#endif

#else

// Without Qt nothing is built in: whatever asks for OpenGL gets nothing,
// and nothing is said.
TEST(TGLEntries, WithoutQtNothingIsInstalledAndNothingIsSaid) {
  testing::internal::CaptureStderr();
  EXPECT_EQ(TGLEntries::setViewerCanvas(nullptr), nullptr);
  TGLEntries::paintOnViewerCanvas(&painterThatMustNotRun, nullptr, true);
  TGLEntries::pushTransform(TTranslation(1, 1));
  TGLEntries::popTransform();
  EXPECT_EQ(TGLEntries::pixelSize2(), 1.0);
  EXPECT_EQ(TGLEntries::findDrawer("TCenterLineStrokeStyle::drawStroke"),
            nullptr);
  EXPECT_EQ(TGLEntries::setOfflineFactory(nullptr), nullptr);
  EXPECT_EQ(TGLEntries::openOffline(TDimension(8, 8), false), nullptr);
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
}

#endif
