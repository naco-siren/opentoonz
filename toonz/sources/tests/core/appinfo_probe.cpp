// Prints what the core and Qt say the executable's path is, before and after
// the QCoreApplication exists; test_tsystem_qt.cpp runs it launched in
// different ways (doc/nexttoonz_core_noqt.md, step 6c). One "name=value"
// per line.

#include "../../common/tsystem/tsystem_qtsemantics.h"
#include "tconvert.h"
#include "tfilepath.h"
#include "tsystem.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

namespace {
void print(const char *name, const std::wstring &value) {
  std::printf("%s=%s\n", name, ::to_string(value).c_str());
}
}  // namespace

int main(int argc, char *argv[]) {
  print("core.before.file", TSystemQtSemantics::applicationFilePath());
  print("core.before.dir", TSystemQtSemantics::applicationDirPath());
  print("core.before.bin", TSystem::getBinDir().getWideString());
  QCoreApplication app(argc, argv);
  print("qt.file", QCoreApplication::applicationFilePath().toStdWString());
  print("qt.dir", QCoreApplication::applicationDirPath().toStdWString());
  // TSystem::getBinDir before step 6c
  print("qt.bin",
        TFilePath(QCoreApplication::applicationFilePath().toStdString())
            .getParentDir()
            .getWideString());
  print("core.after.file", TSystemQtSemantics::applicationFilePath());
  print("core.after.dir", TSystemQtSemantics::applicationDirPath());
  print("core.after.bin", TSystem::getBinDir().getWideString());
  return 0;
}
