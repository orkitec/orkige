/**************************************************************
	created:	2010/08/31 at 0:49
	filename: 	SerializationUtil.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_util/SerializationUtil.h"
#include "engine_gocomponent/TransformComponent.h"

namespace Orkige
{
	namespace SerializationUtil
	{
		//---------------------------------------------------------
		//- SAVE --------------------------------------------------
		//---------------------------------------------------------
		void saveSceneNode(Ogre::SceneNode * node, optr<IArchive> const & ar, const unsigned int file_version)
		{
			String nodeName = node->getName();
			Ogre::Vector3 nodePosition = node->getPosition();
			Ogre::Quaternion nodeOrientation = node->getOrientation();
			Ogre::Vector3 nodeScale = node->getScale();
			unsigned long numAttachedObjects = node->numAttachedObjects();
			unsigned long numChildren = node->numChildren();
			ar << nodeName;
			ar << nodePosition;
			ar << nodeOrientation;
			ar << nodeScale;
			ar << numAttachedObjects;

			for(unsigned short i=0;i<numAttachedObjects;i++)
			{
				Ogre::MovableObject * attachedObject = node->getAttachedObject(i);
				saveMoveAbleObject(attachedObject,ar,file_version);
			}

			ar << numChildren;
			for(unsigned short i=0;i<numChildren;i++)
			{
				Ogre::SceneNode * childNode = static_cast<Ogre::SceneNode*>(node->getChild(i));
				oAssert(childNode);
				saveSceneNode(childNode,ar,file_version);
			}
		}
		//---------------------------------------------------------
		void saveMoveAbleObject(Ogre::MovableObject * object, optr<IArchive> const & ar, const unsigned int file_version)
		{
			String type = object->getMovableType();
			ar << type;
			if(type == "Entity")
			{
				Ogre::Entity * entity = static_cast<Ogre::Entity*>(object);
				oAssert(entity);
				saveEntity(entity,ar,file_version);
			} 
			else if(type == "Light")
			{
				Ogre::Light * light = static_cast<Ogre::Light*>(object);
				oAssert(light);
				saveLight(light,ar,file_version);
			}
		}
		//---------------------------------------------------------
		void saveEntity(Ogre::Entity * entity, optr<IArchive> const & ar, const unsigned int file_version)
		{
			String entityName = entity->getName();
			String entityMeshFileName = entity->getMesh()->getName();

			bool entityCastShadows = entity->getCastShadows();
			bool entityIsVisible = entity->getVisible();

			ar << entityName;
			ar << entityMeshFileName;
			ar << entityCastShadows;
			ar << entityIsVisible;

			bool hasGameObjectUserObject = false;
			Ogre::Any const & userObject = entity->getUserObjectBindings().getUserAny(TransformComponent::USEROBJECT_BINDING_KEY);
			optr<const TransformComponent> userGameObject;
			if( !userObject.isEmpty() )
			{
				const TransformComponent * tempGo = Ogre::any_cast< TransformComponent* >(userObject);
				if(tempGo)
				{
					hasGameObjectUserObject = true;	
					userGameObject = oBadPointer(tempGo);
				}
				else
				{
					oAssert(!"UserObjects other than Gameobjects can not be seriealized!");
				}
			}
			ar << hasGameObjectUserObject;
			if(hasGameObjectUserObject)
				ar << userGameObject;
		}
		//---------------------------------------------------------
		void saveLight(Ogre::Light * light, optr<IArchive> const & ar, const unsigned int file_version)
		{
			String lightName = light->getName();
			Ogre::Light::LightTypes lightType = light->getType();

			Ogre::ColourValue lightDiffuseColour = light->getDiffuseColour();
			Ogre::ColourValue lightSpecularColour = light->getSpecularColour();

			Ogre::Real lightAttenuationRange = light->getAttenuationRange();
			Ogre::Real lightAttenuationConstant = light->getAttenuationConstant();
			Ogre::Real lightAttenuationLinear = light->getAttenuationLinear();
			Ogre::Real lightAttenuationQuadric = light->getAttenuationQuadric();				

			Ogre::Real spotlightInnerAngle = light->getSpotlightInnerAngle().valueRadians();
			Ogre::Real spotlightOuterAngle = light->getSpotlightOuterAngle().valueRadians();
			Ogre::Real spotlightFalloff = light->getSpotlightFalloff();
			Ogre::Real lightPowerScale = light->getPowerScale();

			bool lightCastShadows = light->getCastShadows();

			ar << lightName;
			ar << lightType;
			ar << lightDiffuseColour.r;
			ar << lightDiffuseColour.g;
			ar << lightDiffuseColour.b;
			ar << lightDiffuseColour.a;
			ar << lightSpecularColour.r;
			ar << lightSpecularColour.g;
			ar << lightSpecularColour.b;
			ar << lightSpecularColour.a;
			ar << lightAttenuationRange;
			ar << lightAttenuationConstant;
			ar << lightAttenuationLinear;
			ar << lightAttenuationQuadric;

			ar << spotlightInnerAngle;
			ar << spotlightOuterAngle;
			ar << spotlightFalloff;
			ar << lightPowerScale;

			ar << lightCastShadows;
		}
		//---------------------------------------------------------
		//- LOAD --------------------------------------------------
		//---------------------------------------------------------
		Ogre::SceneNode * loadSceneNode(Ogre::SceneNode * parentSceneNode, optr<IArchive> const & ar, const unsigned int file_version,String const & userAnyId, Ogre::Any * userObject)
		{
			String nodeName;
			Ogre::Vector3 nodePosition;
			Ogre::Quaternion nodeOrientation;
			Ogre::Vector3 nodeScale;
			unsigned long numAttachedObjects;
			unsigned long numChildren;
			ar >> nodeName;
			ar >> nodePosition;
			ar >> nodeOrientation;
			ar >> nodeScale;
			ar >> numAttachedObjects;

			Ogre::SceneNode * childSceneNode = parentSceneNode->createChildSceneNode( nodeName );
			childSceneNode->setPosition(nodePosition);
			childSceneNode->setOrientation(nodeOrientation);
			childSceneNode->setScale(nodeScale);

			for(unsigned long i=0;i<numAttachedObjects;i++)
			{
				Ogre::MovableObject* object = loadMoveAbleObject(childSceneNode,ar,file_version);
				if(object!=NULL && userObject!=NULL)
					object->getUserObjectBindings().setUserAny(userAnyId, *userObject);
			}

			ar >> numChildren;
			for(unsigned long i=0;i<numChildren;i++)
			{
				loadSceneNode(childSceneNode,ar,file_version);
			}

			return childSceneNode;
		}
		//---------------------------------------------------------
		Ogre::MovableObject* loadMoveAbleObject(Ogre::SceneNode * parentSceneNode, optr<IArchive> const & ar, const unsigned int file_version)
		{
			String type;
			ar >> type;
			if(type == "Entity")
			{
				return loadEntity(parentSceneNode,ar,file_version);
			} 
			else if(type == "Light")
			{
				return loadLight(parentSceneNode,ar,file_version);
			}
			return NULL;
		}
		//---------------------------------------------------------
		Ogre::Entity* loadEntity(Ogre::SceneNode * parentSceneNode, optr<IArchive> const & ar, const unsigned int file_version)
		{
			String entityName;
			String entityMeshFileName;
			bool entityCastShadows;
			bool entityIsVisible;
			ar >> entityName;
			ar >> entityMeshFileName;
			ar >> entityCastShadows;
			ar >> entityIsVisible;
			bool hasGameObjectUserObject = false;
			ar >> hasGameObjectUserObject;

			Ogre::Entity* entity = parentSceneNode->getCreator()->createEntity(entityName, entityMeshFileName);
			if(hasGameObjectUserObject)
			{
				optr<TransformComponent> userGameObject;
				ar >> userGameObject;
				entity->getUserObjectBindings().setUserAny(TransformComponent::USEROBJECT_BINDING_KEY, Ogre::Any(userGameObject.get()));
			}
			entity->setCastShadows(entityCastShadows);
			entity->setVisible(entityIsVisible);
			parentSceneNode->attachObject( entity );
			return entity;
		}
		//---------------------------------------------------------
		Ogre::Light* loadLight(Ogre::SceneNode * parentSceneNode, optr<IArchive> const & ar, const unsigned int file_version)
		{
			String lightName;
			Ogre::Light::LightTypes lightType;

			Ogre::ColourValue lightDiffuseColour;
			Ogre::ColourValue lightSpecularColour;

			Ogre::Real lightAttenuationRange;
			Ogre::Real lightAttenuationConstant;
			Ogre::Real lightAttenuationLinear;
			Ogre::Real lightAttenuationQuadric;

			Ogre::Real spotlightInnerAngle;
			Ogre::Real spotlightOuterAngle;
			Ogre::Real spotlightFalloff;
			Ogre::Real lightPowerScale;

			bool lightCastShadows;

			ar >> lightName;
			ar >> lightType;
			ar >> lightDiffuseColour.r;
			ar >> lightDiffuseColour.g;
			ar >> lightDiffuseColour.b;
			ar >> lightDiffuseColour.a;
			ar >> lightSpecularColour.r;
			ar >> lightSpecularColour.g;
			ar >> lightSpecularColour.b;
			ar >> lightSpecularColour.a;
			ar >> lightAttenuationRange;
			ar >> lightAttenuationConstant;
			ar >> lightAttenuationLinear;
			ar >> lightAttenuationQuadric;

			ar >> spotlightInnerAngle;
			ar >> spotlightOuterAngle;
			ar >> spotlightFalloff;
			ar >> lightPowerScale;

			ar >> lightCastShadows;

			Ogre::Light* light = parentSceneNode->getCreator()->createLight(lightName);
			parentSceneNode->attachObject( light );
			light->setType(lightType);

			light->setDiffuseColour(lightDiffuseColour);
			light->setSpecularColour(lightSpecularColour);

			light->setAttenuation(lightAttenuationRange, lightAttenuationConstant, lightAttenuationLinear, lightAttenuationQuadric);

			light->setSpotlightInnerAngle(Ogre::Radian(spotlightInnerAngle));
			light->setSpotlightOuterAngle(Ogre::Radian(spotlightOuterAngle));
			light->setSpotlightFalloff(spotlightFalloff);
			light->setPowerScale(lightPowerScale);

			light->setCastShadows(lightCastShadows);
			return light;
		}
		//---------------------------------------------------------
	}
}
