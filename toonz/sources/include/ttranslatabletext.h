#pragma once

#ifndef TTRANSLATABLETEXT_INCLUDED
#define TTRANSLATABLETEXT_INCLUDED

#include "tcommon.h"

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

// Qt's marker for text to translate, which lupdate finds in the source; the
// same definition as Qt's, for the code that does not include Qt.
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif

//! Text the core shows to the user, before translation
//! (doc/nexttoonz_core_noqt.md, Q2 and step 7): the core says what to show,
//! the interface translates it (QCoreApplication::translate(context, source)
//! on the desktop). Text to translate is written
//!   TTranslatableText("Context", QT_TRANSLATE_NOOP("Context", "Source"))
//! so that lupdate extracts it as before; text that is not to be translated
//! (names from files or libraries) is TTranslatableText::untranslated(...).
struct TTranslatableText {
  std::string context;     //!< the translation context (a class name)
  std::string source;      //!< the text, in UTF-8
  bool translate = false;  //!< whether the interface translates it

  TTranslatableText() {}
  TTranslatableText(const char* context_, const char* source_)
      : context(context_), source(source_), translate(true) {}

  static TTranslatableText untranslated(const std::string& text) {
    TTranslatableText t;
    t.source = text;
    return t;
  }
};

//! ONLY for text the core translates in order to STORE it in data (a new
//! palette's first page name, which is saved with the palette): the text
//! translated through the current translator. Text that is only shown is
//! translated by the interface, never through this.
DVAPI std::wstring translateForData(const TTranslatableText& text);

//! The one place that translator comes from: sets it and returns the one it
//! replaces; nullptr restores the built-in one, which is
//! QCoreApplication::translate in the build with Qt and, without Qt, the
//! source text as it is (so that a new palette's page is "colors" until the
//! iPad shell injects a translator).
typedef std::wstring (*DataTranslator)(const TTranslatableText& text);
DVAPI DataTranslator setDataTranslator(DataTranslator translator);

#endif
