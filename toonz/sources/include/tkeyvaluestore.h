#pragma once

#ifndef TKEYVALUESTORE_INCLUDED
#define TKEYVALUESTORE_INCLUDED

#include "tfilepath.h"

#include <memory>
#include <string>

#undef DVAPI
#undef DVVAR
#ifdef TNZCORE_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

//! A settings file the core reads and writes without knowing its format
//! (doc/nexttoonz_core_noqt.md, step 6b and Q4). The desktop opens it as the
//! QSettings INI file it always was (tkeyvaluestore_qt.cpp), so that the
//! file stays the same byte for byte; a program can inject another store.
//!
//! Keys are UTF-8 and follow QSettings' conventions one to one, so that a
//! key names the same entry of the file whichever store reads it:
//!   - "key" is a key of the file's [General] section;
//!   - "group/key" is key "key" of group "group" (the INI section
//!     [group]); groups nest as "a/b/key";
//!   - an array "name" written by QSettings::beginWriteArray is
//!     "name/size" (its length) and "name/1/key", "name/2/key"... (one
//!     entry per element, counted from 1).
//!
//! Only strings are read and written for now (all SystemVar.ini holds).
//! Typed values (bool, int, double, colours, sizes, maps), which
//! Preferences needs and which QSettings writes each in its own way, are to
//! be added next to these in step 15.
class DVAPI TKeyValueStore {
public:
  virtual ~TKeyValueStore();

  //! Whether the key has a value.
  virtual bool contains(const std::string &key) const = 0;

  //! The key's value as a string, or defaultValue when it has none (as
  //! QSettings::value(key, defaultValue).toString()).
  virtual std::wstring stringValue(
      const std::string &key, const std::wstring &defaultValue = L"") const = 0;

  //! Sets the key's value to a string.
  virtual void setStringValue(const std::string &key,
                              const std::wstring &value) = 0;

  //! Writes what was set to the file (QSettings::sync). The store also
  //! writes it when it is destroyed.
  virtual void sync() = 0;

  //------------------------------------------------------------------

  typedef std::unique_ptr<TKeyValueStore> (*Factory)(const TFilePath &path);

  //! Opens the settings file at the path through the current factory.
  static std::unique_ptr<TKeyValueStore> open(const TFilePath &path);

  //! The one place where the stores come from: sets the factory open()
  //! uses and returns the one it replaces; nullptr restores the built-in
  //! one, which is the QSettings store in the build with Qt and, without
  //! Qt, a store that holds nothing and writes nothing.
  static Factory setFactory(Factory factory);
};

#endif
