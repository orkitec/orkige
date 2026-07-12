/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	MemoryManagerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit coverage for the tagged per-frame allocation counters:
	tag isolation, the frame-boundary fold/reset, per-frame peak tracking,
	the growth probe, and the thread-merge contract (worker threads count
	into the same relaxed atomics). Counter overhead is LOGGED, never
	gated - correctness only, no wall-clock thresholds.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_debug/MemoryManager.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace Orkige;

TEST_CASE("MemoryManager: tags count in isolation", "[unit][memory][perf]")
{
	MemoryManager::reset();
	MemoryManager::countAlloc(MemoryManager::TAG_EVENTS, 3);
	MemoryManager::countAlloc(MemoryManager::TAG_PHYSICS);
	REQUIRE(MemoryManager::currentCount(MemoryManager::TAG_EVENTS) == 3);
	REQUIRE(MemoryManager::currentCount(MemoryManager::TAG_PHYSICS) == 1);
	REQUIRE(MemoryManager::currentCount(MemoryManager::TAG_GUI) == 0);
	REQUIRE(MemoryManager::currentCount(MemoryManager::TAG_TWEENS) == 0);
	// nothing folded yet - the last-frame snapshot is untouched
	REQUIRE(MemoryManager::framesSampled() == 0);
	REQUIRE(MemoryManager::lastFrameTotal() == 0);
	MemoryManager::reset();
}

TEST_CASE("MemoryManager: endFrame folds, resets and tracks the peak",
	"[unit][memory][perf]")
{
	MemoryManager::reset();
	// frame 1: 5 event counts, 2 gui counts
	MemoryManager::countAlloc(MemoryManager::TAG_EVENTS, 5);
	MemoryManager::countAlloc(MemoryManager::TAG_GUI, 2);
	MemoryManager::endFrame();
	REQUIRE(MemoryManager::framesSampled() == 1);
	REQUIRE(MemoryManager::lastFrameCount(MemoryManager::TAG_EVENTS) == 5);
	REQUIRE(MemoryManager::lastFrameCount(MemoryManager::TAG_GUI) == 2);
	REQUIRE(MemoryManager::lastFrameTotal() == 7);
	REQUIRE(MemoryManager::peakFrameTotal() == 7);
	// the running counts were reset by the fold
	REQUIRE(MemoryManager::currentCount(MemoryManager::TAG_EVENTS) == 0);
	// frame 2: quiet - last-frame drops to zero, the peak stays
	MemoryManager::endFrame();
	REQUIRE(MemoryManager::framesSampled() == 2);
	REQUIRE(MemoryManager::lastFrameTotal() == 0);
	REQUIRE(MemoryManager::lastFrameCount(MemoryManager::TAG_EVENTS) == 0);
	REQUIRE(MemoryManager::peakFrameTotal() == 7);
	// frame 3: louder - the peak follows
	MemoryManager::countAlloc(MemoryManager::TAG_PARTICLES, 11);
	MemoryManager::endFrame();
	REQUIRE(MemoryManager::lastFrameTotal() == 11);
	REQUIRE(MemoryManager::peakFrameTotal() == 11);
	MemoryManager::reset();
	REQUIRE(MemoryManager::peakFrameTotal() == 0);
	REQUIRE(MemoryManager::framesSampled() == 0);
}

TEST_CASE("MemoryManager: the growth probe counts only capacity changes",
	"[unit][memory][perf]")
{
	MemoryManager::reset();
	std::vector<int> grower;
	grower.reserve(4);
	for (int i = 0; i < 4; ++i)
	{
		const std::size_t before = grower.capacity();
		grower.push_back(i);
		MemoryManager::countGrowth(MemoryManager::TAG_TWEENS,
			before, grower.capacity());
	}
	// four pushes into reserved space: zero growth events
	REQUIRE(MemoryManager::currentCount(MemoryManager::TAG_TWEENS) == 0);
	const std::size_t before = grower.capacity();
	grower.push_back(4);	// the fifth push reallocates
	MemoryManager::countGrowth(MemoryManager::TAG_TWEENS,
		before, grower.capacity());
	REQUIRE(MemoryManager::currentCount(MemoryManager::TAG_TWEENS) == 1);
	MemoryManager::reset();
}

TEST_CASE("MemoryManager: worker-thread counts merge into the frame",
	"[unit][memory][perf]")
{
	MemoryManager::reset();
	const int threadCount = 4;
	const std::size_t countsPerThread = 10000;
	std::vector<std::thread> workers;
	for (int i = 0; i < threadCount; ++i)
	{
		workers.emplace_back([countsPerThread]()
		{
			for (std::size_t n = 0; n < countsPerThread; ++n)
			{
				MemoryManager::countAlloc(MemoryManager::TAG_PHYSICS);
			}
		});
	}
	for (std::thread & worker : workers)
	{
		worker.join();
	}
	MemoryManager::endFrame();
	REQUIRE(MemoryManager::lastFrameCount(MemoryManager::TAG_PHYSICS) ==
		threadCount * countsPerThread);
	REQUIRE(MemoryManager::lastFrameTotal() ==
		threadCount * countsPerThread);
	MemoryManager::reset();
}

TEST_CASE("MemoryManager: counter cost is logged, not gated",
	"[unit][memory][perf]")
{
	MemoryManager::reset();
	const std::size_t iterations = 1000000;
	const auto start = std::chrono::steady_clock::now();
	for (std::size_t i = 0; i < iterations; ++i)
	{
		MemoryManager::countAlloc(MemoryManager::TAG_OTHER);
	}
	const auto end = std::chrono::steady_clock::now();
	const double totalNs = static_cast<double>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			end - start).count());
	// the deliverable is the printed number; the assertion is correctness
	std::printf("[memory][perf] countAlloc: %.1f ns/call over %zu calls\n",
		totalNs / static_cast<double>(iterations), iterations);
	REQUIRE(MemoryManager::currentCount(MemoryManager::TAG_OTHER) ==
		iterations);
	MemoryManager::reset();
}
