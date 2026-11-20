// Tests of TThread::Executor and TThread::Runnable (tthread.h). This first
// one pins down known issue 45, fixed with it; the characterization tests of
// step 9c of doc/nexttoonz_core_noqt.md follow.

#include <gtest/gtest.h>

#include "tthread.h"
#include "tthreadmessage.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
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

// Processes the main thread's events until done() holds; false after ten
// seconds.
bool runEventsUntil(const std::function<bool()> &done) {
  const Clock::time_point end = Clock::now() + std::chrono::seconds(10);
  while (!done()) {
    if (Clock::now() > end) return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
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

class Probe final : public TThread::Runnable {
public:
  int m_n;
  int m_priority = 5;  // the default of Runnable::schedulingPriority()
  int m_load     = 0;
  QThread::Priority m_running = QThread::NormalPriority;
  std::function<void()> m_body;

  explicit Probe(int n) : m_n(n) {}
  void run() override {
    g_log.add("run", m_n);
    if (m_body) m_body();
  }
  int schedulingPriority() override { return m_priority; }
  int taskLoad() override { return m_load; }
  QThread::Priority runningPriority() override { return m_running; }
};

// A task whose signals are all logged: where they are emitted (direct
// connections), and finished() and exception() also where the task's own
// slots receive them (connections with the task as context, which live in
// the main thread, as the subclasses connect them).
Probe *watched(int n) {
  Probe *p = new Probe(n);
  QObject::connect(p, &TThread::Runnable::started,
                   [n](TThread::RunnableP) { g_log.add("started", n); });
  QObject::connect(p, &TThread::Runnable::finished,
                   [n](TThread::RunnableP) { g_log.add("finished", n); });
  QObject::connect(p, &TThread::Runnable::exception,
                   [n](TThread::RunnableP) { g_log.add("exception", n); });
  QObject::connect(p, &TThread::Runnable::canceled,
                   [n](TThread::RunnableP) { g_log.add("canceled", n); });
  QObject::connect(p, &TThread::Runnable::terminated,
                   [n](TThread::RunnableP) { g_log.add("terminated", n); });
  QObject::connect(p, &TThread::Runnable::finished, p,
                   [n](TThread::RunnableP) { g_log.add("finished-slot", n); });
  QObject::connect(p, &TThread::Runnable::exception, p,
                   [n](TThread::RunnableP) { g_log.add("exception-slot", n); });
  return p;
}

class ExecutorTest : public ::testing::Test {
protected:
  void SetUp() override { g_log.clear(); }
  void TearDown() override {
    // let the workers of this test finish and the queued signals arrive
    QCoreApplication::processEvents();
  }
};

}  // namespace

//------------------------------------------------------------------------------

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
    // the released thread ends and is deleted (a queued slot)
    const Clock::time_point until = Clock::now() + std::chrono::milliseconds(100);
    while (Clock::now() < until)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

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

// Last: Executor::shutdown() is for the whole process.