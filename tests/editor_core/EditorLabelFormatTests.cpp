/**************************************************************
	created:	2026/07/24 at 10:00
	filename: 	EditorLabelFormatTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the Inspector display-name prettifiers
	(tools/editor/EditorLabelFormat.{h,cpp}): camelCase -> spaced
	Title Case for property labels, and the "Component"-suffix strip
	+ prettify for component titles. Pure transforms - the schema
	keys are unaffected.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <EditorLabelFormat.h>

using Orkige::prettifyPropertyLabel;
using Orkige::prettifyComponentTitle;

TEST_CASE("property labels camelCase -> spaced Title Case", "[unit]")
{
	CHECK(prettifyPropertyLabel("position") == "Position");
	CHECK(prettifyPropertyLabel("castShadows") == "Cast Shadows");
	CHECK(prettifyPropertyLabel("x") == "X");
	CHECK(prettifyPropertyLabel("designWidth") == "Design Width");
	CHECK(prettifyPropertyLabel("zOrder") == "Z Order");
}

TEST_CASE("property labels: acronym runs and digits", "[unit]")
{
	// an acronym run splits only at its end (before a trailing word)
	CHECK(prettifyPropertyLabel("innerAngle") == "Inner Angle");
	CHECK(prettifyPropertyLabel("orthoSize") == "Ortho Size");
	// a digit after letters starts a new token
	CHECK(prettifyPropertyLabel("uv0") == "Uv 0");
	// already-clean single words just get their initial capitalised
	CHECK(prettifyPropertyLabel("opacity") == "Opacity");
}

TEST_CASE("property labels: edge inputs", "[unit]")
{
	CHECK(prettifyPropertyLabel("") == "");
	// an all-caps token stays intact (no lower-case follower to split on)
	CHECK(prettifyPropertyLabel("URL") == "URL");
}

TEST_CASE("component titles strip the Component suffix", "[unit]")
{
	CHECK(prettifyComponentTitle("ModelComponent") == "Model");
	CHECK(prettifyComponentTitle("TransformComponent") == "Transform");
	CHECK(prettifyComponentTitle("RigidBodyComponent") == "Rigid Body");
	CHECK(prettifyComponentTitle("VectorAnimationComponent") ==
		"Vector Animation");
	CHECK(prettifyComponentTitle("ScriptComponent") == "Script");
}

TEST_CASE("component titles without the suffix are prettified as-is", "[unit]")
{
	CHECK(prettifyComponentTitle("Camera") == "Camera");
	// a type literally named just "Component" keeps its name (nothing left)
	CHECK(prettifyComponentTitle("Component") == "Component");
}
