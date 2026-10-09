// TXsheet and TStageObject operations on a fresh scene, without any file
// I/O: cell editing, column insertion/removal, clearing vs removing cells,
// frame-count bookkeeping and stage-object parenting.

#include "toonzlib_test_env.h"

#include <gtest/gtest.h>

#include "tdoubleparam.h"
#include "tfx.h"
#include "tfxutil.h"
#include "toonz/toonzscene.h"
#include "toonz/tproject.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshcolumn.h"
#include "toonz/txshlevel.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshzeraryfxcolumn.h"
#include "toonz/tcolumnfx.h"
#include "toonz/fxdag.h"
#include "toonz/tcolumnfxset.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjectid.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/stage.h"

#include <memory>
#include <string>

namespace {

class XsheetOps : public ::testing::Test {
protected:
  void SetUp() override {
    m_scene.reset(new ToonzScene());
    TProjectManager::instance()->initializeScene(m_scene.get());
    m_xsh = m_scene->getXsheet();
    // Level names that do not exist in the reference project, so
    // createNewLevel() keeps them unchanged.
    m_a = m_scene->createNewLevel(PLI_XSHLEVEL, L"xsh_test_a");
    m_b = m_scene->createNewLevel(PLI_XSHLEVEL, L"xsh_test_b");
    ASSERT_NE(m_a, nullptr);
    ASSERT_NE(m_b, nullptr);
  }

  TXshCell a(int frame) const { return TXshCell(m_a, TFrameId(frame)); }
  TXshCell b(int frame) const { return TXshCell(m_b, TFrameId(frame)); }

  // Fills rows [0, count) of col with level frames 1..count.
  void fill(int col, TXshLevel *level, int count) {
    for (int r = 0; r < count; ++r)
      ASSERT_TRUE(m_xsh->setCell(r, col, TXshCell(level, TFrameId(r + 1))));
  }

  std::unique_ptr<ToonzScene> m_scene;
  TXsheet *m_xsh = nullptr;
  TXshLevel *m_a = nullptr;
  TXshLevel *m_b = nullptr;
};

}  // namespace

TEST_F(XsheetOps, FreshSceneIsEmpty) {
  EXPECT_EQ(m_xsh->getColumnCount(), 0);
  EXPECT_EQ(m_xsh->getFrameCount(), 0);
  EXPECT_EQ(m_scene->getFrameCount(), 0);
  EXPECT_TRUE(m_scene->isUntitled());
  EXPECT_EQ(m_a->getName(), L"xsh_test_a");
  EXPECT_EQ(m_a->getType(), PLI_XSHLEVEL);
  // The current project's camera (from the reference project) was cloned.
  EXPECT_NE(m_scene->getCurrentCamera(), nullptr);
}

TEST_F(XsheetOps, SetCellGetCell) {
  ASSERT_TRUE(m_xsh->setCell(0, 0, a(1)));
  EXPECT_EQ(m_xsh->getColumnCount(), 1);
  EXPECT_EQ(m_xsh->getFrameCount(), 1);
  EXPECT_EQ(m_xsh->getCell(0, 0), a(1));

  // Setting a cell further down extends the frame count; the gap is empty.
  ASSERT_TRUE(m_xsh->setCell(4, 0, a(5)));
  EXPECT_EQ(m_xsh->getFrameCount(), 5);
  EXPECT_TRUE(m_xsh->getCell(2, 0).isEmpty());
  EXPECT_EQ(m_xsh->getCell(4, 0).m_frameId, TFrameId(5));

  // Writing to column 2 creates the columns in between.
  ASSERT_TRUE(m_xsh->setCell(1, 2, b(7)));
  EXPECT_EQ(m_xsh->getColumnCount(), 3);
  EXPECT_TRUE(m_xsh->isColumnEmpty(1));
  EXPECT_EQ(m_xsh->getCell(1, 2).getSimpleLevel()->getName(), L"xsh_test_b");

  // Out-of-range reads return the empty cell.
  EXPECT_TRUE(m_xsh->getCell(0, 10).isEmpty());
  EXPECT_TRUE(m_xsh->getCell(100, 0).isEmpty());
  // Negative coordinates are rejected.
  EXPECT_FALSE(m_xsh->setCell(-1, 0, a(1)));

  int r0 = -1, r1 = -1;
  EXPECT_EQ(m_xsh->getCellRange(0, r0, r1), 5);
  EXPECT_EQ(r0, 0);
  EXPECT_EQ(r1, 4);

  // Overwriting a cell with an empty one clears it.
  ASSERT_TRUE(m_xsh->setCell(4, 0, TXshCell()));
  EXPECT_TRUE(m_xsh->getCell(4, 0).isEmpty());
  EXPECT_EQ(m_xsh->getCellRange(0, r0, r1), 1);
}

