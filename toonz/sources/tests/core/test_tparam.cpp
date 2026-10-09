// Unit tests for the parameter classes in tnzbase (include/tdoubleparam.h,
// tparamset.h, tnotanimatableparam.h, tparamcontainer.h, tfxparam.h).
//
// Every animatable value in a scene (pegbar and camera channels, every fx
// parameter) is a TDoubleParam or a set of them, so the interpolation rules
// pinned down here decide what a render looks like between keyframes.
// Frames passed to the API are 0-based (the UI shows frame + 1).

#include <gtest/gtest.h>

#include "tdoublekeyframe.h"
#include "tdoubleparam.h"
#include "texception.h"
#include "tfx.h"
#include "tfxparam.h"
#include "tnotanimatableparam.h"
#include "tparamcontainer.h"
#include "tparamset.h"
#include "tstream.h"

#include <QTemporaryDir>

#include <cmath>
#include <set>
#include <string>

namespace {

constexpr double kEps = 1e-9;

class ChangeCounter final : public TParamObserver {
public:
  int m_count = 0;
  void onChange(const TParamChange &) override { ++m_count; }
};

// Replaces the interpolation of the segment starting at keyframe `index`.
void setSegmentType(TDoubleParam &p, int index, TDoubleKeyframe::Type type) {
  TDoubleKeyframe k = p.getKeyframe(index);
  k.m_type          = type;
  p.setKeyframe(index, k);
}

}  // namespace

//-----------------------------------------------------------------------------
// TDoubleParam: default value and keyframe bookkeeping

TEST(TDoubleParam, DefaultValueWithoutKeyframes) {
  TDoubleParam p(3.5);
  EXPECT_EQ(p.getDefaultValue(), 3.5);
  EXPECT_EQ(p.getValue(0), 3.5);
  EXPECT_EQ(p.getValue(-100), 3.5);
  EXPECT_EQ(p.getValue(1000), 3.5);
  EXPECT_EQ(p.getKeyframeCount(), 0);
  EXPECT_FALSE(p.hasKeyframes());
  EXPECT_TRUE(p.isAnimatable());

  p.setDefaultValue(-2.0);
  EXPECT_EQ(p.getValue(7), -2.0);
}

TEST(TDoubleParam, IsDefaultMeansZeroAndNoKeyframes) {
  // isDefault() does not compare with the value the param was constructed
  // with: it is true only for "no keyframes and default value == 0".
  EXPECT_TRUE(TDoubleParam().isDefault());
  EXPECT_FALSE(TDoubleParam(3.5).isDefault());

  TDoubleParam p;
  p.setValue(0, 0.0);
  EXPECT_FALSE(p.isDefault());
}

TEST(TDoubleParam, SetValueCreatesKeyframes) {
  TDoubleParam p(1.0);

  EXPECT_TRUE(p.setValue(0, 10.0));  // true: a keyframe was created
  EXPECT_EQ(p.getKeyframeCount(), 1);
  EXPECT_FALSE(p.setValue(0, 20.0));  // false: existing keyframe changed
  EXPECT_EQ(p.getKeyframeCount(), 1);

  // A single keyframe holds its value everywhere.
  EXPECT_EQ(p.getValue(-5), 20.0);
  EXPECT_EQ(p.getValue(50), 20.0);
  // setValue never touches the default value.
  EXPECT_EQ(p.getDefaultValue(), 1.0);

  EXPECT_TRUE(p.setValue(10, 30.0));
  EXPECT_EQ(p.getKeyframeCount(), 2);
  EXPECT_TRUE(p.isKeyframe(0));
  EXPECT_TRUE(p.isKeyframe(10));
  EXPECT_FALSE(p.isKeyframe(5));
  EXPECT_EQ(p.keyframeIndexToFrame(0), 0.0);
  EXPECT_EQ(p.keyframeIndexToFrame(1), 10.0);

  const TDoubleKeyframe &k0 = p.getKeyframe(0);
  EXPECT_EQ(k0.m_frame, 0.0);
  EXPECT_EQ(k0.m_value, 20.0);
  EXPECT_TRUE(k0.m_isKeyframe);
}

