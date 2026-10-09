// Unit tests for TOStream / TIStream (include/tstream.h).
//
// TOStream and TIStream are the XML-like serializers behind every .tnz
// scene, palette (.tpl) and most of the persisted state of the application,
// including the params of every fx. The format quirks pinned down here (how
// strings are quoted, how doubles are printed, how tags are matched) are part
// of the on-disk format and must not change by accident.

#include <gtest/gtest.h>

#include "texception.h"
#include "tfilepath.h"
#include "tpixel.h"
#include "tstream.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

#include <climits>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace {

class TStreamTest : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(m_dir.isValid()); }

  TFilePath path(const char *name) const {
    return TFilePath(m_dir.filePath(QString::fromUtf8(name)));
  }

  static std::string readBytes(const TFilePath &fp) {
    QFile f(QString::fromStdWString(fp.getWideString()));
    if (!f.open(QIODevice::ReadOnly)) return std::string();
    QByteArray data = f.readAll();
    return std::string(data.constData(), data.size());
  }

  static void writeBytes(const TFilePath &fp, const std::string &text) {
    QFile f(QString::fromStdWString(fp.getWideString()));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(text.data(), qint64(text.size()));
  }

  QTemporaryDir m_dir;
};

// Writes a small document exercising nested children, attributes and every
// scalar type; used by both the plain and the compressed round trip.
void writeSampleDocument(TOStream &os) {
  os.openChild("scene", {{"name", "shot 01"}, {"version", "71.1"}});

  os.openChild("layer", {{"index", "1"}, {"label", "BG layer"}});
  os << 42 << -7 << 0.25 << std::string("plain");
  os.closeChild();

  os.openChild("layer", {{"index", "2"}, {"label", "FG"}});
  os.openChild("strings");
  os << std::string("with space")
     << std::string("")
     // "cafe" with an e-acute and two CJK characters, as UTF-8 bytes...
     << std::string("caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac")
     // ...and an i-diaeresis plus two CJK characters as a wide string.
     << std::wstring(L"na\u00efve \u4e16\u754c");
  os.closeChild();
  os.openChild("color");
  os << TPixel32(10, 20, 30, 40);
  os.closeChild();
  os.closeChild();

  os.openCloseChild("marker", {{"frame", "12"}});
  os.closeChild();
}

// Reads back the document written by writeSampleDocument().
void readSampleDocument(TIStream &is) {
  std::string tag;
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "scene");
  EXPECT_EQ(is.getTagAttribute("name"), "shot 01");
  EXPECT_EQ(is.getTagAttribute("version"), "71.1");
  EXPECT_EQ(is.getTagAttribute("missing"), "");
  EXPECT_EQ(is.getCurrentTagName(), "scene");

  // First layer: scalars.
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "layer");
  int index = 0;
  EXPECT_TRUE(is.getTagParam("index", index));
  EXPECT_EQ(index, 1);
  std::string label;
  EXPECT_TRUE(is.getTagParam("label", label));
  EXPECT_EQ(label, "BG layer");
  int i0 = 0, i1 = 0;
  double d = 0;
  std::string s;
  is >> i0 >> i1 >> d >> s;
  EXPECT_EQ(i0, 42);
  EXPECT_EQ(i1, -7);
  EXPECT_EQ(d, 0.25);
  EXPECT_EQ(s, "plain");
  EXPECT_TRUE(is.matchEndTag());

  // Second layer: nested children.
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "layer");
  EXPECT_EQ(is.getTagAttribute("label"), "FG");
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "strings");
  std::string spaced, empty, utf8;
  std::wstring wide;
  is >> spaced >> empty >> utf8 >> wide;
  EXPECT_EQ(spaced, "with space");
  EXPECT_EQ(empty, "");
  EXPECT_EQ(utf8, "caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac");
  EXPECT_EQ(wide, L"na\u00efve \u4e16\u754c");
  EXPECT_TRUE(is.matchEndTag());
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "color");
  TPixel32 pix;
  is >> pix;
  EXPECT_EQ(pix, TPixel32(10, 20, 30, 40));
  EXPECT_TRUE(is.matchEndTag());
  EXPECT_TRUE(is.matchEndTag());  // </layer>

  // Begin-end tag: matched, but not pushed on the tag stack.
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "marker");
  EXPECT_TRUE(is.isBeginEndTag());
  EXPECT_EQ(is.getTagAttribute("frame"), "12");
  EXPECT_EQ(is.getCurrentTagName(), "scene");

  EXPECT_FALSE(is.matchTag(tag));  // an end tag is next, not a begin tag
  EXPECT_TRUE(is.matchEndTag());   // </scene>
}

}  // namespace

