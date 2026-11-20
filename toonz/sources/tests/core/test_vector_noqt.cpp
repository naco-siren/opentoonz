// Vector images, styles, regions, pli files and pattern brushes without Qt
// (step 8d of doc/nexttoonz_core_noqt.md), drawn by Skia. The same file is
// built into the core tests of the build without Qt (and run in the iPad
// simulator) and into the toonzlib tests of the build with Qt; each checks
// its pictures and files against the same checksums, so passing in both
// means they come out byte for byte the same.
//
// NEXTTOONZ_PRINT_HASHES=1 prints every checksum as a line of kExpected
// (test_vector_noqt_hashes_<platform>.inc).

#include <gtest/gtest.h>

#include "colorfx.h"
#include "tnzimage.h"

#include "tcolorstyles.h"
#include "tfilepath.h"
#include "tlevel_io.h"
#include "tpalette.h"
#include "traster.h"
#include "tregion.h"
#include "tsimplecolorstyles.h"
#include "tstroke.h"
#include "tsystem.h"
#include "tvectorimage.h"
#include "tvectorrasterizer.h"
#include "tvectorrenderdata.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifndef NEXTTOONZ_FIXTURE_DIR
#error "NEXTTOONZ_FIXTURE_DIR: the reference project, for its pli files"
#endif

namespace {

// FNV-1a, 64 bits
struct Hash {
  unsigned long long h = 1469598103934665603ULL;
  void bytes(const void *p, size_t n) {
    const unsigned char *c = (const unsigned char *)p;
    for (size_t i = 0; i < n; ++i) h = (h ^ c[i]) * 1099511628211ULL;
  }
  void number(double v) {
    // the bits of the double, not its text
    bytes(&v, sizeof v);
  }
  void integer(long long v) { bytes(&v, sizeof v); }
  void text(const std::string &s) {
    integer((long long)s.size());
    bytes(s.data(), s.size());
  }
};

unsigned long long rasterHash(const TRaster32P &ras) {
  Hash h;
  h.integer(ras->getLx());
  h.integer(ras->getLy());
  ras->lock();
  for (int y = 0; y < ras->getLy(); ++y)
    h.bytes(ras->pixels(y), ras->getLx() * sizeof(TPixel32));
  ras->unlock();
  return h.h;
}

unsigned long long fileHash(const TFilePath &fp) {
  Hash h;
  FILE *f = std::fopen(::to_string(fp.getWideString()).c_str(), "rb");
  if (!f) return 0;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) h.bytes(buf, n);
  std::fclose(f);
  return h.h;
}

void hashImage(Hash &h, const TVectorImageP &vi) {
  h.integer(vi->getStrokeCount());
  for (UINT s = 0; s < vi->getStrokeCount(); ++s) {
    const TStroke *stroke = vi->getStroke(s);
    h.integer(stroke->getStyle());
    h.integer(stroke->isSelfLoop());
    h.integer(stroke->getControlPointCount());
    for (int i = 0; i < stroke->getControlPointCount(); ++i) {
      TThickPoint p = stroke->getControlPoint(i);
      h.number(p.x), h.number(p.y), h.number(p.thick);
    }
  }
  h.integer(vi->getRegionCount());
  for (UINT r = 0; r < vi->getRegionCount(); ++r) {
    TRegion *region = vi->getRegion(r);
    h.integer(region->getStyle());
    TRectD box = region->getBBox();
    h.number(box.x0), h.number(box.y0), h.number(box.x1), h.number(box.y1);
    h.integer(region->getSubregionCount());
  }
}

// What hashImage() covers that is made of integers only: the strokes'
// styles and point counts, and the regions, their styles and the edges
// around them (which stroke, which way, which style), subregions included.
// It must be the same on every platform; the coordinates, which may differ
// in the last bits from one processor to another (arm64 fuses multiply and
// add), are not in it.
void hashRegionStructure(Hash &h, const TRegion *region) {
  h.integer(region->getStyle());
  h.integer(region->getEdgeCount());
  for (UINT e = 0; e < region->getEdgeCount(); ++e) {
    const TEdge *edge = region->getEdge(e);
    h.integer(edge->m_index);
    h.integer(edge->m_styleId);
    h.integer(edge->m_w0 < edge->m_w1);
  }
  h.integer(region->getSubregionCount());
  for (UINT s = 0; s < region->getSubregionCount(); ++s)
    hashRegionStructure(h, region->getSubregion(s));
}

