// Loading and saving the .tnz scenes of the reference project through
// ToonzScene.
//
// SceneLoad loads every scenes/*.tnz of the reference project and checks
// the structure the generator (tests/fixtures/gen_reference_project.cpp)
// built: frame and column count, camera, levels resolved through the
// project folders. SceneLoadDetails adds scene-specific checks (pegbar
// parenting and keyframes, fx dag wiring, sub-xsheets). SceneSave and
// SceneSaveRoundTrip re-save scenes inside a scratch copy of the project
// and compare the reloaded xsheet with the original.

#include "toonzlib_test_env.h"

#include <gtest/gtest.h>

#include "tfilepath.h"
#include "tsystem.h"
#include "tfx.h"
#include "tdoubleparam.h"
#include "tparamcontainer.h"
#include "toonz/toonzscene.h"
#include "toonz/tproject.h"
#include "toonz/levelset.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshcolumn.h"
#include "toonz/txshlevel.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshchildlevel.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshzeraryfxcolumn.h"
#include "toonz/tcolumnfx.h"
#include "toonz/fxdag.h"
#include "toonz/tcolumnfxset.h"
#include "toonz/tcamera.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjectid.h"
#include "toonz/tstageobjecttree.h"

#include <QDir>
#include <QStringList>

#include <cctype>
#include <memory>
#include <set>
#include <string>
#include <vector>

using nexttoonz_test::copyFixtureProject;
using nexttoonz_test::fixturePath;

namespace {

const int kFrames = 8;

std::unique_ptr<ToonzScene> loadScene(const TFilePath &path) {
  std::unique_ptr<ToonzScene> scene(new ToonzScene());
  scene->load(path);
  return scene;
}

// "level name:frame" for every row of every column; "" for empty cells.
std::vector<std::vector<std::string>> cellTable(TXsheet *xsh) {
  std::vector<std::vector<std::string>> table;
  for (int c = 0; c < xsh->getColumnCount(); ++c) {
    std::vector<std::string> column;
    for (int r = 0; r < xsh->getFrameCount(); ++r) {
      const TXshCell &cell = xsh->getCell(r, c);
      if (cell.isEmpty()) {
        column.push_back("");
        continue;
      }
      column.push_back(::to_string(cell.m_level->getName()) + ":" +
                       cell.m_frameId.expand());
    }
    table.push_back(column);
  }
  return table;
}

// Expectations for the scenes this file knows about. Scenes are discovered
// from the fixture folder (see fixtureScenes()), so a scene added to the
// reference project is loaded and round-tripped with the generic checks
// even before it gets a row here; kUnknown marks a value without a row.
const int kUnknown = -1;

struct SceneExpectation {
  std::string name;
  int columnCount;
  // framecount attribute of the <tnz> tag, read by
  // ToonzScene::loadFrameCount() without loading the scene.
  int tnzHeaderFrameCount;
};

// TXsheet::insertColumn() does not update the frame count (see
// XsheetOps.InsertZeraryColumnLeavesFrameCountStale), so the generator calls
// updateFrameCount() before saving; every header therefore says 8 frames.
// clang-format off
const SceneExpectation kKnownScenes[] = {
    // name                    columns  tnz header frame count
    {"vector_basic",           1,       8},
    {"toonz_raster_basic",     1,       8},
    {"raster_png_basic",       1,       8},
    {"pegbar_hierarchy",       2,       8},
    {"fx_blur_over_colorcard", 2,       8},
    {"subxsheet",              2,       8},
    {"checkerboard_camera",    1,       8},
};
// clang-format on

// Every scenes/*.tnz of the fixture project, with its row of kKnownScenes
// when there is one. Evaluated during static initialisation (QDir does not
// need a QCoreApplication), so the test list follows the folder; ctest picks
// up new scenes when the test binary is relinked.
std::vector<SceneExpectation> fixtureScenes() {
  std::vector<SceneExpectation> scenes;
  QDir dir(QString::fromUtf8(NEXTTOONZ_FIXTURE_DIR) + "/scenes");
  const QStringList files =
      dir.entryList(QStringList() << "*.tnz", QDir::Files, QDir::Name);
  for (const QString &file : files) {
    const std::string name = file.left(file.length() - 4).toStdString();
    SceneExpectation expectation{name, kUnknown, kUnknown};
    for (const SceneExpectation &known : kKnownScenes)
      if (known.name == name) expectation = known;
    scenes.push_back(expectation);
  }
  return scenes;
}

void PrintTo(const SceneExpectation &s, std::ostream *os) { *os << s.name; }

std::string sceneTestName(
    const ::testing::TestParamInfo<SceneExpectation> &info) {
  std::string name = info.param.name;
  for (char &c : name)
    if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
  return name;
}

// framecount attribute of a .tnz header (ToonzScene::loadFrameCount() reads
// only the <tnz> tag; it is a non-static member).
int headerFrameCount(const TFilePath &path) {
  ToonzScene scene;
  return scene.loadFrameCount(path);
}

TFilePath scenePath(const std::string &name) {
  return fixturePath("scenes/" + name + ".tnz");
}

}  // namespace

