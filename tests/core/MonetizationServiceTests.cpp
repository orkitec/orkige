/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	MonetizationServiceTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless MonetizationService unit tests driven through the SIMULATED
	provider, which goes in through the ordinary plugin interface with no
	privileged path - so these cases exercise exactly the contract a real store
	or ad network would have to satisfy. Covered: the consent-before-init
	ordering constraint, the frame-boundary callback contract, every purchase
	outcome including cancelled / declined / pending / already-owned, restore
	yielding entitlements through the catalog's reverse index, the no-ads
	entitlement suppressing ad serving, the rewarded earned-versus-dismissed
	branch, show-before-ready, banner geometry composed with the safe area, and
	the deterministic scenario parse the future agent-facing verb drives.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_monetization/MonetizationService.h>
#include <core_monetization/SimulatedProvider.h>
#include <core_util/SafeArea.h>

#include <memory>

using Orkige::AdLoadOutcome;
using Orkige::AdShowOutcome;
using Orkige::MonetizationService;
using Orkige::Product;
using Orkige::PurchaseResult;
using Orkige::RestoreResult;
using Orkige::SafeAreaInsets;
using Orkige::SimulatedAdProvider;
using Orkige::SimulatedStoreProvider;
using Orkige::String;

namespace
{
	//! consent as a player who agreed to everything would leave it
	Orkige::ConsentState grantedConsent()
	{
		Orkige::ConsentState consent;
		consent.status = Orkige::CS_GRANTED;
		consent.trackingAuthorized = true;
		return consent;
	}

	//! a catalogued product bound to the simulated storefront under one id
	void addProduct(MonetizationService & service, String const & logicalId,
		Orkige::ProductKind kind, String const & storeId,
		bool grantsNoAds = false)
	{
		Product entry;
		entry.id = logicalId;
		entry.kind = kind;
		entry.grantsNoAds = grantsNoAds;
		service.catalog().add(entry);
		REQUIRE(service.catalog().addStoreId(logicalId, Orkige::SF_SIMULATED,
			storeId));
	}

	//! install both simulated plugins and return borrowed pointers to them
	void installProviders(MonetizationService & service,
		SimulatedStoreProvider ** outStore, SimulatedAdProvider ** outAds)
	{
		std::unique_ptr<SimulatedStoreProvider> store(new SimulatedStoreProvider());
		std::unique_ptr<SimulatedAdProvider> ads(new SimulatedAdProvider());
		*outStore = store.get();
		*outAds = ads.get();
		service.setStoreProvider(std::move(store));
		service.setAdProvider(std::move(ads));
	}

	//! a service with both plugins installed, consent granted and both up
	struct Fixture
	{
		MonetizationService			service;
		SimulatedStoreProvider *	store = NULL;
		SimulatedAdProvider *		ads = NULL;

		Fixture()
		{
			installProviders(this->service, &this->store, &this->ads);
			this->service.setConsent(grantedConsent());
			REQUIRE(this->service.initializeStore());
			REQUIRE(this->service.initializeAds(true));
		}
	};

	//! drive one placement to AS_READY through the whole seam
	void loadReady(Fixture & fixture, Orkige::AdFormat format,
		String const & placement)
	{
		AdLoadOutcome outcome;
		fixture.service.loadAd(format, placement,
			[&outcome](AdLoadOutcome const & answer) { outcome = answer; });
		fixture.service.update();
		REQUIRE(outcome.result == Orkige::ALR_LOADED);
		REQUIRE(fixture.service.adState(format, placement) == Orkige::AS_READY);
	}
}

//--- the ordering constraint -------------------------------------------------

TEST_CASE("ads REFUSE to initialize before consent has been gathered",
	"[unit][monetization]")
{
	// THE ordering constraint. A network that starts before the player has been
	// asked collects an advertising identifier it had no permission to collect,
	// so this is refused BY NAME rather than by quietly serving nothing.
	MonetizationService service;
	SimulatedStoreProvider * store = NULL;
	SimulatedAdProvider * ads = NULL;
	installProviders(service, &store, &ads);

	REQUIRE_FALSE(service.isConsentGathered());
	REQUIRE_FALSE(service.initializeAds(true));
	REQUIRE_FALSE(service.isAdsReady());

	// and with consent recorded, the same call comes up - carrying the consent
	service.setConsent(grantedConsent());
	REQUIRE(service.initializeAds(true));
	REQUIRE(service.isAdsReady());
	REQUIRE(ads->startedWithConsent().status == Orkige::CS_GRANTED);
	REQUIRE(ads->startedWithConsent().personalizedAds());
	// test mode is bound at start, like every real surface binds test units
	REQUIRE(ads->isTestMode());
	REQUIRE(service.isTestMode());
}