void hashStructure(Hash &h, const TVectorImageP &vi) {
  h.integer(vi->getStrokeCount());
  for (UINT s = 0; s < vi->getStrokeCount(); ++s) {
    const TStroke *stroke = vi->getStroke(s);
    h.integer(stroke->getStyle());
    h.integer(stroke->isSelfLoop());
    h.integer(stroke->getControlPointCount());
  }
  h.integer(vi->getRegionCount());
  for (UINT r = 0; r < vi->getRegionCount(); ++r)
    hashRegionStructure(h, vi->getRegion(r));
}

void hashPalette(Hash &h, TPalette *palette) {
  h.integer(palette ? palette->getStyleCount() : -1);
  if (!palette) return;
  for (int i = 0; i < palette->getStyleCount(); ++i) {
    TColorStyle *s = palette->getStyle(i);
    h.integer(s ? s->getTagId() : -1);
    if (!s) continue;
    TPixel32 c = s->getMainColor();
    h.bytes(&c, sizeof c);
    h.text(::to_string(s->getName()));
  }
  h.integer(palette->getPageCount());
}

// The frame's bounding box fitted into a picture of 192 by 192, so that
// every frame shows what it has.
TRaster32P drawFitted(const TVectorImageP &vi, TPalette *palette) {
  const int size = 192;
  TRaster32P ras(size, size);
  ras->clear();
  TRectD box = vi->getBBox();
  double sc  = (box.getLx() > 0 && box.getLy() > 0)
                   ? 0.9 * size / std::max(box.getLx(), box.getLy())
                   : 1.0;
  const TVectorRenderData rd(
      TTranslation(size / 2, size / 2) * TScale(sc) *
          TTranslation(-0.5 * (box.getP00() + box.getP11())),
      TRect(0, 0, size - 1, size - 1), palette, 0, true, true);
  EXPECT_TRUE(TVectorRasterizer::draw(ras, vi.getPointer(), rd));
  return ras;
}

TRaster32P draw(const TVectorImageP &vi, TPalette *palette, int lx = 256,
                int ly = 256, double scale = 1.0) {
  TRaster32P ras(lx, ly);
  ras->clear();
  const TVectorRenderData rd(TTranslation(lx / 2, ly / 2) * TScale(scale),
                             TRect(0, 0, lx - 1, ly - 1), palette, 0, true,
                             true);
  EXPECT_TRUE(TVectorRasterizer::draw(ras, vi.getPointer(), rd));
  return ras;
}

// What every build of this platform must give, filled from its build with
// Qt. The pictures depend on the Skia of the platform (the skia-pack on
// macOS and iPadOS, vcpkg's on Linux) and the exact coordinates on the
// processor (x86_64 or arm64); the files do not.
const std::map<std::string, unsigned long long> kExpected = {
#if defined(LINUX)
#include "test_vector_noqt_hashes_linux.inc"
#elif defined(MACOSX) && (defined(__aarch64__) || defined(__arm64__))
#include "test_vector_noqt_hashes_macos_arm64.inc"
#elif defined(MACOSX)
#include "test_vector_noqt_hashes_macos.inc"
#endif
};
// What every platform must give, whatever its Skia and processor.
const std::map<std::string, unsigned long long> kEveryPlatform = {
#include "test_vector_noqt_hashes_common.inc"
};

std::map<std::string, unsigned long long> &seen() {
  static std::map<std::string, unsigned long long> s;
  return s;
}

void expectHash(const std::string &name, unsigned long long actual) {
  seen()[name] = actual;
  if (std::getenv("NEXTTOONZ_PRINT_HASHES"))
    std::printf("    {\"%s\", 0x%016llxULL},\n", name.c_str(), actual);
  // every checksum, in the tables' format, added to a file: what CI keeps
  // when this test fails (each test runs in a process of its own there)
  if (const char *file = std::getenv("NEXTTOONZ_HASHES_FILE"))
    if (FILE *f = std::fopen(file, "a")) {
      std::fprintf(f, "    {\"%s\", 0x%016llxULL},\n", name.c_str(), actual);
      std::fclose(f);
    }
  const auto &table = kEveryPlatform.count(name) ? kEveryPlatform : kExpected;
  auto it = table.find(name);
  if (it == table.end()) {
    ADD_FAILURE() << "no checksum for " << name << ": 0x" << std::hex
                  << actual;
    return;
  }
  EXPECT_EQ(it->second, actual) << name << ": 0x" << std::hex << actual;
}

class VectorWithoutQt : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    static bool once = false;
    if (once) return;
    once = true;
#ifndef QT_CORE_LIB
    // (the toonzlib tests of the build with Qt have done all this, and
    // more, in their main(), as tcomposer does)
    initColorFx();
    initPliIo();
