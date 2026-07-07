/********************************************************************
	created:	Wednesday 2010/09/08 at 17:03
	filename: 	module.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

// Only the modules already ported to OGRE 14 are registered here.
// PythonScriptComponent is gone for good (ORKIGE_NOSCRIPT); a Lua script
// component returns on sol2 in Phase 2.
#include "engine_graphic/Engine.h"
#include "engine_graphic/IngameConsole.h"
#include "engine_gocomponent/SoundComponent.h"
#include "engine_gocomponent/CameraComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/AnimationComponent.h"
#include "engine_gocomponent/RigidBodyComponent.h"
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
	OSIMPLEEXPORT(Ogre::SceneNode,SceneNode)
	OSIMPLEEXPORT_END

	OSIMPLEEXPORT(Ogre::SceneManager,SceneManager)
		OFUNCIR(getSceneNode)
	OSIMPLEEXPORT_END

	OSIMPLEEXPORT(Ogre::Camera,Camera)
	OSIMPLEEXPORT_END
ORKIGE_MODULE_END
