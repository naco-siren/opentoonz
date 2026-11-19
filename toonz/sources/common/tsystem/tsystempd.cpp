// Enable Unicode on Windows
#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#endif

// Standard C++ headers
#include <memory>
#include <set>

// Standard C headers
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

// Project-specific headers
#include "tsystem.h"
//#include "tunicode.h"  // Uncomment if needed
#include "tfilepath_io.h"
#include "tconvert.h"
#include "tenv.h"

#include <cstdio>


// Undefine PLATFORM to avoid conflicts
#undef PLATFORM

// Disable specific warnings on MSVC
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

// Platform-specific headers and definitions
#ifdef _WIN32
#define PLATFORM WIN32
#include <process.h>
#include <psapi.h>
#include <io.h>
#include <stdlib.h>
#include <direct.h>
#include <shellapi.h>
#include <sys/utime.h>
#include <lm.h>
// gmt: On my machine, this doesn't compile!!!
// #include "winsock2.h"
// #include "lmcons.h"
#endif

#ifdef LINUX
#define PLATFORM LINUX
#include <grp.h>
#include <utime.h>
#include <sys/param.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/dir.h>
#include <sys/sysinfo.h>
#include <sys/swap.h>
#include <sys/statfs.h>
#include <pwd.h>
#include <mntent.h>
#include <dlfcn.h>
#include <sys/time.h>
#endif

#ifdef FREEBSD
#define PLATFORM FREEBSD
#include <sys/param.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <string.h>
#include <unistd.h>
#include <sys/proc.h>
#include <sys/vmmeter.h>
#include <vm/vm_param.h>
#include <grp.h>
#include <utime.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/mount.h>
#include <pwd.h>
#include <dlfcn.h>
#define pagetok(__nb) ((__nb) * (getpagesize()))
#endif

#ifdef MACOSX
#define PLATFORM MACOSX
#include <grp.h>
#include <utime.h>
#include <sys/param.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/dir.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include <pwd.h>
#include <dlfcn.h>

// macOS-specific headers (the recycle bin; iOS has neither)
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE
#include "Carbon/Carbon.h"
#endif
#endif

#ifdef __sgi
#define PLATFORM SGI
#include <sys/param.h>
#include <unistd.h>
#include <grp.h>
#include <sys/dir.h>  // dirent.h
#include <sys/utime.h>
#include <sys/swap.h>
#include <sys/statfs.h>
#include <pwd.h>
#include <mntent.h>
#include <dlfcn.h>
#endif

#ifdef HAIKU
#define PLATFORM HAIKU
#include <grp.h>
#include <utime.h>
#include <sys/param.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <dirent.h>
#include <pwd.h>
#include <dlfcn.h>
#include <sys/time.h>
#endif

#ifndef PLATFORM
PLATFORM_NOT_SUPPORTED
#endif

using namespace std;

#ifdef _WIN32

wstring getFormattedMessage(DWORD lastError) {
  LPVOID lpMsgBuf;
  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, lastError,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),  // Default language
                (LPTSTR)&lpMsgBuf, 0, NULL);

  int wSize = MultiByteToWideChar(0, 0, (char *)lpMsgBuf, -1, 0, 0);
  if (!wSize) return wstring();

  std::unique_ptr<wchar_t[]> wBuffer(new wchar_t[wSize + 1]);
  MultiByteToWideChar(0, 0, (char *)lpMsgBuf, -1, wBuffer.get(), wSize);
  wBuffer[wSize] = '\0';
  wstring wmsg(wBuffer.get());

  LocalFree(lpMsgBuf);
  return wmsg;
}

#endif
//------------------------------------------------------------

void TSystem::outputDebug(string s) {
#ifdef TNZCORE_LIGHT
#ifdef _WIN32
  OutputDebugString((LPCWSTR)s.c_str());
#else
  cerr << s << endl;
#endif
#else
  // What qDebug() did with Qt's default message handler (as step 4 did
  // with qWarning, doc/nexttoonz_core_noqt.md).
  std::fprintf(stderr, "%s\n", s.c_str());
#endif
}

//------------------------------------------------------------

int TSystem::getProcessId() { return getpid(); }

//------------------------------------------------------------

