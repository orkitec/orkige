/********************************************************************
	created:	Tuesday 2026/08/04 at 12:00
	filename: 	RenderMaterialCacheTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

// Headless coverage for the decision in front of both backends' material
// builders (engine_render/RenderMaterialCache.h): may a named material that is
// already live be left alone? Getting this wrong is expensive in two
// directions - too eager and a scene of N meshes sharing one `.omat` pays a
// quadratic rebuild as a load stall, too lazy and an edited asset stops
// reaching the surfaces already using it. Both directions are pinned here, on
// a standalone cache (no render system, no backend), so the rule is the same
// on either flavor.
#include <catch2/catch_test_macros.hpp>
#include <engine_render/RenderMaterialCache.h>

using Orkige::materialDescEqual;
using Orkige::RenderMaterialCache;
using Orkige::RenderMaterialDesc;

namespace
{
	//! a fully-populated description, so a field-by-field mutation below
	//! actually changes something
	RenderMaterialDesc sampleDesc()
	{
		RenderMaterialDesc desc;
		desc.albedo = Orkige::Color(0.25f, 0.5f, 0.75f, 1.0f);
		desc.albedoTexture = "bark.png";
		desc.metalness = 0.3f;
		desc.roughness = 0.6f;
		desc.normalTexture = "bark_n.png";
		desc.emissive = Orkige::Color(0.1f, 0.0f, 0.0f, 1.0f);
		desc.emissiveTexture = "bark_e.png";
		desc.alphaTest = 0.5f;
		desc.twoSided = true;
		return desc;
	}

	//! two distinct stand-ins for "the backend material object" - the cache
	//! only ever compares the address, never dereferences it
	int gMaterialA = 0;
	int gMaterialB = 0;
	void const * const MATERIAL_A = &gMaterialA;
	void const * const MATERIAL_B = &gMaterialB;
}

TEST_CASE("an unchanged description compares equal", "[engine][render][material]")
{
	CHECK(materialDescEqual(RenderMaterialDesc(), RenderMaterialDesc()));
	CHECK(materialDescEqual(sampleDesc(), sampleDesc()));
}

TEST_CASE("every authored field counts as a change", "[engine][render][material]")
{
	// EVERY field, one at a time: a field the comparison forgets is a field an
	// `.omat` edit could change without ever reaching the screen
	{
		RenderMaterialDesc changed = sampleDesc();
		changed.albedo = Orkige::Color(0.26f, 0.5f, 0.75f, 1.0f);
		CHECK_FALSE(materialDescEqual(sampleDesc(), changed));
	}
	{
		RenderMaterialDesc changed = sampleDesc();
		changed.albedoTexture = "stone.png";
		CHECK_FALSE(materialDescEqual(sampleDesc(), changed));
	}
	{
		RenderMaterialDesc changed = sampleDesc();
		changed.metalness = 0.31f;
		CHECK_FALSE(materialDescEqual(sampleDesc(), changed));
	}
	{
		RenderMaterialDesc changed = sampleDesc();
		changed.roughness = 0.61f;
		CHECK_FALSE(materialDescEqual(sampleDesc(), changed));
	}
	{
		RenderMaterialDesc changed = sampleDesc();
		changed.normalTexture = "";
		CHECK_FALSE(materialDescEqual(sampleDesc(), changed));
	}
	{
		RenderMaterialDesc changed = sampleDesc();
		changed.emissive = Orkige::Color(0.0f, 0.0f, 0.0f, 1.0f);
		CHECK_FALSE(materialDescEqual(sampleDesc(), changed));
	}
	{
		RenderMaterialDesc changed = sampleDesc();
		changed.emissiveTexture = "";
		CHECK_FALSE(materialDescEqual(sampleDesc(), changed));
	}
	{
		RenderMaterialDesc changed = sampleDesc();
		changed.alphaTest = 0.0f;
		CHECK_FALSE(materialDescEqual(sampleDesc(), changed));
	}
	{
		RenderMaterialDesc changed = sampleDesc();
		changed.twoSided = false;
		CHECK_FALSE(materialDescEqual(sampleDesc(), changed));
	}
}

TEST_CASE("a material nothing is known about is built", "[engine][render][material]")
{
	RenderMaterialCache cache;
	// no entry at all
	CHECK(cache.needsBuild("Omat/bark.omat", MATERIAL_A, sampleDesc()));
	// nothing lives under the name yet, whatever the memo says
	cache.recordBuilt("Omat/bark.omat", MATERIAL_A, sampleDesc());
	CHECK(cache.needsBuild("Omat/bark.omat", NULL, sampleDesc()));
	// a DIFFERENT name is a different material
	CHECK(cache.needsBuild("Omat/stone.omat", MATERIAL_A, sampleDesc()));
}

TEST_CASE("the same description on the same material is not rebuilt",
	"[engine][render][material]")
{
	RenderMaterialCache cache;
	cache.recordBuilt("Omat/bark.omat", MATERIAL_A, sampleDesc());
	// THE SHARING CASE: every further instance naming this material asks the
	// same question and must be told there is nothing to do
	CHECK_FALSE(cache.needsBuild("Omat/bark.omat", MATERIAL_A, sampleDesc()));
	CHECK_FALSE(cache.needsBuild("Omat/bark.omat", MATERIAL_A, sampleDesc()));
	CHECK(cache.buildCount() == 1);
}

TEST_CASE("an edited asset still rebuilds", "[engine][render][material]")
{
	RenderMaterialCache cache;
	cache.recordBuilt("Omat/bark.omat", MATERIAL_A, sampleDesc());
	// the hot-reload contract: a changed `.omat` parses to a different
	// description, so the update still reaches every surface using it
	RenderMaterialDesc edited = sampleDesc();
	edited.roughness = 0.9f;
	CHECK(cache.needsBuild("Omat/bark.omat", MATERIAL_A, edited));
}

TEST_CASE("a different material object under the same name rebuilds",
	"[engine][render][material]")
{
	RenderMaterialCache cache;
	cache.recordBuilt("Omat/bark.omat", MATERIAL_A, sampleDesc());
	// the entry describes the object it was recorded against - a material
	// that was destroyed and remade is not the one the memo remembers
	CHECK(cache.needsBuild("Omat/bark.omat", MATERIAL_B, sampleDesc()));
}

TEST_CASE("forgetting a material makes it build again", "[engine][render][material]")
{
	RenderMaterialCache cache;
	cache.recordBuilt("Omat/bark.omat", MATERIAL_A, sampleDesc());
	REQUIRE_FALSE(cache.needsBuild("Omat/bark.omat", MATERIAL_A, sampleDesc()));
	// the INCOMPLETE-build answer: a description whose texture was missing is
	// never remembered, so the next call tries again once the map can resolve
	cache.forget("Omat/bark.omat");
	CHECK(cache.needsBuild("Omat/bark.omat", MATERIAL_A, sampleDesc()));
	CHECK(cache.size() == 0);
}

TEST_CASE("a build that produced nothing is not remembered",
	"[engine][render][material]")
{
	RenderMaterialCache cache;
	cache.recordBuilt("Omat/bark.omat", MATERIAL_A, sampleDesc());
	// a failed build hands back no material - the entry must go, not linger
	cache.recordBuilt("Omat/bark.omat", NULL, sampleDesc());
	CHECK(cache.size() == 0);
	CHECK(cache.needsBuild("Omat/bark.omat", MATERIAL_A, sampleDesc()));
	// and it does not count as a build
	CHECK(cache.buildCount() == 1);
}

TEST_CASE("clearing drops every entry", "[engine][render][material]")
{
	RenderMaterialCache cache;
	cache.recordBuilt("Omat/bark.omat", MATERIAL_A, sampleDesc());
	cache.recordBuilt("Omat/stone.omat", MATERIAL_B, RenderMaterialDesc());
	REQUIRE(cache.size() == 2);
	// render-system teardown: the memo describes materials that are going away
	cache.clear();
	CHECK(cache.size() == 0);
	CHECK(cache.needsBuild("Omat/bark.omat", MATERIAL_A, sampleDesc()));
	// the counter is a cumulative record of work done, not of live entries -
	// a scene load reads it as a DELTA
	CHECK(cache.buildCount() == 2);
}
