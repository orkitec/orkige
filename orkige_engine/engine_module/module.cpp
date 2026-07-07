/********************************************************************
	created:	Wednesday 2010/09/08 at 17:03
	filename: 	module.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

// Only the modules already ported to OGRE 14 are registered here; the other
// exports (gocomponents, input, sound, fastgui, ...) return as their modules
// are ported.
#include "engine_graphic/Engine.h"

using namespace Orkige;

ORKIGE_MODULE(orkige_engine)
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
ORKIGE_MODULE_END
