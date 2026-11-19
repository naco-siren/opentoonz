// The QString members of TContentHistory (tcontenthistory.h) and the date
// it writes, in the build with Qt (doc/nexttoonz_core_noqt.md, Q2 and 8d-0).

#include "tcontenthistory.h"

#include <QDateTime>
#include <QString>

void TContentHistory::deserialize(const QString &history) {
  deserializeW(history.toStdWString());
}

const QString TContentHistory::serialize() const {
  return QString::fromStdWString(serializeW());
}

std::wstring formatHistoryDateWithQt(long long msecs) {
  return QDateTime::fromMSecsSinceEpoch(msecs)
      .toString("dd MMM yy   hh:mm")
      .toStdWString();
}
