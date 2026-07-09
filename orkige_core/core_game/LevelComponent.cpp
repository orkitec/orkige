/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	LevelComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/LevelComponent.h"
#include "core_game/SceneSerializer.h"

namespace Orkige
{
	//---------------------------------------------------------
	LevelComponent::LevelComponent()
		: mCols(1), mRows(1), mTileSize(1.0f)
		, mOriginX(0.0f), mOriginY(0.0f), mGoalSlot(0), mPar(0)
	{
	}
	//---------------------------------------------------------
	LevelComponent::~LevelComponent()
	{
	}
	//---------------------------------------------------------
	void LevelComponent::setGeometry(int cols, int rows, float tileSize,
		float originX, float originY)
	{
		this->mCols = cols;
		this->mRows = rows;
		this->mTileSize = tileSize;
		this->mOriginX = originX;
		this->mOriginY = originY;
	}
	//---------------------------------------------------------
	LevelGrid LevelComponent::getGrid() const
	{
		return LevelGrid(mCols, mRows, mTileSize, mOriginX, mOriginY);
	}
	//---------------------------------------------------------
	int LevelComponent::getSlotCount() const
	{
		return this->getGrid().getSlotCount();
	}
	//---------------------------------------------------------
	int LevelComponent::slotForPosition(float x, float y) const
	{
		return this->getGrid().slotForPosition(x, y);
	}
	//---------------------------------------------------------
	int LevelComponent::slotForCell(int col, int row) const
	{
		return this->getGrid().slotForCell(col, row);
	}
	//---------------------------------------------------------
	int LevelComponent::slotCol(int slot) const
	{
		return this->getGrid().slotCol(slot);
	}
	//---------------------------------------------------------
	int LevelComponent::slotRow(int slot) const
	{
		return this->getGrid().slotRow(slot);
	}
	//---------------------------------------------------------
	float LevelComponent::slotCenterX(int slot) const
	{
		return this->getGrid().slotCenterX(slot);
	}
	//---------------------------------------------------------
	float LevelComponent::slotCenterY(int slot) const
	{
		return this->getGrid().slotCenterY(slot);
	}
	//---------------------------------------------------------
	int LevelComponent::starsForMoves(int moves) const
	{
		return LevelGrid::starsForMoves(moves, mPar);
	}
	//---------------------------------------------------------
	void LevelComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void LevelComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(LevelComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(getCols)
		OFUNC(getRows)
		OFUNC(getTileSize)
		OFUNC(getOriginX)
		OFUNC(getOriginY)
		OFUNC(getGoalSlot)
		OFUNC(getPar)
		OFUNC(getSlotCount)
		OFUNC(slotForPosition)
		OFUNC(slotForCell)
		OFUNC(slotCol)
		OFUNC(slotRow)
		OFUNC(slotCenterX)
		OFUNC(slotCenterY)
		OFUNC(starsForMoves)
		// reflected grid-geometry schema
		OPROPERTY("cols", Orkige::PropertyKind::Int, getCols, setColsValue, Orkige::PROP_NONE)
		OPROPERTY("rows", Orkige::PropertyKind::Int, getRows, setRowsValue, Orkige::PROP_NONE)
		OPROPERTY("tileSize", Orkige::PropertyKind::Float, getTileSize, setTileSizeValue, Orkige::PROP_NONE)
		OPROPERTY("originX", Orkige::PropertyKind::Float, getOriginX, setOriginXValue, Orkige::PROP_NONE)
		OPROPERTY("originY", Orkige::PropertyKind::Float, getOriginY, setOriginYValue, Orkige::PROP_NONE)
		OPROPERTY("goalSlot", Orkige::PropertyKind::Int, getGoalSlot, setGoalSlot, Orkige::PROP_NONE)
		OPROPERTY("par", Orkige::PropertyKind::Int, getPar, setPar, Orkige::PROP_NONE)
	OOBJECT_END
	//---------------------------------------------------------
}
