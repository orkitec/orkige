/**************************************************************
	created:	2026/07/26 at 12:00
	filename: 	ComponentEnableTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The generic component enable/disable feature's headless half: the base
	GameObjectComponent plumbing (default true, setEnabled toggles,
	effectivelyEnabled composes with the owner's active state), the
	schema-inheritance substrate (the ONE `enabled` property declared on the
	base surfaces on every kind, opted out for the kinds where disabling is
	meaningless) and the only-when-false serialization (an enabled component is
	byte-identical, a disabled one records the field). The live per-kind suspend
	behaviour (hide, leave the sim, stop sounds, drain particles, pause
	animation) is covered by the player_component_enable selfcheck; here the
	components run DETACHED (no Ogre::Root, no scene nodes) like the rest of
	tests/engine, so it passes in every scripting config.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/LightComponent.h>
#include <engine_gocomponent/WaterComponent.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_gocomponent/ParticleComponent.h>

#include <core_game/GameObject.h>
#include <core_game/GameObjectComponent.h>
#include <core_game/TileComponent.h>
#include <core_game/LevelComponent.h>
#include <core_game/SceneSerializer.h>

#include <core_base/PropertySchema.h>
#include <core_base/PropertyValue.h>
#include <core_base/TypeManager.h>

//---------------------------------------------------------
TEST_CASE("A component is enabled by default and toggles through the base",
	"[component][enable]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	ModelComponent model;	// detached
	CHECK(model.isEnabled());
	CHECK(model.effectivelyEnabled());	// no owner => owner counts as active

	model.setEnabled(false);
	CHECK_FALSE(model.isEnabled());
	CHECK_FALSE(model.effectivelyEnabled());

	model.setEnabled(true);
	CHECK(model.isEnabled());
	CHECK(model.effectivelyEnabled());
}
//---------------------------------------------------------
TEST_CASE("effectivelyEnabled composes the component flag with the owner active state",
	"[component][enable]")
{
	using namespace Orkige;
	EngineTestEnvironment & env = EngineTestEnvironment::get();

	optr<GameObject> object =
		env.gameObjectManager.createGameObject("Enableable").lock();
	REQUIRE(object);
	// wire the owner directly (a content component's onAdd needs a render
	// system, absent headlessly) - effectivelyEnabled reads the owner's active
	// state, which is what this exercises; the live attach path is a selfcheck
	ModelComponent model;
	model.setComponentOwner(object.get());

	// both axes on => effective
	CHECK(model.isEnabled());
	CHECK(object->isActiveInHierarchy());
	CHECK(model.effectivelyEnabled());

	// disabling the component alone suspends it, the object stays active
	model.setEnabled(false);
	CHECK_FALSE(model.effectivelyEnabled());
	CHECK(object->isActiveInHierarchy());
	model.setEnabled(true);
	CHECK(model.effectivelyEnabled());

	// deactivating the OWNER suspends the (still enabled) component - the two
	// axes compose, neither alone is sufficient
	object->setActive(false);
	CHECK(model.isEnabled());
	CHECK_FALSE(object->isActiveInHierarchy());
	CHECK_FALSE(model.effectivelyEnabled());

	// both off, then bring each back: still suspended until BOTH are on
	model.setEnabled(false);
	CHECK_FALSE(model.effectivelyEnabled());
	object->setActive(true);
	CHECK_FALSE(model.effectivelyEnabled());	// component still disabled
	model.setEnabled(true);
	CHECK(model.effectivelyEnabled());			// both on again

	model.setComponentOwner(nullptr);
	env.gameObjectManager.delGameObject(object->getObjectID());
}
//---------------------------------------------------------
TEST_CASE("The generic enabled property is inherited by every disable-capable kind",
	"[component][enable][reflection]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	// the ONE `enabled` property is declared on GameObjectComponent and surfaces
	// on every subclass through schema inheritance - no per-kind redeclaration.
	// A live, editable Bool the inspector header, MCP and serialization consume.
	auto checkHasEnabled = [](GameObjectComponent const & component)
	{
		const PropertySchema schema = getComponentSchema(component);
		PropertyDesc const * enabled =
			schema.find(GameObjectComponent::ENABLED_PROPERTY);
		REQUIRE(enabled != nullptr);
		CHECK(enabled->kind == PropertyKind::Bool);
		CHECK_FALSE(enabled->isReadOnly());
	};

	ModelComponent model;
	SpriteComponent sprite;
	LightComponent light;
	WaterComponent water;
	ParticleComponent particles;
	checkHasEnabled(model);
	checkHasEnabled(sprite);
	checkHasEnabled(light);
	checkHasEnabled(water);
	checkHasEnabled(particles);

	// the property is NOT declared on the kind's own type schema - it is inherited
	CHECK(TypeManager::getSingleton().getPropertySchema(
		ModelComponent::getClassTypeInfo().getId())->find("enabled") == nullptr);
}
//---------------------------------------------------------
TEST_CASE("Kinds where disabling is meaningless expose no enabled checkbox",
	"[component][enable][reflection]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	// TransformComponent, TileComponent, LevelComponent and CameraComponent opt
	// out (supportsDisable == false), so no lying checkbox reaches the inspector
	// / MCP / serialization
	TransformComponent transform;
	CameraComponent camera;
	TileComponent tile;
	LevelComponent level;
	// the trait is a virtual on the base (public) - dispatch through a base ref
	CHECK_FALSE(static_cast<GameObjectComponent&>(transform).supportsDisable());
	CHECK_FALSE(static_cast<GameObjectComponent&>(camera).supportsDisable());
	CHECK_FALSE(static_cast<GameObjectComponent&>(tile).supportsDisable());
	CHECK_FALSE(static_cast<GameObjectComponent&>(level).supportsDisable());

	CHECK(getComponentSchema(transform).find("enabled") == nullptr);
	CHECK(getComponentSchema(camera).find("enabled") == nullptr);
	CHECK(getComponentSchema(tile).find("enabled") == nullptr);
	CHECK(getComponentSchema(level).find("enabled") == nullptr);

	// and setEnabled is inert on an opt-out kind (no coherent suspend to run)
	transform.setEnabled(false);
	CHECK(transform.isEnabled());

	// a disable-capable kind DOES support it
	ModelComponent model;
	CHECK(static_cast<GameObjectComponent&>(model).supportsDisable());
}
//---------------------------------------------------------
TEST_CASE("TypeManager composes the inherited property schema along the base chain",
	"[component][enable][typemanager]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	TypeManager & types = TypeManager::getSingleton();

	// the base itself declares `enabled`
	const PropertySchema baseSchema = types.getInheritedPropertySchema(
		GameObjectComponent::getClassTypeInfo().getId());
	CHECK(baseSchema.find("enabled") != nullptr);

	// a subclass inherits it (its OWN raw schema does not carry it) AND keeps its
	// own fields
	const PropertySchema modelInherited = types.getInheritedPropertySchema(
		ModelComponent::getClassTypeInfo().getId());
	CHECK(modelInherited.find("enabled") != nullptr);	// inherited
	CHECK(modelInherited.find("mesh") != nullptr);		// own
}
//---------------------------------------------------------
TEST_CASE("A disabled component serializes only-when-false (byte-identical when enabled)",
	"[component][enable][serialization]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	// an ENABLED (default) component records NO `enabled` field - an untouched
	// scene is byte-identical, the format marker stays put
	ModelComponent enabledModel;
	GameObject::ComponentPropertyMap enabledCapture =
		SceneSerializer::captureComponentProperties(enabledModel);
	CHECK(enabledCapture.find("enabled") == enabledCapture.end());

	// a DISABLED component records the field, and it round-trips onto a fresh one
	ModelComponent disabledModel;
	disabledModel.setEnabled(false);
	GameObject::ComponentPropertyMap disabledCapture =
		SceneSerializer::captureComponentProperties(disabledModel);
	REQUIRE(disabledCapture.find("enabled") != disabledCapture.end());

	ModelComponent restored;
	CHECK(restored.isEnabled());	// constructed default before apply
	SceneSerializer::applyComponentProperties(disabledCapture, restored);
	CHECK_FALSE(restored.isEnabled());

	// applying the ENABLED capture (no field present) leaves the default true
	ModelComponent restoredEnabled;
	restoredEnabled.setEnabled(false);	// start disabled to prove absence != apply
	SceneSerializer::applyComponentProperties(enabledCapture, restoredEnabled);
	CHECK_FALSE(restoredEnabled.isEnabled());	// untouched - the field was absent
}
//---------------------------------------------------------
TEST_CASE("Sprite/Line/WorldText visibility is unified onto the one enabled flag",
	"[component][enable][unify]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	// the ad-hoc `visible` bool is gone: setSpriteVisible drives the ONE enable
	// switch, so there is exactly one flag (no overlapping `visible` property)
	SpriteComponent sprite;
	CHECK(sprite.isSpriteVisible());
	CHECK(sprite.isEnabled());
	sprite.setSpriteVisible(false);
	CHECK_FALSE(sprite.isEnabled());
	CHECK_FALSE(sprite.isSpriteVisible());
	CHECK_FALSE(sprite.effectivelyEnabled());
	sprite.setEnabled(true);
	CHECK(sprite.isSpriteVisible());	// the alias reads the same flag back

	// no reflected `visible` remains - only the inherited `enabled`
	const PropertySchema schema = getComponentSchema(sprite);
	CHECK(schema.find("visible") == nullptr);
	CHECK(schema.find("enabled") != nullptr);
}
//---------------------------------------------------------
TEST_CASE("Disabling a ParticleComponent stops emission and resumes it exactly",
	"[component][enable][particle]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	// the emitter keeps ticking while disabled so live particles drain
	// gracefully - the tick gate opts it back in
	ParticleComponent particles;
	// the trait is a base virtual (public) - dispatch through a base ref
	CHECK(static_cast<GameObjectComponent&>(particles).ticksWhileDisabled());

	// an emitting emitter STOPS emitting when disabled (live particles keep
	// simulating - the graceful contract, verified live in the selfcheck) ...
	particles.start();
	CHECK(particles.isEmitting());
	particles.setEnabled(false);
	CHECK_FALSE(particles.isEmitting());

	// ... and resumes emitting EXACTLY when re-enabled
	particles.setEnabled(true);
	CHECK(particles.isEmitting());

	// a NON-emitting emitter is not spuriously started by a disable/enable cycle
	ParticleComponent idle;
	CHECK_FALSE(idle.isEmitting());
	idle.setEnabled(false);
	idle.setEnabled(true);
	CHECK_FALSE(idle.isEmitting());
}
