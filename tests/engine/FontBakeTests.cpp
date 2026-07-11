// Unit tests for the TrueType rasterisation seam (engine_gui/FontBake.h),
// exercised against the committed engine-default font (Nunito, SIL OFL). No
// GPU: the seam bakes glyph coverage into CPU buffers and reports metrics.
#include <catch2/catch_test_macros.hpp>

#include <engine_gui/FontBake.h>

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
		if (!file)
		{
			return bytes;
		}
		const std::streamsize size = file.tellg();
		file.seekg(0);
		bytes.resize(static_cast<size_t>(size));
		file.read(reinterpret_cast<char*>(bytes.data()), size);
		return bytes;
	}
}

TEST_CASE("font bake opens the engine default face and reports metrics",
	"[engine][gui][fontbake]")
{
	const std::vector<unsigned char> bytes = readFile(kFontPath);
	REQUIRE(bytes.size() > 1024);

	Orkige::FontBake::Face* face =
		Orkige::FontBake::open(bytes.data(), static_cast<int>(bytes.size()));
	REQUIRE(face != nullptr);

	const float scale =
		Orkige::FontBake::scaleForPixelHeight(face, 32.0f);
	CHECK(scale > 0.0f);

	float ascent = 0, descent = 0, lineGap = 0;
	Orkige::FontBake::verticalMetrics(face, scale, ascent, descent, lineGap);
	CHECK(ascent > 0.0f);		// above the baseline
	CHECK(descent < 0.0f);		// below the baseline
	CHECK(ascent - descent > 0.0f);

	// a garbage blob must not parse (an honest NULL, never a crash)
	unsigned char junk[64] = { 0 };
	CHECK(Orkige::FontBake::open(junk, sizeof(junk)) == nullptr);

	Orkige::FontBake::close(face);
}

TEST_CASE("font bake advances and coverage are sane",
	"[engine][gui][fontbake]")
{
	const std::vector<unsigned char> bytes = readFile(kFontPath);
	REQUIRE(bytes.size() > 1024);
	Orkige::FontBake::Face* face =
		Orkige::FontBake::open(bytes.data(), static_cast<int>(bytes.size()));
	REQUIRE(face != nullptr);

	const float scale = Orkige::FontBake::scaleForPixelHeight(face, 48.0f);

	// a wide glyph advances more than a narrow one
	float advW = 0, advI = 0, lsb = 0;
	Orkige::FontBake::horizontalMetrics(face, 'W', scale, advW, lsb);
	Orkige::FontBake::horizontalMetrics(face, 'i', scale, advI, lsb);
	CHECK(advW > 0.0f);
	CHECK(advI > 0.0f);
	CHECK(advW > advI);

	// 'A' has ink; a space does not
	Orkige::FontBake::Bitmap glyphA =
		Orkige::FontBake::rasterize(face, 'A', scale);
	REQUIRE(glyphA.width > 0);
	REQUIRE(glyphA.height > 0);
	bool anyInk = false;
	for (unsigned char c : glyphA.coverage)
	{
		if (c > 0) { anyInk = true; break; }
	}
	CHECK(anyInk);

	Orkige::FontBake::Bitmap glyphSpace =
		Orkige::FontBake::rasterize(face, ' ', scale);
	CHECK(glyphSpace.width == 0);
	CHECK(glyphSpace.height == 0);

	// the face carries the codepoints the lazy-paging test relies on
	CHECK(Orkige::FontBake::hasCodepoint(face, 'A'));
	CHECK(Orkige::FontBake::hasCodepoint(face, 0x0410));	// Cyrillic 'А'

	Orkige::FontBake::close(face);
}
