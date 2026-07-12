/**************************************************************
	created:	2026/07/09 at 16:00
	filename: 	PropertyReflectionTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The engine half of the reflection proof: the Orkige
	Vec3/Quat/Color <-> POD adapter (ComponentPropertyReflect.h, the Ogre
	containment-safe seam) and the two converted real components. Runs on
	DETACHED components (no Ogre::Root, no scene nodes), so it is headless like
	the rest of tests/engine and passes in BOTH scripting configs (the
	classic-flavored macos-debug-classic LUA build and macos-debug-noscript).
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_gocomponent/LightComponent.h>
#include <engine_gocomponent/ComponentPropertyReflect.h>
#include <engine_render/RenderMath.h>

#include <core_game/SceneSerializer.h>

#include <core_base/PropertyValue.h>
#include <core_base/PropertySchema.h>
#include <core_base/TypeManager.h>

using Catch::Approx;

//---------------------------------------------------------
TEST_CASE("The engine Vec3/Quat/Color reflection adapter round-trips",
	"[reflection][adapter]")
{
	using namespace Orkige;

	const Vec3 vec(1.0f, 2.0f, 3.0f);
	PropertyValue vecValue = PropertyReflect::pack(vec);
	CHECK(vecValue.kind() == PropertyKind::Vec3);
	Vec3 vecBack(0, 0, 0);
	PropertyReflect::unpack(vecValue, vecBack);
	CHECK(vecBack.x == Approx(1.0f));
	CHECK(vecBack.y == Approx(2.0f));
	CHECK(vecBack.z == Approx(3.0f));

	const Quat quat(0.0f, 0.0f, 1.0f, 0.0f);	// (w,x,y,z)
	PropertyValue quatValue = PropertyReflect::pack(quat);
	CHECK(quatValue.kind() == PropertyKind::Quat);
	Quat quatBack = Quat::IDENTITY;
	PropertyReflect::unpack(quatValue, quatBack);
	CHECK(quatBack.w == Approx(0.0f));
	CHECK(quatBack.y == Approx(1.0f));

	const Color color(0.25f, 0.5f, 0.75f, 1.0f);
	PropertyValue colorValue = PropertyReflect::pack(color);
	CHECK(colorValue.kind() == PropertyKind::Color);
	Color colorBack(0, 0, 0, 0);
	PropertyReflect::unpack(colorValue, colorBack);
	CHECK(colorBack.b == Approx(0.75f));
	CHECK(colorBack.a == Approx(1.0f));
}
//---------------------------------------------------------
TEST_CASE("TransformComponent declares its local transform schema",
	"[reflection][transform]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		TransformComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	REQUIRE(schema->find("position") != nullptr);
	CHECK(schema->find("position")->kind == PropertyKind::Vec3);
	CHECK(schema->find("orientation")->kind == PropertyKind::Quat);
	CHECK(schema->find("scale")->kind == PropertyKind::Vec3);
	// getters/setters are present (schema is not read-only)
	CHECK_FALSE(schema->find("position")->isReadOnly());
}
//---------------------------------------------------------
TEST_CASE("CameraComponent enum + float properties round-trip through the registry",
	"[reflection][camera]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		CameraComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	PropertyDesc const * mode = schema->find("projectionMode");
	PropertyDesc const * ortho = schema->find("orthoSize");
	REQUIRE(mode != nullptr);
	REQUIRE(ortho != nullptr);
	CHECK(mode->kind == PropertyKind::Enum);
	CHECK(mode->enumTypeName == "ProjectionMode");
	CHECK(ortho->kind == PropertyKind::Float);
	// the reserved display metadata rode into the schema via OPROPERTY_META
	CHECK(ortho->meta.hasRange);
	CHECK(ortho->meta.maxValue == Approx(100.0f));

	// the enum value<->label table was registered in this config
	EnumInfo const * projEnum = TypeManager::getSingleton().findEnum("ProjectionMode");
	REQUIRE(projEnum != nullptr);
	CHECK(projEnum->size() == 2);
	long long ortographic = -1;
	REQUIRE(projEnum->valueOf("PM_ORTHOGRAPHIC", ortographic));
	CHECK(ortographic == CameraComponent::PM_ORTHOGRAPHIC);

	// a DETACHED camera (no window camera) round-trips its state through the
	// type-erased get/set - the property setter drives the same setProjectionMode
	// / setOrthoSize the component already exposes
	CameraComponent camera;
	Object * instance = &camera;

	CHECK(mode->get(instance).asInt() == CameraComponent::PM_PERSPECTIVE);

	mode->set(instance,
		PropertyValue::makeEnum("ProjectionMode", CameraComponent::PM_ORTHOGRAPHIC));
	CHECK(camera.getProjectionMode() == CameraComponent::PM_ORTHOGRAPHIC);
	CHECK(mode->get(instance).asInt() == CameraComponent::PM_ORTHOGRAPHIC);
	CHECK(mode->get(instance).enumTypeName() == "ProjectionMode");

	ortho->set(instance, PropertyValue::makeFloat(12.5));
	CHECK(camera.getOrthoSize() == Approx(12.5f));
	CHECK(ortho->get(instance).asFloat() == Approx(12.5f));
}
//---------------------------------------------------------
TEST_CASE("LightComponent declares its whole light schema through the registry",
	"[reflection][light]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		LightComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// the reflected surface: kind (enum), colour, the intensity/range scalars,
	// the spot cone angles and the forward-compatible shadow flag
	PropertyDesc const * type = schema->find("type");
	REQUIRE(type != nullptr);
	CHECK(type->kind == PropertyKind::Enum);
	CHECK(type->enumTypeName == "LightType");
	REQUIRE(schema->find("colour") != nullptr);
	CHECK(schema->find("colour")->kind == PropertyKind::Color);
	REQUIRE(schema->find("intensity") != nullptr);
	CHECK(schema->find("intensity")->kind == PropertyKind::Float);
	REQUIRE(schema->find("range") != nullptr);
	CHECK(schema->find("range")->kind == PropertyKind::Float);
	REQUIRE(schema->find("innerAngle") != nullptr);
	REQUIRE(schema->find("outerAngle") != nullptr);
	REQUIRE(schema->find("castsShadows") != nullptr);
	CHECK(schema->find("castsShadows")->kind == PropertyKind::Bool);
	// every field is writable (not a read-only/transient property)
	CHECK_FALSE(schema->find("intensity")->isReadOnly());

	// the LightType enum value<->label table registered in this config
	EnumInfo const * lightEnum = TypeManager::getSingleton().findEnum("LightType");
	REQUIRE(lightEnum != nullptr);
	CHECK(lightEnum->size() == 3);
	long long spot = -1;
	REQUIRE(lightEnum->valueOf("LT_SPOT", spot));
	CHECK(spot == LightComponent::LT_SPOT);
}
//---------------------------------------------------------
TEST_CASE("LightComponent properties round-trip on a DETACHED component",
	"[reflection][light]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		LightComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// a detached light (no scene node, no facade light) round-trips every field
	// through the type-erased get/set - the setters drive the same public API
	LightComponent light;
	Object * instance = &light;

	// defaults: a white point light
	CHECK(schema->find("type")->get(instance).asInt() == LightComponent::LT_POINT);
	CHECK(schema->find("colour")->get(instance).asColor().r == Approx(1.0f));

	schema->find("type")->set(instance,
		PropertyValue::makeEnum("LightType", LightComponent::LT_SPOT));
	CHECK(light.getType() == LightComponent::LT_SPOT);
	CHECK(schema->find("type")->get(instance).enumTypeName() == "LightType");

	PropColor colour;
	colour.r = 0.2f; colour.g = 0.4f; colour.b = 0.6f; colour.a = 1.0f;
	schema->find("colour")->set(instance, PropertyValue::makeColor(colour));
	CHECK(light.getColour().g == Approx(0.4f));

	schema->find("intensity")->set(instance, PropertyValue::makeFloat(2.5));
	CHECK(light.getIntensity() == Approx(2.5f));
	schema->find("range")->set(instance, PropertyValue::makeFloat(42.0));
	CHECK(light.getRange() == Approx(42.0f));
	schema->find("innerAngle")->set(instance, PropertyValue::makeFloat(25.0));
	schema->find("outerAngle")->set(instance, PropertyValue::makeFloat(55.0));
	CHECK(light.getInnerAngle() == Approx(25.0f));
	CHECK(light.getOuterAngle() == Approx(55.0f));
	schema->find("castsShadows")->set(instance, PropertyValue::makeBool(true));
	CHECK(light.getCastsShadows());

	// the exact serialization path (SceneSerializer capture -> apply, the same
	// reflected schema saveComponentProperties/loadComponentProperties use)
	// carries every field onto a fresh component
	GameObject::ComponentPropertyMap captured =
		SceneSerializer::captureComponentProperties(light);
	LightComponent restored;
	SceneSerializer::applyComponentProperties(captured, restored);
	CHECK(restored.getType() == LightComponent::LT_SPOT);
	CHECK(restored.getColour().b == Approx(0.6f));
	CHECK(restored.getIntensity() == Approx(2.5f));
	CHECK(restored.getRange() == Approx(42.0f));
	CHECK(restored.getInnerAngle() == Approx(25.0f));
	CHECK(restored.getOuterAngle() == Approx(55.0f));
	CHECK(restored.getCastsShadows());
}
