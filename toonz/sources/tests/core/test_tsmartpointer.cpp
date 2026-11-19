// Unit tests for TSmartObject and TSmartPointerT (tsmartpointer.h), the
// reference counting under rasters, images, palettes, fxs and most of the
// scene model.
//
// tsmartpointer.cpp is one of the sources tnzcore is made of when it is
// built without Qt (doc/nexttoonz_core_noqt.md, migration step 1): its
// per-class instance counters are created under a TThread::Mutex, which was
// a QMutex until then. The counters exist in debug builds only
// (INSTANCE_COUNT_ENABLED), so the last test runs there only.

#include <gtest/gtest.h>

#include "tsmartpointer.h"

#include <atomic>
#include <thread>
#include <vector>

namespace {

std::atomic<int> g_destroyed{0};

class Counted final : public TSmartObject {
  DECLARE_CLASS_CODE

public:
  Counted() : TSmartObject(m_classCode) {}
  ~Counted() override { ++g_destroyed; }
};

// A class code no class of the tree uses (they use 1 to 122).
DEFINE_CLASS_CODE(Counted, 199)

typedef TSmartPointerT<Counted> CountedP;

}  // namespace

TEST(TSmartPointer, CountsReferencesAndDeletesWithTheLast) {
  g_destroyed = 0;
  CountedP a(new Counted);
  EXPECT_EQ(a->getRefCount(), 1);
  {
    CountedP b = a;
    CountedP c(b);
    EXPECT_EQ(a->getRefCount(), 3);
    EXPECT_TRUE(b == a);
  }
  EXPECT_EQ(a->getRefCount(), 1);
  EXPECT_EQ(g_destroyed.load(), 0);
  a = CountedP();
  EXPECT_FALSE(a);
  EXPECT_EQ(g_destroyed.load(), 1);
}

TEST(TSmartPointer, MovingDoesNotTouchTheCount) {
  g_destroyed = 0;
  CountedP a(new Counted);
  Counted *raw = a.getPointer();
  CountedP b(std::move(a));
  EXPECT_EQ(b.getPointer(), raw);
  EXPECT_EQ(raw->getRefCount(), 1);
  b = CountedP();
  EXPECT_EQ(g_destroyed.load(), 1);
}

TEST(TSmartPointer, CopiesFromManyThreadsKeepTheCount) {
  g_destroyed = 0;
  CountedP shared(new Counted);
  const int threads = 8, rounds = 20000;
  std::vector<std::thread> workers;
  for (int t = 0; t < threads; ++t)
    workers.emplace_back([&shared] {
      for (int i = 0; i < rounds; ++i) {
        CountedP copy = shared;
        CountedP another(copy);
      }
    });
  for (auto &w : workers) w.join();
  EXPECT_EQ(shared->getRefCount(), 1);
  EXPECT_EQ(g_destroyed.load(), 0);
  shared = CountedP();
  EXPECT_EQ(g_destroyed.load(), 1);
}

#ifdef INSTANCE_COUNT_ENABLED
TEST(TSmartPointer, InstanceCountFromManyThreads) {
  // The first object of a class creates its counter; with many threads
  // creating the first ones at once, exactly one counter must come out.
  const int threads = 8, objects = 1000;
  std::vector<std::thread> workers;
  std::atomic<bool> go{false};
  for (int t = 0; t < threads; ++t)
    workers.emplace_back([&] {
      while (!go) std::this_thread::yield();
      std::vector<CountedP> mine;
      for (int i = 0; i < objects; ++i) mine.push_back(new Counted);
    });
  go = true;
  for (auto &w : workers) w.join();
  EXPECT_EQ(Counted::getInstanceCount(), 0);

  CountedP one(new Counted), two(new Counted);
  EXPECT_EQ(Counted::getInstanceCount(), 2);
}
#endif
