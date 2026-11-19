// The QString members of TProperty and TEnumProperty (tproperty_qt.cpp, step
// 8d-0 of doc/nexttoonz_core_noqt.md): what goes in comes out, and what was
// never set is a null QString, as it was. Qt build only.

#include <gtest/gtest.h>

#include "tproperty.h"

#include <QString>

TEST(TPropertyQt, NamesComeBackAsTheyWent) {
  TIntProperty p("Size", 1, 100, 10);
  EXPECT_EQ(p.getQStringName(), QString("Size"));
  for (const QString &s :
       {QString::fromUtf8("Gr\xc3\xb6\xc3\x9f" "e"), QString::fromUtf8("\xe5\xa4\xa7\xe3\x81\x8d\xe3\x81\x95"),
        QString::fromUtf8("\xf0\x9d\x84\x9e clef"), QString(), QString("")}) {
    p.setQStringName(s);
    EXPECT_EQ(p.getQStringName(), s);
    EXPECT_EQ(p.getQStringName().isEmpty(), s.isEmpty());
  }
}

TEST(TPropertyQt, ItemsGiveTheirNamesAndIcons) {
  TEnumProperty e("Mode");
  e.addValue(L"Lines");
  e.addValue(L"Areas", "areas_icon");
  e.addValueWithUIName(L"Both", QString::fromUtf8("Les deux"));
  e.addValueWithUIName(L"None", "Aucun", "none_icon");
  const TEnumProperty::Items &items = e.getItems();
  ASSERT_EQ(items.size(), 4u);
  EXPECT_EQ(items[0].getUIName(), QString("Lines"));
  EXPECT_TRUE(items[0].getIconName().isNull());
  EXPECT_EQ(items[1].getIconName(), QString("areas_icon"));
  EXPECT_EQ(items[2].getUIName(), QString::fromUtf8("Les deux"));
  EXPECT_EQ(items[3].getUIName(), QString("Aucun"));
  EXPECT_EQ(items[3].getIconName(), QString("none_icon"));
  e.setItemUIName(L"Lines", QString::fromUtf8("Traits"));
  EXPECT_EQ(e.getItems()[0].getUIName(), QString("Traits"));

  // An item made with nothing in it, as when a range is given at once.
  TEnumProperty::Range range{L"x", L"y"};
  TEnumProperty r("R", range, L"x");
  EXPECT_TRUE(r.getItems()[0].getUIName().isNull());
  EXPECT_TRUE(r.getItems()[0].getIconName().isNull());
}
