/********************************************************************
	created:	Monday 2010/08/30 at 17:37
	filename: 	ModelComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __ModelComponent_h__30_8_2010__17_37_08__
#define __ModelComponent_h__30_8_2010__17_37_08__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_util/SceneNodeGuard.h"
#include "core_util/StringUtil.h"

namespace Orkige
{
	//! handles 1 Model attached to a GameObject
	class ORKIGE_DLL ModelComponent : public GameObjectComponent, public SceneNodeGuard
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
		Ogre::Entity*		model;					//!< the current model entity instance or NULL
		String				modelFileName;			//!< filename of the current model or empty String
		optr<StringUtil::StringObject> eventData;	//!< name of set or removed model
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		ModelComponent();
		//! destructor
		virtual ~ModelComponent();
		//! loads a model into the componentn and triggers ModelSetEvent (removes old model if there is one)
		void loadModel(String const & modelFileName);
		//! removes model and triggers ModelRemovedEvent
		void removeModel();
		
		//! @see ModelComponent::modelFileName
		inline String const & getCurrentModelFileName();
		//! @see ModelComponent::model
		inline Ogre::Entity * getModel() const;

	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
	private:
	};
	//---------------------------------------------------------------
	inline String const & ModelComponent::getCurrentModelFileName()
	{
		return this->modelFileName;
	}
	//---------------------------------------------------------------
	inline Ogre::Entity * ModelComponent::getModel() const 
	{
		return this->model;
	}
	//---------------------------------------------------------------
}

#endif //__ModelComponent_h__30_8_2010__17_37_08__
