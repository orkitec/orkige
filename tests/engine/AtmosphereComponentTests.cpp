/**************************************************************
	created:	2026/07/24 at 18:00
	filename: 	AtmosphereComponentTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	AtmosphereComponent's headless half: the reflected schema (declaration
	order IS the precedence rule - preset before the look fields), the
	preset-seeds/fields-override authoring model, the reflected round-trip
	on detached and attached components, and the TAKE-OVER contract (the
	first active instance owns, dormant siblings, promotion on
	deactivation/removal, take-back on reactivation) driven headlessly on
	real GameObjects (no render world - the ownership registry is pure
	bookkeeping; the armed pixels are the editor_atmosphere and
	player_atmosphere integration runs, both flavors).
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/AtmosphereComponent.h>

#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_base/PropertySchema.h>
#include <core_base/PropertyValue.h>
#include <core_base/TypeManager.h>

using Orkige::optr;
using Orkige::woptr;

namespace
{
	//! create a bare GameObject carrying only an AtmosphereComponent
	optr<Orkige::GameObject> makeEnvironment(char const * id)
	{
		optr<Orkige::GameObject> gameObject =
			Orkige::GameObjectManager::getSingleton().createGameObject(id).lock();
		REQUIRE(gameObject);
		REQUIRE(gameObject->addComponent<Orkige::AtmosphereComponent>());
		return gameObject;
	}

	Orkige::AtmosphereComponent * atmosphereOf(
		optr<Orkige::GameObject> const & gameObject)
	{
		return gameObject->getComponentPtr<Orkige::AtmosphereComponent>();
	}
}
//---------------------------------------------------------
TEST_CASE("AtmosphereComponent declares its reflected schema with preset "
	"before the look fields", "[reflection][atmosphere]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		AtmosphereComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	PropertyDesc const * enabled = schema->find("enabled");
	REQUIRE(enabled != nullptr);
	CHECK(enabled->kind == PropertyKind::Bool);

	PropertyDesc const * preset = schema->find("preset");
	REQUIRE(preset != nullptr);
	CHECK(preset->kind == PropertyKind::String);

	for(char const * name : { "skyPower", "density", "sunPower",
		"ambientPower", "fogDensity" })
	{
		PropertyDesc const * desc = schema->find(name);
		INFO("property " << name);
		REQUIRE(desc != nullptr);
		CHECK(desc->kind == PropertyKind::Float);
		CHECK_FALSE(desc->isReadOnly());
	}
	for(char const * name : { "skyColour", "fogColour" })
	{
		PropertyDesc const * desc = schema->find(name);
		INFO("property " << name);
		REQUIRE(desc != nullptr);
		CHECK(desc->kind == PropertyKind::Color);
	}

	// DECLARATION ORDER IS THE PRECEDENCE RULE: loads apply in schema order,
	// so `preset` must precede every look field it seeds (a serialized
	// preset re-seeds first, the serialized explicit fields override after)
	int presetIndex = -1;
	int firstLookIndex = -1;
	int index = 0;
	for(PropertyDesc const & desc : schema->properties())
	{
		if(desc.name == "preset")
		{
			presetIndex = index;
		}
		else if(desc.name == "skyColour" && firstLookIndex < 0)
		{
			firstLookIndex = index;
		}
		++index;
	}
	REQUIRE(presetIndex >= 0);
	REQUIRE(firstLookIndex >= 0);
	CHECK(presetIndex < firstLookIndex);
}
//---------------------------------------------------------
TEST_CASE("AtmosphereComponent presets seed and explicit fields override",
	"[atmosphere]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	// the constructed default IS the day preset, enabled (the template look)
	AtmosphereComponent atmosphere;
	CHECK(atmosphere.getEnabled());
	CHECK(atmosphere.getPreset() == "day");
	CHECK(atmosphere.getSkyPower() == Catch::Approx(1.0f));
	CHECK(atmosphere.getDensity() == Catch::Approx(0.47f));

	// a named preset seeds EVERY look field from the tested table
	atmosphere.setPreset("night");
	const AtmosphereDesc night = AtmospherePreset::forSky(
		AtmospherePreset::SKY_NIGHT);
	CHECK(atmosphere.getPreset() == "night");
	CHECK(atmosphere.getSkyPower() == Catch::Approx(night.skyPower));
	CHECK(atmosphere.getDensity() == Catch::Approx(night.density));
	CHECK(atmosphere.getSunPower() == Catch::Approx(night.sunPower));
	CHECK(atmosphere.getFogDensity() == Catch::Approx(night.fogDensity));

	// an explicit field then overrides ITS seed; the others keep the preset's
	// values and the preset label stays (it is the last-seed record, not a
	// live "modified" flag - the round-trip depends on that)
	atmosphere.setDensity(0.2f);
	CHECK(atmosphere.getDensity() == Catch::Approx(0.2f));
	CHECK(atmosphere.getSkyPower() == Catch::Approx(night.skyPower));
	CHECK(atmosphere.getPreset() == "night");

	// case-insensitive words parse; an unknown word warns and changes nothing
	atmosphere.setPreset("DAY");
	CHECK(atmosphere.getPreset() == "day");
	CHECK(atmosphere.getDensity() == Catch::Approx(0.47f));
	atmosphere.setPreset("noon");
	CHECK(atmosphere.getPreset() == "day");

	// "custom" seeds nothing - the stored fields stand
	atmosphere.setSunPower(2.5f);
	atmosphere.setPreset("custom");
	CHECK(atmosphere.getSunPower() == Catch::Approx(2.5f));

	// enabled is independent of the preset seed
	atmosphere.setEnabled(false);
	atmosphere.setPreset("sunset");
	CHECK_FALSE(atmosphere.getEnabled());

	// the armed desc = the look fields + the enabled switch
	const AtmosphereDesc armed = atmosphere.buildDesc();
	CHECK_FALSE(armed.enabled);
	CHECK(armed.density == Catch::Approx(0.9f));	// the sunset seed
}
//---------------------------------------------------------
TEST_CASE("AtmosphereComponent properties round-trip through the registry on "
	"a DETACHED component", "[reflection][atmosphere]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();

	PropertySchema const * schema = TypeManager::getSingleton().getPropertySchema(
		AtmosphereComponent::getClassTypeInfo().getId());
	REQUIRE(schema != nullptr);

	AtmosphereComponent atmosphere;
	Object * instance = &atmosphere;

	PropertyDesc const * preset = schema->find("preset");
	REQUIRE(preset != nullptr);
	preset->set(instance, PropertyValue::makeString("sunset"));
	CHECK(atmosphere.getPreset() == "sunset");
	CHECK(preset->get(instance).asString() == "sunset");
	CHECK(atmosphere.getDensity() == Catch::Approx(0.9f));	// the seed applied

	PropertyDesc const * density = schema->find("density");
	REQUIRE(density != nullptr);
	density->set(instance, PropertyValue::makeFloat(0.33f));
	CHECK(atmosphere.getDensity() == Catch::Approx(0.33f));
	CHECK(density->get(instance).asFloat() == Catch::Approx(0.33f));

	PropertyDesc const * enabled = schema->find("enabled");
	REQUIRE(enabled != nullptr);
	enabled->set(instance, PropertyValue::makeBool(false));
	CHECK_FALSE(atmosphere.getEnabled());

	// the scalar setters clamp to sane floors
	atmosphere.setSkyPower(-2.0f);
	CHECK(atmosphere.getSkyPower() == 0.0f);
	atmosphere.setFogDensity(-1.0f);
	CHECK(atmosphere.getFogDensity() == 0.0f);
}
//---------------------------------------------------------
TEST_CASE("AtmosphereComponent take-over contract: first active owns, "
	"promotion and take-back", "[atmosphere][ownership]")
{
	using namespace Orkige;
	EngineTestEnvironment::get();
	GameObjectManager & manager = GameObjectManager::getSingleton();
	manager.clear();

	// no instance -> no owner
	CHECK(AtmosphereComponent::getAtmosphereOwner() == nullptr);

	// the FIRST added active instance owns; the second is dormant
	optr<GameObject> first = makeEnvironment("EnvironmentA");
	CHECK(atmosphereOf(first)->isAtmosphereOwner());
	optr<GameObject> second = makeEnvironment("EnvironmentB");
	CHECK(atmosphereOf(first)->isAtmosphereOwner());
	CHECK_FALSE(atmosphereOf(second)->isAtmosphereOwner());
	CHECK(AtmosphereComponent::getAtmosphereOwner() == atmosphereOf(first));

	// deactivating the owner PROMOTES the next active instance
	first->setActive(false);
	CHECK_FALSE(atmosphereOf(first)->isAtmosphereOwner());
	CHECK(atmosphereOf(second)->isAtmosphereOwner());

	// reactivating the earlier instance TAKES the ownership back (the owner
	// is always the first active instance in add order - deterministic)
	first->setActive(true);
	CHECK(atmosphereOf(first)->isAtmosphereOwner());
	CHECK_FALSE(atmosphereOf(second)->isAtmosphereOwner());

	// removing the owner promotes; removing the last leaves no owner
	REQUIRE(first->removeComponent(
		AtmosphereComponent::getClassTypeInfo()));
	CHECK(atmosphereOf(second)->isAtmosphereOwner());
	REQUIRE(second->removeComponent(
		AtmosphereComponent::getClassTypeInfo()));
	CHECK(AtmosphereComponent::getAtmosphereOwner() == nullptr);

	// a component added onto an already-INACTIVE object never takes over
	optr<GameObject> sleeper = manager.createGameObject("EnvSleeper").lock();
	REQUIRE(sleeper);
	sleeper->setActive(false);
	REQUIRE(sleeper->addComponent<AtmosphereComponent>());
	CHECK_FALSE(atmosphereOf(sleeper)->isAtmosphereOwner());
	CHECK(AtmosphereComponent::getAtmosphereOwner() == nullptr);
	sleeper->setActive(true);
	CHECK(atmosphereOf(sleeper)->isAtmosphereOwner());

	// PARENT deactivation counts too (activeInHierarchy, not activeSelf)
	optr<GameObject> child = manager.createGameObject("EnvChild").lock();
	REQUIRE(child);
	REQUIRE(child->setParent("EnvSleeper"));
	REQUIRE(child->addComponent<AtmosphereComponent>());
	CHECK_FALSE(atmosphereOf(child)->isAtmosphereOwner());
	sleeper->setActive(false);
	// both are inactive in hierarchy now - nobody owns
	CHECK(AtmosphereComponent::getAtmosphereOwner() == nullptr);
	sleeper->setActive(true);
	CHECK(atmosphereOf(sleeper)->isAtmosphereOwner());

	// a torn-down world leaves no owner (the test's own handles go first -
	// an externally-held GameObject legitimately keeps its components alive)
	first.reset();
	second.reset();
	sleeper.reset();
	child.reset();
	manager.clear();
	CHECK(AtmosphereComponent::getAtmosphereOwner() == nullptr);
}
