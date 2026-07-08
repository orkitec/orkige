/**************************************************************
	created:	2010/08/31 at 0:23
	filename: 	NodeUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __NodeUtil_h__31_8_2010__0_23_43__
#define __NodeUtil_h__31_8_2010__0_23_43__

#include "engine_module/EnginePrerequisites.h"
// TODO(Phase 1): engine_gocomponent is not ported yet; getGameObjectFromNode()
// returns together with TransformComponent (see ORKIGE_ENGINE_HAS_GOCOMPONENT below).
#ifdef ORKIGE_ENGINE_HAS_GOCOMPONENT
#include "engine_gocomponent/TransformComponent.h"
#endif

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
			// OGRE 14: getChildIterator() is gone, getChildren() returns the child list
			for(Ogre::Node* child : sceneNode->getChildren())
			{
				Ogre::SceneNode* sn = static_cast<Ogre::SceneNode*>(child);
				if(sn)
					cleanSceneNode(sn);
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
#ifdef ORKIGE_ENGINE_HAS_GOCOMPONENT
		//! get game object from given scene node
		//! only works for GameObjects with a TransformComponent
		//! @see TransformComponent::getComponentFromNode
		static inline GameObject* getGameObjectFromNode(Ogre::Node const * node,  bool traverseParents = true)
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
#endif //ORKIGE_ENGINE_HAS_GOCOMPONENT
	}
}

#endif //__NodeUtil_h__31_8_2010__0_23_43__