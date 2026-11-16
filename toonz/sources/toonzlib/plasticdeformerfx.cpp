

#include "toonz/plasticdeformerfx.h"

// TnzLib includes
#include "toonz/txsheet.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshcell.h"
#include "toonz/tcolumnfx.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/dpiscale.h"
#include "toonz/stage.h"
#include "toonz/tlog.h"

// TnzExt includes
#include "ext/plasticskeleton.h"
#include "ext/plasticdeformerstorage.h"
#include "ext/ttexturesstorage.h"
#include "ext/plasticvisualsettings.h"
#include "ext/meshutils.h"

// TnzBase includes
#include "trenderer.h"

// TnzCore includes
#include "tgl.h"
#include "tofflinegl.h"
#include "tgldisplaylistsmanager.h"
#include "tconvert.h"
#include "trop.h"
#include "tvectorrasterizer.h"
#include "tcanvasutil.h"

#include <QOpenGLFramebufferObject>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QOpenGLContext>
#include <QImage>
#include <QThread>
#include <algorithm>

FX_IDENTIFIER_IS_HIDDEN(PlasticDeformerFx, "plasticDeformerFx")

//***************************************************************************************************
//    Local namespace
//***************************************************************************************************

namespace {

std::string toString(const TAffine &aff) {
  // Note: toString distinguishes + and - 0. This is a problem when comparing
  // aliases, so near-zero values are explicitly rounded to 0.
  return (areAlmostEqual(aff.a11, 0.0) ? "0" : ::to_string(aff.a11, 5)) + "," +
         (areAlmostEqual(aff.a12, 0.0) ? "0" : ::to_string(aff.a12, 5)) + "," +
         (areAlmostEqual(aff.a13, 0.0) ? "0" : ::to_string(aff.a13, 5)) + "," +
         (areAlmostEqual(aff.a21, 0.0) ? "0" : ::to_string(aff.a21, 5)) + "," +
         (areAlmostEqual(aff.a22, 0.0) ? "0" : ::to_string(aff.a22, 5)) + "," +
         (areAlmostEqual(aff.a23, 0.0) ? "0" : ::to_string(aff.a23, 5));
}

//-----------------------------------------------------------------------------------

std::string toString(SkVD *vd, double sdFrame) {
  std::string result;

  for (int p = 0; p < SkVD::PARAMS_COUNT; ++p)
    result += ::to_string(vd->m_params[p]->getValue(sdFrame), 5) + " ";

  return result;
}

//-----------------------------------------------------------------------------------

std::string toString(const PlasticSkeleton::vertex_type &vx) {
  // TODO: Add z and rigidity support
  return ::to_string(vx.P().x, 5) + " " + ::to_string(vx.P().y, 5);
}

//-----------------------------------------------------------------------------------

std::string toString(const PlasticSkeletonDeformationP &sd, double sdFrame) {
  std::string result;

  const PlasticSkeletonP &skeleton = sd->skeleton(sdFrame);
  if (!skeleton || skeleton->empty()) return result;

  const tcg::list<PlasticSkeleton::vertex_type> &vertices =
      skeleton->vertices();

  tcg::list<PlasticSkeleton::vertex_type>::const_iterator vt,
      vEnd(vertices.end());

  result = toString(*vertices.begin());
  for (vt = vertices.begin(); vt != vEnd; ++vt) {
    result += "; " + toString(*vt);
    result += " " + toString(sd->vertexDeformation(vt->name()), sdFrame);
  }

  return result;
}

}  // namespace

//***************************************************************************************************
//    PlasticDeformerFx  implementation
//***************************************************************************************************

PlasticDeformerFx::PlasticDeformerFx() : TRasterFx() {
  addInputPort("source", m_port);
}

//-----------------------------------------------------------------------------------

TFx *PlasticDeformerFx::clone(bool recursive) const {
  PlasticDeformerFx *fx =
      dynamic_cast<PlasticDeformerFx *>(TFx::clone(recursive));
  assert(fx);

  fx->m_xsh = m_xsh;
  fx->m_col = m_col;

  return fx;
}

//-----------------------------------------------------------------------------------

bool PlasticDeformerFx::canHandle(const TRenderSettings &info, double frame) {
  // Affines are handled directly via OpenGL matrix pushes
  return true;
}

//-----------------------------------------------------------------------------------

