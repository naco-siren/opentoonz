// The desktop's translator of the text the core stores in data
// (ttranslatabletext.h, doc/nexttoonz_core_noqt.md step 7). The build
// without Qt leaves this file out.

#include "ttranslatabletext.h"

#include <QCoreApplication>
#include <QString>

std::wstring translateForDataWithQt(const TTranslatableText& text) {
  return QCoreApplication::translate(text.context.c_str(), text.source.c_str())
      .toStdWString();
}
