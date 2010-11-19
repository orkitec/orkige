/**************************************************************
	created:	2010/08/31 at 0:23
	filename: 	NodeUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __NodeUtil_h__31_8_2010__0_23_43__
#define __NodeUtil_h__31_8_2010__0_23_43__

#include "engine_module/EnginePrerequisites.h"
#include "engine_gocomponent/TransformComponent.h"

namespace Orkige
{
	//! node utilities
	namespace NodeUtil
	{
		//! recursively removes and destroys all attached objects on a SceneNode and its children
		static inline void cleanSceneNode(Ogre::SceneNode* sceneNode)
		{
			oAssert(sceneNode);
			for(unsigned short i = sceneNode->numAttachedObjects(); i > 0; --i )
			{
				try
				{
					Ogre::MovableObject* mo = sceneNode->detachObject(i-1);
					sceneNode->getCreator()->destroyMovableObject(mo);
				}
				catch (...)
				{

				}

			}
			Ogre::Node::ChildNodeIterator it = sceneNode->getChildIterator();

			while(it.hasMoreElements())
			{
				Ogre::SceneNode* sn = static_cast<Ogre::SceneNode*>(it.peekNextValue());
				if(sn)
					cleanSceneNode(sn);

				it.moveNext();
			}
		}
		//---------------------------------------------------------
		//! completely destroys a SceneNode and all its attached childs and MovableOject's
		static inline void wipeSceneNode(Ogre::SceneNode* &sceneNode)
		{
			oAssert(sceneNode);
			cleanSceneNode(sceneNode);
			sceneNode->removeAndDestroyAllChildren();
			Ogre::SceneManager* sceneManager = sceneNode->getCreator();
			oAssert(sceneManager);
			sceneManager->destroySceneNode(sceneNode);
			sceneNode = NULL;
		}
		//---------------------------------------------------------
		//! get game object from given scene node
		//! only works for gameobjest with a TransFormComponent
		static inline GameObject* getGameObjectFromNode(Ogre::Node * node,  bool traverseParents = true)
		{
			oAssert(node);
			GameObject* go = NULL;
			TransformComponent *tc = TransformComponent::getComponentFromNode(node, traverseParents);
			if(tc)
			{
				go = tc->getComponentOwner();
			}
			return go;
		}
	}
}

#endif //__NodeUtil_h__31_8_2010__0_23_43__