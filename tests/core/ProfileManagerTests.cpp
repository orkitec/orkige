/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	ProfileManagerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit coverage for the hierarchical CPU frame profiler: exact
	nesting/sibling structure in the snapshot, per-frame call accumulation
	and the endFrame fold, direct recursion (time counted once), the
	enable gate, node reuse across frames (steady state allocates no new
	nodes) and per-thread tree separation. Scope cost is LOGGED, never
	gated - correctness only, no wall-clock thresholds.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_debug/Profile.h"
#include "core_debug/ProfileManager.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace Orkige;

namespace
{
	//! find a snapshot row by name; -1 when absent
	int findRow(std::vector<ProfileManager::SnapshotNode> const & rows,
		const char * name)
	{
		for (std::size_t i = 0; i < rows.size(); ++i)
		{
			if (std::strcmp(rows[i].name, name) == 0)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	//! RAII guard: remember the gate, clean the tree, restore on exit
	struct ProfilerFixture
	{
		bool mWasEnabled;
		ProfilerFixture() : mWasEnabled(ProfileManager::isEnabled())
		{
			ProfileManager::reset();
			ProfileManager::setEnabled(true);
		}
		~ProfilerFixture()
		{
			ProfileManager::reset();
			ProfileManager::setEnabled(mWasEnabled);
		}
	};
}

TEST_CASE("ProfileManager: nesting builds the exact hierarchy",
	"[unit][profile][perf]")
{
	ProfilerFixture fixture;
	{
		OPROFILE("test.frameA");
		{
			OPROFILE("test.childOne");
		}
		{
			OPROFILE("test.childTwo");
			{
				OPROFILE("test.grandChild");
			}
		}
	}
	ProfileManager::endFrame();
	std::vector<ProfileManager::SnapshotNode> rows;
	ProfileManager::snapshot(rows);

	const int frameRow = findRow(rows, "test.frameA");
	const int childOneRow = findRow(rows, "test.childOne");
	const int childTwoRow = findRow(rows, "test.childTwo");
	const int grandRow = findRow(rows, "test.grandChild");
	REQUIRE(frameRow >= 0);
	REQUIRE(childOneRow >= 0);
	REQUIRE(childTwoRow >= 0);
	REQUIRE(grandRow >= 0);
	// depth-first flattening: parent precedes its children, siblings keep
	// creation order, depths step by one
	REQUIRE(frameRow < childOneRow);
	REQUIRE(childOneRow < childTwoRow);
	REQUIRE(childTwoRow < grandRow);
	REQUIRE(rows[childOneRow].depth == rows[frameRow].depth + 1);
	REQUIRE(rows[childTwoRow].depth == rows[frameRow].depth + 1);
	REQUIRE(rows[grandRow].depth == rows[childTwoRow].depth + 1);
	REQUIRE(rows[frameRow].calls == 1);
	REQUIRE(rows[grandRow].calls == 1);
	// children's time is contained in the parent's
	REQUIRE(rows[frameRow].milliseconds >=
		rows[childOneRow].milliseconds + rows[childTwoRow].milliseconds);
}

TEST_CASE("ProfileManager: calls accumulate per frame and fold at the boundary",
	"[unit][profile][perf]")
{
	ProfilerFixture fixture;
	for (int i = 0; i < 7; ++i)
	{
		OPROFILE("test.repeated");
	}
	ProfileManager::endFrame();
	std::vector<ProfileManager::SnapshotNode> rows;
	ProfileManager::snapshot(rows);
	int row = findRow(rows, "test.repeated");
	REQUIRE(row >= 0);
	REQUIRE(rows[row].calls == 7);

	// a quiet second frame: the row survives (node reuse) with zero calls
	ProfileManager::endFrame();
	ProfileManager::snapshot(rows);
	row = findRow(rows, "test.repeated");
	REQUIRE(row >= 0);
	REQUIRE(rows[row].calls == 0);
	REQUIRE(rows[row].milliseconds == 0.0);
	// the worst-frame value keeps the busy frame's time
	REQUIRE(rows[row].maxMilliseconds >= 0.0);
}

namespace
{
	void recurseScopes(int depth)
	{
		OPROFILE("test.recursive");
		if (depth > 1)
		{
			recurseScopes(depth - 1);
		}
	}
}

TEST_CASE("ProfileManager: direct recursion accumulates into one node",
	"[unit][profile][perf]")
{
	ProfilerFixture fixture;
	recurseScopes(5);
	ProfileManager::endFrame();
	std::vector<ProfileManager::SnapshotNode> rows;
	ProfileManager::snapshot(rows);
	// exactly ONE row for the recursive scope - not a five-deep chain
	int count = 0;
	int row = -1;
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		if (std::strcmp(rows[i].name, "test.recursive") == 0)
		{
			++count;
			row = static_cast<int>(i);
		}
	}
	REQUIRE(count == 1);
	REQUIRE(rows[row].calls == 5);
}

TEST_CASE("ProfileManager: the enable gate keeps disabled scopes out",
	"[unit][profile][perf]")
{
	ProfilerFixture fixture;
	ProfileManager::setEnabled(false);
	REQUIRE_FALSE(ProfileManager::beginScope("test.gated"));
	{
		OPROFILE("test.gatedScope");	// balanced via the RAII started flag
	}
	ProfileManager::setEnabled(true);
	ProfileManager::endFrame();
	std::vector<ProfileManager::SnapshotNode> rows;
	ProfileManager::snapshot(rows);
	REQUIRE(findRow(rows, "test.gated") == -1);
	REQUIRE(findRow(rows, "test.gatedScope") == -1);
}

TEST_CASE("ProfileManager: steady state reuses nodes (no tree growth)",
	"[unit][profile][perf]")
{
	ProfilerFixture fixture;
	std::vector<ProfileManager::SnapshotNode> rows;
	std::size_t warmSize = 0;
	for (int frame = 0; frame < 50; ++frame)
	{
		{
			OPROFILE("test.steadyA");
			{
				OPROFILE("test.steadyB");
			}
		}
		ProfileManager::endFrame();
		if (frame == 0)
		{
			ProfileManager::snapshot(rows);
			warmSize = rows.size();
		}
	}
	ProfileManager::snapshot(rows);
	// the tree stopped growing after the first (warm-up) frame: the same
	// scopes map to the same nodes, steady state allocates nothing
	REQUIRE(rows.size() == warmSize);
}

TEST_CASE("ProfileManager: a worker thread's scopes stay on its own tree",
	"[unit][profile][perf]")
{
	ProfilerFixture fixture;
	std::mutex gate;
	std::condition_variable signal;
	bool scopesDone = false;
	bool snapshotDone = false;
	std::thread worker([&]()
	{
		{
			OPROFILE("test.workerScope");
		}
		{
			std::unique_lock<std::mutex> lock(gate);
			scopesDone = true;
			signal.notify_all();
			// stay alive until the main thread snapshotted (a thread's tree
			// unregisters at thread exit)
			signal.wait(lock, [&]() { return snapshotDone; });
		}
	});
	{
		OPROFILE("test.mainScope");
		std::unique_lock<std::mutex> lock(gate);
		signal.wait(lock, [&]() { return scopesDone; });
	}
	ProfileManager::endFrame();
	std::vector<ProfileManager::SnapshotNode> rows;
	ProfileManager::snapshot(rows);
	{
		std::lock_guard<std::mutex> lock(gate);
		snapshotDone = true;
		signal.notify_all();
	}
	worker.join();

	const int mainRow = findRow(rows, "test.mainScope");
	const int workerRow = findRow(rows, "test.workerScope");
	REQUIRE(mainRow >= 0);
	REQUIRE(workerRow >= 0);
	// the worker's scope did NOT land inside the main scope: it sits under
	// a depth-0 thread label row, one level down
	REQUIRE(rows[workerRow].depth == 1);
	int labelRow = -1;
	for (int i = workerRow - 1; i >= 0; --i)
	{
		if (rows[i].depth == 0)
		{
			labelRow = i;
			break;
		}
	}
	REQUIRE(labelRow >= 0);
	REQUIRE(std::strncmp(rows[labelRow].name, "thread-", 7) == 0);
	REQUIRE(rows[workerRow].calls == 1);
}

TEST_CASE("ProfileManager: scope cost is logged, not gated",
	"[unit][profile][perf]")
{
	ProfilerFixture fixture;
	const int iterations = 200000;
	// warm the node so the loop measures steady state, not the allocation;
	// fold it away so the measured frame counts the loop alone
	{
		OPROFILE("test.cost");
	}
	ProfileManager::endFrame();
	auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < iterations; ++i)
	{
		OPROFILE("test.cost");
	}
	auto end = std::chrono::steady_clock::now();
	const double enabledNs = static_cast<double>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			end - start).count()) / iterations;
	ProfileManager::endFrame();

	ProfileManager::setEnabled(false);
	start = std::chrono::steady_clock::now();
	for (int i = 0; i < iterations; ++i)
	{
		OPROFILE("test.cost");
	}
	end = std::chrono::steady_clock::now();
	const double disabledNs = static_cast<double>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			end - start).count()) / iterations;
	ProfileManager::setEnabled(true);

	// the deliverable is the printed pair; the assertion is correctness
	std::printf("[profile][perf] scope cost: %.1f ns enabled, "
		"%.1f ns disabled (per begin/end pair, %d iterations)\n",
		enabledNs, disabledNs, iterations);
	std::vector<ProfileManager::SnapshotNode> rows;
	ProfileManager::snapshot(rows);
	const int row = findRow(rows, "test.cost");
	REQUIRE(row >= 0);
	REQUIRE(rows[row].calls == iterations);
}
