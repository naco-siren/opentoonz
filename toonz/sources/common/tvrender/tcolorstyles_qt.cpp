// The members of TColorStyle that give Qt types (tcolorstyles.h): the
// style's texts translated for the interface, as the styles translated them
// themselves before step 7 of doc/nexttoonz_core_noqt.md. The build without
// Qt leaves this file out.

#include "tcolorstyles.h"
#include "tsimplecolorstyles.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

namespace {

QString toQString(const TTranslatableText& text) {
  if (text.translate)
    return QCoreApplication::translate(text.context.c_str(),
                                       text.source.c_str());
  return QString::fromUtf8(text.source.c_str());
}

}  // namespace

QString TColorStyle::getDescription() const {
  return toQString(getDescriptionText());
}

QString TColorStyle::getParamNames(int index) const {
  return toQString(getParamNameText(index));
}

void TColorStyle::getParamRange(int pIndex, QStringList& stringItems) const {
  std::vector<TTranslatableText> items;
  getParamRangeText(pIndex, items);
  for (const TTranslatableText& item : items) stringItems << toQString(item);
}

QString TrailStyles::sourceFilters() {
  return QString::fromStdString(sourceFilterPatterns());
}