TEST(TDoubleParam, KeyframesStaySortedWhenInsertedOutOfOrder) {
  TDoubleParam p;
  p.setValue(20, 2.0);
  p.setValue(0, 0.0);
  p.setValue(10, 1.0);
  ASSERT_EQ(p.getKeyframeCount(), 3);
  EXPECT_EQ(p.keyframeIndexToFrame(0), 0.0);
  EXPECT_EQ(p.keyframeIndexToFrame(1), 10.0);
  EXPECT_EQ(p.keyframeIndexToFrame(2), 20.0);

  std::set<double> frames;
  p.getKeyframes(frames);
  EXPECT_EQ(frames, (std::set<double>{0.0, 10.0, 20.0}));
}

//-----------------------------------------------------------------------------
// TDoubleParam: interpolation

TEST(TDoubleParam, DefaultInterpolationIsLinear) {
  TDoubleParam p;
  p.setValue(0, 0.0);
  p.setValue(10, 100.0);

  // Keyframes created by setValue() are TDoubleKeyframe::Linear segments.
  EXPECT_EQ(p.getKeyframe(0).m_type, TDoubleKeyframe::Linear);
  EXPECT_EQ(p.getKeyframe(1).m_type, TDoubleKeyframe::Linear);
  EXPECT_EQ(p.getKeyframe(0).m_prevType, TDoubleKeyframe::None);
  EXPECT_EQ(p.getKeyframe(1).m_prevType, TDoubleKeyframe::Linear);
  EXPECT_EQ(p.getKeyframe(0).m_step, 1);

  EXPECT_EQ(p.getValue(0), 0.0);
  EXPECT_NEAR(p.getValue(2.5), 25.0, kEps);
  EXPECT_NEAR(p.getValue(5), 50.0, kEps);
  EXPECT_EQ(p.getValue(10), 100.0);

  // Outside the keyframe range the first/last value is held.
  EXPECT_EQ(p.getValue(-10), 0.0);
  EXPECT_EQ(p.getValue(25), 100.0);
}

TEST(TDoubleParam, KeyframeInsertedInsideASegmentSplitsIt) {
  TDoubleParam p;
  p.setValue(0, 0.0);
  p.setValue(10, 100.0);
  p.setValue(4, 0.0);
  ASSERT_EQ(p.getKeyframeCount(), 3);
  EXPECT_EQ(p.keyframeIndexToFrame(1), 4.0);

  EXPECT_EQ(p.getValue(2), 0.0);
  EXPECT_NEAR(p.getValue(7), 50.0, kEps);  // (7-4)/(10-4) of the way to 100
}

TEST(TDoubleParam, DeleteKeyframe) {
  TDoubleParam p(5.0);
  p.setValue(0, 0.0);
  p.setValue(4, 0.0);
  p.setValue(10, 100.0);

  p.deleteKeyframe(4);
  EXPECT_EQ(p.getKeyframeCount(), 2);
  EXPECT_FALSE(p.isKeyframe(4));
  EXPECT_NEAR(p.getValue(7), 70.0, kEps);

  p.deleteKeyframe(3);  // not a keyframe: no-op
  EXPECT_EQ(p.getKeyframeCount(), 2);

  p.clearKeyframes();
  EXPECT_EQ(p.getKeyframeCount(), 0);
  EXPECT_EQ(p.getValue(7), 5.0);  // back to the default value
}

TEST(TDoubleParam, NeighbourKeyframeQueries) {
  TDoubleParam p;
  p.setValue(0, 0.0);
  p.setValue(10, 1.0);
  p.setValue(20, 2.0);

  EXPECT_EQ(p.getNextKeyframe(5), 1);
  EXPECT_EQ(p.getNextKeyframe(10), 2);
  EXPECT_EQ(p.getNextKeyframe(20), -1);
  EXPECT_EQ(p.getPrevKeyframe(5), 0);
  EXPECT_EQ(p.getPrevKeyframe(10), 0);
  EXPECT_EQ(p.getPrevKeyframe(0), -1);
  EXPECT_EQ(p.getPrevKeyframe(25), 2);
  EXPECT_EQ(p.getClosestKeyframe(4), 0);
  EXPECT_EQ(p.getClosestKeyframe(6), 1);
  EXPECT_EQ(p.getClosestKeyframe(10), 1);
  EXPECT_EQ(p.getClosestKeyframe(99), 2);
}

