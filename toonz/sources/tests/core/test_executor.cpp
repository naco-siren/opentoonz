// Characterization tests of TThread::Executor and TThread::Runnable
// (tthread.h): what the thread pool does, pinned down on its Qt
// implementation before step 9c of doc/nexttoonz_core_noqt.md moved the
// workers to std::thread, and since step 9d without Qt at all, in both builds
// and in the iPad simulator. The tasks log what the pool tells them through
// Runnable's notify*() hooks, called where QtRunnable emits its signals (the
// Qt side, and the delivery of those signals, is tested in
// tests/thread/test_qtrunnable.cpp).
//
// - tasks of one executor run by scheduling priority, then in the order they
//   were added; an executor runs one task at a time unless told otherwise;
// - setMaxActiveTasks() and setMaxActiveLoad() bound what runs at once;
// - removeTask() and cancelAll() notify canceled at once, in the calling
//   thread, take queued tasks out and leave running ones running;
// - dedicated persistent threads keep serving their executor only, and a
//   gone dedicated executor leaves no worker behind (known issue 45);
// - started and finished come from the worker around run(); an exception
//   notifies exception and not finished;
// - shutdown() cancels every task, the queued ones without running them,
//   notifies terminated for the running ones, and waits for every worker
//   thread to end (5 seconds at most).

#include <gtest/gtest.h>

#include "tthread.h"
#include "tthreadmessage.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// What the tasks and their signals did, in order.
struct Event {
  std::string what;
  int task;
  bool onMain;
  std::thread::id thread;
};

class Log {
  std::mutex m_mutex;
  std::vector<Event> m_events;

public:
  void add(const std::string &what, int task) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(
        {what, task, TThread::isMainThread(), std::this_thread::get_id()});
  }
  std::vector<Event> events() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_events;
  }
  void clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.clear();
  }
  int count(const std::string &what, int task = -1) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return (int)std::count_if(m_events.begin(), m_events.end(),
                              [&](const Event &e) {
                                return e.what == what &&
                                       (task < 0 || e.task == task);
                              });
  }
  // The tasks, in the order of their events of this kind.
  std::vector<int> order(const std::string &what) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<int> tasks;
    for (const Event &e : m_events)
      if (e.what == what) tasks.push_back(e.task);
    return tasks;
  }
  Event first(const std::string &what, int task) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const Event &e : m_events)
      if (e.what == what && e.task == task) return e;
    return {"", -1, false, std::thread::id()};
  }
  int index(const std::string &what, int task) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (size_t i = 0; i < m_events.size(); ++i)
      if (m_events[i].what == what && m_events[i].task == task) return (int)i;
    return -1;
  }
};

Log g_log;

// Waits until done() holds; false after ten seconds.
bool runEventsUntil(const std::function<bool()> &done) {
  const Clock::time_point end = Clock::now() + std::chrono::seconds(10);
  while (!done()) {
    if (Clock::now() > end) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

// Holds a task in run() until released.
class Gate {
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_open = false;

public:
  void wait() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_open; });
  }
  void release() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_open = true;
    m_cv.notify_all();
  }
};

// A task that logs what the pool tells it, in the thread it is told in.
class Probe final : public TThread::Runnable {
public:
  int m_n;
  int m_priority = 5;  // the default of Runnable::schedulingPriority()
  int m_load     = 0;
  std::function<void()> m_body;

  explicit Probe(int n) : m_n(n) {}
  void run() override {
    g_log.add("run", m_n);
    if (m_body) m_body();
  }
  int schedulingPriority() override { return m_priority; }
  int taskLoad() override { return m_load; }

private:
  void notifyStarted() override { g_log.add("started", m_n); }
  void notifyFinished() override { g_log.add("finished", m_n); }
  void notifyException() override { g_log.add("exception", m_n); }
  void notifyCanceled() override { g_log.add("canceled", m_n); }
  void notifyTerminated() override { g_log.add("terminated", m_n); }
};

Probe *watched(int n) { return new Probe(n); }

class ExecutorTest : public ::testing::Test {
protected:
  // the pool needs no more (TThread::init() also installs the Qt dispatcher
  // in the build with Qt; here, as in the build without Qt, there is none:
  // the main thread's work runs at once)
  static void SetUpTestSuite() { TThread::Executor::init(); }
  void SetUp() override { g_log.clear(); }
};

