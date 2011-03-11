/**************************************************************
	created:	2010/07/19 at 23:44
	filename: 	PreviewMenuState.cpp
	author:		MorrK
	
	purpose:	
***************************************************************/

#include "PreviewMenuState.h"
#include <engine_input/InputManager.h>
#include <core_game/Application.h>
#include <engine_graphic/Engine.h>
#include <engine_gui/IngameConsole.h>
#include <engine_fastgui/FastGuiManager.h>
#include "FileUtils.h"


namespace CC
{
	using namespace Orkige;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	PreviewMenuState::PreviewMenuState(Orkige::String const & id, Orkige::String const & sFilename) 
		: GameState(id), sFilenameMenu(sFilename)
	{
		this->registerEvent(Orkige::Engine::FrameStartedEvent,			&PreviewMenuState::onFrameStarted,	this);
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,		&PreviewMenuState::onKeyPressed,	this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,		&PreviewMenuState::onKeyReleased,	this);
		this->registerEvent(Orkige::InputManager::MousePressedEvent,	&PreviewMenuState::onMousePressed,	this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,	&PreviewMenuState::onMouseReleased,	this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,		&PreviewMenuState::onMouseMoved,	this);
		this->registerEvent(Orkige::Button::ButtonHitEvent,				&PreviewMenuState::onButtonHit,		this);
	}
	//---------------------------------------------------------
	PreviewMenuState::~PreviewMenuState()
	{

	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void PreviewMenuState::onEnter()
	{
		/* for debug: lists all menu files

		Ogre::StringVectorPtr menus = Ogre::ResourceGroupManager::getSingleton().findResourceNames("General", "*.menu");

		int i = menus->size();
		Ogre::StringVector::iterator It = (*menus).begin();
		Ogre::StringVector::iterator ItEnd = (*menus).end();
		for ( ; It < ItEnd; ++It)
		{
			Ogre::String s = *It;
			...
		}
		*/

		if (!this->sFilenameMenu.empty())
		{
			LoadMenu();
		}
	}
	//---------------------------------------------------------
	void PreviewMenuState::onExit()
	{
//		SettingsManager::getSingleton().save();		
		FastGuiManager::getSingleton().destroyAllWidgets();
	}
	//---------------------------------------------------------
	bool PreviewMenuState::onButtonHit(Orkige::Event const & event)
	{
		optr< ::Orkige::Button > btn = event.getDataPtr< ::Orkige::Button >();
		Orkige::String sID = btn->getObjectID();

		// TODO show message box, click sound

		/*
		if(btn->getObjectID() == "Quit")
		{
			Orkige::Application::getSingleton().quit();
		}
		*/

		return false;
	}
	//---------------------------------------------------------
	bool PreviewMenuState::onFrameStarted(Orkige::Event const & event)
	{
		optr<FrameEventData> data = event.getDataPtr<FrameEventData>();

		return false;
	}
	//---------------------------------------------------------
	bool PreviewMenuState::onKeyPressed(Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();

		return false;
	}
	//---------------------------------------------------------
	bool PreviewMenuState::onKeyReleased(Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();

		switch(data->key)
		{
		case KeyEventData::KC_O:
			this->SelectAndLoadMenu();
			break;
		case KeyEventData::KC_R:
			this->LoadMenu();
			break;
		case KeyEventData::KC_GRAVE:
			Orkige::IngameConsole::getSingleton().switchVisible();
			break;
		case KeyEventData::KC_BACK:
			break;
		case KeyEventData::KC_ESCAPE:
			{
				Orkige::Application::getSingleton().quit();
			}
			break;
		default:
			{
				
			} 
			break;
		}

		return false;
	}
	//---------------------------------------------------------
	bool PreviewMenuState::onMousePressed(Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		
		return false;
	}
	//---------------------------------------------------------
	bool PreviewMenuState::onMouseReleased(Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		
		return false;
	}
	//---------------------------------------------------------
	bool PreviewMenuState::onMouseMoved(Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		
		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	

	void PreviewMenuState::SelectAndLoadMenu()
	{
		// TODO hide soft mouse cursor

		//Ogre::String sTemp = this->DialogBrowseFile(
		//	Ogre::String("Select menu file to view"), 
		//	Ogre::String("*.menu"), 
		//	Ogre::String("orkige menu definition files (*.menu)\0*.menu\0"));
		Ogre::String sFilenameTemp = FileUtils::DialogBrowseFile(
			"Select menu file to view", 
			"*.menu", 
			"orkige menu files (*.menu)\0*.menu\0");

		// TODO show soft mouse cursor

		if (!sFilenameTemp.empty())
		{
			this->sFilenameMenu = sFilenameTemp;
			this->LoadMenu();
		}
	}

	void PreviewMenuState::LoadMenu()
	{
		if (!this->sFilenameMenu.empty())
		{
			Ogre::String sBasename, sExtension, sPath;
			Ogre::StringUtil::splitFullFilename(this->sFilenameMenu, sBasename, sExtension, sPath);

			sBasename = "FastGui/" + sBasename + "." + sExtension;  // e.g. "FastGui/main_demo.menu"

			//FileUtils::SetCurrentPath(FileUtils::GetResourceDirectory().c_str());

			FastGuiManager::getSingleton().destroyAllWidgets();
			FastGuiManager::getSingleton().getFactory().lock()->load(Orkige::String(sBasename.c_str()));


			// TODO add filename to window title

		}
	}

}