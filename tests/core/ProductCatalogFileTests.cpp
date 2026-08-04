/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	ProductCatalogFileTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the `.ocatalog` parse: the per-storefront identifier
	columns and both directions of the mapping, the product kinds, the no-ads
	link between the store and ad sides, the round trip through serialize, and
	every refusal - because a typo silently ignored here is an unbuyable product
	discovered by a player.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_monetization/ProductCatalog.h>
#include <core_monetization/ProductCatalogFile.h>

using Orkige::Product;
using Orkige::ProductCatalog;
using Orkige::ProductCatalogFile;
using Orkige::String;

TEST_CASE("a catalog file maps one logical product onto every storefront",
	"[monetization][catalog]")
{
	const String text =
		"version 1\n"
		"\n"
		"# the most common purchase in existence\n"
		"product remove_ads\n"
		"\tkind non_consumable\n"
		"\tnoads true\n"
		"\tios com.example.game.removeads\n"
		"\tandroid remove_ads_v2\n"
		"\tsimulated remove_ads\n";

	ProductCatalog catalog;
	String error;
	REQUIRE(ProductCatalogFile::parse(text, catalog, &error));
	CHECK(error.empty());
	REQUIRE(catalog.count() == 1);

	Product const * product = catalog.find("remove_ads");
	REQUIRE(product != NULL);
	CHECK(product->kind == Orkige::PK_NON_CONSUMABLE);
	CHECK(product->grantsNoAds);
	CHECK(catalog.grantsNoAds("remove_ads"));

	// the identifier that travels to a storefront differs per platform
	CHECK(catalog.storeIdFor("remove_ads", Orkige::SF_IOS)
		== "com.example.game.removeads");
	CHECK(catalog.storeIdFor("remove_ads", Orkige::SF_ANDROID)
		== "remove_ads_v2");
	CHECK(catalog.storeIdFor("remove_ads", Orkige::SF_MACOS).empty());

	// THE REVERSE INDEX is what names a restored entitlement, which arrives
	// with no request to correlate against
	CHECK(catalog.logicalIdFor(Orkige::SF_IOS, "com.example.game.removeads")
		== "remove_ads");
	CHECK(catalog.logicalIdFor(Orkige::SF_ANDROID, "remove_ads_v2")
		== "remove_ads");
	CHECK(catalog.logicalIdFor(Orkige::SF_IOS, "remove_ads_v2").empty());
}

TEST_CASE("a catalog carries several products with their own kinds",
	"[monetization][catalog]")
{
	const String text =
		"product coins_500\n"
		"  kind consumable\n"
		"  ios com.example.game.coins500\n"
		"product season_pass\n"
		"  kind subscription\n"
		"  ios com.example.game.season\n"
		"product full_version\n"
		"  kind non_consumable\n"
		"  ios com.example.game.full\n";

	ProductCatalog catalog;
	REQUIRE(ProductCatalogFile::parse(text, catalog, NULL));
	REQUIRE(catalog.count() == 3);

	CHECK(catalog.find("coins_500")->kind == Orkige::PK_CONSUMABLE);
	CHECK(catalog.find("season_pass")->kind == Orkige::PK_SUBSCRIPTION);
	CHECK(catalog.find("full_version")->kind == Orkige::PK_NON_CONSUMABLE);

	// nothing grants no-ads unless it says so
	CHECK_FALSE(catalog.grantsNoAds("full_version"));

	const Orkige::StringVector iosIds = catalog.storeIdsFor(Orkige::SF_IOS);
	CHECK(iosIds.size() == 3);
}

TEST_CASE("catalog keywords are case-insensitive and comments are ignored",
	"[monetization][catalog]")
{
	const String text =
		"# a whole-line comment\n"
		"PRODUCT remove_ads   # trailing comment\n"
		"  KIND Non_Consumable\n"
		"  NoAds TRUE\n"
		"  IOS com.example.removeads\n";

	ProductCatalog catalog;
	String error;
	REQUIRE(ProductCatalogFile::parse(text, catalog, &error));
	REQUIRE(catalog.count() == 1);
	CHECK(catalog.find("remove_ads")->kind == Orkige::PK_NON_CONSUMABLE);
	CHECK(catalog.find("remove_ads")->grantsNoAds);
	// the IDENTIFIERS keep their case - a storefront's are case-sensitive
	CHECK(catalog.storeIdFor("remove_ads", Orkige::SF_IOS)
		== "com.example.removeads");
}

