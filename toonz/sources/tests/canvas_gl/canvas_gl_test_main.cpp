// main() of nexttoonz_canvas_gl_tests.
//
// An offscreen OpenGL context needs a QGuiApplication. On macOS that would
// turn the test into a foreground application with an icon in the Dock;
// the environment variable below keeps it in the background.

#include <gtest/gtest.h>

#include "tthread.h"

#include <QGuiApplication>
#include <QOpenGLContext>

#include <cstdio>

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Listing the tests (which the build does) needs no window system.
  if (::testing::GTEST_FLAG(list_tests)) return RUN_ALL_TESTS();

  qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
  QGuiApplication app(argc, argv);
  TThread::init();

  // TOfflineGL does not survive a platform without OpenGL (the "offscreen"
  // one of Qt, for instance); say so instead of crashing in the first test.
  {
    QOpenGLContext context;
    if (!context.create()) {
      std::fprintf(stderr,
                   "No OpenGL context on this Qt platform. These tests need "
                   "a window system; on Linux run them under xvfb-run with "
                   "QT_QPA_PLATFORM=xcb.\n");
      return 1;
    }
  }
  return RUN_ALL_TESTS();
}
