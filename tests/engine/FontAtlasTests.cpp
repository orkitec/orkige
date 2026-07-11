// Unit tests for the runtime-baked font/vector atlas (engine_gui/
// FontAtlas.h) and the SVG rasteriser seam (SvgRaster.h). Headless: FontAtlas
// bakes into a CPU page and builds the UiAtlas view without a render system,
// so getFont/getGlyph, lazy glyph paging and scale-responsive SVG raster are
// all verified without a GPU.
#include <catch2/catch_test_macros.hpp>

#include <engine_gui/FontAtlas.h>
#include <engine_gui/SvgRaster.h>
#include <engine_gui/UiAtlas.h>

#include <fstream>
#include <string>
#include <vector>

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

	// a tiny self-contained SVG (a filled circle) for the raster tests
	const char kSvg[] =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\">"
		"<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#ffffff\"/></svg>";

	bool anyOpaque(std::vector<unsigned char> const& rgba)
	{
		for (size_t i = 3; i < rgba.size(); i += 4)
		{
			if (rgba[i] > 0) { return true; }
		}
		return false;
	}
}

TEST_CASE("font atlas bakes an eager Latin-1 page from a TTF face",
	"[engine][gui][fontatlas]")
{
	const std::vector<unsigned char> bytes = readFile(kFontPath);
	REQUIRE(bytes.size() > 1024);

	// bake scale 1 keeps device px == design px, so *Scaled (x UiGlyph::scale,
	// default 1 headless) reads back the design metrics directly
	Orkige::FontAtlas fontAtlas("test_ttf_page", 512, 1.0f);
	REQUIRE(fontAtlas.addFace(9, bytes.data(), static_cast<int>(bytes.size()),
		24.0f));
	CHECK(fontAtlas.isValid());

	Orkige::UiAtlas const* atlas = fontAtlas.atlas();
	REQUIRE(atlas != nullptr);
	// the runtime page is a named GPU texture the draw batch would bind
	CHECK(atlas->getTextureName() == "test_ttf_page");
	// a whitepixel for solid fills sits inside the page
	CHECK(atlas->getWhitePixel().x > 0.0f);
	CHECK(atlas->getWhitePixel().x < 1.0f);

	Orkige::UiFont const* font = atlas->getFont(9);
	REQUIRE(font != nullptr);
	CHECK(font->getLineHeightScaled() > 0.0f);
	CHECK(font->getBaselineScaled() > 0.0f);
	CHECK(font->getSpaceLengthScaled() > 0.0f);

	// an inked ASCII glyph has a positive box and in-bounds normalized UVs
	Orkige::UiGlyph const* glyphA = font->getGlyph('A');
	REQUIRE(glyphA != nullptr);
	CHECK(glyphA->getGlyphWidthScaled() > 0.0f);
	CHECK(glyphA->getGlyphHeightScaled() > 0.0f);
	CHECK(glyphA->getGlyphAdvanceScaled() > 0.0f);
	CHECK(glyphA->uvLeft >= 0.0f);
	CHECK(glyphA->uvRight <= 1.0f);
	CHECK(glyphA->uvLeft < glyphA->uvRight);
	CHECK(glyphA->uvTop < glyphA->uvBottom);

	// a wide glyph advances more than a narrow one (real metrics, not a grid)
	Orkige::UiGlyph const* glyphW = font->getGlyph('W');
	Orkige::UiGlyph const* glyphI = font->getGlyph('i');
	REQUIRE(glyphW != nullptr);
	REQUIRE(glyphI != nullptr);
	CHECK(glyphW->getGlyphAdvanceScaled() > glyphI->getGlyphAdvanceScaled());

	// a Latin-1 supplement glyph is in the eager range
	CHECK(font->getGlyph(0x00E9) != nullptr);	// 'é'
}

