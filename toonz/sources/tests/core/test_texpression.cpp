// Unit tests for the expression engine in tnzbase (include/texpression.h,
// tgrammar.h, tparser.h; sources in common/expressions/).
//
// Expressions drive any animated channel whose keyframe segment is of type
// TDoubleKeyframe::Expression. The default TSyntax::Grammar is what the
// application uses (the xsheet extends it with references such as
// "table.ns"), so the operator priorities and function definitions pinned
// down here are part of the scene format.

#include <gtest/gtest.h>

#include "tdoublekeyframe.h"
#include "tdoubleparam.h"
#include "texpression.h"
#include "tgrammar.h"
#include "tparser.h"
#include "tstream.h"

#include <QTemporaryDir>

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kEps = 1e-9;

const TSyntax::Grammar &grammar() {
  static const TSyntax::Grammar g;
  return g;
}

// Parses `text` with the default grammar and evaluates it with the given
// calculator variables: `frame`/`f`, `t` and `rframe`/`r`.
double eval(const std::string &text, double frame = 1.0, double t = 0.0,
            double rframe = 1.0) {
  TExpression e;
  e.setGrammar(&grammar());
  e.setText(text);
  EXPECT_TRUE(e.isValid()) << "'" << text << "': " << e.getError();
  TSyntax::Calculator *calc = e.getCalculator();
  if (!calc) return std::numeric_limits<double>::quiet_NaN();
  return calc->compute(t, frame, rframe);
}

// Two keyframes at `f0` and `f1` with an expression segment between them.
void makeExpressionSegment(TDoubleParam &p, double f0, double f1,
                           const std::string &text) {
  p.setGrammar(&grammar());
  p.setValue(f0, 0.0);
  p.setValue(f1, 0.0);
  TDoubleKeyframe k  = p.getKeyframeAt(f0);
  k.m_type           = TDoubleKeyframe::Expression;
  k.m_expressionText = text;
  p.setKeyframe(k);
}

}  // namespace

//-----------------------------------------------------------------------------
// Parsing and evaluation

TEST(TExpression, Constants) {
  EXPECT_EQ(eval("42"), 42.0);
  EXPECT_EQ(eval("0.5"), 0.5);
  EXPECT_EQ(eval(".25"), 0.25);
  EXPECT_EQ(eval("1.5e2"), 150.0);
  EXPECT_EQ(eval("2E-1"), 0.2);
  EXPECT_NEAR(eval("pi"), std::acos(-1.0), kEps);
}

TEST(TExpression, Arithmetic) {
  EXPECT_EQ(eval("1 + 2 * 3"), 7.0);
  EXPECT_EQ(eval("(1 + 2) * 3"), 9.0);
  EXPECT_EQ(eval("10 - 3 - 2"), 5.0);  // left associative
  EXPECT_EQ(eval("12 / 3 / 2"), 2.0);
  EXPECT_EQ(eval("7 / 2"), 3.5);  // always floating point
  EXPECT_EQ(eval("-3 + 1"), -2.0);
  EXPECT_EQ(eval("2 * -3"), -6.0);
  EXPECT_EQ(eval("2 ^ 10"), 1024.0);
}

TEST(TExpression, OperatorPriorityQuirks) {
  // '^' is left associative: 2^3^2 == (2^3)^2.
  EXPECT_EQ(eval("2 ^ 3 ^ 2"), 64.0);
  // Unary minus binds tighter than '^': -2^2 == (-2)^2.
  EXPECT_EQ(eval("-2 ^ 2"), 4.0);
  // '%' has a LOWER priority than '+' and '-' (8 vs 10), unlike C:
  // 10 - 7 % 4 == (10 - 7) % 4.
  EXPECT_EQ(eval("10 - 7 % 4"), 3.0);
  // '%' is a floored modulo on doubles, and x % 0 is 0.
  EXPECT_EQ(eval("7 % 3"), 1.0);
  EXPECT_EQ(eval("-7 % 3"), 2.0);
  EXPECT_EQ(eval("7.5 % 2"), 1.5);
  EXPECT_EQ(eval("5 % 0"), 0.0);
}