bool TSystem::memoryShortage() {
#ifdef _WIN32

  MEMORYSTATUSEX memStatus;
  memStatus.dwLength = sizeof(MEMORYSTATUSEX);
  GlobalMemoryStatusEx(&memStatus);

  assert(memStatus.ullAvailPhys <= memStatus.ullTotalPhys);

  if (memStatus.ullAvailPhys <
      memStatus.ullTotalPhys *
          0.20)  // if available memory is less then 20% of total memory
    return true;

  PROCESS_MEMORY_COUNTERS c;
  c.cb     = sizeof(PROCESS_MEMORY_COUNTERS);
  BOOL ret = GetProcessMemoryInfo(GetCurrentProcess(), &c,
                                  sizeof(PROCESS_MEMORY_COUNTERS));
  assert(ret);

  return c.WorkingSetSize >
         memStatus.ullTotalVirtual *
             0.6;  // if total memory used by this process(WorkingSetSize) is
// half of max allocatable memory
//(ullTotalVirtual: on 32bits machines, typically it's 2GB)
// It's better "to stay large"; for values >0.6 this function may
// returns that there is memory, but for fragmentation the malloc fails the
// same!

#elif defined(MACOSX)

  // to be done...
  return false;

#elif defined(LINUX)

  // to be done...
  return false;

#elif defined(FREEBSD)

  // to be done...
  return false;

#elif defined(HAIKU)

  // to be done...
  return false;

#else

  @ @ @ERROR : PLATFORM NOT SUPPORTED

#endif
}

//------------------------------------------------------------

TINT64 TSystem::getFreeMemorySize(bool onlyPhysicalMemory) {
  TINT64 totalFree = 0;

#ifdef _WIN32

  MEMORYSTATUSEX buff;
  buff.dwLength = sizeof(MEMORYSTATUSEX);
  GlobalMemoryStatusEx(&buff);

  if (onlyPhysicalMemory)
    return buff.ullAvailPhys >> 10;
  else
    return buff.ullAvailPageFile >> 10;

#elif defined(__sgi)

  // check for virtual memory
  int numberOfResources =
      swapctl(SC_GETNSWP, 0); /* get number of swapping resources configured */

  if (numberOfResources == 0) return 0;

  // avrei voluto fare: struct swaptable *table = new struct swaptable[...]
  struct swaptable *table = (struct swaptable *)calloc(
      1, sizeof(struct swapent) * numberOfResources + sizeof(int));

  table->swt_n = numberOfResources;
  swapctl(SC_LIST, table); /* list all the swapping resources */

  TINT64 virtualFree  = 0;
  TINT64 physicalFree = 0;

  for (int i = 0; i < table->swt_n; i++) {
    virtualFree += table->swt_ent[i].ste_free;
  }

  free(table);
  totalFree = (virtualFree << 4) + physicalFree;

#elif defined(LINUX)

  struct sysinfo *sysInfo = (struct sysinfo *)calloc(1, sizeof(struct sysinfo));

  if (!sysinfo(sysInfo)) {
    if (onlyPhysicalMemory)
      totalFree = sysInfo->freeram;
    else
      totalFree = sysInfo->freeram + sysInfo->freeswap;
  } else {
    assert(!"sysinfo function failed");
  }
  free(sysInfo);

#elif defined(FREEBSD)

  TINT64 ret = 0;
  size_t size;
#ifdef __OpenBSD__
  int mib[] = {CTL_VM, VM_UVMEXP};
  struct uvmexp  uvmexp;
#else
  int mib[] = {CTL_VM, VM_TOTAL};
  struct vmtotal vmtotal;
#endif

#ifdef __OpenBSD__
  size = sizeof(uvmexp);
  if (sysctl(mib, 2, &uvmexp, &size, NULL, 0) < 0)
    return (ret);
  ret = pagetok((guint64)uvmexp.free);
#else
  size = sizeof(vmtotal);
  if (sysctl(mib, 2, &vmtotal, &size, NULL, 0) < 0)
    return (ret);
  ret = pagetok(vmtotal.t_free);
#endif
  return ret;

#elif defined(MACOSX)

  // to be done...
  totalFree = 512 * 1024;

#elif defined(HAIKU)

  // to be done...
  totalFree = 512 * 1024;

#else
  @ @ @ERROR : PLATFORM NOT SUPPORTED
#endif

#ifndef _WIN32
#else
#endif

  return totalFree;
}

//------------------------------------------------------------
/*
ostream& operator<<(ostream&out, const TTime  &t)
{
  return out<<t.getDate()<<" "<<t.getTime();
}
*/

//------------------------------------------------------------

