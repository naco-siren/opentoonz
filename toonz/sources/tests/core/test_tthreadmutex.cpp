// Unit tests for TThread::Mutex and TThread::MutexLocker (tthreadmessage.h).
//
// Until migration step 1 of doc/nexttoonz_core_noqt.md the mutex was a QMutex
// built with QMutex::Recursive and the locker was QMutexLocker; about a
// hundred places lock rasters, strokes, palettes, vector images and caches
// with them. These tests pin down the behaviour those places rely on, so
// that the C++ library mutex behind them now behaves as the Qt one did:
// recursion, exclusion between threads, and the locker's null mutex,
// unlock() and relock().

#include <gtest/gtest.h>

#include "tthreadmessage.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

// Whether another thread can take the mutex right now.
bool lockableFromAnotherThread(TThread::Mutex &mutex) {
  bool locked = false;
  std::thread other([&] {
    locked = mutex.tryLock();
    if (locked) mutex.unlock();
  });
  other.join();
  return locked;
}

}  // namespace

TEST(TThreadMutex, IsNotCopyable) {
  EXPECT_FALSE(std::is_copy_constructible<TThread::Mutex>::value);
  EXPECT_FALSE(std::is_copy_assignable<TThread::Mutex>::value);
  EXPECT_FALSE(std::is_copy_constructible<TThread::MutexLocker>::value);
}

TEST(TThreadMutex, TheOwnerCanLockItAgain) {
  TThread::Mutex mutex;
  mutex.lock();
  mutex.lock();
  EXPECT_TRUE(mutex.tryLock());
  mutex.unlock();
  mutex.unlock();
  // Still held once: no other thread gets it.
  EXPECT_FALSE(lockableFromAnotherThread(mutex));
  mutex.unlock();
  EXPECT_TRUE(lockableFromAnotherThread(mutex));
}

TEST(TThreadMutex, TryLockFailsWhileAnotherThreadHoldsIt) {
  TThread::Mutex mutex;
  EXPECT_TRUE(lockableFromAnotherThread(mutex));
  mutex.lock();
  EXPECT_FALSE(lockableFromAnotherThread(mutex));
  mutex.unlock();
  EXPECT_TRUE(lockableFromAnotherThread(mutex));
}

TEST(TThreadMutex, ExcludesOtherThreads) {
  TThread::Mutex mutex;
  long counter = 0;  // guarded by the mutex, deliberately not atomic
  std::atomic<int> inside{0};
  std::atomic<bool> overlapped{false};

  const int threads = 8, rounds = 20000;
  std::vector<std::thread> workers;
  for (int t = 0; t < threads; ++t)
    workers.emplace_back([&] {
      for (int i = 0; i < rounds; ++i) {
        TThread::MutexLocker locker(&mutex);
        if (inside.fetch_add(1) != 0) overlapped = true;
        ++counter;
        inside.fetch_sub(1);
      }
    });
  for (auto &w : workers) w.join();

  EXPECT_FALSE(overlapped.load());
  EXPECT_EQ(counter, long(threads) * rounds);
}

TEST(TThreadMutex, WorksWithTheStandardLocks) {
  TThread::Mutex mutex;
  {
    std::lock_guard<TThread::Mutex> guard(mutex);
    std::unique_lock<TThread::Mutex> again(mutex);  // recursive
    EXPECT_TRUE(again.owns_lock());
    EXPECT_FALSE(lockableFromAnotherThread(mutex));
  }
  EXPECT_TRUE(lockableFromAnotherThread(mutex));
}

TEST(TThreadMutexLocker, LocksForItsScope) {
  TThread::Mutex mutex;
  {
    TThread::MutexLocker locker(&mutex);
    EXPECT_EQ(locker.mutex(), &mutex);
    EXPECT_FALSE(lockableFromAnotherThread(mutex));
  }
  EXPECT_TRUE(lockableFromAnotherThread(mutex));
}

TEST(TThreadMutexLocker, NestsOnTheSameMutex) {
  TThread::Mutex mutex;
  {
    TThread::MutexLocker outer(&mutex);
    {
      TThread::MutexLocker inner(&mutex);
      EXPECT_FALSE(lockableFromAnotherThread(mutex));
    }
    EXPECT_FALSE(lockableFromAnotherThread(mutex));
  }
  EXPECT_TRUE(lockableFromAnotherThread(mutex));
}

TEST(TThreadMutexLocker, NullMutexDoesNothing) {
  // QMutexLocker accepted a null mutex, and callers rely on it
  // (`TThread::MutexLocker l(cond ? &m : 0)`).
  TThread::MutexLocker locker(nullptr);
  EXPECT_EQ(locker.mutex(), nullptr);
  locker.unlock();
  locker.relock();
}

TEST(TThreadMutexLocker, UnlockAndRelock) {
  // MovieRenderer releases the lock around saving a frame and takes it
  // back when the frame is saved.
  TThread::Mutex mutex;
  {
    TThread::MutexLocker locker(&mutex);
    locker.unlock();
    EXPECT_TRUE(lockableFromAnotherThread(mutex));
    locker.unlock();  // a second unlock does nothing
    EXPECT_TRUE(lockableFromAnotherThread(mutex));
    locker.relock();
    EXPECT_FALSE(lockableFromAnotherThread(mutex));
    locker.relock();  // a second relock does not lock twice
  }
  // The destructor released the one lock the locker held.
  EXPECT_TRUE(lockableFromAnotherThread(mutex));
}

TEST(TThreadMutexLocker, ADestroyedUnlockedLockerLeavesTheMutexFree) {
  TThread::Mutex mutex;
  {
    TThread::MutexLocker locker(&mutex);
    locker.unlock();
  }
  EXPECT_TRUE(lockableFromAnotherThread(mutex));
}
