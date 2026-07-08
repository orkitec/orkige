/**************************************************************
	created:	2010/09/08 at 20:40
	filename: 	EnginePrerequisites.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __EnginePrerequisites_h__8_9_2010__20_40_50__
#define __EnginePrerequisites_h__8_9_2010__20_40_50__

#ifdef WIN32
#	if defined( ORKIGE_STATIC )
#   	define ORKIGE_ENGINE_DLL
#   else
#      if defined( __MINGW32__ )
#			define ORKIGE_ENGINE_DLL
#		else
#			pragma warning( disable : 4251)
#			ifdef orkige_engine_EXPORTS
#				define ORKIGE_ENGINE_DLL __declspec( dllexport )
#			else
#				define ORKIGE_ENGINE_DLL __declspec( dllimport )
#			endif
#		endif
#	endif
#else // Linux / Mac OSX etc
#	define ORKIGE_ENGINE_DLL
#endif

#include "core_module/OrkigePrerequisites.h"
#include "core_base/Meta.h"
#include <core_debug/DisableMemoryManager.h>

#include <Ogre.h>
#include <OgreFontManager.h>
#include <OgreBorderPanelOverlayElement.h>
#include <OgreTextAreaOverlayElement.h>
#include <OgreExternalTextureSourceManager.h>
#include <OgreWireBoundingBox.h>

#ifdef ORKIGE_ENABLE_MYGUI
#	include <MyGUI.h>
#	include <MyGUI_OgrePlatform.h>
#	include <Common/MessageBox/MessageBox.h>
	namespace MyGUI
	{
		typedef Message* MessagePtr;
	}
#endif //ORKIGE_ENABLE_MYGUI

void ORKIGE_ENGINE_DLL init_module_orkige_engine(void);

#include <core_debug/EnableMemoryManager.h>
#include <core_debug/Profile.h>
#endif //__EnginePrerequisites_h__8_9_2010__20_40_50__

