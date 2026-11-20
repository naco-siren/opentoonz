#pragma once

#ifndef TTHREAD_QT_H
#define TTHREAD_QT_H

// The Qt side of TThread::Runnable (step 9d of doc/nexttoonz_core_noqt.md):
// a task that is a QObject and emits Qt signals, as Runnable itself did until
// then. In tnzcore's build with Qt for now (decision Q9-A); it leaves tnzcore
// with the other Qt adapters at step 20.

#include "tthread.h"

#include <QObject>
#include <QThread>

#undef DVAPI
#undef DVVAR
#ifdef TNZCORE_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

namespace TThread {

//! A Runnable that is a QObject: the task manager's notifications are its
//! signals, emitted where and when Runnable's notify*() are called (see
//! there): started(), finished() and exception() in the worker thread,
//! canceled() and terminated() in the controlling one, with the manager's
//! mutex held. Connected to slots of objects that live in another thread
//! (usually the main one) they are queued there, as before.
//!
//! The on*() slots do nothing; reimplement them and connect them to the
//! signals, as before.
class DVAPI QtRunnable : public QObject, public Runnable {
  Q_OBJECT

public:
  QtRunnable();
  ~QtRunnable() override;

Q_SIGNALS:

  void started(TThread::RunnableP sender);
  void finished(TThread::RunnableP sender);
  void exception(TThread::RunnableP sender);
  void canceled(TThread::RunnableP sender);
  void terminated(TThread::RunnableP sender);

public Q_SLOTS:

  virtual void onStarted(TThread::RunnableP sender);
  virtual void onFinished(TThread::RunnableP sender);
  virtual void onException(TThread::RunnableP sender);
  virtual void onCanceled(TThread::RunnableP sender);
  virtual void onTerminated(TThread::RunnableP sender);

private:
  void notifyStarted() override;
  void notifyFinished() override;
  void notifyException() override;
  void notifyCanceled() override;
  void notifyTerminated() override;
};

//! QThread::Priority for a Runnable::runningPriority() that was written with
//! it (the values are the same).
inline Priority toPriority(QThread::Priority p) {
  return static_cast<Priority>(static_cast<int>(p));
}

}  // namespace TThread

#endif  // TTHREAD_QT_H
