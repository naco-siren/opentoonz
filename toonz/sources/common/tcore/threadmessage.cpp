

#include "tthreadmessage.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <memory>
#include <thread>

// The main thread dispatcher and TThread::Message, without Qt (step 9b of
// doc/nexttoonz_core_noqt.md). The Qt dispatcher that TThread::init()
// installs in the build with Qt is in threadmessage_qt.cpp.

namespace {

// The thread that runs the static initializers: the main thread, as
// QThread::currentThread() taken here was.
const std::thread::id mainThreadId = std::this_thread::get_id();

// Without a dispatcher: the work runs at once on the posting thread. A wait
// on the main thread wakes each time some work has run, and every few
// milliseconds in any case.
class ImmediateDispatcher final : public TMainThreadDispatcher {
  std::mutex m_mutex;
  std::condition_variable m_ran;

public:
  void post(std::function<void()> task) override {
    task();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ran.notify_all();
  }
  bool isMainThread() const override { return TThread::isMainThread(); }
  void waitUntil(const std::function<bool()> &done) override {
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!done()) m_ran.wait_for(lock, std::chrono::milliseconds(5));
  }
};

ImmediateDispatcher immediateDispatcher;
std::atomic<TMainThreadDispatcher *> installed(nullptr);

}  // namespace

//------------------------------------------------------------------------------

bool TThread::isMainThread() {
  return std::this_thread::get_id() == mainThreadId;
}

void TThread::setMainThreadDispatcher(TMainThreadDispatcher *dispatcher) {
  installed = dispatcher;
}

TMainThreadDispatcher *TThread::mainThreadDispatcher() {
  TMainThreadDispatcher *dispatcher = installed;
  return dispatcher ? dispatcher : &immediateDispatcher;
}

//------------------------------------------------------------------------------

TThread::Message::Message() {}

//------------------------------------------------------------------------------

void TThread::Message::send() {
  TMainThreadDispatcher *dispatcher = mainThreadDispatcher();
  if (dispatcher->isMainThread()) {
    onDeliver();
    return;
  }
  // a copy, delivered and deleted on the main thread, as the queued signal
  // carried it
  std::shared_ptr<Message> msg(clone());
  dispatcher->post([msg] { msg->onDeliver(); });
}

//------------------------------------------------------------------------------

void TThread::Message::sendBlocking() {
  TMainThreadDispatcher *dispatcher = mainThreadDispatcher();
  if (dispatcher->isMainThread()) {
    onDeliver();
    return;
  }
  // as Qt::BlockingQueuedConnection: queued on the main thread, and this
  // thread waits until it has been delivered
  std::shared_ptr<Message> msg(clone());
  auto delivered = std::make_shared<std::promise<void>>();
  std::future<void> done = delivered->get_future();
  dispatcher->post([msg, delivered] {
    try {
      msg->onDeliver();
    } catch (...) {
      delivered->set_value();  // the sender goes on; the main thread sees it
      throw;
    }
    delivered->set_value();
  });
  done.wait();
}
