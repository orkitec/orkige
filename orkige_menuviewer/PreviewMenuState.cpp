/**************************************************************
	created:	2010/07/19 at 23:44
	filename: 	PreviewMenuState.cpp
	author:		MorrK
	
	purpose:	
***************************************************************/

#include "PreviewMenuState.h"
#include <engine_input/InputManager.h>
#include <core_game/Application.h>
#include <core_game/GameStateManager.h>
#include <engine_graphic/Engine.h>
#include <engine_gui/IngameConsole.h>
#include <engine_gui/YesNoDialog.h>
#include <engine_sound/SoundManager.h>
#include <engine_util/StringUtil.h>
#include <core_debug/Profile.h>
//#include "cc_game/SettingsManager.h"
#include <engine_fastgui/FastGuiManager.h>

namespace CC
{
	using namespace Orkige;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	PreviewMenuState::PreviewMenuState(Orkige::String const & id, Orkige::String const & sFilename) 
		: GameState(id), sFilename(sFilename)
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
		//Ogre::StringVectorPtr scenes = Ogre::ResourceGroupManager::getSingleton().findResourceNames("General", "*.scene");
		Ogre::StringVectorPtr menus = Ogre::ResourceGroupManager::getSingleton().findResourceNames("General", "*.menu");
		

		// just curious...
		int i = menus->size();
		Ogre::StringVector::iterator It = (*menus).begin();
		Ogre::StringVector::iterator ItEnd = (*menus).end();
		for ( ; It < ItEnd; ++It)
		{
			Ogre::String s = *It;
			int i = 0;
		}

		if (!sFilename.empty())
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
/*
		optr< ::Orkige::Button > btn = event.getDataPtr< ::Orkige::Button >();
		if(btn->getObjectID() == "StartRot" || btn->getObjectID() == "Start" )
		{	
			GameStateManager::getSingleton().setAttribute("GameVariation", Orkige::String("NormalGame"));
			this->setTransition("LevelPreview");
			return false;
		}
// 		else if(btn->getObjectID() == "StartNoRot")
// 		{	
// 			GameStateManager::getSingleton().setAttribute("GameVariation", Orkige::String("NoRotation"));
// 			this->setTransition("LevelPreview");
// 			return false;
// 		}
		else if(btn->getObjectID() == "Level1") //chillingo hack
		{	
//			SettingsManager::getSingleton().setSetting("SelectedScene","Level_02.scene");
			this->setLastLevel(Orkige::String("Level_02.scene"));
			GameStateManager::getSingleton().setAttribute("GameVariation", btn->getObjectID());
			this->setTransition("LevelPreview");
			return false;
		}
		else if(btn->getObjectID() == "Level2") //chillingo hack
		{	
//			SettingsManager::getSingleton().setSetting("SelectedScene","Level_10.scene");
			this->setLastLevel(Orkige::String("Level_10.scene"));
			GameStateManager::getSingleton().setAttribute("GameVariation", btn->getObjectID());
			this->setTransition("LevelPreview");
			return false;
		}
		else if(btn->getObjectID() == "Level3") //chillingo hack
		{	
//			SettingsManager::getSingleton().setSetting("SelectedScene","Action_03.scene");
			this->setLastLevel(Orkige::String("Action_03.scene"));
			GameStateManager::getSingleton().setAttribute("GameVariation", btn->getObjectID());
			this->setTransition("LevelPreview");
			return false;
		}
		else if(btn->getObjectID() == "Level4") //chillingo hack
		{	
//			SettingsManager::getSingleton().setSetting("SelectedScene","Labyrinth_01.scene");
			this->setLastLevel(Orkige::String("Labyrinth_01.scene"));
			GameStateManager::getSingleton().setAttribute("GameVariation", btn->getObjectID());
			this->setTransition("LevelPreview");
			return false;
		}
		else if(btn->getObjectID() == "Level5") //chillingo hack
		{	
//			SettingsManager::getSingleton().setSetting("SelectedScene","Level_18.scene");
			this->setLastLevel(Orkige::String("Level_18.scene"));
			GameStateManager::getSingleton().setAttribute("GameVariation", btn->getObjectID());
			this->setTransition("LevelPreview");
			return false;
		}
		else if(btn->getObjectID() == "ModelViewer")
		{	
			this->setTransition("ModelViewer");
			return false;
		}
		else if(btn->getObjectID() == "Quit")
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
	/*
	Ogre::String PreviewMenuState::DialogBrowseFile(Ogre::String const & sTitle, Ogre::String const & sFileType, Ogre::String const & sFileTypeDesc)
	{
		char szFileName[MAX_PATH] = "";

		OPENFILENAME ofn;
		ZeroMemory(&ofn, sizeof(ofn));

		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = 0;
		ofn.lpstrFilter = sFileTypeDesc.c_str(); // e.g. "orkige gui Files (*.ogui)\0*.ogui\0";
		ofn.lpstrFile = (char*)sFileTypeDesc.c_str();
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_EXPLORER; // | OFN_HIDEREADONLY;
		ofn.lpstrDefExt = sFileType.c_str(); // e.g. "ogui";
		ofn.lpstrInitialDir = NULL;

		Ogre::String sFilename;
		if (GetOpenFileName(&ofn))
		{
			sFilename = szFileName;
		}

		return sFilename;
	}
	*/
	std::string PreviewMenuState::DialogBrowseFile(const char* szTitle, const char* szFileType, const char* szFileTypeDesc)
	{
		char szFileName[MAX_PATH] = "";

		OPENFILENAME ofn;
		ZeroMemory(&ofn, sizeof(ofn));

		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = 0;
		ofn.lpstrFilter = szFileTypeDesc; // e.g. "orkige gui Files (*.ogui)\0*.ogui\0";
		ofn.lpstrFile = szFileName;
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_EXPLORER; // | OFN_HIDEREADONLY;
		ofn.lpstrDefExt = szFileType; // e.g. "ogui";
		ofn.lpstrInitialDir = NULL;

		std::string sFilename;
		if (GetOpenFileName(&ofn))
		{
			sFilename = szFileName;
		}

		return sFilename;
	}

	void PreviewMenuState::SelectAndLoadMenu()
	{
		//Ogre::String sTemp = this->DialogBrowseFile(
		//	Ogre::String("Select menu file to view"), 
		//	Ogre::String("*.menu"), 
		//	Ogre::String("orkige menu definition files (*.menu)\0*.menu\0"));
		Ogre::String sTemp = this->DialogBrowseFile(
			"Select menu file to view", 
			"*.menu", 
			"orkige menu definition files (*.menu)\0*.menu\0");

		if (!sTemp.empty())
		{
			this->sFilename = sTemp;
			this->LoadMenu();
		}
	}

	void PreviewMenuState::LoadMenu()
	{
		if (!sFilename.empty())
		{
			Ogre::String sBasename, sExtension, sPath;
			Ogre::StringUtil::splitFullFilename(this->sFilename, sBasename, sExtension, sPath);

			sBasename = "FastGui/" + sBasename + "." + sExtension;  // e.g. "FastGui/main_demo.menu"

			FastGuiManager::getSingleton().getFactory().lock()->load(Orkige::String(sBasename));
		}
	}

}