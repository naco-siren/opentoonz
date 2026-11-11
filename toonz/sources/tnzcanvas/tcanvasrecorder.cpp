#include "canvas/tcanvasrecorder.h"

#include <locale>
#include <sstream>
#include <utility>

//=============================================================================
// Text output

namespace {

// Writes numbers the same way whatever the process locale is (Qt switches
// the C locale to the user's, where the decimal separator may be a comma).
class Writer {
public:
  Writer() {
    m_out.imbue(std::locale::classic());
    m_out.precision(6);
  }

  std::string str() const { return m_out.str(); }

  Writer &operator<<(const char *text) {
    m_out << text;
    return *this;
  }
  Writer &operator<<(std::size_t n) {
    m_out << n;
    return *this;
  }
  Writer &operator<<(int n) {
    m_out << n;
    return *this;
  }
  Writer &operator<<(double value) {
    m_out << (value == 0.0 ? 0.0 : value);  // no "-0"
    return *this;
  }
  Writer &operator<<(const TCanvasPoint &p) {
    return *this << p.x << " " << p.y;
  }
  Writer &operator<<(const TCanvasColor &c) {
    return *this << "(" << (double)c.r << " " << (double)c.g << " "
                 << (double)c.b << " " << (double)c.a << ")";
  }
  Writer &operator<<(const TCanvasAffine &m) {
    return *this << "[" << m.a11 << " " << m.a12 << " " << m.a13 << "; "
                 << m.a21 << " " << m.a22 << " " << m.a23 << "]";
  }

  void indent(int depth) {
    for (int i = 0; i < depth; ++i) m_out << "  ";
  }

private:
  std::ostringstream m_out;
};

const char *name(TCanvasFillRule rule) {
  return rule == TCanvasFillRule::NonZero ? "nonzero" : "evenodd";
}

const char *name(TCanvasMesh::Mode mode) {
  switch (mode) {
  case TCanvasMesh::Mode::Triangles:
    return "triangles";
  case TCanvasMesh::Mode::TriangleStrip:
    return "strip";
  case TCanvasMesh::Mode::TriangleFan:
    return "fan";
  case TCanvasMesh::Mode::Quads:
    return "quads";
  case TCanvasMesh::Mode::Polygon:
    return "polygon";
  }
  return "";
}

const char *name(TCanvasFilter filter) {
  return filter == TCanvasFilter::Nearest ? "nearest" : "linear";
}

void writeImage(Writer &out, const TCanvasImage &image) {
  out << image.width << "x" << image.height
      << (image.premultiplied ? " premul" : " straight");
}

void writeStops(Writer &out, const std::vector<TCanvasGradientStop> &stops) {
  out << "[";
  for (std::size_t i = 0; i < stops.size(); ++i) {
    if (i) out << " ";
    out << stops[i].offset << ":" << stops[i].color;
  }
  out << "]";
}

void writePaint(Writer &out, const TCanvasPaint &paint) {
  out << "color=" << paint.color;
  const TCanvasShader &shader = paint.shader;
  switch (shader.kind) {
  case TCanvasShader::Kind::None:
    break;
  case TCanvasShader::Kind::LinearGradient:
    out << " shader=linear((" << shader.p0 << ")->(" << shader.p1 << ") ";
    writeStops(out, shader.stops);
    out << ")";
    break;
  case TCanvasShader::Kind::RadialGradient:
    out << " shader=radial((" << shader.p0 << ") r=" << shader.radius << " ";
    writeStops(out, shader.stops);
    out << ")";
    break;
  case TCanvasShader::Kind::Image:
    out << " shader=image(";
    writeImage(out, shader.image);
    out << (shader.tile == TCanvasTile::Repeat ? " repeat " : " clamp ")
        << name(shader.filter) << " " << shader.imageToLocal << ")";
    break;
  }
  if (!paint.antialias) out << " noaa";
  if (paint.growDevice != 0.0) out << " grow=" << paint.growDevice << "px";
}

void writePen(Writer &out, const TCanvasPen &pen) {
  out << "width=" << pen.width
      << (pen.unit == TCanvasPen::Unit::Device ? "px" : "");
  if (pen.cap == TCanvasPen::Cap::Round) out << " cap=round";
  if (pen.join == TCanvasPen::Join::Round) out << " join=round";
  if (!pen.dash.empty()) {
    out << " dash=[";
    for (std::size_t i = 0; i < pen.dash.size(); ++i) {
      if (i) out << " ";
      out << pen.dash[i];
    }
    out << "]";
  }
}

// One line per contour, in the letters of an SVG path.
void writePath(Writer &out, const TCanvasPath &path, int depth) {
  const std::vector<TCanvasPoint> &points = path.points();
  std::size_t p                           = 0;
  bool lineOpen                           = false;
  for (TCanvasPath::Verb verb : path.verbs()) {
    switch (verb) {
    case TCanvasPath::MoveTo:
      if (lineOpen) out << "\n";
      out.indent(depth);
      out << "M " << points[p++];
      lineOpen = true;
      break;
    case TCanvasPath::LineTo:
      out << " L " << points[p++];
      break;
    case TCanvasPath::QuadTo:
      out << " Q " << points[p] << " " << points[p + 1];
      p += 2;
      break;
    case TCanvasPath::Close:
      out << " Z";
      break;
    }
  }
  if (lineOpen) out << "\n";
}

void writeMesh(Writer &out, const TCanvasMesh &mesh, int depth) {
  out.indent(depth);
  out << "positions:";
  for (const TCanvasPoint &p : mesh.positions) out << " (" << p << ")";
  out << "\n";
  if (!mesh.colors.empty()) {
    out.indent(depth);
    out << "colors:";
    for (const TCanvasColor &c : mesh.colors) out << " " << c;
    out << "\n";
  }
  if (!mesh.uvs.empty()) {
    out.indent(depth);
    out << "uvs:";
    for (const TCanvasPoint &p : mesh.uvs) out << " (" << p << ")";
    out << "\n";
  }
}

struct DumpVisitor {
  Writer &out;
  int &depth;

