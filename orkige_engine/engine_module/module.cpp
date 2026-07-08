/********************************************************************
	created:	Wednesday 2010/09/08 at 17:03
	filename: 	module.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

// Only the modules already ported to OGRE 14 are registered here.
// PythonScriptComponent is gone for good (ORKIGE_NOSCRIPT); its successor is
// the sol2-based ScriptComponent.
#include "engine_graphic/Engine.h"
#include "engine_graphic/IngameConsole.h"
#include "engine_gocomponent/SoundComponent.h"
#include "engine_gocomponent/CameraComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/AnimationComponent.h"
#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_gocomponent/ScriptComponent.h"
#include "engine_physic/PhysicsWorld.h"
#include "engine_input/InputManager.h"
#include "engine_sound/SoundManager.h"
#include "engine_fastgui/IGuiObject.h"
#include "engine_fastgui/FastGuiManager.h"

using namespace Orkige;

ORKIGE_MODULE(orkige_engine)
	OEXPORT(TransformComponent)
	OEXPORT(ModelComponent)
	OEXPORT(AnimationComponent)
	OEXPORT(CameraComponent)
	OEXPORT(SoundComponent)
	OEXPORT(RigidBodyComponent)
	OEXPORT(ScriptComponent)
	OEXPORTMAP(StringGameObjectMap,Orkige::String,optr<Orkige::GameObject>)

	OEXPORT(Engine)
	OEXPORT(FrameEventData)
	OEXPORT(IngameConsole)

	OEXPORT(KeyEventData)
	OEXPORT(MouseEventData)
	OEXPORT(AccelerationEventData)
	OEXPORT(TouchEventData)
	OEXPORT(GestureEventData)
	OEXPORT(InputManager)

	OEXPORT(PhysicsWorld)
	OEXPORT(SoundManager)
	OEXPORT(SoundSource)

	// PhysicsWorld::castRayHit answers with this plain value type
	OSIMPLEEXPORT(Orkige::PhysicsWorld::RayHit,RayHit)
		OCONSTVAR(hit)
		OCONSTVAR(position)
		OCONSTVAR(bodyId)
	OSIMPLEEXPORT_END

	OEXPORT(IGuiObject)
	OEXPORT(FastGuiWidget)
	OEXPORT(FastGuiView)
	OEXPORT(FastGuiTextbox)
	OEXPORT(FastGuiSlider)
	OEXPORT(FastGuiSelectMenu)
	OEXPORT(FastGuiProgressBar)
	OEXPORT(FastGuiManager)
	OEXPORT(FastGuiLabel)
	OEXPORT(FastGuiDragDropButton)
	OEXPORT(FastGuiDecorWidget)
	OEXPORT(FastGuiCheckBox)
	OEXPORT(FastGuiButtonBlink)
	OEXPORT(FastGuiButton)
	OEXPORT(DragEventData)

	//Exposing some Ogre internals
	// the math value types scripts actually compute with; construction is
	// Vector3(x,y,z) / Quaternion(w,x,y,z), members are plain fields.
	// x/y/z (and cross/dot) physically live on Ogre's VectorBase - the base
	// must be registered for sol2 to resolve them (see OSIMPLEEXPORT_BASED);
	// the typedef exists because macro arguments cannot carry the comma
	using OgreVector3Base [[maybe_unused]] = Ogre::VectorBase<3, Ogre::Real>;
	OSIMPLEEXPORT_BASED(Ogre::Vector3,OgreVector3Base,Vector3)
		OCONSTRUCTOR3(float,float,float)
		OVAR(x)
		OVAR(y)
		OVAR(z)
		OFUNC(length)
		OFUNC(distance)
		OFUNC(squaredDistance)
		OFUNC(dotProduct)
		OFUNC(crossProduct)
		OFUNC(normalisedCopy)
	OSIMPLEEXPORT_END

	OSIMPLEEXPORT(Ogre::Quaternion,Quaternion)
		OCONSTRUCTOR4(float,float,float,float)
		OVAR(w)
		OVAR(x)
		OVAR(y)
		OVAR(z)
	OSIMPLEEXPORT_END

	// enough SceneNode to drive a camera from a script (position + lookAt;
	// Lua passes lookAt's THREE arguments - no default args across the
	// binding: node:lookAt(target, SceneNode.TransformSpace.TS_WORLD,
	// Vector3(0, 0, -1))); position lives on Ogre::Node - base registered
	OSIMPLEEXPORT_BASED(Ogre::SceneNode,Ogre::Node,SceneNode)
		OFUNCOVERL(setPosition, void (Ogre::Node::*)(Ogre::Vector3 const &))
		OFUNCIR(getPosition)
		OFUNC(lookAt)
		OENUM_START(TransformSpace)
			OENUM_VALUE(TS_LOCAL)
			OENUM_VALUE(TS_PARENT)
			OENUM_VALUE(TS_WORLD)
		OENUM_END
	OSIMPLEEXPORT_END

	OSIMPLEEXPORT(Ogre::SceneManager,SceneManager)
		OFUNCIR(getSceneNode)
	OSIMPLEEXPORT_END

	// getParentSceneNode lives on Ogre::MovableObject - base registered
	OSIMPLEEXPORT_BASED(Ogre::Camera,Ogre::MovableObject,Camera)
		OFUNC(getParentSceneNode)
	OSIMPLEEXPORT_END
ORKIGE_MODULE_END