TEST(TExpression, ComparisonsAndLogic) {
  EXPECT_EQ(eval("3 > 2"), 1.0);
  EXPECT_EQ(eval("3 < 2"), 0.0);
  EXPECT_EQ(eval("2 >= 2"), 1.0);
  EXPECT_EQ(eval("2 <= 1"), 0.0);
  EXPECT_EQ(eval("1 + 1 == 2"), 1.0);
  EXPECT_EQ(eval("1 != 1"), 0.0);
  EXPECT_EQ(eval("1 < 2 && 2 < 3"), 1.0);
  EXPECT_EQ(eval("0 || 0"), 0.0);
  EXPECT_EQ(eval("1 and 0"), 0.0);
  EXPECT_EQ(eval("0 or 2"), 1.0);
  EXPECT_EQ(eval("!0"), 1.0);
  EXPECT_EQ(eval("not 3"), 0.0);
}

TEST(TExpression, TernaryOperator) {
  EXPECT_EQ(eval("1 ? 10 : 20"), 10.0);
  EXPECT_EQ(eval("0 ? 10 : 20"), 20.0);
  EXPECT_EQ(eval("(2 > 1) ? 10 : 20"), 10.0);
  EXPECT_EQ(eval("(frame > 5) ? 10 : 20", 7.0), 10.0);
  EXPECT_EQ(eval("1 ? 2 + 3 : 4"), 5.0);

  // BUG (characterised): a binary operator in an unparenthesised condition
  // is reduced only after the middle operand has been parsed, so it takes
  // the wrong operands. "c1 > c2 ? a : b" evaluates as "c1 ? (c2 > a) : b":
  //   2 > 1 ? 10 : 20  ->  2 ? (1 > 10) : 20  ->  0
  //   2 < 1 ? 10 : 20  ->  2 ? (1 < 10) : 20  ->  1
  // (Parser::Imp::parseExpression flushes lower-priority patterns after a
  // "E ? E : E" pattern has consumed its inner expressions.) Parenthesise
  // the condition to get the intended result.
  EXPECT_EQ(eval("2 > 1 ? 10 : 20"), 0.0);
  EXPECT_EQ(eval("2 < 1 ? 10 : 20"), 1.0);
  EXPECT_EQ(eval("frame > 5 ? 10 : 20", 7.0), 0.0);
}

TEST(TExpression, TrigonometryUsesDegrees) {
  EXPECT_NEAR(eval("sin(90)"), 1.0, kEps);
  EXPECT_NEAR(eval("sin(30)"), 0.5, kEps);
  EXPECT_NEAR(eval("cos(180)"), -1.0, kEps);
  EXPECT_NEAR(eval("tan(45)"), 1.0, kEps);
  EXPECT_NEAR(eval("atan(1)"), 45.0, kEps);
  EXPECT_NEAR(eval("atan2(1, 0)"), 90.0, kEps);  // atan2(y, x)
}