TEST_CASE("a DENIED answer is gathered consent and ads may start",
	"[unit][monetization]")
{
	// refusing is a legal outcome, not an error: the surface comes up and
	// serves contextual inventory. Confusing "denied" with "not asked" would
	// switch advertising off for every player who declined tracking.
	MonetizationService service;
	SimulatedStoreProvider * store = NULL;
	SimulatedAdProvider * ads = NULL;
	installProviders(service, &store, &ads);

	Orkige::ConsentState denied;
	denied.status = Orkige::CS_DENIED;
	service.setConsent(denied);

	REQUIRE(service.isConsentGathered());
	REQUIRE(service.initializeAds(true));
	REQUIRE_FALSE(ads->startedWithConsent().personalizedAds());
}

TEST_CASE("the store initializes WITHOUT consent", "[unit][monetization]")
{
	// privacy consent governs advertising identifiers, not payment. Refusing to
	// bring the purchase surface up until a player answered a privacy dialogue
	// would stop a paying customer from paying.
	MonetizationService service;
	SimulatedStoreProvider * store = NULL;
	SimulatedAdProvider * ads = NULL;
	installProviders(service, &store, &ads);

	REQUIRE_FALSE(service.isConsentGathered());
	REQUIRE(service.initializeStore());
	REQUIRE(service.isStoreReady());
}

TEST_CASE("withdrawing consent shuts the ad surface down",
	"[unit][monetization]")
{
	Fixture fixture;
	loadReady(fixture, Orkige::AF_INTERSTITIAL, "");

	// back to "never asked": leaving a network running would keep serving on a
	// permission that no longer exists
	Orkige::ConsentState withdrawn;
	fixture.service.setConsent(withdrawn);

	REQUIRE_FALSE(fixture.service.isAdsReady());
	// and the inventory the surface held is forgotten with it
	REQUIRE(fixture.service.adState(Orkige::AF_INTERSTITIAL, "")
		== Orkige::AS_IDLE);
}

//--- the frame-boundary callback contract ------------------------------------

TEST_CASE("no callback lands before the frame boundary",
	"[unit][monetization]")
{
	// the discipline HttpClient established: an answer never arrives in the
	// middle of a world update
	Fixture fixture;
	addProduct(fixture.service, "remove_ads", Orkige::PK_NON_CONSUMABLE,
		"sim.remove_ads", true);

	bool answered = false;
	const Orkige::MonetizationRequestId id = fixture.service.purchase(
		"remove_ads", [&answered](PurchaseResult const &) { answered = true; });

	REQUIRE(id != 0);
	REQUIRE_FALSE(answered);
	REQUIRE(fixture.service.pendingCount() == 1);

	fixture.service.update();
	REQUIRE(answered);
	REQUIRE(fixture.service.pendingCount() == 0);
}

TEST_CASE("a refusal travels through the SAME callback as a success",
	"[unit][monetization]")
{
	// one error path, not two: a caller that got a handle is always told how it
	// ended, even when the seam refused outright
	MonetizationService service;
	// no provider at all
	PurchaseResult result;
	bool answered = false;
	const Orkige::MonetizationRequestId id = service.purchase("anything",
		[&](PurchaseResult const & answer) { result = answer; answered = true; });

	REQUIRE(id != 0);
	service.update();
	REQUIRE(answered);
	REQUIRE(result.state == Orkige::PS_UNAVAILABLE);
	REQUIRE_FALSE(result.reason.empty());
	REQUIRE(result.productId == "anything");
}

TEST_CASE("latency is deterministic in poll ticks", "[unit][monetization]")
{
	// counted in update() calls, never in wall-clock milliseconds: a simulator
	// that raced the clock would produce a test that passes sometimes
	Fixture fixture;
	fixture.ads->scenario().latencyTicks = 2;

	AdLoadOutcome outcome;
	bool answered = false;
	fixture.service.loadAd(Orkige::AF_INTERSTITIAL, "",
		[&](AdLoadOutcome const & answer) { outcome = answer; answered = true; });

	fixture.service.update();
	REQUIRE_FALSE(answered);
	fixture.service.update();
	REQUIRE_FALSE(answered);
	fixture.service.update();
	REQUIRE(answered);
	REQUIRE(outcome.result == Orkige::ALR_LOADED);
}

//--- purchases ---------------------------------------------------------------

