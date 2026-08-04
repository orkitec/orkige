/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	ProductCatalogTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless ProductCatalog unit tests: the per-storefront product identifier
	model. One logical product carries a DIFFERENT identifier on every store it
	is sold through, both directions of that mapping are load-bearing (a restore
	arrives as storefront identifiers with no request to correlate against), and
	the banner-geometry composition with the display safe area is asserted here
	too since both are pure data the seam depends on.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_monetization/MonetizationTypes.h>
#include <core_monetization/ProductCatalog.h>
#include <core_util/SafeArea.h>

using Orkige::Product;
using Orkige::ProductCatalog;
using Orkige::SafeAreaInsets;
using Orkige::String;

namespace
{
	//! a catalogued product
	Product product(String const & id, Orkige::ProductKind kind,
		bool grantsNoAds = false)
	{
		Product entry;
		entry.id = id;
		entry.kind = kind;
		entry.grantsNoAds = grantsNoAds;
		return entry;
	}
}

TEST_CASE("one logical product carries a different id per storefront",
	"[unit][monetization]")
{
	// the shape the whole store seam is built on: store consoles are
	// independent registries, so the identifier differs per platform and game
	// code must never have to know which one it is on
	ProductCatalog catalog;
	catalog.add(product("remove_ads", Orkige::PK_NON_CONSUMABLE, true));
	REQUIRE(catalog.addStoreId("remove_ads", Orkige::SF_IOS,
		"com.example.game.removeads"));
	REQUIRE(catalog.addStoreId("remove_ads", Orkige::SF_ANDROID,
		"remove_ads_v2"));

	REQUIRE(catalog.storeIdFor("remove_ads", Orkige::SF_IOS)
		== "com.example.game.removeads");
	REQUIRE(catalog.storeIdFor("remove_ads", Orkige::SF_ANDROID)
		== "remove_ads_v2");
	// a storefront it is not sold on answers empty rather than guessing
	REQUIRE(catalog.storeIdFor("remove_ads", Orkige::SF_WINDOWS).empty());
}

TEST_CASE("the reverse lookup names a storefront identifier",
	"[unit][monetization]")
{
	// LOAD-BEARING: a restore lists the store's own identifiers with no request
	// to correlate them against, so without this direction an entitlement
	// cannot be recorded at all
	ProductCatalog catalog;
	catalog.add(product("coins_500", Orkige::PK_CONSUMABLE));
	REQUIRE(catalog.addStoreId("coins_500", Orkige::SF_IOS, "coins.500"));
	REQUIRE(catalog.addStoreId("coins_500", Orkige::SF_ANDROID, "coins_pack_a"));

	REQUIRE(catalog.logicalIdFor(Orkige::SF_IOS, "coins.500") == "coins_500");
	REQUIRE(catalog.logicalIdFor(Orkige::SF_ANDROID, "coins_pack_a")
		== "coins_500");
	// the columns do not leak into each other
	REQUIRE(catalog.logicalIdFor(Orkige::SF_ANDROID, "coins.500").empty());
	REQUIRE(catalog.logicalIdFor(Orkige::SF_WEB, "coins.500").empty());
}

TEST_CASE("a storefront binding for an undeclared product is refused",
	"[unit][monetization]")
{
	// binding an identifier to a product nobody declared would look fine right
	// up until the purchase
	ProductCatalog catalog;
	REQUIRE_FALSE(catalog.addStoreId("never_declared", Orkige::SF_IOS, "x.y"));
	REQUIRE(catalog.logicalIdFor(Orkige::SF_IOS, "x.y").empty());
}

TEST_CASE("empty identifiers are refused rather than bound",
	"[unit][monetization]")
{
	ProductCatalog catalog;
	catalog.add(product("remove_ads", Orkige::PK_NON_CONSUMABLE, true));
	REQUIRE_FALSE(catalog.addStoreId("remove_ads", Orkige::SF_IOS, ""));
	REQUIRE_FALSE(catalog.addStoreId("", Orkige::SF_IOS, "some.id"));
	REQUIRE(catalog.storeIdsFor(Orkige::SF_IOS).empty());
}

TEST_CASE("re-adding a product keeps its storefront bindings",
	"[unit][monetization]")
{
	// a product query refreshes title and price; it must not silently unsell
	// the product on every storefront
	ProductCatalog catalog;
	catalog.add(product("remove_ads", Orkige::PK_NON_CONSUMABLE, true));
	REQUIRE(catalog.addStoreId("remove_ads", Orkige::SF_IOS, "rm.ads"));

	Product refreshed = product("remove_ads", Orkige::PK_NON_CONSUMABLE, true);
	refreshed.displayPrice = "2,99 EUR";
	catalog.add(refreshed);

	REQUIRE(catalog.storeIdFor("remove_ads", Orkige::SF_IOS) == "rm.ads");
	REQUIRE(catalog.find("remove_ads") != NULL);
	REQUIRE(catalog.find("remove_ads")->displayPrice == "2,99 EUR");
}

