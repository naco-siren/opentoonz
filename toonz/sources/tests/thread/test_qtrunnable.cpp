// The Qt side of the thread pool (step 9d of doc/nexttoonz_core_noqt.md):
// TThread::QtRunnable, the task that is a QObject, as Runnable was until
// then. What the programs' tasks rely on, as before:
//
// - started(), finished() and exception() are emitted in the worker thread
//   around run(); connected to the task's own slots they reach them in the
//   main thread (the task lives there), queued;
// - the old string connections to the on*() slots, reimplemented, work;
// - canceled() is emitted in the thread that cancels, and a connection to
//   another object (as IconRenderer to IconGenerator) can queue it;
// - runningPriority() gives the worker the scheduling QThread::setPriority()
//   gives a thread (decision Q9-C), the reference being Qt itself;
// - shutdown() with the Qt dispatcher that TThread::init() installs: the
//   queued slots run inside it, and it waits for the workers, processing
//   events (a timer lets the running task go).
//
// The pool itself is tested without Qt in tests/core/test_executor.cpp. This
// binary makes its QCoreApplication and calls TThread::init()
// (thread_test_main.cpp); shutdown() is for the whole process and comes
// last.

#include <gtest/gtest.h>

#include "tthread.h"
#include "tthread_qt.h"
#include "tthreadmessage.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <pthread.h>

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
  void clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.clear();
  }
  int count(const std::string &what, int task = -1) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int n = 0;
    for (const Event &e : m_events)
      if (e.what == what && (task < 0 || e.task == task)) ++n;
    return n;
  }
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

bool runEventsUntil(const std::function<bool()> &done) {
  const Clock::time_point end = Clock::now() + std::chrono::seconds(10);
  while (!done()) {
    if (Clock::now() > end) return false;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

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

class QtProbe final : public TThread::QtRunnable {
public:
  int m_n;
  TThread::Priority m_running = TThread::NormalPriority;
  std::function<void()> m_body;

  explicit QtProbe(int n) : m_n(n) {}
  void run() override {
    g_log.add("run", m_n);
    if (m_body) m_body();
  }
  TThread::Priority runningPriority() override { return m_running; }
};

// Every signal logged where it is emitted (direct connections), and finished()
// and exception() also where the task's own slots receive them (the task as
// context: it lives in the main thread).
QtProbe *watched(int n) {
  QtProbe *p = new QtProbe(n);
  QObject::connect(p, &TThread::QtRunnable::started,
                   [n](TThread::RunnableP) { g_log.add("started", n); });
  QObject::connect(p, &TThread::QtRunnable::finished,
                   [n](TThread::RunnableP) { g_log.add("finished", n); });
  QObject::connect(p, &TThread::QtRunnable::exception,
                   [n](TThread::RunnableP) { g_log.add("exception", n); });
  QObject::connect(p, &TThread::QtRunnable::canceled,
                   [n](TThread::RunnableP) { g_log.add("canceled", n); });
  QObject::connect(p, &TThread::QtRunnable::terminated,
                   [n](TThread::RunnableP) { g_log.add("terminated", n); });
  QObject::connect(p, &TThread::QtRunnable::finished, p,
                   [n](TThread::RunnableP) { g_log.add("finished-slot", n); });
  QObject::connect(p, &TThread::QtRunnable::exception, p,
                   [n](TThread::RunnableP) { g_log.add("exception-slot", n); });
  return p;
}

}  // namespace

// The way most tasks of the programs are written: reimplemented on*() slots,
// connected by name to the signals of the task itself.
class SelfConnected final : public TThread::QtRunnable {
  Q_OBJECT

public:
  int m_n;
  explicit SelfConnected(int n) : m_n(n) {
    connect(this, SIGNAL(started(TThread::RunnableP)), this,
            SLOT(onStarted(TThread::RunnableP)));
    connect(this, SIGNAL(finished(TThread::RunnableP)), this,
            SLOT(onFinished(TThread::RunnableP)));
    connect(this, SIGNAL(canceled(TThread::RunnableP)), this,
            SLOT(onCanceled(TThread::RunnableP)));
  }
  void run() override { g_log.add("run", m_n); }
  void onStarted(TThread::RunnableP) override { g_log.add("onStarted", m_n); }
  void onFinished(TThread::RunnableP) override {
    g_log.add("onFinished", m_n);
  }
  void onCanceled(TThread::RunnableP) override {
    g_log.add("onCanceled", m_n);
  }
};

namespace {

class QtRunnableTest : public ::testing::Test {
protected:
  void SetUp() override { g_log.clear(); }
  void TearDown() override { QCoreApplication::processEvents(); }
};

}  // namespace

TEST_F(QtRunnableTest, SignalsComeFromTheWorkerAroundRunAndReachTheSlotsInTheMainThread) {
  TThread::Executor ex;
  TThread::RunnableP t(watched(0));
  ex.addTask(t);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished-slot", 0) == 1; }));
  EXPECT_LT(g_log.index("started", 0), g_log.index("run", 0));
  EXPECT_LT(g_log.index("run", 0), g_log.index("finished", 0));
  EXPECT_LT(g_log.index("finished", 0), g_log.index("finished-slot", 0));
  Event s = g_log.first("started", 0), r = g_log.first("run", 0),
        f = g_log.first("finished", 0), fs = g_log.first("finished-slot", 0);
  EXPECT_FALSE(s.onMain);
  EXPECT_EQ(s.thread, r.thread);
  EXPECT_EQ(f.thread, r.thread);
  EXPECT_TRUE(fs.onMain);
}

TEST_F(QtRunnableTest, AnExceptionEmitsExceptionNotFinishedAndReachesTheSlotInTheMainThread) {
  TThread::Executor ex;
  QtProbe *thrower  = watched(0);
  thrower->m_body = [] { throw std::runtime_error("in run()"); };
  TThread::RunnableP t(thrower);
  ex.addTask(t);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("exception-slot", 0) == 1; }));
  EXPECT_EQ(g_log.count("finished", 0), 0);
  EXPECT_FALSE(g_log.first("exception", 0).onMain);
  EXPECT_TRUE(g_log.first("exception-slot", 0).onMain);
}