#endif
  }
  void SetUp() override {
    if (kExpected.empty() && !std::getenv("NEXTTOONZ_PRINT_HASHES"))
      GTEST_SKIP() << "no checksums for this platform yet";
    m_rasterizer = TVectorRasterizer::isSelected();
    TVectorRasterizer::select(true);
  }
  void TearDown() override { TVectorRasterizer::select(m_rasterizer); }
  bool m_rasterizer = true;
};

TStroke *loop(double cx, double cy, double r, int n) {
  std::vector<TThickPoint> points;
  const double step = 2.0 * std::acos(-1.0) / n;
  for (int i = 0; i < n; ++i) {
    const double a = i * step, am = a + step / 2;
    const double cr = r / std::cos(step / 2);
    points.push_back(
        TThickPoint(cx + r * std::cos(a), cy + r * std::sin(a), 0.0));
    points.push_back(
        TThickPoint(cx + cr * std::cos(am), cy + cr * std::sin(am), 0.0));
  }
  points.push_back(points[0]);
  TStroke *stroke = new TStroke(points);
  stroke->setSelfLoop(true);
  return stroke;
}

TStroke *wave(double y, double thickness, int n = 9) {
  std::vector<TThickPoint> points;
  for (int i = 0; i < n; ++i)
    points.push_back(TThickPoint(-110 + 220.0 * i / (n - 1),
                                 y + ((i % 2) ? 10 : -10), thickness));
  return new TStroke(points);
}

std::wstring fixtureDir() { return ::to_wstring(NEXTTOONZ_FIXTURE_DIR); }

// vec_styles.pli has a style of every kind, toonzlib's own among them
// (texture, MyPaint, image styles), which the build without Qt does not
// have yet: it is read in the build with Qt only (doc/nexttoonz_core_noqt.md,
// 8d). Without Qt reading it must fail cleanly (see below).
bool readableHere(const TFilePath &fp) {
#ifdef QT_CORE_LIB
  return true;
#else
  return fp.getName() != "vec_styles";
#endif
}

TFilePath scratch(const std::string &name) {
  TFilePath dir = TSystem::getTempDir() + ("nexttoonz_vector_noqt_" + name);
  if (TFileStatus(dir).doesExist()) TSystem::rmDirTree(dir);
  TSystem::mkDir(dir);
  return dir;
}

}  // namespace

//-----------------------------------------------------------------------------

// Regions computed from crossing loops, filled with plain and colorfx
// styles, under strokes of plain, centre line and colorfx styles.
TEST_F(VectorWithoutQt, RegionsAndStylesAreComputedAndDrawnTheSame) {
  TPaletteP palette = new TPalette();
  std::vector<int> fills, inks;
  fills.push_back(palette->getPage(0)->addStyle(new TSolidColorStyle(TPixel32(200, 60, 40))));
  for (int tag : {1130, 1136, 1138, 1142, 1129})
    if (TColorStyle *s = TColorStyle::create(tag)) fills.push_back(palette->getPage(0)->addStyle(s));
  inks.push_back(palette->getPage(0)->addStyle(new TSolidColorStyle(TPixel32(20, 20, 160))));
  inks.push_back(palette->getPage(0)->addStyle(new TCenterLineStrokeStyle(TPixel32(0, 120, 0), 0xF0F0, 2.0)));
  for (int tag : {108, 111, 123, 141, 201})
    if (TColorStyle *s = TColorStyle::create(tag)) inks.push_back(palette->getPage(0)->addStyle(s));
  ASSERT_EQ(fills.size(), 6u);
  ASSERT_EQ(inks.size(), 7u);

  TVectorImageP vi = new TVectorImage();
  vi->setPalette(palette.getPointer());
  for (int i = 0; i < 3; ++i) {
    TStroke *s = loop(-40 + 40 * i, (i % 2) ? 20 : -20, 45, 8 + i);
    s->setStyle(0);
    vi->addStroke(s);
  }
  vi->findRegions();
  Hash regions, regionsStructure;
  hashImage(regions, vi);
  hashStructure(regionsStructure, vi);
  for (UINT r = 0; r < vi->getRegionCount(); ++r) {
    TRegion *region = vi->getRegion(r);
    TPointD inside = region->getBBox().getP00() * 0.5 + region->getBBox().getP11() * 0.5;
    vi->fill(inside, fills[r % fills.size()]);
  }
  for (size_t i = 0; i < inks.size(); ++i) {
    TStroke *s = wave(-90 + 30.0 * i, 3.0 + i);
    s->setStyle(inks[i]);
    vi->addStroke(s);
  }
  Hash filled, filledStructure;
  hashImage(filled, vi);
  hashStructure(filledStructure, vi);
  // the structure (integers only) on every platform; everything, the
  // coordinates' bits too, per platform
  expectHash("regions/computed/structure", regionsStructure.h);
  expectHash("regions/filled/structure", filledStructure.h);
  expectHash("regions/computed/exact", regions.h);
  expectHash("regions/filled/exact", filled.h);
  expectHash("regions/drawn", rasterHash(draw(vi, palette.getPointer())));
  expectHash("regions/drawn_half", rasterHash(draw(vi, palette.getPointer(), 200, 150, 0.5)));
}

