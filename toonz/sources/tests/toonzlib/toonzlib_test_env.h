#pragma once

// Shared environment for nexttoonz_toonzlib_tests.
//
// The global test environment (toonzlib_test_main.cpp) initialises the
// libraries the way the fixture generator does: a scratch copy of stuff/
// as TOONZROOT, the standard image readers and writers, the standard fx
// and the project manager pointed at the committed reference project.

#include "tfilepath.h"

#include <string>

namespace nexttoonz_test {

// Absolute path of tests/fixtures/reference_project (read only: tests must
// never write into it).
TFilePath fixtureProjectDir();

// fixtureProjectDir() + relativePath, e.g. "drawings/vec_circle.pli".
TFilePath fixturePath(const std::string &relativePath);

// The reference project file (reference_project_otprj.xml).
TFilePath fixtureProjectFile();

// A fresh, empty, per-test scratch directory under the process-wide
// temporary directory. Removed with it when the test binary exits.
TFilePath makeScratchDir(const std::string &name);

// Copies the whole reference project into a fresh scratch directory and
// returns the path of the copy (its folder is named "reference_project").
TFilePath copyFixtureProject(const std::string &scratchName);

}  // namespace nexttoonz_test