TEST_CASE("a purchase grants an entitlement", "[unit][monetization]")
{
	Fixture fixture;
	addProduct(fixture.service, "remove_ads", Orkige::PK_NON_CONSUMABLE,
		"sim.remove_ads", true);

	PurchaseResult result;
	fixture.service.purchase("remove_ads",
		[&result](PurchaseResult const & answer) { result = answer; });
	fixture.service.update();

	REQUIRE(result.state == Orkige::PS_PURCHASED);
	REQUIRE(result.owned());
	// the LOGICAL id comes back, never the storefront's own
	REQUIRE(result.productId == "remove_ads");
	REQUIRE_FALSE(result.transactionId.empty());
	REQUIRE_FALSE(result.receipt.empty());
	REQUIRE(fixture.service.hasEntitlement("remove_ads"));
}

TEST_CASE("every unhappy purchase outcome is reachable and distinct",
	"[unit][monetization]")
{
	struct Case
	{
		Orkige::PurchaseState	state;
		bool					owned;
		bool					entitled;
	};
	const Case cases[] =
	{
		// the player dismissed the sheet - NOT an error, and grants nothing
		{ Orkige::PS_CANCELLED,		false,	false },
		// the store refused the payment
		{ Orkige::PS_DECLINED,		false,	false },
		// deferred: parental approval may settle it days later, in a LATER
		// session. The game must grant NOTHING yet.
		{ Orkige::PS_PENDING,		false,	false },
		// the account already paid once - a SUCCESS path for the player, so the
		// entitlement is granted rather than an error shown
		{ Orkige::PS_ALREADY_OWNED,	true,	true  },
		{ Orkige::PS_UNAVAILABLE,	false,	false },
		{ Orkige::PS_FAILED,		false,	false }
	};

	for(int i = 0; i < 6; ++i)
	{
		Fixture fixture;
		addProduct(fixture.service, "remove_ads", Orkige::PK_NON_CONSUMABLE,
			"sim.remove_ads", true);
		fixture.store->scenario().purchaseState = cases[i].state;

		PurchaseResult result;
		fixture.service.purchase("remove_ads",
			[&result](PurchaseResult const & answer) { result = answer; });
		fixture.service.update();

		REQUIRE(result.state == cases[i].state);
		REQUIRE(result.owned() == cases[i].owned);
		REQUIRE(fixture.service.hasEntitlement("remove_ads") == cases[i].entitled);
		if(!cases[i].owned)
		{
			// the engine never fails a purchase silently
			REQUIRE_FALSE(result.reason.empty());
		}
	}
}

TEST_CASE("a deferred purchase carries a transaction but grants nothing",
	"[unit][monetization]")
{
	// the shape games get wrong twice over: treating it as a failure loses the
	// sale when the approval lands, and treating it as a success hands out
	// goods nobody paid for
	Fixture fixture;
	addProduct(fixture.service, "coins_500", Orkige::PK_CONSUMABLE,
		"sim.coins500");
	fixture.store->scenario().purchaseState = Orkige::PS_PENDING;

	PurchaseResult result;
	fixture.service.purchase("coins_500",
		[&result](PurchaseResult const & answer) { result = answer; });
	fixture.service.update();

	REQUIRE(result.state == Orkige::PS_PENDING);
	REQUIRE_FALSE(result.owned());
	REQUIRE_FALSE(result.transactionId.empty());
	REQUIRE(fixture.service.entitlements().empty());
}

TEST_CASE("a consumable purchase leaves no lasting entitlement",
	"[unit][monetization]")
{
	// a coin pack is SPENT, not owned; recording one would make it look like a
	// permanent unlock and block the next purchase
	Fixture fixture;
	addProduct(fixture.service, "coins_500", Orkige::PK_CONSUMABLE,
		"sim.coins500");

	PurchaseResult result;
	fixture.service.purchase("coins_500",
		[&result](PurchaseResult const & answer) { result = answer; });
	fixture.service.update();

	REQUIRE(result.state == Orkige::PS_PURCHASED);
	REQUIRE(fixture.service.entitlements().empty());
	REQUIRE_FALSE(fixture.service.hasEntitlement("coins_500"));

	// and the transaction is acknowledgeable - a consumable that is never
	// finished cannot be bought again on a real store
	fixture.service.finishTransaction(result.transactionId);
	REQUIRE(fixture.store->finishedTransactions().size() == 1);
}

TEST_CASE("a product with no identifier on this storefront is refused by name",
	"[unit][monetization]")
{
	Fixture fixture;
	Product entry;
	entry.id = "console_only";
	entry.kind = Orkige::PK_NON_CONSUMABLE;
	fixture.service.catalog().add(entry);
	// bound on a DIFFERENT storefront than the one running
	REQUIRE(fixture.service.catalog().addStoreId("console_only",
		Orkige::SF_IOS, "ios.console_only"));

	PurchaseResult result;
	fixture.service.purchase("console_only",
		[&result](PurchaseResult const & answer) { result = answer; });
	fixture.service.update();

	REQUIRE(result.state == Orkige::PS_UNAVAILABLE);
	REQUIRE(result.reason.find("console_only") != String::npos);
}