TEST(TExpression, MathFunctions) {
  EXPECT_EQ(eval("floor(2.7)"), 2.0);
  EXPECT_EQ(eval("floor(-2.5)"), -3.0);
  EXPECT_EQ(eval("ceil(2.1)"), 3.0);
  EXPECT_EQ(eval("ceiling(-2.1)"), -2.0);
  EXPECT_EQ(eval("round(2.5)"), 3.0);
  EXPECT_EQ(eval("round(-2.5)"), -3.0);  // half away from zero
  EXPECT_EQ(eval("abs(-3)"), 3.0);
  EXPECT_EQ(eval("sign(-0.1)"), -1.0);
  EXPECT_EQ(eval("sign(0)"), 0.0);
  EXPECT_EQ(eval("sqrt(16)"), 4.0);
  EXPECT_EQ(eval("sqrt(-4)"), 0.0);  // no NaN: negative input gives 0
  // "sqr" is documented in the grammar as "Square root of x" but computes
  // the square.
  EXPECT_EQ(eval("sqr(3)"), 9.0);
  EXPECT_NEAR(eval("exp(1)"), std::exp(1.0), kEps);
  EXPECT_NEAR(eval("log(exp(2))"), 2.0, kEps);
  EXPECT_EQ(eval("min(3, 4)"), 3.0);
  EXPECT_EQ(eval("max(3, 4)"), 4.0);
  EXPECT_EQ(eval("clamp(15, 0, 10)"), 10.0);
  EXPECT_EQ(eval("crop(-1, 0, 10)"), 0.0);
  EXPECT_EQ(eval("step(4, 5)"), 0.0);
  EXPECT_EQ(eval("step(5, 5)"), 1.0);
  EXPECT_EQ(eval("smoothstep(5, 0, 10)"), 0.5);
  EXPECT_EQ(eval("sin(90) + floor(2.5) * 2"), 5.0);
}

TEST(TExpression, FrameVariables) {
  EXPECT_EQ(eval("frame", 7.0), 7.0);
  EXPECT_EQ(eval("f", 7.0), 7.0);
  EXPECT_EQ(eval("frame * 2 + 1", 3.0), 7.0);
  EXPECT_EQ(eval("t", 1.0, 0.25), 0.25);
  EXPECT_EQ(eval("rframe", 10.0, 0.0, 4.0), 4.0);
  EXPECT_EQ(eval("r", 10.0, 0.0, 4.0), 4.0);
}

TEST(TExpression, ImplicitFrameArgument) {
  // Periodic functions take the frame as an implicit first argument;
  // "f(a; b)" passes an explicit one.
  EXPECT_NEAR(eval("saw(10)", 25.0), 5.0, kEps);
  EXPECT_NEAR(eval("saw(10, 2)", 25.0), 1.0, kEps);
  EXPECT_NEAR(eval("saw(7; 10)", 25.0), 7.0, kEps);
  EXPECT_NEAR(eval("wave(8)", 2.0), 1.0, kEps);  // sin(2 * 2pi / 8)
  EXPECT_NEAR(eval("pulse(10)", 10.0), 1.0, kEps);
}

TEST(TExpression, RandomIsDeterministicPerSeedAndFrame) {
  const double a = eval("random_s(7, 0, 10)", 3.0);
  EXPECT_GE(a, 0.0);
  EXPECT_LE(a, 10.0);
  EXPECT_EQ(eval("random_s(7, 0, 10)", 3.0), a);
  EXPECT_EQ(eval("random_s(7, 0, 10)", 3.75), a);  // floor(frame) is used
  EXPECT_NE(eval("random_s(8, 0, 10)", 3.0), a);

  // The sequences come from TRandom, a platform-independent integer
  // generator (values have float precision), so they can be pinned.
  EXPECT_NEAR(a, 4.8556256294250488, 1e-9);
  EXPECT_NEAR(eval("random_s(8, 0, 10)", 3.0), 2.9515528678894043, 1e-9);
  EXPECT_NEAR(eval("random(0, 10)", 3.0), 8.184131383895874, 1e-9);
}

TEST(TExpression, TextAndGrammarAccessors) {
  TExpression e;
  e.setGrammar(&grammar());
  EXPECT_EQ(e.getGrammar(), &grammar());
  e.setText("frame + 1");
  EXPECT_EQ(e.getText(), "frame + 1");
  EXPECT_FALSE(e.isCycling());
  e.setText("cycle(5)");
  EXPECT_TRUE(e.isCycling());

  // Copies share the text and grammar but parse on their own.
  e.setText("2 * frame");
  TExpression copy(e);
  ASSERT_TRUE(copy.isValid());
  EXPECT_EQ(copy.getCalculator()->compute(0, 4, 0), 8.0);
}

//-----------------------------------------------------------------------------
// Errors

