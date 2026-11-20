// Step 9b of doc/nexttoonz_core_noqt.md: the Qt dispatcher of the build with
// Qt (threadmessage_qt.cpp), which TThread::init() installs. It must do what
// the queued and blocking-queued signals of TThread::Message did: deliver on
// the main thread's event loop, in each sender's order, after what was
// already in the queue, and let sendBlocking() return once delivered; and
// waitUntil() must process events while it waits, as TRenderer does.

#include <gtest/gtest.h>

#include "tthreadmessage.h"
#include "../../common/tcore/tthreadp.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace {

std::mutex g_mutex;
std::vector<std::pair<int, int>> g_order;  // (sender, sequence) as delivered
int g_offMain = 0;

class Mark final : public TThread::Message {
  int m_sender, m_seq;

public:
  Mark(int sender, int seq) : m_sender(sender), m_seq(seq) {}
  TThread::Message *clone() const override { return new Mark(*this); }
  void onDeliver() override {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_order.push_back({m_sender, m_seq});
    if (!TThread::isMainThread()) ++g_offMain;
  }
};

// A QCoreApplication for these tests only: test_tsystem_qt.cpp's
// TSystemNoQCoreApplication needs there to be none afterwards.
class QtDispatcher : public ::testing::Test {
protected:
  QCoreApplication *m_app = nullptr;
  TThreadMessageDispatcher *m_dispatcher = nullptr;

  void SetUp() override {
    static int argc      = 1;
    static char arg0[]   = "nexttoonz_core_tests";
    static char *argv[]  = {arg0, nullptr};
    if (!QCoreApplication::instance()) m_app = new QCoreApplication(argc, argv);
    m_dispatcher = new TThreadMessageDispatcher();
    TThread::setMainThreadDispatcher(m_dispatcher);
    g_order.clear();
    g_offMain = 0;
  }
  void TearDown() override {
    TThread::setMainThreadDispatcher(nullptr);
    QCoreApplication::processEvents();
    delete m_dispatcher;
    delete m_app;
  }
};

}  // namespace

TEST_F(QtDispatcher, MessagesWaitForTheEventLoopAndKeepEachSendersOrder) {
  const int senders = 4, each = 100;
  std::vector<std::thread> threads;
  for (int t = 0; t < senders; ++t)
    threads.emplace_back([t] {
      for (int i = 0; i < each; ++i) Mark(t, i).send();
    });
  for (auto &th : threads) th.join();
  EXPECT_TRUE(g_order.empty()) << "delivered before the event loop ran";
  QCoreApplication::processEvents();
  ASSERT_EQ((int)g_order.size(), senders * each);
  std::map<int, int> next;
  for (auto &d : g_order) EXPECT_EQ(d.second, next[d.first]++);
  EXPECT_EQ(g_offMain, 0);
}

TEST_F(QtDispatcher, PostedWorkComesAfterEventsAlreadyQueued) {
  std::vector<int> seen;
  QTimer::singleShot(0, m_dispatcher, [&] { seen.push_back(1); });
  std::thread other([&] { m_dispatcher->post([&] { seen.push_back(2); }); });
  other.join();
  QCoreApplication::processEvents();
  EXPECT_EQ(seen, (std::vector<int>{1, 2}));
}

TEST_F(QtDispatcher, SendBlockingReturnsOnceTheEventLoopDelivered) {
  std::atomic<bool> returned(false);
  std::thread other([&] {
    Mark(9, 1).sendBlocking();
    returned = true;
    m_dispatcher->post([] {});  // wakes the wait (see waitUntil())
  });
  // the main thread waits through the dispatcher, processing events
  m_dispatcher->waitUntil([&] { return returned.load(); });
  other.join();
  ASSERT_EQ(g_order.size(), 1u);
  EXPECT_EQ(g_order[0], std::make_pair(9, 1));
  EXPECT_EQ(g_offMain, 0);
}

TEST_F(QtDispatcher, TThreadInitInstallsTheQtDispatcherOnTheMainThread) {
  TThreadMessageDispatcher::init();
  EXPECT_EQ(TThread::mainThreadDispatcher(),
            static_cast<TMainThreadDispatcher *>(
                TThreadMessageDispatcher::instance()));
}
