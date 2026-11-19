// The translator of the text the core stores in data (ttranslatabletext.h,
// doc/nexttoonz_core_noqt.md step 7). The Qt one, the default of the build
// with Qt, is in ttranslatabletext_qt.cpp.

#include "ttranslatabletext.h"
#include "tconvert.h"

#include <atomic>

#ifdef QT_CORE_LIB
// ttranslatabletext_qt.cpp
std::wstring translateForDataWithQt(const TTranslatableText& text);
#endif

namespace {

// the source as QString::fromUtf8 read it
std::wstring sourceAsItIs(const TTranslatableText& text) {
  return ::to_wstring(text.source);
}

DataTranslator builtInTranslator() {
#ifdef QT_CORE_LIB
  return &translateForDataWithQt;
#else
  return &sourceAsItIs;
#endif
}

std::atomic<DataTranslator> currentTranslator{nullptr};

}  // namespace

std::wstring translateForData(const TTranslatableText& text) {
  if (!text.translate) return sourceAsItIs(text);
  DataTranslator t = currentTranslator.load();
  return (t ? t : builtInTranslator())(text);
}

DataTranslator setDataTranslator(DataTranslator translator) {
  DataTranslator previous = currentTranslator.exchange(translator);
  return previous ? previous : builtInTranslator();
}