TEST(TDoubleParam, ConstantInterpolationHoldsUntilNextKeyframe) {
  TDoubleParam p;
  p.setValue(0, 0.0);
  p.setValue(10, 100.0);
  setSegmentType(p, 0, TDoubleKeyframe::Constant);
  EXPECT_EQ(p.getKeyframe(1).m_prevType, TDoubleKeyframe::Constant);

  EXPECT_EQ(p.getValue(0), 0.0);
  EXPECT_EQ(p.getValue(9.99), 0.0);
  EXPECT_EQ(p.getValue(10), 100.0);
}

TEST(TDoubleParam, ExponentialInterpolationIsGeometric) {
  TDoubleParam p;
  p.setValue(0, 1.0);
  p.setValue(10, 100.0);
  setSegmentType(p, 0, TDoubleKeyframe::Exponential);
  EXPECT_NEAR(p.getValue(5), 10.0, 1e-9);  // sqrt(1 * 100)
  EXPECT_NEAR(p.getValue(2.5), std::sqrt(10.0), 1e-9);

  // With a non-positive end value it silently falls back to linear.
  TDoubleParam q;
  q.setValue(0, 0.0);
  q.setValue(10, 100.0);
  setSegmentType(q, 0, TDoubleKeyframe::Exponential);
  EXPECT_NEAR(q.getValue(5), 50.0, kEps);
}

TEST(TDoubleParam, EaseInOutInterpolation) {
  TDoubleParam p;
  p.setValue(0, 0.0);
  p.setValue(10, 10.0);

  // EaseInOut with zero-length handles is exactly linear.
  setSegmentType(p, 0, TDoubleKeyframe::EaseInOut);
  EXPECT_NEAR(p.getValue(2.5), 2.5, kEps);

  // Ease over the whole segment (5 frames in, 5 frames out): a pair of
  // parabolas meeting at the midpoint.
  TDoubleKeyframe k0 = p.getKeyframe(0);
  TDoubleKeyframe k1 = p.getKeyframe(1);
  k0.m_speedOut      = TPointD(5, 0);
  k1.m_speedIn       = TPointD(-5, 0);
  p.setKeyframe(0, k0);
  p.setKeyframe(1, k1);
  EXPECT_NEAR(p.getValue(2.5), 1.25, kEps);
  EXPECT_NEAR(p.getValue(5), 5.0, kEps);
  EXPECT_NEAR(p.getValue(7.5), 8.75, kEps);
}

TEST(TDoubleParam, StepHoldsValuesForStepFrames) {
  TDoubleParam p;
  p.setValue(0, 0.0);
  p.setValue(10, 10.0);
  TDoubleKeyframe k0 = p.getKeyframe(0);
  k0.m_step          = 2;
  p.setKeyframe(0, k0);

  EXPECT_EQ(p.getValue(2), 2.0);
  EXPECT_EQ(p.getValue(3), 2.0);
  EXPECT_EQ(p.getValue(3.9), 2.0);
  EXPECT_EQ(p.getValue(4), 4.0);
}

TEST(TDoubleParam, StepIsInheritedByKeyframesInsertedInTheSegment) {
  TDoubleParam p;
  p.setValue(0, 0.0);
  p.setValue(10, 10.0);
  TDoubleKeyframe k0 = p.getKeyframe(0);
  k0.m_step          = 3;
  p.setKeyframe(0, k0);
  p.setValue(5, 5.0);
  EXPECT_EQ(p.getKeyframe(1).m_step, 3);
}

