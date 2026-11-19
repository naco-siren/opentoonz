// The TSystem functions and TFileStatus of tnzcore before migration step 6a
// of doc/nexttoonz_core_noqt.md (common/tsystem/tsystem.cpp), kept verbatim
// but for the names (LegacyTSystem, LegacyTFileStatus) and the exception
// class, which is the real one, so that test_tsystem_qt.cpp can compare
// the new code with them. Built in the Qt build only.

#pragma once
#ifndef LEGACY_TSYSTEM_H
#define LEGACY_TSYSTEM_H

#include "tsystem.h"
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#ifndef TNZCORE_LIGHT

#include <QDateTime>



#include <QFileInfo>
class LegacyTFileStatus {
  bool m_exist;
  QFileInfo m_fileInfo;

public:
  LegacyTFileStatus(const TFilePath &path);
  QString getGroup() const;
  QString getUser() const;
  TINT64 getSize() const;
  QDateTime getLastAccessTime() const;
  QDateTime getLastModificationTime() const;
  QDateTime getCreationTime() const;
  bool isDirectory() const;
  bool isLink() const;
  QFile::Permissions getPermissions() const;

  bool doesExist() const { return m_exist; }
  bool isWritable() const { return (getPermissions() & QFile::WriteUser) != 0; }
  bool isReadable() const {
    return m_exist && (getPermissions() & QFile::ReadUser) != 0;
  }

  bool isWritableDir() const {
    return doesExist() && isDirectory() && isWritable();
  };
};

#endif