// Concurrency seen by tasks that stay a little while in run().
struct Overlap {
  std::atomic<int> active{0}, most{0}, done{0};
  std::function<void()> body() {
    return [this] {
      int now = ++active;
      int seen = most.load();
      while (now > seen && !most.compare_exchange_weak(seen, now)) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
      --active;
      ++done;
    };
  }
};

}  // namespace

//------------------------------------------------------------------------------

TEST_F(ExecutorTest, TasksRunByPriorityThenInTheOrderTheyWereAdded) {
  TThread::Executor ex;  // one task at a time by default
  EXPECT_EQ(ex.maxActiveTasks(), 1);
  Gate gate;
  std::vector<TThread::RunnableP> tasks;
  Probe *first = watched(0);
  first->m_body = [&] { gate.wait(); };
  tasks.push_back(first);
  ex.addTask(tasks.back());
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("run", 0) == 1; }));

  const int priorities[] = {5, 5, 9, 1, 9};
  for (int i = 0; i < 5; ++i) {
    Probe *p      = watched(i + 1);
    p->m_priority = priorities[i];
    tasks.push_back(p);
    ex.addTask(tasks.back());
  }
  gate.release();
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished") == 6; }));
  EXPECT_EQ(g_log.order("run"), (std::vector<int>{0, 3, 5, 1, 2, 4}));
  for (int i = 0; i < 6; ++i) EXPECT_FALSE(g_log.first("run", i).onMain);
}

TEST_F(ExecutorTest, MaxActiveTasksBoundsWhatRunsAtOnce) {
  TThread::Executor ex;
  ex.setMaxActiveTasks(2);
  EXPECT_EQ(ex.maxActiveTasks(), 2);
  Overlap overlap;
  std::vector<TThread::RunnableP> tasks;
  for (int i = 0; i < 6; ++i) {
    Probe *p  = watched(i);
    p->m_body = overlap.body();
    tasks.push_back(p);
    ex.addTask(tasks.back());
  }
  ASSERT_TRUE(runEventsUntil([&] { return overlap.done == 6; }));
  EXPECT_EQ(overlap.most.load(), 2);
}

TEST_F(ExecutorTest, ANonPositiveMaxActiveTasksMeansNoBound) {
  TThread::Executor ex;
  ex.setMaxActiveTasks(0);
  EXPECT_EQ(ex.maxActiveTasks(), (std::numeric_limits<int>::max)());
  ex.setMaxActiveTasks(-1);
  EXPECT_EQ(ex.maxActiveTasks(), (std::numeric_limits<int>::max)());
}

TEST_F(ExecutorTest, MaxActiveLoadBoundsTheSumOfTheLoads) {
  for (int load : {60, 50}) {
    TThread::Executor ex;
    ex.setMaxActiveTasks(-1);
    ex.setMaxActiveLoad(100);
    EXPECT_EQ(ex.maxActiveLoad(), 100);
    Overlap overlap;
    std::vector<TThread::RunnableP> tasks;
    for (int i = 0; i < 4; ++i) {
      Probe *p  = watched(i);
      p->m_load = load;
      p->m_body = overlap.body();
      tasks.push_back(p);
      ex.addTask(tasks.back());
    }
    ASSERT_TRUE(runEventsUntil([&] { return overlap.done == 4; }));
    EXPECT_EQ(overlap.most.load(), load == 60 ? 1 : 2) << "load " << load;
  }
}

TEST_F(ExecutorTest, RemoveTaskCancelsAQueuedTaskAndOnlySignalsARunningOne) {
  TThread::Executor ex, other;
  Gate gate;
  Probe *running = watched(0);
  running->m_body = [&] { gate.wait(); };
  TThread::RunnableP r(running), q1(watched(1)), q2(watched(2));
  ex.addTask(r);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("run", 0) == 1; }));
  ex.addTask(q1);
  ex.addTask(q2);

  // canceled() at once, in the calling thread
  ex.removeTask(q1);
  EXPECT_EQ(g_log.count("canceled", 1), 1);
  EXPECT_TRUE(g_log.first("canceled", 1).onMain);

  // another executor's task: nothing
  other.removeTask(q2);
  EXPECT_EQ(g_log.count("canceled", 2), 0);

  // a running task is told, and goes on
  ex.removeTask(r);
  EXPECT_EQ(g_log.count("canceled", 0), 1);
  EXPECT_TRUE(g_log.first("canceled", 0).onMain);
  gate.release();
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 2) == 1; }));
  EXPECT_EQ(g_log.count("finished", 0), 1);
  EXPECT_EQ(g_log.count("run", 1), 0) << "a removed task ran";
}

