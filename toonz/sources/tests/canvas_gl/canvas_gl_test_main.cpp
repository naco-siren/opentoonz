// main() of nexttoonz_canvas_gl_tests.
//
// An offscreen OpenGL context needs a QGuiApplication. On macOS that would
// turn the test into a foreground application with an icon in the Dock;
// the environment variable below keeps it in the background.

#include <gtest/gtest.h>

#include "tthread.h"

#include <QGuiApplication>

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Listing the tests (which the build does) needs no window system.
  if (::testing::GTEST_FLAG(list_tests)) return RUN_ALL_TESTS();

  qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
  QGuiApplication app(argc, argv);
  TThread::init();
  return RUN_ALL_TESTS();
}
