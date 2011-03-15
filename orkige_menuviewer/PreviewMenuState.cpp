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
	PreviewMenuState::PreviewMenuState(Orkige::String const & id, Orkige::String const & filenameMenu) 
		: GameState(id), filenameMenu(filenameMenu)
	{
		this->registerEvent(Orkige::Engine::FrameStartedEvent,			&PreviewMenuState::onFrameStarted,	this);
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,		&PreviewMenuState::onKeyPressed,	this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,		&PreviewMenuState::onKeyReleased,	this);
		this->registerEvent(Orkige::InputManager::MousePressedEvent,	&PreviewMenuState::onMousePressed,	this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,	&PreviewMenuState::onMouseReleased,	this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,		&PreviewMenuState::onMouseMoved,	this);
		this->registerEvent(Orkige::Button::ButtonHitEvent,				&PreviewMenuState::onButtonHit,		this);
		
		//this->statsValues = onew(new FastGuiTextbox("FastGuiManagerFrameStatsValues", 9, "", Ogre::Vector2(100,100), "", 15));
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

		this->loadMenu();
	}
	//---------------------------------------------------------
	void PreviewMenuState::onExit()
	{
		FastGuiManager::getSingleton().destroyAllWidgets();
	}
	//---------------------------------------------------------
	bool PreviewMenuState::onButtonHit(Orkige::Event const & event)
	{
		optr< ::Orkige::Button > btn = event.getDataPtr< ::Orkige::Button >();
		Orkige::String sID = btn->getObjectID();


		
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
			this->selectAndLoadMenu();
			break;
		case KeyEventData::KC_R:
			this->loadMenu();
			break;
		case KeyEventData::KC_GRAVE:
			Orkige::IngameConsole::getSingleton().switchVisible();
			break;
		case KeyEventData::KC_BACK:
			break;
		case KeyEventData::KC_ESCAPE:
			Orkige::Application::getSingleton().quit();			
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

	void PreviewMenuState::selectAndLoadMenu()
	{
		// hide soft mouse cursor
		FastGuiManager::getSingleton().hideCursor();

		Ogre::String sFilenameTemp = FileUtils::DialogBrowseFile(
			"Select menu file to view", 
			"*.menu", 
			"orkige menu files (*.menu)\0*.menu\0");

		// show soft mouse cursor
		FastGuiManager::getSingleton().showCursor("fastgui_default", "mousepointer");

		if (!sFilenameTemp.empty())
		{
			this->filenameMenu = sFilenameTemp;
			this->loadMenu();
		}
	}
	//---------------------------------------------------------
	void PreviewMenuState::loadMenu()
	{
		if (!this->filenameMenu.empty())
		{
			Ogre::String basename, extension, path;
			Ogre::StringUtil::splitFullFilename(this->filenameMenu, basename, extension, path);

			oAssertDesc(Ogre::StringUtil::endsWith(this->filenameMenu, ".menu"), "");

			// TODO resize window

			// platform dependent resource paths
			this->filenameResourceConfig = Orkige::PlatformUtil::getResourceDirectory();
			if (path.find("_iphone"))
			{
				this->filenameResourceConfig += "data/Config/resources_iphone.cfg";
			}
			else if (path.find("_iphone4"))
			{
				this->filenameResourceConfig += "data/Config/resources_iphone4.cfg";
			}
			else if (path.find("_ipad"))
			{
				this->filenameResourceConfig += "data/Config/resources_ipad.cfg";
			}
			else
			{
				this->filenameResourceConfig += "data/Config/resources.cfg";
			}
			Engine::getSingleton().resetupResources(filenameResourceConfig);


			// assemble engine compatible filename, e.g. "FastGui/main_demo.menu"
			Ogre::String sResourcePath = CC::FileUtils::GetResourceDirectory();
			sResourcePath += "/data/";
			oAssertDesc(this->filenameMenu.length() > sResourcePath.length(), "");
			sResourcePath = this->filenameMenu.substr(sResourcePath.length(), this->filenameMenu.length());			
			//basename = Ogre::StringUtil::standardisePath(sResourcePath);
			basename = Ogre::StringUtil::replaceAll(sResourcePath, "\\", "/");

			Ogre::LogManager::getSingleton().logMessage("Loading menu " + basename);

			FileUtils::SetCurrentPath(FileUtils::GetResourceDirectory().c_str());

			FastGuiManager::getSingleton().destroyAllWidgets();
			FastGuiManager::getSingleton().getFactory().lock()->load(basename);


			// TODO add filename to window title


			//std::stringstream sstr;
			//sstr << "Filename" << std::endl;
			//this->statsValues->setText(sstr.str());
		}
	}

}