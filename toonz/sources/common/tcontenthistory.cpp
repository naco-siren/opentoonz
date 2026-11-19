#include "tcontenthistory.h"
#include "tcontenthistorydate.h"
#include "tconvert.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>

#ifdef _WIN32
#include <windows.h>
#else
extern char **environ;
#endif

// What this file did with QString, QDateTime and QProcess, without Qt
// (doc/nexttoonz_core_noqt.md, 8d-0): lengths are counted in UTF-16 units,
// as QString counted them; dates are instants compared as QDateTime compared
// them and written by TContentHistoryDate::format(). The QString members
// are in tcontenthistory_qt.cpp.

using namespace std;

//-------------------------------------------------------

TContentHistory::TContentHistory(bool isLevel)
    : m_isLevel(isLevel), m_frozenHistory() {}

//-------------------------------------------------------

TContentHistory::~TContentHistory() {}

//-------------------------------------------------------

TContentHistory *TContentHistory::clone() const {
  TContentHistory *history = new TContentHistory(m_isLevel);
  history->deserializeW(serializeW());
  return history;
}

//-------------------------------------------------------

void TContentHistory::deserializeW(const std::wstring &currentHistory) {
  m_frozenHistory = currentHistory;
}

//--------------------------------------------------------------------

namespace {

// The number of UTF-16 units of a code point.
inline int utf16Units(wchar_t c) {
  return (sizeof(wchar_t) == 4 && (unsigned)c >= 0x10000) ? 2 : 1;
}

// QString::size() of the string.
int utf16Size(const std::wstring &s) {
  int n = 0;
  for (wchar_t c : s) n += utf16Units(c);
  return n;
}

// The string cut to count UTF-16 units, or padded with blanks to them.
// QString::truncate could leave the first half of a surrogate pair; so does
// this (a lone surrogate, which QString::fromStdWString gives back as it
// was, and which Qt writes as '?').
std::wstring blanks(const std::wstring &str, int count = 15) {
  std::wstring res;
  int n = 0;
  for (wchar_t c : str) {
    int u = utf16Units(c);
    if (n + u > count) {
      if (n < count)
        res.push_back(wchar_t(0xd800 + (((unsigned)c - 0x10000) >> 10))), ++n;
      break;
    }
    res.push_back(c);
    n += u;
  }
  if (n < count) res.append(count - n, L' ');
  return res;
}

inline std::wstring number(int n) { return std::to_wstring(n); }

std::wstring getStr(const TFrameId &id) {
  if (!id.getLetterW().empty())
    return number(id.getNumber()) + id.getLetterW();
  else
    return number(id.getNumber());
}

// QProcess::systemEnvironment(): every entry, in order.
std::vector<std::wstring> systemEnvironment() {
  std::vector<std::wstring> list;
#ifdef _WIN32
  wchar_t *env = GetEnvironmentStringsW();
  if (!env) return list;
  for (const wchar_t *e = env; *e; e += wcslen(e) + 1) list.push_back(e);
  FreeEnvironmentStringsW(env);
#else
  for (char **e = environ; e && *e; ++e) list.push_back(::to_wstring(*e));
#endif
  return list;
}

bool startsWith(const std::wstring &s, const std::wstring &prefix) {
  return s.compare(0, prefix.size(), prefix) == 0;
}

std::wstring getLine(int counter, long long date, const set<TFrameId> &frames) {
  static std::wstring user;
  static std::wstring machine;
  if (user.empty()) {
    for (const std::wstring &value : systemEnvironment()) {
      if (startsWith(value, L"USERNAME="))
        user = blanks(value.substr(9));
      else if (startsWith(value, L"COMPUTERNAME="))
        machine = blanks(value.substr(13));
    }
  }

  if (frames.empty())
    return L"| #" + blanks(number(counter), 4) +
           blanks(TContentHistoryDate::format(date), 20) +
           blanks(machine, 12) + user + L" |";

  std::wstring framesStr;

  set<TFrameId>::const_iterator it = frames.begin();
  TFrameId first, last;
  while (it != frames.end()) {
    first = last = *it;
    ++it;
    while (it != frames.end() &&
           ((*it).getNumber() == last.getNumber() ||  // 1a, 1b....
            (*it).getNumber() == last.getNumber() + 1)) {
      assert(*it > last);
      last = *it, ++it;
    }
    framesStr += getStr(first) + ((first != last) ? L"-" + getStr(last) : L"");
    if (it != frames.end()) framesStr += L", ";
  }

  return L"| #" + blanks(number(counter), 4) +
         blanks(TContentHistoryDate::format(date), 20) + blanks(machine, 12) +
         user + blanks(framesStr, 20) + L" |";
}

// QString::mid(pos, n) for the positions found below: n < 0 means "to the
// end".
std::wstring mid(const std::wstring &s, int pos, int n) {
  if (pos < 0) pos = 0;
  if (pos > (int)s.size()) return std::wstring();
  return n < 0 ? s.substr(pos) : s.substr(pos, n);
}

int getCurrentCount(const std::wstring &str) {
  if (str.empty()) return 0;

  // lastIndexOf('#') gives -1 when there is none, as rfind's npos does here
  int from = (int)(str.rfind(L'#') == std::wstring::npos ? -1 : str.rfind(L'#')) + 1;
  assert(from != -1);

  size_t space = str.find(L' ', from);
  int to       = (space == std::wstring::npos ? -1 : (int)space) - 1;
  assert(to != -1);
  assert(from <= to);

  return toIntAsQString(::to_string(mid(str, from, to - from + 1)));
}

}  // namespace

