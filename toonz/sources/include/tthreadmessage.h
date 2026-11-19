#pragma once

#ifndef TTHREADMESSAGE_H
#define TTHREADMESSAGE_H

//! HOW TO USE: subclass TThread::Message (MyMessage, in example) defining what
//! to execute in onDeliver method and than, to send a message to be executed in
//! the main thread:
//  MyMessage().send();

#include <mutex>

#undef DVAPI
#undef DVVAR
#ifdef TNZCORE_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

#include "tcommon.h"

namespace TThread {

bool DVAPI isMainThread();

// This class is used for communication between different threads. Calling the
// 'send' method in a thread, the user defined method 'onDeliver' is executed in
// the mainThread.
// using 'sendblocking', the calling thread will block until the main thread has
// executed the onDeliver function.
// WARNING!! the sendblocking method will cause a deadlock if used in a
// executable without main loop! (such as composer, cleanupper, etc.)
// NOTE: if the 'send' is called in the main thread, then onDeliver is executed
// immediately.

class DVAPI Message {
public:
  Message();
  virtual ~Message(){};
  virtual Message *clone() const = 0;
  virtual void onDeliver()       = 0;

  void send();
  void sendBlocking();
};

//! A recursive mutex: the thread that holds it may lock it again, and must
//! unlock it as many times. It was a QMutex built with QMutex::Recursive,
//! which Qt 6 no longer has; the C++ library's recursive mutex has the same
//! semantics. It is Lockable, so std::lock_guard and std::unique_lock accept
//! it as well as MutexLocker.
class Mutex final {
public:
  Mutex() = default;

  void lock() { m_mutex.lock(); }
  bool tryLock() { return m_mutex.try_lock(); }
  void unlock() { m_mutex.unlock(); }

  // The names the C++ library's lock types expect.
  bool try_lock() { return m_mutex.try_lock(); }

private:
  std::recursive_mutex m_mutex;

  Mutex(const Mutex &)            = delete;
  Mutex &operator=(const Mutex &) = delete;
};

//! Locks a Mutex for its lifetime, like the QMutexLocker it replaces: a null
//! mutex is accepted and does nothing, and unlock() and relock() release and
//! retake the lock inside the scope.
class MutexLocker final {
public:
  explicit MutexLocker(Mutex *mutex) : m_mutex(mutex) {
    if (m_mutex) {
      m_mutex->lock();
      m_locked = true;
    }
  }
  ~MutexLocker() { unlock(); }

  void unlock() {
    if (m_locked) {
      m_locked = false;
      m_mutex->unlock();
    }
  }
  void relock() {
    if (m_mutex && !m_locked) {
      m_mutex->lock();
      m_locked = true;
    }
  }
  Mutex *mutex() const { return m_mutex; }

private:
  Mutex *m_mutex;
  bool m_locked = false;

  MutexLocker(const MutexLocker &)            = delete;
  MutexLocker &operator=(const MutexLocker &) = delete;
};

}  // namespace  TThread

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

#endif