  void operator()(const TCanvasOpSave &) const {
    out.indent(depth);
    out << "save\n";
    ++depth;
  }
  void operator()(const TCanvasOpRestore &) const {
    if (depth > 0) --depth;
    out.indent(depth);
    out << "restore\n";
  }
  void operator()(const TCanvasOpConcat &op) const {
    out.indent(depth);
    out << "concat " << op.aff << "\n";
  }
  void operator()(const TCanvasOpClipPath &op) const {
    out.indent(depth);
    out << "clipPath "
        << (op.op == TCanvasClipOp::Intersect ? "intersect " : "difference ")
        << name(op.rule) << (op.antialias ? "" : " noaa") << "\n";
    writePath(out, op.path, depth + 1);
  }
  void operator()(const TCanvasOpClipMesh &op) const {
    out.indent(depth);
    out << "clipMesh "
        << (op.op == TCanvasClipOp::Intersect ? "intersect " : "difference ")
        << name(op.mesh.mode) << "\n";
    writeMesh(out, op.mesh, depth + 1);
  }
  void operator()(const TCanvasOpFillPath &op) const {
    out.indent(depth);
    out << "fillPath " << name(op.rule) << " ";
    writePaint(out, op.paint);
    out << "\n";
    writePath(out, op.path, depth + 1);
  }
  void operator()(const TCanvasOpFillPathAndClip &op) const {
    out.indent(depth);
    out << "fillPathAndClip " << name(op.rule) << " ";
    writePaint(out, op.paint);
    out << "\n";
    writePath(out, op.path, depth + 1);
  }
  void operator()(const TCanvasOpStrokePath &op) const {
    out.indent(depth);
    out << "strokePath ";
    writePen(out, op.pen);
    out << " ";
    writePaint(out, op.paint);
    out << "\n";
    writePath(out, op.path, depth + 1);
  }
  void operator()(const TCanvasOpDrawMesh &op) const {
    out.indent(depth);
    out << "drawMesh ";
    out << name(op.mesh.mode);
    if (!op.mesh.smoothEdges) out << " hardedges";
    out << " ";
    writePaint(out, op.paint);
    out << "\n";
    writeMesh(out, op.mesh, depth + 1);
  }
  void operator()(const TCanvasOpDrawImage &op) const {
    out.indent(depth);
    out << "drawImage ";
    writeImage(out, op.image);
    out << " " << name(op.options.filter) << " opacity=" << op.options.opacity
        << " " << op.imageToLocal << "\n";
  }
};

struct ReplayVisitor {
  TCanvas &canvas;