TEST_F(ExecutorTest, CancelAllCancelsThisExecutorsTasksOnly) {
  TThread::Executor a, b;
  Gate gateA, gateB;
  Probe *a0 = watched(0);
  a0->m_body = [&] { gateA.wait(); };
  Probe *b0 = watched(10);
  b0->m_body = [&] { gateB.wait(); };
  TThread::RunnableP ra0(a0), ra1(watched(1)), ra2(watched(2)), rb0(b0),
      rb1(watched(11));
  a.addTask(ra0);
  b.addTask(rb0);
  ASSERT_TRUE(runEventsUntil([] {
    return g_log.count("run", 0) == 1 && g_log.count("run", 10) == 1;
  }));
  a.addTask(ra1);
  a.addTask(ra2);
  b.addTask(rb1);

  a.cancelAll();
  // the running one first, then the queued ones as the queue is kept (of
  // equal priority, the latest first)
  EXPECT_EQ(g_log.order("canceled"), (std::vector<int>{0, 2, 1}));
  for (int n : {0, 1, 2}) EXPECT_TRUE(g_log.first("canceled", n).onMain);

  gateA.release();
  gateB.release();
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 11) == 1; }));
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 0) == 1; }));
  EXPECT_EQ(g_log.count("run", 1) + g_log.count("run", 2), 0);
  EXPECT_EQ(g_log.count("canceled", 10) + g_log.count("canceled", 11), 0);
}

TEST_F(ExecutorTest, DedicatedPersistentThreadsServeTheirExecutorOnly) {
  std::thread::id dedicatedThread;
  {
    TThread::Executor ex, other;
    ex.setDedicatedThreads(true, true);
    TThread::RunnableP t0(watched(0));
    ex.addTask(t0);
    ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 0) == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    dedicatedThread = g_log.first("run", 0).thread;

    // the thread sleeps for its executor and takes its next task
    TThread::RunnableP t1(watched(1));
    ex.addTask(t1);
    ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 1) == 1; }));
    EXPECT_EQ(g_log.first("run", 1).thread, dedicatedThread);

    // and not another executor's
    TThread::RunnableP t2(watched(2));
    other.addTask(t2);
    ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 2) == 1; }));
    EXPECT_NE(g_log.first("run", 2).thread, dedicatedThread);
  }
  // (the executors are gone: the sleeping thread is released)
}

// Known issue 45: the sleeping thread of a dedicated executor stayed in the
// pool's list of workers after the executor was gone and the thread deleted;
// cancelAll(), removeTask() and shutdown() of any executor then read it.
TEST_F(ExecutorTest, AGoneDedicatedExecutorLeavesNoWorkerBehind) {
  for (int round = 0; round < 5; ++round) {
    {
      TThread::Executor dedicated;
      dedicated.setDedicatedThreads(true, true);
      TThread::RunnableP t(watched(100 + round));
      dedicated.addTask(t);
      ASSERT_TRUE(runEventsUntil(
          [round] { return g_log.count("finished", 100 + round) == 1; }));
    }
    // the released thread ends and deletes itself
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TThread::Executor other;
    Gate gate;
    Probe *p  = watched(round);
    p->m_body = [&] { gate.wait(); };
    TThread::RunnableP r(p);
    other.addTask(r);
    ASSERT_TRUE(runEventsUntil([round] { return g_log.count("run", round) == 1; }));
    other.cancelAll();  // walks the workers
    other.removeTask(r);
    gate.release();
    ASSERT_TRUE(runEventsUntil([round] { return g_log.count("finished", round) == 1; }));
    EXPECT_EQ(g_log.count("canceled", round), 2);
  }
}

