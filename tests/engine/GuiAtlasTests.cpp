// Unit tests for the generated gui atlas (samples/jumper/media/
// gui_default.ogui + .png, produced by Util/make_gui_atlas.py).
//
// The .ogui is parsed by the REAL engine parser: UiAtlas's headless
// constructor (engine_gui/UiAtlas.h) consumes an Ogre::ConfigFile
// loaded from disk against the texture size read straight from the PNG
// header - no render system needed. The resource-system constructor
// funnels into the exact same code path, so the loader-level contract
// (sections, metrics, glyph coverage, sprites, whitepixel) is verified
// here and the GPU-side half (GuiManager boot + rendered HUD) is
// covered by the jumper_selfcheck integration test.
#include <catch2/catch_test_macros.hpp>

#include <engine_gui/UiAtlas.h>

#include <OgreConfigFile.h>

#include <cstdint>
#include <fstream>
#include <string>

namespace
{
	const std::string kAtlasDir = ORKIGE_GUI_ATLAS_DIR;
	const std::string kOguiPath = kAtlasDir + "/gui_default.ogui";
	const std::string kPngPath = kAtlasDir + "/gui_default.png";

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

	//! parse the .ogui through the real UiAtlas parser (headless door:
	//! the texture size comes from the PNG header, not a render system)
	Orkige::UiAtlas loadAtlas(uint32_t& width, uint32_t& height)
	{
		REQUIRE(readPngSize(kPngPath, width, height));
		Ogre::ConfigFile file;
		file.load(kOguiPath, " ", true);
		return Orkige::UiAtlas(file,
			static_cast<Orkige::Real>(width),
			static_cast<Orkige::Real>(height));
	}
}

TEST_CASE("gui atlas texture section matches the png",
	"[engine][gui][atlas]")
{
	uint32_t width = 0, height = 0;
	Orkige::UiAtlas atlas = loadAtlas(width, height);
	CHECK(width > 0);
	CHECK(height > 0);

	CHECK(atlas.getTextureName() == "gui_default.png");
	CHECK(atlas.getTextureSize().x == static_cast<float>(width));
	CHECK(atlas.getTextureSize().y == static_cast<float>(height));

	// the whitepixel every solid fill samples must be inside the texture
	// (UiAtlas normalizes it to 0..1)
	const Orkige::Vec2 whitePixel = atlas.getWhitePixel();
	CHECK(whitePixel.x > 0.0f);
	CHECK(whitePixel.y > 0.0f);
	CHECK(whitePixel.x < 1.0f);
	CHECK(whitePixel.y < 1.0f);
}

TEST_CASE("gui atlas fonts cover ASCII with valid glyph metrics",
	"[engine][gui][atlas]")
{
	uint32_t width = 0, height = 0;
	Orkige::UiAtlas atlas = loadAtlas(width, height);

	// layout math follows the global scale - pin it for the assertions
	const Orkige::Vec2 scaleBackup = Orkige::UiGlyph::scale;
	Orkige::UiGlyph::scale = Orkige::Vec2(1.0f, 1.0f);

	// the HUD font (9) and the title font (24) - the glyphDataIndex values
	// JumperHud and GuiManager::showStats use
	for (const Orkige::uint fontIndex : { 9u, 24u })
	{
		INFO("font index " << fontIndex);
		Orkige::UiFont const* font = atlas.getFont(fontIndex);
		REQUIRE(font != nullptr);

		// the scalar metrics the layout math reads; all must be positive
		CHECK(font->getLineHeightScaled() > 0.0f);
		CHECK(font->getBaselineScaled() > 0.0f);
		CHECK(font->getSpaceLengthScaled() > 0.0f);
		CHECK(font->getMonoWidthScaled() > 0.0f);

		CHECK(font->getRangeBegin() == 33u);	// '!' - space is spacelength, not a glyph
		CHECK(font->getRangeEnd() == 126u);		// '~'

		// UiAtlas creates a glyph slot for EVERY code in the range; a
		// missing glyph_<n> entry silently renders nothing - require full
		// coverage with in-bounds UV rects and positive advances
		for (Orkige::uint code = font->getRangeBegin();
			code <= font->getRangeEnd(); ++code)
		{
			INFO("glyph '" << static_cast<char>(code) << "' (" << code << ")");
			Orkige::UiGlyph const* glyph = font->getGlyph(code);
			REQUIRE(glyph != nullptr);
			CHECK(glyph->getGlyphWidthScaled() > 0.0f);
			CHECK(glyph->getGlyphHeightScaled() > 0.0f);
			CHECK(glyph->getGlyphAdvanceScaled() > 0.0f);
			// normalized texture coordinates inside the atlas
			CHECK(glyph->uvLeft >= 0.0f);
			CHECK(glyph->uvTop >= 0.0f);
			CHECK(glyph->uvRight <= 1.0f);
			CHECK(glyph->uvBottom <= 1.0f);
			CHECK(glyph->uvLeft < glyph->uvRight);
			CHECK(glyph->uvTop < glyph->uvBottom);
		}
	}

	// a character outside the range answers NULL, never a stray pointer
	CHECK(atlas.getFont(9)->getGlyph(32) == nullptr);	// space
	CHECK(atlas.getFont(9)->getGlyph(200) == nullptr);
	// an unknown font index answers NULL
	CHECK(atlas.getFont(9999) == nullptr);

	Orkige::UiGlyph::scale = scaleBackup;
}

TEST_CASE("gui atlas provides the sprites the widgets reference",
	"[engine][gui][atlas]")
{
	uint32_t width = 0, height = 0;
	Orkige::UiAtlas atlas = loadAtlas(width, height);

	// "progressbar_bar" is hardcoded in GuiProgressBar; the button states
	// are GuiButton's baseSpriteName + "_over"/"_down"/"_disabled"
	for (const char* name : { "panel", "button", "button_over", "button_down",
		"button_disabled", "progressbar", "progressbar_bar" })
	{
		INFO("sprite '" << name << "'");
		Orkige::UiSprite const* sprite = atlas.getSprite(name);
		REQUIRE(sprite != nullptr);
		CHECK(sprite->spriteWidth > 0.0f);
		CHECK(sprite->spriteHeight > 0.0f);
		// normalized rect inside the atlas
		CHECK(sprite->uvLeft >= 0.0f);
		CHECK(sprite->uvTop >= 0.0f);
		CHECK(sprite->uvRight <= 1.0f);
		CHECK(sprite->uvBottom <= 1.0f);
		CHECK(sprite->uvLeft < sprite->uvRight);
		CHECK(sprite->uvTop < sprite->uvBottom);
	}
	// an unknown sprite answers NULL, never a stray pointer
	CHECK(atlas.getSprite("no_such_sprite") == nullptr);
}
