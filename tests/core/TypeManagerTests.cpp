/**************************************************************
	created:	2026/07/07 at 14:00
	filename: 	TypeManagerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_base/TypeManager.h>
#include <core_base/Object.h>
#include <core_serialization/XMLArchive.h>

TEST_CASE("TypeInfo identity is name-hash based", "[typemanager]")
{
	const Orkige::TypeInfo a("SomeTypeName");
	const Orkige::TypeInfo sameAsA("SomeTypeName");
	const Orkige::TypeInfo b("OtherTypeName");

	CHECK(a.getName() == "SomeTypeName");
	CHECK(a == sameAsA);
	CHECK(a.getId() == sameAsA.getId());
	CHECK(a != b);
	CHECK((a < b || b < a));	// strict ordering between distinct types
	CHECK_FALSE(a < sameAsA);
}

TEST_CASE("TypeManager creates registered types by name", "[typemanager]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TypeManager & typeManager = Orkige::TypeManager::getSingleton();

	// module init registered the core objects
	REQUIRE(typeManager.isRegistered("Object"));
	REQUIRE(typeManager.isRegistered("XMLArchive"));

	Orkige::Interface * created = typeManager.create("Object");
	REQUIRE(created != nullptr);
	CHECK(created->getTypeInfo().getName() == "Object");
	CHECK(dynamic_cast<Orkige::Object*>(created) != nullptr);
	delete created;

	// unknown names create nothing
	CHECK_FALSE(typeManager.isRegistered("NoSuchTypeAnywhere"));
	CHECK(typeManager.create("NoSuchTypeAnywhere") == nullptr);
}

TEST_CASE("TypeManager registerType rejects duplicate names", "[typemanager]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TypeManager & typeManager = Orkige::TypeManager::getSingleton();

	// "Object" was registered by init_module_orkige_core
	CHECK_FALSE(typeManager.registerType<Orkige::Object>("Object"));

	// a fresh name registers fine, and exactly once
	CHECK(typeManager.registerType<Orkige::Object>("UnitTestAliasedObject"));
	CHECK_FALSE(typeManager.registerType<Orkige::Object>("UnitTestAliasedObject"));
	Orkige::Interface * aliased = typeManager.create("UnitTestAliasedObject");
	REQUIRE(aliased != nullptr);
	// the alias creates a plain Object (TypeInfo comes from the class)
	CHECK(aliased->getTypeInfo().getName() == "Object");
	delete aliased;
	CHECK(typeManager.unRegister("UnitTestAliasedObject"));
	CHECK_FALSE(typeManager.isRegistered("UnitTestAliasedObject"));
}
