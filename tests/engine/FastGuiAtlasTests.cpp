// Unit tests for the generated FastGUI atlas (samples/jumper/media/
// fastgui_default.ogui + .png, produced by Util/make_fastgui_atlas.py).
//
// The .ogui is parsed here EXACTLY like Gorilla::TextureAtlas::_load does:
// through Ogre::ConfigFile with " " as the key/value separator and lowercased
// section names (see engine_fastgui/Gorilla.cpp). The full TextureAtlas class
// cannot run headless - its constructor immediately loads the texture through
// Ogre::TextureManager and clones materials, which needs an initialised
// render system - so the loader-level contract is verified here and the
// GPU-side half (Silverback::loadAtlas + FastGuiManager boot + rendered HUD)
// is covered by the jumper_selfcheck integration test.
#include <catch2/catch_test_macros.hpp>

#include <OgreConfigFile.h>
#include <OgreStringConverter.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <string>

namespace
{
	const std::string kAtlasDir = ORKIGE_FASTGUI_ATLAS_DIR;
	const std::string kOguiPath = kAtlasDir + "/fastgui_default.ogui";
	const std::string kPngPath = kAtlasDir + "/fastgui_default.png";

	//! PNG dimensions straight from the IHDR chunk (no image lib needed)
	bool readPngSize(std::string const& path, uint32_t& width, uint32_t& height)
	{
		std::ifstream file(path, std::ios::binary);
		unsigned char header[24] = {};
		if (!file.read(reinterpret_cast<char*>(header), sizeof(header)))
		{
			return false;
		}
		static const unsigned char magic[8] =
			{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		if (!std::equal(magic, magic + 8, header))
		{
			return false;
		}
		width = (header[16] << 24) | (header[17] << 16) |
			(header[18] << 8) | header[19];
		height = (header[20] << 24) | (header[21] << 16) |
			(header[22] << 8) | header[23];
		return true;
	}

	//! load the .ogui the way Gorilla does (separator " ", trimmed,
	//! lowercased section names) into section -> multimap
	std::map<Ogre::String, Ogre::ConfigFile::SettingsMultiMap>
	loadOgui(std::string const& path)
	{
		Ogre::ConfigFile file;
		file.load(path, " ", true);
		std::map<Ogre::String, Ogre::ConfigFile::SettingsMultiMap> sections;
		for (auto const& [name, settings] : file.getSettingsBySection())
		{
			Ogre::String lowered = name;
			Ogre::StringUtil::toLowerCase(lowered);
			sections[lowered] = settings;
		}
		return sections;
	}
}

TEST_CASE("fastgui atlas texture section matches the png",
	"[engine][fastgui][atlas]")
{
	uint32_t width = 0, height = 0;
	REQUIRE(readPngSize(kPngPath, width, height));
	CHECK(width > 0);
	CHECK(height > 0);

	auto sections = loadOgui(kOguiPath);
	REQUIRE(sections.count("texture") == 1);
	Ogre::ConfigFile::SettingsMultiMap const& texture = sections["texture"];

	REQUIRE(texture.count("file") == 1);
	CHECK(texture.find("file")->second == "fastgui_default.png");

	// the whitepixel Gorilla uses for all solid fills must be inside the
	// texture (parsed as a Vector2 of pixel coordinates)
	REQUIRE(texture.count("whitepixel") == 1);
	const Ogre::Vector2 whitePixel = Ogre::StringConverter::parseVector2(
		texture.find("whitepixel")->second);
	CHECK(whitePixel.x > 0);
	CHECK(whitePixel.y > 0);
	CHECK(whitePixel.x < static_cast<float>(width));
	CHECK(whitePixel.y < static_cast<float>(height));
}

TEST_CASE("fastgui atlas fonts cover ASCII with valid glyph metrics",
	"[engine][fastgui][atlas]")
{
	uint32_t width = 0, height = 0;
	REQUIRE(readPngSize(kPngPath, width, height));
	auto sections = loadOgui(kOguiPath);

	// the HUD font (9) and the title font (24) - the glyphDataIndex values
	// JumperHud and FastGuiManager::showStats use
	for (const char* fontSection : { "font.9", "font.24" })
	{
		INFO("section [" << fontSection << "]");
		REQUIRE(sections.count(fontSection) == 1);
		Ogre::ConfigFile::SettingsMultiMap const& font =
			sections[fontSection];

		// the scalar metrics Gorilla reads; all must be present and positive
		for (const char* key : { "lineheight", "baseline", "spacelength",
			"monowidth" })
		{
			INFO("metric '" << key << "'");
			REQUIRE(font.count(key) == 1);
			CHECK(Ogre::StringConverter::parseReal(
				font.find(key)->second) > 0.0f);
		}
		REQUIRE(font.count("offset") == 1);
		const Ogre::Vector2 offset = Ogre::StringConverter::parseVector2(
			font.find("offset")->second);

		REQUIRE(font.count("range") == 1);
		const Ogre::Vector2 range = Ogre::StringConverter::parseVector2(
			font.find("range")->second);
		const int rangeBegin = static_cast<int>(range.x);
		const int rangeEnd = static_cast<int>(range.y);
		CHECK(rangeBegin == 33);	// '!' - space is spacelength, not a glyph
		CHECK(rangeEnd == 126);		// '~'

		// Gorilla creates a glyph slot for EVERY code in the range; a missing
		// glyph_<n> entry silently renders nothing - require full coverage
		// with in-bounds rects and positive advances
		for (int code = rangeBegin; code <= rangeEnd; ++code)
		{
			const std::string key = "glyph_" + std::to_string(code);
			INFO("glyph '" << static_cast<char>(code) << "' (" << key << ")");
			REQUIRE(font.count(key) == 1);
			const Ogre::StringVector values = Ogre::StringUtil::split(
				font.find(key)->second, " ", 5);
			REQUIRE(values.size() == 5);	// x y w h advance
			const float x = offset.x + Ogre::StringConverter::parseReal(values[0]);
			const float y = offset.y + Ogre::StringConverter::parseReal(values[1]);
			const float w = Ogre::StringConverter::parseReal(values[2]);
			const float h = Ogre::StringConverter::parseReal(values[3]);
			const int advance = Ogre::StringConverter::parseInt(values[4]);
			CHECK(w > 0.0f);
			CHECK(h > 0.0f);
			CHECK(advance > 0);
			CHECK(x >= 0.0f);
			CHECK(y >= 0.0f);
			CHECK(x + w <= static_cast<float>(width));
			CHECK(y + h <= static_cast<float>(height));
		}
	}
}

TEST_CASE("fastgui atlas provides the sprites the widgets reference",
	"[engine][fastgui][atlas]")
{
	uint32_t width = 0, height = 0;
	REQUIRE(readPngSize(kPngPath, width, height));
	auto sections = loadOgui(kOguiPath);
	REQUIRE(sections.count("sprites") == 1);
	Ogre::ConfigFile::SettingsMultiMap const& sprites = sections["sprites"];

	// "progressbar_bar" is hardcoded in FastGuiProgressBar; the button states
	// are FastGuiButton's baseSpriteName + "_over"/"_down"/"_disabled"
	for (const char* name : { "panel", "button", "button_over", "button_down",
		"button_disabled", "progressbar", "progressbar_bar" })
	{
		INFO("sprite '" << name << "'");
		REQUIRE(sprites.count(name) == 1);
		const Ogre::StringVector values = Ogre::StringUtil::split(
			sprites.find(name)->second, " ", 4);
		REQUIRE(values.size() == 4);	// x y w h (parsed as unsigned ints)
		const unsigned int x = Ogre::StringConverter::parseUnsignedInt(values[0]);
		const unsigned int y = Ogre::StringConverter::parseUnsignedInt(values[1]);
		const unsigned int w = Ogre::StringConverter::parseUnsignedInt(values[2]);
		const unsigned int h = Ogre::StringConverter::parseUnsignedInt(values[3]);
		CHECK(w > 0u);
		CHECK(h > 0u);
		CHECK(x + w <= width);
		CHECK(y + h <= height);
	}
}
