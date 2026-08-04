/**************************************************************
	created:	2026/08/03 at 18:00
	filename: 	PlaySimulatorPickTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// The boot-from-shutdown simulator pick. The stakes: a scripted boot-path
// Play run that FAILS (or is killed by a test timeout) exits without the
// shutdown its passing path performs, leaving the designated warm device
// Booted. A retry that then refuses every Booted device falls back to an
// arbitrary never-booted simulator, whose cold first boot can outlast the
// whole preparation budget on a loaded machine - so the retry that exists to
// absorb a slow first attempt is instead guaranteed the slowest path. The
// picker must therefore reclaim the designated warm device in ANY state:
// shutdown -> picked as-is, Booted leftover -> picked with shutdownFirst so
// the caller restores the shutdown state and keeps the warm boot.

#include "PlaySimulatorPick.h"

#include <catch2/catch_test_macros.hpp>

using namespace OrkigeEditor;

namespace
{
	SimulatorPickCandidate device(char const* name, char const* udid,
		bool booted)
	{
		return SimulatorPickCandidate{ name, udid, booted };
	}
}

TEST_CASE("shutdown warm device is picked as-is", "[playsimpick]")
{
	const std::vector<SimulatorPickCandidate> devices = {
		device("iPhone 15", "STOCK-1", false),
		device("orkige-ci-boot", "WARM-1", false),
		device("iPhone 16", "STOCK-2", false),
	};
	SimulatorShutdownPick pick;
	REQUIRE(pickShutdownSimulator(devices, "WARM-1", pick));
	CHECK(pick.udid == "WARM-1");
	CHECK(pick.name == "orkige-ci-boot");
	CHECK_FALSE(pick.shutdownFirst);
}

TEST_CASE("a Booted warm leftover is reclaimed with shutdownFirst, "
	"never traded for a cold stranger", "[playsimpick]")
{
	// the retry-after-failure shape: the warm device is still Booted because
	// the failed attempt could not shut it down
	const std::vector<SimulatorPickCandidate> devices = {
		device("iPhone 15", "STOCK-1", false),
		device("orkige-ci-boot", "WARM-1", true),
		device("iPhone 16", "STOCK-2", false),
	};
	SimulatorShutdownPick pick;
	REQUIRE(pickShutdownSimulator(devices, "WARM-1", pick));
	CHECK(pick.udid == "WARM-1");
	CHECK(pick.shutdownFirst);
}

TEST_CASE("without a warm designation the first shutdown device is taken",
	"[playsimpick]")
{
	const std::vector<SimulatorPickCandidate> devices = {
		device("iPhone 14", "STOCK-0", true),
		device("iPhone 15", "STOCK-1", false),
		device("iPhone 16", "STOCK-2", false),
	};
	SimulatorShutdownPick pick;
	REQUIRE(pickShutdownSimulator(devices, "", pick));
	CHECK(pick.udid == "STOCK-1");
	CHECK_FALSE(pick.shutdownFirst);
}

TEST_CASE("a warm udid missing from the list falls back to the first "
	"shutdown device", "[playsimpick]")
{
	const std::vector<SimulatorPickCandidate> devices = {
		device("iPhone 15", "STOCK-1", false),
	};
	SimulatorShutdownPick pick;
	REQUIRE(pickShutdownSimulator(devices, "GONE-1", pick));
	CHECK(pick.udid == "STOCK-1");
	CHECK_FALSE(pick.shutdownFirst);
}

TEST_CASE("a Booted non-warm device is never picked", "[playsimpick]")
{
	const std::vector<SimulatorPickCandidate> devices = {
		device("iPhone 14", "STOCK-0", true),
		device("iPhone 15", "STOCK-1", true),
	};
	SimulatorShutdownPick pick;
	pick.udid = "untouched";
	CHECK_FALSE(pickShutdownSimulator(devices, "", pick));
	CHECK(pick.udid == "untouched");
}

TEST_CASE("an empty device list yields no pick", "[playsimpick]")
{
	SimulatorShutdownPick pick;
	CHECK_FALSE(pickShutdownSimulator({}, "WARM-1", pick));
}
