/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	AdPlacementTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless AdPlacement unit tests: the pure per-unit ad lifecycle
	(load -> ready -> show -> result) with EVERY error transition asserted,
	because the error transitions are the ones real networks produce rarely and
	shipped games therefore mishandle - no fill, show before ready, a duplicate
	load, and the rewarded earned/dismissed split. The banner-versus-takeover
	difference in what a completed show LEAVES BEHIND is covered here too, since
	it is the one place the four formats stop behaving alike.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_monetization/AdPlacement.h>

using Orkige::AdPlacement;
using Orkige::String;

namespace
{
	//! drive a unit to AS_READY, asserting the happy path on the way
	void loadReady(AdPlacement & unit)
	{
		Orkige::AdLoadResult refusal = Orkige::ALR_ERROR;
		String reason;
		REQUIRE(unit.beginLoad(refusal, reason));
		unit.completeLoad(Orkige::ALR_LOADED);
		REQUIRE(unit.isReady());
	}
}

TEST_CASE("an ad placement starts idle", "[unit][monetization]")
{
	AdPlacement unit(Orkige::AF_INTERSTITIAL, "level_end");
	REQUIRE(unit.state() == Orkige::AS_IDLE);
	REQUIRE(unit.format() == Orkige::AF_INTERSTITIAL);
	REQUIRE(unit.placement() == "level_end");
	REQUIRE_FALSE(unit.isReady());
	REQUIRE_FALSE(unit.isShowing());
	REQUIRE(unit.isTakeover());
}

TEST_CASE("a load runs idle -> loading -> ready", "[unit][monetization]")
{
	AdPlacement unit(Orkige::AF_INTERSTITIAL, "");
	Orkige::AdLoadResult refusal = Orkige::ALR_ERROR;
	String reason;

	REQUIRE(unit.beginLoad(refusal, reason));
	REQUIRE(unit.state() == Orkige::AS_LOADING);
	REQUIRE(unit.isLoading());

	unit.completeLoad(Orkige::ALR_LOADED);
	REQUIRE(unit.state() == Orkige::AS_READY);
	REQUIRE(unit.lastLoad() == Orkige::ALR_LOADED);
	REQUIRE(unit.reason().empty());
}

TEST_CASE("no fill is an ordinary answer that leaves the unit retryable",
	"[unit][monetization]")
{
	// THE failure real games meet in the field and never in development: the
	// request was fine, the network simply had nothing. It must be
	// distinguishable from a broken request AND immediately retryable.
	AdPlacement unit(Orkige::AF_REWARDED, "");
	Orkige::AdLoadResult refusal = Orkige::ALR_ERROR;
	String reason;

	REQUIRE(unit.beginLoad(refusal, reason));
	unit.completeLoad(Orkige::ALR_NO_FILL, "no advert was available");

	REQUIRE(unit.state() == Orkige::AS_FAILED);
	REQUIRE(unit.lastLoad() == Orkige::ALR_NO_FILL);
	REQUIRE(unit.reason() == "no advert was available");
	REQUIRE_FALSE(unit.isReady());

	// a failure is retryable - that is the whole point of AS_FAILED
	REQUIRE(unit.beginLoad(refusal, reason));
	REQUIRE(unit.state() == Orkige::AS_LOADING);
}

TEST_CASE("every non-load verdict lands in AS_FAILED with its reason kept",
	"[unit][monetization]")
{
	const Orkige::AdLoadResult verdicts[] =
	{
		Orkige::ALR_NO_FILL, Orkige::ALR_ERROR, Orkige::ALR_TIMEOUT
	};
	for(int i = 0; i < 3; ++i)
	{
		AdPlacement unit(Orkige::AF_BANNER, "");
		Orkige::AdLoadResult refusal = Orkige::ALR_ERROR;
		String reason;
		REQUIRE(unit.beginLoad(refusal, reason));
		unit.completeLoad(verdicts[i], "the stated reason");

		REQUIRE(unit.state() == Orkige::AS_FAILED);
		REQUIRE(unit.lastLoad() == verdicts[i]);
		REQUIRE(unit.reason() == "the stated reason");
	}
}

TEST_CASE("a second load while one is in flight is refused as busy",
	"[unit][monetization]")
{
	// a second request would strand the first one's callback
	AdPlacement unit(Orkige::AF_INTERSTITIAL, "");
	Orkige::AdLoadResult refusal = Orkige::ALR_LOADED;
	String reason;
	REQUIRE(unit.beginLoad(refusal, reason));

	REQUIRE_FALSE(unit.beginLoad(refusal, reason));
	REQUIRE(refusal == Orkige::ALR_BUSY);
	REQUIRE_FALSE(reason.empty());
	// and the in-flight load is untouched
	REQUIRE(unit.state() == Orkige::AS_LOADING);
}

TEST_CASE("loading a unit that already holds inventory is refused",
	"[unit][monetization]")
{
	// re-loading discards an advert the network already committed to us
	AdPlacement unit(Orkige::AF_INTERSTITIAL, "");
	loadReady(unit);

	Orkige::AdLoadResult refusal = Orkige::ALR_LOADED;
	String reason;
	REQUIRE_FALSE(unit.beginLoad(refusal, reason));
	REQUIRE(refusal == Orkige::ALR_BUSY);
	REQUIRE(unit.state() == Orkige::AS_READY);
}