TEST(TExpression, InvalidExpressionsReportAnError) {
  struct Case {
    const char *text;
    const char *error;
  } const cases[] = {
      {"", "Expression expected"},      {"1 +", "Expression expected"},
      {"(1 + 2", "Uncompleted syntax"}, {"* 2", "Unexpected token"},
      {"foo + 1", "Unexpected token"},  {"sin(1, 2)", "Syntax error"},
  };
  for (const Case &c : cases) {
    TExpression e;
    e.setGrammar(&grammar());
    e.setText(c.text);
    EXPECT_FALSE(e.isValid()) << "'" << c.text << "'";
    EXPECT_EQ(e.getCalculator(), nullptr) << "'" << c.text << "'";
    EXPECT_EQ(e.getError(), c.error) << "'" << c.text << "'";
  }
}

TEST(TExpression, ErrorPositionPointsAtTheOffendingToken) {
  TExpression e;
  e.setGrammar(&grammar());
  e.setText("1 + foo");
  EXPECT_FALSE(e.isValid());
  EXPECT_EQ(e.getErrorPos(), std::make_pair(4, 6));
}

TEST(TExpression, TrailingTokensAreSilentlyIgnored) {
  // Parser::parse() stops at the first token that cannot continue a
  // complete expression and does not require the end of the text, so these
  // are accepted and evaluate to their valid prefix. (The expression field
  // in the UI uses Parser::checkSyntax(), which does flag the extra text.)
  EXPECT_EQ(eval("3 4"), 3.0);
  EXPECT_EQ(eval("2 + 3 )"), 5.0);

  // checkSyntax() accumulates tokens across calls on the same Parser, so
  // use a fresh one per check.
  auto check = [](const std::string &text) {
    TSyntax::Parser parser(&grammar());
    std::vector<TSyntax::SyntaxToken> tokens;
    return parser.checkSyntax(tokens, text);
  };
  EXPECT_EQ(check("1 + 2"), TSyntax::Parser::Correct);
  EXPECT_EQ(check("3 4"), TSyntax::Parser::Error);
  EXPECT_EQ(check(""), TSyntax::Parser::Incomplete);
  EXPECT_EQ(check("(1 + 2"), TSyntax::Parser::Incomplete);
  // A dangling operator at the end is reported as ExtraIgnored, not as
  // Incomplete.
  EXPECT_EQ(check("1 +"), TSyntax::Parser::ExtraIgnored);
}

TEST(TExpression, NoGrammarIsAnError) {
  TExpression e;
  e.setText("1 + 1");
  EXPECT_FALSE(e.isValid());
  EXPECT_EQ(e.getError(), "No grammar defined");
}

//-----------------------------------------------------------------------------
// Expressions inside a TDoubleParam

TEST(TExpressionInParam, FrameIsOneBased) {
  // The param passes (frame + 1) as `frame`, i.e. the number shown in the
  // xsheet, and (frame - segment start + 1) as `rframe`.
  TDoubleParam p;
  makeExpressionSegment(p, 0, 20, "frame * 10");
  EXPECT_EQ(p.getKeyframe(0).m_type, TDoubleKeyframe::Expression);
  EXPECT_NEAR(p.getValue(0), 10.0, kEps);
  EXPECT_NEAR(p.getValue(4), 50.0, kEps);
  EXPECT_NEAR(p.getValue(4.5), 55.0, kEps);

  TDoubleParam q;
  makeExpressionSegment(q, 10, 20, "rframe");
  EXPECT_NEAR(q.getValue(10), 1.0, kEps);
  EXPECT_NEAR(q.getValue(13), 4.0, kEps);

  TDoubleParam r;
  makeExpressionSegment(r, 10, 20, "t");
  EXPECT_NEAR(r.getValue(15), 0.5, kEps);
}

