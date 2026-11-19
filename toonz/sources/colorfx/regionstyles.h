#pragma once

#ifndef TDERIVEDREGIONSTYLES_H
#define TDERIVEDREGIONSTYLES_H

// TnzCore includes
#include "tvectorimage.h"
#include "tregionoutline.h"
#include "tsimplecolorstyles.h"

#undef DVAPI
#undef DVVAR

#ifdef COLORFX_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

//======================================================

//    Forward declarations

class TRandom;
struct TCanvasPen;

//======================================================

//============================================================

class MovingModifier final : public TOutlineStyle::RegionOutlineModifier {
  TPointD m_move;

public:
  MovingModifier(const TPointD &point) : m_move(point) {}

  TOutlineStyle::RegionOutlineModifier *clone() const override;

  TPointD getMovePoint() const { return m_move; }

  void modify(TRegionOutline &outline) const override;

public:
  void loadData(TInputStreamInterface &is) { is >> m_move.x >> m_move.y; }

  void saveData(TOutputStreamInterface &os) const {
    os << m_move.x << m_move.y;
  }
};

//============================================================

class MovingSolidColor final : public TSolidColorStyle {
public:
  MovingSolidColor(const TPixel32 &color, const TPointD &move);

  TColorStyle *clone() const override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  int getTagId() const override { return 1125; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText("MovingSolidColor",
                             QT_TRANSLATE_NOOP("MovingSolidColor", "Offset"));
  }
  std::string getBrushIdName() const override { return "MovingSolidColor"; }

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;
};

//============================================================

class DVAPI ShadowStyle final : public TSolidColorStyle {
  TPointD m_shadowDirection;
  TPixel32 m_shadowColor;
  double m_density;
  double m_len;

public:
  ShadowStyle(const TPixel32 &bgColor, const TPixel32 &shadowColor,
              const TPointD &shadowDirection = TPointD(-1, -1),
              double len = 30.0, double density = 0.4);

  TColorStyle *clone() const override;

  void makeIcon(const TDimension &d) override;
  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  // TPixel32 getMainColor() const {return m_shadowColor; }
  // void setMainColor(const TPixel32 &color){ m_shadowColor=color; }

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  int getTagId() const override { return 1127; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText(
        "ShadowStyle", QT_TRANSLATE_NOOP("ShadowStyle", "Hatched Shading"));
  }
  std::string getBrushIdName() const override { return "ShadowStyle"; }

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;

private:
  void drawPolyline(TCanvas &canvas, const TColorFunction *cf,
                    std::vector<T3DPointD> &polyline,
                    TPointD shadowDirection) const;
};

//============================================================

class DVAPI ShadowStyle2 final : public TSolidColorStyle {
  TPointD m_shadowDirection;
  TPixel32 m_shadowColor;
  double m_shadowLength;

public:
  ShadowStyle2(const TPixel32 &bgColor, const TPixel32 &shadowColor,
               const TPointD &shadowDirection = TPointD(-1, -1),
               double shadowLength            = 70.0);

  TColorStyle *clone() const override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  // TPixel32 getMainColor() const {return m_shadowColor; }
  // void setMainColor(const TPixel32 &color){ m_shadowColor=color; }

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  int getTagId() const override { return 1135; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText("ShadowStyle2",
                             QT_TRANSLATE_NOOP("ShadowStyle2", "Plain Shadow"));
  }
  std::string getBrushIdName() const override { return "ShadowStyle2"; }

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;

private:
  void drawPolyline(TCanvas &canvas, const TColorFunction *cf,
                    const std::vector<T3DPointD> &polyline,
                    TPointD shadowDirection) const;
};

//============================================================

class RubberModifier final : public TOutlineStyle::RegionOutlineModifier {
  double m_deform;

public:
  RubberModifier(double deform) : m_deform(deform) {}

  void loadData(TInputStreamInterface &is) { is >> m_deform; }
  void saveData(TOutputStreamInterface &os) const { os << m_deform; }

  double getDeform() { return m_deform; }
  void setDeform(const double deform) { m_deform = deform; }

