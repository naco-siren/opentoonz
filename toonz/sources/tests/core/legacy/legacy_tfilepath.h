// The TFilePath and TFrameId of tnzcore before migration step 4 of
// doc/nexttoonz_core_noqt.md, kept verbatim but for the class names (Legacy
// prefix) and DVAPI, so that test_tfilepath_qt.cpp can compare the new code
// with them. Built in the Qt build only.

#pragma once

#ifndef LEGACY_T_FILEPATH_INCLUDED
#define LEGACY_T_FILEPATH_INCLUDED

#include "tcommon.h"
#include "texception.h"

// (the DVAPI export macros are gone: this code is compiled into the test)

#include <QString>

//-----------------------------------------------------------------------------
/*
  Example of using LegacyTFilePath and LegacyTFrameId classes.
*/

//!\include frameid_ex.cpp
//! The LegacyTFrameId class describes a frame identified by a four-digit integer and
//! optionally by a character (necessary for appended frames)
class LegacyTFrameId {
  int m_frame;
  QString
      m_letter;  // Used for "appended" frames like pippo.0001a.tzp => f=1 c='a'
  int m_zeroPadding;
  char m_startSeqInd;

public:
  enum {
    EMPTY_FRAME = -1,  // e.g., pippo..tif
    NO_FRAME    = -2   // e.g., pippo.tif
  };

  enum FrameFormat {
    FOUR_ZEROS,
    NO_PAD,
    UNDERSCORE_FOUR_ZEROS,  // pippo_0001.tif
    UNDERSCORE_NO_PAD,
    NO_SEP_FOUR_ZEROS,  // pippo0001.tif
    NO_SEP_NO_PAD,
    CUSTOM_PAD,
    UNDERSCORE_CUSTOM_PAD,
    NO_SEP_CUSTOM_PAD,
    USE_CURRENT_FORMAT
  };  // pippo_1.tif

  LegacyTFrameId(int f = EMPTY_FRAME)
      : m_frame(f), m_letter(""), m_zeroPadding(4), m_startSeqInd('.') {}
  LegacyTFrameId(int f, char c, int p = 4, char s = '.')
      : m_frame(f), m_zeroPadding(p), m_startSeqInd(s) {
    m_letter = (c == '\0') ? QString() : QString(1, QChar(c));
  }
  LegacyTFrameId(int f, QString str, int p = 4, char s = '.')
      : m_frame(f), m_letter(str), m_zeroPadding(p), m_startSeqInd(s) {}
  explicit LegacyTFrameId(const std::string &str, char s = '.');
  explicit LegacyTFrameId(const std::wstring &wstr, char s = '.');

  inline bool operator==(const LegacyTFrameId &f) const {
    return f.m_frame == m_frame && f.m_letter == m_letter;
  }
  inline bool operator!=(const LegacyTFrameId &f) const {
    return (m_frame != f.m_frame || m_letter != f.m_letter);
  }
  inline bool operator<(const LegacyTFrameId &f) const {
    return (m_frame < f.m_frame ||
            (m_frame == f.m_frame &&
             QString::localeAwareCompare(m_letter, f.m_letter) < 0));
  }
  inline bool operator>(const LegacyTFrameId &f) const { return f < *this; }
  inline bool operator>=(const LegacyTFrameId &f) const { return !operator<(f); }
  inline bool operator<=(const LegacyTFrameId &f) const { return !operator>(f); }

  const LegacyTFrameId &operator++();
  const LegacyTFrameId &operator--();

  LegacyTFrameId &operator=(const LegacyTFrameId &f) {
    m_frame       = f.m_frame;
    m_letter      = f.m_letter;
    m_zeroPadding = f.m_zeroPadding;
    m_startSeqInd = f.m_startSeqInd;
    return *this;
  }

  bool isEmptyFrame() const { return m_frame == EMPTY_FRAME; }
  bool isNoFrame() const { return m_frame == NO_FRAME; }

  // operator string() const;
  std::string expand(FrameFormat format = FOUR_ZEROS) const;
  int getNumber() const { return m_frame; }
  QString getLetter() const { return m_letter; }

  void setZeroPadding(int p) { m_zeroPadding = p; }
  int getZeroPadding() const { return m_zeroPadding; }

  void setStartSeqInd(char c) { m_startSeqInd = c; }
  char getStartSeqInd() const { return m_startSeqInd; }

