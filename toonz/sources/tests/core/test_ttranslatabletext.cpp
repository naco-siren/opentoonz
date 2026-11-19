// TTranslatableText and the translator of text the core stores in data
// (step 7 of doc/nexttoonz_core_noqt.md), in both builds.

#include <gtest/gtest.h>

#include "ttranslatabletext.h"

#include <string>
#include <vector>

namespace {
std::vector<std::string> asked;
std::wstring injected(const TTranslatableText &t) {
  asked.push_back(t.context + "|" + t.source);
  return L"<" + std::wstring(t.source.begin(), t.source.end()) + L">";
}
}  // namespace

TEST(TTranslatableText, HoldsContextSourceAndWhetherToTranslate) {
  TTranslatableText t("TFurStrokeStyle",
                      QT_TRANSLATE_NOOP("TFurStrokeStyle", "Herringbone"));
  EXPECT_EQ(t.context, "TFurStrokeStyle");
  EXPECT_EQ(t.source, "Herringbone");
  EXPECT_TRUE(t.translate);
  TTranslatableText u = TTranslatableText::untranslated("caf\xC3\xA9");
  EXPECT_EQ(u.context, "");
  EXPECT_EQ(u.source, "caf\xC3\xA9");
  EXPECT_FALSE(u.translate);
  EXPECT_FALSE(TTranslatableText().translate);
}

TEST(TTranslatableText, DataTranslatorIsTheInjectedOne) {
  asked.clear();
  DataTranslator builtIn = setDataTranslator(&injected);
  ASSERT_NE(builtIn, nullptr);
  EXPECT_EQ(translateForData(TTranslatableText("QObject", "colors")), L"<colors>");
  EXPECT_EQ(asked, std::vector<std::string>{"QObject|colors"});
  // text not to be translated never reaches the translator
  EXPECT_EQ(translateForData(TTranslatableText::untranslated("caf\xC3\xA9")),
            L"café");
  EXPECT_EQ(asked.size(), 1u);
  EXPECT_EQ(setDataTranslator(nullptr), &injected);
  EXPECT_EQ(setDataTranslator(nullptr), builtIn);
#ifndef QT_CORE_LIB
  // without Qt, the source as it is until a program injects a translator
  EXPECT_EQ(translateForData(TTranslatableText("QObject", "colors")), L"colors");
#endif
}