  void modify(TRegionOutline &outline) const override;

  TOutlineStyle::RegionOutlineModifier *clone() const override;
};

//============================================================

class DVAPI TRubberFillStyle final : public TSolidColorStyle {
  typedef std::vector<TQuadratic> QuadraticVector;
  typedef std::vector<TQuadratic *> QuadraticPVector;

public:
  TRubberFillStyle(const TPixel32 &color, double deform);

  TColorStyle *clone() const override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void makeIcon(const TDimension &d) override;

  int getTagId() const override { return 1128; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText("TRubberFillStyle",
                             QT_TRANSLATE_NOOP("TRubberFillStyle", "Blob"));
  }
  std::string getBrushIdName() const override { return "RubberFillStyle"; }

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;

private:
  void transformPolylines();
};

//============================================================

class DVAPI TPointShadowFillStyle final : public TSolidColorStyle {
  TPointD m_shadowDirection;
  TPixel32 m_shadowColor;
  double m_shadowSize;
  double m_density;
  double m_pointSize;

public:
  TPointShadowFillStyle(const TPixel32 &bgColor, const TPixel32 &shadowColor,
                        const TPointD &shadowDirection = TPointD(-1, -1),
                        double density = 0.1, double shadowSize = 30.0,
                        double pointSize = 5.0);

  TColorStyle *clone() const override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  /*
TPixel32 getMainColor() const {return m_shadowColor; }
void setMainColor(const TPixel32 &color){ m_shadowColor=color; }
*/

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  int getTagId() const override { return 1129; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText(
        "TPointShadowFillStyle",
        QT_TRANSLATE_NOOP("TPointShadowFillStyle", "Sponge Shading"));
  }
  std::string getBrushIdName() const override { return "PointShadowFillStyle"; }

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;

private:
  double triangleArea(const TPointD &a, const TPointD &b,
                      const TPointD &c) const;
  void shadowOnEdge_parallel(TCanvas &canvas, const TCanvasPen &pen,
                             const TPointD &p0, const TPointD &p1,
                             const TPointD &p2, TRandom &rnd) const;

  // The smallest and the largest smooth point of the OpenGL driver the
  // style is drawn with, in pixels; without OpenGL, these.
  static constexpr float kSmallestPoint = 1.f;
  static constexpr float kLargestPoint  = 64.f;
  void drawOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                    bool antiAliasing, TRegionOutline &boundary,
                    float smallestPoint, float largestPoint) const;

  void deleteSameVerts(TRegionOutline::Boundary::iterator &rit,
                       std::vector<T3DPointD> &pv) const;
};

//============================================================

class DVAPI TDottedFillStyle final : public TSolidColorStyle {
  TPixel32 m_pointColor;
  double m_dotSize;
  double m_dotDist;
  bool m_isShifted;

public:
  TDottedFillStyle(const TPixel32 &bgColor, const TPixel32 &pointColor,
                   const double dotSize, const double dotDist,
                   const bool isShifted);

  TDottedFillStyle(const TPixel32 &color);

  TColorStyle *clone() const override;

  /*
TPixel32 getMainColor() const {return m_pointColor; }
void setMainColor(const TPixel32 &color){ m_pointColor=color; }
*/

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

  int getTagId() const override { return 1130; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText(
        "TDottedFillStyle",
        QT_TRANSLATE_NOOP("TDottedFillStyle", "Polka Dots"));
  }
  std::string getBrushIdName() const override { return "DottedFillStyle"; }

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;

private:
  int nbClip(const double LDotDist, const bool LIsShifted,
             const TRectD &bbox) const;
};

//============================================================

class DVAPI TCheckedFillStyle final : public TSolidColorStyle {
  TPixel32 m_pointColor;
  double m_HDist, m_HAngle;
  double m_VDist, m_VAngle, m_Thickness;

public:
  TCheckedFillStyle(const TPixel32 &bgColor, const TPixel32 &pointColor,
                    const double HDist, const double HAngle, const double VDist,
                    const double VAngle, const double Thickness);

