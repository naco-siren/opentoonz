// The texts of the colour styles and the palette's page name through Qt's
// translation (step 7 of doc/nexttoonz_core_noqt.md): what the styles now
// give, and that a new palette's first page is named by the data translator.
// The full before/after comparison over every style and language is
// toonz/build-phase1/ab/phase2/translations (not in the repository).
// Built in the Qt build only.

#include <gtest/gtest.h>

#include "tcolorstyles.h"
#include "tpalette.h"
#include "tsimplecolorstyles.h"
#include "ttranslatabletext.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <memory>

namespace {
std::wstring bracketed(const TTranslatableText &t) {
  return L"[" + QString::fromStdString(t.context + "/" + t.source).toStdWString() + L"]";
}
}  // namespace

TEST(TTranslatableTextQt, StylesSayWhatToTranslate) {
  TCenterLineStrokeStyle centerLine;
  TTranslatableText d = centerLine.getDescriptionText();
  EXPECT_EQ(d.context, "TCenterLineStrokeStyle");
  EXPECT_EQ(d.source, "Constant");
  EXPECT_TRUE(d.translate);
  // with no translator installed Qt gives the source back
  EXPECT_EQ(centerLine.getDescription(), QString("Constant"));
  EXPECT_EQ(centerLine.getParamNames(0), QString("Thickness"));

  TSolidColorStyle solid;
  EXPECT_FALSE(solid.getDescriptionText().translate);
  EXPECT_EQ(solid.getDescription(), QString("SolidColorStyle"));

  TRasterImagePatternStrokeStyle pattern;
  QStringList items;
  // through TColorStyle, as the interface calls it (the derived class hides
  // the base's other getParamRange overloads)
  static_cast<const TColorStyle &>(pattern).getParamRange(TrailStyles::cycleParam,
                                                          items);
  EXPECT_EQ(items, QStringList({"Off", "Forward", "Backward", "Repeat"}));
}

TEST(TTranslatableTextQt, NewPalettePageIsNamedByTheDataTranslator) {
  DataTranslator previous = setDataTranslator(&bracketed);
  std::unique_ptr<TPalette> p(new TPalette());
  EXPECT_EQ(p->getPage(0)->getName(), L"[QObject/colors]");
  setDataTranslator(previous);
  std::unique_ptr<TPalette> q(new TPalette());
  EXPECT_EQ(q->getPage(0)->getName(), L"colors");
}
