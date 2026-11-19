// Whether TImageCache is on is per thread (step 8d-0 of
// doc/nexttoonz_core_noqt.md: a thread_local where a QThreadStorage was): a
// thread that never said is on; turning it off in one thread leaves the
// others as they were.

#include <gtest/gtest.h>

#include "timagecache.h"

#include <atomic>
#include <thread>
#include <vector>

TEST(TImageCacheEnabled, IsPerThread) {
  TImageCache *cache = TImageCache::instance();
  EXPECT_TRUE(cache->isEnabled());
  cache->setEnabled(false);
  EXPECT_FALSE(cache->isEnabled());

  std::atomic<int> enabledElsewhere{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
    threads.emplace_back([&, i] {
      if (cache->isEnabled()) ++enabledElsewhere;
      cache->setEnabled(i % 2 == 0);
      cache->setEnabled(i % 2 == 0);  // the same again: nothing changes
      EXPECT_EQ(cache->isEnabled(), i % 2 == 0);
    });
  for (std::thread &t : threads) t.join();
  EXPECT_EQ(enabledElsewhere.load(), 8);

  EXPECT_FALSE(cache->isEnabled());
  cache->setEnabled(true);
  EXPECT_TRUE(cache->isEnabled());
}
