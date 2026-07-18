/********************************************************************
	created:	Friday 2026/07/17 at 10:00
	filename: 	VectorMeshRuns.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorMeshRuns_h__17_7_2026__10_00_00__
#define __VectorMeshRuns_h__17_7_2026__10_00_00__

//! @file VectorMeshRuns.h
//! @brief turn a tessellated vector mesh (with its per-texture draw runs)
//! into facade VectorMesh uploads - the shared converter both the shape and
//! the animation component use
//! @remarks The tessellator emits ONE vertex/index array plus per-texture
//! runs (@see VectorTessellator::Mesh::runs). This helper realizes them as
//! facade sections: an ALL-FLAT mesh goes through the plain
//! VectorMesh::setMesh / updateVertices path (byte-identical to the
//! pre-texture pipeline - the toggle-identity discipline), a mesh with
//! textured runs splits into one Section per run (one draw per texture -
//! the sprite-run batching discipline), with the run-local index rebase
//! cached per topology so the per-frame dynamic update stays a plain
//! per-section vertex rewrite.

#include "engine_render/VectorMesh.h"
#include <core_util/VectorTessellator.h>

#include <vector>

namespace Orkige
{
	//! @brief the tessellated-mesh -> facade-VectorMesh upload converter
	//! (per-component instance state: the cached run-local index rebase)
	class ORKIGE_ENGINE_DLL VectorMeshRuns
	{
		//--- Variables ---------------------------------------------
	protected:
		//! per-run indices rebased to the run's own vertex span (rebuilt by
		//! push, reused by every dynamic update - topology-stable)
		std::vector<std::vector<unsigned int> >	mRebased;
		//! the section descriptors handed to setMeshSections (parallel to the
		//! mesh's runs; kept so update() knows each run's vertex span)
		std::vector<VectorTessellator::Run>		mRuns;
		bool	mSectioned;	//!< the last push used the sections path
	private:
		//--- Methods -----------------------------------------------
	public:
		VectorMeshRuns();

		//! @brief convert the POD tessellator mesh into facade vertices,
		//! multiplying the instance tint into every colour and carrying the
		//! per-vertex UVs along (both components' one conversion loop)
		static void buildVertices(VectorTessellator::Mesh const & built,
			Color const & tint, std::vector<VectorMesh::Vertex> & out);

		//! @brief push the whole mesh: an all-flat build (zero or one
		//! untextured run) takes the plain setMesh identity path; anything
		//! textured splits into one Section per run. Establishes the topology
		//! the later update() calls rewrite.
		void push(VectorMesh & mesh, VectorTessellator::Mesh const & built,
			std::vector<VectorMesh::Vertex> const & vertices);

		//! @brief per-frame DYNAMIC vertex rewrite of the topology the last
		//! push established (positions/colours/UVs move; counts must match -
		//! the caller re-pushes on a topology change). vertices is the SAME
		//! whole-mesh array push consumed; each run uploads its own span.
		void update(VectorMesh & mesh,
			std::vector<VectorMesh::Vertex> const & vertices);

		//! runs pushed last time (0 before any push; 1 for an all-flat mesh)
		std::size_t runCount() const { return this->mRuns.size(); }
		//! how many of the pushed runs bind a texture
		std::size_t texturedRunCount() const;
		//! drop the cached topology (the owning component unloaded its mesh)
		void clear();
	protected:
	private:
	};
}

#endif //__VectorMeshRuns_h__17_7_2026__10_00_00__
