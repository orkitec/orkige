/********************************************************************
	created:	2026/07/27 at 12:00
	filename: 	FileFormatIconTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Unit battery for the file-format fallback icon map (FileFormatIcon): every
// house extension resolves to a specific, non-empty glyph + tint, case and
// leading-dot are irrelevant, and an extension the table has never heard of
// falls back to the generic file glyph. The codepoint table mirrors
// EditorTheme.cpp's ICON_GLYPH_RANGES (the icon FONT ATLAS'S range list -
// that file lives in the orkige_editor executable target, which links ImGui,
// so it is out of reach for this headless editor_core suite; the two tables
// carry a comment binding them together and MUST be kept in sync by hand).
#include "FileFormatIcon.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using OrkigeEditor::fileFormatIcon;
using OrkigeEditor::FileFormatColor;
using OrkigeEditor::FileFormatIcon;

namespace
{
	//! decode the first UTF-8 codepoint of a Font Awesome glyph string (every
	//! FA6 glyph this table returns is a single 3-byte UTF-8 sequence in the
	//! Private Use Area, U+E000-U+F8FF)
	std::uint32_t firstCodepoint(const char* utf8)
	{
		REQUIRE(utf8 != nullptr);
		const auto* bytes = reinterpret_cast<const unsigned char*>(utf8);
		REQUIRE(bytes[0] != 0);
		if ((bytes[0] & 0xF0) == 0xE0)			// 3-byte sequence
		{
			return (static_cast<std::uint32_t>(bytes[0] & 0x0F) << 12) |
				(static_cast<std::uint32_t>(bytes[1] & 0x3F) << 6) |
				static_cast<std::uint32_t>(bytes[2] & 0x3F);
		}
		FAIL("unexpected glyph encoding");
		return 0;
	}

	//! MIRRORS EditorTheme.cpp's ICON_GLYPH_RANGES (ImWchar pairs, 0-terminated)
	//! - every codepoint FileFormatIcon.cpp hands back must fall in one of
	//! these ranges, or the icon font atlas never rasterises the glyph and
	//! the editor silently draws a blank tofu box. Update BOTH tables in the
	//! same change whenever fileFormatIcon starts returning a new glyph.
	constexpr std::uint32_t kIconGlyphRanges[] = {
		0xe13a, 0xe13a,
		0xf001, 0xf001,
		0xf008, 0xf008,
		0xf00a, 0xf00a,
		0xf02b, 0xf02b,
		0xf031, 0xf031,
		0xf03d, 0xf03e,
		0xf047, 0xf047,
		0xf04b, 0xf04b,
		0xf0cb, 0xf0cb,
		0xf11b, 0xf11b,
		0xf1ab, 0xf1ab,
		0xf28b, 0xf28b,
		0xf2ed, 0xf2ed,
		0xf061, 0xf063,
		0xf06e, 0xf06e,
		0xf07b, 0xf07c,
		0xf0b0, 0xf0b0,
		0xf15b, 0xf15c,
		0xf185, 0xf185,
		0xf188, 0xf188,
		0xf1b2, 0xf1b2,
		0xf1c9, 0xf1c9,
		0xf1fc, 0xf1fc,
		0xf245, 0xf245,
		0xf24d, 0xf24d,
		0xf256, 0xf256,
		0xf2d0, 0xf2d0,
		0xf2f1, 0xf2f1,
		0xf424, 0xf424,
		0xf53f, 0xf53f,
		0xf542, 0xf542,
		0xf5fd, 0xf5fd,
		0xf61f, 0xf61f,
		0xf70c, 0xf70c,
	};

	bool codepointInRanges(std::uint32_t codepoint)
	{
		for (std::size_t i = 0; i + 1 < sizeof(kIconGlyphRanges) /
			sizeof(kIconGlyphRanges[0]); i += 2)
		{
			if (codepoint >= kIconGlyphRanges[i] &&
				codepoint <= kIconGlyphRanges[i + 1])
			{
				return true;
			}
		}
		return false;
	}

	//! a glyph + colour is well-formed (non-null/empty glyph, a rasterisable
	//! codepoint, SOME non-black colour) - the shared body every case checks
	void checkWellFormed(FileFormatIcon const& icon)
	{
		REQUIRE(icon.glyph != nullptr);
		REQUIRE(icon.glyph[0] != '\0');
		CHECK(codepointInRanges(firstCodepoint(icon.glyph)));
		CHECK((icon.color.r != 0 || icon.color.g != 0 || icon.color.b != 0));
	}
}

TEST_CASE("fileFormatIcon: every house extension resolves", "[editor_core][file_format_icon]")
{
	static const char* const kHouseExtensions[] = {
		".oscene", ".oprefab", ".olevels", ".omat", ".oshape", ".oanim",
		".oui", ".ogui", ".oactions", ".olayers", ".orkproj", ".orkmeta",
		".lua",
	};
	for (const char* ext : kHouseExtensions)
	{
		INFO("extension: " << ext);
		checkWellFormed(fileFormatIcon(ext));
	}
}

TEST_CASE("fileFormatIcon: common non-house extensions resolve", "[editor_core][file_format_icon]")
{
	static const char* const kOtherExtensions[] = {
		".glb", ".gltf", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".gif",
		".dds", ".ktx", ".oitd", ".ttf", ".wav", ".caf", ".ogg", ".mp3",
		".flac", ".xlf", ".json", ".jsonl", ".xml", ".md",
	};
	for (const char* ext : kOtherExtensions)
	{
		INFO("extension: " << ext);
		checkWellFormed(fileFormatIcon(ext));
	}
}

TEST_CASE("fileFormatIcon: case and leading dot do not matter", "[editor_core][file_format_icon]")
{
	const FileFormatIcon dotted = fileFormatIcon(".lua");
	const FileFormatIcon bare = fileFormatIcon("lua");
	const FileFormatIcon upper = fileFormatIcon(".LUA");
	const FileFormatIcon mixed = fileFormatIcon("Lua");

	CHECK(std::string(dotted.glyph) == std::string(bare.glyph));
	CHECK(std::string(dotted.glyph) == std::string(upper.glyph));
	CHECK(std::string(dotted.glyph) == std::string(mixed.glyph));
	CHECK(dotted.color.r == bare.color.r);
	CHECK(dotted.color.g == bare.color.g);
	CHECK(dotted.color.b == bare.color.b);
}

TEST_CASE("fileFormatIcon: an unknown extension falls back to the generic file glyph",
	"[editor_core][file_format_icon]")
{
	const FileFormatIcon unknown = fileFormatIcon(".notarealformat");
	const FileFormatIcon empty = fileFormatIcon("");
	const FileFormatIcon generic = fileFormatIcon(".file");

	checkWellFormed(unknown);
	checkWellFormed(empty);
	CHECK(std::string(unknown.glyph) == std::string(generic.glyph));
	CHECK(std::string(empty.glyph) == std::string(generic.glyph));
	// the generic glyph is FA6's plain "file" outline (U+f15b) - distinct
	// from every specific house glyph above
	CHECK(firstCodepoint(unknown.glyph) == 0xf15bu);
}

TEST_CASE("fileFormatIcon: sibling extensions in one family share a tint",
	"[editor_core][file_format_icon]")
{
	// audio: .wav/.caf/.ogg/.mp3/.flac read as ONE family at a glance
	const FileFormatIcon wav = fileFormatIcon(".wav");
	const FileFormatIcon caf = fileFormatIcon(".caf");
	const FileFormatIcon ogg = fileFormatIcon(".ogg");
	CHECK(wav.color.r == caf.color.r);
	CHECK(wav.color.g == caf.color.g);
	CHECK(wav.color.b == caf.color.b);
	CHECK(wav.color.r == ogg.color.r);
	CHECK(wav.color.g == ogg.color.g);
	CHECK(wav.color.b == ogg.color.b);

	// scene family: .oscene and its level-sequence sibling .olevels
	const FileFormatIcon scene = fileFormatIcon(".oscene");
	const FileFormatIcon levels = fileFormatIcon(".olevels");
	CHECK(scene.color.r == levels.color.r);
	CHECK(scene.color.g == levels.color.g);
	CHECK(scene.color.b == levels.color.b);

	// distinct families read as visually distinct tints
	const FileFormatIcon lua = fileFormatIcon(".lua");
	CHECK((lua.color.r != wav.color.r || lua.color.g != wav.color.g ||
		lua.color.b != wav.color.b));
}