TEST_CASE("a product query fills the catalog metadata through the reverse index",
	"[unit][monetization]")
{
	Fixture fixture;
	addProduct(fixture.service, "remove_ads", Orkige::PK_NON_CONSUMABLE,
		"sim.remove_ads", true);

	Orkige::ProductQueryResult result;
	fixture.service.requestProducts(
		[&result](Orkige::ProductQueryResult const & answer) { result = answer; });
	fixture.service.update();

	REQUIRE(result.completed);
	REQUIRE(result.products.size() == 1);
	// the storefront answered in ITS identifier; the caller gets the logical one
	REQUIRE(result.products[0].id == "remove_ads");
	REQUIRE_FALSE(result.products[0].displayPrice.empty());
	// and the catalog kept the metadata for a store screen to read later
	REQUIRE(fixture.service.catalog().find("remove_ads")->available);
	// the catalog stays the truth for what the product MEANS
	REQUIRE(fixture.service.catalog().find("remove_ads")->grantsNoAds);
}

//--- restore -----------------------------------------------------------------

TEST_CASE("restore yields entitlements named through the reverse index",
	"[unit][monetization]")
{
	// the ONLY way ownership comes back after a reinstall or on a second
	// device - which is exactly why entitlements are never a save file
	Fixture fixture;
	addProduct(fixture.service, "remove_ads", Orkige::PK_NON_CONSUMABLE,
		"sim.remove_ads", true);
	addProduct(fixture.service, "level_pack", Orkige::PK_NON_CONSUMABLE,
		"sim.levelpack");

	// a real store hands back ITS identifiers with nothing to correlate them
	// against
	fixture.store->scenario().restoreStoreIds.push_back("sim.remove_ads");
	fixture.store->scenario().restoreStoreIds.push_back("sim.levelpack");

	RestoreResult result;
	fixture.service.restore(
		[&result](RestoreResult const & answer) { result = answer; });
	fixture.service.update();

	REQUIRE(result.completed);
	REQUIRE(result.entitlements.size() == 2);
	REQUIRE(result.entitlements[0].productId == "remove_ads");
	REQUIRE(result.entitlements[1].productId == "level_pack");
	REQUIRE(fixture.service.hasEntitlement("remove_ads"));
	REQUIRE(fixture.service.hasEntitlement("level_pack"));
	REQUIRE(fixture.service.isAdFree());
}

TEST_CASE("an empty restore is a SUCCESS, not a failure",
	"[unit][monetization]")
{
	// a player who never bought anything restores nothing, and telling them
	// something went wrong is a support ticket
	Fixture fixture;
	RestoreResult result;
	fixture.service.restore(
		[&result](RestoreResult const & answer) { result = answer; });
	fixture.service.update();

	REQUIRE(result.completed);
	REQUIRE(result.entitlements.empty());
	REQUIRE(result.reason.empty());
}

TEST_CASE("a restore that could not reach the store says so",
	"[unit][monetization]")
{
	Fixture fixture;
	fixture.store->scenario().restoreFails = true;

	RestoreResult result;
	fixture.service.restore(
		[&result](RestoreResult const & answer) { result = answer; });
	fixture.service.update();

	REQUIRE_FALSE(result.completed);
	REQUIRE_FALSE(result.reason.empty());
	REQUIRE(result.entitlements.empty());
}

TEST_CASE("a restored product the catalog never declared is skipped",
	"[unit][monetization]")
{
	Fixture fixture;
	addProduct(fixture.service, "remove_ads", Orkige::PK_NON_CONSUMABLE,
		"sim.remove_ads", true);
	fixture.store->scenario().restoreStoreIds.push_back("sim.remove_ads");
	fixture.store->scenario().restoreStoreIds.push_back("sim.retired_product");

	RestoreResult result;
	fixture.service.restore(
		[&result](RestoreResult const & answer) { result = answer; });
	fixture.service.update();

	REQUIRE(result.completed);
	// the unknown one is dropped rather than recorded under a storefront id
	REQUIRE(result.entitlements.size() == 1);
	REQUIRE(result.entitlements[0].productId == "remove_ads");
}

//--- the no-ads link ---------------------------------------------------------

