/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	TileComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/TileComponent.h"

namespace Orkige
{
	//---------------------------------------------------------
	TileComponent::TileComponent()
		: mOpenEdges(0)
	{
	}
	//---------------------------------------------------------
	TileComponent::~TileComponent()
	{
	}
	//---------------------------------------------------------
	void TileComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		ar << this->mOpenEdges;
	}
	//---------------------------------------------------------
	void TileComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		ar >> this->mOpenEdges;
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(TileComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(getOpenEdges)
		OFUNC(setOpenEdges)
		OFUNC(isEdgeOpen)
	OOBJECT_END
	//---------------------------------------------------------
}
