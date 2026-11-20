#pragma once

#ifndef TTHREADP_H
#define TTHREADP_H

#include <QObject>

#include "tthreadmessage.h"

//=====================================================================

//======================================
//    TThreadMessageDispatcher class
//--------------------------------------

//! The main thread dispatcher of the build with Qt (threadmessage_qt.cpp):
//! posted work is queued on the Qt event loop of the main thread, as the
//! queued signals of TThread::Message were. TThread::init() installs it.
class TThreadMessageDispatcher final : public QObject,
                                       public TMainThreadDispatcher  // singleton
{
  Q_OBJECT

public:
  TThreadMessageDispatcher();

  void post(std::function<void()> task) override;
  bool isMainThread() const override;
  void waitUntil(const std::function<bool()> &done) override;

  //! Makes the singleton and installs it; on the main thread only, as
  //! before (elsewhere it does nothing).
  static void init();
  static TThreadMessageDispatcher *instance();
};

//=====================================================================

namespace TThread {
// Forward declarations
class ExecutorId;
class ExecutorImpSlots;
}

//=====================================================================

//==============================
//    ExecutorImpSlots class
//------------------------------

class TThread::ExecutorImpSlots final : public QObject {
  Q_OBJECT

public:
  ExecutorImpSlots();
  ~ExecutorImpSlots();

  // The following is provided to ensure that point #3 in Qt reference in page
  // "Thread support in Qt"
  // is satisfied:

  //      "You must ensure that all objects created in
  //        a thread are deleted before you delete the QThread."

  // So, specifically, thread creation should happen only in the main thread,
  // not in worker threads.
  void emitRefreshAssignments();

Q_SIGNALS:
  void refreshAssignments();

public Q_SLOTS:
  void onTerminated();
  void onRefreshAssignments();
};

#endif
