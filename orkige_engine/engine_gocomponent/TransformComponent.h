/**************************************************************
	created:	2010/08/31 at 10:41
	filename: 	TransformComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __TransformComponent_h__31_8_2010__10_41_33__
#define __TransformComponent_h__31_8_2010__10_41_33__

#include "engine_module/EnginePrerequisites.h"
#include <core_game/GameObject.h>
#include "engine_util/SceneNodeGuard.h"

namespace Orkige
{
	//! @brief basic Transformation component for all GameObjects in 3D Space
	//! @remarks Phase A1 (Docs/render-abstraction.md, WP-A1.2): owns a facade
	//! RenderNode (via the reshaped SceneNodeGuard base) instead of a raw
	//! Ogre::SceneNode. The node carries `this` as its user pointer - ray
	//! query hits and editors resolve a node back to its component through
	//! RenderNode::findUserPointerUpwards (@see getComponentFromNode).
	class ORKIGE_ENGINE_DLL TransformComponent : public GameObjectComponent, public SceneNodeGuard
	{
		OOBJECT(TransformComponent,GameObjectComponent)
		//--- Types -------------------------------------------
	public:
	protected:
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
		//! get the TransformComponent a facade node belongs to, or NULL
		//! if traverseParents is true parents will be checked until a TransformComponent is found or the root is reached
		//! @remarks resolves through the RenderNode user pointer - within the
		//! engine ONLY TransformComponent tags scene nodes, so the cast is safe
		static TransformComponent* getComponentFromNode(optr<RenderNode> const & node, bool traverseParents = true);
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! re-parents all TransformComponents attached below my node to the world root
		void detachTransformComponents(optr<RenderNode> const & node, bool traverseChildren = true);
		//--- SERIALIZATION ---
		//! save position/orientation/scale to Archive
		virtual void save(optr<IArchive> const & ar);
		//! load position/orientation/scale from Archive
		virtual void load(optr<IArchive> const & ar);
	private:
	};

}

#endif //__TransformComponent_h__31_8_2010__10_41_33__