TEST_CASE("a no-ads entitlement suppresses ad LOADING",
	"[unit][monetization]")
{
	// THE LINK between the two seams. Loading would spend a paying player's
	// data on inventory that can never be shown, so it is refused at the load.
	Fixture fixture;
	addProduct(fixture.service, "remove_ads", Orkige::PK_NON_CONSUMABLE,
		"sim.remove_ads", true);

	// before the purchase, an interstitial loads normally
	loadReady(fixture, Orkige::AF_INTERSTITIAL, "");

	PurchaseResult purchase;
	fixture.service.purchase("remove_ads",
		[&purchase](PurchaseResult const & answer) { purchase = answer; });
	fixture.service.update();
	REQUIRE(fixture.service.isAdFree());

	AdLoadOutcome outcome;
	fixture.service.loadAd(Orkige::AF_BANNER, "hud",
		[&outcome](AdLoadOutcome const & answer) { outcome = answer; });
	fixture.service.update();

	// refused HONESTLY - not a silent no-op the game cannot distinguish from a
	// network that is simply slow
	REQUIRE(outcome.result == Orkige::ALR_SUPPRESSED);
	REQUIRE_FALSE(outcome.reason.empty());
}

TEST_CASE("a no-ads entitlement suppresses ad SHOWING",
	"[unit][monetization]")
{
	Fixture fixture;
	addProduct(fixture.service, "remove_ads", Orkige::PK_NON_CONSUMABLE,
		"sim.remove_ads", true);

	// inventory held BEFORE the purchase - the suppression must still bite
	loadReady(fixture, Orkige::AF_INTERSTITIAL, "");

	PurchaseResult purchase;
	fixture.service.purchase("remove_ads",
		[&purchase](PurchaseResult const & answer) { purchase = answer; });
	fixture.service.update();

	AdShowOutcome outcome;
	fixture.service.showAd(Orkige::AF_INTERSTITIAL, "",
		[&outcome](AdShowOutcome const & answer) { outcome = answer; });
	fixture.service.update();

	REQUIRE(outcome.result == Orkige::ASR_SUPPRESSED);
	REQUIRE_FALSE(fixture.service.isTakeoverActive());
}

TEST_CASE("a no-ads entitlement leaves opt-in rewarded ads alone",
	"[unit][monetization]")
{
	// the default policy: a rewarded advert is one the player CHOSE to watch
	// for something, so a paying player keeps the mechanic
	Fixture fixture;
	addProduct(fixture.service, "remove_ads", Orkige::PK_NON_CONSUMABLE,
		"sim.remove_ads", true);

	PurchaseResult purchase;
	fixture.service.purchase("remove_ads",
		[&purchase](PurchaseResult const & answer) { purchase = answer; });
	fixture.service.update();
	REQUIRE(fixture.service.isAdFree());

	AdLoadOutcome outcome;
	fixture.service.loadAd(Orkige::AF_REWARDED, "",
		[&outcome](AdLoadOutcome const & answer) { outcome = answer; });
	fixture.service.update();
	REQUIRE(outcome.result == Orkige::ALR_LOADED);

	// a game that disagrees flips the policy and the rewarded unit goes quiet
	Orkige::AdPolicy policy;
	policy.suppressRewarded = true;
	fixture.service.setAdPolicy(policy);

	AdLoadOutcome suppressed;
	fixture.service.loadAd(Orkige::AF_REWARDED, "second",
		[&suppressed](AdLoadOutcome const & answer) { suppressed = answer; });
	fixture.service.update();
	REQUIRE(suppressed.result == Orkige::ALR_SUPPRESSED);
}

TEST_CASE("a product that does not grant no-ads leaves advertising running",
	"[unit][monetization]")
{
	Fixture fixture;
	addProduct(fixture.service, "coins_500", Orkige::PK_CONSUMABLE,
		"sim.coins500");
	addProduct(fixture.service, "level_pack", Orkige::PK_NON_CONSUMABLE,
		"sim.levelpack");

	PurchaseResult purchase;
	fixture.service.purchase("level_pack",
		[&purchase](PurchaseResult const & answer) { purchase = answer; });
	fixture.service.update();

	REQUIRE(fixture.service.hasEntitlement("level_pack"));
	REQUIRE_FALSE(fixture.service.isAdFree());

	AdLoadOutcome outcome;
	fixture.service.loadAd(Orkige::AF_BANNER, "hud",
		[&outcome](AdLoadOutcome const & answer) { outcome = answer; });
	fixture.service.update();
	REQUIRE(outcome.result == Orkige::ALR_LOADED);
}

//--- ad serving --------------------------------------------------------------