TEST(TDoubleParam, CycleRepeatsTheCurveWithAValueOffset) {
  // Cycling repeats the keyframed range after the last keyframe, and adds
  // (last value - first value) on every repetition: a ramp keeps ramping,
  // a curve that ends where it starts loops in place.
  TDoubleParam ramp;
  ramp.setValue(0, 0.0);
  ramp.setValue(10, 10.0);
  ramp.enableCycle(true);
  EXPECT_TRUE(ramp.isCycleEnabled());
  EXPECT_NEAR(ramp.getValue(15), 15.0, kEps);
  EXPECT_NEAR(ramp.getValue(25), 25.0, kEps);
  EXPECT_EQ(ramp.getValue(-5), 0.0);  // no cycling before the first key

  TDoubleParam loop;
  loop.setValue(0, 0.0);
  loop.setValue(5, 10.0);
  loop.setValue(10, 0.0);
  loop.enableCycle(true);
  EXPECT_NEAR(loop.getValue(12.5), 5.0, kEps);
  EXPECT_NEAR(loop.getValue(15), 10.0, kEps);
  EXPECT_NEAR(loop.getValue(27.5), 5.0, kEps);
}

//-----------------------------------------------------------------------------
// TDoubleParam: copies, observers, persistence

TEST(TDoubleParam, CloneIsIndependent) {
  TDoubleParam p(1.0);
  p.setValue(0, 0.0);
  p.setValue(10, 100.0);

  TDoubleParamP q = static_cast<TDoubleParam *>(p.clone());
  EXPECT_EQ(q->getKeyframeCount(), 2);
  EXPECT_NEAR(q->getValue(5), 50.0, kEps);

  q->setValue(5, 0.0);
  q->setDefaultValue(7.0);
  EXPECT_EQ(q->getKeyframeCount(), 3);
  EXPECT_EQ(p.getKeyframeCount(), 2);
  EXPECT_NEAR(p.getValue(5), 50.0, kEps);
  EXPECT_EQ(p.getDefaultValue(), 1.0);
}

TEST(TDoubleParam, ObserversAreNotifiedOfChanges) {
  TDoubleParam p;
  ChangeCounter counter;
  p.addObserver(&counter);

  p.setValue(0, 1.0);
  EXPECT_EQ(counter.m_count, 1);
  p.setValue(0, 2.0);
  EXPECT_EQ(counter.m_count, 2);
  p.deleteKeyframe(0);
  EXPECT_EQ(counter.m_count, 3);
  p.setDefaultValue(4.0);
  EXPECT_EQ(counter.m_count, 4);
  p.setDefaultValue(4.0);  // unchanged: no notification
  EXPECT_EQ(counter.m_count, 4);

  p.removeObserver(&counter);
  p.setValue(3, 1.0);
  EXPECT_EQ(counter.m_count, 4);
}

TEST(TDoubleParam, SaveLoadRoundTrip) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const TFilePath fp(dir.filePath("param.xml"));

  TDoubleParam src(2.5);
  src.setValue(0, 0.0);
  src.setValue(4, 8.0);
  src.setValue(10, 20.0);
  setSegmentType(src, 1, TDoubleKeyframe::Constant);
  src.enableCycle(true);
  {
    TOStream os(fp);
    os.openChild("param");
    src.saveData(os);
    os.closeChild();
  }

  TDoubleParam dst;
  {
    TIStream is(fp);
    std::string tag;
    ASSERT_TRUE(is.matchTag(tag));
    ASSERT_EQ(tag, "param");
    dst.loadData(is);
    EXPECT_TRUE(is.matchEndTag());
  }
  EXPECT_EQ(dst.getDefaultValue(), 2.5);
  EXPECT_TRUE(dst.isCycleEnabled());
  ASSERT_EQ(dst.getKeyframeCount(), 3);
  EXPECT_EQ(dst.getKeyframe(1).m_type, TDoubleKeyframe::Constant);
  EXPECT_EQ(dst.getKeyframe(2).m_prevType, TDoubleKeyframe::Constant);
  for (double f : {0.0, 2.0, 4.0, 7.0, 10.0, 13.0})
    EXPECT_NEAR(dst.getValue(f), src.getValue(f), kEps) << "frame " << f;
}