std::string PlasticDeformerFx::getAlias(double frame,
                                        const TRenderSettings &info) const {
  std::string alias(getFxType());
  alias += "[";

  if (m_port.isConnected()) {
    TRasterFxP ifx = m_port.getFx();
    assert(ifx);

    alias += ifx->getAlias(frame, info);
  }

  TStageObject *meshColumnObj =
      m_xsh->getStageObject(TStageObjectId::ColumnId(m_col));
  const PlasticSkeletonDeformationP &sd =
      meshColumnObj->getPlasticSkeletonDeformation();
  if (sd) alias += ", " + toString(sd, meshColumnObj->paramsTime(frame));

  alias += "]";

  return alias;
}

//-----------------------------------------------------------------------------------

bool PlasticDeformerFx::doGetBBox(double frame, TRectD &bbox,
                                  const TRenderSettings &info) {
  if (!m_port.isConnected()) return false;

  // Bounding box computation for plastic deformation is complex; return
  // infinite rect
  bbox = TConsts::infiniteRectD;
  return true;
}

//-----------------------------------------------------------------------------------

void PlasticDeformerFx::buildRenderSettings(double frame,
                                            TRenderSettings &info) {
  // As previously noted, this FX is capable of handling affine transformations.
  // Therefore, it can determine the most suitable input reference to work with.
  // The best approach is to delegate this decision to the *input FX* itself,
  // via handledAffine() to get the appropriate affine reference
  m_was64bit = false;
  if (info.m_bpp == 64) {
    m_was64bit = true;
    info.m_bpp = 32;  // We need to fix the input to 32-bpp
  }
  info.m_affine = m_port->handledAffine(info, frame);
}

//-----------------------------------------------------------------------------------

bool PlasticDeformerFx::buildTextureDataSl(double frame, TRenderSettings &info,
                                           TAffine &worldLevelToLevelAff) {
  // Round instead of truncate for sub-xsheet compatibility
  int row = tround(frame);

  // Initialize level variables
  TLevelColumnFx *lcfx       = (TLevelColumnFx *)m_port.getFx();
  TXshLevelColumn *texColumn = lcfx->getColumn();

  const TXshCell &texCell = texColumn->getCell(row);

  TXshSimpleLevel *texSl = texCell.getSimpleLevel();
  const TFrameId &texFid = texCell.getFrameId();

  if (!texSl || texSl->getType() == MESH_XSHLEVEL) return false;

  // Build DPI data
  TPointD texDpi(texSl->getDpi(texFid, 0));
  if (texDpi.x == 0.0 || texDpi.y == 0.0 || texSl->getType() == PLI_XSHLEVEL)
    texDpi.x = texDpi.y = Stage::inch;

  // Build reference transform data.
  // NOTE: TAffine() here refers to IMAGE coordinates (not WORLD),
  // achieved by removing level DPI affine during render-tree construction (see
  // scenefx.cpp).

  worldLevelToLevelAff = TScale(texDpi.x / Stage::inch, texDpi.y / Stage::inch);

  // Initialize input render settings.
  // For vector images (PLI), allow full affine transform to preserve quality.
  // For raster images, use original reference if scaling up (scale > 1.0).
  // Otherwise, apply affine transform to avoid low-quality OpenGL minification.

  const TAffine &handledAff = TRasterFx::handledAffine(info, frame);

  if (texSl->getType() == PLI_XSHLEVEL) {
    info.m_affine = handledAff;
    buildRenderSettings(frame, info);
  } else {
    info.m_affine = TAffine();
    buildRenderSettings(frame, info);

    // Apply scale if handled affine is minifying (scale < 1.0)
    if (handledAff.a11 < worldLevelToLevelAff.a11)
      info.m_affine =
          TScale(handledAff.a11 / worldLevelToLevelAff.a11) * info.m_affine;
  }

  return true;
}

//-----------------------------------------------------------------------------------

bool PlasticDeformerFx::buildTextureData(double frame, TRenderSettings &info,
                                         TAffine &worldLevelToLevelAff) {
  // Common case (e.g., sub-XSheets): adjust info and match reference
  buildRenderSettings(frame, info);  // Adjust the info
  worldLevelToLevelAff = TAffine();  // Reference match
  return true;
}

//-----------------------------------------------------------------------------------