TEST_F(QtRunnableTest, StringConnectionsToReimplementedSlotsWork) {
  TThread::Executor ex;
  TThread::RunnableP t(new SelfConnected(0));
  ex.addTask(t);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("onFinished", 0) == 1; }));
  EXPECT_TRUE(g_log.first("onStarted", 0).onMain);
  EXPECT_TRUE(g_log.first("onFinished", 0).onMain);
  EXPECT_LT(g_log.index("onStarted", 0), g_log.index("onFinished", 0));

  // canceled() in the cancelling thread, to the task's own slot at once
  // (both in the main thread)
  Gate gate;
  QtProbe *blocker  = watched(1);
  blocker->m_body = [&] { gate.wait(); };
  TThread::RunnableP b(blocker), queued(new SelfConnected(2));
  ex.addTask(b);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("run", 1) == 1; }));
  ex.addTask(queued);
  ex.removeTask(queued);
  EXPECT_EQ(g_log.count("onCanceled", 2), 1);
  EXPECT_TRUE(g_log.first("onCanceled", 2).onMain);
  gate.release();
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 1) == 1; }));
  EXPECT_EQ(g_log.count("run", 2), 0);
}

TEST_F(QtRunnableTest, ACanceledConnectedToAnotherObjectCanBeQueued) {
  // as IconRenderer connects canceled() to IconGenerator
  QObject receiver;
  TThread::Executor ex;
  Gate gate;
  QtProbe *blocker  = watched(0);
  blocker->m_body = [&] { gate.wait(); };
  QtProbe *queued = watched(1);
  QObject::connect(queued, &TThread::QtRunnable::canceled, &receiver,
                   [](TThread::RunnableP) { g_log.add("canceled-queued", 1); },
                   Qt::QueuedConnection);
  TThread::RunnableP b(blocker), q(queued);
  ex.addTask(b);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("run", 0) == 1; }));
  ex.addTask(q);
  ex.cancelAll();
  EXPECT_EQ(g_log.count("canceled", 1), 1);         // emitted now
  EXPECT_EQ(g_log.count("canceled-queued", 1), 0);  // delivered later
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("canceled-queued", 1) == 1; }));
  gate.release();
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("finished", 0) == 1; }));
}

