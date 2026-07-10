/**************************************************************
	created:	2026/07/07 at 14:00
	filename: 	XMLArchiveTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_serialization/XMLArchive.h>
#include <core_base/Object.h>

#include <filesystem>

using Orkige::optr;
using Orkige::woptr;

namespace
{
	//! RAII temp file below std::filesystem::temp_directory_path()
	struct TempFile
	{
		Orkige::String path;
		explicit TempFile(std::string const & name)
			: path((std::filesystem::temp_directory_path() / name).string())
		{
			std::filesystem::remove(this->path);
		}
		~TempFile()
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
		}
	};
}

// Regression coverage for the sequential-read bug: reading N values in a row
// must advance the archive cursor after EVERY read (element passed by
// reference in XMLArchiveReadElement) - a cursor that doesn't advance makes
// every read return the first element again.
TEST_CASE("XMLArchive round-trips a sequence of primitives through a file", "[xmlarchive]")
{
	Orkige::CoreTestEnvironment::get();
	TempFile file("orkige_test_primitives.xml");

	const bool boolTrue = true;
	const bool boolFalse = false;
	const char charValue = 'x';
	const short shortValue = -1234;
	const unsigned short ushortValue = 65500;
	const int intValue = -123456;
	const unsigned int uintValue = 4000000000u;
	const long longValue = -7654321L;
	const unsigned long ulongValue = 3000000000UL;
	const float floatValue = 3.25f;
	const double doubleValue = -2.5;
	const Orkige::String stringValue = "hello world with spaces";
	const Orkige::String emptyString = "";

	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(file.path));
		ar << boolTrue << boolFalse << charValue << shortValue << ushortValue
			<< intValue << uintValue << longValue << ulongValue
			<< floatValue << doubleValue << stringValue << emptyString;
		REQUIRE(ar->stopWriting());
	}

	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		bool rBoolTrue = false, rBoolFalse = true;
		char rChar = 0;
		short rShort = 0;
		unsigned short rUshort = 0;
		int rInt = 0;
		unsigned int rUint = 0;
		long rLong = 0;
		unsigned long rUlong = 0;
		float rFloat = 0.0f;
		double rDouble = 0.0;
		Orkige::String rString, rEmpty = "sentinel";
		ar >> rBoolTrue >> rBoolFalse >> rChar >> rShort >> rUshort
			>> rInt >> rUint >> rLong >> rUlong
			>> rFloat >> rDouble >> rString >> rEmpty;
		REQUIRE(ar->stopReading());

		CHECK(rBoolTrue == boolTrue);
		CHECK(rBoolFalse == boolFalse);
		CHECK(rChar == charValue);
		CHECK(rShort == shortValue);
		CHECK(rUshort == ushortValue);
		CHECK(rInt == intValue);
		CHECK(rUint == uintValue);
		CHECK(rLong == longValue);
		CHECK(rUlong == ulongValue);
		CHECK(rFloat == floatValue);
		CHECK(rDouble == doubleValue);
		CHECK(rString == stringValue);
		CHECK(rEmpty == emptyString);
	}
}

TEST_CASE("XMLArchive round-trips optr primitives and preserves pointer identity", "[xmlarchive]")
{
	Orkige::CoreTestEnvironment::get();
	TempFile file("orkige_test_optr.xml");

	optr<int> sharedInt = Orkige::onew(new int(77));
	// regression: optr<unsigned long> used to be written as an element named
	// "unsigned long" (space = invalid XML) and the file would not parse back
	optr<unsigned long> sharedUlong = Orkige::onew(new unsigned long(3000000000UL));
	optr<Orkige::String> sharedString =
		Orkige::onew(new Orkige::String("shared string"));

	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(file.path));
		// the same pointer written twice must be stored once + one reference
		ar << sharedInt << sharedInt << sharedUlong << sharedString;
		REQUIRE(ar->stopWriting());
	}

	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		optr<int> firstInt, secondInt;
		optr<unsigned long> rUlong;
		optr<Orkige::String> rString;
		ar >> firstInt >> secondInt >> rUlong >> rString;
		REQUIRE(ar->stopReading());

		REQUIRE(firstInt);
		REQUIRE(secondInt);
		CHECK(*firstInt == 77);
		// the reference read must resolve to the SAME object
		CHECK(firstInt.get() == secondInt.get());
		REQUIRE(rUlong);
		CHECK(*rUlong == 3000000000UL);
		REQUIRE(rString);
		CHECK(*rString == "shared string");
	}
}

// Regression coverage for the AttributeWrapper TypeManager registration: an
// Object's wrapped attributes (setAttribute(id, 42)) are serialized as
// "intObjectAttributeWrapper" etc. elements and must be re-creatable through
// TypeManager::create when loading.
TEST_CASE("XMLArchive round-trips an Object with typed attributes", "[xmlarchive]")
{
	Orkige::CoreTestEnvironment::get();
	TempFile file("orkige_test_object.xml");

	{
		Orkige::Object original("test_object");
		original.setAttribute("health", 42);
		original.setAttribute("name", Orkige::String("Bob the Cube"));
		original.setAttribute("ratio", 0.25f);
		original.setAttribute("alive", true);

		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(file.path));
		ar << original;
		REQUIRE(ar->stopWriting());
	}

	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		Orkige::Object loaded;
		ar >> loaded;
		REQUIRE(ar->stopReading());

		CHECK(loaded.getObjectID() == "test_object");
		REQUIRE(loaded.hasAttribute("health"));
		CHECK(loaded.getAttribute<int>("health") == 42);
		REQUIRE(loaded.hasAttribute("name"));
		CHECK(loaded.getAttribute<Orkige::String>("name") == "Bob the Cube");
		REQUIRE(loaded.hasAttribute("ratio"));
		CHECK(loaded.getAttribute<float>("ratio") == 0.25f);
		REQUIRE(loaded.hasAttribute("alive"));
		CHECK(loaded.getAttribute<bool>("alive") == true);
	}
}

TEST_CASE("XMLArchive startReading fails cleanly on a missing file", "[xmlarchive]")
{
	Orkige::CoreTestEnvironment::get();
	optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
	CHECK_FALSE(ar->startReading(
		(std::filesystem::temp_directory_path() /
			"orkige_test_does_not_exist.xml").string()));
}

// The asset-database serialization contract (core_project/AssetDatabase):
// a String value may carry a named side ATTRIBUTE next to it - positionally
// invisible, so archives stay loadable in BOTH directions across the format
// change (old scenes have no attribute, old readers ignore it).
TEST_CASE("XMLArchive attributed String values round-trip and stay "
	"legacy-compatible", "[xmlarchive]")
{
	Orkige::CoreTestEnvironment::get();
	TempFile file("orkige_test_attributed.xml");

	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(file.path));
		// attributed, attributed-with-empty (writes plain), plain
		ar->writeAttributed("ball.png", "assetId", "0123456789abcdef");
		ar->writeAttributed("plain.png", "assetId", "");
		Orkige::String legacy = "legacy.png";
		ar << legacy;
		int sentinel = 7;
		ar << sentinel;
		REQUIRE(ar->stopWriting());
	}

	SECTION("readAttributed sees the attribute (and \"\" where none is)")
	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		Orkige::String value, attribute;
		ar->readAttributed(value, "assetId", attribute);
		CHECK(value == "ball.png");
		CHECK(attribute == "0123456789abcdef");
		ar->readAttributed(value, "assetId", attribute);
		CHECK(value == "plain.png");
		CHECK(attribute.empty());
		// a LEGACY value (plain write) reads with an empty attribute
		ar->readAttributed(value, "assetId", attribute);
		CHECK(value == "legacy.png");
		CHECK(attribute.empty());
		// the cursor advanced correctly through all three
		int sentinel = 0;
		ar >> sentinel;
		CHECK(sentinel == 7);
		REQUIRE(ar->stopReading());
	}
	SECTION("an attribute-unaware reader (old build) reads the plain values")
	{
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		Orkige::String first, second, third;
		ar >> first >> second >> third;
		CHECK(first == "ball.png");
		CHECK(second == "plain.png");
		CHECK(third == "legacy.png");
		int sentinel = 0;
		ar >> sentinel;
		CHECK(sentinel == 7);
		REQUIRE(ar->stopReading());
	}
}