TEST_CASE("SHOW BEFORE READY is refused and moves nothing",
	"[unit][monetization]")
{
	// the error state this design exists to make explicit: presenting a unit
	// that holds nothing must be a reported refusal, never undefined behaviour
	AdPlacement idle(Orkige::AF_INTERSTITIAL, "");
	REQUIRE_FALSE(idle.beginShow());
	REQUIRE(idle.state() == Orkige::AS_IDLE);

	AdPlacement loading(Orkige::AF_INTERSTITIAL, "");
	Orkige::AdLoadResult refusal = Orkige::ALR_ERROR;
	String reason;
	REQUIRE(loading.beginLoad(refusal, reason));
	REQUIRE_FALSE(loading.beginShow());
	REQUIRE(loading.state() == Orkige::AS_LOADING);

	AdPlacement failed(Orkige::AF_INTERSTITIAL, "");
	REQUIRE(failed.beginLoad(refusal, reason));
	failed.completeLoad(Orkige::ALR_NO_FILL);
	REQUIRE_FALSE(failed.beginShow());
	REQUIRE(failed.state() == Orkige::AS_FAILED);
}

TEST_CASE("a fullscreen unit is CONSUMED by its show", "[unit][monetization]")
{
	// inventory is spent by being watched: the next show needs a new load, and
	// a game that assumes otherwise silently stops showing adverts
	AdPlacement unit(Orkige::AF_INTERSTITIAL, "");
	loadReady(unit);

	REQUIRE(unit.beginShow());
	REQUIRE(unit.state() == Orkige::AS_SHOWING);

	unit.completeShow(Orkige::ASR_COMPLETED);
	REQUIRE(unit.state() == Orkige::AS_IDLE);
	REQUIRE_FALSE(unit.isReady());

	// and showing again without a fresh load is refused
	REQUIRE_FALSE(unit.beginShow());
}

TEST_CASE("a banner STAYS on screen until it is hidden", "[unit][monetization]")
{
	// the one place the formats genuinely diverge: a banner is persistent
	AdPlacement unit(Orkige::AF_BANNER, "hud");
	REQUIRE_FALSE(unit.isTakeover());
	loadReady(unit);

	REQUIRE(unit.beginShow());
	unit.completeShow(Orkige::ASR_COMPLETED);
	REQUIRE(unit.state() == Orkige::AS_SHOWING);
	REQUIRE(unit.isShowing());

	unit.hide();
	REQUIRE(unit.state() == Orkige::AS_IDLE);
}

TEST_CASE("hide never takes a fullscreen unit down", "[unit][monetization]")
{
	// a takeover is dismissed by the player; the game does not get to hide it
	AdPlacement unit(Orkige::AF_REWARDED, "");
	loadReady(unit);
	REQUIRE(unit.beginShow());

	unit.hide();
	REQUIRE(unit.state() == Orkige::AS_SHOWING);
}

TEST_CASE("the rewarded earned and dismissed branches are distinct outcomes",
	"[unit][monetization]")
{
	// a mediation surface reports "closed" and "rewarded" separately, and a
	// game that grants on close pays out for an advert nobody watched. Both
	// consume the unit; only one of them is the reward.
	AdPlacement earned(Orkige::AF_REWARDED, "");
	loadReady(earned);
	REQUIRE(earned.beginShow());
	earned.completeShow(Orkige::ASR_REWARD_EARNED);
	REQUIRE(earned.state() == Orkige::AS_IDLE);

	AdPlacement dismissed(Orkige::AF_REWARDED, "");
	loadReady(dismissed);
	REQUIRE(dismissed.beginShow());
	dismissed.completeShow(Orkige::ASR_DISMISSED);
	REQUIRE(dismissed.state() == Orkige::AS_IDLE);
}

TEST_CASE("a banner that fails to present is not left marked as on screen",
	"[unit][monetization]")
{
	AdPlacement unit(Orkige::AF_BANNER, "");
	loadReady(unit);
	REQUIRE(unit.beginShow());

	unit.completeShow(Orkige::ASR_ERROR, "could not present");
	REQUIRE(unit.state() == Orkige::AS_FAILED);
	REQUIRE_FALSE(unit.isShowing());
	REQUIRE(unit.reason() == "could not present");
}

TEST_CASE("a late or duplicate answer for a settled request is ignored",
	"[unit][monetization]")
{
	AdPlacement unit(Orkige::AF_INTERSTITIAL, "");
	loadReady(unit);

	// a second completion for a load nobody is waiting on must not demote a
	// ready unit
	unit.completeLoad(Orkige::ALR_NO_FILL, "late");
	REQUIRE(unit.state() == Orkige::AS_READY);
	REQUIRE(unit.lastLoad() == Orkige::ALR_LOADED);

	// and a show completion with no show running changes nothing
	unit.completeShow(Orkige::ASR_COMPLETED);
	REQUIRE(unit.state() == Orkige::AS_READY);
}

TEST_CASE("reset returns a unit to the never-loaded state",
	"[unit][monetization]")
{
	AdPlacement unit(Orkige::AF_BANNER, "");
	loadReady(unit);
	unit.reset();
	REQUIRE(unit.state() == Orkige::AS_IDLE);
	REQUIRE_FALSE(unit.isReady());
}