TEST(TExpressionInParam, ExpressionKeyframesIgnoreSetValue) {
  TDoubleParam p;
  makeExpressionSegment(p, 0, 10, "frame");
  EXPECT_FALSE(p.setValue(0, 99.0));  // existing expression keyframe
  EXPECT_FALSE(p.setValue(5, 99.0));  // inside an expression segment
  EXPECT_EQ(p.getKeyframeCount(), 2);
  EXPECT_NEAR(p.getValue(5), 6.0, kEps);
}

TEST(TExpressionInParam, InvalidOrUngrammaredExpressionsEvaluateToZero) {
  TDoubleParam p(5.0);
  makeExpressionSegment(p, 0, 10, "1 +");
  EXPECT_EQ(p.getValue(3), 0.0);

  // Without a grammar (the default for a fresh TDoubleParam) every
  // expression is invalid.
  TDoubleParam q(5.0);
  q.setValue(0, 0.0);
  q.setValue(10, 0.0);
  TDoubleKeyframe k  = q.getKeyframe(0);
  k.m_type           = TDoubleKeyframe::Expression;
  k.m_expressionText = "42";
  q.setKeyframe(0, k);
  EXPECT_EQ(q.getValue(3), 0.0);
  q.setGrammar(&grammar());
  EXPECT_EQ(q.getValue(3), 42.0);
}

TEST(TExpressionInParam, NeighbouringLinearSegmentUsesTheExpressionValue) {
  // A keyframe-based segment that ends on an expression keyframe
  // interpolates towards the expression's value at that frame, not towards
  // the keyframe's stored m_value.
  TDoubleParam p;
  p.setGrammar(&grammar());
  p.setValue(0, 0.0);
  p.setValue(10, 0.0);
  p.setValue(20, 0.0);
  TDoubleKeyframe k  = p.getKeyframe(1);
  k.m_type           = TDoubleKeyframe::Expression;
  k.m_expressionText = "100";
  p.setKeyframe(1, k);
  EXPECT_NEAR(p.getValue(5), 50.0, kEps);
  EXPECT_NEAR(p.getValue(15), 100.0, kEps);
}

TEST(TExpressionInParam, CycleRepeatsThePreviousFrames) {
  TDoubleParam p;
  p.setGrammar(&grammar());
  p.setValue(0, 0.0);
  p.setValue(10, 10.0);
  p.setValue(30, 0.0);
  TDoubleKeyframe k  = p.getKeyframe(1);
  k.m_type           = TDoubleKeyframe::Expression;
  k.m_expressionText = "cycle(10)";
  p.setKeyframe(1, k);
  EXPECT_NEAR(p.getValue(5), 5.0, kEps);
  EXPECT_NEAR(p.getValue(15), 5.0, kEps);  // value at frame 5
  EXPECT_NEAR(p.getValue(25), 5.0, kEps);  // value at 15, i.e. at 5
  EXPECT_NEAR(p.getValue(18), 8.0, kEps);
}

TEST(TExpressionInParam, SaveLoadKeepsTheText) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const TFilePath fp(dir.filePath("expr.xml"));

  TDoubleParam src;
  makeExpressionSegment(src, 0, 10, "sin(frame * 30) * 2 + 1");
  {
    TOStream os(fp);
    os.openChild("param");
    src.saveData(os);
    os.closeChild();
  }

  TDoubleParam dst;
  dst.setGrammar(&grammar());
  {
    TIStream is(fp);
    std::string tag;
    ASSERT_TRUE(is.matchTag(tag));
    dst.loadData(is);
    EXPECT_TRUE(is.matchEndTag());
  }
  ASSERT_EQ(dst.getKeyframeCount(), 2);
  EXPECT_EQ(dst.getKeyframe(0).m_type, TDoubleKeyframe::Expression);
  EXPECT_EQ(dst.getKeyframe(0).m_expressionText, "sin(frame * 30) * 2 + 1");
  for (double f : {0.0, 1.0, 2.5, 9.0})
    EXPECT_NEAR(dst.getValue(f), src.getValue(f), kEps) << "frame " << f;
  EXPECT_NEAR(dst.getValue(2), 3.0, kEps);  // sin(90) * 2 + 1
}
