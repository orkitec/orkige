/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	LevelGrid.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __LevelGrid_h__9_7_2026__12_00_00__
#define __LevelGrid_h__9_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"

#include <vector>

namespace Orkige
{
	//! @brief pure, headless-testable grid geometry for the tile-slide levels
	//! (Continuity x Rolando "roller" tier). A level is a cols x rows grid of
	//! square slots of edge tileSize, cell (0,0) centered at (originX, originY);
	//! slot index = row * cols + col.
	//! @remarks This is the SINGLE source of the slot<->world mapping the game
	//! scripts used to triplicate as a hand-kept Lua table: LevelComponent
	//! (core_game/LevelComponent.h) serializes the geometry, wraps a LevelGrid
	//! and exposes these operations to Lua, and game.lua DERIVES the live slot
	//! occupancy by snapping each tile root's world position through
	//! slotForPosition. No renderer, no GameObject - just the math, so the unit
	//! tests exercise the snapping/empty-slot/star rules directly.
	class ORKIGE_CORE_DLL LevelGrid
	{
		//--- Variables ---------------------------------------
	private:
		int		mCols;			//!< columns (>= 1)
		int		mRows;			//!< rows (>= 1)
		float	mTileSize;		//!< slot edge length in world units (> 0)
		float	mOriginX;		//!< world X of cell (0,0)'s center
		float	mOriginY;		//!< world Y of cell (0,0)'s center
		//--- Methods -----------------------------------------
	public:
		//! a degenerate 1x1 unit grid at the origin (safe default)
		LevelGrid();
		//! @param cols columns (clamped to >= 1)
		//! @param rows rows (clamped to >= 1)
		//! @param tileSize slot edge length (clamped to a tiny positive)
		//! @param originX world X of cell (0,0)'s center
		//! @param originY world Y of cell (0,0)'s center
		LevelGrid(int cols, int rows, float tileSize, float originX, float originY);

		int getCols() const { return mCols; }
		int getRows() const { return mRows; }
		float getTileSize() const { return mTileSize; }
		float getOriginX() const { return mOriginX; }
		float getOriginY() const { return mOriginY; }
		//! number of slots (cols * rows)
		int getSlotCount() const { return mCols * mRows; }

		//! is slot a valid index (0 .. slotCount-1)
		bool isValidSlot(int slot) const;
		//! slot index for a cell, or -1 when the cell is outside the grid
		int slotForCell(int col, int row) const;
		//! the cell column of a slot, or -1 when the slot is invalid
		int slotCol(int slot) const;
		//! the cell row of a slot, or -1 when the slot is invalid
		int slotRow(int slot) const;
		//! world X of a slot's center (0 for an invalid slot)
		float slotCenterX(int slot) const;
		//! world Y of a slot's center (0 for an invalid slot)
		float slotCenterY(int slot) const;

		//! @brief snap a world position to the slot whose center it is nearest,
		//! within a half-cell tolerance in BOTH axes; -1 when the position lies
		//! outside the grid. Tile roots sit at slot centers, so this recovers a
		//! tile's slot from its (possibly float-drifted) scene position.
		int slotForPosition(float x, float y) const;

		//! @brief the first slot (lowest index) NOT marked occupied; -1 when
		//! every slot is occupied. occupied is indexed by slot; entries past its
		//! size (or a short vector) count as free. The tile-slide puzzle has
		//! exactly one hole - this derives it from the occupied tiles.
		int firstEmptySlot(std::vector<bool> const & occupied) const;

		//! @brief the star rating for finishing a level in `moves` slides given
		//! its `par`: 3 stars at or under par, 2 up to twice par, else 1 (always
		//! >= 1). par <= 0 means "unscored" and always earns 3.
		static int starsForMoves(int moves, int par);
	private:
	};
}

#endif //__LevelGrid_h__9_7_2026__12_00_00__
