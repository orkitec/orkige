/********************************************************************
	created:	Friday 2026/07/31 at 14:00
	filename: 	TextureEncodeTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	The GPU texture encoder's structural contract: every format encodes a
	deliberately NON-block-aligned gradient (partial edge blocks must round up,
	never truncate) and every container it can ship in carries the right magic,
	header fields and payload sizes.

	The cubemap round trip is the one that would fail silently: a skybox's
	prefiltered roughness chain is what the image-based-lighting samplers
	index, so all six faces AND the complete per-face mip chain have to survive
	into each container - a dropped face or a shortened chain still produces a
	loadable file, just a wrong-looking sky.

	The runtime-load proof (a cooked texture read back through the real
	runtime) is the player_cooked_textures ctest.
*/
#include <catch2/catch_test_macros.hpp>

#include "TextureEncode.h"

#include <cstring>
#include <string>
#include <vector>

using namespace OrkigeExport;

namespace
{
	//! a gradient level chain, indexed [level][face], one face
	TextureLevels gradientLevels(int width, int height, int levelCount)
	{
		TextureLevels levels;
		for(int level = 0; level < levelCount; ++level)
		{
			const int levelWidth = TextureEncode::levelDimension(width, level);
			const int levelHeight =
				TextureEncode::levelDimension(height, level);
			std::vector<unsigned char> pixels;
			pixels.reserve(
				static_cast<std::size_t>(levelWidth) * levelHeight * 4);
			for(int y = 0; y < levelHeight; ++y)
			{
				for(int x = 0; x < levelWidth; ++x)
				{
					pixels.push_back(
						static_cast<unsigned char>(x * 255 / levelWidth));
					pixels.push_back(
						static_cast<unsigned char>(y * 255 / levelHeight));
					pixels.push_back(128);
					pixels.push_back(static_cast<unsigned char>(255 - x));
				}
			}
			levels.push_back({ pixels });
		}
		return levels;
	}

	//! six flat-coloured faces (a distinct colour per face, so a decode could
	//! tell them apart; flat blocks stay crisp through BC/ASTC/ETC2)
	TextureLevels cubeLevels(int size, int levelCount)
	{
		TextureLevels levels(static_cast<std::size_t>(levelCount));
		for(int level = 0; level < levelCount; ++level)
		{
			const int dimension = TextureEncode::levelDimension(size, level);
			for(int face = 0; face < 6; ++face)
			{
				std::vector<unsigned char> pixels;
				pixels.reserve(
					static_cast<std::size_t>(dimension) * dimension * 4);
				for(int texel = 0; texel < dimension * dimension; ++texel)
				{
					pixels.push_back(static_cast<unsigned char>(face * 40));
					pixels.push_back(128);
					pixels.push_back(
						static_cast<unsigned char>(255 - face * 30));
					pixels.push_back(255);
				}
				levels[static_cast<std::size_t>(level)].push_back(pixels);
			}
		}
		return levels;
	}

	//! one face's whole encoded mip chain, in bytes
	std::size_t faceChainBytes(TextureFormatInfo const & info, int size,
		int levelCount)
	{
		std::size_t total = 0;
		for(int level = 0; level < levelCount; ++level)
		{
			const int dimension = TextureEncode::levelDimension(size, level);
			total += TextureEncode::blockDataSize(info, dimension, dimension);
		}
		return total;
	}

	std::uint32_t readU32(std::vector<unsigned char> const & bytes,
		std::size_t offset)
	{
		std::uint32_t value = 0;
		std::memcpy(&value, bytes.data() + offset, 4);
		return value;
	}
}

TEST_CASE("the format table answers by token", "[unit][export][texcook]")
{
	// the vocabulary the sidecar's explicit format tokens name
	CHECK(TextureEncode::formatCount() == 8);
	for(const char * token : { "bc1", "bc3", "bc7", "etc2-rgb", "etc2-rgba",
		"astc-4x4", "astc-6x6", "astc-8x8" })
	{
		INFO(token);
		CHECK(TextureEncode::findFormat(token) != 0);
	}
	CHECK(TextureEncode::findFormat("nonsense") == 0);
	CHECK(TextureEncode::findFormat("") == 0);
}

