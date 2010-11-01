/**************************************************************
	created:	2010/08/31 at 10:44
	filename: 	TransformComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_gocomponent/TransformComponent.h"
#include "engine_module/EnginePrerequisites.h"
#include "engine_graphic/Engine.h"

using namespace Ogre;
namespace Orkige
{
	String TransformComponent::AXES_MESH_FILENAME = "axes.mesh";
	String TransformComponent::USER_BINDING_ID = "TransformComponent";
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	TransformComponent::TransformComponent()
	{
		this->sceneNode = NULL;
	}
	//---------------------------------------------------------
	TransformComponent::~TransformComponent()
	{
		this->sceneNode = NULL;
	}
	//---------------------------------------------------------
	bool TransformComponent::axesVisible()
	{
		if(!this->sceneNode)
			return false;

		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		String const & componentOwnerObjectId = componentOwner->getObjectID();
		oAssert(!componentOwnerObjectId.empty());
		String axesEntityName = componentOwnerObjectId + "_Axes";
		try
		{
			this->sceneNode->getChild(axesEntityName);
			return true;
		}
		catch (Ogre::ItemIdentityException)
		{
			return false;
		}
	}
	//---------------------------------------------------------
	bool TransformComponent::showAxes(bool show)
	{
		oAssert(this->sceneNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		String const & componentOwnerObjectId = componentOwner->getObjectID();
		oAssert(!componentOwnerObjectId.empty());
		String axesEntityName = componentOwnerObjectId + "_Axes";

		if(show)
		{
			try
			{
				this->sceneNode->getChild(axesEntityName);

				//node already exists
				return false;
			}
			catch (Ogre::ItemIdentityException)
			{
			}

			Ogre::Entity* entity = Engine::getSingleton().getSceneManager()->createEntity( axesEntityName, AXES_MESH_FILENAME );
			oAssert(entity);
			entity->setUserAny(*this);
			Ogre::SceneNode* meshNode = this->sceneNode->createChildSceneNode(axesEntityName);
			oAssert(meshNode);
			meshNode->attachObject( entity );
		}
		else
		{
			try
			{
				Ogre::SceneNode* meshNode = static_cast<Ogre::SceneNode*>(this->sceneNode->getChild(axesEntityName));
				oAssert(meshNode);
				Ogre::Entity* entity = static_cast<Ogre::Entity*>(meshNode->getAttachedObject(axesEntityName));
				oAssert(entity);
				Engine::getSingletonPtr()->getSceneManager()->destroyEntity(entity);
				this->sceneNode->removeAndDestroyChild(axesEntityName);
			}
			catch (Ogre::ItemIdentityException)
			{
				return false;
			}
		}
		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void TransformComponent::onAdd()
	{
		oAssert(!this->sceneNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		String const & componentOwnerObjectId = componentOwner->getObjectID();
		oAssert(!componentOwnerObjectId.empty());
		this->sceneNode = Engine::getSingletonPtr()->getSceneManager()->getRootSceneNode()->createChildSceneNode( componentOwnerObjectId );
		
		this->sceneNode->getUserObjectBindings().setUserAny(TransformComponent::USER_BINDING_ID ,Ogre::Any(this));
		

		//oAssert(!this->sceneNode->getUserAny().isEmpty());
		
	}
	//---------------------------------------------------------
	void TransformComponent::onRemove()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		String const & componentOwnerObjectId = componentOwner->getObjectID();
		oAssert(!componentOwnerObjectId.empty());
		this->sceneNode->removeAndDestroyAllChildren();
		Engine::getSingletonPtr()->getSceneManager()->destroySceneNode(componentOwnerObjectId);
		this->sceneNode = NULL;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(TransformComponent)
		GAMEOBJECTCOMPONENT()
		OFUNCIR(getPosition)
		OFUNCIR(getScale)
		OFUNCIR(getOrientation)
		OFUNC(setPosition)
		OFUNC(setScale)
		OFUNC(setOrientation)
		OFUNC(showAxes)
		OFUNC(axesVisible)
	OOBJECT_END
}
