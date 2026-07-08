/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	SpriteQuad.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SpriteQuad_h__8_7_2026__12_00_00__
#define __SpriteQuad_h__8_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>

namespace Orkige
{
	//! @brief a textured, alpha-blended quad in the XY plane - what
	//! SpriteComponent needs from the renderer (the 2D building block)
	//! @remarks Carries SpriteComponent's honest v1 rendering rules
	//! (unlit, alpha-blended, depth-checked/not-written, two-sided; tint
	//! and flips live in vertex data so all sprites of one texture share
	//! one material; zOrder is painter's-algorithm sorting). The pure
	//! helpers stay static on SpriteComponent (headless-testable).
	//!
	//! Backend mapping (whole class): classic = Ogre::ManualObject quad +
	//! generated "Sprite/<texture>" material, zOrder -> render queue id;
	//! next = v2 ManualObject/BufferPacked quad + one HlmsUnlit datablock
	//! per texture, zOrder -> render queue + macroblock; filament = static
	//! quad vertex/index buffer + unlit blended filamat instance per
	//! texture, zOrder -> blendOrder/priority on the renderable.
	class ORKIGE_ENGINE_DLL SpriteQuad
	{
		//--- Types -------------------------------------------------
	public:
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
		//! the selected backend's plumbing (@see RenderPrerequisites.h)
		friend struct RenderBackend;
	private:
		//--- Variables ---------------------------------------------
	public:
		static const int ZORDER_MIN;	//!< lowest accepted zOrder (-40 today)
		static const int ZORDER_MAX;	//!< highest accepted zOrder (+40 today)
	protected:
		Impl*	mImpl;	//!< backend quad guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - detaches and destroys the quad geometry
		~SpriteQuad();

		//--- placement ---
		//! @brief attach to a node (detaches from a previous one first)
		//! map: classic/next=SceneNode::attachObject | filament=parent the quad entity
		void attachTo(optr<RenderNode> const & node);
		//! map: classic/next=SceneNode::detachObject | filament=Scene::remove
		void detach();

		//--- texture / geometry ---
		//! the texture resource name the quad was created from
		String const & getTextureName() const;
		//! texel size of the loaded texture (sprite aspect derivation)
		//! map: classic=TexturePtr::getWidth/Height | next=TextureGpu::getWidth/Height | filament=Texture::getWidth/Height
		void getTextureSize(float & outWidth, float & outHeight) const;
		//! @brief world-unit quad size (rebuilds the vertex data)
		//! map: all backends=facade rebuilds its quad vertices
		void setSize(float width, float height);
		//! @brief shown texture region for atlas sprites, v runs top-down
		//! map: all backends=facade quad UVs
		void setUVRect(float u0, float v0, float u1, float v1);
		//! @brief colour tint multiplied with the texture (vertex colour)
		//! map: all backends=facade quad vertex colours
		void setTint(Color const & tint);
		//! @brief mirror on X and/or Y (swaps quad UVs)
		//! map: all backends=facade quad UVs
		void setFlip(bool flipX, bool flipY);

		//--- sorting / visibility ---
		//! @brief painter's-algorithm sort order, clamped to ZORDER_MIN/MAX
		//! (higher renders later = on top)
		//! map: classic=Renderable render queue RENDER_QUEUE_MAIN+z | next=render queue id (v2 queues 0..255, subset valid for v2 objects) | filament=Renderable blendOrder/priority
		void setZOrder(int zOrder);
		//! map: classic/next=MovableObject::setVisible | filament=Scene::add/remove
		void setVisible(bool visible);
		//! @see RenderWorld::queryRay
		//! map: classic/next=MovableObject::setQueryFlags | filament=facade-side query filter
		void setQueryFlags(unsigned int flags);
	protected:
		//! quads are created by RenderWorld::createSpriteQuad only
		SpriteQuad();
	private:
		SpriteQuad(SpriteQuad const &);					// non-copyable
		SpriteQuad & operator=(SpriteQuad const &);		// non-copyable
	};
}

#endif //__SpriteQuad_h__8_7_2026__12_00_00__