//-----------------------------------------------------------------------------
// Output format

TEST_F(TStreamTest, OutputFormatIsStable) {
  const TFilePath fp = path("format.xml");
  {
    TOStream os(fp);
    ASSERT_TRUE(bool(os));
    os.openChild("scene", {{"version", "71"}, {"name", "a b"}});
    os << 42 << 3.5 << std::string("plain") << std::string("two words");
    os.openCloseChild("marker", {{"frame", "3"}});
    os.openChild("empty");
    os.closeChild();
    os.closeChild();
    EXPECT_TRUE(os.checkStatus());
  }

  // Attributes are written in std::map order (alphabetical), not insertion
  // order. Scalars are separated by a trailing space; strings made only of
  // [A-Za-z0-9_%] are written bare, anything else is double-quoted (and the
  // quoted form has no trailing space). Children are indented by two spaces;
  // a closing tag that directly follows another tag stays at the indentation
  // of the content ("    </empty>", "  </scene>"), while one that follows a
  // value goes on a new line at its own level (see the next test).
  EXPECT_EQ(readBytes(fp),
            "<scene name=\"a b\" version=\"71\">\n"
            "  42 3.5 plain \"two words\"\n"
            "  <marker frame=\"3\"/>\n"
            "  <empty>\n"
            "    </empty>\n"
            "  </scene>\n");
}

TEST_F(TStreamTest, DoublesUseDefaultStreamPrecision) {
  // operator<<(double) uses the default ostream precision (6 significant
  // digits), so doubles do NOT round-trip exactly; values within 1e-8 of
  // zero are written as 0. This is how .tnz files store every double today.
  const TFilePath fp = path("doubles.xml");
  {
    TOStream os(fp);
    os.openChild("d");
    os << 3.14159265358979 << 1234567.0 << 1e-9 << -0.5 << 1e-5;
    os.closeChild();
  }
  EXPECT_EQ(readBytes(fp), "<d>\n  3.14159 1.23457e+06 0 -0.5 1e-05 \n</d>\n");

  TIStream is(fp);
  ASSERT_TRUE(bool(is));
  std::string tag;
  ASSERT_TRUE(is.matchTag(tag));
  double a = 0, b = 0, c = 1, d = 0, e = 0;
  is >> a >> b >> c >> d >> e;
  EXPECT_EQ(a, 3.14159);
  EXPECT_EQ(b, 1234570.0);
  EXPECT_EQ(c, 0.0);
  EXPECT_EQ(d, -0.5);
  EXPECT_EQ(e, 1e-5);
  EXPECT_TRUE(is.matchEndTag());
}

TEST_F(TStreamTest, IntegersRoundTripExactly) {
  const TFilePath fp = path("ints.xml");
  {
    TOStream os(fp);
    os.openChild("i");
    os << 0 << -1 << INT_MAX << INT_MIN;
    os.closeChild();
  }
  TIStream is(fp);
  std::string tag;
  ASSERT_TRUE(is.matchTag(tag));
  int a = 9, b = 0, c = 0, d = 0;
  is >> a >> b >> c >> d;
  EXPECT_EQ(a, 0);
  EXPECT_EQ(b, -1);
  EXPECT_EQ(c, INT_MAX);
  EXPECT_EQ(d, INT_MIN);
  EXPECT_TRUE(is.matchEndTag());
}

//-----------------------------------------------------------------------------
// Round trips

