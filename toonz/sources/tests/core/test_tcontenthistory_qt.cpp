// TContentHistory without QString and QDateTime (step 8d-0 of
// doc/nexttoonz_core_noqt.md) against the code it replaced, kept in
// legacy/legacy_tcontenthistory.cpp: the history text it writes into levels
// and scenes, and the dates in it, in several time zones. Qt build only.

#include <gtest/gtest.h>

#include "tcontenthistory.h"
#include "../../common/tcontenthistorydate.h"
#include "legacy/legacy_tcontenthistory.h"

#include <QDateTime>
#include <QLocale>
#include <QString>

#include <cstdlib>
#include <ctime>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace {

const QString kFmt = "dd MMM yy   hh:mm";

void setTimeZone(const char *tz) {
  if (tz)
    setenv("TZ", tz, 1);
  else
    unsetenv("TZ");
  tzset();
}

// The zones the dates are compared in: UTC, UTC+8 without daylight saving,
// and two with it (northern and southern hemisphere), and the machine's own.
const char *const kZones[] = {"UTC", "Asia/Shanghai", "America/New_York",
                              "Australia/Sydney", nullptr};

// Instants around the daylight saving changes of those zones, and others.
std::vector<long long> instants() {
  std::vector<long long> out;
  const long long hour = 3600000LL;
  // 2024-03-10 07:00 UTC (New York springs forward), 2024-11-03 06:00 UTC
  // (falls back), 2024-04-06 16:00 UTC and 2024-10-05 16:00 UTC (Sydney)
  for (long long t : {1710054000000LL, 1730613600000LL, 1712419200000LL,
                      1728144000000LL})
    for (long long d = -3 * hour; d <= 3 * hour; d += 7 * 60000 + 13)
      out.push_back(t + d);
  std::mt19937_64 rnd(8);
  // 1971 to 2037: after 2037 Qt 5 takes the daylight saving of an earlier
  // year with the same calendar, the C library the zone's own rules (the
  // history records "now", so it never meets them)
  std::uniform_int_distribution<long long> any(31536000000LL, 2145916800000LL);
  for (int i = 0; i < 20000; ++i) out.push_back(any(rnd));
  // the end of the year and the turn of the century
  out.push_back(946684799999LL);
  out.push_back(946684800000LL);
  return out;
}

class ContentHistoryAgainstQt : public ::testing::Test {
protected:
  void TearDown() override { setTimeZone(m_tz.empty() ? nullptr : m_tz.c_str()); }
  void SetUp() override {
    if (const char *tz = std::getenv("TZ")) m_tz = tz;
  }
  std::string m_tz;
};

}  // namespace

// The date as QDateTime wrote it: in the build with Qt it is still written
// by Qt (with the system locale's month names); the version without Qt
// matches it wherever the month names are English.
TEST_F(ContentHistoryAgainstQt, DatesAreWrittenAsQDateTimeWroteThem) {
  const bool english =
      QLocale::system().language() == QLocale::English ||
      QLocale::system().language() == QLocale::C;
  for (const char *zone : kZones) {
    setTimeZone(zone);
    SCOPED_TRACE(zone ? zone : "(the machine's)");
    int checked = 0;
    for (long long t : instants()) {
      const std::wstring qt =
          QDateTime::fromMSecsSinceEpoch(t).toString(kFmt).toStdWString();
      ASSERT_EQ(TContentHistoryDate::format(t), qt) << t;
      if (english) {
        ASSERT_EQ(TContentHistoryDate::formatWithoutQt(t), qt) << t;
        ++checked;
      }
    }
    if (!english) GTEST_SKIP() << "the system locale's month names are not English";
    EXPECT_GT(checked, 20000);
  }
}

