/********************************************************************
	created:	2009/07/15 at 0:57
	filename: 	Application.cpp
	author:		MorrK
	
	purpose:	
*********************************************************************/
#include "Application.h"
//#include "cc_states/IngameState.h"
#include "PreviewMenuState.h"
//#include "cc_states/ModelViewerState.h"
//#include "cc_states/LevelPreviewState.h"
//#include "cc_states/LevelSummaryState.h"
#include <core_game/GameStateManager.h>
#include <engine_gocomponent/TransformComponent.h>
#include <core_util/PlatformUtil.h>

#include "cc_gui/CcGuiFactory.h"
#include "cc_gui/CcFastGuiFactory.h"
//#include "CcFastGuiFactory.h"


namespace CC
{
	using namespace Orkige;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Application::Application(String const & _externalWindowHandle, Orkige::String const & _topLevelWindowHandle) : ::Orkige::Application("data/Config/LogConfig.xml"), externalWindowHandle(_externalWindowHandle), topLevelWindowHandle(_topLevelWindowHandle)
	{
	}
	//---------------------------------------------------------
	Application::~Application()
	{
	}
	//---------------------------------------------------------
	bool Application::init()
	{		

		Orkige::Application::init();
		this->localisation = onew(new Orkige::Localisation("en"));
//		this->settingsManager = onew(new SettingsManager(Orkige::PlatformUtil::getResourceDirectory() + "data/Config/game.cfg",
//			Orkige::PlatformUtil::getResourceDirectory() + "data/Config/levelCards.cfg"));
//		this->statisticsManager = onew(new StatisticsManager());
//		this->gsm->registerState(onew(new IngameState("Ingame")));
		this->gsm->registerState(onew(new PreviewMenuState("MainMenu")));
//		this->gsm->registerState(onew(new ModelViewerState("ModelViewer")));
//		this->gsm->registerState(onew(new LevelPreviewState("LevelPreview")));
//		this->gsm->registerState(onew(new LevelSummaryState("LevelSummary")));

		//initialize the timer system
		Timer::initialise();

		//create graphic system and Ogre root
		this->engine = onew(new Engine());

		this->staticPluginLoader.load();

		//load the game config and check in which mode app should be started
		bool editorMode = true;//cfg->getEntry<bool>("App","OrkigeWorldEditor");

		//default initialization when we aren't in "tool" mode
		if( !this->engine->setup(false, "orkige 3D Engine RenderWindow", this->externalWindowHandle, this->topLevelWindowHandle))
			return false;

		this->engine->createDefaultCameraAndViewport();

		this->fastGuiManager = onew(new FastGuiManager(onew(new CcFastGuiFactory())));

		

		//go into new scope before init loadingbar so i gets destroyed after loading process
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

			//setup Compositors and Compositormanager
			//this->engine->enableCompositors();


			//loadingBar.worldGeometryStageStarted("Initializing Inputsystem!");
			this->inputManager = onew(new InputManager(editorMode));
			

			//loadingBar.worldGeometryStageStarted("Initializing Sound!");
//			this->soundManager = onew(new SoundManager(Engine::getSingleton().getCamera()));
			/*this->soundManager->initSound();*/
//			this->soundManager->init();
			
			this->gem->trigger(Event("InitEngine"));

//			this->collisionTools = onew(new CollisionTools());

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

		this->gsm->setState("MainMenu");


		SoundManager::getSingleton().createSound("click","click.wav", false);
// 		SoundManager::getSingleton().createSound("scrdjelly1","scrdjelly1.wav", false);
// 		SoundManager::getSingleton().createSound("scrdjelly2","scrdjelly2.wav", false);
// 		SoundManager::getSingleton().createSound("coinmed","coinmed.wav", false);
// 		SoundManager::getSingleton().createSound("pickcard","pickcard.wav", false);
// 		SoundManager::getSingleton().createSound("placecard","placecard.wav", false);
// 		SoundManager::getSingleton().createSound("plantturn","plantturn.wav", false);
// 		SoundManager::getSingleton().createSound("jellywin","jellywin.wav", false);
// 		SoundManager::getSingleton().createSound("jellylose","jellylose.wav", false);
// 		SoundManager::getSingleton().createSound("lever","lever.wav", false);
// 		SoundManager::getSingleton().createSound("opendoor","opendoor.wav", false);
// 		//SoundManager::getSingleton().createSound("jellyTheme1_retro","jellyTheme1_retro.wav", true);
// 		SoundManager::getSingleton().createSound("snakewhstle","snakewhstle.wav", false);
// 		SoundManager::getSingleton().createSound("speedup","speedup.wav", false);
// 		SoundManager::getSingleton().createSound("sheild","sheild.wav", false);
// 		SoundManager::getSingleton().createSound("collctvalerian","collctvalerian.wav", false);
// 		SoundManager::getSingleton().createSound("respawn","respawn.wav", false);

// 		if (SettingsManager::getSingleton().getSetting("EnableSound") == "no")
// 		{
// 			SoundManager::getSingleton().setMasterVolume(0);
// 		}

		
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