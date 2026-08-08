/********************************************************************
	created:	Saturday 2026/08/08 at 12:00
	filename: 	WaterTuningTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

// The live `water.*` look tier, headless: registration, the defaults that make
// an untouched run byte-identical, and the clamp bands. No render system boots
// here - the accessors read the cvar registry, and the tier's onChange hook is
// an honest no-op while no RenderSystem exists, which is exactly what this
// binary proves by calling registerCVars() and setting values in-process.
#include <catch2/catch_test_macros.hpp>

#include <core_debug/CVarManager.h>
#include <engine_render/RenderWaterTuning.h>

using Orkige::CVar;
using Orkige::CVarManager;
using Orkige::CVarType;
namespace WaterTuning = Orkige::WaterTuning;

namespace
{
	//! the tier's six names, in the order the doc lists them
	char const * const kNames[] =
	{
		WaterTuning::CVAR_MIRROR_SPECULAR,
		WaterTuning::CVAR_MIRROR_FRESNEL_SCALE,
		WaterTuning::CVAR_MIRROR_ALBEDO_SCALE,
		WaterTuning::CVAR_MIRROR_LOD,
		WaterTuning::CVAR_MIRROR_DISTORT,
		WaterTuning::CVAR_MIRROR_ROUGHNESS
	};

	//! put the whole tier back on its defaults (the registry singleton lives
	//! across Catch2 cases, so every case starts from a known state)
	void resetTier()
	{
		WaterTuning::registerCVars();
		for (char const * name : kNames)
		{
			CVarManager::getSingleton().reset(name);
		}
	}
}

TEST_CASE("the water.* tier registers as session-scoped float knobs",
	"[engine][render][water][cvar]")
{
	resetTier();
	CVarManager & cvars = CVarManager::getSingleton();

	for (char const * name : kNames)
	{
		CVar const * cvar = cvars.find(name);
		REQUIRE(cvar != nullptr);
		CHECK(cvar->type == CVarType::Float);
		// look-dev overrides are SESSION-scoped: persisting one would write a
		// dialled-in experiment into the project manifest
		CHECK_FALSE(cvar->hasFlag(Orkige::CVAR_PERSIST));
		CHECK_FALSE(cvar->hasFlag(Orkige::CVAR_READONLY));
		// every knob says what it drives, and on which flavor
		CHECK_FALSE(cvar->description.empty());
	}

	// the whole tier answers one prefix, so the console's `find water.` and the
	// documentation list the same six knobs
	CHECK(cvars.findByPrefix("water.").size() == 6u);
}

TEST_CASE("the water.* defaults reproduce the baked look constants exactly",
	"[engine][render][water][cvar]")
{
	// THE byte-identity contract: a run that sets nothing must render exactly
	// what it rendered before the tier existed, so each default is the constant
	// its flavor baked in and each multiplier is the identity.
	CHECK(WaterTuning::DEFAULT_MIRROR_SPECULAR == 0.43f);
	CHECK(WaterTuning::DEFAULT_MIRROR_FRESNEL_SCALE == 1.0f);
	CHECK(WaterTuning::DEFAULT_MIRROR_ALBEDO_SCALE == 1.0f);
	CHECK(WaterTuning::DEFAULT_MIRROR_LOD == 0.05f);
	CHECK(WaterTuning::DEFAULT_MIRROR_DISTORT == 0.09f);
	CHECK(WaterTuning::DEFAULT_MIRROR_ROUGHNESS == 0.16f);

	// and the accessors hand back exactly those values - through the registry's
	// canonical string round trip, which must not perturb a single bit
	resetTier();
	CHECK(WaterTuning::mirrorSpecular() == WaterTuning::DEFAULT_MIRROR_SPECULAR);
	CHECK(WaterTuning::mirrorFresnelScale() ==
		WaterTuning::DEFAULT_MIRROR_FRESNEL_SCALE);
	CHECK(WaterTuning::mirrorAlbedoScale() ==
		WaterTuning::DEFAULT_MIRROR_ALBEDO_SCALE);
	CHECK(WaterTuning::mirrorLod() == WaterTuning::DEFAULT_MIRROR_LOD);
	CHECK(WaterTuning::mirrorDistort() == WaterTuning::DEFAULT_MIRROR_DISTORT);
	CHECK(WaterTuning::mirrorRoughness() ==
		WaterTuning::DEFAULT_MIRROR_ROUGHNESS);
}

TEST_CASE("an unregistered water.* tier still answers its defaults",
	"[engine][render][water][cvar]")
{
	// a process that never registered the tier (a tool linking the facade, a
	// backend built before the app boot ran) must still get the shipped look,
	// never zero - the accessors fall back rather than depend on registration
	CVarManager & cvars = CVarManager::getSingleton();
	if (!cvars.exists(WaterTuning::CVAR_MIRROR_SPECULAR))
	{
		CHECK(WaterTuning::mirrorSpecular() ==
			WaterTuning::DEFAULT_MIRROR_SPECULAR);
	}
	// the fallback is the same constant either way, so the assertion above is
	// the interesting one only on a fresh registry; this one always holds
	resetTier();
	CHECK(WaterTuning::mirrorSpecular() ==
		WaterTuning::DEFAULT_MIRROR_SPECULAR);
}

TEST_CASE("setting a water.* knob moves what the backends read",
	"[engine][render][water][cvar]")
{
	resetTier();
	CVarManager & cvars = CVarManager::getSingleton();

	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_SPECULAR, "0.8"));
	CHECK(WaterTuning::mirrorSpecular() == 0.8f);
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_FRESNEL_SCALE, "2.5"));
	CHECK(WaterTuning::mirrorFresnelScale() == 2.5f);
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_ALBEDO_SCALE, "0.25"));
	CHECK(WaterTuning::mirrorAlbedoScale() == 0.25f);
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_LOD, "0.5"));
	CHECK(WaterTuning::mirrorLod() == 0.5f);
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_DISTORT, "0.4"));
	CHECK(WaterTuning::mirrorDistort() == 0.4f);
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_ROUGHNESS, "0.6"));
	CHECK(WaterTuning::mirrorRoughness() == 0.6f);

	// a reset puts the shipped look back (the look-dev round trip)
	resetTier();
	CHECK(WaterTuning::mirrorSpecular() == WaterTuning::DEFAULT_MIRROR_SPECULAR);
	CHECK(WaterTuning::mirrorLod() == WaterTuning::DEFAULT_MIRROR_LOD);
}

TEST_CASE("water.* values outside their band are clamped, not refused",
	"[engine][render][water][cvar]")
{
	resetTier();
	CVarManager & cvars = CVarManager::getSingleton();

	// a weight/fraction knob lives in [0;1]
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_SPECULAR, "7"));
	CHECK(WaterTuning::mirrorSpecular() == 1.0f);
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_SPECULAR, "-3"));
	CHECK(WaterTuning::mirrorSpecular() == 0.0f);
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_LOD, "9"));
	CHECK(WaterTuning::mirrorLod() == 1.0f);
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_DISTORT, "-1"));
	CHECK(WaterTuning::mirrorDistort() == 0.0f);
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_ROUGHNESS, "4"));
	CHECK(WaterTuning::mirrorRoughness() == 1.0f);

	// a multiplier gets a generous band, and still a finite one
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_FRESNEL_SCALE, "1000"));
	CHECK(WaterTuning::mirrorFresnelScale() == WaterTuning::MAX_SCALE);
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_ALBEDO_SCALE, "-5"));
	CHECK(WaterTuning::mirrorAlbedoScale() == 0.0f);

	// the CVAR keeps the string the owner typed - clamping is a READ-side
	// decision of the look, never a rewrite of what was set
	CHECK(cvars.find(WaterTuning::CVAR_MIRROR_SPECULAR)->asFloat() == -3.0f);

	resetTier();
}

TEST_CASE("a water.* set fires its live re-apply hook with no render system",
	"[engine][render][water][cvar]")
{
	// the tier's onChange calls RenderSystem::refreshWaterLook() through
	// RenderSystem::get(); in a headless process there is no render system, so
	// the hook must be an honest no-op rather than a null dereference. Reaching
	// the end of this case IS the assertion.
	resetTier();
	CVarManager & cvars = CVarManager::getSingleton();
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_SPECULAR, "0.1"));
	REQUIRE(cvars.setString(WaterTuning::CVAR_MIRROR_ALBEDO_SCALE, "3"));
	REQUIRE(cvars.reset(WaterTuning::CVAR_MIRROR_SPECULAR));
	resetTier();
	SUCCEED("the water.* re-apply hook is a no-op without a render system");
}
