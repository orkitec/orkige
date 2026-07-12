/**************************************************************
	created:	2026/07/12 at 18:00
	filename: 	MaterialAssetTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless parser tests for the `.omat` text asset: a well-formed material
	round-trips every field, omitted directives keep their documented
	defaults, and every malformation (unknown/duplicate directive, wrong
	value counts, garbage/out-of-range values, unsupported version, empty
	text) fails honestly with defaults + a line-numbered error. Pure - no
	renderer.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/MaterialAsset.h"

using namespace Orkige;
using Catch::Approx;

TEST_CASE("material_parse_omat_valid", "[unit][material]")
{
	const String text =
		"# a full surface description\n"
		"version 1\n"
		"albedo 0.8 0.4 0.2 1.0\n"
		"albedoTexture rock_albedo.png   # trailing comment\n"
		"metalness 0.25\n"
		"roughness 0.6\n"
		"normalTexture rock_normal.png\n"
		"emissive 0.9 0.7 0.3\n"
		"emissiveTexture rock_glow.png\n";

	MaterialAsset::ParsedMaterial material;
	String error;
	REQUIRE(MaterialAsset::parse(text, material, &error));
	CHECK(error.empty());
	CHECK(material.albedo.r == Approx(0.8f));
	CHECK(material.albedo.g == Approx(0.4f));
	CHECK(material.albedo.b == Approx(0.2f));
	CHECK(material.albedo.a == Approx(1.0f));
	CHECK(material.albedoTexture == "rock_albedo.png");
	CHECK(material.metalness == Approx(0.25f));
	CHECK(material.roughness == Approx(0.6f));
	CHECK(material.normalTexture == "rock_normal.png");
	CHECK(material.emissive.r == Approx(0.9f));
	CHECK(material.emissive.g == Approx(0.7f));
	CHECK(material.emissive.b == Approx(0.3f));
	CHECK(material.emissiveTexture == "rock_glow.png");
}
//---------------------------------------------------------
TEST_CASE("material_parse_omat_defaults", "[unit][material]")
{
	// a minimal material: every omitted directive keeps its documented
	// default (a plain white dielectric, fully rough, no maps, no emission)
	MaterialAsset::ParsedMaterial material;
	REQUIRE(MaterialAsset::parse("albedo 1 0 0 1\n", material));
	CHECK(material.albedo.r == Approx(1.0f));
	CHECK(material.albedo.g == Approx(0.0f));
	CHECK(material.albedoTexture.empty());
	CHECK(material.metalness == Approx(0.0f));
	CHECK(material.roughness == Approx(1.0f));
	CHECK(material.normalTexture.empty());
	CHECK(material.emissive.r == Approx(0.0f));
	CHECK(material.emissiveTexture.empty());

	// `version 1` alone is a valid (all-defaults) material
	REQUIRE(MaterialAsset::parse("version 1\n", material));
	CHECK(material.roughness == Approx(1.0f));
}
//---------------------------------------------------------
TEST_CASE("material_parse_omat_malformed", "[unit][material]")
{
	MaterialAsset::ParsedMaterial material;
	String error;

	SECTION("empty / comment-only text is not a material")
	{
		CHECK_FALSE(MaterialAsset::parse("", material, &error));
		CHECK_FALSE(error.empty());
		CHECK_FALSE(MaterialAsset::parse("# nothing here\n\n", material,
			&error));
	}
	SECTION("an unknown directive is an error, not ignored")
	{
		CHECK_FALSE(MaterialAsset::parse(
			"version 1\nmetallness 0.5\n", material, &error));
		CHECK(error.find("line 2") != String::npos);
		CHECK(error.find("metallness") != String::npos);
	}
	SECTION("a duplicate directive is an error")
	{
		CHECK_FALSE(MaterialAsset::parse(
			"roughness 0.5\nroughness 0.6\n", material, &error));
		CHECK(error.find("line 2") != String::npos);
		CHECK(error.find("duplicate") != String::npos);
	}
	SECTION("wrong value counts fail")
	{
		// too few
		CHECK_FALSE(MaterialAsset::parse("albedo 1 0 0\n", material, &error));
		CHECK(error.find("line 1") != String::npos);
		// too many (trailing token)
		CHECK_FALSE(MaterialAsset::parse("metalness 0.5 0.5\n", material,
			&error));
		CHECK_FALSE(MaterialAsset::parse("emissive 1 1 1 1\n", material,
			&error));
		// a texture directive without a name
		CHECK_FALSE(MaterialAsset::parse("albedoTexture\n", material,
			&error));
	}
	SECTION("garbage and out-of-range values fail")
	{
		CHECK_FALSE(MaterialAsset::parse("roughness smooth\n", material,
			&error));
		CHECK_FALSE(MaterialAsset::parse("metalness 1.5\n", material,
			&error));
		CHECK_FALSE(MaterialAsset::parse("metalness -0.1\n", material,
			&error));
		CHECK_FALSE(MaterialAsset::parse("albedo 1 1 1 2\n", material,
			&error));
	}
	SECTION("only version 1 is accepted")
	{
		CHECK_FALSE(MaterialAsset::parse("version 2\nroughness 0.5\n",
			material, &error));
		CHECK(error.find("version") != String::npos);
		CHECK_FALSE(MaterialAsset::parse("version zero\n", material, &error));
	}
	SECTION("failure leaves the out material at the defaults")
	{
		material.roughness = 0.123f;
		material.albedoTexture = "stale.png";
		CHECK_FALSE(MaterialAsset::parse(
			"albedo 1 1 1 1\nbogus 1\n", material, &error));
		CHECK(material.roughness == Approx(1.0f));
		CHECK(material.albedoTexture.empty());
	}
}