// Every pli of the reference project, read: frames, palette, strokes,
// regions, and each frame drawn.
TEST_F(VectorWithoutQt, PliFilesAreReadTheSame) {
  TFilePathSet files =
      TSystem::readDirectory(TFilePath(fixtureDir()) + "drawings", false, true);
  int read = 0;
  for (const TFilePath &fp : files) {
    if (fp.getType() != "pli" || !readableHere(fp)) continue;
    const std::string name = ::to_string(fp.withoutParentDir().getWideString());
    SCOPED_TRACE(name);
    TLevelReaderP lr(fp);
    TLevelP level;
    try {
      level = lr->loadInfo();
    } catch (TException &e) {
      ADD_FAILURE() << ::to_string(e.getMessage());
      continue;
    }
    ASSERT_TRUE(level);
    Hash h;
    h.text(::to_string(lr->getCreatorW()));
    hashPalette(h, level->getPalette());
    for (TLevel::Iterator it = level->begin(); it != level->end(); ++it) {
      h.text(it->first.expand());
      TVectorImageP vi = lr->getFrameReader(it->first)->load();
      ASSERT_TRUE(vi);
      hashImage(h, vi);
      expectHash("pli/" + name + "/" + it->first.expand(),
                 rasterHash(drawFitted(vi, level->getPalette())));
    }
    expectHash("pli/" + name, h.h);
    ++read;
  }
  // the reference project's 15, and all but vec_styles.pli without Qt
#ifdef QT_CORE_LIB
  EXPECT_EQ(read, 15);
#else
  EXPECT_EQ(read, 14);
#endif
}

#ifndef QT_CORE_LIB
// Without toonzlib's styles, a level that uses them is refused as it always
// was refused when a style was unknown: with the exception, not a crash nor
// a level half read. When toonzlib's styles come into the build without Qt,
// this is to become "and it is read" (doc/nexttoonz_core_noqt.md, 8d).
TEST_F(VectorWithoutQt, ALevelWithStylesThisBuildDoesNotHaveIsRefused) {
  const TFilePath fp =
      TFilePath(fixtureDir()) + "drawings" + "vec_styles.pli";
  ASSERT_TRUE(TFileStatus(fp).doesExist());
  TLevelReaderP lr(fp);
  std::string message;
  try {
    lr->loadInfo();
    ADD_FAILURE() << "read without toonzlib's styles";
  } catch (TException &e) {
    message = ::to_string(e.getMessage());
  }
  EXPECT_EQ(message.rfind("Unknown color style id; id = ", 0), 0u) << message;
}
#endif

// And written again: the bytes of the file, with a creator.
TEST_F(VectorWithoutQt, PliFilesAreWrittenTheSame) {
  const TFilePath out = scratch("pli_write");
  TFilePathSet files =
      TSystem::readDirectory(TFilePath(fixtureDir()) + "drawings", false, true);
  for (const TFilePath &fp : files) {
    if (fp.getType() != "pli" || !readableHere(fp)) continue;
    const std::string name = ::to_string(fp.withoutParentDir().getWideString());
    SCOPED_TRACE(name);
    TLevelReaderP lr(fp);
    TLevelP level = lr->loadInfo();
    ASSERT_TRUE(level && level->getPalette());
    std::vector<std::pair<TFrameId, TVectorImageP>> frames;
    for (TLevel::Iterator it = level->begin(); it != level->end(); ++it)
      frames.push_back({it->first, lr->getFrameReader(it->first)->load()});
    const TFilePath copy = out + fp.withoutParentDir();
    {
      TLevelWriterP lw(copy);
      lw->setCreatorW(L"NextToonz 8d test");
      for (auto &f : frames) {
        f.second->setPalette(level->getPalette());
        lw->getFrameWriter(f.first)->save(f.second);
      }
    }
    expectHash("pli_written/" + name, fileHash(copy));
    TLevelReaderP again(copy);
    again->loadInfo();
    EXPECT_EQ(again->getCreatorW(), L"NextToonz 8d test");
  }
}