  TCheckedFillStyle(const TPixel32 &color);

  TColorStyle *clone() const override;

  /*
TPixel32 getMainColor() const {return m_pointColor; }
void setMainColor(const TPixel32 &color){ m_pointColor=color; }
*/

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

  int getTagId() const override { return 1131; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText("TCheckedFillStyle",
                             QT_TRANSLATE_NOOP("TCheckedFillStyle", "Square"));
  }
  std::string getBrushIdName() const override { return "CheckedFillStyle"; }

private:
  void getHThickline(const TPointD &lc, const double lx, TPointD &p0,
                     TPointD &p1, TPointD &p2, TPointD &p3) const;
  void getVThickline(const TPointD &lc, const double ly, TPointD &p0,
                     TPointD &p1, TPointD &p2, TPointD &p3) const;
  int nbClip(const TRectD &bbox) const;

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;
};

//============================================================

class ArtisticModifier final : public TOutlineStyle::RegionOutlineModifier {
  TPointD m_move;
  double m_period;

public:
  ArtisticModifier(const TPointD &point, double period)
      : m_move(point), m_period(period) {}

  TOutlineStyle::RegionOutlineModifier *clone() const override;

  void loadData(TInputStreamInterface &is) {
    is >> m_move.x >> m_move.y >> m_period;
  }
  void saveData(TOutputStreamInterface &os) const {
    os << m_move.x << m_move.y << m_period;
  }

  TPointD getMovePoint() const { return m_move; }
  double getPeriod() const { return m_period; }

  void modify(TRegionOutline &outline) const override;
};

//============================================================

class ArtisticSolidColor final : public TSolidColorStyle {
public:
  ArtisticSolidColor(const TPixel32 &color, const TPointD &move, double period);

  TColorStyle *clone() const override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  int getTagId() const override { return 1132; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText(
        "ArtisticSolidColor",
        QT_TRANSLATE_NOOP("ArtisticSolidColor", "Irregular"));
  }
  std::string getBrushIdName() const override { return "ArtisticSolidColor"; }

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;
};

//============================================================

class DVAPI TChalkFillStyle final : public TSolidColorStyle {
  TPixel32 m_color0;
  double m_density, m_size;

public:
  TChalkFillStyle(const TPixel32 &color0, const TPixel32 &color1,
                  const double density, const double size);
  TChalkFillStyle(const TPixel32 &color0, const TPixel32 &color1);

  TColorStyle *clone() const override;

  /*
TPixel32 getMainColor() const {return m_color0; }
void setMainColor(const TPixel32 &color){ m_color0=color; }
*/

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

  TTranslatableText getDescriptionText() const override {
    return TTranslatableText("TChalkFillStyle",
                             QT_TRANSLATE_NOOP("TChalkFillStyle", "Chalk"));
  }
  std::string getBrushIdName() const override { return "ChalkFillStyle"; }

  void loadData(int oldId, TInputStreamInterface &) override;
  void getObsoleteTagIds(std::vector<int> &ids) const override {
    ids.push_back(1133);
  }
  int getTagId() const override { return 1143; };

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;
};

//============================================================

class DVAPI TChessFillStyle final : public TSolidColorStyle {
  TPixel32 m_pointColor;
  double m_HDist, m_VDist, m_Angle;

public:
  TChessFillStyle(const TPixel32 &bgColor, const TPixel32 &pointColor,
                  const double HDist, const double VDist, const double Angle);
  TChessFillStyle(const TPixel32 &color);

  TColorStyle *clone() const override;

  /*
TPixel32 getMainColor() const {return m_pointColor; }
void setMainColor(const TPixel32 &color){ m_pointColor=color; }
*/

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

  int getTagId() const override { return 1136; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText(
        "TChessFillStyle", QT_TRANSLATE_NOOP("TChessFillStyle", "Chessboard"));
  }
  std::string getBrushIdName() const override { return "ChessFillStyle"; }

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;

private:
  void makeGrid(TRectD &bbox, TRotation &rotM, std::vector<TPointD> &grid,
                int &nbClip) const;
};