  void operator()(const TCanvasOpSave &) const { canvas.save(); }
  void operator()(const TCanvasOpRestore &) const { canvas.restore(); }
  void operator()(const TCanvasOpConcat &op) const { canvas.concat(op.aff); }
  void operator()(const TCanvasOpClipPath &op) const {
    canvas.clipPath(op.path, op.rule, op.op, op.antialias);
  }
  void operator()(const TCanvasOpClipMesh &op) const {
    canvas.clipMesh(op.mesh, op.op);
  }
  void operator()(const TCanvasOpFillPath &op) const {
    canvas.fillPath(op.path, op.rule, op.paint);
  }
  void operator()(const TCanvasOpFillPathAndClip &op) const {
    canvas.fillPathAndClip(op.path, op.rule, op.paint);
  }
  void operator()(const TCanvasOpStrokePath &op) const {
    canvas.strokePath(op.path, op.pen, op.paint);
  }
  void operator()(const TCanvasOpDrawMesh &op) const {
    canvas.drawMesh(op.mesh, op.paint);
  }
  void operator()(const TCanvasOpDrawImage &op) const {
    canvas.drawImage(op.image, op.imageToLocal, op.options);
  }
};

}  // namespace

//=============================================================================
// TCanvasDisplayList

void TCanvasDisplayList::replay(TCanvas &canvas) const {
  const ReplayVisitor visitor = {canvas};
  for (const TCanvasOp &op : ops) std::visit(visitor, op);
}

std::string TCanvasDisplayList::dump() const {
  Writer out;
  int depth                 = 0;
  const DumpVisitor visitor = {out, depth};
  for (const TCanvasOp &op : ops) std::visit(visitor, op);
  return out.str();
}

//=============================================================================
// TCanvasRecorder

TCanvasRecorder::TCanvasRecorder(const TCanvasIRect &deviceBounds)
    : m_bounds(deviceBounds) {}

TCanvasDisplayList TCanvasRecorder::takeDisplayList() {
  TCanvasDisplayList list;
  list.ops.swap(m_list.ops);
  return list;
}

void TCanvasRecorder::onSave() { m_list.ops.emplace_back(TCanvasOpSave()); }

void TCanvasRecorder::onRestore() {
  m_list.ops.emplace_back(TCanvasOpRestore());
}

void TCanvasRecorder::onConcat(const TCanvasAffine &aff) {
  m_list.ops.emplace_back(TCanvasOpConcat{aff});
}

void TCanvasRecorder::onClipPath(const TCanvasPath &path, TCanvasFillRule rule,
                                 TCanvasClipOp op, bool antialias) {
  m_list.ops.emplace_back(TCanvasOpClipPath{path, rule, op, antialias});
}

void TCanvasRecorder::onClipMesh(const TCanvasMesh &mesh, TCanvasClipOp op) {
  m_list.ops.emplace_back(TCanvasOpClipMesh{mesh, op});
}

void TCanvasRecorder::onFillPath(const TCanvasPath &path, TCanvasFillRule rule,
                                 const TCanvasPaint &paint) {
  m_list.ops.emplace_back(TCanvasOpFillPath{path, rule, paint});
}

void TCanvasRecorder::onFillPathAndClip(const TCanvasPath &path,
                                        TCanvasFillRule rule,
                                        const TCanvasPaint &paint) {
  m_list.ops.emplace_back(TCanvasOpFillPathAndClip{path, rule, paint});
}

void TCanvasRecorder::onStrokePath(const TCanvasPath &path,
                                   const TCanvasPen &pen,
                                   const TCanvasPaint &paint) {
  m_list.ops.emplace_back(TCanvasOpStrokePath{path, pen, paint});
}

void TCanvasRecorder::onDrawMesh(const TCanvasMesh &mesh,
                                 const TCanvasPaint &paint) {
  m_list.ops.emplace_back(TCanvasOpDrawMesh{mesh, paint});
}

void TCanvasRecorder::onDrawImage(const TCanvasImage &image,
                                  const TCanvasAffine &imageToLocal,
                                  const TCanvasImageOptions &options) {
  m_list.ops.emplace_back(TCanvasOpDrawImage{image, imageToLocal, options});
}
