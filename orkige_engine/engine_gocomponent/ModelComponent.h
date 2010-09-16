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

namespace Orkige
{
	//! handles 1 Model attached to a GameObject
	class ORKIGE_DLL ModelComponent : public GameObjectComponent
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
		Ogre::Entity*		model;				//!< the current model entity instance or NULL
		Ogre::SceneNode*	modelNode;			//!< SceneNode our Model gets attached to or NULL
		String				modelFileName;		//!< filename of the current model or empty String
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
		inline Ogre::Entity* getModel();
		//! @see ModelComponent::modelNode
		inline Ogre::SceneNode* getModelNode();

		//! get local position of ModelComponent::modelNode
		inline Ogre::Vector3 const & getLocalPosition() const;
		//! get local orientation of ModelComponent::modelNode
		inline Ogre::Quaternion const & getLocalOrientation() const;
		//! get local scale of ModelComponent::modelNode
		inline Ogre::Vector3 const & getLocalScale() const;
		//! set local position of ModelComponent::modelNode
		inline void setLocalPosition(Ogre::Vector3 const & position);
		//! set local scale of ModelComponent::modelNode
		inline void setLocalScale(Ogre::Vector3 const & scale);
		//! set local orientation of ModelComponent::modelNode
		inline void setLocalOrientation(Ogre::Quaternion const & orientation);
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
	inline Ogre::Entity* ModelComponent::getModel()
	{
		return this->model;
	}
	//---------------------------------------------------------------
	inline Ogre::SceneNode* ModelComponent::getModelNode()
	{
		return this->modelNode;
	}
	//---------------------------------------------------------------
	inline Ogre::Vector3 const & ModelComponent::getLocalPosition() const
	{
		oAssert(this->modelNode);
		return this->modelNode->getPosition();
	}
	//---------------------------------------------------------------
	inline Ogre::Quaternion const & ModelComponent::getLocalOrientation() const
	{
		oAssert(this->modelNode);
		return this->modelNode->getOrientation();
	}
	//---------------------------------------------------------------
	inline Ogre::Vector3 const & ModelComponent::getLocalScale() const
	{
		oAssert(this->modelNode);
		return this->modelNode->getScale();
	}
	//---------------------------------------------------------------
	inline void ModelComponent::setLocalPosition(Ogre::Vector3 const & position)
	{
		oAssert(this->modelNode);
		this->modelNode->setPosition(position);
	}
	//---------------------------------------------------------------
	inline void ModelComponent::setLocalScale(Ogre::Vector3 const & scale)
	{
		oAssert(this->modelNode);
		this->modelNode->setScale(scale);
	}
	//---------------------------------------------------------------
	inline void ModelComponent::setLocalOrientation(Ogre::Quaternion const & orientation)
	{
		oAssert(this->modelNode);
		this->modelNode->setOrientation(orientation);
	}
	//---------------------------------------------------------------
}

#endif //__ModelComponent_h__30_8_2010__17_37_08__