namespace {

struct SchedParams {
  int policy   = -1;
  int priority = -1;
};

SchedParams currentSchedParams() {
  SchedParams p;
  sched_param param;
  if (pthread_getschedparam(pthread_self(), &p.policy, &param) == 0)
    p.priority = param.sched_priority;
  return p;
}

// What QThread::setPriority() gives a thread, called in the thread itself, as
// the workers did before step 9c.
class ReferenceThread final : public QThread {
public:
  QThread::Priority m_priority;
  SchedParams m_params;
  explicit ReferenceThread(QThread::Priority p) : m_priority(p) {}
  void run() override {
    setPriority(m_priority);
    m_params = currentSchedParams();
  }
};

}  // namespace

TEST_F(QtRunnableTest, RunningPriorityGivesTheWorkerWhatQThreadSetPriorityGives) {
  const QThread::Priority priorities[] = {
      QThread::IdlePriority,    QThread::LowestPriority,
      QThread::LowPriority,     QThread::NormalPriority,
      QThread::HighPriority,    QThread::HighestPriority,
      QThread::TimeCriticalPriority};
  for (QThread::Priority priority : priorities) {
    ReferenceThread reference(priority);
    reference.start();
    reference.wait();

    TThread::Executor ex;
    std::mutex mutex;
    SchedParams seen;
    std::atomic<bool> done(false);
    QtProbe *p   = new QtProbe(0);
    p->m_running = TThread::toPriority(priority);
    p->m_body    = [&] {
      std::lock_guard<std::mutex> lock(mutex);
      seen = currentSchedParams();
      done = true;
    };
    TThread::RunnableP t(p);
    ex.addTask(t);
    ASSERT_TRUE(runEventsUntil([&] { return done.load(); }));
    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_EQ(seen.policy, reference.m_params.policy) << "priority " << priority;
    EXPECT_EQ(seen.priority, reference.m_params.priority)
        << "priority " << priority;
  }
}

// Last: Executor::shutdown() is for the whole process.
TEST_F(QtRunnableTest, ZzShutdownWithTheQtDispatcher) {
  TThread::Executor ex;
  Gate gate;
  QtProbe *running = watched(0);
  running->m_body  = [&] { gate.wait(); };
  QtProbe *queued  = watched(1);
  QObject::connect(queued, &TThread::QtRunnable::canceled, queued,
                   [](TThread::RunnableP) { g_log.add("canceled-slot", 1); },
                   Qt::QueuedConnection);
  TThread::RunnableP r(running), q(queued);
  ex.addTask(r);
  ASSERT_TRUE(runEventsUntil([] { return g_log.count("run", 0) == 1; }));
  ex.addTask(q);

  QTimer::singleShot(100, [&] { gate.release(); });
  TThread::Executor::shutdown();

  EXPECT_EQ(g_log.order("canceled"), (std::vector<int>{0, 1}));
  EXPECT_EQ(g_log.order("terminated"), (std::vector<int>{0}));
  // the queued slot ran inside shutdown(), as with its processEvents()
  EXPECT_EQ(g_log.count("canceled-slot", 1), 1);
  EXPECT_EQ(g_log.count("finished", 0), 1);
  EXPECT_EQ(TThread::Executor::liveWorkerCount(), 0);
  EXPECT_EQ(g_log.count("run", 1), 0);
}

#include "test_qtrunnable.moc"
