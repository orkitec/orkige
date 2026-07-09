/**************************************************************
	created:	2026/07/09 at 14:00
	filename: 	LevelGridTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The pure tile-grid math the roller levels derive their slot map from
	(core_game/LevelGrid) plus the LevelComponent serialization round-trip -
	all headless, no renderer.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_game/LevelGrid.h>
#include <core_game/LevelComponent.h>
#include <core_game/SceneSerializer.h>
#include <core_serialization/XMLArchive.h>

#include <filesystem>
#include <vector>

TEST_CASE("LevelGrid maps slots to cells and world centers", "[level]")
{
	// the roller 2x2 grid: tile size 6, cell (0,0) centered at (-3, -3) - so
	// the four slot centers are (-3,-3) (3,-3) (-3,3) (3,3)
	Orkige::LevelGrid grid(2, 2, 6.0f, -3.0f, -3.0f);

	REQUIRE(grid.getSlotCount() == 4);
	REQUIRE(grid.slotForCell(0, 0) == 0);
	REQUIRE(grid.slotForCell(1, 0) == 1);
	REQUIRE(grid.slotForCell(0, 1) == 2);
	REQUIRE(grid.slotForCell(1, 1) == 3);
	// out-of-range cells have no slot
	REQUIRE(grid.slotForCell(2, 0) == -1);
	REQUIRE(grid.slotForCell(-1, 0) == -1);

	REQUIRE(grid.slotCol(3) == 1);
	REQUIRE(grid.slotRow(3) == 1);
	REQUIRE(grid.slotCol(2) == 0);
	REQUIRE(grid.slotRow(2) == 1);
	// invalid slots answer -1
	REQUIRE(grid.slotCol(4) == -1);
	REQUIRE(grid.slotRow(-1) == -1);

	REQUIRE(grid.slotCenterX(0) == -3.0f);
	REQUIRE(grid.slotCenterY(0) == -3.0f);
	REQUIRE(grid.slotCenterX(3) == 3.0f);
	REQUIRE(grid.slotCenterY(3) == 3.0f);
}

TEST_CASE("LevelGrid snaps world positions to slots within a half-cell", "[level]")
{
	Orkige::LevelGrid grid(2, 2, 6.0f, -3.0f, -3.0f);

	// a tile root exactly on a center
	REQUIRE(grid.slotForPosition(-3.0f, -3.0f) == 0);
	REQUIRE(grid.slotForPosition(3.0f, 3.0f) == 3);
	// float drift within tolerance still snaps
	REQUIRE(grid.slotForPosition(-2.98f, -3.02f) == 0);
	REQUIRE(grid.slotForPosition(3.1f, -2.9f) == 1);
	// off the grid entirely -> no slot
	REQUIRE(grid.slotForPosition(20.0f, 0.0f) == -1);
	REQUIRE(grid.slotForPosition(-3.0f, 30.0f) == -1);
}

TEST_CASE("LevelGrid derives the empty slot from the occupied set", "[level]")
{
	Orkige::LevelGrid grid(2, 2, 6.0f, -3.0f, -3.0f);

	// slots 0, 2, 3 occupied -> slot 1 is the hole (the roller level 1 layout)
	std::vector<bool> occupied(4, false);
	occupied[0] = true;
	occupied[2] = true;
	occupied[3] = true;
	REQUIRE(grid.firstEmptySlot(occupied) == 1);

	// everything occupied -> no hole
	std::vector<bool> full(4, true);
	REQUIRE(grid.firstEmptySlot(full) == -1);

	// a short vector counts the missing tail as free (slot 3 here)
	std::vector<bool> partial;
	partial.push_back(true);
	partial.push_back(true);
	partial.push_back(true);
	REQUIRE(grid.firstEmptySlot(partial) == 3);
}

TEST_CASE("LevelGrid star rating compares moves against par", "[level]")
{
	// par 3: 3 stars at or under par, 2 up to twice par, else 1
	REQUIRE(Orkige::LevelGrid::starsForMoves(2, 3) == 3);
	REQUIRE(Orkige::LevelGrid::starsForMoves(3, 3) == 3);
	REQUIRE(Orkige::LevelGrid::starsForMoves(4, 3) == 2);
	REQUIRE(Orkige::LevelGrid::starsForMoves(6, 3) == 2);
	REQUIRE(Orkige::LevelGrid::starsForMoves(7, 3) == 1);
	REQUIRE(Orkige::LevelGrid::starsForMoves(100, 3) == 1);
	// par 0 is unscored - always 3 stars, never a penalty
	REQUIRE(Orkige::LevelGrid::starsForMoves(0, 0) == 3);
	REQUIRE(Orkige::LevelGrid::starsForMoves(50, 0) == 3);
}

TEST_CASE("LevelComponent round-trips its grid geometry through a scene", "[level]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	Orkige::GameObjectManager & manager = env.gameObjectManager;
	manager.clear();
	const Orkige::String path =
		(std::filesystem::temp_directory_path() / "orkige_level_test.oscene").string();
	std::filesystem::remove(path);

	{
		optr<Orkige::GameObject> level =
			manager.createGameObject("Level").lock();
		REQUIRE(level);
		REQUIRE(level->addComponent<Orkige::LevelComponent>());
		Orkige::LevelComponent* component =
			level->getComponentPtr<Orkige::LevelComponent>();
		component->setGeometry(3, 2, 6.0f, -6.0f, -3.0f);
		component->setGoalSlot(4);
		component->setPar(5);
	}

	REQUIRE(Orkige::SceneSerializer::saveScene(path, manager));
	manager.clear();
	REQUIRE(manager.getGameObjects().empty());
	REQUIRE(Orkige::SceneSerializer::loadScene(path, manager));

	optr<Orkige::GameObject> level = manager.getGameObject("Level").lock();
	REQUIRE(level);
	REQUIRE(level->hasComponent<Orkige::LevelComponent>());
	Orkige::LevelComponent* component =
		level->getComponentPtr<Orkige::LevelComponent>();
	REQUIRE(component->getCols() == 3);
	REQUIRE(component->getRows() == 2);
	REQUIRE(component->getTileSize() == 6.0f);
	REQUIRE(component->getOriginX() == -6.0f);
	REQUIRE(component->getOriginY() == -3.0f);
	REQUIRE(component->getGoalSlot() == 4);
	REQUIRE(component->getPar() == 5);
	// and the grid math is live off the deserialized geometry
	REQUIRE(component->getSlotCount() == 6);
	REQUIRE(component->slotForPosition(-6.0f, -3.0f) == 0);
	REQUIRE(component->starsForMoves(5) == 3);

	std::error_code ignored;
	std::filesystem::remove(path, ignored);
	manager.clear();
}
