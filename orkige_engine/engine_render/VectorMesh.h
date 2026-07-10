/********************************************************************
	created:	Thursday 2026/07/10 at 11:00
	filename: 	VectorMesh.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorMesh_h__10_7_2026__11_00_00__
#define __VectorMesh_h__10_7_2026__11_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"

#include <cstddef>

namespace Orkige
{
	//! @brief a world-space, untextured, vertex-coloured indexed triangle mesh
	//! drawn in ONE draw call - the flat-colour organic-shape building block
	//! @remarks The arbitrary-triangle sibling of SpriteBatch: where a
	//! SpriteBatch is textured quads with fixed winding, a VectorMesh is a
	//! tessellated closed shape (fills + an alpha-feather edge) as an arbitrary
	//! indexed triangle list. It carries the SAME honest 2D rules as
	//! SpriteQuad/SpriteBatch - unlit, alpha-BLENDED, depth-checked/not-written,
	//! two-sided - and its zOrder is the SAME painter's window
	//! (RenderBackend::renderQueueForZOrder, clamped to
	//! SpriteQuad::ZORDER_MIN/MAX), so shapes interleave with sprites by one
	//! unified 2D sort. Colour lives ENTIRELY in the vertex data (fill colours
	//! times the instance tint, plus the feather alpha ramp), so every shape
	//! shares ONE generated untextured unlit datablock/material ("VectorFill") -
	//! there is no per-shape material.
	//!
	//! The refill contract mirrors SpriteBatch::setQuads: setMesh copies a whole
	//! CPU vertex+index array each call. Static shapes call it once; a future
	//! deformable shape can call it per frame with no facade change.
	//!
	//! Backend mapping (whole class): classic = Ogre::ManualObject
	//! (OT_TRIANGLE_LIST) + the shared unlit vertex-colour "VectorFill"
	//! material, zOrder -> render queue; next = v2 Ogre::ManualObject
	//! (SCENE_DYNAMIC) + the shared HlmsUnlit vertex-colour datablock, zOrder ->
	//! render queue group.
	class ORKIGE_ENGINE_DLL VectorMesh
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief one mesh vertex - a shape-local XY corner (z from the node)
		//! and a straight RGBA colour (fill colour times tint, or feather alpha)
		struct Vertex
		{
			Vec2	position;	//!< shape-local XY (the node carries world placement/scale)
			Color	colour;		//!< straight RGBA, already tinted at fill time
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
		Impl*	mImpl;	//!< backend mesh guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - detaches and destroys the mesh geometry
		~VectorMesh();

		//--- placement ---
		//! @brief attach to a node (detaches from a previous one first)
		//! map: classic/next=SceneNode::attachObject
		void attachTo(optr<RenderNode> const & node);
		//! map: classic/next=SceneNode::detachObject
		void detach();

		//--- content ---
		//! @brief refill from a CPU vertex + index array (indexCount must be a
		//! multiple of 3; each index addresses a vertex). A vertexCount or
		//! indexCount of 0 clears the mesh to nothing. Called once for static
		//! shapes, per-frame for the future deformable path.
		//! map: all backends=rebuild the ManualObject from the arrays
		void setMesh(Vertex const * vertices, std::size_t vertexCount,
			unsigned int const * indices, std::size_t indexCount);
		//! how many triangles the mesh currently holds
		std::size_t getTriangleCount() const;

		//--- sorting / visibility ---
		//! @brief painter's-algorithm sort order, clamped to the sprite window
		//! (SpriteQuad::ZORDER_MIN/MAX); higher renders later = on top
		void setZOrder(int zOrder);
		//! map: classic/next=MovableObject::setVisible
		void setVisible(bool visible);
		//! @brief the scene-query flags (editor picking); shapes default to
		//! never-picked like the sprite batch
		void setQueryFlags(unsigned int flags);
	protected:
		//! meshes are created by RenderWorld::createVectorMesh only
		VectorMesh();
	private:
		VectorMesh(VectorMesh const &);					// non-copyable
		VectorMesh & operator=(VectorMesh const &);		// non-copyable
	};
}

#endif //__VectorMesh_h__10_7_2026__11_00_00__