TEST_CASE("no fill reaches the game as an ordinary answer",
	"[unit][monetization]")
{
	// the big one: common in low-traffic regions, never seen in development,
	// and the most likely thing to break a real game
	Fixture fixture;
	fixture.ads->scenario().loadResult = Orkige::ALR_NO_FILL;

	AdLoadOutcome outcome;
	fixture.service.loadAd(Orkige::AF_INTERSTITIAL, "",
		[&outcome](AdLoadOutcome const & answer) { outcome = answer; });
	fixture.service.update();

	REQUIRE(outcome.result == Orkige::ALR_NO_FILL);
	REQUIRE_FALSE(outcome.ready());
	REQUIRE_FALSE(outcome.reason.empty());
	REQUIRE(fixture.service.adState(Orkige::AF_INTERSTITIAL, "")
		== Orkige::AS_FAILED);
}

TEST_CASE("fill can differ per format", "[unit][monetization]")
{
	// a real and commonly surprising difference - a banner fills where an
	// interstitial does not
	Fixture fixture;
	fixture.ads->scenario().loadResult = Orkige::ALR_LOADED;
	fixture.ads->scenario().loadResultByFormat[Orkige::AF_INTERSTITIAL] =
		Orkige::ALR_NO_FILL;

	AdLoadOutcome banner;
	fixture.service.loadAd(Orkige::AF_BANNER, "hud",
		[&banner](AdLoadOutcome const & answer) { banner = answer; });
	AdLoadOutcome interstitial;
	fixture.service.loadAd(Orkige::AF_INTERSTITIAL, "",
		[&interstitial](AdLoadOutcome const & answer) { interstitial = answer; });
	fixture.service.update();

	REQUIRE(banner.result == Orkige::ALR_LOADED);
	REQUIRE(interstitial.result == Orkige::ALR_NO_FILL);
}

TEST_CASE("SHOW BEFORE READY answers ASR_NOT_READY through the seam",
	"[unit][monetization]")
{
	Fixture fixture;
	AdShowOutcome outcome;
	fixture.service.showAd(Orkige::AF_INTERSTITIAL, "",
		[&outcome](AdShowOutcome const & answer) { outcome = answer; });
	fixture.service.update();

	REQUIRE(outcome.result == Orkige::ASR_NOT_READY);
	REQUIRE_FALSE(outcome.reason.empty());
	// nothing was presented, so the app was never taken over
	REQUIRE_FALSE(fixture.service.isTakeoverActive());
}

TEST_CASE("the reward branch carries the reward, the dismissal carries none",
	"[unit][monetization]")
{
	// the branch games get wrong. Granting on "closed" pays out for an advert
	// nobody watched, so the amount travels ONLY with the earned result.
	{
		Fixture fixture;
		fixture.ads->scenario().showResult = Orkige::ASR_REWARD_EARNED;
		fixture.ads->scenario().rewardId = "coins";
		fixture.ads->scenario().rewardAmount = 50.0;
		loadReady(fixture, Orkige::AF_REWARDED, "level_end");

		AdShowOutcome outcome;
		fixture.service.showAd(Orkige::AF_REWARDED, "level_end",
			[&outcome](AdShowOutcome const & answer) { outcome = answer; });
		fixture.service.update();

		REQUIRE(outcome.result == Orkige::ASR_REWARD_EARNED);
		REQUIRE(outcome.rewardEarned());
		REQUIRE(outcome.rewardId == "coins");
		REQUIRE(outcome.rewardAmount == 50.0);
	}
	{
		Fixture fixture;
		// the player closed it early - the surface would still report an
		// amount, and the seam must not pass one on
		fixture.ads->scenario().showResult = Orkige::ASR_DISMISSED;
		fixture.ads->scenario().rewardId = "coins";
		fixture.ads->scenario().rewardAmount = 50.0;
		loadReady(fixture, Orkige::AF_REWARDED, "level_end");

		AdShowOutcome outcome;
		fixture.service.showAd(Orkige::AF_REWARDED, "level_end",
			[&outcome](AdShowOutcome const & answer) { outcome = answer; });
		fixture.service.update();

		REQUIRE(outcome.result == Orkige::ASR_DISMISSED);
		REQUIRE_FALSE(outcome.rewardEarned());
		REQUIRE(outcome.rewardAmount == 0.0);
		REQUIRE(outcome.rewardId.empty());
	}
}

