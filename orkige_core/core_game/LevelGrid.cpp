/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	LevelGrid.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/LevelGrid.h"

#include <cmath>

namespace Orkige
{
	//---------------------------------------------------------
	LevelGrid::LevelGrid()
		: mCols(1), mRows(1), mTileSize(1.0f), mOriginX(0.0f), mOriginY(0.0f)
	{
	}
	//---------------------------------------------------------
	LevelGrid::LevelGrid(int cols, int rows, float tileSize, float originX, float originY)
		: mCols(cols < 1 ? 1 : cols)
		, mRows(rows < 1 ? 1 : rows)
		, mTileSize(tileSize > 1e-4f ? tileSize : 1e-4f)
		, mOriginX(originX)
		, mOriginY(originY)
	{
	}
	//---------------------------------------------------------
	bool LevelGrid::isValidSlot(int slot) const
	{
		return slot >= 0 && slot < this->getSlotCount();
	}
	//---------------------------------------------------------
	int LevelGrid::slotForCell(int col, int row) const
	{
		if(col < 0 || col >= mCols || row < 0 || row >= mRows)
		{
			return -1;
		}
		return row * mCols + col;
	}
	//---------------------------------------------------------
	int LevelGrid::slotCol(int slot) const
	{
		if(!this->isValidSlot(slot))
		{
			return -1;
		}
		return slot % mCols;
	}
	//---------------------------------------------------------
	int LevelGrid::slotRow(int slot) const
	{
		if(!this->isValidSlot(slot))
		{
			return -1;
		}
		return slot / mCols;
	}
	//---------------------------------------------------------
	float LevelGrid::slotCenterX(int slot) const
	{
		const int col = this->slotCol(slot);
		if(col < 0)
		{
			return 0.0f;
		}
		return mOriginX + static_cast<float>(col) * mTileSize;
	}
	//---------------------------------------------------------
	float LevelGrid::slotCenterY(int slot) const
	{
		const int row = this->slotRow(slot);
		if(row < 0)
		{
			return 0.0f;
		}
		return mOriginY + static_cast<float>(row) * mTileSize;
	}
	//---------------------------------------------------------
	int LevelGrid::slotForPosition(float x, float y) const
	{
		// round to the nearest cell, then reject positions that fall more than a
		// half cell from that center (i.e. outside the grid extent)
		const int col = static_cast<int>(std::floor((x - mOriginX) / mTileSize + 0.5f));
		const int row = static_cast<int>(std::floor((y - mOriginY) / mTileSize + 0.5f));
		const int slot = this->slotForCell(col, row);
		if(slot < 0)
		{
			return -1;
		}
		const float half = mTileSize * 0.5f + 1e-3f;
		if(std::fabs(x - this->slotCenterX(slot)) > half ||
			std::fabs(y - this->slotCenterY(slot)) > half)
		{
			return -1;
		}
		return slot;
	}
	//---------------------------------------------------------
	int LevelGrid::firstEmptySlot(std::vector<bool> const & occupied) const
	{
		const int count = this->getSlotCount();
		for(int slot = 0; slot < count; ++slot)
		{
			const bool isOccupied = (static_cast<size_t>(slot) < occupied.size())
				? occupied[static_cast<size_t>(slot)] : false;
			if(!isOccupied)
			{
				return slot;
			}
		}
		return -1;
	}
	//---------------------------------------------------------
	int LevelGrid::starsForMoves(int moves, int par)
	{
		if(par <= 0)
		{
			return 3;	// unscored level - never penalize
		}
		if(moves <= par)
		{
			return 3;
		}
		if(moves <= par * 2)
		{
			return 2;
		}
		return 1;
	}
	//---------------------------------------------------------
}
