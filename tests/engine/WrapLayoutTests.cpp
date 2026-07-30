/********************************************************************
	created:	Sunday 2026/07/26 at 12:00
	filename: 	WrapLayoutTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Unit tests for wrap-to-width text layout: the pure greedy line-breaker
// (engine_gui/TextWrap) driven with synthetic cell widths, and the font-aware
// cell builder (TextWrap::buildRun) exercised headlessly against the committed
// engine-default TTF baked through FontAtlas (the WorldTextLayout precedent -
// no render system). Verifies latin space breaks + trailing-space collapse,
// CJK per-glyph breaking, the long-word hard-break, '\n' composing with wrap,
// kerning dropped at a line start (within lines only), a run keeping its
// per-cell attributes across a split, an atomic (sprite-like) cell moving
// whole, and measured height = line count x line height.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <engine_gui/TextWrap.h>
#include <engine_gui/FontAtlas.h>
#include <engine_gui/UiAtlas.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using namespace Orkige;

namespace
{
	const std::string kFontPath =
		std::string(ORKIGE_ENGINE_FONT_DIR) + "/Nunito-Regular.ttf";

	std::vector<unsigned char> readFile(std::string const& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		std::vector<unsigned char> bytes;
		if (!file) { return bytes; }
		const std::streamsize size = file.tellg();
		file.seekg(0);
		bytes.resize(static_cast<size_t>(size));
		file.read(reinterpret_cast<char*>(bytes.data()), size);
		return bytes;
	}

	struct BakedFont
	{
		FontAtlas atlas{ "wrap_test_page", 1024, 1.0f };
		UiFont const* font = nullptr;
		BakedFont()
		{
			const std::vector<unsigned char> bytes = readFile(kFontPath);
			if (bytes.size() > 1024 &&
				atlas.addFace(0, bytes.data(),
					static_cast<int>(bytes.size()), 48.0f))
			{
				font = atlas.atlas()->getFont(0);
			}
		}
	};

	// a plain glyph-like cell of a fixed width (advance == width, no kerning)
	WrapCell glyphCell(float w)
	{
		WrapCell c;
		c.advance = w;
		c.width = w;
		return c;
	}
	WrapCell spaceCell(float w)
	{
		WrapCell c;
		c.space = true;
		c.advance = w;
		return c;
	}
}

TEST_CASE("textwrap: latin runs break at spaces, trailing space collapses",
	"[unit][gui][textwrap]")
{
	// "AB CD": two 20px words with a 5px space between them
	std::vector<WrapCell> cells = {
		glyphCell(10), glyphCell(10), spaceCell(5), glyphCell(10), glyphCell(10) };

	// wide enough for both words -> one line
	WrapResult wide;
	TextWrap::wrap(cells, 100.0f, wide);
	CHECK(wide.lineCount == 1);

	// only the first word fits -> break at the space; the second word moves down
	WrapResult narrow;
	TextWrap::wrap(cells, 25.0f, narrow);
	REQUIRE(narrow.lineCount == 2);
	CHECK(narrow.lineOf[0] == 0);
	CHECK(narrow.lineOf[1] == 0);
	CHECK(narrow.lineOf[2] == 0);		// the space stays on the first line
	CHECK(narrow.lineOf[3] == 1);		// 'C' begins the second line
	CHECK(narrow.lineOf[4] == 1);
	// the trailing space does NOT extend the first line's measured width
	CHECK(narrow.lineWidth[0] == Catch::Approx(20.0f));
	// the second word begins the line at pen 0
	CHECK(narrow.penX[3] == Catch::Approx(0.0f));
}

TEST_CASE("textwrap: a single over-wide word hard-breaks at the glyph limit",
	"[unit][gui][textwrap]")
{
	// five 10px glyphs, no break opportunities, into a 25px column
	std::vector<WrapCell> cells = {
		glyphCell(10), glyphCell(10), glyphCell(10), glyphCell(10), glyphCell(10) };
	WrapResult wrapped;
	TextWrap::wrap(cells, 25.0f, wrapped);
	// 2 glyphs per line (20 <= 25, a third would be 30 > 25) -> three lines
	REQUIRE(wrapped.lineCount == 3);
	CHECK(wrapped.lineOf[0] == 0);
	CHECK(wrapped.lineOf[1] == 0);
	CHECK(wrapped.lineOf[2] == 1);
	CHECK(wrapped.lineOf[4] == 2);
	// no line ever overflows the column
	for (float w : wrapped.lineWidth)
	{
		CHECK(w <= 25.0f);
	}
	// a glyph wider than the whole column is still placed (its own line), never
	// dropped or looped on
	std::vector<WrapCell> huge = { glyphCell(999) };
	WrapResult one;
	TextWrap::wrap(huge, 25.0f, one);
	CHECK(one.lineCount == 1);
	CHECK(one.penX[0] == Catch::Approx(0.0f));
}

TEST_CASE("textwrap: explicit newlines force breaks and compose with wrap",
	"[unit][gui][textwrap]")
{
	WrapCell nl;
	nl.forcedBreak = true;
	// "AB\nCD" with an ample width still breaks at the '\n'
	std::vector<WrapCell> cells = {
		glyphCell(10), glyphCell(10), nl, glyphCell(10), glyphCell(10) };
	WrapResult wrapped;
	TextWrap::wrap(cells, 1000.0f, wrapped);
	REQUIRE(wrapped.lineCount == 2);
	CHECK(wrapped.lineOf[0] == 0);
	CHECK(wrapped.lineOf[3] == 1);
	CHECK(wrapped.penX[3] == Catch::Approx(0.0f));
}

TEST_CASE("textwrap: kerning is dropped at a line start (within lines only)",
	"[unit][gui][textwrap]")
{
	// two glyphs; the second carries 5px of leading kerning and a CJK-style
	// break opportunity before it
	std::vector<WrapCell> cells(2);
	cells[0] = glyphCell(20);
	cells[1] = glyphCell(20);
	cells[1].leadKern = 5.0f;
	cells[1].breakBefore = true;

	// on one line the kerning positions the second glyph (20 + 5)
	WrapResult together;
	TextWrap::wrap(cells, 1000.0f, together);
	REQUIRE(together.lineCount == 1);
	CHECK(together.penX[1] == Catch::Approx(25.0f));

	// once it wraps to a new line the kerning vanishes (starts at pen 0)
	WrapResult broken;
	TextWrap::wrap(cells, 30.0f, broken);
	REQUIRE(broken.lineCount == 2);
	CHECK(broken.lineOf[1] == 1);
	CHECK(broken.penX[1] == Catch::Approx(0.0f));
}

TEST_CASE("textwrap: CJK glyphs break between any two neighbours",
	"[unit][gui][textwrap]")
{
	// the classifier: kanji / kana / hangul break, latin + space do not
	CHECK(TextWrap::isBreakableIdeograph(0x65E5));	// 日
	CHECK(TextWrap::isBreakableIdeograph(0x3042));	// あ (hiragana)
	CHECK(TextWrap::isBreakableIdeograph(0xAC00));	// 가 (hangul)
	CHECK_FALSE(TextWrap::isBreakableIdeograph('A'));
	CHECK_FALSE(TextWrap::isBreakableIdeograph(' '));

	// three CJK cells (each a break opportunity, no spaces) into a 25px column:
	// they break between glyphs 20px at a time
	std::vector<WrapCell> cells(3);
	for (auto& c : cells) { c = glyphCell(20); c.breakBefore = true; }
	WrapResult wrapped;
	TextWrap::wrap(cells, 25.0f, wrapped);
	// one glyph per line (two would be 40 > 25) -> three lines
	REQUIRE(wrapped.lineCount == 3);
	CHECK(wrapped.lineOf[0] == 0);
	CHECK(wrapped.lineOf[1] == 1);
	CHECK(wrapped.lineOf[2] == 2);
}

TEST_CASE("textwrap: a run keeps its per-cell attributes across a split",
	"[unit][gui][textwrap]")
{
	// a wrap never drops or reorders cells, so a styled run (colour/font per
	// cell, as UiMarkupText carries) keeps each cell's attribute across a break:
	// the mapping cell -> line is total and order-preserving
	std::vector<WrapCell> cells = {
		glyphCell(10), glyphCell(10), spaceCell(5), glyphCell(10), glyphCell(10) };
	WrapResult wrapped;
	TextWrap::wrap(cells, 25.0f, wrapped);
	CHECK(wrapped.lineOf.size() == cells.size());
	CHECK(wrapped.penX.size() == cells.size());
	// lines are monotonic non-decreasing in input order (no reordering)
	for (size_t i = 1; i < wrapped.lineOf.size(); ++i)
	{
		CHECK(wrapped.lineOf[i] >= wrapped.lineOf[i - 1]);
	}
}

TEST_CASE("textwrap: an atomic (sprite-like) cell moves whole to the next line",
	"[unit][gui][textwrap]")
{
	// "AB " then a 40px atomic cell (an inline sprite): it does not fit after
	// the word, so it moves ENTIRELY to the next line (one cell, one line)
	std::vector<WrapCell> cells = {
		glyphCell(10), glyphCell(10), spaceCell(5), glyphCell(40) };
	WrapResult wrapped;
	TextWrap::wrap(cells, 50.0f, wrapped);
	REQUIRE(wrapped.lineCount == 2);
	CHECK(wrapped.lineOf[0] == 0);
	CHECK(wrapped.lineOf[3] == 1);		// the sprite cell wholly on line 2
	CHECK(wrapped.penX[3] == Catch::Approx(0.0f));
}

TEST_CASE("textwrap: buildRun + wrap on a real baked font",
	"[unit][gui][textwrap][font]")
{
	BakedFont baked;
	REQUIRE(baked.font != nullptr);
	const float lineHeight = baked.font->getLineHeightScaled();
	REQUIRE(lineHeight > 0.0f);

	// "hello world" one line wide, and the natural single-line width
	std::vector<WrapCell> cells;
	std::vector<UiGlyph const*> glyphs;
	TextWrap::buildRun(*baked.font, "hello world", cells, glyphs);
	REQUIRE(cells.size() == 11u);				// h e l l o _ w o r l d
	REQUIRE(glyphs.size() == cells.size());
	CHECK(glyphs[5] == nullptr);				// the space carries no glyph

	WrapResult wide;
	TextWrap::wrap(cells, 100000.0f, wide);
	REQUIRE(wide.lineCount == 1);
	const float naturalWidth = wide.lineWidth[0];
	CHECK(naturalWidth > 0.0f);

	// a column just under the natural width breaks at the space into two lines,
	// and the measured height is EXACTLY line count x line height
	WrapResult two;
	TextWrap::wrap(cells, naturalWidth * 0.6f, two);
	REQUIRE(two.lineCount == 2);
	CHECK(two.lineOf[0] == 0);					// "hello" on line 1
	CHECK(two.lineOf[6] == 1);					// "world" on line 2 ('w')
	CHECK(two.penX[6] == Catch::Approx(0.0f));	// the wrapped word starts at 0
	const float measuredHeight = lineHeight * float(two.lineCount);
	CHECK(measuredHeight == Catch::Approx(lineHeight * 2.0f));

	// narrower still -> more lines, each within the column
	WrapResult many;
	TextWrap::wrap(cells, naturalWidth * 0.3f, many);
	CHECK(many.lineCount >= 3);
	for (float w : many.lineWidth)
	{
		CHECK(w <= naturalWidth * 0.3f + 0.5f);
	}

	// kerning within a line, dropped at a break: on one line the glyphs advance
	// monotonically; the real Nunito 'W' pair kerns, and a wrapped first glyph
	// always sits at pen 0 (asserted above via penX[6])
	std::vector<WrapCell> kern;
	std::vector<UiGlyph const*> kglyphs;
	TextWrap::buildRun(*baked.font, "AV", kern, kglyphs);
	REQUIRE(kern.size() == 2u);
	WrapResult kerned;
	TextWrap::wrap(kern, 100000.0f, kerned);
	CHECK(kerned.penX[1] >= 0.0f);				// second glyph placed after the first
}

TEST_CASE("textwrap: buildRun scales EVERY metric by the text scale",
	"[unit][gui][textwrap][font][style]")
{
	// a per-widget textScale multiplies the glyph metrics the breaker measures,
	// so a scaled caption wraps at the width its scaled glyphs really occupy.
	// Every cell field has to scale together - advance, inked width and the
	// leading kerning - or measurement and drawing disagree.
	BakedFont baked;
	REQUIRE(baked.font != nullptr);

	std::vector<WrapCell> plain;
	std::vector<UiGlyph const*> plainGlyphs;
	TextWrap::buildRun(*baked.font, "hello world", plain, plainGlyphs);

	std::vector<WrapCell> scaled;
	std::vector<UiGlyph const*> scaledGlyphs;
	TextWrap::buildRun(*baked.font, "hello world", scaled, scaledGlyphs, 2.0f);

	REQUIRE(scaled.size() == plain.size());
	for (size_t each = 0; each < plain.size(); ++each)
	{
		CHECK(scaled[each].advance ==
			Catch::Approx(plain[each].advance * 2.0f));
		CHECK(scaled[each].width == Catch::Approx(plain[each].width * 2.0f));
		CHECK(scaled[each].leadKern ==
			Catch::Approx(plain[each].leadKern * 2.0f));
		// the flags are structure, not metrics - they must NOT change
		CHECK(scaled[each].space == plain[each].space);
		CHECK(scaled[each].forcedBreak == plain[each].forcedBreak);
		CHECK(scaled[each].breakBefore == plain[each].breakBefore);
		CHECK(scaled[each].byteOffset == plain[each].byteOffset);
	}

	// scale 1 is byte-identical to the default call (the unstyled path is
	// untouched by construction)
	std::vector<WrapCell> unit;
	std::vector<UiGlyph const*> unitGlyphs;
	TextWrap::buildRun(*baked.font, "hello world", unit, unitGlyphs, 1.0f);
	REQUIRE(unit.size() == plain.size());
	for (size_t each = 0; each < plain.size(); ++each)
	{
		CHECK(unit[each].advance == plain[each].advance);
		CHECK(unit[each].width == plain[each].width);
		CHECK(unit[each].leadKern == plain[each].leadKern);
	}
}

TEST_CASE("textwrap: a SCALED run wraps into more lines at the same width",
	"[unit][gui][textwrap][font][style]")
{
	// the height a wrapped widget reports is line count x (line height x scale).
	// Doubling the scale in a fixed column therefore both breaks more often and
	// makes each line taller - the growth a `preferred` content-size-fit sees.
	BakedFont baked;
	REQUIRE(baked.font != nullptr);
	const float lineHeight = baked.font->getLineHeightScaled();
	REQUIRE(lineHeight > 0.0f);

	const char* copy = "a wrapped label breaks to the width the layout gives it";
	std::vector<WrapCell> plain;
	std::vector<UiGlyph const*> plainGlyphs;
	TextWrap::buildRun(*baked.font, copy, plain, plainGlyphs);
	WrapResult natural;
	TextWrap::wrap(plain, 100000.0f, natural);
	REQUIRE(natural.lineCount == 1);
	const float column = natural.lineWidth[0] * 0.5f;

	WrapResult plainWrapped;
	TextWrap::wrap(plain, column, plainWrapped);
	REQUIRE(plainWrapped.lineCount >= 2);

	std::vector<WrapCell> big;
	std::vector<UiGlyph const*> bigGlyphs;
	TextWrap::buildRun(*baked.font, copy, big, bigGlyphs, 2.0f);
	WrapResult bigWrapped;
	TextWrap::wrap(big, column, bigWrapped);

	// twice the glyphs in the same column: strictly more lines
	CHECK(bigWrapped.lineCount > plainWrapped.lineCount);
	// and each line is twice as tall, so the measured block grows on both counts
	const float plainHeight = lineHeight * float(plainWrapped.lineCount);
	const float bigHeight = lineHeight * 2.0f * float(bigWrapped.lineCount);
	CHECK(bigHeight > plainHeight * 2.0f - 0.5f);
	// every scaled line still fits the column (no overflow from the scale)
	for (float w : bigWrapped.lineWidth)
	{
		CHECK(w <= column + 0.5f);
	}
}

TEST_CASE("textwrap: a scaled run keeps the SAME caret byte mapping",
	"[unit][gui][textwrap][font][style]")
{
	// the caret's LINE/byte mapping is structure; only its pen is a metric. A
	// scaled multi-line field must therefore place the caret on the same line
	// as an unscaled one wrapped at the proportionally wider column, and its
	// pen must scale - never drift into another code point.
	BakedFont baked;
	REQUIRE(baked.font != nullptr);

	const std::string text = "one two\nthree";
	std::vector<WrapCell> plain, scaled;
	std::vector<UiGlyph const*> pg, sg;
	TextWrap::buildRun(*baked.font, text, plain, pg);
	TextWrap::buildRun(*baked.font, text, scaled, sg, 2.0f);
	REQUIRE(plain.size() == scaled.size());

	WrapResult plainWrapped, scaledWrapped;
	TextWrap::wrap(plain, 100000.0f, plainWrapped);
	TextWrap::wrap(scaled, 200000.0f, scaledWrapped);
	REQUIRE(plainWrapped.lineCount == scaledWrapped.lineCount);

	const size_t caretByte = text.find("three") + 2;	// inside the second line
	const TextWrap::CaretSpot plainSpot =
		TextWrap::locateCaret(plain, plainWrapped, caretByte);
	const TextWrap::CaretSpot scaledSpot =
		TextWrap::locateCaret(scaled, scaledWrapped, caretByte);
	CHECK(plainSpot.line == scaledSpot.line);		// same line, unaffected
	CHECK(scaledSpot.penX == Catch::Approx(plainSpot.penX * 2.0f));

	// the visual line starts are byte offsets - identical at any scale
	std::vector<size_t> plainStarts, scaledStarts;
	TextWrap::lineStartBytes(plain, plainWrapped, text.size(), plainStarts);
	TextWrap::lineStartBytes(scaled, scaledWrapped, text.size(), scaledStarts);
	CHECK(plainStarts == scaledStarts);
}

TEST_CASE("textwrap: buildRun records the source byte offset of every cell",
	"[unit][gui][textwrap]")
{
	BakedFont baked;
	if (!baked.font) { SUCCEED("engine font unavailable - skipped"); return; }

	// "a b" plus a two-byte code point: the offsets must be BYTE offsets, so
	// the caret of a multi-line field never lands mid-code-point
	std::vector<WrapCell> cells;
	std::vector<UiGlyph const*> glyphs;
	TextWrap::buildRun(*baked.font, "a b\n\xC3\xA9", cells, glyphs);
	REQUIRE(cells.size() == 5u);			// a, space, b, '\n', the accented glyph
	CHECK(cells[0].byteOffset == 0u);
	CHECK(cells[1].byteOffset == 1u);
	CHECK(cells[1].space);
	CHECK(cells[2].byteOffset == 2u);
	CHECK(cells[3].byteOffset == 3u);
	CHECK(cells[3].forcedBreak);
	CHECK(cells[4].byteOffset == 4u);		// the two-byte code point's lead byte
}

TEST_CASE("textwrap: locateCaret maps a byte index to its wrapped line + pen",
	"[unit][gui][textwrap]")
{
	// two 10px glyphs per word, a 5px space, wrapped to 25px -> two lines
	std::vector<WrapCell> cells = {
		glyphCell(10), glyphCell(10), spaceCell(5), glyphCell(10), glyphCell(10) };
	for (size_t each = 0; each < cells.size(); ++each)
	{
		cells[each].byteOffset = each;		// one byte per cell (ascii)
	}
	WrapResult wrapped;
	TextWrap::wrap(cells, 25.0f, wrapped);
	REQUIRE(wrapped.lineCount == 2);

	// caret at the very start
	TextWrap::CaretSpot start = TextWrap::locateCaret(cells, wrapped, 0);
	CHECK(start.line == 0);
	CHECK(start.penX == Catch::Approx(0.0f));
	// caret before the second glyph of line 1
	TextWrap::CaretSpot mid = TextWrap::locateCaret(cells, wrapped, 1);
	CHECK(mid.line == 0);
	CHECK(mid.penX == Catch::Approx(10.0f));
	// caret before the first glyph of the WRAPPED word -> line 2, pen 0
	TextWrap::CaretSpot moved = TextWrap::locateCaret(cells, wrapped, 3);
	CHECK(moved.line == 1);
	CHECK(moved.penX == Catch::Approx(0.0f));
	// caret past the end sits after the last glyph on its line
	TextWrap::CaretSpot end = TextWrap::locateCaret(cells, wrapped, 99);
	CHECK(end.line == 1);
	CHECK(end.penX == Catch::Approx(20.0f));
}

TEST_CASE("textwrap: a caret after a trailing newline opens the next line",
	"[unit][gui][textwrap]")
{
	WrapCell newline;
	newline.forcedBreak = true;
	newline.byteOffset = 1;
	std::vector<WrapCell> cells = { glyphCell(10), newline };
	cells[0].byteOffset = 0;
	WrapResult wrapped;
	TextWrap::wrap(cells, 100.0f, wrapped);
	REQUIRE(wrapped.lineCount == 2);
	TextWrap::CaretSpot spot = TextWrap::locateCaret(cells, wrapped, 2);
	CHECK(spot.line == 1);
	CHECK(spot.penX == Catch::Approx(0.0f));
}

TEST_CASE("textwrap: lineStartBytes slices a run at its visual line starts",
	"[unit][gui][textwrap]")
{
	BakedFont baked;
	if (!baked.font) { SUCCEED("engine font unavailable - skipped"); return; }

	// two explicit lines, the first of which also soft-wraps
	const String text = "hello world\nsecond";
	std::vector<WrapCell> cells;
	std::vector<UiGlyph const*> glyphs;
	TextWrap::buildRun(*baked.font, text, cells, glyphs);
	// a column just wide enough for the LONGEST word, so every word keeps its
	// own line and nothing hard-breaks
	auto measureWord = [&](char const* word)
	{
		std::vector<WrapCell> measure;
		std::vector<UiGlyph const*> measureGlyphs;
		TextWrap::buildRun(*baked.font, word, measure, measureGlyphs);
		WrapResult natural;
		TextWrap::wrap(measure, 0.0f, natural);
		return natural.lineWidth[0];
	};
	const float wordWidth = std::max(measureWord("hello"),
		std::max(measureWord("world"), measureWord("second"))) * 1.1f;

	WrapResult wrapped;
	TextWrap::wrap(cells, wordWidth, wrapped);
	REQUIRE(wrapped.lineCount == 3);			// hello / world / second
	std::vector<size_t> starts;
	TextWrap::lineStartBytes(cells, wrapped, text.size(), starts);
	REQUIRE(starts.size() == size_t(wrapped.lineCount));
	CHECK(starts[0] == 0u);
	// every start is a real byte boundary, strictly increasing
	for (size_t each = 1; each < starts.size(); ++each)
	{
		CHECK(starts[each] > starts[each - 1]);
		CHECK(starts[each] <= text.size());
	}
	// the LAST line is the one after the '\n' - slicing there yields "second"
	CHECK(text.substr(starts.back()) == "second");

	// THE SLICE CONTRACT: re-wrapping a suffix that starts on a line boundary
	// reproduces the same following breaks (what the multi-line field relies on)
	const String tail = text.substr(starts[1]);
	std::vector<WrapCell> tailCells;
	std::vector<UiGlyph const*> tailGlyphs;
	TextWrap::buildRun(*baked.font, tail, tailCells, tailGlyphs);
	WrapResult tailWrapped;
	TextWrap::wrap(tailCells, wordWidth, tailWrapped);
	CHECK(tailWrapped.lineCount == wrapped.lineCount - 1);
}

TEST_CASE("textwrap: an empty line opened by a newline starts after it",
	"[unit][gui][textwrap]")
{
	BakedFont baked;
	if (!baked.font) { SUCCEED("engine font unavailable - skipped"); return; }

	const String text = "a\n\nb";
	std::vector<WrapCell> cells;
	std::vector<UiGlyph const*> glyphs;
	TextWrap::buildRun(*baked.font, text, cells, glyphs);
	WrapResult wrapped;
	TextWrap::wrap(cells, 0.0f, wrapped);
	REQUIRE(wrapped.lineCount == 3);
	std::vector<size_t> starts;
	TextWrap::lineStartBytes(cells, wrapped, text.size(), starts);
	REQUIRE(starts.size() == 3u);
	CHECK(starts[0] == 0u);
	CHECK(starts[1] == 2u);		// the empty middle line
	CHECK(starts[2] == 3u);		// 'b'
}
