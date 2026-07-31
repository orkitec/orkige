/********************************************************************
	created:	Friday 2026/07/31 at 15:00
	filename: 	ExportTextureCookTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	The export-time texture cook.

	The format-resolution MATRIX is the heart of it and is asserted exhaustively:
	every (auto|explicit) x (desktop|ios|android|web) x (next|classic) x
	(opaque|alpha) cell has exactly one right answer, and the wrong one ships a
	texture the target GPU cannot decode - a black sprite on a device, found by
	a person, not a test. The impossible pairs must REFUSE: a half-cooked
	export is worse than none.

	The payload cases then assert the file-level contract: a cooked texture
	replaces its source and takes its sidecar with it (the asset id is what
	scene references resolve through - lose it and the shipped scene loads
	nothing), a texture without a sidecar has no import intent and is never
	touched, and a cubemap keeps its baked prefiltered chain.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportFiles.h"
#include "ExportImage.h"
#include "ExportTextureCook.h"

#include <core_project/AssetDatabase.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

using namespace OrkigeExport;

namespace
{
	struct ScratchDir
	{
		Orkige::String path;
		explicit ScratchDir(Orkige::String const & name)
		{
			this->path = (std::filesystem::temp_directory_path() /
				("orkige_cook_test_" + name)).string();
			ExportFiles::removeTree(this->path, 0);
			ExportFiles::makeDirectories(this->path, 0);
		}
		~ScratchDir() { ExportFiles::removeTree(this->path, 0); }
	};

	Orkige::TextureImportSettings autoSettings()
	{
		return Orkige::TextureImportSettings();	// format "auto", quality normal
	}

	//! resolve and return "format/container", or "-" when the source ships
	Orkige::String resolved(Orkige::TextureImportSettings const & settings,
		Orkige::String const & platform, Orkige::String const & flavor,
		bool alpha)
	{
		TextureCookTarget target;
		Orkige::String error;
		if(!resolveTextureFormat(settings, platform, flavor, alpha, target,
			nullptr, &error))
		{
			return "REFUSED";
		}
		if(target.format.empty())
		{
			return "-";
		}
		return target.format + "/" + target.container;
	}

	Orkige::TextureImportSettings withFormat(Orkige::String const & format)
	{
		Orkige::TextureImportSettings settings;
		settings.format = format;
		return settings;
	}

	Orkige::TextureImportSettings withQuality(Orkige::String const & quality)
	{
		Orkige::TextureImportSettings settings;
		settings.quality = quality;
		return settings;
	}

	ExportImage solid(int width, int height, unsigned char red,
		unsigned char green, unsigned char blue, unsigned char alpha)
	{
		ExportImage image(width, height);
		for(std::size_t index = 0; index + 3 < image.pixels.size(); index += 4)
		{
			image.pixels[index] = red;
			image.pixels[index + 1] = green;
			image.pixels[index + 2] = blue;
			image.pixels[index + 3] = alpha;
		}
		return image;
	}

	//! write a sidecar carrying an id and the given texture import block
	void writeSidecar(Orkige::String const & assetPath,
		Orkige::TextureImport const & import)
	{
		REQUIRE(Orkige::AssetDatabase::writeMetaFile(
			assetPath + Orkige::AssetDatabase::META_FILE_EXTENSION,
			"testid0123456789", import));
	}

	//! an id-only sidecar (no <texture> block) - the DEFAULT settings apply
	void writeIdOnlySidecar(Orkige::String const & assetPath)
	{
		REQUIRE(Orkige::AssetDatabase::writeMetaFile(
			assetPath + Orkige::AssetDatabase::META_FILE_EXTENSION,
			"testid0123456789"));
	}

	void putU32(std::vector<unsigned char> & bytes, std::size_t offset,
		std::uint32_t value)
	{
		bytes[offset] = static_cast<unsigned char>(value);
		bytes[offset + 1] = static_cast<unsigned char>(value >> 8);
		bytes[offset + 2] = static_cast<unsigned char>(value >> 16);
		bytes[offset + 3] = static_cast<unsigned char>(value >> 24);
	}

	int mipCountFor(int size)
	{
		int count = 1;
		while((size >> count) > 0)
		{
			++count;
		}
		return count;
	}

	//! an UNCOMPRESSED masked-32bpp six-face cubemap .dds with a BAKED mip
	//! chain - the shape a prefiltered sky bake produces, and the only .dds
	//! the cook re-encodes
	std::vector<unsigned char> buildUncompressedCubemapDds(int size)
	{
		const int mips = mipCountFor(size);
		std::vector<unsigned char> bytes(128, 0);
		bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
		putU32(bytes, 4, 124);							// header size
		putU32(bytes, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000);
		putU32(bytes, 12, static_cast<std::uint32_t>(size));	// height
		putU32(bytes, 16, static_cast<std::uint32_t>(size));	// width
		putU32(bytes, 28, static_cast<std::uint32_t>(mips));
		putU32(bytes, 76, 32);							// pixel-format size
		putU32(bytes, 80, 0x1 | 0x40);					// ALPHAPIXELS | RGB
		putU32(bytes, 88, 32);							// bit count
		putU32(bytes, 92, 0x000000FFu);					// R (byte 0)
		putU32(bytes, 96, 0x0000FF00u);					// G (byte 1)
		putU32(bytes, 100, 0x00FF0000u);				// B (byte 2)
		putU32(bytes, 104, 0xFF000000u);				// A (byte 3)
		putU32(bytes, 108, 0x1000 | 0x8 | 0x400000);	// caps
		putU32(bytes, 112, 0xFE00);						// CUBEMAP + six faces
		// FACE-major: each face's whole chain, a distinct flat colour per face
		for(int face = 0; face < 6; ++face)
		{
			for(int level = 0; level < mips; ++level)
			{
				const int dimension = std::max(1, size >> level);
				for(int texel = 0; texel < dimension * dimension; ++texel)
				{
					bytes.push_back(static_cast<unsigned char>(face * 40));
					bytes.push_back(128);
					bytes.push_back(
						static_cast<unsigned char>(255 - face * 30));
					bytes.push_back(255);
				}
			}
		}
		return bytes;
	}
}

TEST_CASE("the auto format table per platform and flavor", "[unit][export]")
{
	const Orkige::TextureImportSettings settings = autoSettings();

	// the browser: compressed-texture support is a property of the visitor's
	// GPU, so nothing is guaranteed and auto ships the PNG
	CHECK(resolved(settings, "web", "classic", true) == "-");
	CHECK(resolved(settings, "web", "next", false) == "-");

	// the classic GLES2 mobile flavor: ETC2/ASTC are GLES3-tier
	CHECK(resolved(settings, "ios", "classic", true) == "-");
	CHECK(resolved(settings, "android", "classic", false) == "-");

	// modern mobile: ASTC, block size from the quality
	CHECK(resolved(settings, "ios", "next", true) == "astc-6x6/oitd");
	CHECK(resolved(settings, "android", "next", false) == "astc-6x6/oitd");
	CHECK(resolved(withQuality("high"), "ios", "next", true) ==
		"astc-4x4/oitd");
	CHECK(resolved(withQuality("low"), "ios", "next", false) ==
		"astc-8x8/oitd");
	CHECK(resolved(withQuality("high"), "android", "next", true) ==
		"astc-4x4/oitd");
	CHECK(resolved(withQuality("low"), "android", "next", false) ==
		"astc-8x8/oitd");

	// desktop next: bc1 opaque, bc7 for alpha (and for a high-quality opaque)
	CHECK(resolved(settings, "", "next", false) == "bc1/dds");
	CHECK(resolved(settings, "", "next", true) == "bc7/dds");
	CHECK(resolved(withQuality("high"), "", "next", false) == "bc7/dds");

	// desktop classic: bc3 for alpha - the classic default GL renderer has no
	// BC7 everywhere it runs
	CHECK(resolved(settings, "", "classic", true) == "bc3/dds");
	CHECK(resolved(settings, "", "classic", false) == "bc1/dds");

	// "macos" spells the same desktop slot as ""
	CHECK(resolved(settings, "macos", "next", false) == "bc1/dds");
}

TEST_CASE("format none always ships the source", "[unit][export]")
{
	const Orkige::TextureImportSettings none = withFormat("none");
	for(const char * platform : { "", "macos", "ios", "android", "web" })
	{
		for(const char * flavor : { "next", "classic" })
		{
			INFO(platform << "/" << flavor);
			CHECK(resolved(none, platform, flavor, true) == "-");
			CHECK(resolved(none, platform, flavor, false) == "-");
		}
	}
}

TEST_CASE("an explicit format wins over the auto pick", "[unit][export]")
{
	CHECK(resolved(withFormat("astc-8x8"), "ios", "next", true) ==
		"astc-8x8/oitd");
	// the etc2 FAMILY resolves its variant per texture: EAC alpha only when
	// the image actually carries any
	CHECK(resolved(withFormat("etc2"), "android", "next", false) ==
		"etc2-rgb/oitd");
	CHECK(resolved(withFormat("etc2"), "android", "next", true) ==
		"etc2-rgba/oitd");
	// the classic container for the mobile families is KTX1
	CHECK(resolved(withFormat("etc2"), "ios", "classic", false) ==
		"etc2-rgb/ktx");
	CHECK(resolved(withFormat("astc-4x4"), "web", "classic", true) ==
		"astc-4x4/ktx");
}

TEST_CASE("impossible format pairs refuse", "[unit][export]")
{
	// BCn on a mobile GPU: there is no hardware for it
	CHECK(resolved(withFormat("bc7"), "ios", "next", true) == "REFUSED");
	CHECK(resolved(withFormat("bc1"), "android", "next", true) == "REFUSED");
	// BC7 on the classic desktop flavor: its default GL renderer has none
	CHECK(resolved(withFormat("bc7"), "", "classic", true) == "REFUSED");
	// the mobile families on a desktop export: the desktop runtimes load only
	// the BC family
	CHECK(resolved(withFormat("astc-4x4"), "", "classic", true) == "REFUSED");
	CHECK(resolved(withFormat("astc-4x4"), "", "next", true) == "REFUSED");
	CHECK(resolved(withFormat("etc2"), "", "next", true) == "REFUSED");
	// a token nothing answers to
	CHECK(resolved(withFormat("nonsense"), "", "next", true) == "REFUSED");

	// every refusal carries a reason a person can act on
	TextureCookTarget target;
	Orkige::String error;
	CHECK_FALSE(resolveTextureFormat(withFormat("bc7"), "ios", "next", true,
		target, nullptr, &error));
	CHECK(error.find("bc7") != Orkige::String::npos);
	CHECK(error.find("ios") != Orkige::String::npos);
}

TEST_CASE("permitted but lossy overrides warn loudly", "[unit][export]")
{
	std::vector<Orkige::String> warnings;
	auto sink = [&warnings](Orkige::String const & message)
	{
		warnings.push_back(message);
	};
	TextureCookTarget target;

	// an explicit compressed format on the web is allowed but is a per-visitor
	// GPU lottery - it must not pass silently
	REQUIRE(resolveTextureFormat(withFormat("astc-4x4"), "web", "classic",
		true, target, sink, 0));
	CHECK(target.format == "astc-4x4");
	REQUIRE(warnings.size() == 1);
	CHECK(warnings[0].find("WARNING") != Orkige::String::npos);

	warnings.clear();
	REQUIRE(resolveTextureFormat(withFormat("etc2"), "android", "classic",
		false, target, sink, 0));
	CHECK(target.container == "ktx");
	REQUIRE(warnings.size() == 1);
	CHECK(warnings[0].find("WARNING") != Orkige::String::npos);

	// the auto path is never lossy and never warns
	warnings.clear();
	REQUIRE(resolveTextureFormat(autoSettings(), "android", "next", false,
		target, sink, 0));
	CHECK(warnings.empty());
}

TEST_CASE("the mip chain matches the encoder's level layout", "[unit][export]")
{
	const ExportImage base = solid(20, 12, 10, 20, 30, 255);
	// off: exactly one level
	CHECK(buildMipLevels(base, false).size() == 1);

	// on: base>>i on each axis (min 1), down to 1x1 - 20x12 gives 5 levels
	const std::vector<ExportImage> levels = buildMipLevels(base, true);
	REQUIRE(levels.size() == 5);
	CHECK((levels[0].width == 20 && levels[0].height == 12));
	CHECK((levels[1].width == 10 && levels[1].height == 6));
	CHECK((levels[2].width == 5 && levels[2].height == 3));
	CHECK((levels[3].width == 2 && levels[3].height == 1));
	CHECK((levels[4].width == 1 && levels[4].height == 1));

	// a square power-of-two chain stops exactly at 1x1
	const std::vector<ExportImage> square =
		buildMipLevels(solid(8, 8, 1, 2, 3, 255), true);
	REQUIRE(square.size() == 4);
	CHECK(square[3].width == 1);

	// a 1x1 source has nothing below it
	CHECK(buildMipLevels(solid(1, 1, 1, 2, 3, 255), true).size() == 1);
}

TEST_CASE("a texture without a sidecar is never touched", "[unit][export]")
{
	ScratchDir scratch("nosidecar");
	const Orkige::String path = ExportFiles::join(scratch.path, "loose.png");
	REQUIRE(encodePngFile(solid(20, 20, 9, 9, 9, 255), path, 0));
	const unsigned long long before = ExportFiles::treeSize(path);

	TextureCookResult result;
	Orkige::String error;
	REQUIRE(cookTexturePayload(scratch.path, "", "next", result, nullptr,
		&error));
	// no sidecar means no import intent at all
	CHECK(result.cooked == 0);
	CHECK(ExportFiles::isRegularFile(path));
	CHECK(ExportFiles::treeSize(path) == before);
}

TEST_CASE("conditioning without compression rewrites the PNG in place",
	"[unit][export]")
{
	ScratchDir scratch("condition");
	const Orkige::String path = ExportFiles::join(scratch.path, "big.png");
	REQUIRE(encodePngFile(solid(64, 64, 200, 100, 50, 128), path, 0));

	Orkige::TextureImport import;
	import.base.maxSize = 16;
	import.base.premultiply = true;
	import.base.format = "none";
	writeSidecar(path, import);

	TextureCookResult result;
	Orkige::String error;
	REQUIRE(cookTexturePayload(scratch.path, "", "next", result, nullptr,
		&error));
	CHECK(result.cooked == 1);

	ExportImage cooked;
	REQUIRE(decodeImageFile(path, cooked, &error));
	CHECK(cooked.width == 16);
	CHECK(cooked.height == 16);
	// alpha folded into RGB, alpha itself preserved
	CHECK(static_cast<int>(cooked.pixels[3]) == 128);
	CHECK(static_cast<int>(cooked.pixels[0]) == 200 * 128 / 255);
}

TEST_CASE("per-platform overrides resolve", "[unit][export]")
{
	// the same sidecar cooks differently per platform slot: the base caps at
	// 32, android caps harder, web opts back out of the cap entirely
	auto cookFor = [](Orkige::String const & platform, ScratchDir & scratch)
	{
		const Orkige::String path = ExportFiles::join(scratch.path, "p.png");
		REQUIRE(encodePngFile(solid(64, 64, 10, 20, 30, 255), path, 0));
		Orkige::TextureImport import;
		import.base.maxSize = 32;
		import.base.format = "none";
		import.hasAndroid = true;
		import.android = import.base;
		import.android.maxSize = 8;
		import.hasWeb = true;
		import.web = import.base;
		import.web.maxSize = 0;
		writeSidecar(path, import);

		TextureCookResult result;
		Orkige::String error;
		REQUIRE(cookTexturePayload(scratch.path, platform, "next", result,
			nullptr, &error));
		ExportImage cooked;
		REQUIRE(decodeImageFile(path, cooked, &error));
		return cooked.width;
	};

	{
		ScratchDir scratch("plat_desktop");
		CHECK(cookFor("", scratch) == 32);
	}
	{
		ScratchDir scratch("plat_android");
		CHECK(cookFor("android", scratch) == 8);
	}
	{
		ScratchDir scratch("plat_web");
		CHECK(cookFor("web", scratch) == 64);	// the override lifts the cap
	}
}

TEST_CASE("a compressed texture replaces its source and keeps its sidecar",
	"[unit][export]")
{
	ScratchDir scratch("compress");
	const Orkige::String png = ExportFiles::join(scratch.path, "spr.png");
	// a gradient with alpha: desktop next resolves bc7
	ExportImage sprite(20, 12);
	for(int y = 0; y < 12; ++y)
	{
		for(int x = 0; x < 20; ++x)
		{
			const std::size_t index =
				(static_cast<std::size_t>(y) * 20 + x) * 4;
			sprite.pixels[index] = static_cast<unsigned char>(x * 12);
			sprite.pixels[index + 1] = static_cast<unsigned char>(y * 20);
			sprite.pixels[index + 2] = 128;
			sprite.pixels[index + 3] = static_cast<unsigned char>(255 - x);
		}
	}
	REQUIRE(encodePngFile(sprite, png, 0));

	Orkige::TextureImport import;
	import.base.generateMips = true;
	import.base.quality = "low";
	writeSidecar(png, import);

	TextureCookResult result;
	Orkige::String error;
	REQUIRE(cookTexturePayload(scratch.path, "", "next", result, nullptr,
		&error));
	CHECK(result.cooked == 1);

	const Orkige::String dds = ExportFiles::join(scratch.path, "spr.dds");
	CHECK(ExportFiles::isRegularFile(dds));
	CHECK_FALSE(ExportFiles::exists(png));
	// the sidecar travels WITH the texture: the id is how a scene reference
	// finds the shipped file, so leaving it on the vanished .png would ship a
	// scene that resolves nothing
	const Orkige::String metaExtension =
		Orkige::AssetDatabase::META_FILE_EXTENSION;
	CHECK(ExportFiles::isRegularFile(dds + metaExtension));
	CHECK_FALSE(ExportFiles::exists(png + metaExtension));
	Orkige::String movedId;
	REQUIRE(Orkige::AssetDatabase::readMetaFile(dds + metaExtension, movedId));
	CHECK(movedId == "testid0123456789");

	std::vector<unsigned char> bytes;
	REQUIRE(ExportFiles::readBytes(dds, bytes, &error));
	REQUIRE(bytes.size() > 128);
	CHECK((bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S'));
	// 20x12 with generateMips bakes 5 levels
	std::uint32_t mipCount = 0;
	std::memcpy(&mipCount, bytes.data() + 28, 4);
	CHECK(mipCount == 5);
}

TEST_CASE("an id-only sidecar cooks with the default settings",
	"[unit][export]")
{
	ScratchDir scratch("idonly");
	const Orkige::String png = ExportFiles::join(scratch.path, "raw.png");
	REQUIRE(encodePngFile(solid(20, 20, 1, 2, 3, 255), png, 0));
	writeIdOnlySidecar(png);

	// desktop + next: the default format "auto" compresses
	TextureCookResult result;
	Orkige::String error;
	REQUIRE(cookTexturePayload(scratch.path, "", "next", result, nullptr,
		&error));
	CHECK(result.cooked == 1);
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(scratch.path, "raw.dds")));
}

TEST_CASE("web auto ships the PNG untouched", "[unit][export]")
{
	ScratchDir scratch("webauto");
	const Orkige::String png = ExportFiles::join(scratch.path, "raw.png");
	REQUIRE(encodePngFile(solid(20, 20, 1, 2, 3, 255), png, 0));
	writeIdOnlySidecar(png);

	TextureCookResult result;
	Orkige::String error;
	REQUIRE(cookTexturePayload(scratch.path, "web", "classic", result, nullptr,
		&error));
	CHECK(result.cooked == 0);
	CHECK(ExportFiles::isRegularFile(png));
}

TEST_CASE("an impossible payload refuses and keeps its source",
	"[unit][export]")
{
	ScratchDir scratch("refuse");
	const Orkige::String png = ExportFiles::join(scratch.path, "bad.png");
	REQUIRE(encodePngFile(solid(20, 20, 1, 2, 3, 255), png, 0));
	Orkige::TextureImport import;
	import.base.format = "bc7";		// no BCn on a mobile GPU
	writeSidecar(png, import);

	TextureCookResult result;
	Orkige::String error;
	CHECK_FALSE(cookTexturePayload(scratch.path, "ios", "next", result,
		nullptr, &error));
	CHECK_FALSE(error.empty());
	// a refused export leaves the payload as it found it, never half-cooked
	CHECK(ExportFiles::isRegularFile(png));
}

TEST_CASE("a non-cubemap dds ships verbatim", "[unit][export]")
{
	ScratchDir scratch("flatdds");
	const Orkige::String dds = ExportFiles::join(scratch.path, "flat.dds");
	// a minimal uncompressed 2D .dds: final artwork, not something to re-cook
	std::vector<unsigned char> bytes(128 + 4 * 4 * 4, 0);
	bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
	bytes[4] = 124;
	REQUIRE(ExportFiles::writeBytes(dds, bytes, 0));
	writeIdOnlySidecar(dds);

	const unsigned long long before = ExportFiles::treeSize(dds);
	TextureCookResult result;
	Orkige::String error;
	REQUIRE(cookTexturePayload(scratch.path, "", "next", result, nullptr,
		&error));
	CHECK(result.cooked == 0);
	CHECK(ExportFiles::treeSize(dds) == before);
}

TEST_CASE("an uncompressed cubemap decodes into faces and its baked chain",
	"[unit][export]")
{
	ScratchDir scratch("cubedecode");
	const Orkige::String path = ExportFiles::join(scratch.path, "sky.dds");
	REQUIRE(ExportFiles::writeBytes(path, buildUncompressedCubemapDds(8), 0));

	DdsCubemap cube;
	REQUIRE(decodeDdsCubemap(path, cube));
	CHECK(cube.size == 8);
	CHECK(cube.mips == 4);				// 8 -> 4 -> 2 -> 1, READ not derived
	REQUIRE(cube.faces.size() == 6);
	for(std::size_t face = 0; face < 6; ++face)
	{
		REQUIRE(cube.faces[face].size() == 4);
		CHECK(cube.faces[face][0].size() == 8 * 8 * 4);
		CHECK(cube.faces[face][3].size() == 1 * 1 * 4);
		// the face order is preserved: face i carries its own marker
		CHECK(static_cast<int>(cube.faces[face][0][0]) ==
			static_cast<int>(face) * 40);
	}
	CHECK_FALSE(cubemapHasAlpha(cube));	// every texel is opaque
}

TEST_CASE("a cubemap cooks in place on desktop and renames on mobile",
	"[unit][export]")
{
	// desktop: the BC container reuses the .dds name, so the cook must not
	// clobber its own input mid-encode
	{
		ScratchDir scratch("cubedesktop");
		const Orkige::String dds = ExportFiles::join(scratch.path, "sky.dds");
		REQUIRE(ExportFiles::writeBytes(dds, buildUncompressedCubemapDds(8),
			0));
		writeIdOnlySidecar(dds);

		TextureCookResult result;
		Orkige::String error;
		REQUIRE(cookTexturePayload(scratch.path, "", "next", result, nullptr,
			&error));
		CHECK(result.cooked == 1);
		REQUIRE(ExportFiles::isRegularFile(dds));

		std::vector<unsigned char> bytes;
		REQUIRE(ExportFiles::readBytes(dds, bytes, &error));
		CHECK((bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S'));
		// the BAKED chain is the prefiltered roughness chain the
		// image-based-lighting samplers index - it is re-encoded level by
		// level, never regenerated or truncated
		std::uint32_t mips = 0;
		std::memcpy(&mips, bytes.data() + 28, 4);
		CHECK(mips == 4);
		std::uint32_t caps2 = 0;
		std::memcpy(&caps2, bytes.data() + 112, 4);
		CHECK(caps2 == 0xFE00u);
		// opaque desktop auto -> bc1 / DXT1
		CHECK((bytes[84] == 'D' && bytes[85] == 'X' && bytes[86] == 'T' &&
			bytes[87] == '1'));
		// the .cooking temp must not survive
		CHECK_FALSE(ExportFiles::exists(dds + ".cooking"));
	}

	// mobile: the container changes, so the file is renamed and its sidecar
	// goes with it
	{
		ScratchDir scratch("cubemobile");
		const Orkige::String dds = ExportFiles::join(scratch.path, "sky.dds");
		REQUIRE(ExportFiles::writeBytes(dds, buildUncompressedCubemapDds(8),
			0));
		writeIdOnlySidecar(dds);

		TextureCookResult result;
		Orkige::String error;
		REQUIRE(cookTexturePayload(scratch.path, "android", "next", result,
			nullptr, &error));
		CHECK(result.cooked == 1);
		const Orkige::String oitd = ExportFiles::join(scratch.path,
			"sky.oitd");
		CHECK(ExportFiles::isRegularFile(oitd));
		CHECK_FALSE(ExportFiles::exists(dds));
		const Orkige::String metaExtension =
			Orkige::AssetDatabase::META_FILE_EXTENSION;
		CHECK(ExportFiles::isRegularFile(oitd + metaExtension));
		CHECK_FALSE(ExportFiles::exists(dds + metaExtension));

		std::vector<unsigned char> bytes;
		REQUIRE(ExportFiles::readBytes(oitd, bytes, &error));
		CHECK((bytes[0] == 'O' && bytes[1] == 'I' && bytes[2] == 'T' &&
			bytes[3] == 'D'));
		CHECK(bytes[4 + 13] == 5);		// TypeCube
	}

	// web auto ships the cubemap verbatim
	{
		ScratchDir scratch("cubeweb");
		const Orkige::String dds = ExportFiles::join(scratch.path, "sky.dds");
		REQUIRE(ExportFiles::writeBytes(dds, buildUncompressedCubemapDds(8),
			0));
		writeIdOnlySidecar(dds);
		const unsigned long long before = ExportFiles::treeSize(dds);

		TextureCookResult result;
		Orkige::String error;
		REQUIRE(cookTexturePayload(scratch.path, "web", "classic", result,
			nullptr, &error));
		CHECK(result.cooked == 0);
		CHECK(ExportFiles::treeSize(dds) == before);
	}
}
