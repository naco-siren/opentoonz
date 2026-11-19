// TFileStatus and the file-system functions of TSystem, in both builds (the
// one with Qt and the one without). Nothing here makes a QCoreApplication:
// since migration step 6a of doc/nexttoonz_core_noqt.md TFileStatus reports
// the file system as it is whether or not the Qt application object exists
// (it used to report every path as missing before it did), and these tests
// guard that. test_tsystem_qt.cpp compares the functions with the old code.

#include <gtest/gtest.h>

#include "tfilepath.h"
#include "tsystem.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;

class TSystemTest : public ::testing::Test {
protected:
  fs::path root;

  void SetUp() override {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    ASSERT_FALSE(ec);
    for (int i = 0; i < 100; ++i) {
      fs::path p = base / ("nt_tsystem_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_" + std::to_string(i));
      if (fs::create_directory(p, ec)) {
        root = fs::canonical(p);
        return;
      }
    }
    FAIL() << "no temporary folder";
  }
  void TearDown() override {
    std::error_code ec;
    if (!root.empty()) fs::remove_all(root, ec);
  }

  TFilePath at(const std::string &name) const {
    return TFilePath((root / name).wstring());
  }
  void write(const std::string &name, const std::string &content) const {
    std::ofstream(root / name, std::ios::binary) << content;
  }
};

}  // namespace

TEST_F(TSystemTest, StatusOfAnExistingFile) {
  write("file.txt", "12345");
  TFileStatus fs(at("file.txt"));
  EXPECT_TRUE(fs.doesExist());
  EXPECT_FALSE(fs.isDirectory());
  EXPECT_FALSE(fs.isLink());
  EXPECT_EQ(fs.getSize(), 5);
  EXPECT_TRUE(fs.isReadable());
  EXPECT_TRUE(fs.isWritable());
  EXPECT_FALSE(fs.isWritableDir());
  EXPECT_NE(fs.getPermissions() & TFileStatus::ReadOwner, 0);
}

TEST_F(TSystemTest, StatusOfAFolder) {
  TFileStatus fs(TFilePath(root.wstring()));
  EXPECT_TRUE(fs.doesExist());
  EXPECT_TRUE(fs.isDirectory());
  EXPECT_TRUE(fs.isWritableDir());
}

TEST_F(TSystemTest, StatusOfAMissingPath) {
  TFileStatus fs(at("missing"));
  EXPECT_FALSE(fs.doesExist());
  EXPECT_FALSE(fs.isDirectory());
  EXPECT_EQ(fs.getSize(), 0);
  EXPECT_EQ(fs.getPermissions(), 0);
  EXPECT_FALSE(fs.isReadable());
  EXPECT_FALSE(TFileStatus(at("missing/below")).doesExist());
}

TEST_F(TSystemTest, MakesListsCopiesAndRemoves) {
  TSystem::mkDir(at("a/b/c"));
  EXPECT_TRUE(TFileStatus(at("a/b/c")).isDirectory());
  write("a/one.0001.png", "1");
  write("a/one.0002.png", "2");
  write("a/two.txt", "two");
  write("a/.hidden", "h");

  TFilePathSet all = TSystem::readDirectory(at("a"), false);
  ASSERT_EQ(all.size(), 4u);  // b, one.0001.png, one.0002.png, two.txt
  TFilePathSet grouped = TSystem::readDirectory(at("a"), true, true);
  EXPECT_EQ(grouped.size(), 2u);  // one..png, two.txt
  EXPECT_EQ(TSystem::readDirectory(at("a"), false, true, true).size(), 4u);

  TSystem::copyFile(at("a/copy.txt"), at("a/two.txt"));
  EXPECT_EQ(TFileStatus(at("a/copy.txt")).getSize(), 3);
  EXPECT_THROW(TSystem::copyFile(at("a/copy.txt"), at("a/two.txt"), false),
               TSystemException);
  TSystem::renameFile(at("a/moved.txt"), at("a/copy.txt"));
  EXPECT_FALSE(TFileStatus(at("a/copy.txt")).doesExist());
  EXPECT_TRUE(TFileStatus(at("a/moved.txt")).doesExist());
  TSystem::deleteFile(at("a/moved.txt"));
  EXPECT_FALSE(TFileStatus(at("a/moved.txt")).doesExist());
  EXPECT_THROW(TSystem::deleteFile(at("a/moved.txt")), TSystemException);

  EXPECT_TRUE(TSystem::doesExistFileOrLevel(at("a/one..png")));
  EXPECT_FALSE(TSystem::doesExistFileOrLevel(at("a/three..png")));

  EXPECT_THROW(TSystem::mkDir(at("a/plus+sign")), TSystemException);
  TSystem::rmDir(at("a/b/c"));
  EXPECT_FALSE(TFileStatus(at("a/b/c")).doesExist());
}
