// The QString and QStringList members of TImageReader, TImageWriter
// (timage_io.h) and Tiio::Writer (tiio.h), for the code on the Qt side;
// built with Qt only (doc/nexttoonz_core_noqt.md, step 5).

#include "timage_io.h"
#include "tiio.h"

#include <QStringList>

namespace {
void append(QStringList &out, const std::vector<std::string> &names) {
  for (const std::string &n : names) out.push_back(QString::fromStdString(n));
}
}  // namespace

void TImageReader::define(QString extension, TImageReaderCreateProc *proc) {
  define(extension.toStdString(), proc);
}

void TImageWriter::define(QString extension, TImageWriterCreateProc *proc,
                          bool isRenderFormat) {
  define(extension.toStdString(), proc, isRenderFormat);
}

void TImageReader::getSupportedFormats(QStringList &names) {
  std::vector<std::string> v;
  getSupportedFormats(v);
  append(names, v);
}

void TImageWriter::getSupportedFormats(QStringList &names,
                                       bool onlyRenderFormat) {
  std::vector<std::string> v;
  getSupportedFormats(v, onlyRenderFormat);
  append(names, v);
}

void Tiio::Writer::getSupportedFormats(QStringList &formats,
                                       bool onlyRenderFormats) {
  std::vector<std::string> v;
  getSupportedFormats(v, onlyRenderFormats);
  append(formats, v);
}
