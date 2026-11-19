// The LZO service of the build with Qt (tcodec.h): the lzocompress and
// lzodecompress helpers next to the executable, run through QProcess, as
// tcodec.cpp did before step 8d-0 of doc/nexttoonz_core_noqt.md.

#include "tcodec.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QStringList>

namespace {

bool lzoCompressWithQt(const QByteArray src, QByteArray &dst) {
#ifdef _WIN32
  QDir exeDir;
  if (QCoreApplication::applicationName() == "ToonzPreview") {
    QStringList paths = QCoreApplication::libraryPaths();
    exeDir            = QDir(paths.first());
  } else
    exeDir = QDir(QCoreApplication::applicationDirPath());
#else
  QDir exeDir(QCoreApplication::applicationDirPath());
#endif  // _WIN32

  QString compressExe = exeDir.filePath("lzocompress");
  QProcess process;
  process.start(compressExe, QStringList() << QString::number(src.size()));
  if (!process.waitForStarted()) return false;
  process.write(src);
  if (!process.waitForFinished()) return false;
  dst = process.readAll();
  return process.exitCode() == 0;
}

bool lzoDecompressWithQt(const QByteArray src, int dstSize, QByteArray &dst) {
#ifdef _WIN32
  QDir exeDir;
  if (QCoreApplication::applicationName() == "ToonzPreview") {
    QStringList paths = QCoreApplication::libraryPaths();
    exeDir            = QDir(paths.first());
    exeDir.cdUp();
  } else
    exeDir = QDir(QCoreApplication::applicationDirPath());
#else
  QDir exeDir(QCoreApplication::applicationDirPath());
#endif  // _WIN32
  QString decompressExe = exeDir.filePath("lzodecompress");
  QProcess process;
  process.start(decompressExe, QStringList() << QString::number(dstSize)
                                             << QString::number(src.size()));
  if (!process.waitForStarted()) return false;
  process.write(src);
  if (!process.waitForFinished()) return false;
  dst = process.readAll();
  return process.exitCode() == 0 && dst.size() == dstSize;
}

bool compressWithQt(const std::string &src, std::string &dst) {
  QByteArray out;
  if (!lzoCompressWithQt(QByteArray(src.data(), int(src.size())), out))
    return false;
  dst.assign(out.constData(), size_t(out.size()));
  return true;
}

bool decompressWithQt(const std::string &src, int dstSize, std::string &dst) {
  QByteArray out;
  if (!lzoDecompressWithQt(QByteArray(src.data(), int(src.size())), dstSize,
                           out))
    return false;
  dst.assign(out.constData(), size_t(out.size()));
  return true;
}

const TLzoService qtLzoService = {&compressWithQt, &decompressWithQt};

}  // namespace

const TLzoService *lzoServiceWithQt() { return &qtLzoService; }
