/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	TileComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __TileComponent_h__9_7_2026__12_00_00__
#define __TileComponent_h__9_7_2026__12_00_00__

#include "core_game/GameObjectComponent.h"

namespace Orkige
{
	//! @brief a thin marker on a level's movable tile roots. v1 carries only an
	//! open-edges bitmask (bit 0 top, 1 bottom, 2 left, 3 right) that is future
	//! home for slide-legality validation - the game does not yet read it (the
	//! tiles' scene positions and their prefab-suppressed wall children already
	//! encode the layout). Its PRESENCE (and the "tile" tag the generator sets)
	//! is what game.lua discovers tile roots by, and it gives the editor's 2D
	//! authoring a component to stamp when a tile becomes a tile.
	class ORKIGE_CORE_DLL TileComponent : public GameObjectComponent
	{
		OOBJECT(TileComponent, GameObjectComponent)
		//--- Types -------------------------------------------
	public:
		//! open-edge bits (a suppressed prefab wall on that edge)
		enum OpenEdge
		{
			EDGE_TOP	= 1 << 0,
			EDGE_BOTTOM	= 1 << 1,
			EDGE_LEFT	= 1 << 2,
			EDGE_RIGHT	= 1 << 3
		};
		//--- Variables ---------------------------------------
	private:
		int	mOpenEdges;		//!< bitmask of OpenEdge (0 = fully walled)
		//--- Methods -----------------------------------------
	public:
		TileComponent();
		virtual ~TileComponent();

		int getOpenEdges() const { return mOpenEdges; }
		void setOpenEdges(int mask) { mOpenEdges = mask; }
		//! is the given edge (a single OpenEdge bit) open
		bool isEdgeOpen(int edgeBit) const { return (mOpenEdges & edgeBit) != 0; }

		//--- SERIALIZATION ---
		virtual void save(optr<IArchive> const & ar);
		virtual void load(optr<IArchive> const & ar);
	protected:
	private:
	};
}

#endif //__TileComponent_h__9_7_2026__12_00_00__