TEST_CASE("the catalog reports which product grants no-ads",
	"[unit][monetization]")
{
	// THE LINK between the two seams is a catalog fact, so every game does not
	// re-derive it
	ProductCatalog catalog;
	catalog.add(product("remove_ads", Orkige::PK_NON_CONSUMABLE, true));
	catalog.add(product("coins_500", Orkige::PK_CONSUMABLE));

	REQUIRE(catalog.grantsNoAds("remove_ads"));
	REQUIRE_FALSE(catalog.grantsNoAds("coins_500"));
	REQUIRE_FALSE(catalog.grantsNoAds("never_heard_of_it"));
}

TEST_CASE("the storefront identifier list is what a product query sends",
	"[unit][monetization]")
{
	ProductCatalog catalog;
	catalog.add(product("a", Orkige::PK_CONSUMABLE));
	catalog.add(product("b", Orkige::PK_NON_CONSUMABLE));
	REQUIRE(catalog.addStoreId("a", Orkige::SF_IOS, "store.a"));
	REQUIRE(catalog.addStoreId("b", Orkige::SF_IOS, "store.b"));
	REQUIRE(catalog.addStoreId("a", Orkige::SF_ANDROID, "android_a"));

	const Orkige::StringVector ios = catalog.storeIdsFor(Orkige::SF_IOS);
	REQUIRE(ios.size() == 2);
	const Orkige::StringVector android = catalog.storeIdsFor(Orkige::SF_ANDROID);
	REQUIRE(android.size() == 1);
	REQUIRE(android[0] == "android_a");
	REQUIRE(catalog.storeIdsFor(Orkige::SF_WEB).empty());
}

TEST_CASE("the vocabulary tokens round-trip", "[unit][monetization]")
{
	// the tokens are what the future script table and MCP verb trade in, so a
	// name that does not survive a round trip is a silent protocol break
	REQUIRE(Orkige::storefrontName(Orkige::SF_ANDROID) == "android");
	REQUIRE(Orkige::storefrontFromName("android") == Orkige::SF_ANDROID);
	REQUIRE(Orkige::storefrontFromName("nonsense") == Orkige::SF_UNKNOWN);

	Orkige::ProductKind kind = Orkige::PK_CONSUMABLE;
	REQUIRE(Orkige::productKindFromName("subscription", kind));
	REQUIRE(kind == Orkige::PK_SUBSCRIPTION);
	REQUIRE(Orkige::productKindName(kind) == "subscription");

	Orkige::AdFormat format = Orkige::AF_BANNER;
	REQUIRE(Orkige::adFormatFromName("app_open", format));
	REQUIRE(format == Orkige::AF_APP_OPEN);
	REQUIRE(Orkige::adFormatName(format) == "app_open");

	Orkige::AdLoadResult load = Orkige::ALR_LOADED;
	REQUIRE(Orkige::adLoadResultFromName("no_fill", load));
	REQUIRE(load == Orkige::ALR_NO_FILL);

	Orkige::AdShowResult show = Orkige::ASR_COMPLETED;
	REQUIRE(Orkige::adShowResultFromName("reward_earned", show));
	REQUIRE(show == Orkige::ASR_REWARD_EARNED);

	Orkige::PurchaseState state = Orkige::PS_PURCHASED;
	REQUIRE(Orkige::purchaseStateFromName("already_owned", state));
	REQUIRE(state == Orkige::PS_ALREADY_OWNED);
	REQUIRE(Orkige::purchaseStateName(state) == "already_owned");

	Orkige::ConsentStatus consent = Orkige::CS_NOT_GATHERED;
	REQUIRE(Orkige::consentStatusFromName("restricted", consent));
	REQUIRE(consent == Orkige::CS_RESTRICTED);
}

TEST_CASE("only a banner is an overlay - the other formats take the app over",
	"[unit][monetization]")
{
	REQUIRE_FALSE(Orkige::adFormatIsTakeover(Orkige::AF_BANNER));
	REQUIRE(Orkige::adFormatIsTakeover(Orkige::AF_INTERSTITIAL));
	REQUIRE(Orkige::adFormatIsTakeover(Orkige::AF_REWARDED));
	REQUIRE(Orkige::adFormatIsTakeover(Orkige::AF_APP_OPEN));
}

