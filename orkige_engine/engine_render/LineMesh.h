/********************************************************************
	created:	Friday 2026/07/25 at 12:00
	filename: 	LineMesh.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __LineMesh_h__25_7_2026__12_00_00__
#define __LineMesh_h__25_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"

#include <cstddef>

namespace Orkige
{
	//! @brief a world/local-space, vertex-coloured 3D LINE renderable drawn in
	//! ONE draw call - a connected polyline (strip) or an independent-segment
	//! list, dynamically updatable in place
	//! @remarks The LINE sibling of VectorMesh: where VectorMesh is a 2D
	//! alpha-blended triangle mesh (the flat-shape building block, hardwired
	//! into the sprite 2D tier), a LineMesh is 3D line geometry that lives in
	//! the lit scene pass alongside meshes - it depth-tests against the world
	//! (or, as an overlay, ignores depth), casts no shadow and is never picked.
	//! Colour is per vertex (the shared unlit vertex-colour recipe, the cube /
	//! grid gizmo look). LINE WIDTH is intentionally absent: hardware line width
	//! beyond one pixel is dead on the core render APIs, so lines are hairlines;
	//! a thickness upgrade is a future quad-expansion pass, NOT a facade knob
	//! today (an honest v1 scope).
	//!
	//! The refill contract mirrors VectorMesh: setLines copies a whole CPU
	//! vertex array each call (establishing the topology + count); a moving
	//! polyline then calls updateVertices per change - the DYNAMIC fast path
	//! that rewrites the existing vertex buffer IN PLACE
	//! (ManualObject::beginUpdate) without re-specifying the topology, so a
	//! same-count update never reallocates. The next backend forbids mapping a
	//! buffer twice per frame, so a consumer that both rebuilds and updates in
	//! one frame must defer the first update one tick (the VectorMesh soft-body
	//! precedent - the discipline lives in the consumer, @see LineComponent /
	//! DebugDraw).
	//!
	//! Backend mapping (whole class): classic = Ogre::ManualObject
	//! (OT_LINE_STRIP / OT_LINE_LIST) + the shared unlit vertex-colour
	//! "VertexColour" material (depth-tested) or "VertexColourOverlay"
	//! (depth-ignoring); next = v2 Ogre::ManualObject (SCENE_DYNAMIC) + the
	//! matching HlmsUnlit vertex-colour datablock, in the default 3D item queue.
	class ORKIGE_ENGINE_DLL LineMesh
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief line connectivity
		enum Topology
		{
			TOPOLOGY_STRIP = 0,		//!< a connected polyline: N points -> N-1 segments
			TOPOLOGY_SEGMENTS = 1	//!< independent segments: point PAIRS (count even)
		};
		//! @brief one line vertex - a world/local XYZ position (the node carries
		//! the world placement) and a straight RGBA colour
		struct Vertex
		{
			Vec3	position;	//!< node-local XYZ
			Color	colour;		//!< straight RGBA per vertex
			Vertex() : position(Vec3::ZERO), colour(1, 1, 1, 1) {}
			Vertex(Vec3 const & p, Color const & c) : position(p), colour(c) {}
		};
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
		//! the selected backend's plumbing (@see RenderPrerequisites.h)
		friend struct RenderBackend;
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Impl*	mImpl;	//!< backend line guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - detaches and destroys the line geometry
		~LineMesh();

		//--- placement ---
		//! @brief attach to a node (detaches from a previous one first)
		//! map: classic/next=SceneNode::attachObject
		void attachTo(optr<RenderNode> const & node);
		//! map: classic/next=SceneNode::detachObject
		void detach();

		//--- content ---
		//! @brief refill from a CPU vertex array, establishing topology + count.
		//! For TOPOLOGY_STRIP a vertex count < 2 clears the mesh; for
		//! TOPOLOGY_SEGMENTS an odd count drops the trailing vertex. Called once
		//! for a static polyline, again whenever the point COUNT or topology
		//! changes.
		//! map: classic/next=(re)build the ManualObject line section
		void setLines(Vertex const * vertices, std::size_t vertexCount,
			Topology topology);
		//! @brief DYNAMIC fast path: rewrite the vertex positions/colours of an
		//! ALREADY-BUILT mesh WITHOUT changing topology - the per-change upload.
		//! vertexCount MUST equal the count from the last setLines; a mismatch,
		//! an empty mesh or a mesh never given a setLines is ignored (call
		//! setLines first). Reuses the existing hardware buffers.
		//! map: classic/next=ManualObject::beginUpdate(0) + re-emit vertices
		void updateVertices(Vertex const * vertices, std::size_t vertexCount);
		//! vertices in the mesh right now (0 when empty)
		std::size_t getVertexCount() const;
		//! line segments the mesh currently draws (strip: verts-1; segments: verts/2)
		std::size_t getLineCount() const;

		//--- look / visibility ---
		//! @brief depth-test the lines against the scene (default true) or draw
		//! them as an OVERLAY that ignores depth (false = on-top authoring/debug
		//! lines). Swaps the material/datablock; cheap, may be called live.
		void setDepthTest(bool depthTest);
		//! @see LineMesh::setDepthTest
		bool getDepthTest() const;
		//! map: classic/next=MovableObject::setVisible
		void setVisible(bool visible);
		//! @brief the scene-query flags (editor picking); lines default to
		//! never-picked like the vector mesh / sprite batch
		void setQueryFlags(unsigned int flags);
	protected:
		//! meshes are created by RenderWorld::createLineMesh only
		LineMesh();
	private:
		LineMesh(LineMesh const &);					// non-copyable
		LineMesh & operator=(LineMesh const &);		// non-copyable
	};
}

#endif //__LineMesh_h__25_7_2026__12_00_00__