TEST_F(TStreamTest, PlainRoundTrip) {
  const TFilePath fp = path("plain.xml");
  {
    TOStream os(fp);
    ASSERT_TRUE(bool(os));
    writeSampleDocument(os);
  }
  EXPECT_EQ(readBytes(fp).substr(0, 7), "<scene ");

  TIStream is(fp);
  ASSERT_TRUE(bool(is));
  readSampleDocument(is);
}

TEST_F(TStreamTest, CompressedRoundTripIsAutoDetected) {
  const TFilePath fp = path("compressed.xml");
  {
    TOStream os(fp, true);
    writeSampleDocument(os);
  }  // the compressed file is only written when the stream is destroyed

  const TFilePath plainFp = path("uncompressed.xml");
  {
    TOStream os(plainFp);
    writeSampleDocument(os);
  }
  const std::string plain = readBytes(plainFp);

  // Header: "TABc", the endianness probe 0x0A0B0C0D, the uncompressed and
  // compressed sizes as native-endian 32-bit ints, then an LZ4 frame.
  const std::string bytes = readBytes(fp);
  ASSERT_GT(bytes.size(), 16u);
  EXPECT_EQ(bytes.substr(0, 4), "TABc");
  auto int32At = [&bytes](size_t offset) {
    std::int32_t v = 0;
    std::memcpy(&v, bytes.data() + offset, sizeof v);
    return v;
  };
  EXPECT_EQ(int32At(4), 0x0A0B0C0D);
  EXPECT_EQ(int32At(8), std::int32_t(plain.size()));
  EXPECT_EQ(int32At(12), std::int32_t(bytes.size() - 16));

  // TIStream recognises the compressed form by its leading 'T' and inflates
  // it transparently.
  TIStream is(fp);
  ASSERT_TRUE(bool(is));
  readSampleDocument(is);
}

TEST_F(TStreamTest, SpecialCharactersInStrings) {
  const std::string tricky[] = {
      "quote\"inside", "back\\slash", "it's",
      "<tag> & more",  "tab\there",   "dash-and.dot",
  };
  const TFilePath fp = path("special.xml");
  {
    TOStream os(fp);
    os.openChild("s", {{"attr", "say \"hi\""}});
    for (const std::string &s : tricky) os << s;
    os.closeChild();
  }
  TIStream is(fp);
  std::string tag;
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(is.getTagAttribute("attr"), "say \"hi\"");
  for (const std::string &expected : tricky) {
    std::string s;
    is >> s;
    EXPECT_EQ(s, expected);
  }
  EXPECT_TRUE(is.matchEndTag());
}

TEST_F(TStreamTest, FilePathRoundTrip) {
  const TFilePath paths[] = {
      TFilePath("scenes/shot01.tnz"),
      TFilePath("+drawings/A.0001.png"),
      TFilePath("my scenes/shot 01/bg.tlv"),
      TFilePath(L"\u65e5\u672c/\u4e16\u754c.pli"),
  };
  const TFilePath fp = path("paths.xml");
  {
    TOStream os(fp);
    os.openChild("paths");
    for (const TFilePath &p : paths) os << p;
    os.closeChild();
  }
  TIStream is(fp);
  std::string tag;
  ASSERT_TRUE(is.matchTag(tag));
  for (const TFilePath &expected : paths) {
    TFilePath p;
    is >> p;
    EXPECT_EQ(p, expected);
  }
  EXPECT_TRUE(is.matchEndTag());
}

TEST_F(TStreamTest, FilePathWithApostrophe) {
  const TFilePath fp = path("apostrophe.xml");
  {
    TOStream os(fp);
    os.openChild("p");
    os << TFilePath("it's/a.png") << std::string("it's");
    os.closeChild();
  }
  TIStream is(fp);
  std::string tag;
  ASSERT_TRUE(is.matchTag(tag));
  TFilePath p;
  std::string s;
  is >> p >> s;
  EXPECT_EQ(s, "it's");  // std::string extraction undoes the escaping
#if defined(LINUX) || defined(FREEBSD)
  // BUG (characterised): on Linux and FreeBSD the writer escapes ' as \'
  // (see escape() in tstream.cpp), but operator>>(TFilePath &) keeps escape
  // sequences verbatim and TFilePath then turns the backslash into a path
  // separator, so the path comes back with an extra directory level.
  EXPECT_EQ(p, TFilePath("it/'s/a.png"));
#else
  EXPECT_EQ(p, TFilePath("it's/a.png"));
#endif
  EXPECT_TRUE(is.matchEndTag());
}

