/********************************************************************
	created:	Wednesday 2010/09/08 at 17:03
	filename: 	module.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_gui/IngameConsole.h"
#include "engine_gocomponent/PythonScriptComponent.h"
#include "engine_gocomponent/SoundComponent.h"

#include "engine_gocomponent/CameraComponent.h"
#include "engine_gocomponent/TransformComponent.h"

#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/AnimationComponent.h"
#include "engine_input/InputManager.h"
#include "engine_sound/SoundManager.h"
#include "engine_sound/SoundSource.h"
#include "engine_graphic/Engine.h"
#include "engine_graphic/ColoredBoundingBox.h"

#include "engine_gui/GuiManager.h"

using namespace Orkige;

ORKIGE_MODULE(orkige_engine)
	OEXPORT(TransformComponent)
	OEXPORT(ModelComponent)
	OEXPORT(AnimationComponent)
#ifndef ORKIGE_NOSCRIPT
	OEXPORT(PythonScriptComponent)
#endif
	OEXPORT(CameraComponent)
	OEXPORT(SoundComponent)
	OEXPORT(IngameConsole)
	OEXPORTMAP(StringGameObjectMap,Orkige::String,optr<Orkige::GameObject>)
	OEXPORT(Engine)
	OEXPORT(FrameEventData)

	//Exposing some Ogre internals
	OSIMPLEEXPORT(Ogre::SceneNode,SceneNode)
	OSIMPLEEXPORT_END

	OSIMPLEEXPORT(Ogre::SceneManager,SceneManager)
		OFUNCIR(getSceneNode)
	OSIMPLEEXPORT_END

	OSIMPLEEXPORT(Ogre::Camera,Camera)
	OSIMPLEEXPORT_END
	
	OEXPORT(SoundManager)
	OEXPORT(SoundSource)
	
	OEXPORT(KeyEventData)
	OEXPORT(MouseEventData)
	OEXPORT(AccelerationEventData)
	OEXPORT(TouchEventData)
	OEXPORT(GestureEventData)
	OEXPORT(InputManager)

	OEXPORT(GuiManager)
	OEXPORT(GuiFactory)
	OEXPORT(IGuiObject)
	OEXPORT(Widget)
	OEXPORT(Dialog)
	OEXPORT(OkDialog)
	OEXPORT(YesNoDialog)
	OEXPORT(Button)
	OEXPORT(TextBox)
	OEXPORT(SelectMenu)
	OEXPORT(Label)
	OEXPORT(Separator)
	OEXPORT(Slider)
	OEXPORT(ParamsPanel)
	OEXPORT(CheckBox)
	OEXPORT(DecorWidget)
	OEXPORT(ProgressBar)
ORKIGE_MODULE_END
