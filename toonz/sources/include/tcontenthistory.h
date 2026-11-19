#pragma once

#ifndef _CONTENT_HISTORY_H
#define _CONTENT_HISTORY_H

#include "tcommon.h"
#include "tfilepath.h"
#include <string>

// The QString members are in tcontenthistory_qt.cpp
// (doc/nexttoonz_core_noqt.md, Q2 and 8d-0).
class QString;
#include <map>
#include <set>

#undef DVAPI
#undef DVVAR
#ifdef TNZCORE_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

//! this class keeps tracks of modification times on contents, as well as user
//! and machine info;
//! typically used for levels and scene files (or whatever you like...)
// it is used for visualitation purposed only in the infoview, in example

class DVAPI TContentHistory {
  bool m_isLevel;
  // When each frame (or the content) was last modified: the start of that
  // minute in local time, as milliseconds since the epoch, as the QDateTime
  // there was compared (doc/nexttoonz_core_noqt.md, 8d-0).
  std::map<TFrameId, long long> m_records;
  std::wstring m_frozenHistory;

  std::wstring currentToString() const;

public:
  //! set isLevel=true if you want to keep track of single-frames modifications
  TContentHistory(bool isLevel);

  ~TContentHistory();

  TContentHistory *clone() const;

  //! history can come only from a deserialization
  //! WARNING! any preexistent history will be erased by calling this method
  void deserialize(const QString &history);  // tcontenthistory_qt.cpp
  void deserializeW(const std::wstring &history);

  //! the returned string is only for visualitation and serialization
  const QString serialize() const;  // tcontenthistory_qt.cpp
  std::wstring serializeW() const;

  //! after calling this method, following history info added will not "merge"
  //! with the history until now
  void fixCurrentHistory();

  //! do not call this 2 methods if isLevel==false! (assertion fails)
  void frameModifiedNow(const TFrameId &id) { frameRangeModifiedNow(id, id); }
  void frameRangeModifiedNow(const TFrameId &fromId, const TFrameId &toId);

  //! do not call this method if isLevel==true! (assertion fails)
  void modifiedNow();
};

#endif