TEST_F(ExecutorTest, StartedAndFinishedComeFromTheWorkerAroundRun) {
  TThread::Executor ex;
  TThread::RunnableP t(watched(0));
  ex.addTask(t);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 0) == 1; }));
  int started = g_log.index("started", 0), run = g_log.index("run", 0),
      finished = g_log.index("finished", 0);
  EXPECT_LT(started, run);
  EXPECT_LT(run, finished);
  Event st = g_log.first("started", 0), r = g_log.first("run", 0),
        f = g_log.first("finished", 0);
  EXPECT_FALSE(st.onMain);
  EXPECT_EQ(st.thread, r.thread);
  EXPECT_EQ(f.thread, r.thread);
  EXPECT_EQ(g_log.count("exception"), 0);
}

TEST_F(ExecutorTest, AnExceptionNotifiesExceptionAndNotFinishedAndTheExecutorGoesOn) {
  TThread::Executor ex;
  Probe *thrower  = watched(0);
  thrower->m_body = [] { throw std::runtime_error("in run()"); };
  TThread::RunnableP t0(thrower), t1(watched(1));
  ex.addTask(t0);
  ex.addTask(t1);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 1) == 1; }));
  EXPECT_EQ(g_log.count("finished", 0), 0);
  EXPECT_EQ(g_log.count("exception", 0), 1);
  EXPECT_FALSE(g_log.first("exception", 0).onMain);
  EXPECT_EQ(g_log.first("exception", 0).thread, g_log.first("run", 0).thread);
  EXPECT_LT(g_log.index("started", 0), g_log.index("exception", 0));
  EXPECT_LT(g_log.index("exception", 0), g_log.index("run", 1));
}

// Last: Executor::shutdown() is for the whole process (no other core test
// uses the pool). Since the decision of 2026-11-20 (step 9 of
// doc/nexttoonz_core_noqt.md) it also waits until every worker thread has
// ended (5 seconds at most): here the running task is let go while shutdown()
// waits, and a dedicated executor's sleeping thread is released by it.
TEST_F(ExecutorTest, ZzShutdownCancelsEveryTaskAndWaitsForTheWorkersToEnd) {
  TThread::Executor ex, dedicated;
  dedicated.setDedicatedThreads(true, true);
  TThread::RunnableP d0(watched(20));
  dedicated.addTask(d0);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 20) == 1; }));

  Gate gate;
  Probe *running  = watched(0);
  running->m_body = [&] { gate.wait(); };
  TThread::RunnableP r(running), q1(watched(1)), q2(watched(2));
  ex.addTask(r);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("run", 0) == 1; }));
  ex.addTask(q1);
  ex.addTask(q2);
  EXPECT_GE(TThread::Executor::liveWorkerCount(), 2);

  // let the running task go a little after shutdown() starts waiting
  std::thread releaser([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    gate.release();
  });
  const Clock::time_point start = Clock::now();
  TThread::Executor::shutdown();
  releaser.join();
  const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                          Clock::now() - start)
                          .count();

  // the running one, then the queued ones (of equal priority, the latest
  // first), then terminated for the running one; all in the calling thread
  EXPECT_EQ(g_log.order("canceled"), (std::vector<int>{0, 2, 1}));
  EXPECT_EQ(g_log.order("terminated"), (std::vector<int>{0}));
  EXPECT_LT(g_log.index("canceled", 1), g_log.index("terminated", 0));
  for (int n : {0, 1, 2}) EXPECT_TRUE(g_log.first("canceled", n).onMain);
  EXPECT_TRUE(g_log.first("terminated", 0).onMain);

  // the running task was not stopped: it ended, and notified finished, before
  // shutdown() returned; every worker thread had ended, the sleeping
  // dedicated one too, well before the 5 seconds
  EXPECT_EQ(g_log.count("finished", 0), 1);
  EXPECT_EQ(TThread::Executor::liveWorkerCount(), 0);
  EXPECT_GE(waited, 90);
  EXPECT_LT(waited, 4000);
  EXPECT_EQ(g_log.count("run", 1) + g_log.count("run", 2), 0)
      << "a task queued at the shutdown ran";
}
