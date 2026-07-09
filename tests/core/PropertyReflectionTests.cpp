/**************************************************************
	created:	2026/07/09 at 16:00
	filename: 	PropertyReflectionTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The neutral property-reflection substrate. These tests
	run in EVERY scripting config (ORKIGE_LUA and ORKIGE_SCRIPTING=OFF) and on
	BOTH render flavors (tests/core builds in the default next tree too),
	because that is the whole point of the substrate: a component's declared
	property schema must be queryable through the sol2-independent registry.

	Coverage:
	  * PropertyValue: every PropertyKind round-trips through its canonical
	    string form (the CVar dialect) and typed reads.
	  * PropertySchema / EnumInfo hand-built + queried.
	  * TestReflectComponent (TestComponents.h) declares one property per core
	    PropertyKind via the OPROPERTY* macros; the test enumerates that schema
	    from TypeManager, reads each property as a PropertyValue, sets several
	    and reads them back - proving the dual-emitting macro populated the
	    NEUTRAL registry in this config.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <core_base/PropertyValue.h>
#include <core_base/PropertySchema.h>
#include <core_base/PropertyReflect.h>
#include <core_base/TypeManager.h>

using Catch::Approx;

//---------------------------------------------------------
TEST_CASE("PropertyValue round-trips every kind through its canonical string",
	"[reflection][property]")
{
	using namespace Orkige;

	SECTION("scalars")
	{
		PropertyValue anInt = PropertyValue::makeInt(-42);
		CHECK(anInt.kind() == PropertyKind::Int);
		CHECK(anInt.asInt() == -42);
		CHECK(anInt.toString() == "-42");

		PropertyValue aFloat = PropertyValue::makeFloat(1.5);
		CHECK(aFloat.kind() == PropertyKind::Float);
		CHECK(aFloat.asFloat() == Approx(1.5));

		PropertyValue aBool = PropertyValue::makeBool(true);
		CHECK(aBool.kind() == PropertyKind::Bool);
		CHECK(aBool.asBool());
		CHECK(aBool.toString() == "1");

		PropertyValue aString = PropertyValue::makeString("hello world");
		CHECK(aString.kind() == PropertyKind::String);
		CHECK(aString.asString() == "hello world");
	}

	SECTION("math PODs")
	{
		PropVec3 vec; vec.x = 1.0f; vec.y = 2.0f; vec.z = 3.0f;
		PropertyValue aVec = PropertyValue::makeVec3(vec);
		CHECK(aVec.kind() == PropertyKind::Vec3);
		CHECK(aVec.toString() == "1 2 3");
		CHECK(aVec.asVec3().y == Approx(2.0f));

		PropQuat quat; quat.w = 0.0f; quat.x = 0.0f; quat.y = 1.0f; quat.z = 0.0f;
		PropertyValue aQuat = PropertyValue::makeQuat(quat);
		CHECK(aQuat.kind() == PropertyKind::Quat);
		CHECK(aQuat.asQuat().y == Approx(1.0f));

		PropColor col; col.r = 0.25f; col.g = 0.5f; col.b = 0.75f; col.a = 1.0f;
		PropertyValue aColor = PropertyValue::makeColor(col);
		CHECK(aColor.kind() == PropertyKind::Color);
		CHECK(aColor.asColor().b == Approx(0.75f));
	}

	SECTION("enum keeps its type name")
	{
		PropertyValue anEnum = PropertyValue::makeEnum("TestTeam", 2);
		CHECK(anEnum.kind() == PropertyKind::Enum);
		CHECK(anEnum.asInt() == 2);
		CHECK(anEnum.enumTypeName() == "TestTeam");
	}

	SECTION("references keep id + hint")
	{
		PropertyValue asset = PropertyValue::makeAssetRef("texture", "tex:1234");
		CHECK(asset.kind() == PropertyKind::AssetRef);
		CHECK(asset.referenceId() == "tex:1234");
		CHECK(asset.referenceHint() == "texture");

		PropertyValue object = PropertyValue::makeObjectRef("GameObject", "obj:9");
		CHECK(object.kind() == PropertyKind::ObjectRef);
		CHECK(object.referenceId() == "obj:9");
		CHECK(object.referenceHint() == "GameObject");
	}

	SECTION("fromString parses per kind and rejects garbage")
	{
		PropertyValue vec = PropertyValue::makeVec3(PropVec3());
		REQUIRE(vec.fromString("4 5 6"));
		CHECK(vec.asVec3().z == Approx(6.0f));

		PropertyValue number = PropertyValue::makeFloat(0.0);
		String error;
		CHECK_FALSE(number.fromString("not-a-number", &error));
		CHECK_FALSE(error.empty());
		CHECK(number.asFloat() == Approx(0.0));	// unchanged on rejection
	}
}
//---------------------------------------------------------
TEST_CASE("PropertySchema and EnumInfo build and query", "[reflection][schema]")
{
	using namespace Orkige;

	PropertySchema schema;
	schema.add(PropertyDesc("count", PropertyKind::Int, PROP_NONE,
		PropertyGetter(), PropertySetter()));
	// idempotent re-add of the same name replaces, does not duplicate
	schema.add(PropertyDesc("count", PropertyKind::Int, PROP_READONLY,
		PropertyGetter(), PropertySetter()));
	schema.add(PropertyDesc("speed", PropertyKind::Float, PROP_NONE,
		PropertyGetter(), PropertySetter()));

	CHECK(schema.size() == 2);
	REQUIRE(schema.find("count") != nullptr);
	CHECK(schema.find("count")->hasFlag(PROP_READONLY));
	CHECK(schema.find("count")->isReadOnly());	// no setter either
	CHECK(schema.find("missing") == nullptr);

	EnumInfo team("TestTeam");
	team.addValue("TEAM_RED", 0);
	team.addValue("TEAM_BLUE", 1);
	long long value = -1;
	REQUIRE(team.valueOf("TEAM_BLUE", value));
	CHECK(value == 1);
	String label;
	REQUIRE(team.labelOf(0, label));
	CHECK(label == "TEAM_RED");
	CHECK_FALSE(team.valueOf("TEAM_PURPLE", value));
}
//---------------------------------------------------------
TEST_CASE("A component's OPROPERTY schema enumerates through the neutral registry",
	"[reflection][registry]")
{
	using namespace Orkige;
	CoreTestEnvironment::get();
	registerOrkigeTestComponents();

	TypeManager & types = TypeManager::getSingleton();
	PropertySchema const * schema = types.getPropertySchema(
		TestReflectComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// one property of each declared kind is present, in declaration order
	REQUIRE(schema->size() == 6);
	REQUIRE(schema->find("count") != nullptr);
	CHECK(schema->find("count")->kind == PropertyKind::Int);
	CHECK(schema->find("speed")->kind == PropertyKind::Float);
	CHECK(schema->find("enabled")->kind == PropertyKind::Bool);
	CHECK(schema->find("label")->kind == PropertyKind::String);
	CHECK(schema->find("team")->kind == PropertyKind::Enum);
	CHECK(schema->find("team")->enumTypeName == "TestTeam");
	CHECK(schema->find("icon")->kind == PropertyKind::AssetRef);
	CHECK(schema->find("icon")->referenceHint == "texture");

	// the enum value<->label table was registered too (combo-box source)
	EnumInfo const * teamEnum = types.findEnum("TestTeam");
	REQUIRE(teamEnum != nullptr);
	CHECK(teamEnum->size() == 3);
	long long blue = -1;
	REQUIRE(teamEnum->valueOf("TEAM_BLUE", blue));
	CHECK(blue == 1);

	// read/write EACH property through the type-erased get/set on a live instance
	TestReflectComponent probe;
	void * instance = &probe;

	SECTION("read reflects the instance state")
	{
		probe.setCount(7);
		probe.setSpeed(2.5f);
		probe.setEnabled(true);
		probe.setLabel("player");
		probe.setTeam(TestReflectComponent::TEAM_GREEN);
		probe.setIcon("tex:hero");

		CHECK(schema->find("count")->get(instance).asInt() == 7);
		CHECK(schema->find("speed")->get(instance).asFloat() == Approx(2.5f));
		CHECK(schema->find("enabled")->get(instance).asBool());
		CHECK(schema->find("label")->get(instance).asString() == "player");

		PropertyValue teamValue = schema->find("team")->get(instance);
		CHECK(teamValue.kind() == PropertyKind::Enum);
		CHECK(teamValue.asInt() == TestReflectComponent::TEAM_GREEN);
		CHECK(teamValue.enumTypeName() == "TestTeam");

		PropertyValue iconValue = schema->find("icon")->get(instance);
		CHECK(iconValue.kind() == PropertyKind::AssetRef);
		CHECK(iconValue.referenceId() == "tex:hero");
	}

	SECTION("set writes back through the registry (round-trip)")
	{
		schema->find("count")->set(instance, PropertyValue::makeInt(99));
		CHECK(probe.getCount() == 99);
		CHECK(schema->find("count")->get(instance).asInt() == 99);

		schema->find("speed")->set(instance, PropertyValue::makeFloat(4.5));
		CHECK(probe.getSpeed() == Approx(4.5f));

		schema->find("enabled")->set(instance, PropertyValue::makeBool(true));
		CHECK(probe.getEnabled());

		schema->find("label")->set(instance, PropertyValue::makeString("boss"));
		CHECK(probe.getLabel() == "boss");

		schema->find("team")->set(instance,
			PropertyValue::makeEnum("TestTeam", TestReflectComponent::TEAM_BLUE));
		CHECK(probe.getTeam() == TestReflectComponent::TEAM_BLUE);

		schema->find("icon")->set(instance,
			PropertyValue::makeAssetRef("texture", "tex:crown"));
		CHECK(probe.getIcon() == "tex:crown");
	}
}