TEST_CASE("block sizes round partial edge blocks up", "[unit][export][texcook]")
{
	TextureFormatInfo const * bc1 = TextureEncode::findFormat("bc1");
	REQUIRE(bc1 != 0);
	// 4x4 blocks, 8 bytes each: a 20x12 image is 5x3 blocks
	CHECK(TextureEncode::blockDataSize(*bc1, 20, 12) == 5 * 3 * 8);
	// a partial edge block still costs a whole block - truncating it would
	// drop the right-hand column of texels
	CHECK(TextureEncode::blockDataSize(*bc1, 17, 12) == 5 * 3 * 8);
	CHECK(TextureEncode::blockDataSize(*bc1, 1, 1) == 8);

	TextureFormatInfo const * astc = TextureEncode::findFormat("astc-8x8");
	REQUIRE(astc != 0);
	CHECK(TextureEncode::blockDataSize(*astc, 16, 16) == 2 * 2 * 16);
	CHECK(TextureEncode::blockDataSize(*astc, 17, 16) == 3 * 2 * 16);

	CHECK(TextureEncode::levelDimension(20, 0) == 20);
	CHECK(TextureEncode::levelDimension(20, 1) == 10);
	// a chain never runs below 1 texel
	CHECK(TextureEncode::levelDimension(1, 4) == 1);
}

TEST_CASE("formats declare which containers can carry them",
	"[unit][export][texcook]")
{
	// BCn ships as .dds on both flavors; the mobile families ship in KTX1
	// (classic) or the Ogre-Next native container
	CHECK(TextureEncode::fitsContainer(*TextureEncode::findFormat("bc1"),
		"dds"));
	CHECK(TextureEncode::fitsContainer(*TextureEncode::findFormat("bc7"),
		"dds"));	// through the DX10 extension header
	CHECK_FALSE(TextureEncode::fitsContainer(
		*TextureEncode::findFormat("bc1"), "ktx"));
	CHECK(TextureEncode::fitsContainer(*TextureEncode::findFormat("etc2-rgb"),
		"ktx"));
	CHECK(TextureEncode::fitsContainer(*TextureEncode::findFormat("astc-4x4"),
		"ktx"));
	// .oitd carries any GPU format
	CHECK(TextureEncode::fitsContainer(*TextureEncode::findFormat("bc1"),
		"oitd"));
	CHECK_FALSE(TextureEncode::fitsContainer(
		*TextureEncode::findFormat("bc1"), "zip"));
}