//-----------------------------------------------------------------------------
// TPointParam / TRangeParam (sets of TDoubleParams)

TEST(TPointParam, DefaultAndAnimatedValues) {
  TPointParam pp(TPointD(1, 2));
  EXPECT_EQ(pp.getDefaultValue(), TPointD(1, 2));
  EXPECT_EQ(pp.getValue(0), TPointD(1, 2));
  EXPECT_EQ(pp.getParamCount(), 2);
  EXPECT_EQ(pp.getParamName(0), "x");
  EXPECT_EQ(pp.getParamName(1), "y");

  pp.setValue(0, TPointD(0, 0));
  pp.setValue(10, TPointD(10, 20));
  EXPECT_EQ(pp.getKeyframeCount(), 2);  // distinct frames across x and y
  EXPECT_EQ(pp.getX()->getKeyframeCount(), 2);
  EXPECT_EQ(pp.getY()->getKeyframeCount(), 2);
  EXPECT_TRUE(pp.isKeyframe(10));
  EXPECT_NEAR(pp.getValue(5).x, 5.0, kEps);
  EXPECT_NEAR(pp.getValue(5).y, 10.0, kEps);

  // A keyframe on only one component still counts as a keyframe of the set.
  pp.getX()->setValue(3, 0.0);
  EXPECT_EQ(pp.getKeyframeCount(), 3);
  EXPECT_TRUE(pp.isKeyframe(3));
  EXPECT_FALSE(pp.getY()->isKeyframe(3));

  pp.deleteKeyframe(10);
  EXPECT_EQ(pp.getKeyframeCount(), 2);
}

TEST(TPointParam, CopyConstructorDeepCopiesComponents) {
  TPointParam a(TPointD(1, 2));
  a.setValue(0, TPointD(0, 0));
  TPointParam b(a);
  b.setValue(5, TPointD(9, 9));
  b.setDefaultValue(TPointD(7, 7));
  EXPECT_EQ(a.getKeyframeCount(), 1);
  EXPECT_EQ(b.getKeyframeCount(), 2);
  EXPECT_EQ(a.getDefaultValue(), TPointD(1, 2));
  EXPECT_NE(a.getX().getPointer(), b.getX().getPointer());
}

TEST(TRangeParam, DefaultAndAnimatedValues) {
  TRangeParam r(DoublePair(1, 5));
  EXPECT_EQ(r.getDefaultValue(), DoublePair(1, 5));
  EXPECT_EQ(r.getValue(3), DoublePair(1, 5));
  EXPECT_EQ(r.getParamName(0), "min");
  EXPECT_EQ(r.getParamName(1), "max");

  r.setValue(0, DoublePair(0, 10));
  r.setValue(10, DoublePair(10, 20));
  const DoublePair mid = r.getValue(5);
  EXPECT_NEAR(mid.first, 5.0, kEps);
  EXPECT_NEAR(mid.second, 15.0, kEps);
  EXPECT_NEAR(r.getMin()->getValue(2), 2.0, kEps);
  EXPECT_NEAR(r.getMax()->getValue(2), 12.0, kEps);

  // TRangeParam::getKeyframeCount() is declared in tparamset.h
  // but never defined (calling it fails to link); go through TParamSet.
  const TParamSet &set = r;
  EXPECT_EQ(set.getKeyframeCount(), 2);
}

//-----------------------------------------------------------------------------
// Not-animatable params

