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
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/WaterComponent.h>
#include <engine_gocomponent/VectorAnimationComponent.h>
#include <engine_gocomponent/AnimationComponent.h>
#include <engine_gocomponent/BoneAttachComponent.h>
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
//---------------------------------------------------------
TEST_CASE("ModelComponent declares mesh AND material references in its schema",
	"[reflection][model][material]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		ModelComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// the mesh reference (asset-kind "mesh") and the `.omat` material
	// reference (asset-kind "material") - both writable AssetRefs, so the
	// Inspector picker, scene serialization and the MCP verbs reach them
	// generically
	PropertyDesc const * mesh = schema->find("mesh");
	PropertyDesc const * material = schema->find("material");
	REQUIRE(mesh != nullptr);
	REQUIRE(material != nullptr);
	CHECK(mesh->kind == PropertyKind::AssetRef);
	CHECK(mesh->referenceHint == "mesh");
	CHECK(material->kind == PropertyKind::AssetRef);
	CHECK(material->referenceHint == "material");
	CHECK_FALSE(material->isReadOnly());

	// the per-instance shadow participation flags (default true: content
	// casts AND receives until a designer opts it out)
	PropertyDesc const * casts = schema->find("castShadows");
	PropertyDesc const * receives = schema->find("receiveShadows");
	REQUIRE(casts != nullptr);
	REQUIRE(receives != nullptr);
	CHECK(casts->kind == PropertyKind::Bool);
	CHECK(receives->kind == PropertyKind::Bool);

	// the runtime accents are TRANSIENT Colors: every live surface
	// (inspector, Lua, MCP, debug protocol) reaches them, a scene save
	// never records them - the .omat stays the authored truth
	PropertyDesc const * tint = schema->find("tint");
	PropertyDesc const * boost = schema->find("emissiveBoost");
	REQUIRE(tint != nullptr);
	REQUIRE(boost != nullptr);
	CHECK(tint->kind == PropertyKind::Color);
	CHECK(boost->kind == PropertyKind::Color);
	CHECK(tint->hasFlag(PROP_TRANSIENT));
	CHECK(boost->hasFlag(PROP_TRANSIENT));
}
//---------------------------------------------------------
TEST_CASE("ModelComponent material reference round-trips on a DETACHED component",
	"[reflection][model][material]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		ModelComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);
	PropertyDesc const * material = schema->find("material");
	REQUIRE(material != nullptr);

	// a detached model (no scene node, no mesh, no render system) RECORDS the
	// reference through the type-erased set - the same tolerant setter the
	// scene loader drives; the live apply happens when a mesh loads
	ModelComponent model;
	Object * instance = &model;
	CHECK(material->get(instance).referenceId().empty());

	material->set(instance,
		PropertyValue::makeAssetRef("material", "rock.omat"));
	CHECK(model.getMaterialFileName() == "rock.omat");
	CHECK(material->get(instance).referenceId() == "rock.omat");

	// the shadow flags flip on a detached component (recorded, applied when
	// a mesh loads) and default to true
	PropertyDesc const * casts = schema->find("castShadows");
	PropertyDesc const * receives = schema->find("receiveShadows");
	REQUIRE(casts != nullptr);
	REQUIRE(receives != nullptr);
	CHECK(casts->get(instance).asBool());
	CHECK(receives->get(instance).asBool());
	casts->set(instance, PropertyValue::makeBool(false));
	receives->set(instance, PropertyValue::makeBool(false));
	CHECK_FALSE(model.getCastShadows());
	CHECK_FALSE(model.getReceiveShadows());

	// the runtime accents record on a detached component too (applied when
	// a mesh loads) ...
	PropertyDesc const * tint = schema->find("tint");
	REQUIRE(tint != nullptr);
	PropColor red;
	red.r = 1.0f; red.g = 0.25f; red.b = 0.2f; red.a = 1.0f;
	tint->set(instance, PropertyValue::makeColor(red));
	CHECK(model.getTint().g == Approx(0.25f));

	// the serialization path (capture -> apply) carries the reference AND
	// the shadow flags - but NOT the transient accents (runtime-only; the
	// restored component keeps the neutral white tint)
	GameObject::ComponentPropertyMap captured =
		SceneSerializer::captureComponentProperties(model);
	ModelComponent restored;
	SceneSerializer::applyComponentProperties(captured, restored);
	CHECK(restored.getMaterialFileName() == "rock.omat");
	CHECK_FALSE(restored.getCastShadows());
	CHECK_FALSE(restored.getReceiveShadows());
	CHECK(restored.getTint().g == Approx(1.0f));

	// clearing detaches cleanly (no mesh to reload on a detached component)
	material->set(instance, PropertyValue::makeAssetRef("material", ""));
	CHECK(model.getMaterialFileName().empty());
}
//---------------------------------------------------------
TEST_CASE("WaterComponent declares its water surface schema",
	"[reflection][water]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		WaterComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// the plane size, the surface colours/knobs and the tiling normal-map
	// AssetRef all reach the Inspector / scene serialization / MCP / Lua
	// generically off ONE schema
	REQUIRE(schema->find("sizeX") != nullptr);
	CHECK(schema->find("sizeX")->kind == PropertyKind::Float);
	REQUIRE(schema->find("sizeZ") != nullptr);
	CHECK(schema->find("sizeZ")->kind == PropertyKind::Float);
	REQUIRE(schema->find("deepColour") != nullptr);
	CHECK(schema->find("deepColour")->kind == PropertyKind::Color);
	REQUIRE(schema->find("shallowColour") != nullptr);
	CHECK(schema->find("shallowColour")->kind == PropertyKind::Color);
	REQUIRE(schema->find("opacity") != nullptr);
	CHECK(schema->find("opacity")->kind == PropertyKind::Float);
	REQUIRE(schema->find("waveScale") != nullptr);
	CHECK(schema->find("waveScale")->kind == PropertyKind::Float);
	REQUIRE(schema->find("waveSpeed") != nullptr);
	CHECK(schema->find("waveSpeed")->kind == PropertyKind::Float);
	REQUIRE(schema->find("fresnelPower") != nullptr);
	CHECK(schema->find("fresnelPower")->kind == PropertyKind::Float);

	PropertyDesc const * normal = schema->find("normalTexture");
	REQUIRE(normal != nullptr);
	CHECK(normal->kind == PropertyKind::AssetRef);
	CHECK(normal->referenceHint == "texture");
	CHECK_FALSE(normal->isReadOnly());

	// receive-only shadow participation (water never casts by design)
	PropertyDesc const * receives = schema->find("receiveShadows");
	REQUIRE(receives != nullptr);
	CHECK(receives->kind == PropertyKind::Bool);
	CHECK(schema->find("castShadows") == nullptr);
}
//---------------------------------------------------------
TEST_CASE("WaterComponent properties round-trip on a DETACHED component",
	"[reflection][water]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		WaterComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// a DETACHED surface (no scene node, no render system) RECORDS its state
	// through the type-erased get/set - the tolerant setters no-op the material
	// apply until the component gets a node
	WaterComponent water;
	Object * instance = &water;

	// the engine-default tiling water normal map is the AssetRef default
	CHECK(schema->find("normalTexture")->get(instance).referenceId() ==
		WaterComponent::DEFAULT_NORMAL_TEXTURE);

	schema->find("sizeX")->set(instance, PropertyValue::makeFloat(30.0));
	schema->find("sizeZ")->set(instance, PropertyValue::makeFloat(18.0));
	schema->find("opacity")->set(instance, PropertyValue::makeFloat(0.5));
	schema->find("waveScale")->set(instance, PropertyValue::makeFloat(8.0));
	schema->find("waveSpeed")->set(instance, PropertyValue::makeFloat(0.1));
	schema->find("fresnelPower")->set(instance, PropertyValue::makeFloat(2.0));
	PropColor deep; deep.r = 0.0f; deep.g = 0.05f; deep.b = 0.15f; deep.a = 1.0f;
	schema->find("deepColour")->set(instance, PropertyValue::makeColor(deep));
	schema->find("normalTexture")->set(instance,
		PropertyValue::makeAssetRef("texture", "custom_water.png"));
	CHECK(schema->find("receiveShadows")->get(instance).asBool());
	schema->find("receiveShadows")->set(instance, PropertyValue::makeBool(false));
	CHECK_FALSE(water.getReceiveShadows());

	CHECK(water.getSizeX() == Approx(30.0f));
	CHECK(water.getSizeZ() == Approx(18.0f));
	CHECK(water.getOpacity() == Approx(0.5f));
	CHECK(water.getWaveScale() == Approx(8.0f));
	CHECK(water.getWaveSpeed() == Approx(0.1f));
	CHECK(water.getFresnelPower() == Approx(2.0f));
	CHECK(water.getDeepColour().b == Approx(0.15f));
	CHECK(water.getNormalTexture() == "custom_water.png");

	// the serialization path (capture -> apply) carries every field
	GameObject::ComponentPropertyMap captured =
		SceneSerializer::captureComponentProperties(water);
	WaterComponent restored;
	SceneSerializer::applyComponentProperties(captured, restored);
	CHECK(restored.getSizeX() == Approx(30.0f));
	CHECK(restored.getSizeZ() == Approx(18.0f));
	CHECK(restored.getOpacity() == Approx(0.5f));
	CHECK(restored.getWaveScale() == Approx(8.0f));
	CHECK(restored.getFresnelPower() == Approx(2.0f));
	CHECK(restored.getNormalTexture() == "custom_water.png");
	CHECK_FALSE(restored.getReceiveShadows());
}
//---------------------------------------------------------
TEST_CASE("VectorAnimationComponent declares its playback + rig schema",
	"[reflection][vectoranim]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		VectorAnimationComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// the animation reference (asset-kind "vectoranim" - a `.oanim`), the
	// playback tunables and the shared 2D rig state all reach the Inspector /
	// scene serialization / MCP generically off ONE schema
	PropertyDesc const * animation = schema->find("animation");
	REQUIRE(animation != nullptr);
	CHECK(animation->kind == PropertyKind::AssetRef);
	CHECK(animation->referenceHint == "vectoranim");
	CHECK_FALSE(animation->isReadOnly());

	REQUIRE(schema->find("clip") != nullptr);
	CHECK(schema->find("clip")->kind == PropertyKind::String);
	REQUIRE(schema->find("speed") != nullptr);
	CHECK(schema->find("speed")->kind == PropertyKind::Float);
	REQUIRE(schema->find("playing") != nullptr);
	CHECK(schema->find("playing")->kind == PropertyKind::Bool);
	REQUIRE(schema->find("transitionTime") != nullptr);
	CHECK(schema->find("transitionTime")->kind == PropertyKind::Float);
	REQUIRE(schema->find("tint") != nullptr);
	CHECK(schema->find("tint")->kind == PropertyKind::Color);
	REQUIRE(schema->find("scale") != nullptr);
	CHECK(schema->find("scale")->kind == PropertyKind::Float);
	REQUIRE(schema->find("zOrder") != nullptr);
	CHECK(schema->find("zOrder")->kind == PropertyKind::Int);
	REQUIRE(schema->find("visible") != nullptr);
	CHECK(schema->find("visible")->kind == PropertyKind::Bool);
}
//---------------------------------------------------------
TEST_CASE("VectorAnimationComponent properties round-trip on a DETACHED component",
	"[reflection][vectoranim]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		VectorAnimationComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// a DETACHED rig (no scene node, no render system) RECORDS the animation
	// reference and its playback settings through the type-erased get/set - the
	// same tolerant setters the scene loader drives; the live build happens when
	// the component gets a node
	VectorAnimationComponent anim;
	Object * instance = &anim;

	CHECK(schema->find("animation")->get(instance).referenceId().empty());
	CHECK(anim.isPlaying());	// a placed rig plays on Play by default

	schema->find("animation")->set(instance,
		PropertyValue::makeAssetRef("vectoranim", "hero.oanim"));
	schema->find("clip")->set(instance, PropertyValue::makeString("walk"));
	schema->find("speed")->set(instance, PropertyValue::makeFloat(1.5));
	schema->find("playing")->set(instance, PropertyValue::makeBool(false));
	schema->find("transitionTime")->set(instance, PropertyValue::makeFloat(0.25));
	schema->find("zOrder")->set(instance, PropertyValue::makeInt(3));

	CHECK(anim.getAnimationName() == "hero.oanim");
	CHECK(anim.getClipProperty() == "walk");
	CHECK(anim.getSpeed() == Approx(1.5f));
	CHECK_FALSE(anim.isPlaying());
	CHECK(anim.getTransitionTime() == Approx(0.25f));
	CHECK(anim.getZOrder() == 3);

	// the serialization path (capture -> apply) carries every field, the
	// animation reference last so the scalar state is in place first
	GameObject::ComponentPropertyMap captured =
		SceneSerializer::captureComponentProperties(anim);
	VectorAnimationComponent restored;
	SceneSerializer::applyComponentProperties(captured, restored);
	CHECK(restored.getAnimationName() == "hero.oanim");
	CHECK(restored.getClipProperty() == "walk");
	CHECK(restored.getSpeed() == Approx(1.5f));
	CHECK_FALSE(restored.isPlaying());
	CHECK(restored.getZOrder() == 3);

	// clearing detaches cleanly (no mesh to tear down on a detached component)
	schema->find("animation")->set(instance,
		PropertyValue::makeAssetRef("vectoranim", ""));
	CHECK(anim.getAnimationName().empty());
}
//---------------------------------------------------------
TEST_CASE("AnimationComponent declares its skeletal playback state schema",
	"[reflection][animation]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		AnimationComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// the mid-animation playback state - the primary clip, its phase in seconds,
	// the loop flag, the playback speed and the pause state - all reach the
	// Inspector / scene serialization / MCP generically off ONE schema
	REQUIRE(schema->find("clip") != nullptr);
	CHECK(schema->find("clip")->kind == PropertyKind::String);
	REQUIRE(schema->find("clipTime") != nullptr);
	CHECK(schema->find("clipTime")->kind == PropertyKind::Float);
	REQUIRE(schema->find("clipLoop") != nullptr);
	CHECK(schema->find("clipLoop")->kind == PropertyKind::Bool);
	REQUIRE(schema->find("speed") != nullptr);
	CHECK(schema->find("speed")->kind == PropertyKind::Float);
	REQUIRE(schema->find("paused") != nullptr);
	CHECK(schema->find("paused")->kind == PropertyKind::Bool);
	// every playback field is writable (a load must be able to restore it) and
	// serialized (none is transient runtime-only state)
	CHECK_FALSE(schema->find("clip")->isReadOnly());
	CHECK_FALSE(schema->find("clipTime")->hasFlag(PROP_TRANSIENT));
	CHECK_FALSE(schema->find("speed")->hasFlag(PROP_TRANSIENT));
}
//---------------------------------------------------------
TEST_CASE("AnimationComponent playback state round-trips on a DETACHED component",
	"[reflection][animation]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		AnimationComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// a DETACHED component (no scene node, no mesh) RECORDS the playback state
	// through the type-erased get/set - the same setters the scene loader drives;
	// the live resume happens on the first runtime tick once a mesh exists
	AnimationComponent anim;
	Object * instance = &anim;

	// defaults: nothing playing, unit speed, looping, not paused
	CHECK(schema->find("clip")->get(instance).asString().empty());
	CHECK(schema->find("speed")->get(instance).asFloat() == Approx(1.0f));
	CHECK(schema->find("clipLoop")->get(instance).asBool());
	CHECK_FALSE(schema->find("paused")->get(instance).asBool());

	// a scene saved mid-animation: the walk clip at 0.37 s, playing looped at
	// 1.5x speed, paused
	schema->find("clip")->set(instance, PropertyValue::makeString("walk"));
	schema->find("clipTime")->set(instance, PropertyValue::makeFloat(0.37));
	schema->find("clipLoop")->set(instance, PropertyValue::makeBool(true));
	schema->find("speed")->set(instance, PropertyValue::makeFloat(1.5));
	schema->find("paused")->set(instance, PropertyValue::makeBool(true));

	CHECK(anim.getPlaybackClip() == "walk");
	CHECK(anim.getPlaybackTime() == Approx(0.37f));
	CHECK(anim.getPlaybackLoop());
	CHECK(anim.getSpeed() == Approx(1.5f));
	CHECK(anim.getPaused());

	// the exact serialization path (capture -> apply, the same reflected schema
	// save/loadComponentProperties use) carries every field to a fresh component
	// so the loaded scene resumes the same clip at the same normalized phase
	GameObject::ComponentPropertyMap captured =
		SceneSerializer::captureComponentProperties(anim);
	AnimationComponent restored;
	SceneSerializer::applyComponentProperties(captured, restored);
	CHECK(restored.getPlaybackClip() == "walk");
	CHECK(restored.getPlaybackTime() == Approx(0.37f));
	CHECK(restored.getPlaybackLoop());
	CHECK(restored.getSpeed() == Approx(1.5f));
	CHECK(restored.getPaused());
}
//---------------------------------------------------------
TEST_CASE("BoneAttachComponent declares its bone-follow schema",
	"[reflection][boneattach]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		BoneAttachComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// the target character, the bone name, the bone-local offset and the follow
	// flags all reach the Inspector / scene serialization / MCP off ONE schema
	REQUIRE(schema->find("target") != nullptr);
	CHECK(schema->find("target")->kind == PropertyKind::String);
	REQUIRE(schema->find("bone") != nullptr);
	CHECK(schema->find("bone")->kind == PropertyKind::String);
	REQUIRE(schema->find("offset") != nullptr);
	CHECK(schema->find("offset")->kind == PropertyKind::Vec3);
	REQUIRE(schema->find("followRotation") != nullptr);
	CHECK(schema->find("followRotation")->kind == PropertyKind::Bool);
	REQUIRE(schema->find("followScale") != nullptr);
	CHECK(schema->find("followScale")->kind == PropertyKind::Bool);
	CHECK_FALSE(schema->find("bone")->isReadOnly());
}
//---------------------------------------------------------
TEST_CASE("BoneAttachComponent properties round-trip on a DETACHED component",
	"[reflection][boneattach]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		BoneAttachComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	// a DETACHED follower (no scene node, no target) RECORDS its bone-follow
	// intent through the type-erased get/set - the same setters the scene loader
	// drives; the live follow happens when a ticking runtime resolves the target
	BoneAttachComponent attach;
	Object * instance = &attach;

	// defaults: no target/bone, zero offset, follows rotation not scale
	CHECK(schema->find("target")->get(instance).asString().empty());
	CHECK(schema->find("followRotation")->get(instance).asBool());
	CHECK_FALSE(schema->find("followScale")->get(instance).asBool());

	schema->find("target")->set(instance, PropertyValue::makeString("Hero"));
	schema->find("bone")->set(instance, PropertyValue::makeString("hand.R"));
	PropVec3 offset; offset.x = 0.0f; offset.y = -0.5f; offset.z = 0.1f;
	schema->find("offset")->set(instance, PropertyValue::makeVec3(offset));
	schema->find("followScale")->set(instance, PropertyValue::makeBool(true));

	CHECK(attach.getTarget() == "Hero");
	CHECK(attach.getBone() == "hand.R");
	CHECK(attach.getOffset().y == Approx(-0.5f));
	CHECK(attach.getOffset().z == Approx(0.1f));
	CHECK(attach.getFollowScale());

	// the serialization path (capture -> apply) carries every field
	GameObject::ComponentPropertyMap captured =
		SceneSerializer::captureComponentProperties(attach);
	BoneAttachComponent restored;
	SceneSerializer::applyComponentProperties(captured, restored);
	CHECK(restored.getTarget() == "Hero");
	CHECK(restored.getBone() == "hand.R");
	CHECK(restored.getOffset().y == Approx(-0.5f));
	CHECK(restored.getOffset().z == Approx(0.1f));
	CHECK(restored.getFollowScale());
}
