// Unit tests for TUnit / TMeasure / TMeasuredValue (tnzbase, include/tunit.h).
//
// Measures convert between the internal units stored in scenes (inches for
// lengths, "stage inches" for fx lengths) and the units shown to the user.
// The standard measures live in the TMeasureManager singleton, which needs
// neither the Preferences singleton nor a QApplication; the application only
// changes their *current* unit at startup. These tests read the defaults and
// never mutate the shared measures.

#include <gtest/gtest.h>

#include "tunit.h"

#include <memory>
#include <string>

namespace {

constexpr double kEps = 1e-9;

const TMeasure *measure(const std::string &name) {
  const TMeasure *m = TMeasureManager::instance()->get(name);
  EXPECT_NE(m, nullptr) << name;
  return m;
}

const TUnit *unit(const std::string &measureName, const std::wstring &ext) {
  const TMeasure *m = measure(measureName);
  if (!m) return nullptr;
  const TUnit *u = m->getUnit(ext);
  EXPECT_NE(u, nullptr) << measureName;
  return u;
}

}  // namespace

//-----------------------------------------------------------------------------
// TUnit and TMeasure as plain objects

TEST(TUnit, SimpleConverter) {
  // convertTo() goes from the measure's main unit to this unit,
  // convertFrom() back: value * factor + offset.
  TUnit cm(L"cm", new TSimpleUnitConverter(2.54));
  EXPECT_NEAR(cm.convertTo(1.0), 2.54, kEps);
  EXPECT_NEAR(cm.convertFrom(5.08), 2.0, kEps);

  TUnit fahrenheit(L"F", new TSimpleUnitConverter(1.8, 32));
  EXPECT_NEAR(fahrenheit.convertTo(100.0), 212.0, kEps);
  EXPECT_NEAR(fahrenheit.convertFrom(32.0), 0.0, kEps);

  TUnit identity(L"u");  // no converter: identity
  EXPECT_EQ(identity.convertTo(3.5), 3.5);
  EXPECT_EQ(identity.convertFrom(3.5), 3.5);
}

TEST(TUnit, Extensions) {
  TUnit inch(L"in");
  EXPECT_EQ(inch.getDefaultExtension(), L"in");
  inch.addExtension(L"inch");
  inch.addExtension(L"inch");  // duplicates are ignored
  EXPECT_EQ(inch.getExtensions().size(), 2u);
  EXPECT_TRUE(inch.isExtension(L"inch"));
  EXPECT_FALSE(inch.isExtension(L"cm"));

  // setDefaultExtension() also registers the extension.
  inch.setDefaultExtension(L"\"");
  EXPECT_EQ(inch.getDefaultExtension(), L"\"");
  EXPECT_TRUE(inch.isExtension(L"\""));
  EXPECT_EQ(inch.getExtensions().size(), 3u);

  std::unique_ptr<TUnit> copy(inch.clone());
  EXPECT_EQ(copy->getDefaultExtension(), L"\"");
  EXPECT_TRUE(copy->isExtension(L"inch"));
}

TEST(TMeasure, UnitLookupAndCurrentUnit) {
  // TMeasure does not own its units.
  std::unique_ptr<TUnit> inch(new TUnit(L"in"));
  std::unique_ptr<TUnit> mm(new TUnit(L"mm", new TSimpleUnitConverter(25.4)));
  inch->addExtension(L"inch");

  TMeasure m("myLength", inch.get());
  m.add(mm.get());
  EXPECT_EQ(m.getName(), "myLength");
  EXPECT_EQ(m.getMainUnit(), inch.get());
  EXPECT_EQ(m.getCurrentUnit(), inch.get());
  EXPECT_EQ(m.getStandardUnit(), inch.get());
  EXPECT_EQ(m.getUnit(L"inch"), inch.get());
  EXPECT_EQ(m.getUnit(L"mm"), mm.get());
  EXPECT_EQ(m.getUnit(L"cm"), nullptr);

  m.setCurrentUnit(mm.get());
  EXPECT_EQ(m.getCurrentUnit(), mm.get());
  EXPECT_EQ(m.getMainUnit(), inch.get());
}

//-----------------------------------------------------------------------------
// The standard measures