namespace {

// The deformed mesh drawn on the CPU canvas (migration step 7 of
// doc/nexttoonz_canvas_ir.md): the faces as tglDraw(mesh image, texture,
// ...) draws them, in the same stacking order, each textured with the
// input. The input stays premultiplied, as a canvas image is filtered; the
// OpenGL path has to take the premultiplication out because it smooths the
// mesh's border with antialiased lines, which only touch alpha. Here the
// border is the silhouette of one mesh drawn with smooth edges.
//
// One difference remains: where faces fold over each other, the canvas
// paints the topmost face over the ones below as one shape (the canvas
// mesh is painted once where its triangles overlap), while OpenGL blends
// each face over the ones before it. That is the same where the input is
// opaque.
bool drawOnCanvas(TTile &tile, const TTile &inTile, const TRectD &texBBox,
                  const TMeshImage &meshImage,
                  const PlasticDeformerDataGroup &group,
                  const TAffine &meshToTextureAff,
                  const TAffine &deformedMeshToTile) {
  const TRaster32P in = inTile.getRaster();
  if (!in) return false;

  // The input with a transparent pixel all round, so that clamped at its
  // edges it is transparent beyond them, as the OpenGL texture is.
  TRaster32P tex(in->getLx() + 2, in->getLy() + 2);
  tex->clear();
  tex->extract(1, 1, in->getLx(), in->getLy())->copy(in);

  TCanvasMesh mesh;
  mesh.mode = TCanvasMesh::Mode::Triangles;
  const std::vector<TTextureMeshP> &meshes = meshImage.meshes();
  for (const std::pair<int, int> &sorted : group.m_sortedFaces) {
    const TTextureMesh &m      = *meshes[sorted.second];
    const double *dstCoords    = group.m_datas[sorted.second].m_output.get();
    const TTextureMesh::face_type &fc = m.face(sorted.first);
    const TTextureMesh::edge_type &ed0 = m.edge(fc.edge(0)),
                                  &ed1 = m.edge(fc.edge(1));
    const int v[3] = {ed0.vertex(0), ed0.vertex(1),
                      ed1.vertex((ed1.vertex(0) == ed0.vertex(0)) |
                                 (ed1.vertex(0) == ed0.vertex(1)))};
    for (int i : v) {
      mesh.positions.push_back(
          TCanvasPoint(dstCoords[2 * i], dstCoords[2 * i + 1]));
      mesh.uvs.push_back(toCanvas(meshToTextureAff * m.vertex(i).P()));
    }
  }
  if (!mesh.isValid()) return false;

  TCanvasPaint paint(TCanvasColor(1.f, 1.f, 1.f, 1.f));
  paint.antialias           = true;
  TCanvasShader &shader     = paint.shader;
  shader.kind               = TCanvasShader::Kind::Image;
  // (tex outlives the drawing: nothing needs keeping alive.)
  shader.image.width        = tex->getLx();
  shader.image.height       = tex->getLy();
  shader.image.rowBytes     = (std::ptrdiff_t)tex->getWrap() * 4;
  shader.image.premultiplied = true;
  shader.imageToLocal =
      TCanvasAffine::translation(texBBox.x0 - 1.0, texBBox.y0 - 1.0);
  shader.tile   = TCanvasTile::Clamp;
  shader.filter = TCanvasFilter::Linear;

  TRaster32P out = tile.getRaster();
  TRaster32P drawn = out ? out : TRaster32P(tile.getRaster()->getSize());
  drawn->clear();
  tex->lock();
  shader.image.pixels = tex->getRawData();
  const bool done =
      TVectorRasterizer::drawOnCanvas(drawn, [&](TCanvas &canvas) {
        canvas.concat(toCanvas(deformedMeshToTile));
        canvas.drawMesh(mesh, paint);
      });
  tex->unlock();
  if (!done) return false;

  if (!out) TRop::convert(tile.getRaster(), drawn);
  return true;
}

}  // namespace

//-----------------------------------------------------------------------------------

