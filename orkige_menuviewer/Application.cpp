/********************************************************************
	created:	2009/07/15 at 0:57
	filename: 	Application.cpp
	author:		MorrK
	
	purpose:	
*********************************************************************/
#include "Application.h"
#include "PreviewMenuState.h"
#include <core_game/GameStateManager.h>
#include <engine_gocomponent/TransformComponent.h>
#include <core_util/PlatformUtil.h>

#include "CcGuiFactory.h"
#include "CcFastGuiFactory.h"
#include "FileUtils.h"


namespace CC
{
	using namespace Orkige;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Application::Application(String const & _externalWindowHandle, Orkige::String const & _topLevelWindowHandle) 
		: ::Orkige::Application("data/Config/LogConfig.xml"), externalWindowHandle(_externalWindowHandle), topLevelWindowHandle(_topLevelWindowHandle)
	{
	}
	//---------------------------------------------------------
	Application::~Application()
	{
	}
	//---------------------------------------------------------
	bool Application::init(Orkige::String const & commandLine)
	{
		// set root resource directory as current
		FileUtils::SetCurrentPath(FileUtils::GetResourceDirectory().c_str());

		// parse command line 
		String filenameMenu;
		if (Ogre::StringUtil::endsWith(commandLine, ".menu"))
		{
			filenameMenu = commandLine;
		}
		String filenameResourceConfig;
		if (Ogre::StringUtil::endsWith(commandLine, ".cfg"))
		{
			filenameResourceConfig = commandLine;
		}

		Orkige::Application::init();
		this->localisation = onew(new Orkige::Localisation("en"));
//		this->settingsManager = onew(new SettingsManager(Orkige::PlatformUtil::getResourceDirectory() + "data/Config/game.cfg",
//			Orkige::PlatformUtil::getResourceDirectory() + "data/Config/levelCards.cfg"));
//		this->statisticsManager = onew(new StatisticsManager());
		this->gsm->registerState(onew(new PreviewMenuState("PreviewMenu", filenameMenu)));

		//initialize the timer system
		Timer::initialise();

		//create graphic system and Ogre root
		if (!filenameResourceConfig.empty())
			this->engine = onew(new Engine(Ogre::ST_GENERIC, filenameResourceConfig));
		else
			this->engine = onew(new Engine());

		this->staticPluginLoader.load();

		//load the game config and check in which mode app should be started
		bool editorMode = true;

		//default initialization when we aren't in "tool" mode
		if( !this->engine->setup(false, "Menu Viewer - press O for open, R for reload", this->externalWindowHandle, this->topLevelWindowHandle))
			return false;

		this->engine->createDefaultCameraAndViewport();

		this->fastGuiManager = onew(new FastGuiManager(onew(new CcFastGuiFactory())));

		

		//go into new scope before init loadingbar so it gets destroyed after loading process
		{
			//loadingBar.start();
			//loading resource after loadingbar is initialized so we can see our nice bar :)
			Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();

			//create console subsystem
// 			if(/*cfg->getEntry<bool>("App","IngameConsole")*/true)
// 			{
// 				this->ingameConsole = onew(new IngameConsole());
// 				this->ingameConsole->init();
// 			}

			//loadingBar.worldGeometryStageStarted("Initializing Inputsystem!");
			this->inputManager = onew(new InputManager(editorMode));
			
			//loadingBar.worldGeometryStageStarted("Initializing Sound!");
			this->soundManager = onew(new SoundManager(Engine::getSingleton().getCamera()));
			/*this->soundManager->initSound();*/
			this->soundManager->init();
			
			this->gem->trigger(Event("InitEngine"));

//			this->settingsManager->load();
//			this->settingsManager->loadLayout();
		}//finish loadingbar scope

#ifndef ORKIGE_IPHONE
		this->fastGuiManager->showCursor("fastgui_default", "mousepointer");
#endif
		this->fastGuiManager->enableInputEvents();
#ifdef ORKIGE_DEBUG
		this->fastGuiManager->showStats();
#endif

		SoundManager::getSingleton().createSound("click", "click.wav", false);

		this->gsm->setInitialState("PreviewMenu");


// 		if (SettingsManager::getSingleton().getSetting("EnableSound") == "no")
 		{
 			SoundManager::getSingleton().setMasterVolume(0);
 		}

		
		this->registerEvent(Orkige::Button::ButtonHitEvent,			&Application::onGuiEvent, this, 1);
		this->registerEvent(Orkige::CheckBox::CheckBoxToggledEvent,	&Application::onGuiEvent, this, 1);

		return true;
	}
	//---------------------------------------------------------
	bool Application::deinit()
	{
		this->unRegisterAllEvents();

		this->gem->trigger(Event("DeInitEngine"));

		this->gsm.reset();
		
		this->fastGuiManager.reset();

		this->ingameConsole.reset();

		this->engine.reset();
		
	
		return Orkige::Application::deinit();
	}
	//---------------------------------------------------------
	bool Application::run()
	{
		OPROFILEFUNC();
		if(!Orkige::Application::run())
			return false;

		this->engine->renderOneFrame();
#ifdef ORKIGE_DEBUG
		this->fastGuiManager->updateStats();
#endif
		this->soundManager->update();
		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool Application::onGuiEvent(Event const & e)
	{
		SoundManager::getSingleton().playSound("click");
		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