TEST(TMeasureManager, LengthIsStoredInInches) {
  const TMeasure *length = measure("length");
  ASSERT_NE(length, nullptr);
  EXPECT_EQ(length->getMainUnit()->getDefaultExtension(), L"\"");
  EXPECT_TRUE(length->getMainUnit()->isExtension(L"in"));
  EXPECT_TRUE(length->getMainUnit()->isExtension(L"inch"));
  // The default current unit is mm (so tcomposer matches a default UI).
  EXPECT_EQ(length->getCurrentUnit()->getDefaultExtension(), L"mm");

  EXPECT_NEAR(unit("length", L"mm")->convertTo(1.0), 25.4, kEps);
  EXPECT_NEAR(unit("length", L"cm")->convertTo(2.0), 5.08, kEps);
  EXPECT_NEAR(unit("length", L"mm")->convertFrom(127.0), 5.0, kEps);
  EXPECT_NEAR(unit("length", L"in")->convertTo(3.0), 3.0, kEps);

  EXPECT_EQ(TMeasureManager::instance()->get("no.such.measure"), nullptr);
}

TEST(TMeasureManager, FieldAndPixelUnits) {
  // One horizontal field is half an inch; vertical fields also scale by the
  // field-guide aspect ratio (1.38 unless the scene sets another one).
  EXPECT_NEAR(unit("length.x", L"fld")->convertTo(1.0), 2.0, kEps);
  EXPECT_NEAR(unit("length.x", L"field")->convertFrom(1.0), 0.5, kEps);
  EXPECT_NEAR(UnitParameters::getFieldGuideAspectRatio(), 1.38, kEps);
  EXPECT_NEAR(unit("length.y", L"fld")->convertTo(1.0), 2.76, kEps);
  EXPECT_NEAR(unit("camera.lx", L"fld")->convertTo(1.0), 1.0, kEps);

  // Pixels use the current-dpi callback, 72 dpi until the application
  // installs one (toonzlib does, from the current camera).
  EXPECT_NEAR(unit("length.x", L"px")->convertTo(1.0), 72.0, kEps);
  EXPECT_NEAR(unit("length.y", L"pixel")->convertFrom(36.0), 0.5, kEps);
}

TEST(TMeasureManager, FxLengthUsesStageInches) {
  // Fx lengths are stored in "stage inches": 1 inch == 53.33333 units.
  const TMeasure *fx = measure("fxLength");
  ASSERT_NE(fx, nullptr);
  EXPECT_EQ(fx->getMainUnit()->getDefaultExtension(), L"fxLength");
  EXPECT_EQ(fx->getCurrentUnit(), fx->getMainUnit());
  EXPECT_NEAR(unit("fxLength", L"in")->convertFrom(1.0), 53.33333, 1e-9);
  EXPECT_NEAR(unit("fxLength", L"mm")->convertFrom(25.4), 53.33333, 1e-9);
  EXPECT_NEAR(unit("fxLength", L"px")->convertTo(53.33333), 120.0, 1e-9);
  EXPECT_NEAR(unit("fxLength", L"fld")->convertFrom(1.0), 53.33333 / 2, 1e-9);
}

TEST(TMeasureManager, ScaleShearAndColorChannel) {
  // Scale is stored as a factor and shown as a percentage.
  const TMeasure *scale = measure("scale");
  ASSERT_NE(scale, nullptr);
  EXPECT_NEAR(scale->getCurrentUnit()->convertTo(0.5), 50.0, kEps);
  EXPECT_EQ(scale->getCurrentUnit()->getDefaultExtension(), L"%");

  // Shear is stored as a tangent and shown as an angle in degrees.
  const TMeasure *shear = measure("shear");
  ASSERT_NE(shear, nullptr);
  EXPECT_NEAR(shear->getCurrentUnit()->convertTo(1.0), 45.0, kEps);
  EXPECT_NEAR(shear->getCurrentUnit()->convertFrom(45.0), 1.0, kEps);

  // Color channels are stored in [0, 1] and shown in [0, 255].
  const TMeasure *channel = measure("colorChannel");
  ASSERT_NE(channel, nullptr);
  EXPECT_NEAR(channel->getCurrentUnit()->convertTo(1.0), 255.0, kEps);

  const TMeasure *angle = measure("angle");
  ASSERT_NE(angle, nullptr);
  EXPECT_EQ(angle->getMainUnit()->getDefaultExtension(), L"\u00b0");
  EXPECT_EQ(angle->getMainUnit()->convertTo(90.0), 90.0);
}