//============================================================

class DVAPI TStripeFillStyle final : public TSolidColorStyle {
  TPixel32 m_pointColor;
  double m_Dist, m_Angle, m_Thickness;

public:
  TStripeFillStyle(const TPixel32 &bgColor, const TPixel32 &pointColor,
                   const double Dist, const double Angle,
                   const double Thickness);
  TStripeFillStyle(const TPixel32 &color);

  TColorStyle *clone() const override;

  /*
TPixel32 getMainColor() const {return m_pointColor; }
void setMainColor(const TPixel32 &color){ m_pointColor=color; }
*/

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

  int getTagId() const override { return 1137; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText("TStripeFillStyle",
                             QT_TRANSLATE_NOOP("TStripeFillStyle", "Banded"));
  }
  std::string getBrushIdName() const override { return "StripeFillStyle"; }

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;

  void makeIcon(const TDimension &d) override;

private:
  void getThickline(const TPointD &lc, const double ly, TPointD &p0,
                    TPointD &p1, TPointD &p2, TPointD &p3) const;
  int nbClip(const TRectD &bbox) const;
};

//============================================================

class DVAPI TLinGradFillStyle final : public TSolidColorStyle {
  TPixel32 m_pointColor;
  double m_Angle;
  double m_XPos, m_YPos, m_Size;

public:
  TLinGradFillStyle(const TPixel32 &bgColor, const TPixel32 &pointColor,
                    const double Angle, const double XPos, const double YPos,
                    const double Size);
  TLinGradFillStyle(const TPixel32 &color);

  TColorStyle *clone() const override;

  /*
TPixel32 getMainColor() const {return m_pointColor; }
void setMainColor(const TPixel32 &color){ m_pointColor=color; }
*/

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

  int getTagId() const override { return 1138; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText(
        "TLinGradFillStyle",
        QT_TRANSLATE_NOOP("TLinGradFillStyle", "Linear Gradient"));
  }
  std::string getBrushIdName() const override { return "LinGradFillStyle"; }

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;

private:
  void getRects(const TRectD &bbox, std::vector<TPointD> &r0,
                std::vector<TPointD> &r1, std::vector<TPointD> &r2) const;

  void getRect(const TRectD &bbox, std::vector<TPointD> &r) const;
};

//============================================================

class DVAPI TRadGradFillStyle final : public TSolidColorStyle {
  TPixel32 m_pointColor;
  double m_Radius;
  double m_XPos, m_YPos;
  double m_Smooth;

public:
  TRadGradFillStyle(const TPixel32 &bgColor, const TPixel32 &pointColor,
                    const double XPos, const double YPos, const double Radius,
                    const double Smooth);
  TRadGradFillStyle(const TPixel32 &color);

  TColorStyle *clone() const override;

  //  TPixel32 getMainColor() const {return m_pointColor; }
  //  void setMainColor(const TPixel32 &color){ m_pointColor=color; }

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

  int getTagId() const override { return 1139; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText(
        "TRadGradFillStyle",
        QT_TRANSLATE_NOOP("TRadGradFillStyle", "Radial Gradient"));
  }
  std::string getBrushIdName() const override { return "RadGradFillStyle"; }

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;
};

//============================================================

class DVAPI TCircleStripeFillStyle final : public TSolidColorStyle {
  TPixel32 m_pointColor;
  double m_XPos, m_YPos;
  double m_Dist, m_Thickness;

public:
  TCircleStripeFillStyle(const TPixel32 &bgColor, const TPixel32 &pointColor,
                         const double XPos, const double YPos,
                         const double Dist, const double Thickness);
  TCircleStripeFillStyle(const TPixel32 &color);

  TColorStyle *clone() const override;

  /*
TPixel32 getMainColor() const {return m_pointColor; }
void setMainColor(const TPixel32 &color){ m_pointColor=color; }
*/

