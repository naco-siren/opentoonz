// main() and the global environment of nexttoonz_toonzlib_tests.
//
// The libraries are initialised the same way as in
// tests/fixtures/gen_reference_project.cpp: a QCoreApplication, a scratch
// copy of stuff/ used as TOONZROOT (Preferences and the project manager
// write into it), the standard image readers/writers and the standard fx.
//
// ctest runs every test in its own process (gtest_discover_tests), so the
// set-up below runs once per test there; keep it cheap.

#include "toonzlib_test_env.h"

#include <gtest/gtest.h>

#include "tenv.h"
#include "tsystem.h"
#include "tfilepath.h"
#include "tiio_std.h"
#include "tnzimage.h"
#include "colorfx.h"
#include "stdfx/shaderfx.h"
#include "toonz/preferences.h"
#include "toonz/tproject.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <string>

DV_IMPORT_API void initStdFx();

namespace {

// Process-wide scratch directory; created in main() before the tests run
// and removed when main() returns.
QTemporaryDir *g_scratchRoot = nullptr;
std::string g_argv0;

TFilePath scratchRoot() {
  return TFilePath(g_scratchRoot->path().toStdWString());
}

// Copies stuff/ into the scratch directory, minus read-only assets none of
// these tests use: doc/ and library/ except library/shaders/ (brushes,
// textures, particles... are ~26 MB of the ~29 MB). Copying everything made
// every ctest process (one per test) spend ~2.5 s in set-up. Add a folder
// back here if a test starts to need it.
void copyStuff(const TFilePath &dst, const TFilePath &src) {
  static const QStringList kSkipped = {"doc", "library"};
  TSystem::mkDir(dst);
  QDir srcDir(QString::fromStdWString(src.getWideString()));
  const QFileInfoList entries =
      srcDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
  for (const QFileInfo &fi : entries) {
    if (kSkipped.contains(fi.fileName())) continue;
    const TFilePath to = dst + TFilePath(fi.fileName().toStdWString());
    if (fi.isDir())
      TSystem::copyDir(to, TFilePath(fi.filePath().toStdWString()));
    else
      QFile::copy(fi.filePath(), QString::fromStdWString(to.getWideString()));
  }
  // Shader fx interfaces (see loadShaderInterfaces() below).
  TSystem::mkDir(dst + "library");
  TSystem::copyDir(dst + "library" + "shaders", src + "library" + "shaders");
}

class ToonzlibEnvironment final : public ::testing::Environment {
public:
  void SetUp() override {
    ASSERT_NE(g_scratchRoot, nullptr);
    ASSERT_TRUE(g_scratchRoot->isValid());

    const TFilePath stuffSource(
        QString::fromUtf8(NEXTTOONZ_STUFF_DIR).toStdWString());
    ASSERT_TRUE(TFileStatus(stuffSource).doesExist())
        << "stuff dir not found: " << NEXTTOONZ_STUFF_DIR;
    ASSERT_TRUE(TFileStatus(nexttoonz_test::fixtureProjectFile()).doesExist())
        << "reference project not found: " << NEXTTOONZ_FIXTURE_DIR;

    const TFilePath stuff = scratchRoot() + "stuff";
    copyStuff(stuff, stuffSource);

    TEnv::setRootVarName("TOONZROOT");
    TEnv::setSystemVarPrefix("TOONZ");
    TEnv::setApplicationFileName(g_argv0.c_str());
    TEnv::setStuffDir(stuff);

    Tiio::defineStd();
    initImageIo();
    initStdFx();
    initColorFx();
    // Shader fx are declared from their interface files, as tcomposer and
    // the generator do; scenes/shader_fx.tnz cannot be loaded otherwise.
    loadShaderInterfaces(stuff + "library" + "shaders");
    Preferences::instance();

    // Make the reference project the current project, as the generator
    // does. Scenes find their own project through scenes.xml anyway; this
    // matters for scenes created from scratch (initializeScene).
    TProjectManager *pm = TProjectManager::instance();
    pm->addProjectsRoot(nexttoonz_test::fixtureProjectDir().getParentDir());
    pm->setCurrentProjectPath(nexttoonz_test::fixtureProjectFile());
  }
};

}  // namespace

namespace nexttoonz_test {

TFilePath fixtureProjectDir() {
  return TFilePath(
      QDir::cleanPath(QString::fromUtf8(NEXTTOONZ_FIXTURE_DIR)).toStdWString());
}

TFilePath fixturePath(const std::string &relativePath) {
  return fixtureProjectDir() + TFilePath(relativePath);
}

TFilePath fixtureProjectFile() {
  return fixtureProjectDir() + "reference_project_otprj.xml";
}

TFilePath makeScratchDir(const std::string &name) {
  TFilePath dir = scratchRoot() + "work" + name;
  if (TFileStatus(dir).doesExist()) TSystem::rmDirTree(dir);
  TSystem::mkDir(dir);
  return dir;
}

TFilePath copyFixtureProject(const std::string &scratchName) {
  // Keep the folder name: TProjectManager looks for
  // <folder>/<folder>_otprj.xml.
  TFilePath project = makeScratchDir(scratchName) + "reference_project";
  TSystem::copyDir(project, fixtureProjectDir());
  return project;
}

}  // namespace nexttoonz_test

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);

  QTemporaryDir scratch(QDir::tempPath() + "/nexttoonz_toonzlib_tests-XXXXXX");
  g_scratchRoot = &scratch;
  g_argv0       = argv[0];

  ::testing::AddGlobalTestEnvironment(new ToonzlibEnvironment);
  const int result = RUN_ALL_TESTS();

  g_scratchRoot = nullptr;
  return result;
}