TEST(TBoolParam, ValueDefaultAndReset) {
  TBoolParam b(true);
  EXPECT_TRUE(b.getValue());
  EXPECT_TRUE(b.getDefaultValue());
  EXPECT_FALSE(b.isAnimatable());
  EXPECT_FALSE(b.isKeyframe(0));
  EXPECT_FALSE(b.hasKeyframes());

  ChangeCounter counter;
  b.addObserver(&counter);
  b.setValue(false);
  EXPECT_FALSE(b.getValue());
  EXPECT_TRUE(b.getDefaultValue());
  EXPECT_EQ(counter.m_count, 1);
  b.setValue(false);  // unchanged: no notification
  EXPECT_EQ(counter.m_count, 1);
  EXPECT_EQ(b.getValueAlias(0, 0), "0");

  b.reset();
  EXPECT_TRUE(b.getValue());
  EXPECT_EQ(counter.m_count, 2);
  EXPECT_EQ(b.getValueAlias(0, 0), "1");
  b.removeObserver(&counter);
}

TEST(TIntParam, RangeIsMetadataOnly) {
  TIntParam i(3);
  int lo = 0, hi = 0;
  i.setValueRange(0, 10);
  EXPECT_TRUE(i.getValueRange(lo, hi));
  EXPECT_EQ(lo, 0);
  EXPECT_EQ(hi, 10);
  // setValue does not clamp to the range.
  i.setValue(42);
  EXPECT_EQ(i.getValue(), 42);
  // Not tested through clone(): TIntParam's copy constructor does not copy
  // the range or the wheel flag, which are left uninitialised in the copy.
}

TEST(TEnumParam, ItemsAndCaptions) {
  TEnumParam e(0, "Linear");
  e.addItem(1, "Gamma");
  e.addItem(5, "Custom");
  EXPECT_EQ(e.getItemCount(), 3);
  EXPECT_EQ(e.getValue(), 0);

  e.setValue(std::string("Custom"));
  EXPECT_EQ(e.getValue(), 5);
  e.setValue(1);
  EXPECT_EQ(e.getValue(), 1);
  int item = -1;
  std::string caption;
  e.getItem(2, item, caption);
  EXPECT_EQ(item, 5);
  EXPECT_EQ(caption, "Custom");

  EXPECT_THROW(e.setValue(3), TException);
  EXPECT_THROW(e.setValue(std::string("Nope")), TException);
}

//-----------------------------------------------------------------------------
// TParamContainer and bindParam (how fxs expose their params)

TEST(TParamContainer, AddGetAndNames) {
  TDoubleParamP amount(1.0);
  TBoolParamP enabled(true);

  TParamContainer c;
  c.add(new TParamVarT<TDoubleParamP>("amount", &amount));
  c.add(new TParamVarT<TBoolParamP>("enabled", &enabled, 0, true));

  ASSERT_EQ(c.getParamCount(), 2);
  EXPECT_EQ(c.getParamName(0), "amount");
  EXPECT_EQ(c.getParam(0), amount.getPointer());
  EXPECT_EQ(c.getParam("enabled"), enabled.getPointer());
  EXPECT_EQ(c.getParam("missing"), nullptr);
  EXPECT_FALSE(c.isParamHidden(0));
  EXPECT_TRUE(c.isParamHidden(1));
  // add() names the param after its variable.
  EXPECT_EQ(amount->getName(), "amount");
  EXPECT_EQ(enabled->getName(), "enabled");
}

TEST(TParamContainer, UnlinkRebindsTheVariableToAClone) {
  TDoubleParamP amount(1.0);
  amount->setValue(0, 4.0);
  TParam *const original = amount.getPointer();
  TParamP keepAlive(original);

  TParamContainer c;
  c.add(new TParamVarT<TDoubleParamP>("amount", &amount));
  c.unlink();

  // The member variable itself now points to a fresh copy.
  EXPECT_NE(amount.getPointer(), original);
  EXPECT_EQ(c.getParam(0), amount.getPointer());
  EXPECT_EQ(amount->getValue(0), 4.0);
  amount->setValue(0, 9.0);
  EXPECT_EQ(static_cast<TDoubleParam *>(original)->getValue(0), 4.0);
}

namespace {

class ParamHostFx final : public TFx {
  FX_DECLARATION(ParamHostFx)

public:
  TDoubleParamP m_radius;
  TPointParamP m_center;
  TBoolParamP m_enabled;
  TRangeParamP m_range;

