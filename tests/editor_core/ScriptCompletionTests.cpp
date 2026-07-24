// ScriptCompletionTests - the Script panel's completion symbol collection,
// pinned headlessly: the API-index parse, the reflected-kind fold, the
// runtime-table fold and the context-classifying query. Every source is fed
// as PURE data (a fake index text, DTO kinds, name lists) - exactly what the
// collector's design promises the tests can do.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include <catch2/catch_test_macros.hpp>

#include "ScriptCompletion.h"

#include <algorithm>

using namespace Orkige;

namespace
{
	bool contains(std::vector<String> const& names, String const& name)
	{
		return std::find(names.begin(), names.end(), name) != names.end();
	}

	ScriptCompletionSymbols makeFixtureSymbols()
	{
		ScriptCompletionSymbols symbols;
		addLuaKeywords(symbols);
		// a fake generated-index excerpt in the real format
		addApiIndexSymbols(symbols,
			"```text\n"
			"# GLOBAL TABLES\n"
			"\n"
			"## world\n"
			"world.exists(id) -> bool  -- is a GameObject alive\n"
			"world.get(id) -> GameObject?  -- the live GameObject\n"
			"world.setTimeScale(scale)  -- gameplay time scale\n"
			"\n"
			"## music\n"
			"music.play(id, file [, loop]) -> bool  -- start a track\n"
			"\n"
			"## globals\n"
			"loc(key [, ...]) -> string  -- localised string\n"
			"\n"
			"## Vector3\n"
			"Vector3(x, y, z)  -- 3D vector\n"
			"Vector3:length() -> number  -- magnitude\n"
			"Vector3.x  -- x component\n"
			"```\n");
		// the reflected component kinds (the registry DTO)
		addReflectedKinds(symbols, {
			{ "transform", "getTransform", "TransformComponent",
			  { "position", "orientation", "scale" } },
			{ "rigidbody", "getRigidBody", "RigidBodyComponent",
			  { "mass", "layer" } },
		});
		// a live runtime table (the ScriptRuntime enumeration feed)
		addRuntimeTable(symbols, "Engine",
			{ "getSingleton", "setBloom", "setGrade", "__index", "__name" });
		symbols.finalize();
		return symbols;
	}
}

TEST_CASE("completion parses the generated API index", "[editor][completion]")
{
	const ScriptCompletionSymbols symbols = makeFixtureSymbols();
	// signature tables became globals, their members registered per prefix
	CHECK(contains(symbols.globals(), "world"));
	CHECK(contains(symbols.globals(), "music"));
	CHECK(contains(symbols.globals(), "loc"));	// a bare global function
	REQUIRE(symbols.membersFor("world") != nullptr);
	CHECK(contains(*symbols.membersFor("world"), "exists"));
	CHECK(contains(*symbols.membersFor("world"), "setTimeScale"));
	// Type:method and Type.field both land under the type prefix
	REQUIRE(symbols.membersFor("Vector3") != nullptr);
	CHECK(contains(*symbols.membersFor("Vector3"), "length"));
	CHECK(contains(*symbols.membersFor("Vector3"), "x"));
	// header lines ("# ...", "## ...") and fences never became symbols
	CHECK_FALSE(contains(symbols.globals(), "GLOBAL"));
}

TEST_CASE("completion folds the reflected kinds into self/world/properties",
	"[editor][completion]")
{
	const ScriptCompletionSymbols symbols = makeFixtureSymbols();
	REQUIRE(symbols.membersFor("self") != nullptr);
	CHECK(contains(*symbols.membersFor("self"), "transform"));
	CHECK(contains(*symbols.membersFor("self"), "rigidbody"));
	// the universal self surface
	CHECK(contains(*symbols.membersFor("self"), "id"));
	CHECK(contains(*symbols.membersFor("self"), "getComponent"));
	// the registry's world accessors merged into the world members
	CHECK(contains(*symbols.membersFor("world"), "getTransform"));
	CHECK(contains(*symbols.membersFor("world"), "getRigidBody"));
	// component properties complete after the self-field AND the kind name
	REQUIRE(symbols.membersFor("transform") != nullptr);
	CHECK(contains(*symbols.membersFor("transform"), "position"));
	REQUIRE(symbols.membersFor("TransformComponent") != nullptr);
	CHECK(contains(*symbols.membersFor("TransformComponent"), "scale"));
}

TEST_CASE("completion folds runtime tables and skips metatable internals",
	"[editor][completion]")
{
	const ScriptCompletionSymbols symbols = makeFixtureSymbols();
	REQUIRE(symbols.membersFor("Engine") != nullptr);
	CHECK(contains(*symbols.membersFor("Engine"), "setBloom"));
	CHECK_FALSE(contains(*symbols.membersFor("Engine"), "__index"));
	// the case-insensitive prefix fallback: the `local engine =
	// Engine.getSingleton()` idiom completes engine: from Engine's members
	REQUIRE(symbols.membersFor("engine") != nullptr);
	CHECK(contains(*symbols.membersFor("engine"), "setGrade"));
}

TEST_CASE("completion queries classify the context", "[editor][completion]")
{
	const ScriptCompletionSymbols symbols = makeFixtureSymbols();
	// after "world." only world's members apply, prefix-filtered
	{
		const std::vector<String> suggestions = suggestCompletions(symbols,
			"    local alive = world.", "ex", {}, 20);
		REQUIRE_FALSE(suggestions.empty());
		CHECK(suggestions[0] == "exists");
		CHECK_FALSE(contains(suggestions, "play"));
	}
	// ':' works like '.' (method-call spelling)
	{
		const std::vector<String> suggestions = suggestCompletions(symbols,
			"engine:", "setB", {}, 20);
		REQUIRE_FALSE(suggestions.empty());
		CHECK(suggestions[0] == "setBloom");
	}
	// bare identifiers offer globals + keywords + document identifiers
	{
		const std::vector<String> suggestions = suggestCompletions(symbols,
			"    if ", "lo", { "localHelper" }, 20);
		CHECK(contains(suggestions, "local"));		// keyword
		CHECK(contains(suggestions, "loc"));		// API global
		CHECK(contains(suggestions, "localHelper"));	// document identifier
		CHECK_FALSE(contains(suggestions, "exists"));	// member-only name
	}
	// an unknown table falls back to the document's identifiers
	{
		const std::vector<String> suggestions = suggestCompletions(symbols,
			"myTable.", "fi", { "field1", "other" }, 20);
		CHECK(contains(suggestions, "field1"));
		CHECK_FALSE(contains(suggestions, "other"));
	}
	// the limit bounds the list (an empty fragment offers everything)
	{
		const std::vector<String> suggestions = suggestCompletions(symbols,
			"", "", {}, 3);
		CHECK(suggestions.size() == 3);
	}
	// the already-typed exact fragment is not suggested back
	{
		const std::vector<String> suggestions = suggestCompletions(symbols,
			"world.", "exists", {}, 20);
		CHECK_FALSE(contains(suggestions, "exists"));
	}
}