void PlasticDeformerFx::doCompute(TTile &tile, double frame,
                                  const TRenderSettings &info) {
  if (!m_port.isConnected()) {
    tile.getRaster()->clear();
    return;
  }

  // Round instead of truncate for sub-xsheet compatibility
  int row = tround(frame);

  // Build texture data
  TRenderSettings texInfo(info);
  TAffine worldTexLevelToTexLevelAff;

  if (dynamic_cast<TLevelColumnFx *>(m_port.getFx())) {
    if (!buildTextureDataSl(frame, texInfo, worldTexLevelToTexLevelAff)) return;
  } else {
    buildTextureData(frame, texInfo, worldTexLevelToTexLevelAff);
  }

  // Initialize mesh level variables
  const TXshCell &meshCell = m_xsh->getCell(row, m_col);

  TXshSimpleLevel *meshSl = meshCell.getSimpleLevel();
  const TFrameId &meshFid = meshCell.getFrameId();

  if (!meshSl || meshSl->getType() != MESH_XSHLEVEL) return;

  // Retrieve mesh image and deformation
  TStageObject *meshColumnObj =
      m_xsh->getStageObject(TStageObjectId::ColumnId(m_col));

  TMeshImageP mi(meshSl->getFrame(meshFid, false));
  if (!mi) return;

  // Retrieve deformation data
  const PlasticSkeletonDeformationP &sd =
      meshColumnObj->getPlasticSkeletonDeformation();
  assert(sd);

  double sdFrame = meshColumnObj->paramsTime(frame);

  // Build DPI data
  TPointD meshDpi(meshSl->getDpi(meshFid, 0));
  assert(meshDpi.x != 0.0 && meshDpi.y != 0.0);

  // Build reference transforms
  const TAffine &imageToTextureAff           = texInfo.m_affine;
  const TAffine &worldTexLevelToWorldMeshAff = m_texPlacement;
  const TAffine &meshToWorldMeshAff =
      TScale(Stage::inch / meshDpi.x, Stage::inch / meshDpi.y);

  const TAffine &meshToTexLevelAff = worldTexLevelToTexLevelAff *
                                     worldTexLevelToWorldMeshAff.inv() *
                                     meshToWorldMeshAff;
  const TAffine &meshToTextureAff = imageToTextureAff * meshToTexLevelAff;

  // Retrieve deformer data
  TScale worldMeshToMeshAff(meshDpi.x / Stage::inch, meshDpi.y / Stage::inch);

  std::unique_ptr<const PlasticDeformerDataGroup> dataGroup(
      PlasticDeformerStorage::instance()->processOnce(
          sdFrame, mi.getPointer(), sd.getPointer(), sd->skeletonId(sdFrame),
          worldMeshToMeshAff));

  // Build the mesh's bounding box and map it to input reference
  TRectD meshBBox(meshToTextureAff * mi->getBBox());

  // Build the tile's geometry
  TRectD texBBox;
  m_port->getBBox(frame, texBBox, texInfo);

  // Use intersection (* operator) instead of union
  TRectD bbox = texBBox * meshBBox;
  if (bbox.getLx() <= 0.0 || bbox.getLy() <= 0.0) return;

  // Align to pixel boundaries
  bbox.x0 = tfloor(bbox.x0);
  bbox.y0 = tfloor(bbox.y0);
  bbox.x1 = tceil(bbox.x1);
  bbox.y1 = tceil(bbox.y1);

  TDimension tileSize(tround(bbox.getLx()), tround(bbox.getLy()));

  // Compute the input image
  TTile inTile;
  m_port->allocateAndCompute(inTile, bbox.getP00(), tileSize, TRasterP(), frame,
                             texInfo);

  if (TVectorRasterizer::isSelected() &&
      drawOnCanvas(tile, inTile, bbox, *mi, *dataGroup, meshToTextureAff,
                   TTranslation(-tile.m_pos) * info.m_affine *
                       meshToWorldMeshAff))
    return;

  // Draw the textured mesh using the working approach
  {
    // Prepare texture
    TRaster32P tex(inTile.getRaster());
    TRop::depremultiply(tex);
    static TAtomicVar var;
    const std::string &texId = "render_tex " + std::to_string(++var);

    // Use the working context creation approach
    std::unique_ptr<QOpenGLContext> context(new QOpenGLContext());
    context->moveToThread(QThread::currentThread());

    // Share context with current if available
    if (QOpenGLContext::currentContext())
      context->setShareContext(QOpenGLContext::currentContext());

    context->setFormat(QSurfaceFormat::defaultFormat());

    if (!context->create()) {
      TSysLog::error("PlasticDeformerFx: Failed to create OpenGL context");
      tile.getRaster()->clear();
      return;
    }

    if (!context->makeCurrent(info.m_offScreenSurface.get())) {
      TSysLog::error("PlasticDeformerFx: Failed to make context current");
      tile.getRaster()->clear();
      return;
    }

    TDimension d = tile.getRaster()->getSize();
    QOpenGLFramebufferObject fb(d.lx, d.ly);

    if (!fb.bind()) {
      TSysLog::error("PlasticDeformerFx: Failed to bind FBO");
      tile.getRaster()->clear();
      return;
    }

    // Load texture
    TTexturesStorage *ts                = TTexturesStorage::instance();
    const DrawableTextureDataP &texData = ts->loadTexture(texId, tex, bbox);
    if (!texData) {
      TSysLog::error("PlasticDeformerFx: Failed to load texture data");
      fb.release();
      tile.getRaster()->clear();
      return;
    }

    // Set up OpenGL for rendering
    glViewport(0, 0, d.lx, d.ly);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, d.lx, 0, d.ly);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Use the working transformation
    tglMultMatrix(TTranslation(-tile.m_pos) * info.m_affine *
                  meshToWorldMeshAff);

    glEnable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);

    // Draw the mesh
    tglDraw(*mi, *texData, meshToTextureAff, *dataGroup);

    // Retrieve drawing and copy to output tile

    QImage img = fb.toImage().scaled(QSize(d.lx, d.ly), Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
    int wrap   = tile.getRaster()->getLx() * sizeof(TPixel32);
    if (!m_was64bit) {
      uchar *srcPix = img.bits();
      uchar *dstPix = tile.getRaster()->getRawData() + wrap * (d.ly - 1);
      for (int y = 0; y < d.ly; y++) {
        memcpy(dstPix, srcPix, wrap);
        dstPix -= wrap;
        srcPix += wrap;
      }
    } else if (m_was64bit) {
      TRaster64P newRaster(tile.getRaster()->getSize());
      TRaster32P tempRaster(tile.getRaster()->getSize());
      uchar *srcPix = img.bits();
      uchar *dstPix = tempRaster.getPointer()->getRawData() + wrap * (d.ly - 1);
      for (int y = 0; y < d.ly; y++) {
        memcpy(dstPix, srcPix, wrap);
        dstPix -= wrap;
        srcPix += wrap;
      }
      TRop::convert(newRaster, tempRaster);
      int size = tile.getRaster()->getLx() * tile.getRaster()->getLy() *
                 sizeof(TPixel64);
      srcPix = newRaster.getPointer()->getRawData();
      dstPix = tile.getRaster()->getRawData();
      memcpy(dstPix, srcPix, size);
      texInfo.m_bpp = 64;
    }

    fb.release();

    // Cleanup context->getRaster(tile.getRaster());
    glFlush();
    glFinish();

    // Unload texture to prevent memory leaks
    // ts->unloadTexture(texId); // Auto-released ttexturesstorage??

    context->doneCurrent();
  }

  // Verify no OpenGL errors
  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    TSysLog::warning("PlasticDeformerFx: OpenGL error after draw: " +
                     std::to_string(err));
  }
}

