/********************************************************************
	created:	Friday 2026/07/18 at 00:30
	filename: 	DecalComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __DecalComponent_h__18_7_2026__00_30_00__
#define __DecalComponent_h__18_7_2026__00_30_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/RenderDecal.h"
#include "engine_util/SceneNodeGuard.h"

namespace Orkige
{
	//! @brief a projected surface mark on a GameObject - impact marks, paint
	//! splats, footprints and the blob-shadow fallback tier - the component
	//! consumer of the engine_render RenderDecal facade.
	//! @remarks Follows LightComponent/WaterComponent structure: needs a sibling
	//! TransformComponent, owns a child scene node (SceneNodeGuard) and attaches a
	//! facade RenderDecal to it, so the mark follows the object's transform and
	//! projects DOWN the object's local -Y onto the surface below it (orient the
	//! object so its +Y is the surface normal - a ground mark needs no rotation).
	//!
	//! PER-FLAVOR LOOK (RenderCaps::ProjectedDecals, Docs/render-abstraction.md):
	//! next = a true projected Ogre-Next Decal wrapping over any geometry inside
	//! its box; classic = a surface-aligned textured quad floating just above the
	//! surface (flat, does not wrap uneven geometry). A per-flavor tolerance.
	//!
	//! LIFETIME + FADE (dormant unless a runtime ticks GameObjects - the editor
	//! shows a static preview, WYSIWYG, like WaterComponent/ScriptComponent):
	//! `lifetime` > 0 makes the mark age out - over the last `fadeDuration`
	//! seconds its opacity ramps to 0 and it hides (the game destroys the owning
	//! object; a `lifetime` of 0 is a permanent mark). Lua `self.decal:fade(sec)`
	//! starts an immediate fade regardless of lifetime; `self.decal:place(pos,
	//! normal)` re-stamps a fresh mark at a world position (teleporting the owner
	//! transform, resetting the age). Opacity is smooth on classic; on next it
	//! thresholds (the mark pops out at fade end - @see RenderDecal).
	//!
	//! BUDGET: the world caps concurrently visible decals (RenderWorld::
	//! setMaxDecals, the `r.maxDecals` cvar); when exceeded the OLDEST decals are
	//! hidden. Independent of the per-component lifetime fade.
	//!
	//! BLOB SHADOW: set `texture` to the engine `decal_blob.png` (a soft dark
	//! ellipse) and place the object under a character to get a cheap blob shadow
	//! where real shadow maps are off/refused (Docs/materials.md#blob-shadows).
	class ORKIGE_ENGINE_DLL DecalComponent
		: public GameObjectComponent, public SceneNodeGuard
	{
		OOBJECT(DecalComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
		//! the fixed decal-pool texture resolution (mirrors the next backend's
		//! DECAL_TEXTURE_SIZE - decal textures should be authored at this size)
		static unsigned int const DECAL_TEXTURE_SIZE = 256u;
		//! the engine-default neutral mark texture (a soft white round mark)
		static char const * const DEFAULT_TEXTURE;
		//! the engine blob-shadow texture (a soft dark ellipse)
		static char const * const BLOB_TEXTURE;
		//--- Variables ---------------------------------------------
	protected:
		optr<RenderDecal>	mDecal;			//!< the facade decal or NULL (detached)
		String				mTexture;		//!< the mark texture name (AssetRef)
		String				mTextureAssetId;//!< stable asset id (rename survival)
		float				mSizeX;			//!< footprint width (world units, local X)
		float				mSizeZ;			//!< footprint depth (world units, local Z)
		float				mProjectionDepth;//!< next box depth along -Y (classic ignores)
		float				mOpacity;		//!< authored base opacity 0..1
		float				mLifetime;		//!< seconds before the mark expires (0 = permanent)
		float				mFadeDuration;	//!< fade-out window at end of life (seconds)

		//--- live (NOT serialized) ---
		float				mAge;			//!< seconds since the mark was placed
		float				mManualFadeDuration;//!< > 0 while a Lua fade() runs
		float				mManualFadeElapsed;	//!< seconds into the manual fade
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		DecalComponent();
		//! destructor
		virtual ~DecalComponent();

		//! the facade decal or NULL (selfcheck/introspection)
		inline optr<RenderDecal> const & getDecal() const { return this->mDecal; }
		//! is a facade decal currently live
		inline bool hasDecal() const { return this->mDecal != nullptr; }
		//! the live age in seconds (0 in the editor - it never ticks)
		inline float getAge() const { return this->mAge; }

		//--- reflected property accessors ---
		//! set the mark texture REFERENCE (a name, or "" = no mark)
		void setTexture(String const & texture);
		//! @see DecalComponent::mTexture
		inline String const & getTexture() const { return this->mTexture; }
		//! footprint width in world units (local X)
		void setSizeX(float sizeX);
		//! @see DecalComponent::mSizeX
		inline float getSizeX() const { return this->mSizeX; }
		//! footprint depth in world units (local Z)
		void setSizeZ(float sizeZ);
		//! @see DecalComponent::mSizeZ
		inline float getSizeZ() const { return this->mSizeZ; }
		//! projection-box depth along -Y (next only; classic aligns a flat quad)
		void setProjectionDepth(float projectionDepth);
		//! @see DecalComponent::mProjectionDepth
		inline float getProjectionDepth() const { return this->mProjectionDepth; }
		//! authored base opacity 0..1 (the fade ramps DOWN from this)
		void setOpacity(float opacity);
		//! @see DecalComponent::mOpacity
		inline float getOpacity() const { return this->mOpacity; }
		//! seconds before the mark ages out (0 = permanent)
		void setLifetime(float lifetime);
		//! @see DecalComponent::mLifetime
		inline float getLifetime() const { return this->mLifetime; }
		//! fade-out window at end of life in seconds
		void setFadeDuration(float fadeDuration);
		//! @see DecalComponent::mFadeDuration
		inline float getFadeDuration() const { return this->mFadeDuration; }

		//--- Lua drive (self.decal) ---
		//! @brief re-stamp a fresh mark at a WORLD position, oriented so its
		//! projection faces down @p upNormal (teleports the owner transform and
		//! resets the age + any running fade). A ground mark passes normal (0,1,0).
		void place(Vec3 const & position, Vec3 const & upNormal);
		//! @brief start an immediate fade-out over @p durationSeconds, regardless
		//! of the lifetime; the mark hides when it completes.
		void fade(float durationSeconds);

		//! @brief the PURE opacity factor 0..1 for a decal's age (multiplied by
		//! the base opacity). A running manual fade wins; else the lifetime ramp:
		//! 1 until the last @p fadeDuration of @p lifetime, then a linear ramp to
		//! 0, then 0 once expired; a @p lifetime of 0 is permanent (1). Static +
		//! headless-testable (the lifetime/fade curve), no component state.
		static float fadeFactor(float age, float lifetime, float fadeDuration,
			float manualFadeDuration, float manualFadeElapsed);
	protected:
		//! Component override: create the child scene node + facade decal
		virtual void onAdd();
		//! Component override: drop the decal + node
		virtual void onRemove();
		//! a deactivated GameObject hides its mark
		virtual void onSetActive(bool activeInHierarchy);
		//! per-tick: age the mark, ramp the fade, expire at end of life (dormant
		//! unless a runtime ticks GameObjects)
		virtual void onUpdateComponent(float deltaTime);
		//--- SERIALIZATION ---
		//! save the texture AssetRef + size/opacity/lifetime knobs
		virtual void save(optr<IArchive> const & ar);
		//! load the decal state (and build when attached)
		virtual void load(optr<IArchive> const & ar);
	private:
		//! @brief create the facade decal (once) and apply the current state; no-op
		//! without a scene node (a detached component keeps state on the component)
		void buildDecal();
		//! push texture + size onto the live decal
		void applyStateToDecal();
		//! the CURRENT opacity factor (base * fade ramp) for the live age
		float currentOpacity() const;
		//! push currentOpacity onto the live decal (the per-frame fade site)
		void applyOpacity();
	};
}

#endif //__DecalComponent_h__18_7_2026__00_30_00__