  int getColorParamCount() const override { return 2; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

  int getTagId() const override { return 1140; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText(
        "TCircleStripeFillStyle",
        QT_TRANSLATE_NOOP("TCircleStripeFillStyle", "Concentric"));
  }
  std::string getBrushIdName() const override {
    return "CircleStripeFillStyle";
  }

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;

private:
  void getCircleStripeQuads(const TPointD &center, const double r1,
                            const double r2, std::vector<TPointD> &pv) const;
  void drawCircleStripe(TCanvas &canvas, const TPointD &center,
                        const double r1, const double r2,
                        const TPixel32 &col) const;
};

//============================================================

class DVAPI TMosaicFillStyle final : public TSolidColorStyle {
  TPixel32 m_pointColor[4];
  double m_size;
  double m_deform;
  double m_minThickness;
  double m_maxThickness;

public:
  TMosaicFillStyle(const TPixel32 &bgColor, const TPixel32 pointColor[4],
                   const double size, const double deform,
                   const double minThickness, const double maxThickness);
  TMosaicFillStyle(const TPixel32 bgColor);

  TColorStyle *clone() const override;

  /*
TPixel32 getMainColor() const {return m_pointColor[0]; }
void setMainColor(const TPixel32 &color){ m_pointColor[0]=color; }
*/

  int getColorParamCount() const override { return 5; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

  int getTagId() const override { return 1141; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText(
        "TMosaicFillStyle",
        QT_TRANSLATE_NOOP("TMosaicFillStyle", "Stained Glass"));
  }
  std::string getBrushIdName() const override { return "MosaicFillStyle"; }

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;

private:
  void preaprePos(const TRectD &box, std::vector<TPointD> &v, int &lX, int &lY,
                  TRandom &rand) const;
  bool getQuad(const int ix, const int iy, const int lX, const int lY,
               std::vector<TPointD> &v, TPointD *pquad, TRandom &rand) const;
};

//============================================================

class DVAPI TPatchFillStyle final : public TSolidColorStyle {
  TPixel32 m_pointColor[6];
  double m_size;
  double m_deform;
  double m_thickness;

public:
  TPatchFillStyle(const TPixel32 &bgColor, const TPixel32 pointColor[6],
                  const double size, const double deform,
                  const double thickness);
  TPatchFillStyle(const TPixel32 &bgColor);

  TColorStyle *clone() const override;

  int getColorParamCount() const override { return 7; }
  TPixel32 getColorParamValue(int index) const override;
  void setColorParamValue(int index, const TPixel32 &color) override;

  bool isRegionStyle() const override { return true; }
  bool isStrokeStyle() const override { return false; }

  int getParamCount() const override;
  TColorStyle::ParamType getParamType(int index) const override;

  TTranslatableText getParamNameText(int index) const override;
  void getParamRange(int index, double &min, double &max) const override;
  double getParamValue(TColorStyle::double_tag, int index) const override;
  void setParamValue(int index, double value) override;

  void drawRegion(const TColorFunction *cf, const bool antiAliasing,
                  TRegionOutline &boundary) const override;
  //! On a canvas; drawRegion() is this seen through the OpenGL canvas.
  int canvasSupport() const override { return RegionsOnCanvas; }
  void drawRegionOnCanvas(TCanvas &canvas, const TColorFunction *cf,
                          bool antiAliasing,
                          TRegionOutline &outline) const override;

  int getTagId() const override { return 1142; };
  TTranslatableText getDescriptionText() const override {
    return TTranslatableText("TPatchFillStyle",
                             QT_TRANSLATE_NOOP("TPatchFillStyle", "Beehive"));
  }
  std::string getBrushIdName() const override { return "PatchFillStyle"; }

private:
  void preaprePos(const TRectD &box, std::vector<TPointD> &v, int &lX, int &lY,
                  TRandom &rand) const;
  bool getQuadLine(const TPointD &a, const TPointD &b, const double thickn,
                   TPointD *quad) const;
  int nbClip(const int lX, const int lY, const std::vector<TPointD> &v) const;

protected:
  void loadData(TInputStreamInterface &is) override;
  void saveData(TOutputStreamInterface &os) const override;
};

#endif  // TDERIVEDREGIONSTYLES_H