TEST_CASE("a fullscreen ad is an observable takeover while it is up",
	"[unit][monetization]")
{
	// the host reads this and applies the SAME consequences the backgrounding
	// gate carries - stop advancing the sim, duck audio. This class only
	// reports; the loop that owns the tick order is the one entitled to skip it.
	Fixture fixture;
	loadReady(fixture, Orkige::AF_INTERSTITIAL, "");

	REQUIRE_FALSE(fixture.service.isTakeoverActive());
	fixture.ads->scenario().latencyTicks = 1;

	AdShowOutcome outcome;
	bool answered = false;
	fixture.service.showAd(Orkige::AF_INTERSTITIAL, "",
		[&](AdShowOutcome const & answer) { outcome = answer; answered = true; });

	// the app is covered from the moment the show is issued
	REQUIRE(fixture.service.isTakeoverActive());
	REQUIRE(fixture.service.takeoverFormat() == Orkige::AF_INTERSTITIAL);

	fixture.service.update();
	REQUIRE_FALSE(answered);
	REQUIRE(fixture.service.isTakeoverActive());

	fixture.service.update();
	REQUIRE(answered);
	// and it is ours again
	REQUIRE_FALSE(fixture.service.isTakeoverActive());
}

TEST_CASE("a banner never takes the app over", "[unit][monetization]")
{
	Fixture fixture;
	loadReady(fixture, Orkige::AF_BANNER, "hud");

	AdShowOutcome outcome;
	fixture.service.showAd(Orkige::AF_BANNER, "hud",
		[&outcome](AdShowOutcome const & answer) { outcome = answer; });
	REQUIRE_FALSE(fixture.service.isTakeoverActive());
	fixture.service.update();
	REQUIRE(outcome.result == Orkige::ASR_COMPLETED);
}

TEST_CASE("a second fullscreen ad while one is up is refused",
	"[unit][monetization]")
{
	// two takeovers at once is a black screen on some surfaces and a lost
	// callback on the rest
	Fixture fixture;
	loadReady(fixture, Orkige::AF_INTERSTITIAL, "first");
	loadReady(fixture, Orkige::AF_REWARDED, "second");
	// the first show stays up while the second is attempted
	fixture.ads->scenario().latencyTicks = 5;

	AdShowOutcome first;
	fixture.service.showAd(Orkige::AF_INTERSTITIAL, "first",
		[&first](AdShowOutcome const & answer) { first = answer; });
	REQUIRE(fixture.service.isTakeoverActive());

	AdShowOutcome second;
	fixture.service.showAd(Orkige::AF_REWARDED, "second",
		[&second](AdShowOutcome const & answer) { second = answer; });
	fixture.service.update();

	REQUIRE(second.result == Orkige::ASR_ERROR);
	REQUIRE_FALSE(second.reason.empty());
}

TEST_CASE("ad requests are refused honestly when the surface never came up",
	"[unit][monetization]")
{
	MonetizationService service;
	SimulatedStoreProvider * store = NULL;
	SimulatedAdProvider * ads = NULL;
	installProviders(service, &store, &ads);
	service.setConsent(grantedConsent());
	ads->scenario().adInitializeFails = true;

	REQUIRE_FALSE(service.initializeAds(true));

	AdLoadOutcome outcome;
	service.loadAd(Orkige::AF_BANNER, "hud",
		[&outcome](AdLoadOutcome const & answer) { outcome = answer; });
	service.update();

	REQUIRE(outcome.result == Orkige::ALR_NOT_INITIALIZED);
	REQUIRE_FALSE(outcome.reason.empty());
}

TEST_CASE("a load with consent never gathered is refused by name",
	"[unit][monetization]")
{
	MonetizationService service;
	SimulatedStoreProvider * store = NULL;
	SimulatedAdProvider * ads = NULL;
	installProviders(service, &store, &ads);

	AdLoadOutcome outcome;
	service.loadAd(Orkige::AF_BANNER, "hud",
		[&outcome](AdLoadOutcome const & answer) { outcome = answer; });
	service.update();

	REQUIRE(outcome.result == Orkige::ALR_CONSENT_MISSING);
}

//--- the banner's screen cost ------------------------------------------------

TEST_CASE("the seam reports banner geometry composed with the safe area",
	"[unit][monetization]")
{
	// without this a HUD sits under the advert and nobody finds out until a
	// device with live inventory runs the build
	Fixture fixture;
	fixture.ads->scenario().bannerHeight = 50;
	fixture.ads->scenario().bannerWidth = 320;
	fixture.ads->scenario().bannerPosition = Orkige::BP_BOTTOM;

	SafeAreaInsets display;
	display.mTop = 47;
	display.mBottom = 34;

	// nothing on screen yet: layout is exactly the display's own
	REQUIRE_FALSE(fixture.service.bannerGeometry().visible);
	REQUIRE(fixture.service.layoutInsets(display).mBottom == 34);

	loadReady(fixture, Orkige::AF_BANNER, "hud");
	AdShowOutcome outcome;
	fixture.service.showAd(Orkige::AF_BANNER, "hud",
		[&outcome](AdShowOutcome const & answer) { outcome = answer; });
	fixture.service.update();

	const Orkige::BannerGeometry geometry = fixture.service.bannerGeometry();
	REQUIRE(geometry.visible);
	REQUIRE(geometry.height == 50);
	REQUIRE(geometry.width == 320);
	REQUIRE(fixture.service.layoutInsets(display).mBottom == 84);
	REQUIRE(fixture.service.layoutInsets(display).mTop == 47);

	// and hiding it gives the space back
	fixture.service.hideBanner();
	REQUIRE_FALSE(fixture.service.bannerGeometry().visible);
	REQUIRE(fixture.service.layoutInsets(display).mBottom == 34);
	REQUIRE(fixture.service.adState(Orkige::AF_BANNER, "hud")
		== Orkige::AS_IDLE);
}

