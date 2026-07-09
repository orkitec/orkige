/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	LevelComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __LevelComponent_h__9_7_2026__12_00_00__
#define __LevelComponent_h__9_7_2026__12_00_00__

#include "core_game/GameObjectComponent.h"
#include "core_game/LevelGrid.h"

namespace Orkige
{
	//! @brief the level-metadata component: attached to a scene's single
	//! "Level" root object, it carries the tile grid GEOMETRY (cols, rows, tile
	//! size, origin, the goal slot and the par slide count) and NOTHING about
	//! which tile sits where - that layout is the scene's own arrangement of
	//! tile prefab instances, which game.lua reads back by snapping each tile
	//! root through the grid (this component's slotForPosition). This is what
	//! killed the triplicated slot->tile Lua table: the grid lives once, here,
	//! and the occupancy is DERIVED from the scene at load.
	//! @remarks Data-only (no scene node, no render cost), so it lives in the
	//! renderer-independent core and round-trips headlessly. The grid math is
	//! delegated to the pure LevelGrid (core_game/LevelGrid.h) and re-exposed to
	//! Lua here so game.lua and the LevelGrid unit tests share one implementation.
	class ORKIGE_CORE_DLL LevelComponent : public GameObjectComponent
	{
		OOBJECT(LevelComponent, GameObjectComponent)
		//--- Variables ---------------------------------------
	private:
		int		mCols;			//!< grid columns
		int		mRows;			//!< grid rows
		float	mTileSize;		//!< slot edge length (world units)
		float	mOriginX;		//!< world X of cell (0,0)'s center
		float	mOriginY;		//!< world Y of cell (0,0)'s center
		int		mGoalSlot;		//!< the slot the goal tile occupies at authoring (metadata)
		int		mPar;			//!< target slide count for a 3-star finish
		//--- Methods -----------------------------------------
	public:
		LevelComponent();
		virtual ~LevelComponent();

		//! set the whole grid geometry at once (editor/generator authoring)
		void setGeometry(int cols, int rows, float tileSize,
			float originX, float originY);

		int getCols() const { return mCols; }
		int getRows() const { return mRows; }
		float getTileSize() const { return mTileSize; }
		float getOriginX() const { return mOriginX; }
		float getOriginY() const { return mOriginY; }
		int getGoalSlot() const { return mGoalSlot; }
		void setGoalSlot(int slot) { mGoalSlot = slot; }
		int getPar() const { return mPar; }
		void setPar(int par) { mPar = par; }

		//--- reflected property setters (task #94 P2): set one geometry field,
		//! keep the rest (the drive round-trips each grid field by name)
		void setColsValue(int cols) { this->setGeometry(cols, mRows, mTileSize, mOriginX, mOriginY); }
		void setRowsValue(int rows) { this->setGeometry(mCols, rows, mTileSize, mOriginX, mOriginY); }
		void setTileSizeValue(float tileSize) { this->setGeometry(mCols, mRows, tileSize, mOriginX, mOriginY); }
		void setOriginXValue(float originX) { this->setGeometry(mCols, mRows, mTileSize, originX, mOriginY); }
		void setOriginYValue(float originY) { this->setGeometry(mCols, mRows, mTileSize, mOriginX, originY); }

		//! the pure grid this component's geometry describes
		LevelGrid getGrid() const;

		//--- LevelGrid math re-exposed for Lua (game.lua) ---
		int getSlotCount() const;						//!< cols * rows
		int slotForPosition(float x, float y) const;	//!< world pos -> slot (-1 = off grid)
		int slotForCell(int col, int row) const;		//!< cell -> slot (-1 = off grid)
		int slotCol(int slot) const;					//!< slot -> column (-1 = invalid)
		int slotRow(int slot) const;					//!< slot -> row (-1 = invalid)
		float slotCenterX(int slot) const;				//!< slot -> world center X
		float slotCenterY(int slot) const;				//!< slot -> world center Y
		//! star rating for finishing this level in `moves` slides (@see LevelGrid)
		int starsForMoves(int moves) const;

		//--- SERIALIZATION ---
		virtual void save(optr<IArchive> const & ar);
		virtual void load(optr<IArchive> const & ar);
	protected:
	private:
	};
}

#endif //__LevelComponent_h__9_7_2026__12_00_00__
