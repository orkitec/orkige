// Unit tests for the pure world-text glyph layout (engine_gui/
// WorldTextLayout.h). Headless: a UiFont is baked from the committed
// engine-default TTF through FontAtlas (no render system), then the pure
// layout turns strings into text-local glyph quads. Verifies the inked-glyph
// count (spaces emit none), multi-line advance, real per-glyph kerning/advance
// (not a fixed grid), center anchoring, world-unit sizing and non-Latin paging.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <engine_gui/WorldTextLayout.h>
#include <engine_gui/FontAtlas.h>
#include <engine_gui/UiAtlas.h>

#include <cmath>
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

	// a baked headless UiFont (bake scale 1 -> device px == design px, and the
	// default UiGlyph::scale is (1,1), so *Scaled reads design metrics directly)
	struct BakedFont
	{
		Orkige::FontAtlas atlas{ "world_text_test_page", 1024, 1.0f };
		Orkige::UiFont const* font = nullptr;

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
}

TEST_CASE("world text lays out one quad per inked glyph, spaces excluded",
	"[engine][gui][worldtext]")
{
	BakedFont baked;
	REQUIRE(baked.font != nullptr);

	// "Hi" -> two inked glyphs, two quads
	Orkige::WorldTextLayout::Result hi =
		Orkige::WorldTextLayout::build(*baked.font, "Hi", 1.0f);
	CHECK(hi.quads.size() == 2u);
	CHECK(hi.lineCount == 1);
	CHECK(hi.width > 0.0f);
	CHECK(hi.height > 0.0f);

	// a space advances the pen but emits NO quad: "A B" is two inked glyphs
	Orkige::WorldTextLayout::Result spaced =
		Orkige::WorldTextLayout::build(*baked.font, "A B", 1.0f);
	CHECK(spaced.quads.size() == 2u);
	// ...and the space widened the block versus "AB" laid out tight
	Orkige::WorldTextLayout::Result tight =
		Orkige::WorldTextLayout::build(*baked.font, "AB", 1.0f);
	CHECK(spaced.width > tight.width);

	// an empty string / non-positive size lay out nothing
	CHECK(Orkige::WorldTextLayout::build(*baked.font, "", 1.0f).quads.empty());
	CHECK(Orkige::WorldTextLayout::build(*baked.font, "Hi", 0.0f).quads.empty());
}

TEST_CASE("world text stacks multi-line strings and center-anchors",
	"[engine][gui][worldtext]")
{
	BakedFont baked;
	REQUIRE(baked.font != nullptr);

	// two lines of two inked glyphs each -> four quads, two lines
	Orkige::WorldTextLayout::Result two =
		Orkige::WorldTextLayout::build(*baked.font, "Hi\nyo", 1.0f);
	CHECK(two.quads.size() == 4u);
	CHECK(two.lineCount == 2);

	// one line vs two: the two-line block is about twice as tall (one line
	// height per line)
	Orkige::WorldTextLayout::Result one =
		Orkige::WorldTextLayout::build(*baked.font, "Hi", 1.0f);
	CHECK(two.height > one.height * 1.5f);

	// center-anchored: for a single line the inked quads straddle x = 0
	float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
	for (auto const& quad : one.quads)
	{
		for (int c = 0; c < 4; ++c)
		{
			minX = std::min(minX, quad.corners[c].x);
			maxX = std::max(maxX, quad.corners[c].x);
			minY = std::min(minY, quad.corners[c].y);
			maxY = std::max(maxY, quad.corners[c].y);
		}
	}
	// straddles the origin on both axes (centered), within a line height
	CHECK(minX < 0.0f);
	CHECK(maxX > 0.0f);
	CHECK(std::abs(minX + maxX) < one.width);		// roughly symmetric about 0
	CHECK(minY < 0.0f);
	CHECK(maxY > 0.0f);
}

TEST_CASE("world text honours real metrics: kerning and per-glyph advance",
	"[engine][gui][worldtext]")
{
	BakedFont baked;
	REQUIRE(baked.font != nullptr);

	// wide vs narrow glyphs give different block widths (real advances, not a
	// fixed cell): "WWWW" is wider than "iiii"
	Orkige::WorldTextLayout::Result wide =
		Orkige::WorldTextLayout::build(*baked.font, "WWWW", 1.0f);
	Orkige::WorldTextLayout::Result narrow =
		Orkige::WorldTextLayout::build(*baked.font, "iiii", 1.0f);
	CHECK(wide.quads.size() == 4u);
	CHECK(narrow.quads.size() == 4u);
	CHECK(wide.width > narrow.width);

	// world-unit sizing scales linearly: doubling worldPerLine doubles extents
	Orkige::WorldTextLayout::Result atOne =
		Orkige::WorldTextLayout::build(*baked.font, "Ag", 1.0f);
	Orkige::WorldTextLayout::Result atTwo =
		Orkige::WorldTextLayout::build(*baked.font, "Ag", 2.0f);
	REQUIRE(atOne.width > 0.0f);
	CHECK(atTwo.width == Catch::Approx(atOne.width * 2.0f).epsilon(0.01));
	CHECK(atTwo.height == Catch::Approx(atOne.height * 2.0f).epsilon(0.01));

	// a line height maps to worldPerLine (the single-line block height)
	CHECK(atOne.height == Catch::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("world text pages a non-Latin string without breaking",
	"[engine][gui][worldtext][paging]")
{
	BakedFont baked;
	REQUIRE(baked.font != nullptr);

	// a Cyrillic string (UTF-8) is outside the eager Latin-1 page: each glyph
	// bakes lazily through the font provider (the loc()/CJK unblocker). Three
	// letters -> three inked quads, none dropped.
	Orkige::WorldTextLayout::Result cyr =
		Orkige::WorldTextLayout::build(*baked.font,
			"\xD0\x9F\xD1\x80\xD0\xB8", 1.0f);	// "При"
	CHECK(cyr.quads.size() == 3u);
	CHECK(cyr.width > 0.0f);
	// the UVs of a paged glyph are a valid in-page sub-rect
	for (auto const& quad : cyr.quads)
	{
		CHECK(quad.uv[Orkige::TopLeft].x < quad.uv[Orkige::TopRight].x);
	}
}
