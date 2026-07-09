/********************************************************************
	created:	Wednesday 2026/07/09 at 14:00
	filename: 	SpriteBatch.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SpriteBatch_h__9_7_2026__14_00_00__
#define __SpriteBatch_h__9_7_2026__14_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>

#include <cstddef>

namespace Orkige
{
	//! @brief a world-space, single-texture, N-quad batch drawn in ONE draw
	//! call - the 2D particle-system building block
	//! @remarks The sibling of SpriteQuad for the many-quads case: where a
	//! SpriteQuad is one textured quad per movable object, a SpriteBatch holds
	//! a whole CPU vertex array (four vertices per quad, TL/TR/BR/BL) that the
	//! owner refills every frame through setQuads - the backend copies it into
	//! one ManualObject so the whole system is a SINGLE draw. Vertices are
	//! WORLD-space (the batch is attached to the world root, particles fly
	//! independent of any emitter node); tint and per-quad geometry live in the
	//! vertex data, so all quads of one texture+blend share one material.
	//! zOrder maps onto the SAME render-queue painter's window SpriteQuad uses
	//! (RenderBackend::renderQueueForZOrder, 50 +- 40) - no parallel depth
	//! scheme. Two blend modes: alpha (the sprite recipe, order-dependent) and
	//! additive (the glow recipe, order-independent - the recommended default
	//! for bursts).
	//!
	//! Backend mapping (whole class): classic = Ogre::ManualObject rebuilt from
	//! the vertex array + the shared "Sprite/<tex>" / additive "SpriteAdd/<tex>"
	//! material; next = v2 Ogre::ManualObject rebuilt the same way + the shared
	//! per-texture HlmsUnlit datablock (alpha or additive blendblock).
	class ORKIGE_ENGINE_DLL SpriteBatch
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief how the batch's quads composite with the framebuffer
		//! map: classic=Pass::setSceneBlending | next=HlmsBlendblock factors
		enum BlendMode
		{
			BLEND_ALPHA,		//!< src.a over dst (the sprite recipe; order-dependent)
			BLEND_ADDITIVE		//!< src.rgb*src.a + dst (glow; order-independent - the burst default)
		};
		//! @brief one batch vertex - a world-space corner with its UV and
		//! colour. The owner supplies four per quad in the corner order
		//! top-left, top-right, bottom-right, bottom-left (the SpriteQuad
		//! winding); the backend forms two triangles (0,3,2)(0,2,1) per quad.
		struct Vertex
		{
			Vec3	position;	//!< world-space corner position
			Vec2	uv;			//!< texture coordinate (v runs top-down)
			Color	colour;		//!< vertex colour, multiplied with the texture
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
		Impl*	mImpl;	//!< backend batch guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - detaches and destroys the batch geometry
		~SpriteBatch();

		//--- placement ---
		//! @brief attach to a node (detaches from a previous one first)
		//! map: classic/next=SceneNode::attachObject
		void attachTo(optr<RenderNode> const & node);
		//! map: classic/next=SceneNode::detachObject
		void detach();

		//--- content ---
		//! the texture resource name the batch was created from
		String const & getTextureName() const;
		//! texel size of the loaded texture (atlas-frame UV derivation)
		void getTextureSize(float & outWidth, float & outHeight) const;
		//! the blend mode chosen at creation
		BlendMode getBlendMode() const;
		//! @brief refill the batch from a CPU vertex array (four vertices per
		//! quad, TL/TR/BR/BL); a quadCount of 0 clears the batch to nothing.
		//! Called once per frame by the owner (the particle component).
		//! map: all backends=rebuild the ManualObject from the array
		void setQuads(Vertex const * vertices, std::size_t quadCount);
		//! how many quads the batch currently holds
		std::size_t getQuadCount() const;

		//--- sorting / visibility ---
		//! @brief painter's-algorithm sort order, clamped to the sprite window
		//! (SpriteQuad::ZORDER_MIN/MAX); higher renders later = on top
		void setZOrder(int zOrder);
		//! map: classic/next=MovableObject::setVisible
		void setVisible(bool visible);
	protected:
		//! batches are created by RenderWorld::createSpriteBatch only
		SpriteBatch();
	private:
		SpriteBatch(SpriteBatch const &);				// non-copyable
		SpriteBatch & operator=(SpriteBatch const &);	// non-copyable
	};
}

#endif //__SpriteBatch_h__9_7_2026__14_00_00__