TEST_CASE("font atlas bakes glyphs beyond the base range on demand",
	"[engine][gui][fontatlas][paging]")
{
	const std::vector<unsigned char> bytes = readFile(kFontPath);
	REQUIRE(bytes.size() > 1024);

	Orkige::FontAtlas fontAtlas("test_paging_page", 512, 1.0f);
	// eager base range only covers ASCII + Latin-1 (32..255)
	REQUIRE(fontAtlas.addFace(0, bytes.data(), static_cast<int>(bytes.size()),
		24.0f, 32, 255));
	Orkige::UiFont const* font = fontAtlas.atlas()->getFont(0);
	REQUIRE(font != nullptr);
	CHECK(font->getRangeEnd() == 255u);

	// a Cyrillic codepoint is outside the eager page; getGlyph bakes it lazily
	// into free page space (this is the loc()/CJK localisation unblocker)
	const Orkige::uint cyrillicA = 0x0410;		// 'А'
	Orkige::UiGlyph const* baked = font->getGlyph(cyrillicA);
	REQUIRE(baked != nullptr);
	CHECK(baked->getGlyphWidthScaled() > 0.0f);
	CHECK(baked->getGlyphAdvanceScaled() > 0.0f);
	CHECK(baked->uvLeft < baked->uvRight);

	// a second lookup returns the cached sparse glyph (stable, same UVs)
	Orkige::UiGlyph const* again = font->getGlyph(cyrillicA);
	REQUIRE(again != nullptr);
	CHECK(again->uvLeft == baked->uvLeft);
	CHECK(again->uvTop == baked->uvTop);
}

TEST_CASE("svg rasteriser is scale-responsive and non-empty",
	"[engine][gui][svg]")
{
	const unsigned char* svg = reinterpret_cast<const unsigned char*>(kSvg);
	const int size = static_cast<int>(sizeof(kSvg) - 1);

	Orkige::SvgRaster::Image at1x = Orkige::SvgRaster::rasterize(svg, size, 32);
	Orkige::SvgRaster::Image at2x = Orkige::SvgRaster::rasterize(svg, size, 64);

	// both bakes carry ink (the filled circle rasterised)
	REQUIRE(at1x.width == 32);
	REQUIRE(at2x.width == 64);
	CHECK(at1x.height > 0);
	CHECK(anyOpaque(at1x.rgba));
	CHECK(anyOpaque(at2x.rgba));

	// aspect preserved and the 2x bake is dimension-scaled (crisp: more texels,
	// not a stretched upscale). Square source -> square-ish output both ways.
	CHECK(at2x.height == at1x.height * 2);
	CHECK(at1x.rgba.size() == size_t(at1x.width) * at1x.height * 4);
	CHECK(at2x.rgba.size() == size_t(at2x.width) * at2x.height * 4);

	// a garbage blob rasterises to nothing (honest empty image)
	unsigned char junk[8] = { 'n', 'o', 't', 's', 'v', 'g', 0, 0 };
	Orkige::SvgRaster::Image bad =
		Orkige::SvgRaster::rasterize(junk, sizeof(junk), 32);
	CHECK(bad.width == 0);
	CHECK(bad.rgba.empty());
}

TEST_CASE("font atlas bakes an svg sprite at the requested design size",
	"[engine][gui][fontatlas][svg]")
{
	const unsigned char* svg = reinterpret_cast<const unsigned char*>(kSvg);
	const int size = static_cast<int>(sizeof(kSvg) - 1);

	// a 1x page and a 2x page: the sprite keeps its DESIGN size, the 2x page
	// just holds more texels for it (crispness comes for free from the source)
	Orkige::FontAtlas atlas1("svg_page_1x", 256, 1.0f);
	Orkige::FontAtlas atlas2("svg_page_2x", 256, 2.0f);
	REQUIRE(atlas1.addSvgSprite("dot", svg, size, 24.0f));
	REQUIRE(atlas2.addSvgSprite("dot", svg, size, 24.0f));

	Orkige::UiSprite const* s1 = atlas1.atlas()->getSprite("dot");
	Orkige::UiSprite const* s2 = atlas2.atlas()->getSprite("dot");
	REQUIRE(s1 != nullptr);
	REQUIRE(s2 != nullptr);
	// same design size regardless of bake scale (widgets lay out in design px)
	CHECK(s1->spriteWidth == 24.0f);
	CHECK(s2->spriteWidth == 24.0f);
	// both carry a valid normalized sub-rect
	CHECK(s1->uvLeft < s1->uvRight);
	CHECK(s2->uvLeft < s2->uvRight);
}