TEST_CASE("an empty catalog file is a valid, empty catalog",
	"[monetization][catalog]")
{
	ProductCatalog catalog;
	String error;
	// "" and a comment-only file both parse; a game with nothing to sell is
	// ordinary, and the refusal for it belongs at the store, not here
	CHECK(ProductCatalogFile::parse("", catalog, &error));
	CHECK(catalog.count() == 0);
	CHECK(ProductCatalogFile::parse("# nothing yet\n", catalog, &error));
	CHECK(catalog.count() == 0);
}

TEST_CASE("a catalog refuses what it does not understand, by line",
	"[monetization][catalog]")
{
	ProductCatalog catalog;
	String error;

	SECTION("an unknown directive")
	{
		CHECK_FALSE(ProductCatalogFile::parse(
			"product a\n  colour red\n", catalog, &error));
		CHECK(error.find("line 2") == 0);
	}
	SECTION("a storefront line with no product open")
	{
		CHECK_FALSE(ProductCatalogFile::parse(
			"ios com.example.thing\n", catalog, &error));
		CHECK(error.find("line 1") == 0);
	}
	SECTION("an unknown product kind")
	{
		CHECK_FALSE(ProductCatalogFile::parse(
			"product a\n  kind rental\n", catalog, &error));
		CHECK(error.find("line 2") == 0);
	}
	SECTION("a duplicate logical id")
	{
		CHECK_FALSE(ProductCatalogFile::parse(
			"product a\nproduct a\n", catalog, &error));
		CHECK(error.find("line 2") == 0);
	}
	SECTION("a product with no id")
	{
		CHECK_FALSE(ProductCatalogFile::parse("product\n", catalog, &error));
		CHECK(error.find("line 1") == 0);
	}
	SECTION("a storefront line with no identifier")
	{
		CHECK_FALSE(ProductCatalogFile::parse(
			"product a\n  ios\n", catalog, &error));
		CHECK(error.find("line 2") == 0);
	}
	SECTION("a noads value that is not a boolean")
	{
		CHECK_FALSE(ProductCatalogFile::parse(
			"product a\n  noads maybe\n", catalog, &error));
		CHECK(error.find("line 2") == 0);
	}
	SECTION("a version that is not 1")
	{
		CHECK_FALSE(ProductCatalogFile::parse("version 2\n", catalog, &error));
		CHECK(error.find("line 1") == 0);
	}
	SECTION("a version that is not first")
	{
		CHECK_FALSE(ProductCatalogFile::parse(
			"product a\nversion 1\n", catalog, &error));
		CHECK(error.find("line 2") == 0);
	}
	SECTION("the unknown storefront token is not a column")
	{
		CHECK_FALSE(ProductCatalogFile::parse(
			"product a\n  unknown thing\n", catalog, &error));
		CHECK(error.find("line 2") == 0);
	}

	// a failed parse leaves NOTHING behind: a half-read catalog would sell some
	// products and silently refuse others
	CHECK(catalog.count() == 0);
}

TEST_CASE("a catalog round-trips through serialize", "[monetization][catalog]")
{
	const String text =
		"version 1\n"
		"product coins_500\n"
		"  kind consumable\n"
		"  ios com.example.game.coins500\n"
		"  android coins_500_v3\n"
		"product remove_ads\n"
		"  kind non_consumable\n"
		"  noads true\n"
		"  ios com.example.game.removeads\n"
		"  web removeads\n";

	ProductCatalog first;
	REQUIRE(ProductCatalogFile::parse(text, first, NULL));

	const String written = ProductCatalogFile::serialize(first);
	ProductCatalog second;
	String error;
	REQUIRE(ProductCatalogFile::parse(written, second, &error));
	CHECK(error.empty());

	REQUIRE(second.count() == first.count());
	const Orkige::StringVector ids = first.logicalIds();
	for(std::size_t i = 0; i < ids.size(); ++i)
	{
		Product const * a = first.find(ids[i]);
		Product const * b = second.find(ids[i]);
		REQUIRE(b != NULL);
		CHECK(a->kind == b->kind);
		CHECK(a->grantsNoAds == b->grantsNoAds);
		CHECK(first.storeIdFor(ids[i], Orkige::SF_IOS)
			== second.storeIdFor(ids[i], Orkige::SF_IOS));
		CHECK(first.storeIdFor(ids[i], Orkige::SF_ANDROID)
			== second.storeIdFor(ids[i], Orkige::SF_ANDROID));
		CHECK(first.storeIdFor(ids[i], Orkige::SF_WEB)
			== second.storeIdFor(ids[i], Orkige::SF_WEB));
	}

	// serializing twice is byte-identical, so a rewrite is a clean diff
	CHECK(ProductCatalogFile::serialize(second) == written);
}
