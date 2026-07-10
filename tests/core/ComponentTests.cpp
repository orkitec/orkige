/**************************************************************
	created:	2026/07/07 at 14:00
	filename: 	ComponentTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"
#include "TestComponents.h"

#include <core_base/Object.h>

#include <algorithm>

using Orkige::optr;
using Orkige::woptr;

TEST_CASE("ComponentHolder add/get/has/remove lifecycle", "[components]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::registerOrkigeTestComponents();

	REQUIRE(Orkige::GameObject::isComponentRegistered<Orkige::TestHealthComponent>());

	optr<Orkige::GameObject> gameObject =
		Orkige::onew(new Orkige::GameObject("componentTestObject"));

	CHECK_FALSE(gameObject->hasComponent<Orkige::TestHealthComponent>());

	const int addCountBefore = Orkige::TestHealthComponent::addCount;
	REQUIRE(gameObject->addComponent<Orkige::TestHealthComponent>());
	CHECK(gameObject->hasComponent<Orkige::TestHealthComponent>());
	CHECK(Orkige::TestHealthComponent::addCount == addCountBefore + 1);

	// the same component cannot be attached twice
	CHECK_FALSE(gameObject->addComponent<Orkige::TestHealthComponent>());

	// the attached component is a live, stateful instance owned by the holder
	Orkige::TestHealthComponent * health =
		gameObject->getComponentPtr<Orkige::TestHealthComponent>();
	REQUIRE(health != nullptr);
	CHECK(health->getGameObject() == gameObject.get());
	CHECK(health->getHealth() == 100);
	health->setHealth(55);
	CHECK(gameObject->getComponentPtr<Orkige::TestHealthComponent>()
		->getHealth() == 55);

	// getAttachedComponentTypes reflects the attachment
	Orkige::TypeInfoList attached = gameObject->getAttachedComponentTypes();
	CHECK(attached.size() == 1);
	CHECK(attached.front() == Orkige::TestHealthComponent::getClassTypeInfo());

	const int removeCountBefore = Orkige::TestHealthComponent::removeCount;
	REQUIRE(gameObject->removeComponent<Orkige::TestHealthComponent>());
	CHECK_FALSE(gameObject->hasComponent<Orkige::TestHealthComponent>());
	CHECK(Orkige::TestHealthComponent::removeCount == removeCountBefore + 1);
	// removing an absent component fails
	CHECK_FALSE(gameObject->removeComponent<Orkige::TestHealthComponent>());
}

TEST_CASE("Component dependencies are auto-added and cascade on removal", "[components]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::registerOrkigeTestComponents();

	optr<Orkige::GameObject> gameObject =
		Orkige::onew(new Orkige::GameObject("dependencyTestObject"));

	// adding the dependent component pulls its dependency in
	REQUIRE(gameObject->addComponent<Orkige::TestArmorComponent>());
	CHECK(gameObject->hasComponent<Orkige::TestArmorComponent>());
	CHECK(gameObject->hasComponent<Orkige::TestHealthComponent>());

	// the dependency list registered via addDependency holds exactly the
	// health component (no duplicates from repeated construction)
	Orkige::TypeInfoList const & dependencies = Orkige::GameObject::
		getDependencies(Orkige::TestArmorComponent::getClassTypeInfo());
	REQUIRE(dependencies.size() == 1);
	CHECK(dependencies.front() ==
		Orkige::TestHealthComponent::getClassTypeInfo());

	// removing the DEPENDENCY cascades: the dependent armor goes too
	REQUIRE(gameObject->removeComponent<Orkige::TestHealthComponent>());
	CHECK_FALSE(gameObject->hasComponent<Orkige::TestHealthComponent>());
	CHECK_FALSE(gameObject->hasComponent<Orkige::TestArmorComponent>());
}

TEST_CASE("AttributeHolder set/get typed attributes", "[components]")
{
	Orkige::CoreTestEnvironment::get();

	Orkige::Object object("attributeTestObject");

	CHECK_FALSE(object.hasAttribute("health"));
	object.setAttribute("health", 42);
	object.setAttribute("name", Orkige::String("Bob"));
	object.setAttribute("ratio", 0.5f);
	object.setAttribute("alive", true);

	REQUIRE(object.hasAttribute("health"));
	CHECK(object.getAttribute<int>("health") == 42);
	CHECK(object.getAttribute<Orkige::String>("name") == "Bob");
	CHECK(object.getAttribute<float>("ratio") == 0.5f);
	CHECK(object.getAttribute<bool>("alive") == true);

	// setting an existing id replaces the value
	object.setAttribute("health", 43);
	CHECK(object.getAttribute<int>("health") == 43);

	// Object-derived attributes are stored without wrapping
	optr<Orkige::Object> child = Orkige::onew(new Orkige::Object("child"));
	object.setAttribute("child", child);
	CHECK(object.getAttribute("child")->getObjectID() == "child");
	CHECK(object.getAttribute("child") == child);

	// deletion
	REQUIRE(object.delAttribute("health"));
	CHECK_FALSE(object.hasAttribute("health"));
	CHECK_FALSE(object.delAttribute("health"));

	object.clearAttributes();
	CHECK_FALSE(object.hasAttribute("name"));
	CHECK(object.getAttributes().empty());
}
