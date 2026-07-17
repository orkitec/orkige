/********************************************************************
	created:	Monday 2010/08/30 at 17:37
	filename: 	ModelComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ModelComponent_h__30_8_2010__17_37_08__
#define __ModelComponent_h__30_8_2010__17_37_08__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/MeshInstance.h"
#include "engine_util/SceneNodeGuard.h"
#include "core_util/StringUtil.h"

namespace Orkige
{
	//! @brief handles 1 Model attached to a GameObject
	//! @remarks owns a facade
	//! MeshInstance on an owned child node of the sibling TransformComponent.
	//! The historical shareSkeletonInstance flag of loadModel was dropped -
	//! Ogre skeleton-instance sharing has no facade shape and no caller
	//! (recoverable from git if a real need returns).
	//!
	//! MATERIAL: the reflected `material` reference names a `.omat` asset
	//! (core_util/MaterialAsset) rendered over the mesh's imported materials -
	//! whole-instance, all sub-meshes (@see MeshInstance::setMaterial). The
	//! component reads the asset text through the resource system, parses it,
	//! feeds RenderSystem::createMaterial under the shared name
	//! "Omat/<asset>" (so every user of one `.omat` shares ONE live renderer
	//! material - editing the asset updates them all on the next apply) and
	//! assigns it. Clearing the reference reloads the mesh, restoring the
	//! imported materials. A missing/malformed asset or a mesh that cannot
	//! host the maps logs and leaves the mesh's current materials - honest,
	//! never half-applied.
	//!
	//! SHADOWS: the reflected `castShadows`/`receiveShadows` flags (default
	//! true) are this object's per-instance shadow participation on a
	//! shadow-capable flavor (@see RenderWorld::setShadowQuality).
	//! `castShadows` maps to the mesh instance's caster flag;
	//! `receiveShadows` maps to the material/datablock level, where the
	//! backend swaps the instance onto a no-receive VARIANT of its material
	//! (@see MeshInstance::setReceiveShadows), so instances sharing one
	//! `.omat` keep independent flags. Both apply live (inspector, Lua, MCP,
	//! debug protocol) and re-apply after every model/material change.
	//!
	//! RUNTIME ACCENTS: `tint` (multiplies the albedo, white = none) and
	//! `emissiveBoost` (adds emitted colour, black = none - the hit-flash
	//! accent) are per-instance, RUNTIME-ONLY looks (reflected TRANSIENT, so
	//! inspector/Lua/MCP reach them but a scene save never records them - the
	//! `.omat` stays the authored truth). They ride the same per-instance
	//! variant machinery as `receiveShadows` (@see MeshInstance::setTint) and
	//! re-apply after every model/material/shadow-flag change.
	class ORKIGE_ENGINE_DLL ModelComponent : public GameObjectComponent, public SceneNodeGuard
	{
		OOBJECT(ModelComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a new model was set through ModelComponent::loadModel
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(ModelSetEvent);
		//! @brief triggered when before a new model is set and already one exists
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(ModelRemovedEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<MeshInstance>	mesh;					//!< the current mesh instance or NULL
		String				modelFileName;			//!< filename of the current model or empty String
		String				modelAssetId;			//!< stable asset id of the mesh ("" = none/engine media)
		String				materialFileName;		//!< `.omat` asset rendered over the imported materials ("" = none)
		String				materialAssetId;		//!< stable asset id of the material ("" = none)
		bool				mCastShadows;			//!< this instance throws shadows (@see class remarks)
		bool				mReceiveShadows;		//!< cast shadows darken this instance (@see class remarks)
		Color				mTint;					//!< runtime albedo tint, white = none (@see class remarks)
		Color				mEmissiveBoost;			//!< runtime additive emission, black = none (@see class remarks)
		optr<StringUtil::StringObject> eventData;	//!< name of set or removed model
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		ModelComponent();
		//! destructor
		virtual ~ModelComponent();
		//! loads a model into the component and triggers ModelSetEvent (removes old model if there is one)
		//! @remarks a mesh that fails to load logs an error and leaves the component empty
		void loadModel(String const & modelFileName);
		//! removes model and triggers ModelRemovedEvent
		void removeModel();
		//! @brief set the mesh REFERENCE by name (the reflected AssetRef setter):
		//! empty removes the model; a name loads it when the scene
		//! node exists, otherwise just records the reference (a detached load).
		//! Tolerant where loadModel asserts - so the property drive can set it.
		void setModelReference(String const & modelFileName);
		//! @brief set the `.omat` MATERIAL reference (the reflected AssetRef
		//! setter, @see class remarks): a name applies the material when a
		//! mesh is live (otherwise it is recorded and applied on load); empty
		//! clears it and reloads the mesh with its imported materials.
		void setMaterialReference(String const & materialFileName);

		//! @brief per-instance caster flag (reflected `castShadows`, default
		//! true; @see class remarks) - applies live to a loaded mesh
		void setCastShadows(bool casts);
		//! @see ModelComponent::mCastShadows
		inline bool getCastShadows() const;
		//! @brief per-instance receiver flag (reflected `receiveShadows`,
		//! default true; @see class remarks) - applies live to a loaded mesh
		void setReceiveShadows(bool receives);
		//! @see ModelComponent::mReceiveShadows
		inline bool getReceiveShadows() const;

		//! @brief runtime albedo tint (RGB multiplier, 1 1 1 = none; @see
		//! class remarks) - the Lua-friendly three-float form
		void setTint(float red, float green, float blue);
		//! @brief runtime additive emission (RGB, 0 0 0 = none - the
		//! hit-flash accent; @see class remarks)
		void setEmissiveBoost(float red, float green, float blue);
		//! @see ModelComponent::mTint
		inline Color const & getTint() const;
		//! @see ModelComponent::mEmissiveBoost
		inline Color const & getEmissiveBoost() const;
		//! reflected transient setters (Color -> the three-float forms)
		inline void setTintColor(Color const & tint)
		{
			this->setTint(tint.r, tint.g, tint.b);
		}
		inline void setEmissiveBoostColor(Color const & boost)
		{
			this->setEmissiveBoost(boost.r, boost.g, boost.b);
		}

		//! @see ModelComponent::modelFileName
		inline String const & getCurrentModelFileName() const;
		//! @see ModelComponent::modelAssetId
		inline String const & getModelAssetId() const;
		//! @see ModelComponent::materialFileName
		inline String const & getMaterialFileName() const;
		//! @see ModelComponent::materialAssetId
		inline String const & getMaterialAssetId() const;
		//! the facade mesh instance or NULL (@see ModelComponent::mesh)
		inline optr<MeshInstance> const & getMeshInstance() const;

	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! deactivated GameObjects hide their model
		virtual void onSetActive(bool activeInHierarchy);
		//! @brief read + parse the recorded `.omat` and render the live mesh
		//! with it (@see class remarks); no-op without a mesh or a reference.
		//! Any failure (missing/malformed asset, unsupported mesh) logs and
		//! leaves the mesh's current materials.
		void applyMaterial();
		//! @brief push the cast/receive flags onto the live mesh (no-op
		//! detached); runs after every model load and material apply, because
		//! a fresh material assignment resets the backend's receive state
		void applyShadowFlags();
		//! @brief push the runtime accents onto the live mesh (no-op
		//! detached); runs LAST - after every model/material/shadow-flag
		//! apply, because each of those resets the backend's accent state
		//! (@see MeshInstance::setTint)
		void applyAccents();
		//--- SERIALIZATION ---
		//! save the model + material file names (each with its stable asset
		//! id riding the record) to Archive
		virtual void save(optr<IArchive> const & ar);
		//! @brief load the model + material references from Archive and apply
		//! them; an asset id that resolves in the open project's AssetDatabase
		//! wins over a stale file name (rename survival)
		virtual void load(optr<IArchive> const & ar);
	private:
	};
	//---------------------------------------------------------------
	inline bool ModelComponent::getCastShadows() const
	{
		return this->mCastShadows;
	}
	//---------------------------------------------------------------
	inline bool ModelComponent::getReceiveShadows() const
	{
		return this->mReceiveShadows;
	}
	//---------------------------------------------------------------
	inline Color const & ModelComponent::getTint() const
	{
		return this->mTint;
	}
	//---------------------------------------------------------------
	inline Color const & ModelComponent::getEmissiveBoost() const
	{
		return this->mEmissiveBoost;
	}
	//---------------------------------------------------------------
	inline String const & ModelComponent::getCurrentModelFileName() const
	{
		return this->modelFileName;
	}
	//---------------------------------------------------------------
	inline String const & ModelComponent::getModelAssetId() const
	{
		return this->modelAssetId;
	}
	//---------------------------------------------------------------
	inline String const & ModelComponent::getMaterialFileName() const
	{
		return this->materialFileName;
	}
	//---------------------------------------------------------------
	inline String const & ModelComponent::getMaterialAssetId() const
	{
		return this->materialAssetId;
	}
	//---------------------------------------------------------------
	inline optr<MeshInstance> const & ModelComponent::getMeshInstance() const
	{
		return this->mesh;
	}
	//---------------------------------------------------------------
}

#endif //__ModelComponent_h__30_8_2010__17_37_08__