//=============================================================================
// Every scene

// Every row of kKnownScenes still has its scene in the fixture project.
TEST(SceneLoad, KnownScenesExist) {
  for (const SceneExpectation &known : kKnownScenes)
    EXPECT_TRUE(TFileStatus(scenePath(known.name)).doesExist()) << known.name;
}

class SceneLoad : public ::testing::TestWithParam<SceneExpectation> {};

TEST_P(SceneLoad, FrameColumnAndCameraCounts) {
  const SceneExpectation &expected  = GetParam();
  std::unique_ptr<ToonzScene> scene = loadScene(scenePath(expected.name));

  // load() returns silently with an empty scene when the project cannot be
  // found through scenes.xml.
  ASSERT_TRUE(scene->getProject() != nullptr);
  EXPECT_EQ(scene->getProject()->getName(), TFilePath("reference_project"));

  // Every reference scene is 8 frames at 320x180 (16 x 9 inches).
  EXPECT_EQ(scene->getFrameCount(), kFrames);
  TXsheet *xsh = scene->getXsheet();
  ASSERT_NE(xsh, nullptr);
  EXPECT_GT(xsh->getColumnCount(), 0);
  if (expected.columnCount != kUnknown)
    EXPECT_EQ(xsh->getColumnCount(), expected.columnCount);
  if (expected.tnzHeaderFrameCount != kUnknown)
    EXPECT_EQ(headerFrameCount(scenePath(expected.name)),
              expected.tnzHeaderFrameCount);

  TCamera *camera = scene->getCurrentCamera();
  ASSERT_NE(camera, nullptr);
  EXPECT_EQ(camera->getRes(), TDimension(320, 180));
  EXPECT_DOUBLE_EQ(camera->getSize().lx, 16.0);
  EXPECT_DOUBLE_EQ(camera->getSize().ly, 9.0);

  // Every simple level was found through the "+drawings" project folder and
  // its frames were loaded.
  TLevelSet *levels = scene->getLevelSet();
  for (int i = 0; i < levels->getLevelCount(); ++i) {
    TXshSimpleLevel *sl = levels->getLevel(i)->getSimpleLevel();
    if (!sl) continue;
    SCOPED_TRACE(::to_string(sl->getName()));
    EXPECT_TRUE(
        TSystem::doesExistFileOrLevel(scene->decodeFilePath(sl->getPath())))
        << ::to_string(sl->getPath());
    EXPECT_GT(sl->getFrameCount(), 0);
  }
}

INSTANTIATE_TEST_SUITE_P(ReferenceProject, SceneLoad,
                         ::testing::ValuesIn(fixtureScenes()), sceneTestName);

//=============================================================================
// Scene-specific structure

TEST(SceneLoadDetails, VectorBasicLevelAndCells) {
  std::unique_ptr<ToonzScene> scene = loadScene(scenePath("vector_basic"));
  TXsheet *xsh                      = scene->getXsheet();
  ASSERT_EQ(xsh->getColumnCount(), 1);
  for (int r = 0; r < kFrames; ++r) {
    const TXshCell &cell = xsh->getCell(r, 0);
    ASSERT_FALSE(cell.isEmpty()) << "row " << r;
    TXshSimpleLevel *sl = cell.getSimpleLevel();
    ASSERT_NE(sl, nullptr);
    EXPECT_EQ(sl->getType(), PLI_XSHLEVEL);
    EXPECT_EQ(sl->getName(), L"vec_circle");
    EXPECT_EQ(sl->getPath(), TFilePath("+drawings/vec_circle.pli"));
    EXPECT_EQ(cell.m_frameId, TFrameId(r + 1));
  }
  EXPECT_TRUE(xsh->getCell(kFrames, 0).isEmpty());
}

