/********************************************************************
	created:	Saturday 2026/07/12 at 20:30
	filename: 	WaterComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __WaterComponent_h__12_7_2026__20_30_00__
#define __WaterComponent_h__12_7_2026__20_30_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/MeshInstance.h"
#include "engine_render/RenderWater.h"
#include "engine_util/SceneNodeGuard.h"

namespace Orkige
{
	//! @brief a flat animated water surface - a lake/sea plane in the XZ plane
	//! rendered with a scrolling water material.
	//! @remarks Needs a sibling TransformComponent, owns a child scene node and
	//! a facade MeshInstance of the shared engine water PLANE mesh
	//! (`water_plane.glb`, a unit grid in the XZ plane sized by the reflected
	//! `sizeX`/`sizeZ` node scale). The look is a per-instance water material
	//! (@see RenderWaterDesc / RenderSystem::createWaterMaterial) whose deep/
	//! shallow colour, opacity, wave scale/speed and fresnel are reflected
	//! designer knobs, plus a `normalTexture` AssetRef defaulting to the
	//! engine-generated tiling water normal map. The surface is FLAT - the
	//! ripple lives entirely in the scrolling normal detail, so there is NO
	//! per-frame CPU vertex work (a single plane is mobile-safe).
	//!
	//! Like ScriptComponent and VectorAnimationComponent, the animation is
	//! DORMANT unless a runtime ticks GameObjects: per gameplay tick the
	//! component advances a scroll clock and drives RenderSystem::setWaterTime,
	//! but the editor never ticks it, so a placed lake shows a static preview
	//! (WYSIWYG) and the ripple pauses for free when the owning GameObject goes
	//! inactive.
	//!
	//! Backend note (Docs/render-abstraction.md): next renders the full PBS
	//! water (two scrolling detail normal maps + fresnel transparency + deep/
	//! shallow colour), classic the honest transparent Blinn-Phong subset (one
	//! flat water tint + a scrolling shimmer overlay) - a per-flavor look, NOT a
	//! pixel-parity case.
	class ORKIGE_ENGINE_DLL WaterComponent
		: public GameObjectComponent, public SceneNodeGuard
	{
		OOBJECT(WaterComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
		//! the shared engine water plane mesh resource (a unit XZ grid)
		static char const * const PLANE_MESH_NAME;
		//! the engine-default tiling water normal map resource
		static char const * const DEFAULT_NORMAL_TEXTURE;
		//--- Variables ---------------------------------------------
	protected:
		optr<MeshInstance>	mMesh;			//!< the water plane instance or NULL
		String				mMaterialName;	//!< the per-instance water material name ("" until built)

		//--- reflected surface state ---
		float				mSizeX;			//!< plane width in world units (node scale on X)
		float				mSizeZ;			//!< plane depth in world units (node scale on Z)
		RenderWaterDesc		mDesc;			//!< deep/shallow colour, opacity, wave, fresnel, normal map
		String				mNormalAssetId;	//!< stable asset id of the normal map (rename survival)

		//--- live animation cursor (NOT serialized) ---
		float				mScrollTime;	//!< accumulated ripple scroll seconds
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		WaterComponent();
		//! destructor
		virtual ~WaterComponent();

		//! the water plane instance or NULL (selfcheck/introspection)
		inline optr<MeshInstance> const & getMeshInstance() const { return this->mMesh; }
		//! is a surface currently built
		inline bool hasSurface() const { return this->mMesh != nullptr; }
		//! the per-instance water material name (empty until built)
		inline String const & getMaterialName() const { return this->mMaterialName; }
		//! the accumulated ripple scroll time in seconds (0 in the editor - it
		//! never ticks; a selfcheck reads it to confirm the animation advances)
		inline float getScrollTime() const { return this->mScrollTime; }

		//--- reflected property accessors ---
		//! plane width in world units (node scale on X)
		void setSizeX(float sizeX);
		//! @see WaterComponent::mSizeX
		inline float getSizeX() const { return this->mSizeX; }
		//! plane depth in world units (node scale on Z)
		void setSizeZ(float sizeZ);
		//! @see WaterComponent::mSizeZ
		inline float getSizeZ() const { return this->mSizeZ; }

		//! deep-water body colour
		void setDeepColour(Color const & colour);
		//! @see RenderWaterDesc::deepColour
		inline Color const & getDeepColour() const { return this->mDesc.deepColour; }
		//! shallow-water / surface scatter colour
		void setShallowColour(Color const & colour);
		//! @see RenderWaterDesc::shallowColour
		inline Color const & getShallowColour() const { return this->mDesc.shallowColour; }
		//! surface transparency 0..1 (1 = opaque)
		void setOpacity(float opacity);
		//! @see RenderWaterDesc::opacity
		inline float getOpacity() const { return this->mDesc.opacity; }
		//! detail-normal tiling factor (higher = smaller ripples)
		void setWaveScale(float waveScale);
		//! @see RenderWaterDesc::waveScale
		inline float getWaveScale() const { return this->mDesc.waveScale; }
		//! ripple scroll speed (UV units per second)
		void setWaveSpeed(float waveSpeed);
		//! @see RenderWaterDesc::waveSpeed
		inline float getWaveSpeed() const { return this->mDesc.waveSpeed; }
		//! edge-reflection strength knob
		void setFresnelPower(float fresnelPower);
		//! @see RenderWaterDesc::fresnelPower
		inline float getFresnelPower() const { return this->mDesc.fresnelPower; }
		//! @brief set the tiling water normal map REFERENCE (the reflected
		//! AssetRef setter): a name (or the empty string = flat surface)
		//! re-applies the material when built, otherwise records the reference
		void setNormalTexture(String const & normalTexture);
		//! @see RenderWaterDesc::normalTexture
		inline String const & getNormalTexture() const { return this->mDesc.normalTexture; }
		//! @brief whether cast shadows darken the surface (reflected
		//! `receiveShadows`, default true; RECEIVE-ONLY - the plane never
		//! casts, its caster flag is off by design)
		void setReceiveShadows(bool receives);
		//! @see RenderWaterDesc::receiveShadows
		inline bool getReceiveShadows() const { return this->mDesc.receiveShadows; }
	protected:
		//! component override: create the child scene node, build the surface
		virtual void onAdd();
		//! component override: drop the mesh + node
		virtual void onRemove();
		//! deactivated GameObjects hide their surface
		virtual void onSetActive(bool activeInHierarchy);
		//! per-tick ripple: advance the scroll clock and drive setWaterTime
		//! (dormant unless a runtime ticks GameObjects)
		virtual void onUpdateComponent(float deltaTime);
		//--- SERIALIZATION ---
		//! save the size + surface knobs + normal-map AssetRef
		virtual void save(optr<IArchive> const & ar);
		//! load the surface state from Archive (and build when attached)
		virtual void load(optr<IArchive> const & ar);
	private:
		//! @brief build the plane mesh (once), scale the node and apply the
		//! water material; no-op without a scene node. A missing plane mesh or
		//! material is logged, not fatal - the component stays empty.
		void buildSurface();
		//! @brief (re)create + assign the per-instance water material from the
		//! current mDesc; no-op without a live mesh
		void applyMaterial();
		//! push the node scale (mSizeX, 1, mSizeZ) onto the live node
		void applyScale();
	};
}

#endif //__WaterComponent_h__12_7_2026__20_30_00__