// "Now" is the current time with the local seconds taken off, the
// milliseconds kept, as the QDateTime was.
TEST_F(ContentHistoryAgainstQt, NowIsTheStartOfTheMinuteInLocalTime) {
  for (const char *zone : kZones) {
    setTimeZone(zone);
    for (int attempt = 0; attempt < 5; ++attempt) {
      QDateTime date = QDateTime::currentDateTime();
      QDateTime dateNoSecs(date.date(),
                           date.time().addSecs(-date.time().second()));
      const long long ours = TContentHistoryDate::now();
      const long long qt   = dateNoSecs.toMSecsSinceEpoch();
      if (ours / 60000 != qt / 60000) continue;  // a minute went by
      EXPECT_LT(ours - qt, 1000);
      EXPECT_GE(ours - qt, 0);
      EXPECT_LT(ours % 60000, 1000);
      break;
    }
  }
}

// The same edits, made on the old class and on the new one in the same
// minute, give the same history, and so does the history read back.
TEST_F(ContentHistoryAgainstQt, TheHistoryTextIsTheSame) {
  // A user and a machine whose names are cut in the middle of a pair of
  // surrogates (15 and 12 UTF-16 units). Both classes read them once.
  setenv("USERNAME", "abcdefghijklmn\xf0\x9d\x84\x9e" "xyz", 1);
  setenv("COMPUTERNAME", "Atelier-\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e-\xf0\x9d\x84\x9e" "01", 1);

  std::mt19937 rnd(84);
  for (const char *zone : kZones) {
    setTimeZone(zone);
    SCOPED_TRACE(zone ? zone : "(the machine's)");
    for (int round = 0; round < 200; ++round) {
      // Both classes group the frames modified in the same millisecond (as
      // QDateTime compared them); the old one reads the clock itself, so a
      // millisecond can end between its edit and the new one's. A round is
      // made again then, with the same edits; a real difference fails every
      // time.
      const unsigned seed = rnd();
      bool same           = false;
      std::string lastBefore, lastAfter;
      for (int attempt = 0; attempt < 20 && !same; ++attempt) {
        std::mt19937 rnd(seed);
        const bool isLevel = round % 4 != 0;
        LegacyContentHistory before(isLevel);
        TContentHistory after(isLevel);
        if (round % 3 == 0) {
          const QString frozen =
              round % 2 ? "| #    DATE:       Time:   MACHINE:    USER:          FRAMES MODIFIED:     || #7   01 Jan 20   10:00   m           u              1                    |"
                        : "| #12 x";
          before.deserialize(frozen);
          after.deserialize(frozen);
        }
        const long long start = TContentHistoryDate::now();
        int edits = 1 + int(rnd() % 6);
        for (int e = 0; e < edits; ++e) {
          if (!isLevel) {
            before.modifiedNow();
            after.modifiedNow();
            continue;
          }
          int a = 1 + int(rnd() % 30), b = a + int(rnd() % 5);
          std::wstring letter = rnd() % 3 ? L"" : std::wstring(1, L'a' + rnd() % 3);
          TFrameId from(a, letter), to(b);
          if (rnd() % 2) {
            before.frameModifiedNow(from);
            after.frameModifiedNow(from);
          } else {
            before.frameRangeModifiedNow(from, to);
            after.frameRangeModifiedNow(from, to);
          }
          if (rnd() % 5 == 0) {
            before.fixCurrentHistory();
            after.fixCurrentHistory();
          }
        }
        if (TContentHistoryDate::now() / 60000 != start / 60000) continue;
        lastBefore = before.serialize().toStdString();
        lastAfter  = after.serialize().toStdString();
        if (lastBefore != lastAfter) continue;
        std::unique_ptr<TContentHistory> copy(after.clone());
        std::unique_ptr<LegacyContentHistory> legacyCopy(before.clone());
        ASSERT_EQ(copy->serialize().toStdString(),
                  legacyCopy->serialize().toStdString());
        same = true;
      }
      ASSERT_TRUE(same) << round << "\n" << lastAfter << "\n" << lastBefore;
    }
  }
}
