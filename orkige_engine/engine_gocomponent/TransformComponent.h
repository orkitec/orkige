/**************************************************************
	created:	2010/08/31 at 10:41
	filename: 	TransformComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __TransformComponent_h__31_8_2010__10_41_33__
#define __TransformComponent_h__31_8_2010__10_41_33__

#include "engine_module/EnginePrerequisites.h"
#include <core_game/GameObject.h>
#include "engine_util/SceneNodeGuard.h"

namespace Orkige
{
	//! basic Transformation component for all GameObjects in 3D Space
	class ORKIGE_ENGINE_DLL TransformComponent : public GameObjectComponent, public Ogre::Any, public SceneNodeGuard
	{
		OOBJECT(TransformComponent,GameObjectComponent)
		//--- Types -------------------------------------------
	public:
	protected:	
	private:
		//--- Variables ---------------------------------------
	public:
		static const String USEROBJECT_BINDING_KEY;	//!< @see Ogre::UserObjectBindings
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		TransformComponent();
		//! destructor
		virtual ~TransformComponent();
		//! get TransFormComponent from given Node* or NULL if this Node isn't associated with a TransformComponent
		//! if traverseParents is true also parents will be checked until a TransformComponent is found or the RootNode is reached
		static TransformComponent* getComponentFromNode(Ogre::Node const * node, bool traverseParents = true);
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! detaches all TransformComponents that are attached to my SceneNode
		void detachTransformComponents(const Ogre::Node* node, bool traverseChildren = true);
		//--- SERIALIZATION ---
		//! save position/orientation/scale to Archive
		virtual void save(optr<IArchive> const & ar);
		//! load position/orientation/scale from Archive
		virtual void load(optr<IArchive> const & ar);
	private:
	};

}

#endif //__TransformComponent_h__31_8_2010__10_41_33__
