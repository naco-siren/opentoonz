#pragma once

#ifndef TGLVIEW_H
#define TGLVIEW_H

#include "tcommon.h"

#undef DVAPI
#undef DVVAR
#ifdef TNZGLVIEW_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

//! tnzglview: the desktop OpenGL drawing that left the core with step 8c of
//! doc/nexttoonz_core_noqt.md (tgl.h, tvectorgl.h, tofflinegl.h and the
//! rest keep their headers in include/).
namespace TGLView {

//! Puts the OpenGL implementations in the seams of tglentries.h: the viewer
//! canvas, the drawers of the styles that set OpenGL state of their own, and
//! the offline contexts. A program that can reach a drawing through OpenGL
//! calls this in main(), before it draws; without it the core draws nothing
//! through OpenGL, and says so once on the standard error.
DVAPI void install();

}  // namespace TGLView

#endif