TEST_CASE("a banner's height composes with the display safe area",
	"[unit][monetization]")
{
	// the fault this arithmetic exists to prevent: a HUD anchored to the bottom
	// safe edge sits UNDERNEATH the advert, and with no advert in development
	// nobody sees it until release
	SafeAreaInsets display;
	display.mTop = 47;		// a notch
	display.mBottom = 34;	// a home indicator

	Orkige::BannerGeometry banner;
	banner.visible = true;
	banner.position = Orkige::BP_BOTTOM;
	banner.width = 320;
	banner.height = 50;
	banner.insideSafeArea = true;

	const SafeAreaInsets composed = banner.composeWith(display);
	// the strip is parked against the safe edge, so its whole height is space
	// the UI additionally cannot use
	REQUIRE(composed.mBottom == 84);
	// the other edges are untouched
	REQUIRE(composed.mTop == 47);
	REQUIRE(composed.mLeft == 0);
	REQUIRE(composed.mRight == 0);
}

TEST_CASE("a top banner eats the top inset instead", "[unit][monetization]")
{
	SafeAreaInsets display;
	display.mTop = 47;
	display.mBottom = 34;

	Orkige::BannerGeometry banner;
	banner.visible = true;
	banner.position = Orkige::BP_TOP;
	banner.height = 50;

	const SafeAreaInsets composed = banner.composeWith(display);
	REQUIRE(composed.mTop == 97);
	REQUIRE(composed.mBottom == 34);
}

TEST_CASE("an edge-to-edge banner only takes the space beyond the inset",
	"[unit][monetization]")
{
	// a banner drawn from the physical window edge already covers the display's
	// own inset, so adding the two would reserve the same strip twice
	SafeAreaInsets display;
	display.mBottom = 34;

	Orkige::BannerGeometry banner;
	banner.visible = true;
	banner.position = Orkige::BP_BOTTOM;
	banner.height = 50;
	banner.insideSafeArea = false;

	REQUIRE(banner.composeWith(display).mBottom == 50);

	// and one shorter than the existing inset takes no new space at all
	banner.height = 20;
	REQUIRE(banner.composeWith(display).mBottom == 34);
}

TEST_CASE("no banner leaves the display insets exactly as they were",
	"[unit][monetization]")
{
	// a build that serves no advertising must lay out byte-identically to one
	// with no monetization at all
	SafeAreaInsets display;
	display.mTop = 47;
	display.mBottom = 34;

	Orkige::BannerGeometry none;
	REQUIRE(none.composeWith(display).mTop == 47);
	REQUIRE(none.composeWith(display).mBottom == 34);

	// "visible with no height" is the same nothing
	Orkige::BannerGeometry empty;
	empty.visible = true;
	empty.height = 0;
	REQUIRE(empty.composeWith(display).mBottom == 34);
}

TEST_CASE("the ad policy silences the interruptive formats but not rewarded",
	"[unit][monetization]")
{
	// a deliberate product decision: a rewarded advert is one the player CHOSE
	// to watch for something, so silencing it for a paying player removes a
	// mechanic they still want
	Orkige::AdPolicy policy;
	REQUIRE(policy.suppresses(Orkige::AF_BANNER));
	REQUIRE(policy.suppresses(Orkige::AF_INTERSTITIAL));
	REQUIRE(policy.suppresses(Orkige::AF_APP_OPEN));
	REQUIRE_FALSE(policy.suppresses(Orkige::AF_REWARDED));

	// and a game that disagrees flips the field
	policy.suppressRewarded = true;
	REQUIRE(policy.suppresses(Orkige::AF_REWARDED));
}

TEST_CASE("personalised ads need every consent gate to agree",
	"[unit][monetization]")
{
	Orkige::ConsentState consent;
	// nothing asked yet
	REQUIRE_FALSE(consent.gathered());
	REQUIRE_FALSE(consent.personalizedAds());

	consent.status = Orkige::CS_GRANTED;
	REQUIRE(consent.gathered());
	// the operating system's own tracking permission is a separate gate
	REQUIRE_FALSE(consent.personalizedAds());

	consent.trackingAuthorized = true;
	REQUIRE(consent.personalizedAds());

	// a child-directed app is never personalised, whatever anyone answered
	consent.childDirected = true;
	REQUIRE_FALSE(consent.personalizedAds());
	consent.childDirected = false;

	// and a refusal is a legal outcome, not an error - it is still "gathered"
	consent.status = Orkige::CS_DENIED;
	REQUIRE(consent.gathered());
	REQUIRE_FALSE(consent.personalizedAds());
}