TEST(SceneLoadDetails, PegbarHierarchy) {
  std::unique_ptr<ToonzScene> scene = loadScene(scenePath("pegbar_hierarchy"));
  TXsheet *xsh                      = scene->getXsheet();
  ASSERT_EQ(xsh->getColumnCount(), 2);

  const TStageObjectId peg = TStageObjectId::PegbarId(0);
  for (int c = 0; c < 2; ++c) {
    TStageObjectId parent =
        xsh->getStageObject(TStageObjectId::ColumnId(c))->getParent();
    EXPECT_EQ(parent, peg) << "column " << c << " parent is "
                           << parent.toString();
  }
  // The pegbar itself hangs from the table.
  EXPECT_EQ(xsh->getStageObject(peg)->getParent(), TStageObjectId::TableId);

  TDoubleParam *pegX = xsh->getStageObject(peg)->getParam(TStageObject::T_X);
  ASSERT_EQ(pegX->getKeyframeCount(), 2);
  EXPECT_DOUBLE_EQ(pegX->getKeyframe(0).m_frame, 0.0);
  EXPECT_DOUBLE_EQ(pegX->getKeyframe(1).m_frame, kFrames - 1);
  EXPECT_DOUBLE_EQ(pegX->getValue(0), -4.0);
  EXPECT_DOUBLE_EQ(pegX->getValue(kFrames - 1), 4.0);

  TDoubleParam *pegAngle =
      xsh->getStageObject(peg)->getParam(TStageObject::T_Angle);
  ASSERT_EQ(pegAngle->getKeyframeCount(), 2);
  EXPECT_DOUBLE_EQ(pegAngle->getValue(kFrames - 1), 90.0);

  // Column 2 (index 1) is offset from the pegbar by a single key.
  TDoubleParam *dotX = xsh->getStageObject(TStageObjectId::ColumnId(1))
                           ->getParam(TStageObject::T_X);
  EXPECT_EQ(dotX->getKeyframeCount(), 1);
  EXPECT_DOUBLE_EQ(dotX->getValue(0), 3.0);

  // The camera zooms from 100% to 60%.
  TStageObjectId cam = xsh->getStageObjectTree()->getCurrentCameraId();
  TDoubleParam *camScale =
      xsh->getStageObject(cam)->getParam(TStageObject::T_Scale);
  ASSERT_EQ(camScale->getKeyframeCount(), 2);
  EXPECT_DOUBLE_EQ(camScale->getValue(0), 1.0);
  EXPECT_DOUBLE_EQ(camScale->getValue(kFrames - 1), 0.6);
}

TEST(SceneLoadDetails, FxBlurOverColorCard) {
  std::unique_ptr<ToonzScene> scene =
      loadScene(scenePath("fx_blur_over_colorcard"));
  TXsheet *xsh = scene->getXsheet();
  ASSERT_EQ(xsh->getColumnCount(), 2);
  FxDag *dag = xsh->getFxDag();

  // Column 1 is a zerary colour card connected directly to the xsheet node.
  TXshZeraryFxColumn *cardColumn = xsh->getColumn(0)->getZeraryFxColumn();
  ASSERT_NE(cardColumn, nullptr);
  TZeraryColumnFx *cardFx = cardColumn->getZeraryColumnFx();
  ASSERT_NE(cardFx, nullptr);
  ASSERT_NE(cardFx->getZeraryFx(), nullptr);
  EXPECT_EQ(cardFx->getZeraryFx()->getFxType(), "colorCardFx");
  EXPECT_TRUE(dag->getTerminalFxs()->containsFx(cardFx));

  // Column 2 goes through the blur; the blur, not the column, is terminal.
  TFx *columnFx = xsh->getColumn(1)->getFx();
  ASSERT_NE(columnFx, nullptr);
  EXPECT_FALSE(dag->getTerminalFxs()->containsFx(columnFx));

  TFx *blur        = nullptr;
  TFxSet *terminal = dag->getTerminalFxs();
  for (int i = 0; i < terminal->getFxCount(); ++i)
    if (terminal->getFx(i)->getFxType() == "STD_blurFx")
      blur = terminal->getFx(i);
  ASSERT_NE(blur, nullptr) << "no STD_blurFx connected to the xsheet node";
  EXPECT_EQ(terminal->getFxCount(), 2);
  EXPECT_EQ(blur->getFxId(), L"Blur01");
  // Characterisation: FxDag keys its id table by the lower-cased fx id but
  // getFxById() does not lower-case its argument, so the id as returned by
  // getFxId() is not found.
  EXPECT_EQ(dag->getFxById(L"blur01"), blur);
  EXPECT_EQ(dag->getFxById(L"Blur01"), nullptr);
  EXPECT_TRUE(dag->getInternalFxs()->containsFx(blur));

  ASSERT_GE(blur->getInputPortCount(), 1);
  EXPECT_EQ(blur->getInputPort(0)->getFx(), columnFx);

  TDoubleParam *value =
      dynamic_cast<TDoubleParam *>(blur->getParams()->getParam("value"));
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->getKeyframeCount(), 0);
  EXPECT_DOUBLE_EQ(value->getDefaultValue(), 12.0);
  EXPECT_DOUBLE_EQ(value->getValue(0), 12.0);
}

