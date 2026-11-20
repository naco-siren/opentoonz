// The thread pool needs a QCoreApplication whose main thread processes
// events (Executor::init()): workers are created there, through a queued
// signal, and the tasks' signals are delivered there.

#include <gtest/gtest.h>

#include "tthread.h"

#include <QCoreApplication>

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  TThread::init();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