  ParamHostFx()
      : m_radius(5.0)
      , m_center(TPointD(1, 2))
      , m_enabled(true)
      , m_range(DoublePair(0, 1)) {
    bindParam(this, "radius", m_radius);
    bindParam(this, "center", m_center);
    bindParam(this, "enabled", m_enabled, true);
    bindParam(this, "range", m_range);
  }

  std::string getPluginId() const override { return "nexttoonzTest"; }
};

class FxChangeCounter final : public TFxObserver {
public:
  int m_count = 0;
  void onChange(const TFxChange &) override { ++m_count; }
};

}  // namespace

FX_IDENTIFIER(ParamHostFx, "nexttoonzTestParamHostFx")

TEST(BindParam, RegistersParamsInOrder) {
  TFxP holder(new ParamHostFx);
  ParamHostFx *fx = static_cast<ParamHostFx *>(holder.getPointer());

  EXPECT_EQ(fx->getFxType(), "nexttoonzTestParamHostFx");
  const TParamContainer *params = fx->getParams();
  ASSERT_EQ(params->getParamCount(), 4);
  EXPECT_EQ(params->getParamName(0), "radius");
  EXPECT_EQ(params->getParamName(1), "center");
  EXPECT_EQ(params->getParamName(2), "enabled");
  EXPECT_EQ(params->getParamName(3), "range");
  EXPECT_TRUE(params->isParamHidden(2));
  EXPECT_FALSE(params->isParamHidden(0));
  EXPECT_EQ(params->getParam("center"), fx->m_center.getPointer());
  EXPECT_EQ(fx->m_radius->getName(), "radius");
}

TEST(BindParam, ParamChangesReachFxObservers) {
  TFxP holder(new ParamHostFx);
  ParamHostFx *fx = static_cast<ParamHostFx *>(holder.getPointer());
  FxChangeCounter counter;
  fx->addObserver(&counter);

  fx->m_radius->setValue(0, 2.0);
  EXPECT_EQ(counter.m_count, 1);
  fx->m_enabled->setValue(false);
  EXPECT_EQ(counter.m_count, 2);

  fx->removeObserver(&counter);
}

TEST(BindParam, CloneCopiesParamValuesIntoIndependentParams) {
  TFxP holder(new ParamHostFx);
  ParamHostFx *fx = static_cast<ParamHostFx *>(holder.getPointer());
  fx->m_radius->setValue(0, 1.0);
  fx->m_radius->setValue(10, 11.0);
  fx->m_enabled->setValue(false);

  TFxP cloneHolder(fx->clone(false));
  auto *copy = dynamic_cast<ParamHostFx *>(cloneHolder.getPointer());
  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy->m_radius.getPointer(), fx->m_radius.getPointer());
  EXPECT_EQ(copy->m_radius->getKeyframeCount(), 2);
  EXPECT_NEAR(copy->m_radius->getValue(5), 6.0, kEps);
  EXPECT_FALSE(copy->m_enabled->getValue());

  copy->m_radius->setValue(5, 0.0);
  EXPECT_EQ(fx->m_radius->getKeyframeCount(), 2);
}

TEST(BindParam, LinkParamsSharesTheParamObjects) {
  TFxP aHolder(new ParamHostFx), bHolder(new ParamHostFx);
  auto *a = static_cast<ParamHostFx *>(aHolder.getPointer());
  auto *b = static_cast<ParamHostFx *>(bHolder.getPointer());

  b->linkParams(a);
  EXPECT_EQ(b->m_radius.getPointer(), a->m_radius.getPointer());
  EXPECT_EQ(b->getLinkedFx(), a);
  a->m_radius->setValue(0, 42.0);
  EXPECT_EQ(b->m_radius->getValue(0), 42.0);

  b->unlinkParams();
  EXPECT_NE(b->m_radius.getPointer(), a->m_radius.getPointer());
  EXPECT_EQ(b->getLinkedFx(), b);
  EXPECT_EQ(b->m_radius->getValue(0), 42.0);  // the clone keeps the values
}
