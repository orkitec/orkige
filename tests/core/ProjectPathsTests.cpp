/**************************************************************
	created:	2026/07/24 at 20:30
	filename: 	ProjectPathsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the reserved-output-dirs policy
	(core_project/ProjectPaths): the ONE place every project-tree
	walker asks whether a directory is generated OUTPUT / editor-
	private (builds, .orkige, native build trees, VCS) and must never
	be scanned, indexed, watched or browsed. The wired proof that no
	walker touches them is AssetDatabaseTests + the editor selfchecks.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_project/ProjectPaths.h>

using namespace Orkige;

TEST_CASE("ProjectPaths: builds / .orkige / VCS are reserved by name",
	"[projectpaths]")
{
	CHECK(ProjectPaths::isReservedOutputDir("builds", "anything"));
	CHECK(ProjectPaths::isReservedOutputDir(".orkige", "anything"));
	CHECK(ProjectPaths::isReservedOutputDir(".git", "anything"));
	CHECK(ProjectPaths::isReservedOutputDir(".svn", "anything"));
	// ordinary project dirs are NOT reserved
	CHECK_FALSE(ProjectPaths::isReservedOutputDir("assets", ""));
	CHECK_FALSE(ProjectPaths::isReservedOutputDir("scripts", ""));
	CHECK_FALSE(ProjectPaths::isReservedOutputDir("scenes", ""));
	CHECK_FALSE(ProjectPaths::isReservedOutputDir("native", ""));
	CHECK_FALSE(ProjectPaths::isReservedOutputDir("loc", ""));
}

TEST_CASE("ProjectPaths: native/build* is reserved only under native",
	"[projectpaths]")
{
	CHECK(ProjectPaths::isReservedOutputDir("build-next", "native"));
	CHECK(ProjectPaths::isReservedOutputDir("build-export-classic", "native"));
	CHECK(ProjectPaths::isReservedOutputDir("build", "native"));
	// a 'build*' dir NOT under native is ordinary (the source stays browsable)
	CHECK_FALSE(ProjectPaths::isReservedOutputDir("build-next", "assets"));
	CHECK_FALSE(ProjectPaths::isReservedOutputDir("buildings", "assets"));
}

TEST_CASE("ProjectPaths: isUnderReservedOutput walks the whole rel path",
	"[projectpaths]")
{
	CHECK(ProjectPaths::isUnderReservedOutput("builds/macos/assets/foo.png"));
	CHECK(ProjectPaths::isUnderReservedOutput(".orkige/breadcrumbs.jsonl"));
	CHECK(ProjectPaths::isUnderReservedOutput(
		"native/build-next/CMakeFiles/x.o"));
	// ordinary project-relative paths pass through
	CHECK_FALSE(ProjectPaths::isUnderReservedOutput("assets/textures/ball.png"));
	CHECK_FALSE(ProjectPaths::isUnderReservedOutput("scripts/player.lua"));
	CHECK_FALSE(ProjectPaths::isUnderReservedOutput("scenes/level1.oscene"));
	CHECK_FALSE(ProjectPaths::isUnderReservedOutput(
		"native/src/Game.cpp"));	// native SOURCE stays included
}
