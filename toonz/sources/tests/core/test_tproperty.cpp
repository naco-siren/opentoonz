// The names of TProperty and TEnumProperty kept as std::wstring (step 8d-0
// of doc/nexttoonz_core_noqt.md).

#include <gtest/gtest.h>

#include "tproperty.h"

TEST(TPropertyNames, ThePropertysNameIsItsUINameUntilOneIsSet) {
  TIntProperty p("Size", 1, 100, 10);
  EXPECT_EQ(p.getUINameW(), L"Size");
  p.setUINameW(L"Taille");
  EXPECT_EQ(p.getUINameW(), L"Taille");
  EXPECT_EQ(p.getName(), "Size");
}

TEST(TPropertyNames, AValueIsItsOwnUINameUntilOneIsSet) {
  TEnumProperty e("Mode");
  e.addValue(L"Lines");
  e.addValueWithUINameW(L"Areas", L"Zones", L"areas_icon");
  ASSERT_EQ(e.getItems().size(), 2u);
  EXPECT_EQ(e.getItems()[0].m_uiName, L"Lines");
  EXPECT_EQ(e.getItems()[0].m_iconName, L"");
  EXPECT_EQ(e.getItems()[1].m_uiName, L"Zones");
  EXPECT_EQ(e.getItems()[1].m_iconName, L"areas_icon");
  e.setItemUINameW(L"Lines", L"Traits");
  EXPECT_EQ(e.getItems()[0].m_uiName, L"Traits");
  EXPECT_THROW(e.setItemUINameW(L"Nothing", L"x"), TProperty::RangeError);
}

TEST(TPropertyNames, AGroupTakesTheUINamesOfAnother) {
  TPropertyGroup ref, mine;
  TEnumProperty *r = new TEnumProperty("Mode");
  r->addValueWithUINameW(L"Lines", L"Traits");
  r->addValueWithUINameW(L"Areas", L"Zones");
  r->setUINameW(L"Mode (fr)");
  ref.bind(*r);
  TEnumProperty *m = new TEnumProperty("Mode");
  m->addValue(L"Areas");
  m->addValue(L"Other");
  mine.bind(*m);
  mine.assignUINames(&ref);
  EXPECT_EQ(m->getUINameW(), L"Mode (fr)");
  EXPECT_EQ(m->getItems()[0].m_uiName, L"Zones");
  EXPECT_EQ(m->getItems()[1].m_uiName, L"Other");
  delete r;
  delete m;
}
