// Unit tests for the registry of sound track readers and writers
// (tsound_io.h), without Qt: since migration step 5 of
// doc/nexttoonz_core_noqt.md it is keyed by std::string, and its QString
// members are in tsound_io_qt.cpp.

#include <gtest/gtest.h>

#include "texception.h"
#include "tfilepath.h"
#include "tsound_io.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
TSoundTrackReader *noReader(const TFilePath &) { return nullptr; }
TSoundTrackWriter *noWriter(const TFilePath &) { return nullptr; }

bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}
}  // namespace

TEST(TSoundTrackIo, RegistryListsWhatWasDefinedInOrder) {
  TSoundTrackReader::define("zz-test", noReader);
  TSoundTrackReader::define(std::string("aa-test"), noReader);
  TSoundTrackWriter::define("ww-test", noWriter);
  std::vector<std::string> readers, writers;
  TSoundTrackReader::getSupportedFormats(readers);
  TSoundTrackWriter::getSupportedFormats(writers);
  EXPECT_TRUE(contains(readers, "zz-test"));
  EXPECT_TRUE(contains(readers, "aa-test"));
  EXPECT_TRUE(std::is_sorted(readers.begin(), readers.end()));
  EXPECT_TRUE(contains(writers, "ww-test"));
  EXPECT_FALSE(contains(writers, "zz-test"));
}

TEST(TSoundTrackIo, AnUnknownTypeThrows) {
  EXPECT_THROW(TSoundTrackReaderP(TFilePath("x.no-such-type")), TException);
  EXPECT_THROW(TSoundTrackWriterP(TFilePath("x.no-such-type")), TException);
}
