/**************************************************************
	created:	2010/08/31 at 10:41
	filename: 	TransformComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __TransformComponent_h__31_8_2010__10_41_33__
#define __TransformComponent_h__31_8_2010__10_41_33__

#include "engine_module/EnginePrerequisites.h"
#include <core_game/GameObject.h>

namespace Orkige
{
	//! basic Transformation component for all GameObjects in 3D Space
	class ORKIGE_DLL TransformComponent : public GameObjectComponent , public Ogre::Any
	{
		OOBJECT(TransformComponent,GameObjectComponent)
		//--- Types -------------------------------------------
	public:
		static String AXES_MESH_FILENAME;	//!< name mesh that should be shown on TransformComponent::showAxes
		static String USER_BINDING_ID;		//!< @see Ogre::UserObjectBindings
	protected:
		Ogre::SceneNode* sceneNode;			//!< transform SceneNode
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		TransformComponent();
		//! destructor
		virtual ~TransformComponent();
		//! get the Transform SceneNode
		inline Ogre::SceneNode* getSceneNode() const;
		//! get position relative to the rootSceneNode
		inline Ogre::Vector3 const & getPosition() const;
		//! get position in the world
		inline Ogre::Vector3 const & getWorldPosition() const;
		//! get position relative to the rootSceneNode
		inline Ogre::Quaternion const & getOrientation() const;
		//! get the orientation independent from root
		inline Ogre::Quaternion const & getWorldOrientation() const;
		//! get scale
		inline Ogre::Vector3 const & getScale() const;
		//! set position
		inline void setPosition(Ogre::Vector3 const & position);
		//! set scale
		inline void setScale(Ogre::Vector3 const & scale);
		//! set orientation
		inline void setOrientation(Ogre::Quaternion const & orientation);

		//! show transform axes
		bool showAxes(bool show);
		//! are axes visible?
		bool axesVisible();
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
	private:
	};
	//---------------------------------------------------------
	inline Ogre::SceneNode* TransformComponent::getSceneNode() const
	{
		return this->sceneNode;
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 const & TransformComponent::getPosition() const
	{
		oAssert(this->sceneNode);
		return this->sceneNode->getPosition();
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 const & TransformComponent::getWorldPosition() const
	{
		oAssert(this->sceneNode);
		return this->sceneNode->_getDerivedPosition();
	}
	//---------------------------------------------------------
	inline Ogre::Quaternion const & TransformComponent::getOrientation() const
	{
		oAssert(this->sceneNode);
		return this->sceneNode->getOrientation();
	}
	//---------------------------------------------------------
	inline Ogre::Quaternion const & TransformComponent::getWorldOrientation() const
	{
		oAssert(this->sceneNode);
		return this->sceneNode->_getDerivedOrientation();
	}
	//---------------------------------------------------------
	inline Ogre::Vector3 const & TransformComponent::getScale() const
	{
		oAssert(this->sceneNode);
		return this->sceneNode->getScale();
	}
	//---------------------------------------------------------
	inline void TransformComponent::setPosition(Ogre::Vector3 const & position)
	{
		oAssert(this->sceneNode);
		this->sceneNode->setPosition(position);
	}
	//---------------------------------------------------------
	inline void TransformComponent::setScale(Ogre::Vector3 const & scale)
	{
		oAssert(this->sceneNode);
		this->sceneNode->setScale(scale);
	}
	//---------------------------------------------------------
	inline void TransformComponent::setOrientation(Ogre::Quaternion const & orientation)
	{
		oAssert(this->sceneNode);
		this->sceneNode->setOrientation(orientation);
	}
	//---------------------------------------------------------
}

#endif //__TransformComponent_h__31_8_2010__10_41_33__
