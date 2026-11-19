#pragma once

#ifndef TCONTENTHISTORYDATE_H
#define TCONTENTHISTORYDATE_H

// Inside tnzcore only, and for its tests: how TContentHistory writes a date
// (doc/nexttoonz_core_noqt.md, 8d-0). Not a public interface.

#include "tcommon.h"

#include <string>

#undef DVAPI
#ifdef TNZCORE_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

namespace TContentHistoryDate {

//! The instant TContentHistory records for "now": the current time with the
//! seconds of the local time taken off (the milliseconds stay), as the
//! QDateTime it used to be.
DVAPI long long now();

//! The instant as QDateTime::toString("dd MMM yy   hh:mm") wrote it in local
//! time: with Qt's month names in the build with Qt, the English ones (what
//! Qt writes in an English system locale) without.
DVAPI std::wstring format(long long msecsSinceEpoch);

//! format() without Qt, whichever build this is.
DVAPI std::wstring formatWithoutQt(long long msecsSinceEpoch);

}  // namespace TContentHistoryDate

#endif