TEST_F(XsheetOps, InsertAndRemoveColumnShiftCells) {
  fill(0, m_a, 3);
  fill(1, m_b, 5);
  ASSERT_EQ(m_xsh->getColumnCount(), 2);

  m_xsh->insertColumn(0);
  EXPECT_EQ(m_xsh->getColumnCount(), 3);
  EXPECT_TRUE(m_xsh->isColumnEmpty(0));
  EXPECT_EQ(m_xsh->getCell(0, 1), a(1));
  EXPECT_EQ(m_xsh->getCell(4, 2), b(5));
  EXPECT_EQ(m_xsh->getFrameCount(), 5);

  m_xsh->removeColumn(0);
  EXPECT_EQ(m_xsh->getColumnCount(), 2);
  EXPECT_EQ(m_xsh->getCell(2, 0), a(3));
  EXPECT_EQ(m_xsh->getCell(4, 1), b(5));

  // Removing the first column moves the second into its place.
  m_xsh->removeColumn(0);
  EXPECT_EQ(m_xsh->getColumnCount(), 1);
  EXPECT_EQ(m_xsh->getCell(0, 0), b(1));
}

// Characterisation: removeColumn() does not recompute the frame count, so
// removing the longest column leaves getFrameCount() stale until something
// calls updateFrameCount() (the UI does it through its xsheet handle).
TEST_F(XsheetOps, RemoveColumnLeavesFrameCountStale) {
  fill(0, m_a, 3);
  fill(1, m_b, 6);
  ASSERT_EQ(m_xsh->getFrameCount(), 6);

  m_xsh->removeColumn(1);
  EXPECT_EQ(m_xsh->getColumnCount(), 1);
  EXPECT_EQ(m_xsh->getFrameCount(), 6);  // stale

  m_xsh->updateFrameCount();
  EXPECT_EQ(m_xsh->getFrameCount(), 3);
}

// Characterisation: inserting a column that already holds cells does not
// update the frame count either. The fixture generator builds
// checkerboard_camera this way (a pre-filled zerary colour-card column), so
// that scene was saved with framecount="0" in its header.
TEST_F(XsheetOps, InsertZeraryColumnLeavesFrameCountStale) {
  TFxP card = TFxUtil::makeColorCard(TPixel32(30, 50, 110, 255));
  TXshZeraryFxColumn *column = new TXshZeraryFxColumn(8);
  column->getZeraryColumnFx()->setZeraryFx(card.getPointer());
  m_xsh->insertColumn(0, column);  // the xsheet takes ownership

  EXPECT_EQ(m_xsh->getColumnCount(), 1);
  EXPECT_FALSE(m_xsh->isColumnEmpty(0));
  EXPECT_EQ(m_xsh->getColumn(0)->getMaxFrame(), 7);
  EXPECT_EQ(m_xsh->getFrameCount(), 0);  // stale

  // The column fx is connected to the xsheet node.
  EXPECT_TRUE(m_xsh->getFxDag()->getTerminalFxs()->containsFx(
      column->getZeraryColumnFx()));

  m_xsh->updateFrameCount();
  EXPECT_EQ(m_xsh->getFrameCount(), 8);
}

// Stage objects follow their columns when columns are inserted or removed.
TEST_F(XsheetOps, InsertColumnShiftsStageObjects) {
  fill(0, m_a, 2);
  fill(1, m_b, 2);
  m_xsh->getStageObject(TStageObjectId::ColumnId(1))
      ->getParam(TStageObject::T_X)
      ->setValue(0, 2.5);

  m_xsh->insertColumn(0);
  EXPECT_DOUBLE_EQ(m_xsh->getStageObject(TStageObjectId::ColumnId(2))
                       ->getParam(TStageObject::T_X, 0),
                   2.5);
  EXPECT_DOUBLE_EQ(m_xsh->getStageObject(TStageObjectId::ColumnId(1))
                       ->getParam(TStageObject::T_X, 0),
                   0.0);

  m_xsh->removeColumn(0);
  EXPECT_DOUBLE_EQ(m_xsh->getStageObject(TStageObjectId::ColumnId(1))
                       ->getParam(TStageObject::T_X, 0),
                   2.5);
}

