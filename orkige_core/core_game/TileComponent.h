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
	//! @brief a thin marker on a level's movable tile roots. It carries an
	//! open-edges bitmask (bit 0 top, 1 bottom, 2 left, 3 right) that is future
	//! home for slide-legality validation - the game does not yet read it (the
	//! tiles' scene positions and their prefab-suppressed wall children already
	//! encode the layout) - and, for a tile painted straight from a texture or
	//! .oshape (no prefab), the stable id of that source asset (sourceAssetId).
	//! Its PRESENCE (and the "tile" tag the generator sets) is what game.lua
	//! discovers tile roots by, and it gives the editor's 2D authoring a
	//! component to stamp when a tile becomes a tile: a PREFAB tile leaves
	//! sourceAssetId empty (its prefabRef is its identity), a BARE-asset tile
	//! records the painted asset there so a re-paint of the same asset is a
	//! no-op and the grid-paint tool knows what a cell holds.
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
		//! number of edges a tile has (top/bottom/left/right)
		static const int EDGE_COUNT = 4;
		//! @brief the conventional prefab-local id of the wall child on each
		//! edge, in top/bottom/left/right order (EDGE_WALL_LOCAL_IDS[0] =
		//! "WallTop", ...). This is the tile-prefab contract the 2D grid
		//! authoring reads: an OPEN edge is expressed as that wall child being
		//! suppressed on the instance, and the bit set in openEdges. Kept here
		//! as the single source so the mapping does not drift between the
		//! authoring tool, the runtime and the asset generators.
		static const char * const EDGE_WALL_LOCAL_IDS[EDGE_COUNT];
		//! the OpenEdge bit for an edge index (0..EDGE_COUNT-1) = 1 << index
		static int edgeBitForIndex(int index);
		//--- Variables ---------------------------------------
	private:
		int	mOpenEdges;		//!< bitmask of OpenEdge (0 = fully walled)
		//! stable id of the source texture/.oshape for a bare-asset tile (a tile
		//! painted directly from an asset, no prefab); empty for a prefab tile
		String	mSourceAssetId;
		//--- Methods -----------------------------------------
	public:
		TileComponent();
		virtual ~TileComponent();

		int getOpenEdges() const { return mOpenEdges; }
		void setOpenEdges(int mask) { mOpenEdges = mask; }
		//! is the given edge (a single OpenEdge bit) open
		bool isEdgeOpen(int edgeBit) const { return (mOpenEdges & edgeBit) != 0; }
		//! @see TileComponent::mSourceAssetId
		String const & getSourceAssetId() const { return mSourceAssetId; }
		void setSourceAssetId(String const & id) { mSourceAssetId = id; }

		//--- SERIALIZATION ---
		virtual void save(optr<IArchive> const & ar);
		virtual void load(optr<IArchive> const & ar);
	protected:
	private:
	};
}

#endif //__TileComponent_h__9_7_2026__12_00_00__
