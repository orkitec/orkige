/**************************************************************
	created:	2026/07/09 at 18:30
	filename: 	PropertyTweenTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The declarative property-path tween (core_game/PropertyTween): it resolves a
	reflected property by (objectId, componentType, propertyName) through the
	property registry and interpolates it toward a target for the numeric kinds
	(Float/Int/Vec3/Color), rejecting every other kind with an honest error.
	Headless and scripting-free - the Lua `tween.property` binding is a thin face
	on top, proven end to end by the player tween selfcheck.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <core_game/PropertyTween.h>
#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_tween/TweenManager.h>
#include <core_tween/EaseLibrary.h>

using Orkige::optr;
using Orkige::woptr;

using Catch::Approx;

namespace
{
	//! a fresh world with one object carrying the reflected tween-target component
	Orkige::TestTweenTargetComponent* makeTarget(
		Orkige::GameObjectManager & manager, Orkige::String const & id)
	{
		Orkige::registerOrkigeTestComponents();
		optr<Orkige::GameObject> object = manager.createGameObject(id).lock();
		REQUIRE(object);
		REQUIRE(object->addComponent<Orkige::TestTweenTargetComponent>());
		return object->getComponentPtr<Orkige::TestTweenTargetComponent>();
	}
}

//---------------------------------------------------------
TEST_CASE("PropertyTween interpolates a reflected Float to its target",
	"[tween][property]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	env.gameObjectManager.clear();
	Orkige::TweenManager tweens;

	Orkige::TestTweenTargetComponent * target = makeTarget(env.gameObjectManager, "Obj");
	target->setScalar(0.0f);

	Orkige::String error;
	const Orkige::TweenManager::TweenId id = Orkige::PropertyTween::start(
		"Obj", "TestTweenTargetComponent", "scalar", "10", 1.0f,
		&Orkige::Ease::linear, Orkige::TweenManager::CompleteFunction(), 0.0f,
		&error);
	REQUIRE(id != 0);
	CHECK(error.empty());

	tweens.update(0.5f);					// linear halfway
	CHECK(target->getScalar() == Approx(5.0f));
	tweens.update(0.6f);					// past the end lands EXACTLY on target
	CHECK(target->getScalar() == Approx(10.0f));
	CHECK(tweens.getActiveCount() == 0);

	env.gameObjectManager.clear();
}
//---------------------------------------------------------
TEST_CASE("PropertyTween interpolates a reflected Vec3 by name", "[tween][property]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	env.gameObjectManager.clear();
	Orkige::TweenManager tweens;

	Orkige::TestTweenTargetComponent * target = makeTarget(env.gameObjectManager, "Obj");
	Orkige::PropVec3 start; start.x = 0.0f; start.y = 0.0f; start.z = 0.0f;
	target->setOffset(start);

	Orkige::String error;
	const Orkige::TweenManager::TweenId id = Orkige::PropertyTween::start(
		"Obj", "TestTweenTargetComponent", "offset", "2 4 6", 1.0f,
		&Orkige::Ease::linear, Orkige::TweenManager::CompleteFunction(), 0.0f,
		&error);
	REQUIRE(id != 0);

	tweens.update(0.5f);
	CHECK(target->getOffset().x == Approx(1.0f));
	CHECK(target->getOffset().y == Approx(2.0f));
	CHECK(target->getOffset().z == Approx(3.0f));
	tweens.update(0.6f);
	CHECK(target->getOffset().x == Approx(2.0f));
	CHECK(target->getOffset().y == Approx(4.0f));
	CHECK(target->getOffset().z == Approx(6.0f));

	env.gameObjectManager.clear();
}
//---------------------------------------------------------
TEST_CASE("PropertyTween interpolates a reflected Color by name", "[tween][property]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	env.gameObjectManager.clear();
	Orkige::TweenManager tweens;

	Orkige::TestTweenTargetComponent * target = makeTarget(env.gameObjectManager, "Obj");
	Orkige::PropColor start; start.r = 0.0f; start.g = 0.0f; start.b = 0.0f; start.a = 0.0f;
	target->setColor(start);

	Orkige::String error;
	const Orkige::TweenManager::TweenId id = Orkige::PropertyTween::start(
		"Obj", "TestTweenTargetComponent", "color", "1 0.5 0.25 1", 1.0f,
		&Orkige::Ease::linear, Orkige::TweenManager::CompleteFunction(), 0.0f,
		&error);
	REQUIRE(id != 0);

	tweens.update(1.5f);					// straight to the end
	CHECK(target->getColor().r == Approx(1.0f));
	CHECK(target->getColor().g == Approx(0.5f));
	CHECK(target->getColor().b == Approx(0.25f));
	CHECK(target->getColor().a == Approx(1.0f));

	env.gameObjectManager.clear();
}
//---------------------------------------------------------
TEST_CASE("PropertyTween rejects a non-numeric property with an honest error",
	"[tween][property]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	env.gameObjectManager.clear();
	Orkige::TweenManager tweens;

	makeTarget(env.gameObjectManager, "Obj");

	SECTION("a String property is not interpolatable")
	{
		Orkige::String error;
		const Orkige::TweenManager::TweenId id = Orkige::PropertyTween::start(
			"Obj", "TestTweenTargetComponent", "name", "hello", 1.0f,
			&Orkige::Ease::linear, Orkige::TweenManager::CompleteFunction(), 0.0f,
			&error);
		CHECK(id == 0);
		CHECK_FALSE(error.empty());
		CHECK(tweens.getActiveCount() == 0);
	}

	SECTION("an unknown property is an error")
	{
		Orkige::String error;
		const Orkige::TweenManager::TweenId id = Orkige::PropertyTween::start(
			"Obj", "TestTweenTargetComponent", "nope", "1", 1.0f,
			&Orkige::Ease::linear, Orkige::TweenManager::CompleteFunction(), 0.0f,
			&error);
		CHECK(id == 0);
		CHECK_FALSE(error.empty());
	}

	SECTION("a missing object is an error")
	{
		Orkige::String error;
		const Orkige::TweenManager::TweenId id = Orkige::PropertyTween::start(
			"Ghost", "TestTweenTargetComponent", "scalar", "1", 1.0f,
			&Orkige::Ease::linear, Orkige::TweenManager::CompleteFunction(), 0.0f,
			&error);
		CHECK(id == 0);
		CHECK_FALSE(error.empty());
	}

	env.gameObjectManager.clear();
}
//---------------------------------------------------------
TEST_CASE("PropertyTween is reaped when its target object dies", "[tween][property]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	env.gameObjectManager.clear();
	Orkige::TweenManager tweens;

	Orkige::TestTweenTargetComponent * target = makeTarget(env.gameObjectManager, "Victim");
	target->setScalar(0.0f);

	Orkige::String error;
	REQUIRE(Orkige::PropertyTween::start("Victim", "TestTweenTargetComponent",
		"scalar", "10", 10.0f, &Orkige::Ease::linear,
		Orkige::TweenManager::CompleteFunction(), 0.0f, &error) != 0);
	tweens.update(0.1f);
	CHECK(tweens.getActiveCount() == 1);

	// the target dies between frames: the id-keyed reap retires the tween
	REQUIRE(env.gameObjectManager.delGameObject("Victim"));
	tweens.update(0.1f);
	CHECK(tweens.getActiveCount() == 0);

	env.gameObjectManager.clear();
}
