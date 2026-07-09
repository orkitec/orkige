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
#include "engine_render/SpriteQuad.h"
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
	//! Phase A1 (Docs/render-abstraction.md, WP-A1.2): renders through the
	//! facade SpriteQuad, which carries the honest v1 rendering rules the
	//! component pioneered - unlit, alpha-BLENDED, depth-checked/not-written,
	//! two-sided, one generated material per texture (tint/flips live in the
	//! vertex data), zOrder = painter's-algorithm sorting (higher renders
	//! later = on top; overlapping sprites should use distinct zOrders).
	//! The pure helpers below stay static and headless-testable.
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
		//! zOrder 0 renders in the middle of the sprite queue window; the
		//! clamp range mirrors SpriteQuad::ZORDER_MIN/MAX
		static const int ZORDER_MIN;	//!< lowest accepted zOrder (-40)
		static const int ZORDER_MAX;	//!< highest accepted zOrder (+40)
	protected:
		optr<SpriteQuad>	mQuad;			//!< the facade sprite quad or NULL
		String				mTextureName;	//!< texture resource name or empty
		String				mTextureAssetId;	//!< stable asset id of the texture ("" = none/engine media)
		float				mWidth;			//!< world-units width (<= 0 = derive from texture aspect)
		float				mHeight;		//!< world-units height (<= 0 = derive from texture aspect)
		float				mTexelWidth;	//!< loaded texture width in texels (0 before load)
		float				mTexelHeight;	//!< loaded texture height in texels (0 before load)
		float				mU0, mV0;		//!< UV rect top-left (atlas region), default 0/0
		float				mU1, mV1;		//!< UV rect bottom-right, default 1/1
		Color				mTint;			//!< colour tint (vertex colour), default white
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
		//! @see SpriteComponent::mTextureAssetId
		inline String const & getTextureAssetId() const;
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
		//! full texture (0,0)-(1,1). v runs top-down (texture convention).
		void setUVRect(float u0, float v0, float u1, float v1);
		//! colour tint, multiplied with the texture (default 1,1,1,1)
		void setTint(float red, float green, float blue, float alpha);
		//! current tint
		inline Color const & getTint() const;
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
			bool flipX, bool flipY, Vec2 outCorners[4]);
		//! the render queue id a zOrder maps to (clamped; base queue 50 on
		//! both Ogre backends)
		//! @remarks pure math kept for the unit tests; the live mapping is
		//! SpriteQuad::setZOrder inside the render backend
		static unsigned char renderQueueForZOrder(int zOrder);
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! deactivated GameObjects hide their sprite (setSpriteVisible state is kept)
		virtual void onSetActive(bool activeInHierarchy);
		//! push the stored sprite state onto the facade quad (needs a quad)
		void applyStateToQuad();
		//! apply the EFFECTIVE visibility to the node: the sprite's own flag
		//! AND the owner's activeInHierarchy state
		void applyVisibility();
		//--- SERIALIZATION ---
		//! save texture name (plus its stable asset id as the assetId
		//! attribute), size, UV rect, tint, flips and zOrder to Archive
		virtual void save(optr<IArchive> const & ar);
		//! @brief load the sprite state from Archive (and load the sprite when
		//! attached); a texture asset id that resolves in the open project's
		//! AssetDatabase wins over a stale texture name (rename survival)
		virtual void load(optr<IArchive> const & ar);
	private:
	};
	//---------------------------------------------------------------
	inline String const & SpriteComponent::getTextureName() const
	{
		return this->mTextureName;
	}
	//---------------------------------------------------------------
	inline String const & SpriteComponent::getTextureAssetId() const
	{
		return this->mTextureAssetId;
	}
	//---------------------------------------------------------------
	inline bool SpriteComponent::hasSprite() const
	{
		return this->mQuad != nullptr;
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
	inline Color const & SpriteComponent::getTint() const
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