// Pattern brushes made from a pli level: the vector pattern's stamps and
// the raster pattern's textures (each frame rasterised by Skia), along a
// stroke.
TEST_F(VectorWithoutQt, PatternBrushesAreDrawnTheSame) {
  const TFilePath library = scratch("patterns");
  TSystem::mkDir(library + "custom styles");
  TSystem::copyFile(library + "custom styles" + "seams.pli",
                    TFilePath(fixtureDir()) + "drawings" + "vec_circle.pli");
  const TFilePath rasterRoot = TRasterImagePatternStrokeStyle::getRootDir();
  const TFilePath vectorRoot = TVectorImagePatternStrokeStyle::getRootDir();
  TRasterImagePatternStrokeStyle::setRootDir(library);
  TVectorImagePatternStrokeStyle::setRootDir(library);
  {
    TPaletteP palette = new TPalette();
    const int r = palette->getPage(0)->addStyle(new TRasterImagePatternStrokeStyle("seams"));
    const int v = palette->getPage(0)->addStyle(new TVectorImagePatternStrokeStyle("seams"));
    TVectorImageP vi = new TVectorImage();
    vi->setPalette(palette.getPointer());
    TStroke *a = wave(-40, 25, 11);
    a->setStyle(r);
    vi->addStroke(a);
    TStroke *b = wave(50, 25, 11);
    b->setStyle(v);
    vi->addStroke(b);
    expectHash("patterns/drawn", rasterHash(draw(vi, palette.getPointer())));
    expectHash("patterns/raster_icon",
               rasterHash(palette->getStyle(r)->getIcon(TDimension(64, 48))));
    expectHash("patterns/vector_icon",
               rasterHash(palette->getStyle(v)->getIcon(TDimension(64, 48))));
  }
  TRasterImagePatternStrokeStyle::setRootDir(rasterRoot.getParentDir());
  TVectorImagePatternStrokeStyle::setRootDir(vectorRoot.getParentDir());
}

// The same files read and drawn by eight threads at once, as a render with
// -nthreads 8 does: the same pictures (and, under ThreadSanitizer, no race in
// the image cache, the string ids, the memory manager or the styles).
TEST_F(VectorWithoutQt, PliFilesAreDrawnTheSameInEightThreads) {
  TFilePathSet set =
      TSystem::readDirectory(TFilePath(fixtureDir()) + "drawings", false, true);
  std::vector<TFilePath> files;
  for (const TFilePath &fp : set)
    if (fp.getType() == "pli" && readableHere(fp)) files.push_back(fp);
  std::vector<std::map<std::string, unsigned long long>> results(8);
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t)
    threads.emplace_back([&, t] {
      for (size_t i = 0; i < files.size(); ++i) {
        const TFilePath &fp = files[(i + t) % files.size()];
        const std::string name =
            ::to_string(fp.withoutParentDir().getWideString());
        TLevelReaderP lr(fp);
        TLevelP level = lr->loadInfo();
        for (TLevel::Iterator it = level->begin(); it != level->end(); ++it) {
          TVectorImageP vi = lr->getFrameReader(it->first)->load();
          results[t]["pli/" + name + "/" + it->first.expand()] =
              rasterHash(drawFitted(vi, level->getPalette()));
        }
      }
    });
  for (std::thread &t : threads) t.join();
  for (int t = 0; t < 8; ++t) {
    EXPECT_GT(results[t].size(), 50u);
    for (const auto &r : results[t]) {
      auto it = kExpected.find(r.first);
      ASSERT_NE(it, kExpected.end()) << r.first;
      EXPECT_EQ(it->second, r.second) << "thread " << t << ": " << r.first;
    }
  }
}

// Every checksum the table has was met (none left behind by a test that
// stopped looking).
TEST_F(VectorWithoutQt, ZzEveryChecksumWasChecked) {
  if (std::getenv("NEXTTOONZ_PRINT_HASHES")) return;
  if (!::testing::GTEST_FLAG(filter).empty() &&
      ::testing::GTEST_FLAG(filter) != "*")
    return;
  for (const auto *table : {&kExpected, &kEveryPlatform})
    for (const auto &e : *table) {
#ifndef QT_CORE_LIB
      if (e.first.find("vec_styles.pli") != std::string::npos) continue;
#endif
      EXPECT_TRUE(seen().count(e.first)) << "not checked: " << e.first;
    }
}
