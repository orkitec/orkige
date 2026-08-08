/**************************************************************
	created:	2026/07/10 at 16:00
	filename: 	MemorySamplerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file MemorySamplerTests.cpp
//! @brief unit coverage for the process memory sampler: on every platform the
//! test suite runs (macOS, Linux) the OS query must return a plausible,
//! positive resident set size that grows when the process allocates and holds
//! a large block. The sampler is the seam the runtime memory-stats feature
//! rests on, so this pins the "is the game growing?" signal at the source.

#include <catch2/catch_test_macros.hpp>

#include "core_debug/MemorySampler.h"

#include <cstddef>
#include <memory>
#include <vector>

using namespace Orkige;

TEST_CASE("MemorySampler reports a positive resident set size", "[unit][memory]")
{
	const std::size_t bytes = MemorySampler::residentBytes();
	// the test binary is already resident in RAM, so a working query must
	// report a non-trivial footprint (well over 1 MB); a 0 would mean the
	// platform query is missing or failed
	REQUIRE(bytes > 1024u * 1024u);
}

TEST_CASE("MemorySampler tracks growth when memory is committed",
	"[unit][memory]")
{
	const std::size_t before = MemorySampler::residentBytes();
	REQUIRE(before > 0u);

	// commit a large block and touch every page so it becomes resident (not
	// just reserved); keep it alive across the second sample
	const std::size_t blockBytes = 64u * 1024u * 1024u;	// 64 MB
	std::vector<unsigned char> block(blockBytes, 0);
	for (std::size_t i = 0; i < blockBytes; i += 4096u)
	{
		block[i] = static_cast<unsigned char>(i);
	}

	const std::size_t after = MemorySampler::residentBytes();
#if defined(__EMSCRIPTEN__)
	// the wasm "resident" analog is the linear-memory size, which only ever
	// grows: when an earlier allocation peak already grew the heap, a fresh
	// block reuses freed space and the size legitimately stays flat - so
	// growth cannot be asserted, only that the footprint never shrinks and
	// is large enough to hold the committed block
	REQUIRE(after >= before);
	REQUIRE(after > blockBytes);
#else
	// the resident footprint must have grown by a meaningful fraction of the
	// committed block (allow slack for allocator/OS accounting)
	REQUIRE(after > before + blockBytes / 2u);
#endif

	// keep the block observable so the compiler cannot elide the commit
	REQUIRE(block.front() == 0);
}
