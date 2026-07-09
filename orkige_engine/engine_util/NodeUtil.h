/**************************************************************
	created:	2010/08/31 at 0:23
	filename: 	NodeUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __NodeUtil_h__31_8_2010__0_23_43__
#define __NodeUtil_h__31_8_2010__0_23_43__

#include "engine_module/EnginePrerequisites.h"
#include "engine_render/RenderNode.h"
// TODO(Phase 1): engine_gocomponent is not ported yet; getGameObjectFromNode()
// returns together with TransformComponent (see ORKIGE_ENGINE_HAS_GOCOMPONENT below).
#ifdef ORKIGE_ENGINE_HAS_GOCOMPONENT
#include "engine_gocomponent/TransformComponent.h"
#endif

namespace Orkige
{
	//! @brief node utilities - the node -> GameObject back-mapping
	//! @remarks the historical
	//! cleanSceneNode/wipeSceneNode destroy chains are GONE - facade
	//! RenderNode/MeshInstance/SpriteQuad handles are RAII, dropping the optr
	//! detaches and destroys the backend object (recoverable from git).
	namespace NodeUtil
	{
#ifdef ORKIGE_ENGINE_HAS_GOCOMPONENT
		//! get the GameObject a facade node belongs to (via the node
		//! user pointer a TransformComponent tags its node with)
		//! only works for GameObjects with a TransformComponent
		//! @see TransformComponent::getComponentFromNode
		static inline GameObject* getGameObjectFromNode(optr<RenderNode> const & node, bool traverseParents = true)
		{
			oAssert(node);
			GameObject* go = NULL;
			TransformComponent* tc = TransformComponent::getComponentFromNode(node, traverseParents);
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