TEST_CASE("the cook validates its shape before encoding",
	"[unit][export][texcook]")
{
	std::string error;
	CHECK(TextureEncode::validate("bc1", "normal", 20, 12, 2, 1, "dds", 0));

	CHECK_FALSE(TextureEncode::validate("nonsense", "normal", 20, 12, 2, 1,
		"dds", &error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(TextureEncode::validate("bc1", "normal", 20, 12, 2, 1, "ktx",
		&error));
	CHECK_FALSE(TextureEncode::validate("bc1", "extreme", 20, 12, 2, 1, "dds",
		&error));
	CHECK_FALSE(TextureEncode::validate("bc1", "normal", 0, 12, 2, 1, "dds",
		&error));
	CHECK_FALSE(TextureEncode::validate("bc1", "normal", 20, 12, 0, 1, "dds",
		&error));
	// a cubemap is 6 square faces, nothing else
	CHECK_FALSE(TextureEncode::validate("bc1", "normal", 20, 12, 2, 3, "dds",
		&error));
	CHECK_FALSE(TextureEncode::validate("bc1", "normal", 20, 12, 2, 6, "dds",
		&error));
	CHECK(TextureEncode::validate("bc1", "normal", 16, 16, 2, 6, "dds", 0));
}

TEST_CASE("every format encodes a non-block-aligned image",
	"[unit][export][texcook]")
{
	const int width = 20;	// deliberately NOT a block multiple
	const int height = 12;
	const TextureLevels rgba = gradientLevels(width, height, 2);

	for(int index = 0; index < TextureEncode::formatCount(); ++index)
	{
		TextureFormatInfo const & info = TextureEncode::formats()[index];
		INFO(info.token);
		TextureLevels encoded;
		std::string error;
		REQUIRE(TextureEncode::encodeLevels(info, "low", width, height, 1,
			rgba, encoded, &error));
		REQUIRE(encoded.size() == 2);
		CHECK(encoded[0].size() == 1);
		CHECK(encoded[1].size() == 1);
		CHECK(encoded[0][0].size() ==
			TextureEncode::blockDataSize(info, width, height));
		CHECK(encoded[1][0].size() ==
			TextureEncode::blockDataSize(info, width / 2, height / 2));

		if(TextureEncode::fitsContainer(info, "dds"))
		{
			std::vector<unsigned char> dds;
			REQUIRE(TextureEncode::buildContainer("dds", info, width, height,
				encoded, dds, &error));
			CHECK(dds.size() >= 128);
			CHECK(std::memcmp(dds.data(), "DDS ", 4) == 0);
		}
		if(TextureEncode::fitsContainer(info, "ktx"))
		{
			std::vector<unsigned char> ktx;
			REQUIRE(TextureEncode::buildContainer("ktx", info, width, height,
				encoded, ktx, &error));
			CHECK(ktx.size() > 64 + encoded[0][0].size());
			CHECK(std::memcmp(ktx.data() + 1, "KTX 11", 6) == 0);

			std::vector<unsigned char> oitd;
			REQUIRE(TextureEncode::buildContainer("oitd", info, width, height,
				encoded, oitd, &error));
			CHECK(std::memcmp(oitd.data(), "OITD", 4) == 0);
			CHECK(oitd.size() == 4 + 17 + encoded[0][0].size() +
				encoded[1][0].size());
		}
	}
}

TEST_CASE("a cubemap keeps all six faces and its whole chain",
	"[unit][export][texcook]")
{
	const int size = 8;			// square, power-of-two: a clean chain
	const int mips = 4;			// 8 -> 4 -> 2 -> 1
	const TextureLevels cube = cubeLevels(size, mips);

	for(const char * token : { "bc1", "astc-4x4", "etc2-rgb" })
	{
		INFO(token);
		TextureFormatInfo const * info = TextureEncode::findFormat(token);
		REQUIRE(info != 0);
		TextureLevels encoded;
		std::string error;
		REQUIRE(TextureEncode::encodeLevels(*info, "low", size, size, 6, cube,
			encoded, &error));
		REQUIRE(encoded.size() == static_cast<std::size_t>(mips));
		for(std::vector<std::vector<unsigned char> > const & level : encoded)
		{
			CHECK(level.size() == 6);
		}

		if(TextureEncode::fitsContainer(*info, "dds"))
		{
			std::vector<unsigned char> dds;
			REQUIRE(TextureEncode::buildContainer("dds", *info, size, size,
				encoded, dds, &error));
			// caps2 (file offset 4 + 108) must carry CUBEMAP + all six faces
			CHECK(readU32(dds, 4 + 108) == 0xFE00u);
			// and the payload must be six COMPLETE chains
			CHECK(dds.size() == 4 + 124 + 6 * faceChainBytes(*info, size, mips));
		}
		if(TextureEncode::fitsContainer(*info, "ktx"))
		{
			std::vector<unsigned char> oitd;
			REQUIRE(TextureEncode::buildContainer("oitd", *info, size, size,
				encoded, oitd, &error));
			CHECK(oitd[4 + 13] == 5);					// TypeCube
			CHECK(readU32(oitd, 4 + 8) == 6u);			// depthOrSlices
			CHECK(oitd.size() == 4 + 17 + 6 * faceChainBytes(*info, size, mips));

			std::vector<unsigned char> ktx;
			REQUIRE(TextureEncode::buildContainer("ktx", *info, size, size,
				encoded, ktx, &error));
			CHECK(readU32(ktx, 12 + 10 * 4) == 6u);		// numberOfFaces
		}
	}
}

TEST_CASE("the raw level stream transposes face-major to level-major",
	"[unit][export][texcook]")
{
	// two faces' worth of a 2x2 base with one mip: the reader must land each
	// face's chain in the [level][face] indexing the encoder wants
	const int size = 2;
	const int levels = 2;
	std::vector<unsigned char> stream;
	for(int face = 0; face < 6; ++face)
	{
		for(int level = 0; level < levels; ++level)
		{
			const int dimension = TextureEncode::levelDimension(size, level);
			for(int texel = 0; texel < dimension * dimension * 4; ++texel)
			{
				// each face's bytes carry its own index, so a mis-transpose
				// shows up as the wrong face
				stream.push_back(static_cast<unsigned char>(face));
			}
		}
	}
	TextureLevels out;
	std::string error;
	REQUIRE(TextureEncode::takeRgbaLevels(stream.data(), stream.size(), size,
		size, levels, 6, out, &error));
	REQUIRE(out.size() == 2);
	REQUIRE(out[0].size() == 6);
	CHECK(out[0][0][0] == 0);
	CHECK(out[0][3][0] == 3);
	CHECK(out[1][5][0] == 5);
	CHECK(out[0][0].size() == 2 * 2 * 4);
	CHECK(out[1][0].size() == 1 * 1 * 4);

	// a short stream refuses rather than encoding uninitialised memory
	error.clear();
	CHECK_FALSE(TextureEncode::takeRgbaLevels(stream.data(), stream.size() - 1,
		size, size, levels, 6, out, &error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("the whole cook produces a container in one call",
	"[unit][export][texcook]")
{
	const TextureLevels rgba = gradientLevels(16, 16, 1);
	std::vector<unsigned char> file;
	std::string error;
	REQUIRE(TextureEncode::encodeToContainer("bc1", "low", 16, 16, 1, rgba,
		"dds", file, &error));
	CHECK(std::memcmp(file.data(), "DDS ", 4) == 0);

	// an impossible pair refuses before any encoding happens
	error.clear();
	CHECK_FALSE(TextureEncode::encodeToContainer("bc1", "low", 16, 16, 1, rgba,
		"ktx", file, &error));
	CHECK_FALSE(error.empty());
}
