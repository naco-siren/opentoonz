// Step 9b of doc/nexttoonz_core_noqt.md: the main thread dispatcher and
// TThread::Message on it, in both builds (the Qt dispatcher itself is in
// test_tthreaddispatcher_qt.cpp). What the senders rely on: on the main
// thread a message is delivered at once; from another thread, on the main
// thread, in the order each thread sent, each copy delivered once and
// deleted; sendBlocking() returns after the delivery. Without a dispatcher
// (programs that never call TThread::init()) the work runs at once on the
// sending thread.

#include <gtest/gtest.h>

#include "tthreadmessage.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace {

// A dispatcher whose main thread runs the posted work when the test says.
class QueueDispatcher final : public TMainThreadDispatcher {
  std::mutex m_mutex;
  std::deque<std::function<void()>> m_tasks;

public:
  void post(std::function<void()> task) override {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasks.push_back(std::move(task));
  }
  bool isMainThread() const override { return TThread::isMainThread(); }
  void waitUntil(const std::function<bool()> &done) override {
    while (!done()) {
      if (!runOne()) std::this_thread::yield();
    }
  }
  bool runOne() {
    std::function<void()> task;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_tasks.empty()) return false;
      task = std::move(m_tasks.front());
      m_tasks.pop_front();
    }
    task();
    return true;
  }
  int pending() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return (int)m_tasks.size();
  }
};

struct Installed {
  explicit Installed(TMainThreadDispatcher *d) {
    TThread::setMainThreadDispatcher(d);
  }
  ~Installed() { TThread::setMainThreadDispatcher(nullptr); }
};

// What the messages saw.
std::mutex g_mutex;
std::map<int, std::vector<int>> g_delivered;  // sender -> sequence numbers
int g_offMain = 0;
std::atomic<int> g_alive(0);

void reset() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_delivered.clear();
  g_offMain = 0;
}

class Note final : public TThread::Message {
  int m_sender, m_seq;

public:
  Note(int sender, int seq) : m_sender(sender), m_seq(seq) { ++g_alive; }
  Note(const Note &other) : TThread::Message(), m_sender(other.m_sender), m_seq(other.m_seq) {
    ++g_alive;
  }
  ~Note() override { --g_alive; }
  TThread::Message *clone() const override { return new Note(*this); }
  void onDeliver() override {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_delivered[m_sender].push_back(m_seq);
    if (!TThread::isMainThread()) ++g_offMain;
  }
};

}  // namespace

TEST(TMainThreadDispatcher, WithoutOneWorkRunsAtOnceOnThePostingThread) {
  TThread::setMainThreadDispatcher(nullptr);
  std::thread::id ranOn;
  std::thread other([&] {
    bool ran = false;
    TThread::mainThreadDispatcher()->post([&] {
      ran   = true;
      ranOn = std::this_thread::get_id();
    });
    EXPECT_TRUE(ran);  // inside post()
  });
  std::thread::id otherId = other.get_id();
  other.join();
  EXPECT_EQ(ranOn, otherId);
}

TEST(TMainThreadDispatcher, WithoutOneAMessageIsDeliveredOnTheSendingThread) {
  TThread::setMainThreadDispatcher(nullptr);
  reset();
  std::thread other([] {
    Note(1, 1).send();
    Note(1, 2).sendBlocking();
  });
  other.join();
  EXPECT_EQ(g_delivered[1], (std::vector<int>{1, 2}));
  EXPECT_EQ(g_offMain, 2);
  EXPECT_EQ(g_alive.load(), 0);
}

TEST(TMainThreadDispatcher, OnTheMainThreadAMessageIsDeliveredAtOnce) {
  QueueDispatcher queue;
  Installed installed(&queue);
  reset();
  Note(7, 1).send();
  Note(7, 2).sendBlocking();
  EXPECT_EQ(g_delivered[7], (std::vector<int>{1, 2}));
  EXPECT_EQ(queue.pending(), 0);  // nothing was posted
}

TEST(TMainThreadDispatcher, MessagesFromOtherThreadsArriveInOrderOnTheMainThread) {
  QueueDispatcher queue;
  Installed installed(&queue);
  reset();
  const int senders = 8, each = 200;
  std::vector<std::thread> threads;
  for (int t = 0; t < senders; ++t)
    threads.emplace_back([t] {
      for (int i = 0; i < each; ++i) Note(t, i).send();
    });
  for (auto &th : threads) th.join();
  EXPECT_EQ(queue.pending(), senders * each);  // none delivered yet
  EXPECT_TRUE(g_delivered.empty());
  while (queue.runOne()) {
  }
  for (int t = 0; t < senders; ++t) {
    ASSERT_EQ((int)g_delivered[t].size(), each) << "sender " << t;
    for (int i = 0; i < each; ++i) EXPECT_EQ(g_delivered[t][i], i);
  }
  EXPECT_EQ(g_offMain, 0);
  EXPECT_EQ(g_alive.load(), 0);  // every copy deleted after its delivery
}

TEST(TMainThreadDispatcher, SendBlockingReturnsOnceTheMainThreadDelivered) {
  QueueDispatcher queue;
  Installed installed(&queue);
  reset();
  std::atomic<bool> returned(false);
  std::thread other([&] {
    Note(3, 1).sendBlocking();
    returned = true;
  });
  while (queue.pending() == 0) std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(returned) << "sendBlocking() returned before the delivery";
  EXPECT_TRUE(queue.runOne());
  other.join();
  EXPECT_TRUE(returned);
  EXPECT_EQ(g_delivered[3], (std::vector<int>{1}));
  EXPECT_EQ(g_offMain, 0);
}

TEST(TMainThreadDispatcher, TheDefaultWaitEndsWhenPostedWorkMakesItTrue) {
  TThread::setMainThreadDispatcher(nullptr);
  std::atomic<bool> done(false);
  std::thread other([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    TThread::mainThreadDispatcher()->post([&] { done = true; });
  });
  TThread::mainThreadDispatcher()->waitUntil([&] { return done.load(); });
  other.join();
  EXPECT_TRUE(done);
}