  FrameFormat getCurrentFormat() const {
    switch (m_zeroPadding) {
    case 0:
      return (m_startSeqInd == '.'
                  ? NO_PAD
                  : (m_startSeqInd == '_' ? UNDERSCORE_NO_PAD : NO_SEP_NO_PAD));
    case 4:
      return (m_startSeqInd == '.'
                  ? FOUR_ZEROS
                  : (m_startSeqInd == '_' ? UNDERSCORE_FOUR_ZEROS
                                          : NO_SEP_FOUR_ZEROS));
    default:
      break;
    }

    return (m_startSeqInd == '.' ? CUSTOM_PAD
                                 : (m_startSeqInd == '_' ? UNDERSCORE_CUSTOM_PAD
                                                         : NO_SEP_CUSTOM_PAD));
  }
};

//-----------------------------------------------------------------------------
/*! \relates LegacyTFrameId*/

inline std::ostream &operator<<(std::ostream &out, const LegacyTFrameId &f) {
  if (f.isNoFrame())
    out << "<noframe>";
  else if (f.isEmptyFrame())
    out << "''";
  else
    out << f.expand().c_str();
  return out;
}

//-----------------------------------------------------------------------------

/*!The LegacyTFilePath class defines a file path. Its constructor creates a string
   according to the platform and specific rules. For more information see the
   constructor.*/
class LegacyTFilePath {
  static bool m_underscoreFormatAllowed;
  static bool m_noSeparatorFormatAllowed;

  // Specifies file path condition for sequential images per project.
  // See filepathproperties.h
  static bool m_useStandard;
  static bool m_acceptNonAlphabetSuffix;
  static int m_letterCountForSuffix;

  std::wstring m_path;

  struct LegacyTFilePathInfo {
    QString parentDir;  // with slash
    QString levelName;
    QChar sepChar;  // either "." or "_", or "#" (indicates no separator)
    LegacyTFrameId fId;
    QString extension;
  };

  void setPath(std::wstring path);

public:
  /*! This static method allows correct reading of levels in the form
   * pippo_0001.tif (always represented as pippo..tif) */
  static void setUnderscoreFormatAllowed(bool state) {
    m_underscoreFormatAllowed = state;
  }
  static void setNoSeparatorFormatAllowed(bool state) {
    m_noSeparatorFormatAllowed = state;
  }
  // Called from TProjectManager::getCurrentProject() and
  // ProjectPopup::updateProjectFromFields Returns true if something changed
  static bool setFilePathProperties(bool useStandard, bool acceptNonAlphaSuffix,
                                    int letterCountForSuffix,
                                    bool allowNoSeparatorFormat) {
    if (m_useStandard == useStandard &&
        m_acceptNonAlphabetSuffix == acceptNonAlphaSuffix &&
        m_letterCountForSuffix == letterCountForSuffix &&
        m_noSeparatorFormatAllowed == allowNoSeparatorFormat)
      return false;
    m_useStandard              = useStandard;
    m_acceptNonAlphabetSuffix  = acceptNonAlphaSuffix;
    m_letterCountForSuffix     = letterCountForSuffix;
    m_noSeparatorFormatAllowed = allowNoSeparatorFormat;
    return true;
  }
  static bool useStandard() { return m_useStandard; }
  static QString fidRegExpStr();

  /*!This constructor creates a string by removing redundancies ('//', './',
     etc.) and trailing slashes, correcting (redressing) "twisted" slashes.
      Note: if the current directory is ".", it becomes "".
      If the path is "<alpha>:", a slash will be added*/

  explicit LegacyTFilePath(const char *path = "");
  explicit LegacyTFilePath(const std::string &path);
  explicit LegacyTFilePath(const std::wstring &path);
  explicit LegacyTFilePath(const QString &path);

  ~LegacyTFilePath() {}
  LegacyTFilePath(const LegacyTFilePath &fp) : m_path(fp.m_path) {}
  LegacyTFilePath &operator=(const LegacyTFilePath &fp) {
    m_path = fp.m_path;
    return *this;
  }

  bool operator==(const LegacyTFilePath &fp) const;
  inline bool operator!=(const LegacyTFilePath &fp) const {
    return !(m_path == fp.m_path);
  }
  bool operator<(const LegacyTFilePath &fp) const;

  const std::wstring getWideString() const;
  QString getQString() const;

  /*!Returns:
   a.  "" if there is no file extension
   b. "." if there is a file extension but no frame
   c.".." if there are both a file extension and a frame */
  std::string getDots() const;  // Returns "" (no extension), "." (extension, no
                                // frame) or ".." (extension and frame)

  /*!Returns the file extension, including the leading period (.).
      Returns "" if there is no file extension.*/
  std::string getDottedType()
      const;  // Returns the extension WITH the DOT (if no extension returns "")

  /*!Returns the file extension, excluding the leading period (.).
      Returns "" if there is no file extension.*/
  std::string getUndottedType() const;  // Returns the extension WITHOUT the DOT

