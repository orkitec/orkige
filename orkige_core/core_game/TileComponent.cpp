/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	TileComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_game/TileComponent.h"
#include "core_game/SceneSerializer.h"

namespace Orkige
{
	//---------------------------------------------------------
	// top/bottom/left/right, matching the OpenEdge bit order (1<<index)
	const char * const TileComponent::EDGE_WALL_LOCAL_IDS[TileComponent::EDGE_COUNT] =
	{
		"WallTop", "WallBottom", "WallLeft", "WallRight"
	};
	//---------------------------------------------------------
	int TileComponent::edgeBitForIndex(int index)
	{
		if(index < 0 || index >= EDGE_COUNT)
		{
			return 0;
		}
		return 1 << index;
	}
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
		// reflection-driven NAMED serialization
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void TileComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(TileComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(getOpenEdges)
		OFUNC(setOpenEdges)
		OFUNC(isEdgeOpen)
		// reflected schema: the open-edges bitmask
		OPROPERTY("openEdges", Orkige::PropertyKind::Int, getOpenEdges, setOpenEdges, Orkige::PROP_NONE)
	OOBJECT_END
	//---------------------------------------------------------
}
