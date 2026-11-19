// The members of TFilePath, TFrameId and TFileType that take or give Qt
// types (tfilepath.h, tfiletype.h). They are for the code on the Qt side and
// are built only with Qt; the rest of these classes no longer uses Qt
// (doc/nexttoonz_core_noqt.md, migration step 4).

#include "tfilepath.h"
#include "tfiletype.h"

#include <QString>

TFrameId::TFrameId(int f, QString str, int p, char s)
    : m_frame(f)
    , m_letter(str.toStdWString())
    , m_zeroPadding(p)
    , m_startSeqInd(s) {}

QString TFrameId::getLetter() const { return QString::fromStdWString(m_letter); }

#ifndef __APPLE__
// On Apple, tfilepath.cpp asks CoreFoundation, which is what Qt does there.
// Elsewhere Qt uses ICU or the C library; this keeps it until the core no
// longer links Qt.
int TFrameId::compareLetters(const std::wstring &a, const std::wstring &b) {
  return QString::localeAwareCompare(QString::fromStdWString(a),
                                     QString::fromStdWString(b));
}
#endif

//-----------------------------------------------------------------------------

TFilePath::TFilePath(const QString &path) { setPath(path.toStdWString()); }

QString TFilePath::getQString() const {
  return QString::fromStdWString(getWideString());
}

QChar TFilePath::getSepChar() const {
  wchar_t c = getSepCharW();
  return c ? QChar(c) : QChar();
}

QString TFilePath::fidRegExpStr() {
  if (m_useStandard) return QString("(\\d+)([a-zA-Z]?)");
  QString suffixLetter = (m_acceptNonAlphabetSuffix)
                             ? "[^\\._ \\\\/:,;*?\"<>|0123456789]"
                             : "[a-zA-Z]";
  QString countLetter  = (m_letterCountForSuffix == 0)
                             ? "{0,}"
                             : (QString("{0,%1}").arg(m_letterCountForSuffix));
  return QString("(?<framenum>\\d+)(?<suffix>%1%2)")
      .arg(suffixLetter)
      .arg(countLetter);
  // const QString fIdRegExp("(\\d+)([a-zA-Z]?)");
}

//-----------------------------------------------------------------------------

TFileType::Type TFileType::getInfoFromExtension(const QString &type) {
  return getInfoFromExtension(type.toStdString());
}