//-----------------------------------------------------------------------------
// Parser behaviour on hand-written input

TEST_F(TStreamTest, CommentsAreSkipped) {
  const TFilePath fp = path("comments.xml");
  writeBytes(fp,
             "<!-- leading comment -->\n"
             "<root a='single quoted'>\n"
             "  <!-- inner -- comment -->\n"
             "  <item>5</item>\n"
             "</root>\n");
  TIStream is(fp);
  std::string tag;
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "root");
  EXPECT_EQ(is.getTagAttribute("a"), "single quoted");
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "item");
  int v = 0;
  is >> v;
  EXPECT_EQ(v, 5);
  EXPECT_TRUE(is.matchEndTag());
  EXPECT_TRUE(is.matchEndTag());
}

TEST_F(TStreamTest, SkipCurrentTagSkipsNestedContent) {
  const TFilePath fp = path("skip.xml");
  writeBytes(fp,
             "<root>\n"
             "  <unknown x=\"1\"><a>1 2</a><b/><c><d>x</d></c></unknown>\n"
             "  <known>7</known>\n"
             "</root>\n");
  TIStream is(fp);
  std::string tag;
  ASSERT_TRUE(is.matchTag(tag));
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "unknown");
  is.skipCurrentTag();
  EXPECT_EQ(is.getCurrentTagName(), "root");
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "known");
  int v = 0;
  is >> v;
  EXPECT_EQ(v, 7);
  EXPECT_TRUE(is.matchEndTag());
  EXPECT_TRUE(is.matchEndTag());
}

TEST_F(TStreamTest, DeprecatedOpenChildRejectsBeginEndTags) {
  const TFilePath fp = path("openchild.xml");
  writeBytes(fp, "<root><leaf k=\"v\"/><node>1</node></root>");
  TIStream is(fp);
  std::string tag;
  ASSERT_TRUE(is.openChild(tag));
  EXPECT_EQ(tag, "root");

  // openChild() only accepts <tag>, not <tag/>; the rejected tag stays
  // pending and the next matchTag() returns it.
  EXPECT_FALSE(is.openChild(tag));
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_EQ(tag, "leaf");
  EXPECT_TRUE(is.isBeginEndTag());

  ASSERT_TRUE(is.openChild(tag));
  EXPECT_EQ(tag, "node");
  int v = 0;
  is >> v;
  EXPECT_EQ(v, 1);
  is.closeChild();
  is.closeChild();
}

TEST_F(TStreamTest, MismatchedEndTagThrows) {
  const TFilePath fp = path("mismatch.xml");
  writeBytes(fp, "<a>\n</b>\n");
  TIStream is(fp);
  std::string tag;
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_THROW(is.matchEndTag(), TException);
}

TEST_F(TStreamTest, MissingEndTagThrowsOnCloseChild) {
  const TFilePath fp = path("unterminated.xml");
  writeBytes(fp, "<a>\n<b>1</b>\n");
  TIStream is(fp);
  std::string tag;
  ASSERT_TRUE(is.matchTag(tag));
  EXPECT_THROW(is.closeChild(), TException);  // finds <b>, not </a>
}

TEST_F(TStreamTest, MissingFileGivesInvalidStream) {
  TIStream is(path("does_not_exist.xml"));
  EXPECT_FALSE(bool(is));

  TOStream os(path("no_such_dir/out.xml"));
  EXPECT_FALSE(bool(os));
  EXPECT_FALSE(os.checkStatus());
}

TEST_F(TStreamTest, VersionIsCarriedByTheReader) {
  const TFilePath fp = path("version.xml");
  writeBytes(fp, "<a/>");
  TIStream is(fp);
  EXPECT_EQ(is.getVersion(), VersionNumber(0, 0));
  is.setVersion(VersionNumber(71, 1));
  EXPECT_EQ(is.getVersion(), VersionNumber(71, 1));
  EXPECT_EQ(is.getFilePath(), fp);
}