TINT64 TSystem::getDiskSize(const TFilePath &diskName) {
  TINT64 size = 0;
  if (!diskName.isAbsolute()) {
    assert(0);
    return 0;
  }
#ifndef _WIN32
#ifdef HAIKU
	size = 0;
#else
  struct statfs buf;
#ifdef __sgi
  statfs(::to_string(diskName).c_str(), &buf, sizeof(struct statfs), 0);
#else
  statfs(::to_string(diskName).c_str(), &buf);
#endif
  size = (TINT64)((buf.f_blocks * buf.f_bsize) >> 10);
#endif
#else
  DWORD sectorsPerCluster;     // sectors per cluster
  DWORD bytesPerSector;        // bytes per sector
  DWORD numberOfFreeClusters;  // free clusters
  DWORD totalNumberOfClusters;

  BOOL rc = GetDiskFreeSpaceW(diskName.getWideString().c_str(),  // root path
                              &sectorsPerCluster,     // sectors per cluster
                              &bytesPerSector,        // bytes per sector
                              &numberOfFreeClusters,  // free clusters
                              &totalNumberOfClusters  // total clusters
                              );

  if (!rc)
    throw TSystemException(diskName, getFormattedMessage(GetLastError()));
  else
    size = (totalNumberOfClusters * sectorsPerCluster * bytesPerSector) >> 10;
#endif
  return size;
}

//------------------------------------------------------------

TINT64 TSystem::getFreeDiskSize(const TFilePath &diskName) {
  TINT64 size = 0;
  if (!diskName.isAbsolute()) {
    assert(0);
    return 0;
  }
#ifndef _WIN32
#ifdef HAIKU
	size = 0;
#else
  struct statfs buf;
#ifdef __sgi
  statfs(diskName.getWideString().c_str(), &buf, sizeof(struct statfs), 0);
#else
  statfs(::to_string(diskName).c_str(), &buf);
#endif
  size = (TINT64)(buf.f_bfree * buf.f_bsize) >> 10;
#endif
#else
  DWORD sectorsPerCluster;     // sectors per cluster
  DWORD bytesPerSector;        // bytes per sector
  DWORD numberOfFreeClusters;  // free clusters
  DWORD totalNumberOfClusters;

  BOOL rc = GetDiskFreeSpaceW(diskName.getWideString().c_str(),  // root path
                              &sectorsPerCluster,     // sectors per cluster
                              &bytesPerSector,        // bytes per sector
                              &numberOfFreeClusters,  // free clusters
                              &totalNumberOfClusters  // total clusters
                              );

  if (!rc)  // eccezione... getLastError etc...
    throw TSystemException(diskName, "cannot get disk info!");
  else
    size = (numberOfFreeClusters * sectorsPerCluster * bytesPerSector) >> 10;
#endif
  return size;
}

//------------------------------------------------------------

TINT64 TSystem::getMemorySize(bool onlyPhysicalMemory) {
#ifdef _WIN32

  MEMORYSTATUS buff;
  GlobalMemoryStatus(&buff);
  if (onlyPhysicalMemory)
    return buff.dwTotalPhys >> 10;
  else
    return buff.dwTotalPageFile >> 10;

#elif defined(__sgi)

  int physicalMemory;

  if (swapctl(SC_GETSWAPMAX, &physicalMemory))
    return ((size_t)0);
  else
    return logSwapLibero >> 1;
#elif defined(LINUX)

  struct sysinfo *sysInfo = (struct sysinfo *)calloc(1, sizeof(struct sysinfo));
  TINT64 ret              = 0;

  if (!sysinfo(sysInfo))
    ret = sysInfo->totalram;
  else
    assert(!"sysinfo function failed");

  free(sysInfo);
  return ret;

#elif defined(FREEBSD)

  TINT64 ret = 0;
  size_t size;
#ifdef __OpenBSD__
  int mib[] = {CTL_VM, VM_UVMEXP};
  struct uvmexp  uvmexp;
#else
  int mib[] = {CTL_VM, VM_TOTAL};
  struct vmtotal vmtotal;
#endif

#ifdef __OpenBSD__
  size = sizeof(uvmexp);
  if (sysctl(mib, 2, &uvmexp, &size, NULL, 0) < 0)
    return (ret);
  ret = pagetok((guint64)uvmexp.npages);
#else
  size = sizeof(vmtotal);
  if (sysctl(mib, 2, &vmtotal, &size, NULL, 0) < 0)
    return (ret);
  /* cheat : rm = tot used, add free to get total */
  ret = pagetok(vmtotal.t_rm + vmtotal.t_free);
#endif
  return ret;

#elif defined(MACOSX)

  // to be done...
  return 512 * 1024;

#elif defined(HAIKU)

  // to be done...
  return 512 * 1024;

#else
  @ @ @ERROR : PLATFORM NOT SUPPORTED
#endif

#ifndef _WIN32
#else
#endif
}

//------------------------------------------------------------