TEST(SceneLoadDetails, SubXsheetChildLevel) {
  std::unique_ptr<ToonzScene> scene = loadScene(scenePath("subxsheet"));
  TXsheet *xsh                      = scene->getXsheet();
  ASSERT_EQ(xsh->getColumnCount(), 2);

  TXshChildLevel *child = xsh->getCell(0, 0).m_level
                              ? xsh->getCell(0, 0).m_level->getChildLevel()
                              : nullptr;
  ASSERT_NE(child, nullptr);
  TXsheet *sub = child->getXsheet();
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(sub->getColumnCount(), 1);
  EXPECT_EQ(sub->getFrameCount(), kFrames);
  EXPECT_EQ(sub->getCell(0, 0).getSimpleLevel()->getName(), L"vec_sub");

  // Both parent columns expose the same child level; column 2 starts half
  // way through it.
  for (int r = 0; r < kFrames; ++r) {
    SCOPED_TRACE("row " + std::to_string(r));
    const TXshCell &a = xsh->getCell(r, 0);
    const TXshCell &b = xsh->getCell(r, 1);
    EXPECT_EQ(a.m_level.getPointer(), child);
    EXPECT_EQ(b.m_level.getPointer(), child);
    EXPECT_EQ(a.m_frameId, TFrameId(r + 1));
    EXPECT_EQ(b.m_frameId, TFrameId((r + kFrames / 2) % kFrames + 1));
  }
}

TEST(SceneLoadDetails, CheckerboardCamera) {
  std::unique_ptr<ToonzScene> scene =
      loadScene(scenePath("checkerboard_camera"));
  TXsheet *xsh = scene->getXsheet();
  ASSERT_EQ(xsh->getColumnCount(), 1);
  TXshZeraryFxColumn *column = xsh->getColumn(0)->getZeraryFxColumn();
  ASSERT_NE(column, nullptr);
  EXPECT_EQ(column->getZeraryColumnFx()->getZeraryFx()->getFxType(),
            "checkBoardFx");

  TStageObjectId cam   = xsh->getStageObjectTree()->getCurrentCameraId();
  TStageObject *camera = xsh->getStageObject(cam);
  EXPECT_EQ(camera->getParam(TStageObject::T_X)->getKeyframeCount(), 2);
  EXPECT_DOUBLE_EQ(camera->getParam(TStageObject::T_X, kFrames - 1), 2.0);
  EXPECT_DOUBLE_EQ(camera->getParam(TStageObject::T_Angle, kFrames - 1), 15.0);
}

//=============================================================================
// Save round trip
//
// Re-saving a loaded scene is not byte-identical to the generator's output,
// so the round trips compare structure, not files:
//  - loading fills in default png format properties for the output settings
//    (+outputs/<scene>.png), which the next save writes out;
//  - the terminal fx list is a std::set<TFx *> and is written in pointer
//    order, so it can change from one save to the next (seen on
//    fx_blur_over_colorcard and pegbar_hierarchy); the order of levels in a
//    cast folder changed on pegbar_hierarchy's first re-save as well.