TEST_CASE("banner geometry is answerable with no ad provider at all",
	"[unit][monetization]")
{
	// a layout asks unconditionally, whether or not this build serves adverts
	MonetizationService service;
	SafeAreaInsets display;
	display.mBottom = 34;

	REQUIRE_FALSE(service.bannerGeometry().visible);
	REQUIRE(service.layoutInsets(display).mBottom == 34);
}

//--- the deterministic scenario surface --------------------------------------

TEST_CASE("scenario settings parse from the key/value form an agent drives",
	"[unit][monetization]")
{
	// the pure parse the future cvar and MCP verb both ride on
	Orkige::SimulatedScenario scenario;
	String error;

	REQUIRE(scenario.apply("loadResult", "no_fill", error));
	REQUIRE(scenario.loadResult == Orkige::ALR_NO_FILL);

	REQUIRE(scenario.apply("loadResult.banner", "loaded", error));
	REQUIRE(scenario.loadResultByFormat[Orkige::AF_BANNER] == Orkige::ALR_LOADED);

	REQUIRE(scenario.apply("showResult", "reward_earned", error));
	REQUIRE(scenario.showResult == Orkige::ASR_REWARD_EARNED);

	REQUIRE(scenario.apply("purchaseState", "pending", error));
	REQUIRE(scenario.purchaseState == Orkige::PS_PENDING);

	REQUIRE(scenario.apply("latencyTicks", "3", error));
	REQUIRE(scenario.latencyTicks == 3);

	REQUIRE(scenario.apply("rewardAmount", "12.5", error));
	REQUIRE(scenario.rewardAmount == 12.5);

	REQUIRE(scenario.apply("bannerPosition", "top", error));
	REQUIRE(scenario.bannerPosition == Orkige::BP_TOP);

	REQUIRE(scenario.apply("bannerInsideSafeArea", "false", error));
	REQUIRE_FALSE(scenario.bannerInsideSafeArea);

	REQUIRE(scenario.apply("restoreStoreIds", "a.one,a.two", error));
	REQUIRE(scenario.restoreStoreIds.size() == 2);
	REQUIRE(scenario.restoreStoreIds[1] == "a.two");

	REQUIRE(scenario.apply("reason", "a stated reason", error));
	REQUIRE(scenario.reason == "a stated reason");
}

TEST_CASE("an unrecognised scenario setting is refused with a reason",
	"[unit][monetization]")
{
	Orkige::SimulatedScenario scenario;
	String error;

	REQUIRE_FALSE(scenario.apply("noSuchKey", "1", error));
	REQUIRE_FALSE(error.empty());

	error.clear();
	REQUIRE_FALSE(scenario.apply("loadResult", "not_a_result", error));
	REQUIRE_FALSE(error.empty());

	error.clear();
	REQUIRE_FALSE(scenario.apply("latencyTicks", "many", error));
	REQUIRE_FALSE(error.empty());

	error.clear();
	REQUIRE_FALSE(scenario.apply("loadResult.nonsense", "loaded", error));
	REQUIRE_FALSE(error.empty());
}

TEST_CASE("a pinned scenario reproduces exactly across runs",
	"[unit][monetization]")
{
	// no randomness anywhere: the same scenario answers the same way every
	// time, which is what makes an assertion about an unhappy path meaningful
	for(int run = 0; run < 3; ++run)
	{
		Fixture fixture;
		String error;
		REQUIRE(fixture.ads->scenario().apply("loadResult", "timeout", error));

		AdLoadOutcome outcome;
		fixture.service.loadAd(Orkige::AF_APP_OPEN, "",
			[&outcome](AdLoadOutcome const & answer) { outcome = answer; });
		fixture.service.update();
		REQUIRE(outcome.result == Orkige::ALR_TIMEOUT);
	}
}