  /*!Same as getUndottedType():
      Returns the file extension, excluding the leading period (.).
      Returns "" if there is no file extension.*/
  std::string getType() const {
    return getUndottedType();
  }  // Returns the extension WITHOUT the DOT

  /*!Returns the base filename (no extension, no dots, no slash)*/
  std::string getName() const;       // noDot! noSlash!
  std::wstring getWideName() const;  // noDot! noSlash!

  /*!Returns the filename (with extension, excluding the frame number if
     present). ex.: LegacyTFilePath("/pippo/pluto.0001.gif").getLevelName() ==
     "pluto..gif"
   */
  std::string getLevelName()
      const;  // e.g., LegacyTFilePath("/pippo/pluto.0001.gif").getLevelName() ==
              // "pluto..gif"
  std::wstring getLevelNameW()
      const;  // e.g., LegacyTFilePath("/pippo/pluto.0001.gif").getLevelName() ==
              // "pluto..gif"

  /*!Returns the parent directory excluding any trailing slash.*/
  LegacyTFilePath getParentDir() const;  // noSlash!;

  LegacyTFrameId getFrame() const;
  bool isFfmpegType() const;
  bool isUneditable() const;
  bool isLevelName()
      const;  //{return getFrame() == LegacyTFrameId(LegacyTFrameId::EMPTY_FRAME);};
  bool isAbsolute() const;
  bool isRoot() const;
  bool isEmpty() const { return m_path == L""; }

  /*!Returns a LegacyTFilePath with the specified extension type.
type is a string indicating the file extension (e.g., .bmp or bmp)*/
  LegacyTFilePath withType(const std::string &type) const;
  /*!Returns a LegacyTFilePath with filename "name".*/
  LegacyTFilePath withName(const std::string &name) const;
  /*!Returns a LegacyTFilePath with filename "name". Unicode*/
  LegacyTFilePath withName(const std::wstring &name) const;
  /*!Returns a LegacyTFilePath with parent directory "dir".*/
  LegacyTFilePath withParentDir(const LegacyTFilePath &dir) const;
  /*!Returns a LegacyTFilePath without a parent directory */
  LegacyTFilePath withoutParentDir() const { return withParentDir(LegacyTFilePath()); }
  /*!Returns a LegacyTFilePath with frame "frame".*/
  LegacyTFilePath withFrame(
      const LegacyTFrameId &frame,
      LegacyTFrameId::FrameFormat format = LegacyTFrameId::USE_CURRENT_FORMAT) const;
  /*!Returns a LegacyTFilePath with a frame identified by an integer "f".*/
  LegacyTFilePath withFrame(int f) const { return withFrame(LegacyTFrameId(f)); }
  /*!Returns a LegacyTFilePath with a frame identified by an integer and a character*/
  LegacyTFilePath withFrame(int f, char letter) const {
    return withFrame(LegacyTFrameId(f, letter));
  }

  LegacyTFilePath withFrame() const {
    return withFrame(LegacyTFrameId(LegacyTFrameId::EMPTY_FRAME));
  }
  LegacyTFilePath withNoFrame() const {
    return withFrame(LegacyTFrameId(LegacyTFrameId::NO_FRAME));
  }  // pippo.tif

  LegacyTFilePath operator+(const LegacyTFilePath &fp) const;
  LegacyTFilePath &operator+=(const LegacyTFilePath &fp) /*{*this=*this+fp;return *this;}*/;

  inline LegacyTFilePath operator+(const std::string &s) const {
    return operator+(LegacyTFilePath(s));
  }
  inline LegacyTFilePath &operator+=(const std::string &s) {
    return operator+=(LegacyTFilePath(s));
  }

  LegacyTFilePath &operator+=(const std::wstring &s);
  LegacyTFilePath operator+(const std::wstring &s) const {
    LegacyTFilePath res(*this);
    return res += s;
  }

  bool isAncestorOf(const LegacyTFilePath &) const;

  LegacyTFilePath operator-(
      const LegacyTFilePath &fp) const;  // LegacyTFilePath("/a/b/c.txt") - LegacyTFilePath("/a/")
                                   // == LegacyTFilePath("b/c.txt");
  // If fp.isAncestorOf(this) == false, returns this

  bool match(const LegacyTFilePath &fp)
      const;  // They are equal except for the number of frame digits

  // '/a/b/c.txt' => head='a' tail='b/c.txt'
  void split(std::wstring &head, LegacyTFilePath &tail) const;

  LegacyTFilePathInfo analyzePath() const;

  QChar getSepChar() const;
};

//-----------------------------------------------------------------------------

std::ostream &operator<<(std::ostream &out, const LegacyTFilePath &path);

typedef std::list<LegacyTFilePath> LegacyTFilePathSet;

#endif  // LEGACY_T_FILEPATH_INCLUDED
