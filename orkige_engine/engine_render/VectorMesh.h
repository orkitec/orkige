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
#include <core_util/String.h>

#include <cstddef>

namespace Orkige
{
	//! @brief a world-space, vertex-coloured indexed triangle mesh drawn in ONE
	//! draw call per texture - the organic-shape building block (flat-colour
	//! fills plus textured cutout parts)
	//! @remarks The arbitrary-triangle sibling of SpriteBatch: where a
	//! SpriteBatch is textured quads with fixed winding, a VectorMesh is a
	//! tessellated closed shape (fills + an alpha-feather edge) as an arbitrary
	//! indexed triangle list. It carries the SAME honest 2D rules as
	//! SpriteQuad/SpriteBatch - unlit, alpha-BLENDED, depth-checked/not-written,
	//! two-sided - and its zOrder is the SAME painter's window
	//! (RenderBackend::renderQueueForZOrder, clamped to
	//! SpriteQuad::ZORDER_MIN/MAX), so shapes interleave with sprites by one
	//! unified 2D sort. For FLAT geometry colour lives ENTIRELY in the vertex
	//! data (fill colours times the instance tint, plus the feather alpha
	//! ramp), so every flat shape shares ONE generated untextured unlit
	//! datablock/material ("VectorFill") - there is no per-shape material. A
	//! TEXTURED section instead binds the per-(texture,sampler) sprite
	//! material/datablock ("Sprite/<tex>#bilinear-clamp" - reused wholesale,
	//! never a parallel recipe) and its vertices additionally carry UVs; the
	//! vertex colour becomes the multiply tint. One section = one draw, so the
	//! batching discipline is the tessellator's per-texture runs.
	//!
	//! The refill contract mirrors SpriteBatch::setQuads: setMesh /
	//! setMeshSections copies whole CPU vertex+index arrays each call
	//! (establishing the topology). A deformable/animated shape then calls
	//! updateVertices / updateSectionVertices per frame - the DYNAMIC fast path
	//! that rewrites the existing vertex buffer IN PLACE
	//! (ManualObject::beginUpdate) without re-specifying or reallocating the
	//! index/topology, so a jiggling soft shape costs a buffer rewrite, not a
	//! section rebuild.
	//!
	//! Backend mapping (whole class): classic = Ogre::ManualObject
	//! (OT_TRIANGLE_LIST, one section per Section) + the shared unlit
	//! vertex-colour "VectorFill" material / the per-texture sprite material,
	//! zOrder -> render queue; next = v2 Ogre::ManualObject (SCENE_DYNAMIC) +
	//! the shared HlmsUnlit vertex-colour datablock / the per-texture sprite
	//! datablock, zOrder -> render queue group.
	class ORKIGE_ENGINE_DLL VectorMesh
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief one mesh vertex - a shape-local XY corner (z from the node),
		//! a straight RGBA colour (fill colour times tint, or feather alpha)
		//! and a texture coordinate (consumed only by textured sections;
		//! untextured geometry leaves it (0,0) and emits no UV stream)
		struct Vertex
		{
			Vec2	position;	//!< shape-local XY (the node carries world placement/scale)
			Color	colour;		//!< straight RGBA, already tinted at fill time
			Vec2	uv;			//!< texture coordinate (textured sections only)
		};
		//! @brief one draw section: an indexed triangle list bound to one
		//! texture (or the shared flat "VectorFill" recipe when texture is
		//! empty). Sections render in array order within the mesh's zOrder -
		//! the tessellator's per-texture runs map here 1:1.
		struct Section
		{
			String				texture;	//!< texture resource name; empty = flat untextured
			Vertex const *		vertices;	//!< the section's vertex array
			std::size_t			vertexCount;
			unsigned int const *	indices;	//!< section-local indices (3 per triangle)
			std::size_t			indexCount;
			Section() : vertices(0), vertexCount(0), indices(0),
				indexCount(0) {}
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
		//! @brief DYNAMIC fast path: rewrite the vertex positions/colours of an
		//! ALREADY-BUILT mesh WITHOUT changing topology - the per-frame deform
		//! upload. vertexCount MUST equal the count from the last setMesh (the
		//! index list is unchanged and reused); a mismatch, an empty mesh or a
		//! mesh never given a setMesh is ignored (call setMesh first). Cheaper
		//! than setMesh: it reuses the existing hardware buffers instead of
		//! rebuilding the section.
		//! map: classic/next = ManualObject::beginUpdate(0) + re-emit vertices
		//! and the cached indices
		void updateVertices(Vertex const * vertices, std::size_t vertexCount);
		//! @brief refill from a SECTION list (one draw per section, array
		//! order = paint order): each section is its own vertex+index array
		//! bound to its texture (empty = the shared flat "VectorFill"
		//! recipe). Textured sections emit their vertices' UVs and bind the
		//! per-(texture,sampler) sprite material/datablock (bilinear+clamp);
		//! a texture that fails to load logs once and falls back to the flat
		//! recipe (the tint-coloured silhouette - visible, never a crash). A
		//! sectionCount of 0 clears the mesh. setMesh is exactly
		//! setMeshSections of ONE untextured section.
		//! map: classic/next=one ManualObject section per entry
		void setMeshSections(Section const * sections,
			std::size_t sectionCount);
		//! @brief DYNAMIC fast path per section: rewrite one section's vertex
		//! data (positions/colours/UVs) without changing topology - the
		//! per-frame animated-rig upload. vertexCount MUST equal that
		//! section's count from the last setMeshSections; a mismatch or an
		//! unknown section is ignored. updateVertices is exactly
		//! updateSectionVertices(0, ...).
		//! map: classic/next=ManualObject::beginUpdate(section)
		void updateSectionVertices(std::size_t sectionIndex,
			Vertex const * vertices, std::size_t vertexCount);
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
