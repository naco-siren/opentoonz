# Runs inside the VM: adds environment-controlled variations to the GL canvas
# (in the VM's clone only) to find which difference from the old code moves
# Mesa's stipple phase.
import os
p = os.path.expanduser('~/opentoonz/toonz/sources/tnzglcanvas/tglcanvas.cpp')
s = open(p).read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:60]
    s = s.replace(old, new, 1)
s = s.replace('#include <algorithm>', '#include <algorithm>\n#include <cstdlib>', 1)
# X1: stipple on before a mesh that is not flat; X2: such a mesh in immediate mode
rep('''  if (!flat) {
    blendOver(m_state, bound.premultiplied(), drawTriangles);
    return;
  }''', '''  if (!flat) {
    if (getenv("X1")) { glEnable(GL_LINE_STIPPLE); glLineStipple(1, 0xF0F0); }
    if (getenv("X2")) {
      glBegin(mode);
      for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        glColor4f(colors[i].r, colors[i].g, colors[i].b, colors[i].a);
        glVertex2d(mesh.positions[i].x, mesh.positions[i].y);
      }
      glEnd();
      return;
    }
    blendOver(m_state, bound.premultiplied(), drawTriangles);
    return;
  }''')
# X3: no texture coordinates with line vertices
rep('''    for (const TCanvasPoint &point : contour.points) {
      glTexCoord2d(point.x, point.y);
      glVertex2d(point.x, point.y);
    }
    glEnd();
  }
}''', '''    for (const TCanvasPoint &point : contour.points) {
      if (!getenv("X3")) glTexCoord2d(point.x, point.y);
      glVertex2d(point.x, point.y);
    }
    glEnd();
  }
}''')
# X4: leave the stipple and the width alone afterwards; X5: do not set the width at all
rep('''    if (!pen.dash.empty()) glDisable(GL_LINE_STIPPLE);
    glLineWidth(1.f);''', '''    if (!getenv("X4")) {
      if (!pen.dash.empty()) glDisable(GL_LINE_STIPPLE);
      glLineWidth(1.f);
    }''')
rep('''  glLineWidth((GLfloat)std::min(std::max(pixels, (double)range[0]),
                                (double)range[1]));''', '''  if (!getenv("X5"))
    glLineWidth((GLfloat)std::min(std::max(pixels, (double)range[0]),
                                  (double)range[1]));''')
open(p, 'w').write(s)