//--------------------------------------------------------------------

std::wstring TContentHistory::currentToString() const {
  if (m_records.empty()) return L"";

  int counter = getCurrentCount(m_frozenHistory);

  if (!m_isLevel) {
    assert(m_records.size() == 1);
    return getLine(++counter, m_records.begin()->second, set<TFrameId>());
  }

  std::wstring out;
  std::multimap<long long, TFrameId> dateSorted;
  std::map<TFrameId, long long>::const_iterator it;
  for (it = m_records.begin(); it != m_records.end(); ++it)
    dateSorted.insert(pair<long long, TFrameId>(it->second, it->first));

  std::multimap<long long, TFrameId>::const_iterator it1 = dateSorted.begin();
  long long currDate = it1->first;

  while (it1 != dateSorted.end()) {
    set<TFrameId> frames;
    while (it1 != dateSorted.end() && currDate == it1->first) {
      frames.insert(it1->second);
      ++it1;
    }
    assert(!frames.empty());
    out += getLine(++counter, currDate, frames);
    if (it1 != dateSorted.end()) currDate = it1->first;
  }

  return out;
}

//--------------------------------------------------------------------

std::wstring TContentHistory::serializeW() const {
  const std::wstring currentHistory = currentToString();

  if (!m_frozenHistory.empty())
    return m_frozenHistory + currentHistory;
  else if (!currentHistory.empty()) {
    if (m_isLevel)
      return L"| #    DATE:       Time:   MACHINE:    USER:          FRAMES "
             L"MODIFIED:     |" +
             currentHistory;
    else
      return L"| #    DATE:       Time:   MACHINE:    USER:           |" +
             currentHistory;
  } else
    return L"";
}

//--------------------------------------------------------------------

void TContentHistory::fixCurrentHistory() {
  m_frozenHistory = serializeW();
  m_records.clear();
}

//--------------------------------------------------------------------

void TContentHistory::frameRangeModifiedNow(const TFrameId &fromId,
                                            const TFrameId &toId) {
  assert(m_isLevel);
  const long long dateNoSecs = TContentHistoryDate::now();

  int i;
  m_records[fromId] = dateNoSecs;

  if (fromId == toId) return;

  for (i = fromId.getNumber() + 1; i <= toId.getNumber() - 1; i++)
    m_records[TFrameId(i)] = dateNoSecs;

  m_records[toId] = dateNoSecs;
}

//--------------------------------------------------------------------

void TContentHistory::modifiedNow() {
  assert(!m_isLevel);
  m_records[0] = TContentHistoryDate::now();
}

//====================================================================

namespace {

bool localTime(long long msecs, std::tm &out) {
  // floor, for the instants before the epoch
  long long secs = msecs >= 0 ? msecs / 1000 : -((-msecs + 999) / 1000);
  std::time_t t  = (std::time_t)secs;
#ifdef _WIN32
  return localtime_s(&out, &t) == 0;
#else
  return localtime_r(&t, &out) != nullptr;
#endif
}

}  // namespace

long long TContentHistoryDate::now() {
  using namespace std::chrono;
  long long ms =
      duration_cast<milliseconds>(system_clock::now().time_since_epoch())
          .count();
  std::tm tm;
  if (!localTime(ms, tm)) return ms;
  return ms - (long long)tm.tm_sec * 1000;
}

std::wstring TContentHistoryDate::formatWithoutQt(long long msecs) {
  static const wchar_t *const months[] = {L"Jan", L"Feb", L"Mar", L"Apr",
                                          L"May", L"Jun", L"Jul", L"Aug",
                                          L"Sep", L"Oct", L"Nov", L"Dec"};
  std::tm tm;
  if (!localTime(msecs, tm)) return L"";
  int yy = (tm.tm_year + 1900) % 100;
  if (yy < 0) yy = -yy;
  wchar_t buf[64];
  std::swprintf(buf, 64, L"%02d %ls %02d   %02d:%02d", tm.tm_mday,
                months[tm.tm_mon], yy, tm.tm_hour, tm.tm_min);
  return buf;
}

#ifdef QT_CORE_LIB
// tcontenthistory_qt.cpp: QDateTime::toString, with the system locale's
// month names
std::wstring formatHistoryDateWithQt(long long msecs);
#endif

std::wstring TContentHistoryDate::format(long long msecs) {
#ifdef QT_CORE_LIB
  return formatHistoryDateWithQt(msecs);
#else
  return formatWithoutQt(msecs);
#endif
}
