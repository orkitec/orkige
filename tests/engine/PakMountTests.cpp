/**************************************************************
	created:	2026/07/18 at 06:30
	filename: 	PakMountTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless pak-mount unit tests: the pure mount-point -> sub-tree prefix
	resolver (PakMount::normalizeMountPoint) and the small zip reader
	(MiniZip) that backs the mount on BOTH render flavors - STORED read in
	place, DEFLATE inflated, nested paths, missing entries. No renderer.
	The whole-mount contract (mount -> resource resolution -> scene/sound
	from the pak) is proven by the player_pak_selfcheck integration run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "engine_filesystem/MiniZip.h"
#include "engine_filesystem/PakMount.h"

#include <string>
#include <vector>

TEST_CASE("PakMountNormalizeTest", "[filesystem]")
{
	using Orkige::PakMount::normalizeMountPoint;

	// empty / root-only mount points mean "the whole zip" - no prefix
	CHECK(normalizeMountPoint("") == "");
	CHECK(normalizeMountPoint("/") == "");
	CHECK(normalizeMountPoint("///") == "");

	// a bare name gains a trailing slash so it matches a directory prefix
	CHECK(normalizeMountPoint("game") == "game/");
	CHECK(normalizeMountPoint("game/") == "game/");
	CHECK(normalizeMountPoint("assets") == "assets/");
	CHECK(normalizeMountPoint("assets/") == "assets/");

	// nested + noise: leading "./", leading "/", backslashes
	CHECK(normalizeMountPoint("./game/") == "game/");
	CHECK(normalizeMountPoint("/assets") == "assets/");
	CHECK(normalizeMountPoint("a\\b") == "a/b/");
	CHECK(normalizeMountPoint("assets/data") == "assets/data/");
}

namespace
{
	std::string asString(std::vector<unsigned char> const & bytes)
	{
		return std::string(bytes.begin(), bytes.end());
	}
}

TEST_CASE("MiniZipReadTest", "[filesystem]")
{
	Orkige::MiniZip zip;
	REQUIRE(zip.open(ORKIGE_MINIZIP_TEST_ZIP));
	REQUIRE(zip.isOpen());

	// the three entries the CMake configure step packed (one STORED, two
	// DEFLATE incl. a nested path) - directory entries are not listed
	REQUIRE(zip.names().size() == 3);
	CHECK(zip.contains("stored.txt"));
	CHECK(zip.contains("deflate.txt"));
	CHECK(zip.contains("sub/nested.bin"));
	CHECK_FALSE(zip.contains("missing.dat"));

	// STORED: read in place, byte-for-byte
	std::vector<unsigned char> stored;
	REQUIRE(zip.read("stored.txt", stored));
	CHECK(asString(stored) == "stored payload contents 12345");

	// DEFLATE: inflated back to the original 4096 'x'
	std::vector<unsigned char> deflate;
	REQUIRE(zip.read("deflate.txt", deflate));
	CHECK(deflate.size() == 4096);
	CHECK(deflate.front() == 'x');
	CHECK(deflate.back() == 'x');

	// a nested DEFLATE binary payload (0..255) round-trips exactly
	std::vector<unsigned char> nested;
	REQUIRE(zip.read("sub/nested.bin", nested));
	REQUIRE(nested.size() == 256);
	for (int i = 0; i < 256; ++i)
	{
		CHECK(nested[static_cast<std::size_t>(i)] ==
			static_cast<unsigned char>(i));
	}

	// sizeOf reports the uncompressed size; a missing read fails cleanly
	std::uint64_t size = 0;
	CHECK(zip.sizeOf("deflate.txt", size));
	CHECK(size == 4096);
	std::vector<unsigned char> missing;
	CHECK_FALSE(zip.read("missing.dat", missing));
}

TEST_CASE("MiniZipRejectsNonZipTest", "[filesystem]")
{
	Orkige::MiniZip zip;
	// a file that is not a zip (this source file) has no central directory
	CHECK_FALSE(zip.open(ORKIGE_MINIZIP_TEST_SRC));
	CHECK_FALSE(zip.isOpen());
	// a path that does not exist
	CHECK_FALSE(zip.open("/no/such/pak.zip"));
}
