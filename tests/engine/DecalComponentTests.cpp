/**************************************************************
	created:	2026/07/18 at 01:00
	filename: 	DecalComponentTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	DecalComponent's headless half: the reflected property schema (the ONE
	registry inspector/serialization/MCP/Lua ride), the detached round-trip
	through the type-erased property drive, and the PURE lifetime/fade curve
	(fadeFactor). Runs on DETACHED components (no Ogre::Root, no scene nodes)
	like the rest of tests/engine; the live backend behaviour (a projected/
	aligned decal marking a surface, the opacity fade, the world visible-decal
	budget evicting the oldest, and the toggle-identity budget-0) is covered by
	the render_facade_selfcheck decal leg on both flavors.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/DecalComponent.h>

#include <core_base/PropertyValue.h>
#include <core_base/PropertySchema.h>
#include <core_base/TypeManager.h>

//---------------------------------------------------------
TEST_CASE("DecalComponent declares its reflected schema",
	"[reflection][decal]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		DecalComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// the mark texture is an AssetRef (rename-tracked like every asset field)
	PropertyDesc const * texture = schema->find("texture");
	REQUIRE(texture != nullptr);
	CHECK(texture->kind == PropertyKind::AssetRef);

	// the footprint + fade knobs are floats
	for(char const * name : { "sizeX", "sizeZ", "projectionDepth", "opacity",
		"lifetime", "fadeDuration" })
	{
		PropertyDesc const * desc = schema->find(name);
		INFO("property " << name);
		REQUIRE(desc != nullptr);
		CHECK(desc->kind == PropertyKind::Float);
		CHECK_FALSE(desc->isReadOnly());
	}
}
//---------------------------------------------------------
TEST_CASE("DecalComponent properties round-trip on a DETACHED component",
	"[reflection][decal]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		DecalComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// a detached component (no owner, no node) records state; the backend apply
	// happens when a facade decal exists (onAdd under a live runtime)
	DecalComponent decal;
	Object * instance = &decal;

	PropertyDesc const * opacity = schema->find("opacity");
	REQUIRE(opacity != nullptr);
	opacity->set(instance, PropertyValue::makeFloat(0.25f));
	CHECK(decal.getOpacity() == 0.25f);
	CHECK(opacity->get(instance).asFloat() == 0.25f);

	PropertyDesc const * lifetime = schema->find("lifetime");
	REQUIRE(lifetime != nullptr);
	lifetime->set(instance, PropertyValue::makeFloat(3.0f));
	CHECK(decal.getLifetime() == 3.0f);
	CHECK(lifetime->get(instance).asFloat() == 3.0f);

	PropertyDesc const * texture = schema->find("texture");
	REQUIRE(texture != nullptr);
	texture->set(instance, PropertyValue::makeString("splat.png"));
	CHECK(decal.getTexture() == "splat.png");
	CHECK(texture->get(instance).asString() == "splat.png");

	// opacity is clamped to 0..1 through the setter
	decal.setOpacity(4.0f);
	CHECK(decal.getOpacity() == 1.0f);
	decal.setOpacity(-1.0f);
	CHECK(decal.getOpacity() == 0.0f);
}
//---------------------------------------------------------
TEST_CASE("DecalComponent::fadeFactor is the pure lifetime/fade curve",
	"[decal][fade]")
{
	using namespace Orkige;

	// permanent: a lifetime of 0 never fades (factor stays 1 at any age)
	CHECK(DecalComponent::fadeFactor(0.0f, 0.0f, 0.5f, 0.0f, 0.0f) == 1.0f);
	CHECK(DecalComponent::fadeFactor(100.0f, 0.0f, 0.5f, 0.0f, 0.0f) == 1.0f);

	// within life, before the fade window: full opacity
	CHECK(DecalComponent::fadeFactor(1.0f, 5.0f, 1.0f, 0.0f, 0.0f) == 1.0f);

	// inside the fade window: a linear ramp toward 0 (remaining / fadeDuration)
	// lifetime 5, fade 2 -> at age 4 (1s remaining) the factor is 0.5
	CHECK(DecalComponent::fadeFactor(4.0f, 5.0f, 2.0f, 0.0f, 0.0f) == 0.5f);

	// expired: age at/after the lifetime is fully faded out
	CHECK(DecalComponent::fadeFactor(5.0f, 5.0f, 2.0f, 0.0f, 0.0f) == 0.0f);
	CHECK(DecalComponent::fadeFactor(9.0f, 5.0f, 2.0f, 0.0f, 0.0f) == 0.0f);

	// a manual fade wins over the lifetime ramp: half-elapsed = half opacity
	CHECK(DecalComponent::fadeFactor(0.0f, 0.0f, 0.0f, 1.0f, 0.5f) == 0.5f);
	// a completed manual fade is fully hidden
	CHECK(DecalComponent::fadeFactor(0.0f, 0.0f, 0.0f, 1.0f, 1.0f) == 0.0f);
}
