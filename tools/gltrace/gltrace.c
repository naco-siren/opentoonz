// Logs the OpenGL calls that matter to dashed lines, via dyld interposing.
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
static FILE *out(void) { static FILE *f; if (!f) { const char *p = getenv("GLTRACE"); f = p ? fopen(p, "w") : stderr; } return f; }
static int on(void) { return getenv("GLTRACE") != 0; }
static const char *mode(GLenum m) { switch (m) { case GL_LINES: return "LINES"; case GL_LINE_STRIP: return "LINE_STRIP"; case GL_LINE_LOOP: return "LINE_LOOP"; case GL_TRIANGLES: return "TRIS"; case GL_TRIANGLE_STRIP: return "TSTRIP"; case GL_TRIANGLE_FAN: return "TFAN"; case GL_QUADS: return "QUADS"; case GL_QUAD_STRIP: return "QSTRIP"; case GL_POLYGON: return "POLYGON"; case GL_POINTS: return "POINTS"; } return "?"; }
static int nverts;
#define INTERPOSE(rep, orig) __attribute__((used)) static struct { const void *r; const void *o; } _i_##orig __attribute__((section("__DATA,__interpose"))) = { (const void *)(unsigned long)&rep, (const void *)(unsigned long)&orig };
static void my_glLineStipple(GLint f, GLushort p) { if (on()) fprintf(out(), "stipple %d %x\n", f, p); glLineStipple(f, p); }
static void my_glEnable(GLenum c) { if (on() && (c == GL_LINE_STIPPLE || c == GL_LINE_SMOOTH || c == GL_POLYGON_SMOOTH || c == GL_STENCIL_TEST || c == GL_BLEND)) fprintf(out(), "enable %x\n", c); glEnable(c); }
static void my_glDisable(GLenum c) { if (on() && (c == GL_LINE_STIPPLE || c == GL_LINE_SMOOTH || c == GL_POLYGON_SMOOTH || c == GL_STENCIL_TEST || c == GL_BLEND)) fprintf(out(), "disable %x\n", c); glDisable(c); }
static void my_glLineWidth(GLfloat w) { if (on()) fprintf(out(), "linewidth %g\n", w); glLineWidth(w); }
static void my_glBegin(GLenum m) { nverts = 0; glBegin(m); if (on()) fprintf(out(), "begin %s", mode(m)); }
static void my_glEnd(void) { glEnd(); if (on()) fprintf(out(), " %d verts\n", nverts); }
static void my_glVertex2d(GLdouble x, GLdouble y) { if (on()) fprintf(out(), " (%.3f %.3f)", x, y); nverts++; glVertex2d(x, y); }
static void my_glVertex2dv(const GLdouble *v) { if (on()) fprintf(out(), " (%.3f %.3f)", v[0], v[1]); nverts++; glVertex2dv(v); }
static void my_glVertex3d(GLdouble x, GLdouble y, GLdouble z) { if (on()) fprintf(out(), " (%.3f %.3f)", x, y); nverts++; glVertex3d(x, y, z); }
static void my_glDrawArrays(GLenum m, GLint f, GLsizei n) { if (on()) fprintf(out(), "drawarrays %s %d\n", mode(m), n); glDrawArrays(m, f, n); }
static void my_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { if (on()) fprintf(out(), "colormask %d%d%d%d\n", r, g, b, a); glColorMask(r, g, b, a); }
static void my_glHint(GLenum t, GLenum m) { if (on()) fprintf(out(), "hint %x %x\n", t, m); glHint(t, m); }
static void my_glPushAttrib(GLbitfield m) { if (on()) fprintf(out(), "pushattrib %x\n", m); glPushAttrib(m); }
static void my_glPopAttrib(void) { if (on()) fprintf(out(), "popattrib\n"); glPopAttrib(); }
INTERPOSE(my_glLineStipple, glLineStipple)
INTERPOSE(my_glEnable, glEnable)
INTERPOSE(my_glDisable, glDisable)
INTERPOSE(my_glLineWidth, glLineWidth)
INTERPOSE(my_glBegin, glBegin)
INTERPOSE(my_glEnd, glEnd)
INTERPOSE(my_glVertex2d, glVertex2d)
INTERPOSE(my_glVertex2dv, glVertex2dv)
INTERPOSE(my_glVertex3d, glVertex3d)
INTERPOSE(my_glDrawArrays, glDrawArrays)
INTERPOSE(my_glColorMask, glColorMask)
INTERPOSE(my_glHint, glHint)
INTERPOSE(my_glPushAttrib, glPushAttrib)
INTERPOSE(my_glPopAttrib, glPopAttrib)
