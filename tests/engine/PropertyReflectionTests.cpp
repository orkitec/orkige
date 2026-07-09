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
#include <engine_gocomponent/ComponentPropertyReflect.h>
#include <engine_render/RenderMath.h>

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
	void * instance = &camera;

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