// From tsystem.cpp (doc/nexttoonz_core_noqt.md, step 6a): they need
// moveFileToRecycleBin, which is still on Qt.
void TSystem::moveFileOrLevelToRecycleBin_throw(const TFilePath &fp) {
  if (fp.isLevelName()) {
    TFilePathSet files;
    files = TSystem::readDirectory(fp.getParentDir(), false, true, true);

    for (const auto &file : files) {
      if (file.getLevelNameW() == fp.getLevelNameW())
        TSystem::moveFileToRecycleBin(file);
    }
  } else {
    TSystem::moveFileToRecycleBin(fp);
  }
}

//--------------------------------------------------------------

bool TSystem::moveFileOrLevelToRecycleBin(const TFilePath &fp) {
  try {
    moveFileOrLevelToRecycleBin_throw(fp);
  } catch (...) {
    return false;
  }
  return true;
}

//--------------------------------------------------------------

#if defined(LINUX) && defined(QT_CORE_LIB)
void moveFileToRecycleBinWithQt(const TFilePath &fp);  // tsystem_qt.cpp
#endif

void TSystem::moveFileToRecycleBin(const TFilePath &fp) {
#if defined(_WIN32)
  //
  // from http://msdn.microsoft.com/msdnmag/issues/01/04/c/default.aspx
  //
  // Copy pathname to double-NULL-terminated string.
  //
  wchar_t buf[_MAX_PATH + 1];               // allow one more character
  wcscpy(buf, fp.getWideString().c_str());  // copy caller's path name
  buf[wcslen(buf) + 1] = 0;                 // need two NULLs at end

  SHFILEOPSTRUCTW data;
  memset(&data, 0, sizeof(SHFILEOPSTRUCTW));
  data.fFlags |= FOF_SILENT;          // don't report progress
  data.fFlags |= FOF_NOERRORUI;       // don't report errors
  data.fFlags |= FOF_NOCONFIRMATION;  // don't confirm delete

  data.wFunc = FO_DELETE;             // REQUIRED: delete operation
  data.pFrom = buf;                   // REQUIRED: which file(s)
  data.pTo   = NULL;                  // MUST be NULL
  data.fFlags |= FOF_ALLOWUNDO;       // ..send to Recycle Bin
  int ret = SHFileOperationW(&data);  // do it!

#elif defined(MACOSX) && TARGET_OS_IPHONE
  outputDebug("There is no trash to move files to on iOS.");
#elif defined(MACOSX)
  FSRef foundRef;
  OSErr err = FSFindFolder(kOnSystemDisk, kTrashFolderType, kDontCreateFolder,
                           &foundRef);

  if (err) {
    assert(false);
    deleteFile(fp);
    return;
  }
  UInt8 path[255];
  err = FSRefMakePath(&foundRef, path, 254);
  if (err) {
    assert(false);
    deleteFile(fp);
    return;
  }
  // TFilePath dest = TFilePath(path)+(fp.getName()+fp.getDottedType());
  string fullNameWithExt = ::to_string(fp);
  int i                  = fullNameWithExt.rfind("/");
  string nameWithExt     = fullNameWithExt.substr(i + 1);
  TFilePath dest         = TFilePath((char *)path) + nameWithExt;

  try {
    renameFile(dest, fp);
  } catch (...) {
    try {
      copyFile(dest, fp);
      deleteFile(fp);
    } catch (...) {
    }
  }
#elif defined(LINUX)
  // The freedesktop.org trash, written with Qt (tsystem_qt.cpp); the build
  // without Qt has no recycle bin on Linux (doc/nexttoonz_core_noqt.md, 8d-0).
#ifdef QT_CORE_LIB
  moveFileToRecycleBinWithQt(fp);
#else
  outputDebug("Moving files to the trash needs Qt on Linux.");
#endif
#else
  assert(!"Not implemented yet");
#endif
}

//------------------------------------------------------------

// TSystemException::getMessage is in tsystem.cpp (doc/nexttoonz_core_noqt.md,
// step 6a).

//------------------------------------------------------------

void TSystem::touchFile(const TFilePath &path) {
#ifndef TNZCORE_LIGHT

  // string filename = path.getFullPath();
  if (TFileStatus(path).doesExist()) {
    int ret;
#ifdef _WIN32
    ret = _wutime(path.getWideString().c_str(), 0);
#else
    ret = utimes(::to_string(path).c_str(), 0);
#endif
    if (0 != ret) throw TSystemException(path, errno);
  } else {
    Tofstream file(path);
    if (!file) {
      throw TSystemException(path, errno);
    }
    file.close();  // altrimenti il compilatore da' un warning:
                   // variabile non utilizzata
  }

#endif
}

//------------------------------------------------------------
