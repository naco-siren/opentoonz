// The QString and QStringList members of TSoundTrackReader and
// TSoundTrackWriter (tsound_io.h), for the code on the Qt side; built with Qt
// only (doc/nexttoonz_core_noqt.md, step 5).

#include "tsound_io.h"

#include <QStringList>

void TSoundTrackReader::define(QString extension,
                               TSoundTrackReaderCreateProc *proc) {
  define(extension.toStdString(), proc);
}

void TSoundTrackWriter::define(QString extension,
                               TSoundTrackWriterCreateProc *proc) {
  define(extension.toStdString(), proc);
}

void TSoundTrackReader::getSupportedFormats(QStringList &names) {
  std::vector<std::string> v;
  getSupportedFormats(v);
  for (const std::string &n : v) names.push_back(QString::fromStdString(n));
}

void TSoundTrackWriter::getSupportedFormats(QStringList &names) {
  std::vector<std::string> v;
  getSupportedFormats(v);
  for (const std::string &n : v) names.push_back(QString::fromStdString(n));
}