namespace {

// Parent of every column's stage object, as strings.
std::vector<std::string> columnParents(TXsheet *xsh) {
  std::vector<std::string> parents;
  for (int c = 0; c < xsh->getColumnCount(); ++c)
    parents.push_back(xsh->getStageObject(TStageObjectId::ColumnId(c))
                          ->getParent()
                          .toString());
  return parents;
}

// Fx types of the fxs connected to the xsheet node.
std::multiset<std::string> terminalFxTypes(TXsheet *xsh) {
  std::multiset<std::string> types;
  TFxSet *terminal = xsh->getFxDag()->getTerminalFxs();
  for (int i = 0; i < terminal->getFxCount(); ++i)
    types.insert(terminal->getFx(i)->getFxType());
  return types;
}

}  // namespace

TEST(SceneSave, VectorBasicReloadsWithSameCells) {
  // Work in a scratch copy of the whole project: scenes.xml must be found
  // above the scene for the "+drawings" folder alias to resolve.
  const TFilePath project = copyFixtureProject("scene_save_vector_basic");
  const TFilePath source  = project + "scenes" + "vector_basic.tnz";
  const TFilePath resaved = project + "scenes" + "vector_basic_resaved.tnz";

  std::unique_ptr<ToonzScene> original = loadScene(source);
  ASSERT_TRUE(original->getProject() != nullptr);
  ASSERT_EQ(original->getFrameCount(), kFrames);

  original->save(resaved, nullptr, /*saveSceneIcon=*/false);
  ASSERT_TRUE(TFileStatus(resaved).doesExist());
  // The temporary file used while saving is gone.
  EXPECT_FALSE(
      TFileStatus(TFilePath(resaved.getWideString() + L".tmp")).doesExist());
  EXPECT_EQ(headerFrameCount(resaved), kFrames);

  std::unique_ptr<ToonzScene> reloaded = loadScene(resaved);
  ASSERT_TRUE(reloaded->getProject() != nullptr);
  EXPECT_EQ(reloaded->getFrameCount(), original->getFrameCount());
  EXPECT_EQ(reloaded->getXsheet()->getColumnCount(),
            original->getXsheet()->getColumnCount());
  EXPECT_EQ(cellTable(reloaded->getXsheet()), cellTable(original->getXsheet()));
  EXPECT_EQ(reloaded->getCurrentCamera()->getRes(),
            original->getCurrentCamera()->getRes());

  // The level keeps its project-relative path and still loads.
  TXshSimpleLevel *sl = reloaded->getXsheet()->getCell(0, 0).getSimpleLevel();
  ASSERT_NE(sl, nullptr);
  EXPECT_EQ(sl->getPath(), TFilePath("+drawings/vec_circle.pli"));
  EXPECT_EQ(sl->getFrameCount(), kFrames);
}

class SceneSaveRoundTrip : public ::testing::TestWithParam<SceneExpectation> {};

TEST_P(SceneSaveRoundTrip, ReloadsWithSameStructure) {
  const std::string name  = GetParam().name;
  const TFilePath project = copyFixtureProject("scene_save_" + name);
  const TFilePath resaved = project + "scenes" + (name + "_resaved.tnz");

  std::unique_ptr<ToonzScene> original =
      loadScene(project + "scenes" + (name + ".tnz"));
  ASSERT_TRUE(original->getProject() != nullptr);
  original->save(resaved, nullptr, /*saveSceneIcon=*/false);

  std::unique_ptr<ToonzScene> reloaded = loadScene(resaved);
  ASSERT_TRUE(reloaded->getProject() != nullptr);

  TXsheet *a = original->getXsheet();
  TXsheet *b = reloaded->getXsheet();
  EXPECT_EQ(b->getFrameCount(), a->getFrameCount());
  // Saving writes the recomputed frame count, fixing a stale header.
  EXPECT_EQ(headerFrameCount(resaved), kFrames);
  EXPECT_EQ(b->getColumnCount(), a->getColumnCount());
  EXPECT_EQ(cellTable(b), cellTable(a));
  EXPECT_EQ(columnParents(b), columnParents(a));
  EXPECT_EQ(terminalFxTypes(b), terminalFxTypes(a));
  EXPECT_EQ(reloaded->getLevelSet()->getLevelCount(),
            original->getLevelSet()->getLevelCount());
  EXPECT_EQ(reloaded->getCurrentCamera()->getRes(),
            original->getCurrentCamera()->getRes());
}

INSTANTIATE_TEST_SUITE_P(ReferenceProject, SceneSaveRoundTrip,
                         ::testing::ValuesIn(fixtureScenes()), sceneTestName);
