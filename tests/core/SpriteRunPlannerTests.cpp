/**************************************************************
	created:	2026/07/17 at 14:00
	filename: 	SpriteRunPlannerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The pure grouping half of sprite-run batching: the painter's
	contract (stable zOrder order, contiguous same-material runs, no
	reorder across a material change) and the dirty tracking (an
	unmoved run never rebuilds; one moved member dirties only its own
	run). Headless - the planner never sees a renderer type.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/SpriteRunPlanner.h>

using Orkige::SpriteRunPlanner;

namespace
{
	SpriteRunPlanner::Item item(std::uint64_t id, char const * key,
		int zOrder, std::uint64_t hash = 1)
	{
		SpriteRunPlanner::Item result;
		result.id = id;
		result.materialKey = key;
		result.zOrder = zOrder;
		result.stateHash = hash;
		return result;
	}
}
//---------------------------------------------------------
TEST_CASE("Contiguous same-material sprites form one run per zOrder",
	"[spriterun]")
{
	SpriteRunPlanner planner;
	planner.plan({
		item(1, "A", 0), item(2, "A", 0), item(3, "A", 0),
		item(4, "A", 1), item(5, "A", 1),
	});
	REQUIRE(planner.runs().size() == 2);
	CHECK(planner.runs()[0].zOrder == 0);
	CHECK(planner.runs()[0].members == std::vector<std::uint64_t>{1, 2, 3});
	CHECK(planner.runs()[1].zOrder == 1);
	CHECK(planner.runs()[1].members == std::vector<std::uint64_t>{4, 5});
	CHECK(planner.solo().empty());
}
//---------------------------------------------------------
TEST_CASE("An interleaved different-material sprite breaks the run",
	"[spriterun]")
{
	// the painter's contract: draw order within a zOrder is registration
	// order - merging across the B sprite would reorder blended output
	SpriteRunPlanner planner;
	planner.plan({
		item(1, "A", 0), item(2, "A", 0), item(3, "B", 0),
		item(4, "A", 0), item(5, "A", 0), item(6, "A", 0),
	});
	REQUIRE(planner.runs().size() == 2);
	CHECK(planner.runs()[0].members == std::vector<std::uint64_t>{1, 2});
	CHECK(planner.runs()[1].members == std::vector<std::uint64_t>{4, 5, 6});
	// the lone B stays on its own quad
	CHECK(planner.solo() == std::vector<std::uint64_t>{3});
}
//---------------------------------------------------------
TEST_CASE("zOrder ties keep registration order and split by material",
	"[spriterun]")
{
	SpriteRunPlanner planner;
	planner.plan({
		item(1, "A", 0), item(2, "B", 0), item(3, "A", 0), item(4, "B", 0),
	});
	// alternating materials at one z: nothing merges, order preserved
	CHECK(planner.runs().empty());
	CHECK(planner.solo() == std::vector<std::uint64_t>{1, 2, 3, 4});
}
//---------------------------------------------------------
TEST_CASE("A single sprite never becomes a one-quad batch", "[spriterun]")
{
	SpriteRunPlanner planner;
	planner.plan({ item(7, "A", 3) });
	CHECK(planner.runs().empty());
	CHECK(planner.solo() == std::vector<std::uint64_t>{7});
}
//---------------------------------------------------------
TEST_CASE("Sorting by zOrder is stable across registration order",
	"[spriterun]")
{
	// registration order 5,6 at z=-1 must come out before 1,2 at z=0 and
	// keep 5 before 6 (stable)
	SpriteRunPlanner planner;
	planner.plan({
		item(1, "A", 0), item(2, "A", 0),
		item(5, "A", -1), item(6, "A", -1),
	});
	REQUIRE(planner.runs().size() == 2);
	CHECK(planner.runs()[0].members == std::vector<std::uint64_t>{5, 6});
	CHECK(planner.runs()[1].members == std::vector<std::uint64_t>{1, 2});
}
//---------------------------------------------------------
TEST_CASE("An unmoved run never rebuilds; one moved member dirties only "
	"its own run", "[spriterun][dirty]")
{
	SpriteRunPlanner planner;
	const std::vector<SpriteRunPlanner::Item> frame1 = {
		item(1, "A", 0, 11), item(2, "A", 0, 12),
		item(3, "A", 1, 13), item(4, "A", 1, 14),
	};
	planner.plan(frame1);
	REQUIRE(planner.runs().size() == 2);
	CHECK(planner.runs()[0].needsRebuild);	// first plan uploads everything
	CHECK(planner.runs()[1].needsRebuild);

	// identical frame: nothing rebuilds
	planner.plan(frame1);
	CHECK_FALSE(planner.runs()[0].needsRebuild);
	CHECK_FALSE(planner.runs()[1].needsRebuild);

	// the dirty boundary: member 3 moves (hash changes) - ONLY its run
	// rebuilds, the untouched neighbours' run stays as uploaded
	planner.plan({
		item(1, "A", 0, 11), item(2, "A", 0, 12),
		item(3, "A", 1, 99), item(4, "A", 1, 14),
	});
	CHECK_FALSE(planner.runs()[0].needsRebuild);
	CHECK(planner.runs()[1].needsRebuild);
}
//---------------------------------------------------------
TEST_CASE("Membership changes dirty the affected run", "[spriterun][dirty]")
{
	SpriteRunPlanner planner;
	planner.plan({ item(1, "A", 0, 1), item(2, "A", 0, 2), item(3, "A", 0, 3) });
	planner.plan({ item(1, "A", 0, 1), item(2, "A", 0, 2), item(3, "A", 0, 3) });
	CHECK_FALSE(planner.runs()[0].needsRebuild);
	// a member leaves - the run must re-upload without it
	planner.plan({ item(1, "A", 0, 1), item(3, "A", 0, 3) });
	REQUIRE(planner.runs().size() == 1);
	CHECK(planner.runs()[0].members == std::vector<std::uint64_t>{1, 3});
	CHECK(planner.runs()[0].needsRebuild);
}
//---------------------------------------------------------
TEST_CASE("reset() forgets the previous plan (the batching re-enable path)",
	"[spriterun][dirty]")
{
	SpriteRunPlanner planner;
	const std::vector<SpriteRunPlanner::Item> frame = {
		item(1, "A", 0, 1), item(2, "A", 0, 2),
	};
	planner.plan(frame);
	planner.plan(frame);
	CHECK_FALSE(planner.runs()[0].needsRebuild);
	planner.reset();
	planner.plan(frame);
	CHECK(planner.runs()[0].needsRebuild);
}
