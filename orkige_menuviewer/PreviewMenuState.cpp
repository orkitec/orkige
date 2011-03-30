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
		this->mousePos = Ogre::Vector2::ZERO;

		this->registerEvent(Orkige::Engine::FrameStartedEvent,			&PreviewMenuState::onFrameStarted,	this);
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,		&PreviewMenuState::onKeyPressed,	this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,		&PreviewMenuState::onKeyReleased,	this);
		this->registerEvent(Orkige::InputManager::MousePressedEvent,	&PreviewMenuState::onMousePressed,	this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,	&PreviewMenuState::onMouseReleased,	this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,		&PreviewMenuState::onMouseMoved,	this);
		
		this->registerEvent(Orkige::Button::ButtonHitEvent,				&PreviewMenuState::onButtonHit,		this);
		this->registerEvent(Orkige::CheckBox::CheckBoxToggledEvent,		&PreviewMenuState::onCheckBoxToggled, this);

		
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
		// for debug: lists all menu files

		Ogre::StringVectorPtr menus = Ogre::ResourceGroupManager::getSingleton().findResourceNames("General", "*.menu");

		Ogre::LogManager::getSingleton().logMessage("Found " + Orkige::StringUtil::intToString(menus->size()) + " menus");

		Ogre::StringVector::iterator It = (*menus).begin();
		Ogre::StringVector::iterator ItEnd = (*menus).end();
		for ( ; It < ItEnd; ++It)
		{
			Ogre::String s = *It;
			Ogre::LogManager::getSingleton().logMessage("File:: " + s);
		}

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
		oAssert(btn);

		Ogre::LogManager::getSingleton().logMessage("Button hit: " + btn->getObjectID());
		
		return false;
	}
	//---------------------------------------------------------
	bool PreviewMenuState::onCheckBoxToggled(Orkige::Event const & event)
	{
		optr< ::Orkige::FastGuiCheckBox > checkbox = event.getDataPtr< ::Orkige::FastGuiCheckBox >();
		oAssert(checkbox);

		Ogre::LogManager::getSingleton().logMessage("Checkbox hit: " + checkbox->getObjectID());

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
		
		// store mouse position
		this->mousePos.x = static_cast<Ogre::Real>(data->absX);
		this->mousePos.y = static_cast<Ogre::Real>(data->absY);

		return false;
	}
	//---------------------------------------------------------
	bool PreviewMenuState::onMouseReleased(Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		
		// store mouse position
		Ogre::Vector2 mouseSize(static_cast<Ogre::Real>(data->absX), static_cast<Ogre::Real>(data->absY));
		mouseSize -= mousePos;

		// convert pixel to percent
		this->mousePos.x *= 100.0 / Engine::getSingleton().getRenderWindow()->getWidth();
		this->mousePos.y *= 100.0 / Engine::getSingleton().getRenderWindow()->getHeight();
		mouseSize.x *= 100.0 / Engine::getSingleton().getRenderWindow()->getWidth();
		mouseSize.y *= 100.0 / Engine::getSingleton().getRenderWindow()->getHeight();

		// center
		this->mousePos.x -= 50.0;
		this->mousePos.y -= 50.0;

		// size = 30% 10%
		// position = -15% -15%
		char tmp[128];
		sprintf(tmp, "size = %d%% %d%%\nposition = %d%% %d%%\n", 
			static_cast<int>(mouseSize.x),
			static_cast<int>(mouseSize.y),
			static_cast<int>(this->mousePos.x),
			static_cast<int>(this->mousePos.y));
		Ogre::LogManager::getSingleton().logMessage(Orkige::String(tmp));

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

			// platform dependent resource paths and resize window
			this->filenameResourceConfig = Orkige::PlatformUtil::getResourceDirectory();
			if (path.find("_ipad") != String::npos)
			{
				Ogre::LogManager::getSingleton().logMessage("set to iPad");
				this->filenameResourceConfig += "data/Config/resources_ipad.cfg";
				Engine::getSingleton().getRenderWindow()->resize(1024, 768);
			}
			else if (path.find("_iphone4") != String::npos)
			{
				Ogre::LogManager::getSingleton().logMessage("set to iPhone 4 or newer");
				this->filenameResourceConfig += "data/Config/resources_iphone4.cfg";
				Engine::getSingleton().getRenderWindow()->resize(960, 640);
			}
			else if (path.find("_iphone") != String::npos)
			{
				Ogre::LogManager::getSingleton().logMessage("set to iPhone 1,2,3");
				this->filenameResourceConfig += "data/Config/resources_iphone.cfg";
				Engine::getSingleton().getRenderWindow()->resize(480, 320);
			}
			else
			{
				Ogre::LogManager::getSingleton().logMessage("set to Windows");
				this->filenameResourceConfig += "data/Config/resources.cfg";
				//renderWindow->resize(800, 600);
				Engine::getSingleton().getRenderWindow()->resize(1024, 768);
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

			// TODO notify rendering of screen resolution change
			//FastGuiManager::getSingleton().updateStats();
		}
	}

}