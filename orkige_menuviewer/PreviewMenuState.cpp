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
	PreviewMenuState::PreviewMenuState(Orkige::String const & id) : GameState(id)
	{
		this->registerEvent(Orkige::Engine::FrameStartedEvent,	&PreviewMenuState::onFrameStarted,		this);
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,		&PreviewMenuState::onKeyPressed,		this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,		&PreviewMenuState::onKeyReleased,		this);
		this->registerEvent(Orkige::InputManager::MousePressedEvent,		&PreviewMenuState::onMousePressed,		this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,	&PreviewMenuState::onMouseReleased,	this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,		&PreviewMenuState::onMouseMoved,		this);
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
		Ogre::StringVectorPtr scenes = Ogre::ResourceGroupManager::getSingleton().findResourceNames("General", "*.scene");
		

		// just curious...
		int i = scenes->size();
		Ogre::StringVector::iterator It = (*scenes).begin();
		Ogre::StringVector::iterator ItEnd = (*scenes).end();
		for ( ; It < ItEnd; ++It)
		{
			Ogre::String s = *It;
			int i = 0;
		}


		//if(SettingsManager::getSingleton().getSettingAs<bool>("DemoMode") == true)
		if (true)
		{
			FastGuiManager::getSingleton().getFactory().lock()->load("FastGui/main_demo.menu");
		}
		else
		{
			FastGuiManager::getSingleton().getFactory().lock()->load("FastGui/main.menu");
		}
		
	
		if (FastGuiManager::getSingleton().widgetExists("scenemenu"))
		{
			woptr<Orkige::FastGuiWidget> widget_fast = FastGuiManager::getSingleton().getWidget("scenemenu") ;
			oAssert(widget_fast.lock());
			optr<Orkige::FastGuiSelectMenu> SelectMenu_fast = boost::static_pointer_cast<Orkige::FastGuiSelectMenu>(widget_fast.lock());//.lock()->setItems(*scenes) ;
			oAssert(SelectMenu_fast);
			SelectMenu_fast->setItems(*scenes);
//			SelectMenu_fast->selectItem(this->getLastLevel());
		}
	}
	//---------------------------------------------------------
	void PreviewMenuState::onExit()
	{
		if(FastGuiManager::getSingleton().widgetExists("scenemenu"))
		{
//			String sceneName =boost::static_pointer_cast<Orkige::FastGuiSelectMenu>(FastGuiManager::getSingleton().getWidget("scenemenu").lock())->getSelectedItem();
//
//			SettingsManager::getSingleton().setSetting("SelectedScene",sceneName);
			
//			this->setLastLevel(boost::static_pointer_cast<Orkige::FastGuiSelectMenu>(FastGuiManager::getSingleton().getWidget("scenemenu").lock())->getSelectedItem());
//			Orkige::String selectedItem = boost::static_pointer_cast<Orkige::FastGuiSelectMenu>(FastGuiManager::getSingleton().getWidget("scenemenu").lock())->getSelectedItem();
//			GameStateManager::getSingleton().setAttribute("CurrentLevel", selectedItem);
		}

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
		//OPROFILE("PreviewMenuState::onFrameStarted");
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
				
			} break;
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
/*
	Orkige::String PreviewMenuState::getLastLevel	()
	{
		Orkige::String filename = Orkige::String(Orkige::PlatformUtil::getDocumentsDirectory() + "lastlevel.txt");
		std::ifstream in(filename.c_str());
		if (in)
		{
			Orkige::String buf;
			std::getline (in, buf);
			in.close();
			return buf;
		}
		else
		{
			return Orkige::String("Level_01.scene");
		}
	}
	//---------------------------------------------------------
	void PreviewMenuState::setLastLevel	( Orkige::String sceneName )	
	{
		Orkige::String filename = Orkige::String(Orkige::PlatformUtil::getDocumentsDirectory() + "lastlevel.txt");
		std::ofstream out(filename.c_str());
		if (out)
		{
			out << sceneName;
			out.close();
		}	
	}
*/
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------

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



}