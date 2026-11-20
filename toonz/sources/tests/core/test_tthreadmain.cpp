// Step 9a of doc/nexttoonz_core_noqt.md: TThread::isMainThread() and the
// locks of TMetaImage, which went from Qt (QThread::currentThread() taken at
// static initialization; QReadWriteLock with QReadLocker and QWriteLocker) to
// the C++ library. What the callers rely on: the main thread is the one that
// ran the static initializers, readers share the image, a writer has it to
// itself.

#include <gtest/gtest.h>

#include "tmetaimage.h"
#include "tthreadmessage.h"

#include <atomic>
#include <chrono>
#include <thread>

TEST(TThreadMain, TheMainThreadIsTheOneThatStartedTheProgram) {
  EXPECT_TRUE(TThread::isMainThread());
  bool inOther = true;
  std::thread other([&] { inOther = TThread::isMainThread(); });
  other.join();
  EXPECT_FALSE(inOther);
}

TEST(TMetaImageLocks, ReadersShareTheImage) {
  TMetaImage image;
  TMetaImage::Reader mine(image);
  bool read = false;
  std::thread other([&] {
    TMetaImage::Reader theirs(image);  // does not wait for mine
    read = true;
  });
  other.join();
  EXPECT_TRUE(read);
}

TEST(TMetaImageLocks, AWriterWaitsForTheReaders) {
  TMetaImage image;
  std::atomic<bool> written(false);
  std::thread writer;
  {
    TMetaImage::Reader mine(image);
    writer = std::thread([&] {
      TMetaImage::Writer w(image);
      written = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(written) << "the writer got in while a reader held the image";
  }
  writer.join();
  EXPECT_TRUE(written);
}

TEST(TMetaImageLocks, ReadersWaitForTheWriter) {
  TMetaImage image;
  std::atomic<bool> read(false);
  std::thread reader;
  {
    TMetaImage::Writer mine(image);
    reader = std::thread([&] {
      TMetaImage::Reader r(image);
      read = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(read) << "a reader got in while the writer held the image";
  }
  reader.join();
  EXPECT_TRUE(read);
}
