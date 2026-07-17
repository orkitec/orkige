/**************************************************************
	created:	2026/07/17 at 12:00
	filename: 	StaticFlagTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The static mobility flag's headless half: the reflected schema entry
	(inspector/serialization/MCP ride the ONE registry), the pure
	hierarchy rule (a static object needs a static or absent parent; a
	static parent cannot go dynamic under static children) and the
	detached round-trip through the type-erased property drive. Runs on
	DETACHED components (no Ogre::Root, no scene nodes) like the rest of
	tests/engine; the live backend behaviour (SCENE_STATIC /
	StaticGeometry, the mobility-contract warning + repair) is covered by
	the player_static_contract and render-toggle pixel ctests.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/TransformComponent.h>

#include <core_base/PropertyValue.h>
#include <core_base/PropertySchema.h>
#include <core_base/TypeManager.h>

//---------------------------------------------------------
TEST_CASE("TransformComponent declares the static mobility flag in its schema",
	"[reflection][static]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		TransformComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);
	PropertyDesc const * staticFlag = schema->find("static");
	REQUIRE(staticFlag != nullptr);
	CHECK(staticFlag->kind == PropertyKind::Bool);
	CHECK_FALSE(staticFlag->isReadOnly());

	// declaration ORDER is part of the contract: the transform fields come
	// first so a scene load places the object BEFORE freezing it
	bool sawPosition = false;
	bool sawStaticAfterPosition = false;
	for(PropertyDesc const & each : schema->properties())
	{
		if(each.name == "position")
		{
			sawPosition = true;
		}
		if(each.name == "static" && sawPosition)
		{
			sawStaticAfterPosition = true;
		}
	}
	CHECK(sawStaticAfterPosition);
}
//---------------------------------------------------------
TEST_CASE("The static-flag hierarchy rule is pure and honest",
	"[static][rules]")
{
	using namespace Orkige;

	// flagging static: allowed under a static parent or with no parent...
	CHECK(TransformComponent::staticFlagChangeError(true, true, false) == nullptr);
	CHECK(TransformComponent::staticFlagChangeError(true, true, true) == nullptr);
	// ...refused under a dynamic parent (the frozen world transform would
	// embed a pose that keeps changing)
	CHECK(TransformComponent::staticFlagChangeError(true, false, false) != nullptr);

	// clearing the flag: allowed while no child depends on it...
	CHECK(TransformComponent::staticFlagChangeError(false, true, false) == nullptr);
	CHECK(TransformComponent::staticFlagChangeError(false, false, false) == nullptr);
	// ...refused while static children embed this object's frozen pose
	CHECK(TransformComponent::staticFlagChangeError(false, true, true) != nullptr);
}
//---------------------------------------------------------
TEST_CASE("The static flag round-trips on a DETACHED transform",
	"[reflection][static]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		TransformComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);
	PropertyDesc const * staticFlag = schema->find("static");
	REQUIRE(staticFlag != nullptr);

	// a detached component (no owner, no node) records the flag; the
	// backend apply happens when the node exists (onAdd)
	TransformComponent transform;
	Object * instance = &transform;
	CHECK(staticFlag->get(instance).asBool() == false);
	staticFlag->set(instance, PropertyValue::makeBool(true));
	CHECK(transform.getStaticFlag());
	CHECK(staticFlag->get(instance).asBool() == true);
	staticFlag->set(instance, PropertyValue::makeBool(false));
	CHECK_FALSE(transform.getStaticFlag());
}