//-----------------------------------------------------------------------------
// TMeasuredValue: parsing and formatting user input

TEST(TMeasuredValue, ParsesNumbersWithUnits) {
  TMeasuredValue v("length");
  int err = 99;

  EXPECT_TRUE(v.setValue(L"10 cm", &err));
  EXPECT_EQ(err, 0);
  EXPECT_NEAR(v.getValue(TMeasuredValue::MainUnit), 10.0 / 2.54, kEps);
  EXPECT_NEAR(v.getValue(TMeasuredValue::CurrentUnit), 100.0, kEps);
  EXPECT_EQ(v.toWideString(), L"100 mm");

  EXPECT_TRUE(v.setValue(L"2in"));
  EXPECT_NEAR(v.getValue(TMeasuredValue::MainUnit), 2.0, kEps);
  EXPECT_EQ(v.toWideString(), L"50.8 mm");

  // No unit: the current unit (mm) is assumed.
  EXPECT_TRUE(v.setValue(L"  12.7  "));
  EXPECT_NEAR(v.getValue(TMeasuredValue::MainUnit), 0.5, kEps);

  EXPECT_TRUE(v.setValue(L"-2.5e1 mm"));
  EXPECT_NEAR(v.getValue(TMeasuredValue::CurrentUnit), -25.0, kEps);
}

TEST(TMeasuredValue, RejectsBadInput) {
  TMeasuredValue v("length");
  v.setValue(TMeasuredValue::MainUnit, 1.0);
  int err = 0;

  EXPECT_FALSE(v.setValue(L"", &err));
  EXPECT_EQ(err, -1);
  EXPECT_FALSE(v.setValue(L"-", &err));
  EXPECT_EQ(err, -1);
  EXPECT_FALSE(v.setValue(L"5 parsecs", &err));
  EXPECT_EQ(err, -2);

  // An exponent is only recognised after a decimal point: "1e1 mm" parses
  // as the number 1 followed by the unknown unit "e1 mm".
  EXPECT_FALSE(v.setValue(L"1e1 mm", &err));
  EXPECT_EQ(err, -2);
  EXPECT_TRUE(v.setValue(L"1.0e1 mm", &err));
  EXPECT_NEAR(v.getValue(TMeasuredValue::CurrentUnit), 10.0, kEps);

  // Setting the value it already has returns false with no error.
  EXPECT_FALSE(v.setValue(L"10 mm", &err));
  EXPECT_EQ(err, 0);
}

TEST(TMeasuredValue, FormattingTrimsZeros) {
  TMeasuredValue v("length");
  v.setValue(TMeasuredValue::CurrentUnit, 12.6);
  EXPECT_EQ(v.toWideString(), L"12.6 mm");  // 7 decimals, zeros trimmed
  EXPECT_EQ(v.toWideString(0), L"13 mm");   // rounded to 0 decimals
  v.setValue(TMeasuredValue::CurrentUnit, 3.0);
  EXPECT_EQ(v.toWideString(), L"3 mm");

  TMeasuredValue c("colorChannel");
  c.setValue(TMeasuredValue::MainUnit, 0.5);
  EXPECT_EQ(c.toWideString(), L"127.5");  // unit with no extension

  TMeasuredValue s("scale");
  s.setValue(TMeasuredValue::MainUnit, 1.25);
  EXPECT_EQ(s.toWideString(), L"125 %");
}

TEST(TMeasuredValue, ModifyValueStepsInTheCurrentUnit) {
  TMeasuredValue v("length");
  v.setValue(TMeasuredValue::CurrentUnit, 10.0);
  v.modifyValue(1.0);
  EXPECT_NEAR(v.getValue(TMeasuredValue::CurrentUnit), 11.0, kEps);
  v.modifyValue(-1.0, 1);  // precision 1: steps of 0.1
  EXPECT_NEAR(v.getValue(TMeasuredValue::CurrentUnit), 10.9, kEps);
}
