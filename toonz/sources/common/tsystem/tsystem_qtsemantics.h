#pragma once

#ifndef TSYSTEM_QTSEMANTICS_H
#define TSYSTEM_QTSEMANTICS_H

// Inside the core only; NOT a public interface (it is not in include/).
//
// File-system and process helpers that do what Qt 5.15 did, rule for rule
// (QFile::encodeName, QDir, QFile, QFileInfo, QCoreApplication), defined in
// tsystem.cpp. TSystem uses them, and tenv.cpp uses them where it used Qt
// directly, so that both behave as they did with Qt
// (doc/nexttoonz_core_noqt.md, steps 6a and 6c). On Windows the ones tenv.cpp
// uses are Qt itself (tsystem_qt.cpp).

#include <string>

namespace TSystemQtSemantics {

//! QFile::encodeName / QFile::decodeName (NFD and NFC on Apple)
std::string encodeName(const std::wstring &path);
std::wstring decodeName(const std::string &bytes);

//! QDir::cleanPath
std::wstring cleanPath(const std::wstring &path);
//! QFileInfo(path).absoluteFilePath(), absolutePath(), canonicalFilePath()
std::wstring absoluteFilePath(const std::wstring &path);
std::wstring absolutePath(const std::wstring &path);
std::wstring canonicalFilePath(const std::wstring &path);

//! QDir::currentPath, QDir::homePath, QDir::tempPath
std::wstring currentPath();
std::wstring homePath();
std::wstring tempPath();

//! QCoreApplication::applicationFilePath and applicationDirPath, also when
//! there is no QCoreApplication (they were empty then; step 6c's decision)
std::wstring applicationFilePath();
std::wstring applicationDirPath();

//! QString::toLatin1
std::string toLatin1(const std::wstring &s);

//! QString::fromLocal8Bit(qgetenv(name)): empty when it is not set
std::wstring environmentVariable(const char *name);

//! QDir().mkpath, QDir().rename, QDir(path).removeRecursively()
bool mkpath(const std::wstring &path);
bool rename(const std::wstring &oldName, const std::wstring &newName);
bool removeRecursively(const std::wstring &path);

//! QFile::copy
bool copyFileAsQt(const std::wstring &src, const std::wstring &dst);

//! tenv's first-run copy of a folder (copyDirOrFail): QDir().mkpath(dst),
//! then every entry of src, hidden and system ones too, by name ignoring
//! case; folders that are not links recursively, the rest with QFile::copy
bool copyDirTree(const std::wstring &dst, const std::wstring &src);

}  // namespace TSystemQtSemantics

#endif
