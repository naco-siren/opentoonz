

// The Qt side of the thread pool (step 9d of doc/nexttoonz_core_noqt.md):
// QtRunnable, the task whose notifications are Qt signals, and the part of
// TThread::init() that needs Qt. To leave tnzcore at step 20 (decision Q9-A).

#include "tthread_qt.h"
#include "tthreadp.h"

#include <QMetaType>

static_assert(TThread::IdlePriority == (int)QThread::IdlePriority &&
                  TThread::LowestPriority == (int)QThread::LowestPriority &&
                  TThread::LowPriority == (int)QThread::LowPriority &&
                  TThread::NormalPriority == (int)QThread::NormalPriority &&
                  TThread::HighPriority == (int)QThread::HighPriority &&
                  TThread::HighestPriority == (int)QThread::HighestPriority &&
                  TThread::TimeCriticalPriority ==
                      (int)QThread::TimeCriticalPriority &&
                  TThread::InheritPriority == (int)QThread::InheritPriority,
              "TThread::Priority follows QThread::Priority");

using namespace TThread;

// Called by TThread::init() in the build with Qt: the Qt dispatcher of
// TThread::Message, and RunnableP as an argument of queued signals.
void initThreadQt() {
  qRegisterMetaType<TThread::RunnableP>("TThread::RunnableP");
  TThreadMessageDispatcher::init();
}

//------------------------------------------------------------------------------

QtRunnable::QtRunnable() {}

QtRunnable::~QtRunnable() {}

// The smart pointer passed with each signal keeps the task alive until the
// connected slots have run (queued ones included).
void QtRunnable::notifyStarted() { Q_EMIT started(RunnableP(this)); }
void QtRunnable::notifyFinished() { Q_EMIT finished(RunnableP(this)); }
void QtRunnable::notifyException() { Q_EMIT exception(RunnableP(this)); }
void QtRunnable::notifyCanceled() { Q_EMIT canceled(RunnableP(this)); }
void QtRunnable::notifyTerminated() { Q_EMIT terminated(RunnableP(this)); }

//! Convenience slot for the started() signal - so it's not necessary to declare
//! the task in a header file for moc'ing each time. You must both reimplement
//! \b and connect it to the started() signal to make it work.
void QtRunnable::onStarted(RunnableP) {}

//! The analogous of onStarted() for the finished() signal.
void QtRunnable::onFinished(RunnableP) {}

//! The analogous of onStarted() for the exception() signal.
void QtRunnable::onException(RunnableP) {}

//! The analogous of onStarted() for the canceled() signal.
void QtRunnable::onCanceled(RunnableP) {}

//! The analogous of onStarted() for the terminated() signal.
void QtRunnable::onTerminated(RunnableP) {}
