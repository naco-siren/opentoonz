

// The main thread dispatcher of the build with Qt (step 9b of
// doc/nexttoonz_core_noqt.md): what TThread::Message did with Qt signals.
// To leave tnzcore with the rest of the Qt adapters at step 20.

#include "tthreadp.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>

#include <cassert>

namespace {
TThreadMessageDispatcher *Dispatcher = 0;  // made in the main thread
}

//------------------------------------------------------------------------------

TThreadMessageDispatcher::TThreadMessageDispatcher() {}

//------------------------------------------------------------------------------

void TThreadMessageDispatcher::init() {
  if (!TThread::isMainThread()) return;
  if (Dispatcher == 0) {
    Dispatcher = new TThreadMessageDispatcher();
    TThread::setMainThreadDispatcher(Dispatcher);
  }
}

//------------------------------------------------------------------------------

TThreadMessageDispatcher *TThreadMessageDispatcher::instance() {
  assert(Dispatcher);
  return Dispatcher;
}

//------------------------------------------------------------------------------

// A queued call to this object, which lives in the main thread: a
// QMetaCallEvent in the main thread's event queue, as a queued signal is.
void TThreadMessageDispatcher::post(std::function<void()> task) {
  QMetaObject::invokeMethod(this, std::move(task), Qt::QueuedConnection);
}

//------------------------------------------------------------------------------

bool TThreadMessageDispatcher::isMainThread() const {
  return TThread::isMainThread();
}

//------------------------------------------------------------------------------

// As TRenderer waits today: the events are processed, sleeping until there
// is one, until done() holds.
void TThreadMessageDispatcher::waitUntil(const std::function<bool()> &done) {
  while (!done())
    QCoreApplication::processEvents(QEventLoop::AllEvents |
                                    QEventLoop::WaitForMoreEvents);
}
