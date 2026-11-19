

#include "tsmartpointer.h"
#include "tthreadmessage.h"

#include <atomic>

//-------------------------------------------------------------------

namespace {

//-------------------------------------------------------------------

typedef TAtomicVar *TAtomicVarPtr;

const int maxClassCode = 200;
// Atomic pointers: the double-checked lock below reads them unlocked
// (doc/nexttoonz_core_noqt.md, 8d).
std::atomic<TAtomicVarPtr> instanceCounts[maxClassCode + 1];

//-------------------------------------------------------------------

inline TAtomicVar &getInstanceCounter(TINT32 classCode) {
  assert(0 <= classCode && classCode <= maxClassCode);
  std::atomic<TAtomicVarPtr> &instanceCountPtr = instanceCounts[classCode];
  TAtomicVarPtr counter = instanceCountPtr.load(std::memory_order_acquire);
  if (counter == 0) {
    static TThread::Mutex mutex;
    TThread::MutexLocker g(&mutex);
    counter = instanceCountPtr.load(std::memory_order_relaxed);
    if (counter == 0) {
      counter = new TAtomicVar();
      instanceCountPtr.store(counter, std::memory_order_release);
    }
  }
  assert(counter);
  return *counter;
}

//-------------------------------------------------------------------

}  // namespace

//-------------------------------------------------------------------

#ifdef INSTANCE_COUNT_ENABLED
const TINT32 TSmartObject::m_unknownClassCode = 0;
#endif

void TSmartObject::incrementInstanceCount() {
#ifdef INSTANCE_COUNT_ENABLED
  TAtomicVar &instanceCount = getInstanceCounter(m_classCodeRef);
  ++instanceCount;
#else
  assert(0);
#endif
}

//-------------------------------------------------------------------

void TSmartObject::decrementInstanceCount() {
#ifdef INSTANCE_COUNT_ENABLED
  TAtomicVar &instanceCount = getInstanceCounter(m_classCodeRef);
  assert(instanceCount > 0);
  --instanceCount;
#else
  assert(0);
#endif
}

//-------------------------------------------------------------------

TINT32 TSmartObject::getInstanceCount(ClassCode
#ifdef INSTANCE_COUNT_ENABLED
                                          code
#endif
                                      ) {
#ifdef INSTANCE_COUNT_ENABLED
  TAtomicVar &instanceCount = getInstanceCounter(code);
  return instanceCount;
#else
  assert(0);
  return 0;
#endif
}

//-------------------------------------------------------------------