namespace LegacyTSystem {
// change names. No T (already in TSystem). Use the usual convention for
// compound names (capitals and no '_'). Let's use lowercase with initial
// capital e.g. MaxPathLen;

// extern const int MaxPathLen;      // Maximum length of full path
// extern const int MaxFNameLen;     // Maximum length of filename component
// extern const int MaxHostNameLen;  // Maximum length of the host name  ==64

bool doHaveMainLoop();
void hasMainLoop(bool state);

// For command-line tools that still create a QApplication (tcomposer,
// tcleanup): call it before the QApplication is constructed. On macOS it
// makes AppKit ignore saved window state, so that a previous crash of any
// program in the app bundle cannot leave the tool waiting on the modal
// "reopen windows?" alert. Does nothing on other platforms.
void disableWindowStateRestoration();

// void enableFrameGrouping(bool on);
// bool isFrameGroupingEnabled();

// add (at the end) to set
void readDirectory(TFilePathSet &fpset, const QDir &dir,
                         bool groupFrames = true);

void readDirectory(TFilePathSet &fpset, const TFilePath &path,
                         bool groupFrames = true, bool onlyFiles = false,
                         bool getHiddenFiles = false);
void readDirectory(TFilePathSet &fpset, const TFilePathSet &pathSet,
                         bool groupFrames = true, bool onlyFiles = false,
                         bool getHiddenFiles = false);
void readDirectoryTree(TFilePathSet &fpset, const TFilePath &path,
                             bool groupFrames = true, bool onlyFiles = false);
void readDirectoryTree(TFilePathSet &fpset, const TFilePathSet &pathSet,
                             bool groupFrames = true, bool onlyFiles = false);

// to retrieve the both lists with groupFrames option = on and off.
void readDirectory(TFilePathSet &groupFpSet, TFilePathSet &allFpSet,
                         const TFilePath &path);

// return the folder path list which is readable and executable
void readDirectory_Dir_ReadExe(TFilePathSet &dst, const TFilePath &path);

// return the folder item list which is readable and executable (only names)
void readDirectory_DirItems(QStringList &dst, const TFilePath &path);

// create a new set
TFilePathSet readDirectory(const TFilePath &path, bool groupFrames = true,
                                 bool onlyFiles      = false,
                                 bool getHiddenFiles = false);
TFilePathSet readDirectory(const TFilePathSet &pathSet,
                                 bool groupFrames    = true,
                                 bool onlyFiles      = false,
                                 bool getHiddenFiles = false);
TFilePathSet readDirectoryTree(const TFilePath &path,
                                     bool groupFrames = true,
                                     bool onlyFiles   = false);
TFilePathSet readDirectoryTree(const TFilePathSet &pathSet,
                                     bool groupFrames = true,
                                     bool onlyFiles   = false);

//
TFilePathSet packLevelNames(const TFilePathSet &);

TFilePath getHomeDirectory();
TFilePath getTempDir();
TFilePath getTestDir(std::string name = "verify_tnzcore");
TFilePath getBinDir();
TFilePath getDllDir();

/*!Returns the process id of the current process*/
int getProcessId();

// note: these can throw exceptions!
// mkdir also creates the parent if it doesn't exist
void mkDir(const TFilePath &path);
void rmDir(const TFilePath &path);
void rmDirTree(const TFilePath &path);
void copyDir(const TFilePath &dst, const TFilePath &src);
bool touchParentDir(const TFilePath &fp);

void touchFile(const TFilePath &path);
void copyFile(const TFilePath &dst, const TFilePath &src,
                    bool overwrite = true);
void renameFile(const TFilePath &dst, const TFilePath &src,
                      bool overwrite = true);
// Atomically replace dst with src where supported. On failure the
// existing destination is left in place.
void replaceFile(const TFilePath &dst, const TFilePath &src);
void deleteFile(const TFilePath &dst);
void hideFile(const TFilePath &dst);
void moveFileToRecycleBin(const TFilePath &fp);

void copyFileOrLevel_throw(const TFilePath &dst, const TFilePath &src);
bool renameImageSequence(const TFilePathSet &files,
                               const TFilePath &levelName, int prefixLength);
void renameFileOrLevel_throw(const TFilePath &dst, const TFilePath &src,
                                   bool renamePalette = false);
void removeFileOrLevel_throw(const TFilePath &fp);
void hideFileOrLevel_throw(const TFilePath &fp);
void moveFileOrLevelToRecycleBin_throw(const TFilePath &fp);

bool doesExistFileOrLevel(const TFilePath &fp);
bool copyFileOrLevel(const TFilePath &dst, const TFilePath &src);
bool renameFileOrLevel(const TFilePath &dst, const TFilePath &src,
                             bool renamePalette = false);
bool removeFileOrLevel(const TFilePath &fp);
bool hideFileOrLevel(const TFilePath &fp);
bool moveFileOrLevelToRecycleBin(const TFilePath &fp);

void sleep(TINT64 delay);
inline void sleep(int ms) { sleep(static_cast<TINT64>(ms)); }

TFilePathSet getDisks();

/*! returns disk size in Kbytes */
TINT64 getDiskSize(const TFilePath &);

/* returns free disk size in Kbytes */
TINT64 getFreeDiskSize(const TFilePath &);

/*! returns available physical (+ virtual mem if boolean=true)  memory in KBytes
 */
TINT64 getFreeMemorySize(bool onlyPhysicalMemory);

/*! return total physical (+ virtual mem if boolean=true) memory in kbytes */
TINT64 getMemorySize(bool onlyPhysicalMemory);

/*! return true if not enough memory. It can happen for 2 reasons:
      1) free physical memory is close to 0;
      2) the calling process has allocated the maximum amount of memory  allowed
   for a single process(typically, for a 32 bits machine, 2GB)*/

bool memoryShortage();

/*! run an external viewer */
// void showDocument(const TFilePath &dst);

int getProcessorCount();

enum CPUExtensions {
  CPUExtensionsNone = 0x00000000L,
  // CpuSupportsCpuId      = 0x00000001L,
  // CpuSupportsFpu        = 0x00000002L,
  // CpuSupportsMmx        = 0x00000004L,
  // CpuSupportsIntegerSse = 0x00000008L,
  CpuSupportsSse  = 0x00000010L,
  CpuSupportsSse2 = 0x00000020L,
  // CpuSupports3DNow      = 0x00000040L,
  // CpuSupports3DNowExt   = 0x00000080L
};

/*! returns a bit mask containing the CPU extensions supported */
long getCPUExtensions();

/*! enables/disables the CPU extensions, if available*/
// void enableCPUExtensions(bool on);

// things to do:

// some study on memory mapped files

// investigate
std::iostream openTemporaryFile();
// another idea is to have a TQualcosaP that returns a UniqueFileName
// and on the destructor deletes the file

// think about:
//  void setFileProtection(const TFilePath &path, ???);

void outputDebug(std::string s);

bool isUNC(const TFilePath &fp);

//! Returns the filepath in UNC format
// ex.: TFilePath("O:\\temp").toUNC() == TFilePath("\\\\vega\\PERSONALI\\temp")
// if "\\\\vega\\PERSONALI\\" is mounted as "O:\\" on the local host
// A TException is thrown if the the conversion is not possible

TFilePath toUNC(const TFilePath &fp);

/*!Returns the filepath in a format that refers to the local host
            ex.: TFilePath("\\\\dell530\\discoD\\temp").toLocalPath() ==
   TFilePath("C:\\temp")
      if the local host is "dell530"
     No conversion is done if the filepath already refers to the local host
     A TException is thrown if the the conversion is not possible
   */
TFilePath toLocalPath(const TFilePath &fp);

bool showDocument(const TFilePath &fp);

#ifndef TNZCORE_LIGHT
QDateTime getFileTime(const TFilePath &path);
QString getHostName();
QString getUserName();
QString getSystemValue(const TFilePath &name);
TFilePath getUniqueFile(QString field = "");
#endif

bool isDLLBlackListed(QString dllFile);

}  // namespace LegacyTSystem

#endif
