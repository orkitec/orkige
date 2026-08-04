/**************************************************************
	created:	2026/08/04 at 15:00
	filename: 	PlatformAdsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the platform half of the AD seam - which provider a
	project asked for, and what a build that carries none actually says. The
	engine MEDIATES rather than advertises: there is no ad network here, so the
	absence is the normal answer and it has to be a NAMED one. A real network
	integration goes in through the same AdProvider interface the simulated
	provider uses, so nothing above this file changes when one arrives.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_monetization/MonetizationService.h>
#include <core_monetization/PlatformAds.h>
#include <core_monetization/SimulatedProvider.h>

#include <memory>

using Orkige::AdLoadOutcome;
using Orkige::AdProvider;
using Orkige::MonetizationService;
using Orkige::SimulatedAdProvider;
using Orkige::String;

TEST_CASE("the simulated ad surface has to be asked for by name",
	"[monetization][ads]")
{
	Orkige::AdProviderChoice choice = Orkige::APC_SIMULATED;

	// AN UNSET SETTING IS THE SAFE ANSWER, the same rule the store side
	// carries: a shipped game that decayed into the simulator would serve a
	// fake advert and report a reward nobody watched an advert for, and
	// nothing about it would look wrong from inside the game.
	REQUIRE(Orkige::adProviderChoiceFromName("", choice));
	REQUIRE(choice == Orkige::APC_PLATFORM);

	REQUIRE(Orkige::adProviderChoiceFromName("simulated", choice));
	REQUIRE(choice == Orkige::APC_SIMULATED);
	REQUIRE(Orkige::adProviderChoiceFromName("none", choice));
	REQUIRE(choice == Orkige::APC_NONE);
	REQUIRE(Orkige::adProviderChoiceFromName("platform", choice));
	REQUIRE(choice == Orkige::APC_PLATFORM);

	// a value nobody recognises is REFUSED rather than rounded to something.
	// The caller keeps the safe choice and says what it did not understand.
	choice = Orkige::APC_SIMULATED;
	REQUIRE_FALSE(Orkige::adProviderChoiceFromName("Simulated", choice));
	REQUIRE_FALSE(Orkige::adProviderChoiceFromName("admob", choice));
	REQUIRE_FALSE(Orkige::adProviderChoiceFromName("yes", choice));
	// untouched by a refusal
	REQUIRE(choice == Orkige::APC_SIMULATED);
}

TEST_CASE("every ad provider choice round-trips through its token",
	"[monetization][ads]")
{
	// the tokens are what a manifest is written in, so they are a contract
	const Orkige::AdProviderChoice ALL[] =
	{
		Orkige::APC_PLATFORM, Orkige::APC_SIMULATED, Orkige::APC_NONE
	};
	for (Orkige::AdProviderChoice const & choice : ALL)
	{
		Orkige::AdProviderChoice parsed = Orkige::APC_NONE;
		const String name = Orkige::adProviderChoiceName(choice);
		REQUIRE_FALSE(name.empty());
		REQUIRE(Orkige::adProviderChoiceFromName(name, parsed));
		REQUIRE(parsed == choice);
	}
}

TEST_CASE("a build with no ad provider says so, and says it once",
	"[monetization][ads]")
{
	// THE ABSENCE IS THE NORMAL STATE: the engine ships no ad network, so
	// every build answers this way until a project installs a provider.
	REQUIRE_FALSE(Orkige::platformAdsAvailable());

	// and it is NAMED. A developer watching every load fail deserves a
	// prerequisite they can act on, not silence.
	const String reason = Orkige::platformAdsUnavailableReason();
	REQUIRE_FALSE(reason.empty());

	// created rather than crashed: a caller asks and gets an honest NULL
	std::unique_ptr<AdProvider> provider(Orkige::createPlatformAdProvider());
	REQUIRE(provider.get() == NULL);
}

TEST_CASE("with no ad provider installed every request is refused by name",
	"[monetization][ads]")
{
	MonetizationService service;
	Orkige::ConsentState consent;
	consent.status = Orkige::CS_GRANTED;
	service.setConsent(consent);

	// nothing installed - which is exactly what the platform choice yields on
	// every build today
	REQUIRE(service.adProvider() == NULL);
	REQUIRE_FALSE(service.initializeAds(true));
	REQUIRE_FALSE(service.isAdsReady());

	// THE REFUSAL STILL TRAVELS THROUGH THE ORDINARY CALLBACK, so a game has
	// one error path rather than two, and it arrives at a frame boundary.
	int answers = 0;
	AdLoadOutcome seen;
	const Orkige::MonetizationRequestId id = service.loadAd(
		Orkige::AF_INTERSTITIAL, "gate",
		[&](AdLoadOutcome const & outcome) { ++answers; seen = outcome; });
	REQUIRE(id != 0);
	// not before the frame boundary
	REQUIRE(answers == 0);
	service.update();
	REQUIRE(answers == 1);
	REQUIRE_FALSE(seen.ready());
	// a refusal that does not say what is missing sends a developer hunting
	REQUIRE_FALSE(seen.reason.empty());

	// and exactly once - a second update must not re-deliver it
	service.update();
	REQUIRE(answers == 1);
}

TEST_CASE("the simulated provider goes in through the ordinary seam",
	"[monetization][ads]")
{
	// EVERY PROVIDER IS A PLUGIN, including ours: the simulator is installed
	// through the same setAdProvider a real integration would use, which is
	// what keeps the interface honest.
	MonetizationService service;
	auto simulated = std::make_unique<SimulatedAdProvider>();
	SimulatedAdProvider * owner = simulated.get();
	service.setAdProvider(std::move(simulated));
	REQUIRE(service.adProvider() == owner);

	// CONSENT IS AN ORDERING CONSTRAINT: the surface refuses to come up while
	// the player has not been asked, so a network can never be started on a
	// permission nobody gave.
	REQUIRE_FALSE(service.initializeAds(true));
	REQUIRE_FALSE(service.isAdsReady());

	Orkige::ConsentState consent;
	consent.status = Orkige::CS_GRANTED;
	consent.trackingAuthorized = true;
	service.setConsent(consent);
	REQUIRE(service.initializeAds(true));
	REQUIRE(service.isAdsReady());
	// the provider was handed the consent it was started with - the ordering
	// proof, asserted rather than assumed
	REQUIRE(owner->startedWithConsent().status == Orkige::CS_GRANTED);
	REQUIRE(service.isTestMode());
}
