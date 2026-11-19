// The QString members of ParsedPli (pli_io.h), in the build with Qt only
// (doc/nexttoonz_core_noqt.md, Q2 and 8d).

#include "pli_io.h"

#include <QString>

void ParsedPli::setCreator(const QString &creator) {
  setCreatorW(creator.toStdWString());
}

QString ParsedPli::getCreator() const {
  return QString::fromStdWString(getCreatorW());
}
