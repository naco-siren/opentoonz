// Level formats and LZO after step 8d-0 of doc/nexttoonz_core_noqt.md: the
// format tables keyed by std::string list the formats in the order the
// QString keys gave, and TRasterCodecLZO goes through the service it is
// given.

#include <gtest/gtest.h>

#include "tcodec.h"
#include "timage_io.h"
#include "tlevel_io.h"
#include "traster.h"

#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

void expectQStringOrder(const QStringList &formats) {
  QStringList sorted = formats;
  std::stable_sort(sorted.begin(), sorted.end());
  EXPECT_EQ(formats, sorted);
  EXPECT_FALSE(formats.isEmpty());
}

}  // namespace

TEST(LevelFormats, AreListedInTheOrderOfTheirQStringKeys) {
  QStringList readers, writers, renderWriters;
  TLevelReader::getSupportedFormats(readers);
  TLevelWriter::getSupportedFormats(writers, false);
  TLevelWriter::getSupportedFormats(renderWriters, true);
  expectQStringOrder(readers);
  expectQStringOrder(writers);
  expectQStringOrder(renderWriters);
  EXPECT_TRUE(readers.contains("pli"));
  EXPECT_TRUE(readers.contains("tlv"));

  std::vector<std::string> stdReaders;
  TLevelReader::getSupportedFormats(stdReaders);
  ASSERT_EQ(int(stdReaders.size()), readers.size());
  for (int i = 0; i < readers.size(); ++i)
    EXPECT_EQ(QString::fromStdString(stdReaders[i]), readers[i]);
}

namespace {

int g_compressed = 0, g_decompressed = 0;

// A stand-in for LZO: the bytes reversed.
bool reverseCompress(const std::string &src, std::string &dst) {
  ++g_compressed;
  dst.assign(src.rbegin(), src.rend());
  return true;
}
bool reverseDecompress(const std::string &src, int dstSize, std::string &dst) {
  ++g_decompressed;
  dst.assign(src.rbegin(), src.rend());
  return int(dst.size()) == dstSize;
}
const TLzoService kReverse = {&reverseCompress, &reverseDecompress};

bool failCompress(const std::string &, std::string &) { return false; }
bool failDecompress(const std::string &, int, std::string &) { return false; }
const TLzoService kFailing = {&failCompress, &failDecompress};

TRaster32P someRaster() {
  TRaster32P ras(37, 11);
  for (int y = 0; y < ras->getLy(); ++y)
    for (int x = 0; x < ras->getLx(); ++x)
      ras->pixels(y)[x] = TPixel32(x * 7, y * 13, (x * y) & 0xff, 255);
  return ras;
}

bool sameRaster(const TRasterP &a, const TRasterP &b) {
  if (a->getSize() != b->getSize() || a->getPixelSize() != b->getPixelSize())
    return false;
  for (int y = 0; y < a->getLy(); ++y)
    if (std::memcmp(a->getRawData(0, y), b->getRawData(0, y),
                    a->getLx() * a->getPixelSize()))
      return false;
  return true;
}

}  // namespace

TEST(LzoService, TheCodecGoesThroughTheServiceSet) {
  const TLzoService *builtIn = setLzoService(&kReverse);
  ASSERT_NE(builtIn, nullptr);  // the helpers, in the build with Qt
  g_compressed = g_decompressed = 0;
  {
    TRasterCodecLZO codec("LZO", false);
    TRaster32P in = someRaster();
    TINT32 size   = 0;
    TRasterP compressed = codec.compress(in, 1, size);
    ASSERT_TRUE(compressed);
    TRasterP out;
    codec.decompress(compressed, out);
    ASSERT_TRUE(out);
    EXPECT_TRUE(sameRaster(in, out));
  }
  EXPECT_EQ(g_compressed, 1);
  EXPECT_EQ(g_decompressed, 1);

  setLzoService(&kFailing);
  {
    TRasterCodecLZO codec("LZO", false);
    TINT32 size = 0;
    EXPECT_THROW(codec.compress(someRaster(), 1, size), TException);
  }
  EXPECT_EQ(setLzoService(nullptr), &kFailing);
  EXPECT_EQ(setLzoService(nullptr), builtIn);
}

// The built-in one runs the helpers next to the executable, as before.
TEST(LzoService, TheHelpersCompressAndDecompress) {
  TRasterCodecLZO codec("LZO", false);
  TRaster32P in = someRaster();
  TINT32 size   = 0;
  TRasterP compressed = codec.compress(in, 1, size);
  ASSERT_TRUE(compressed);
  TRasterP out;
  codec.decompress(compressed, out);
  ASSERT_TRUE(out);
  EXPECT_TRUE(sameRaster(in, out));
}