// Characterisation: clearCells() blanks cells in place and removeCells()
// deletes them and shifts the cells below up. (The doc comments on the two
// functions in txsheet.h describe it the other way round.)
TEST_F(XsheetOps, ClearCellsBlanksInPlace) {
  fill(0, m_a, 6);

  m_xsh->clearCells(1, 0, 2);
  EXPECT_EQ(m_xsh->getCell(0, 0), a(1));
  EXPECT_TRUE(m_xsh->getCell(1, 0).isEmpty());
  EXPECT_TRUE(m_xsh->getCell(2, 0).isEmpty());
  EXPECT_EQ(m_xsh->getCell(3, 0), a(4));
  EXPECT_EQ(m_xsh->getFrameCount(), 6);

  // Clearing the tail shrinks the frame count.
  m_xsh->clearCells(4, 0, 2);
  EXPECT_EQ(m_xsh->getFrameCount(), 4);

  // Clearing everything leaves an empty column behind.
  m_xsh->clearCells(0, 0, 10);
  EXPECT_EQ(m_xsh->getColumnCount(), 1);
  EXPECT_TRUE(m_xsh->isColumnEmpty(0));
  EXPECT_EQ(m_xsh->getFrameCount(), 0);
}

TEST_F(XsheetOps, RemoveAndInsertCellsShift) {
  fill(0, m_a, 6);

  m_xsh->removeCells(1, 0, 2);
  EXPECT_EQ(m_xsh->getCell(0, 0), a(1));
  EXPECT_EQ(m_xsh->getCell(1, 0), a(4));
  EXPECT_EQ(m_xsh->getCell(3, 0), a(6));
  EXPECT_TRUE(m_xsh->getCell(4, 0).isEmpty());
  EXPECT_EQ(m_xsh->getFrameCount(), 4);

  m_xsh->insertCells(0, 0, 3);
  EXPECT_TRUE(m_xsh->getCell(0, 0).isEmpty());
  EXPECT_TRUE(m_xsh->getCell(2, 0).isEmpty());
  EXPECT_EQ(m_xsh->getCell(3, 0), a(1));
  EXPECT_EQ(m_xsh->getCell(6, 0), a(6));
  EXPECT_EQ(m_xsh->getFrameCount(), 7);
}

TEST_F(XsheetOps, StageObjectParenting) {
  fill(0, m_a, 1);
  fill(1, m_b, 1);
  const TStageObjectId col0 = TStageObjectId::ColumnId(0);
  const TStageObjectId col1 = TStageObjectId::ColumnId(1);
  const TStageObjectId peg  = TStageObjectId::PegbarId(0);

  // Columns hang from the table by default.
  EXPECT_EQ(m_xsh->getStageObject(col0)->getParent(), TStageObjectId::TableId);
  EXPECT_EQ(m_xsh->getStageObject(TStageObjectId::TableId)->getParent(),
            TStageObjectId::NoneId);

  // getStageObject() creates the pegbar on first access, under the table.
  TStageObject *pegbar = m_xsh->getStageObject(peg);
  ASSERT_NE(pegbar, nullptr);
  EXPECT_EQ(pegbar->getParent(), TStageObjectId::TableId);

  m_xsh->getStageObject(col0)->setParent(peg);
  m_xsh->getStageObject(col1)->setParent(peg);
  EXPECT_EQ(m_xsh->getStageObject(col0)->getParent(), peg);
  EXPECT_EQ(m_xsh->getStageObject(col1)->getParent(), peg);

  std::vector<TStageObject *> children;
  for (TStageObject *child : pegbar->getChildren()) children.push_back(child);
  EXPECT_EQ(children.size(), 2u);

  // Moving the pegbar moves its children: T_X is in inches, placements are
  // in stage units.
  pegbar->getParam(TStageObject::T_X)->setValue(0, 1.0);
  TAffine placement = m_xsh->getPlacement(col0, 0);
  EXPECT_NEAR(placement.a13, Stage::inch, 1e-9);
  EXPECT_NEAR(placement.a23, 0.0, 1e-9);

  // Re-parenting back to the table detaches the column from the pegbar.
  m_xsh->getStageObject(col0)->setParent(TStageObjectId::TableId);
  EXPECT_EQ(m_xsh->getStageObject(col0)->getParent(), TStageObjectId::TableId);
  EXPECT_NEAR(m_xsh->getPlacement(col0, 0).a13, 0.0, 1e-9);
  EXPECT_NEAR(m_xsh->getPlacement(col1, 0).a13, Stage::inch, 1e-9);
}
