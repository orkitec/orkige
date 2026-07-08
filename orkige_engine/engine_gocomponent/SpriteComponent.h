/********************************************************************
	created:	Wednesday 2026/07/08 at 10:00
	filename: 	SpriteComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SpriteComponent_h__8_7_2026__10_00_00__
#define __SpriteComponent_h__8_7_2026__10_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_util/SceneNodeGuard.h"
#include "core_util/StringUtil.h"

namespace Orkige
{
	//! @brief a textured quad in the XY plane - the 2D/2.5D building block
	//! @remarks Follows ModelComponent's structure: needs a sibling
	//! TransformComponent, owns a child scene node, loads a texture resource
	//! by name (project assets resolve through the AUTODETECT resource group)
	//! and shows it on a unit-thin quad centered on the node.
	//!
	//! RENDERING/SORTING (the honest v1 rules):
	//! * The material is generated programmatically per texture
	//!   ("Sprite/<texture>", see createSpriteMaterial) - unlit, alpha-BLENDED
	//!   (SBT_TRANSPARENT_ALPHA), depth-CHECKED but not depth-written,
	//!   two-sided. Transparent PNGs therefore work with soft edges, and 3D
	//!   geometry in front of a sprite still occludes it. Alpha-REJECT
	//!   (cutout) rendering is not offered yet; if hard-edged sprites that
	//!   write depth become necessary, add it as a per-sprite material flag.
	//! * zOrder maps to the Ogre render queue (RENDER_QUEUE_MAIN + zOrder,
	//!   clamped, see renderQueueForZOrder): higher zOrder renders LATER =
	//!   on top, a painter's algorithm across sprites. Within ONE zOrder Ogre
	//!   sorts alpha-blended renderables by camera distance - which is
	//!   degenerate when 2D sprites share the same Z plane, so overlapping
	//!   sprites should use distinct zOrder values (or distinct Z positions).
	//! * Tint and flips live in the quad's vertex data (colour / swapped UVs),
	//!   so all sprites of one texture share one material.
	class ORKIGE_ENGINE_DLL SpriteComponent : public GameObjectComponent, public SceneNodeGuard
	{
		OOBJECT(SpriteComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a new sprite texture was set through loadSprite
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(SpriteSetEvent);
		//! @brief triggered before the sprite is removed and one exists
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(SpriteRemovedEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
		//! zOrder 0 renders in Ogre::RENDER_QUEUE_MAIN; the clamp range below
		static const int ZORDER_MIN;	//!< lowest accepted zOrder (-40)
		static const int ZORDER_MAX;	//!< highest accepted zOrder (+40)
	protected:
		Ogre::ManualObject*	mQuad;			//!< the sprite quad or NULL
		String				mTextureName;	//!< texture resource name or empty
		float				mWidth;			//!< world-units width (<= 0 = derive from texture aspect)
		float				mHeight;		//!< world-units height (<= 0 = derive from texture aspect)
		float				mTexelWidth;	//!< loaded texture width in texels (0 before load)
		float				mTexelHeight;	//!< loaded texture height in texels (0 before load)
		float				mU0, mV0;		//!< UV rect top-left (atlas region), default 0/0
		float				mU1, mV1;		//!< UV rect bottom-right, default 1/1
		Ogre::ColourValue	mTint;			//!< colour tint (vertex colour), default white
		bool				mFlipX;			//!< mirror horizontally
		bool				mFlipY;			//!< mirror vertically
		int					mZOrder;		//!< sprite sort order (see class remarks)
		bool				mVisible;		//!< sprite visibility (applied to the scene node)
		optr<StringUtil::StringObject> mEventData;	//!< name of the set/removed texture
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		SpriteComponent();
		//! destructor
		virtual ~SpriteComponent();
		//! @brief show the given texture on the sprite quad (replaces the
		//! current one) and trigger SpriteSetEvent
		//! @remarks the texture is resolved through the AUTODETECT resource
		//! group, so plain engine resources AND project assets ("ball.png" in
		//! projects/<name>/assets/) both work. A missing texture is an error
		//! log, not a crash - the sprite stays empty.
		void loadSprite(String const & textureName);
		//! remove the sprite quad and trigger SpriteRemovedEvent
		void removeSprite();
		//! @see SpriteComponent::mTextureName
		inline String const & getTextureName() const;
		//! is a sprite quad currently showing
		inline bool hasSprite() const;

		//! @brief world-units size; a value <= 0 derives that dimension from
		//! the texture aspect ratio (both <= 0: height 1, width = aspect)
		void setSize(float width, float height);
		//! configured width (<= 0 = derived; see getRenderedWidth)
		inline float getWidth() const;
		//! configured height (<= 0 = derived; see getRenderedHeight)
		inline float getHeight() const;
		//! the actually rendered width in world units (after aspect derivation)
		float getRenderedWidth() const;
		//! the actually rendered height in world units (after aspect derivation)
		float getRenderedHeight() const;
		//! @brief the shown texture region (for atlas sprites); defaults to the
		//! full texture (0,0)-(1,1). v runs top-down (Ogre texture convention).
		void setUVRect(float u0, float v0, float u1, float v1);
		//! colour tint, multiplied with the texture (default 1,1,1,1)
		void setTint(float red, float green, float blue, float alpha);
		//! current tint
		inline Ogre::ColourValue const & getTint() const;
		//! mirror the sprite on the X and/or Y axis
		void setFlip(bool flipX, bool flipY);
		//! @see SpriteComponent::mFlipX
		inline bool getFlipX() const;
		//! @see SpriteComponent::mFlipY
		inline bool getFlipY() const;
		//! sprite sort order (clamped to ZORDER_MIN..ZORDER_MAX; see remarks)
		void setZOrder(int zOrder);
		//! @see SpriteComponent::mZOrder
		inline int getZOrder() const;
		//! show/hide the sprite (the scene node's visibility)
		void setSpriteVisible(bool visible);
		//! is the sprite visible (true when no quad exists yet - it will show)
		bool isSpriteVisible() const;

		//--- pure helpers (headless-testable, no renderer required) ---
		//! @brief derive the rendered size from the configured size and the
		//! texture texel size (<= 0 configured = from aspect; fallback 1x1)
		static void resolveSize(float configuredWidth, float configuredHeight,
			float textureWidth, float textureHeight,
			float & outWidth, float & outHeight);
		//! @brief the quad's UV corners in vertex order top-left, top-right,
		//! bottom-right, bottom-left - flips swap the respective coordinates
		static void computeUVCorners(float u0, float v0, float u1, float v1,
			bool flipX, bool flipY, Ogre::Vector2 outCorners[4]);
		//! the Ogre render queue id a zOrder maps to (clamped)
		static Ogre::uint8 renderQueueForZOrder(int zOrder);
		//! @brief get or create the shared unlit alpha-blended sprite material
		//! for a LOADED texture (idempotent; name "Sprite/<texture>")
		static Ogre::MaterialPtr createSpriteMaterial(Ogre::TexturePtr const & texture);
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! (re)build the quad geometry from the current state (needs a texture)
		void rebuildQuad();
		//--- SERIALIZATION ---
		//! save texture name, size, UV rect, tint, flips and zOrder to Archive
		virtual void save(optr<IArchive> const & ar);
		//! load the sprite state from Archive (and load the sprite when attached)
		virtual void load(optr<IArchive> const & ar);
	private:
	};
	//---------------------------------------------------------------
	inline String const & SpriteComponent::getTextureName() const
	{
		return this->mTextureName;
	}
	//---------------------------------------------------------------
	inline bool SpriteComponent::hasSprite() const
	{
		return this->mQuad != NULL;
	}
	//---------------------------------------------------------------
	inline float SpriteComponent::getWidth() const
	{
		return this->mWidth;
	}
	//---------------------------------------------------------------
	inline float SpriteComponent::getHeight() const
	{
		return this->mHeight;
	}
	//---------------------------------------------------------------
	inline Ogre::ColourValue const & SpriteComponent::getTint() const
	{
		return this->mTint;
	}
	//---------------------------------------------------------------
	inline bool SpriteComponent::getFlipX() const
	{
		return this->mFlipX;
	}
	//---------------------------------------------------------------
	inline bool SpriteComponent::getFlipY() const
	{
		return this->mFlipY;
	}
	//---------------------------------------------------------------
	inline int SpriteComponent::getZOrder() const
	{
		return this->mZOrder;
	}
	//---------------------------------------------------------------
}

#endif //__SpriteComponent_h__8_7_2026__10_00_00__
