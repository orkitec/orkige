/**************************************************************
	created:	2026/07/11 at 22:30
	filename: 	ScreenStackTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the pure LIFO screen-name stack behind the gui
	screen-flow router (core_util/ScreenStack). No renderer, no widget - just the
	push/replace/pop/current arithmetic the router drives widget lifecycles off.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/ScreenStack.h"

using namespace Orkige;

TEST_CASE("ScreenStack: empty stack has no current", "[unit][gui][screens]")
{
	ScreenStack stack;
	CHECK(stack.empty());
	CHECK(stack.size() == 0u);
	CHECK(stack.current() == "");
	CHECK(stack.pop() == "");	// popping empty is a harmless no-op
	CHECK(stack.empty());
}

TEST_CASE("ScreenStack: push covers, pop reveals (LIFO)", "[unit][gui][screens]")
{
	ScreenStack stack;
	stack.push("title");
	CHECK(stack.current() == "title");
	CHECK(stack.size() == 1u);

	stack.push("settings");
	CHECK(stack.current() == "settings");
	CHECK(stack.size() == 2u);

	// the whole bottom-to-top path (the agent readback)
	REQUIRE(stack.path().size() == 2u);
	CHECK(stack.path()[0] == "title");
	CHECK(stack.path()[1] == "settings");

	// pop returns the popped screen and reveals the one beneath
	CHECK(stack.pop() == "settings");
	CHECK(stack.current() == "title");
	CHECK(stack.size() == 1u);
}

TEST_CASE("ScreenStack: replace swaps the top without changing depth",
	"[unit][gui][screens]")
{
	ScreenStack stack;
	stack.push("a");
	stack.push("b");
	stack.replace("c");
	CHECK(stack.size() == 2u);		// depth unchanged
	CHECK(stack.current() == "c");	// top swapped
	CHECK(stack.path()[0] == "a");	// the one beneath is untouched

	// replace on an empty stack is a plain push (nothing to replace)
	ScreenStack empty;
	empty.replace("only");
	CHECK(empty.size() == 1u);
	CHECK(empty.current() == "only");
}

TEST_CASE("ScreenStack: contains and clear", "[unit][gui][screens]")
{
	ScreenStack stack;
	stack.push("menu");
	stack.push("play");
	CHECK(stack.contains("menu"));
	CHECK(stack.contains("play"));
	CHECK_FALSE(stack.contains("pause"));

	stack.clear();
	CHECK(stack.empty());
	CHECK_FALSE(stack.contains("menu"));
}
