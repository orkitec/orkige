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
	//! @remarks Phase A1 (Docs/render-abstraction.md, WP-A1.2): owns a facade
	//! MeshInstance on an owned child node of the sibling TransformComponent.
	//! The historical shareSkeletonInstance flag of loadModel was dropped -
	//! Ogre skeleton-instance sharing has no facade shape and no caller
	//! (recoverable from git if a real need returns).
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
		//! @brief set the mesh REFERENCE by name (the reflected AssetRef setter,
		//! task #94 P2): empty removes the model; a name loads it when the scene
		//! node exists, otherwise just records the reference (a detached load).
		//! Tolerant where loadModel asserts - so the property drive can set it.
		void setModelReference(String const & modelFileName);

		//! @see ModelComponent::modelFileName
		inline String const & getCurrentModelFileName() const;
		//! @see ModelComponent::modelAssetId
		inline String const & getModelAssetId() const;
		//! the facade mesh instance or NULL (@see ModelComponent::mesh)
		inline optr<MeshInstance> const & getMeshInstance() const;

	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! deactivated GameObjects hide their model
		virtual void onSetActive(bool activeInHierarchy);
		//--- SERIALIZATION ---
		//! save the model file name (plus its stable asset id as the assetId
		//! attribute) to Archive
		virtual void save(optr<IArchive> const & ar);
		//! @brief load the model file name from Archive and load the model; a
		//! mesh asset id that resolves in the open project's AssetDatabase
		//! wins over a stale file name (rename survival)
		virtual void load(optr<IArchive> const & ar);
	private:
	};
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
	inline optr<MeshInstance> const & ModelComponent::getMeshInstance() const
	{
		return this->mesh;
	}
	//---------------------------------------------------------------
}

#endif //__ModelComponent_h__30_8_2010__17_37_08__
