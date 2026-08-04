/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	PlatformStoreTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the platform store seam, driven through the ordinary
	MonetizationService on whatever platform the suite is running on. A real
	storefront cannot be reached from CI - there is no signed app, no store
	account and no device - so what is proved here is the part that has to hold
	WITHOUT one: the absence is named rather than silent, every request still
	gets exactly one answer at a frame boundary, and no refusal comes back
	blank. On a platform that compiles a real provider this drives that
	provider's own refusal paths; on one that does not, it drives the honest
	absence.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_monetization/MonetizationService.h>
#include <core_monetization/PlatformStore.h>
#include <core_monetization/ProductCatalog.h>
#include <core_monetization/ProductCatalogFile.h>

#include <memory>

using Orkige::MonetizationService;
using Orkige::ProductCatalogFile;
using Orkige::ProductQueryResult;
using Orkige::PurchaseResult;
using Orkige::RestoreResult;
using Orkige::StoreProvider;
using Orkige::String;

TEST_CASE("the simulated store has to be asked for by name",
	"[monetization][store]")
{
	Orkige::StoreProviderChoice choice = Orkige::SPC_SIMULATED;

	// AN UNSET SETTING IS THE SAFE ANSWER. A shipped game that decayed into the
	// simulator would hand every product out for free and look correct doing it.
	REQUIRE(Orkige::storeProviderChoiceFromName("", choice));
	CHECK(choice == Orkige::SPC_PLATFORM);

	REQUIRE(Orkige::storeProviderChoiceFromName("platform", choice));
	CHECK(choice == Orkige::SPC_PLATFORM);
	REQUIRE(Orkige::storeProviderChoiceFromName("simulated", choice));
	CHECK(choice == Orkige::SPC_SIMULATED);
	REQUIRE(Orkige::storeProviderChoiceFromName("none", choice));
	CHECK(choice == Orkige::SPC_NONE);

	// a value nobody understands is refused rather than guessed at
	choice = Orkige::SPC_PLATFORM;
	CHECK_FALSE(Orkige::storeProviderChoiceFromName("Simulated", choice));
	CHECK_FALSE(Orkige::storeProviderChoiceFromName("fake", choice));
	CHECK(choice == Orkige::SPC_PLATFORM);

	CHECK(Orkige::storeProviderChoiceName(Orkige::SPC_PLATFORM) == "platform");
	CHECK(Orkige::storeProviderChoiceName(Orkige::SPC_SIMULATED)
		== "simulated");
	CHECK(Orkige::storeProviderChoiceName(Orkige::SPC_NONE) == "none");
}

TEST_CASE("a platform with no store says so instead of failing silently",
	"[monetization][store]")
{
	// exactly one of these two shapes is compiled, and BOTH are honest
	if(Orkige::platformStorefront() == Orkige::SF_UNKNOWN)
	{
		CHECK_FALSE(Orkige::platformStoreAvailable());
		CHECK_FALSE(Orkige::platformStoreUnavailableReason().empty());

		std::unique_ptr<StoreProvider> provider(
			Orkige::createPlatformStoreProvider());
		CHECK(provider.get() == NULL);
		return;
	}

	// a build that carries a real store still answers the runtime question
	// honestly: a machine with no app identity or no payment permission is not
	// a failure, it is a named prerequisite
	std::unique_ptr<StoreProvider> provider(
		Orkige::createPlatformStoreProvider());
	REQUIRE(provider.get() != NULL);
	CHECK(provider->storefront() == Orkige::platformStorefront());
	CHECK(String(provider->name()).empty() == false);

	if(!Orkige::platformStoreAvailable())
	{
		CHECK_FALSE(Orkige::platformStoreUnavailableReason().empty());
	}
	else
	{
		CHECK(Orkige::platformStoreUnavailableReason().empty());
	}
}

TEST_CASE("the platform store answers every request exactly once",
	"[monetization][store]")
{
	MonetizationService money;

	// the catalog a game would ship, read from the same text an agent writes
	const String catalog =
		"version 1\n"
		"product remove_ads\n"
		"  kind non_consumable\n"
		"  noads true\n"
		"  ios com.orkitec.test.removeads\n"
		"  macos com.orkitec.test.removeads\n"
		"  android remove_ads\n"
		"  windows remove_ads\n"
		"  web remove_ads\n";
	REQUIRE(ProductCatalogFile::parse(catalog, money.catalog(), NULL));

	std::unique_ptr<StoreProvider> provider(
		Orkige::createPlatformStoreProvider());
	if(provider.get() != NULL)
	{
		money.setStoreProvider(std::move(provider));
		// the surface comes up so that each refusal can name ITSELF rather than
		// collapsing into one generic "the store is not available"
		CHECK(money.initializeStore());
	}
	else
	{
		// no provider at all: the service refuses on its own, still through the
		// ordinary callback
		CHECK_FALSE(money.initializeStore());
	}

	int products = 0;
	int purchases = 0;
	int restores = 0;
	String productsReason;
	String purchaseReason;
	String restoreReason;
	PurchaseResult purchaseOutcome;

	money.requestProducts([&](ProductQueryResult const & result)
	{
		++products;
		productsReason = result.reason;
		// a query that did not complete MUST say why - an empty answer with no
		// reason is the failure mode this whole seam exists to prevent
		if(!result.completed) { CHECK_FALSE(result.reason.empty()); }
	});
	money.purchase("remove_ads", [&](PurchaseResult const & result)
	{
		++purchases;
		purchaseOutcome = result;
		purchaseReason = result.reason;
	});
	money.restore([&](RestoreResult const & result)
	{
		++restores;
		restoreReason = result.reason;
		if(!result.completed) { CHECK_FALSE(result.reason.empty()); }
	});

	REQUIRE(money.pendingCount() == 3);

	// a few frames: an immediate refusal is delivered at the NEXT update, and a
	// platform answer would arrive within a handful more
	for(int frame = 0; frame < 8; ++frame)
	{
		money.update();
	}

	CHECK(products == 1);
	CHECK(purchases == 1);
	CHECK(restores == 1);
	CHECK(money.pendingCount() == 0);

	// nothing was bought here on any platform - there is no store account - so
	// the purchase must NOT report ownership, and must say why
	CHECK_FALSE(purchaseOutcome.owned());
	CHECK_FALSE(purchaseReason.empty());
	CHECK(purchaseOutcome.productId == "remove_ads");

	// and nothing may leak into the entitlement cache from a refusal
	CHECK_FALSE(money.hasEntitlement("remove_ads"));
	CHECK_FALSE(money.isAdFree());
}

TEST_CASE("an empty catalog is refused by name, not by an empty answer",
	"[monetization][store]")
{
	std::unique_ptr<StoreProvider> provider(
		Orkige::createPlatformStoreProvider());
	if(provider.get() == NULL)
	{
		SUCCEED("this build carries no platform store");
		return;
	}

	MonetizationService money;
	money.setStoreProvider(std::move(provider));
	REQUIRE(money.initializeStore());

	int answers = 0;
	String reason;
	// the catalog is EMPTY: the provider is handed no identifiers at all
	money.requestProducts([&](ProductQueryResult const & result)
	{
		++answers;
		reason = result.reason;
		CHECK_FALSE(result.completed);
		CHECK(result.products.empty());
	});
	for(int frame = 0; frame < 8; ++frame)
	{
		money.update();
	}

	CHECK(answers == 1);
	// "no products came back" and "no products were asked for" look identical
	// to a caller, and send a developer hunting in the wrong console
	CHECK_FALSE(reason.empty());
}
