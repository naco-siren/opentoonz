

#include "tthreadmessage.h"
#include "tthreadp.h"

#include <thread>

namespace {
// The thread that runs the static initializers: the main thread, as
// QThread::currentThread() taken here was.
const std::thread::id mainThreadId = std::this_thread::get_id();
}  // namespace

TThreadMessageDispatcher
    *Dispatcher;  // MUST BE CREATED  in the main thread!!!!!!

//------------------------------------------------------------------------------

bool TThread::isMainThread() {
  return std::this_thread::get_id() == mainThreadId;
}

//------------------------------------------------------------------------------

TThreadMessageDispatcher::TThreadMessageDispatcher() {
  connect(this, SIGNAL(signaled(TThread::Message *)), this,
          SLOT(onSignal(TThread::Message *)));
  connect(this, SIGNAL(blockingSignaled(TThread::Message *)), this,
          SLOT(onSignal(TThread::Message *)), Qt::BlockingQueuedConnection);
}

//------------------------------------------------------------------------------

void TThreadMessageDispatcher::init() {
  if (!TThread::isMainThread()) return;
  if (Dispatcher == 0) Dispatcher = new TThreadMessageDispatcher();
}

//------------------------------------------------------------------------------

TThreadMessageDispatcher *TThreadMessageDispatcher::instance() {
  assert(Dispatcher);
  return Dispatcher;
}

//------------------------------------------------------------------------------

void TThreadMessageDispatcher::emitSignaled(TThread::Message *msg) {
  Q_EMIT signaled(msg);
}

//------------------------------------------------------------------------------

void TThreadMessageDispatcher::emitBlockingSignaled(TThread::Message *msg) {
  Q_EMIT blockingSignaled(msg);
}

//------------------------------------------------------------------------------

void TThreadMessageDispatcher::onSignal(TThread::Message *msg) {
  msg->onDeliver();
  delete msg;
}

//------------------------------------------------------------------------------

TThread::Message::Message() {}

//------------------------------------------------------------------------------
void TThread::Message::send() {
  if (isMainThread())
    onDeliver();
  else
    TThreadMessageDispatcher::instance()->emitSignaled(clone());
}

//------------------------------------------------------------------------------

void TThread::Message::sendBlocking() {
  if (isMainThread())
    onDeliver();
  else
    TThreadMessageDispatcher::instance()->emitBlockingSignaled(clone());
}

//------------------------------------------------------------------------------
