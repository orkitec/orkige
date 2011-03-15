/********************************************************************
	created:	2009/07/15 at 0:56
	filename: 	Application.h
	author:		MorrK
	
	purpose:	
*********************************************************************/
#ifndef __Application_h__15_7_2009__0_56_19__
#define __Application_h__15_7_2009__0_56_19__
#include <OgreBuildSettings.h>
#ifdef OGRE_STATIC_LIB
#ifdef ORKIGE_GLESRS
#  define OGRE_STATIC_GLES 1
#else
#  define OGRE_STATIC_GL
#endif
#ifdef WIN32
#    define OGRE_STATIC_Direct3D9
#  endif
//#  define OGRE_STATIC_BSPSceneManager
#  define OGRE_STATIC_ParticleFX
//#  define OGRE_STATIC_CgProgramManager
#  ifdef OGRE_USE_PCZ
#    define OGRE_STATIC_PCZSceneManager
#    define OGRE_STATIC_OctreeZone
#  else
#    define OGRE_STATIC_OctreeSceneManager
#  endif
#ifdef ORKIGE_IPHONE
#  if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
//#     undef OGRE_STATIC_CgProgramManager
#     undef OGRE_STATIC_GL
#     define OGRE_STATIC_GLES 1
#     ifdef __OBJC__
#       import <UIKit/UIKit.h>
#     endif
#  endif
#endif
#endif

#include <core_game/Application.h>
#include "OgreStaticPluginLoader.h"
#include <core_debug/LogManager.h>
#include <core_util/Timer.h>
#include <core_game/GameObjectManager.h>
#include <core_game/GameStateManager.h>

#include <engine_graphic/Engine.h>

#include <engine_input/InputManager.h>
#include <engine_graphic/Engine.h>
#include <engine_sound/SoundManager.h>
#include <engine_gui/IngameConsole.h>
#include <engine_physic/CollisionTools.h>
#include <engine_fastgui/FastGuiManager.h>
#include <engine_base/Localisation.h>
#include <engine_util/StringUtil.h>
//#include "cc_game/SettingsManager.h"
//#include "cc_game/StatisticsManager.h"


namespace CC
{
	
	class Application : public Orkige::Application, Orkige::EventHandler
	{
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::StaticPluginLoader		staticPluginLoader;


		optr<Orkige::Engine>			engine;
		optr<Orkige::InputManager>		inputManager;
		optr<Orkige::SoundManager>		soundManager;
		optr<Orkige::IngameConsole>		ingameConsole;
		optr<Orkige::FastGuiManager>	fastGuiManager;
//		optr<Orkige::CollisionTools>	collisionTools;
//		optr<SettingsManager>			settingsManager;
//		optr<StatisticsManager>			statisticsManager;
		optr<Orkige::Localisation>		localisation;
		Orkige::String					externalWindowHandle;
		Orkige::String					topLevelWindowHandle;
	private:
		//--- Methods -----------------------------------------------
	public:
		Application(Orkige::String const & externalWindowHandle = Orkige::StringUtil::BLANK, Orkige::String const & topLevelWindowHandle = Orkige::StringUtil::BLANK);
		virtual ~Application();

		bool init(Orkige::String const & sCommandLine);
		bool deinit();
		virtual bool run();
	protected:
		//! react on gui events an play sounds
		bool onGuiEvent(Orkige::Event const & e);
	private:

	};
	//---------------------------------------------------------------
}

#endif //__Application_h__15_7_2009__0_56_19__