//-----------------------------------------------------------------------------------

void PlasticDeformerFx::doDryCompute(TRectD &rect, double frame,
                                     const TRenderSettings &info) {
  if (!m_port.isConnected()) return;

  // Round instead of truncate for sub-xsheet compatibility
  int row = tround(frame);

  TRenderSettings texInfo(info);
  TAffine worldTexLevelToTexLevelAff;

  if (dynamic_cast<TLevelColumnFx *>(m_port.getFx())) {
    if (!buildTextureDataSl(frame, texInfo, worldTexLevelToTexLevelAff)) return;
  } else {
    buildTextureData(frame, texInfo, worldTexLevelToTexLevelAff);
  }

  const TXshCell &meshCell = m_xsh->getCell(row, m_col);

  TXshSimpleLevel *meshSl = meshCell.getSimpleLevel();
  const TFrameId &meshFid = meshCell.getFrameId();

  if (!meshSl || meshSl->getType() != MESH_XSHLEVEL) return;

  TStageObject *meshColumnObj =
      m_xsh->getStageObject(TStageObjectId::ColumnId(m_col));

  TMeshImageP mi(meshSl->getFrame(meshFid, false));
  if (!mi) return;

  const PlasticSkeletonDeformationP &sd =
      meshColumnObj->getPlasticSkeletonDeformation();
  assert(sd);

  TPointD meshDpi(meshSl->getDpi(meshFid, 0));
  assert(meshDpi.x != 0.0 && meshDpi.y != 0.0);

  const TAffine &textureToImageAff        = texInfo.m_affine;
  const TAffine &worldImageToWorldMeshAff = m_texPlacement;
  const TAffine &meshToWorldMeshAff =
      TScale(Stage::inch / meshDpi.x, Stage::inch / meshDpi.y);

  const TAffine &meshToTextureAff =
      textureToImageAff.inv() * worldTexLevelToTexLevelAff *
      worldImageToWorldMeshAff.inv() * meshToWorldMeshAff;

  // Build the mesh's bounding box and map it to input reference
  TRectD meshBBox(meshToTextureAff * mi->getBBox());

  // Build the tile's geometry
  TRectD texBBox;
  m_port->getBBox(frame, texBBox, texInfo);

  TRectD bbox = texBBox * meshBBox;
  if (bbox.getLx() <= 0.0 || bbox.getLy() <= 0.0) return;

  // Align to pixel boundaries
  bbox.x0 = tfloor(bbox.x0);
  bbox.y0 = tfloor(bbox.y0);
  bbox.x1 = tceil(bbox.x1);
  bbox.y1 = tceil(bbox.y1);

  m_port->dryCompute(bbox, frame, texInfo);
}
