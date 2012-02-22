/**************************************************************
	created:	2010/07/19 at 23:44
	filename: 	MenuState.cpp
	author:		MorrK
	
	purpose:	
***************************************************************/

#include "MenuState.h"
#include <engine_input/InputManager.h>
#include <core_game/Application.h>
#include <engine_graphic/Engine.h>
#include <engine_fastgui/FastGuiManager.h>
#include "FileUtils.h"
#include "ConsoleCommands.h"


namespace SceneOptimizer
{
	using namespace Orkige;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	MenuState::MenuState(Orkige::String const & id) 
		: GameState(id)
	{
		this->mousePos = Ogre::Vector2::ZERO;

		this->registerEvent(Orkige::Engine::FrameStartedEvent,			&MenuState::onFrameStarted,	this);
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,		&MenuState::onKeyPressed,	this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,		&MenuState::onKeyReleased,	this);
		this->registerEvent(Orkige::InputManager::MousePressedEvent,	&MenuState::onMousePressed,	this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,	&MenuState::onMouseReleased,	this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,		&MenuState::onMouseMoved,	this);
		
		this->registerEvent(Orkige::FastGuiButton::ButtonHitEvent,				&MenuState::onButtonHit,		this);
		this->registerEvent(Orkige::FastGuiCheckBox::CheckBoxToggledEvent,		&MenuState::onCheckBoxToggled, this);
	}
	//---------------------------------------------------------
	MenuState::~MenuState()
	{

	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void MenuState::onEnter()
	{
		this->merger = new MeshMerger ();
		cam = Orkige::Engine::getSingleton().getCamera();

		this->freeCamAttach = Orkige::Engine::getSingleton().getSceneManager()->createSceneNode("FreeCameraAttach");
		cam->detachFromParent();
		this->freeCamAttach->attachObject(cam);
		//KS::initConsoleCommands();
		// for debug: lists all menu files

		//Ogre::StringVectorPtr menus = Ogre::ResourceGroupManager::getSingleton().findResourceNames("General", "*.menu");

		//Ogre::LogManager::getSingleton().logMessage("Found " + Orkige::StringUtil::intToString(menus->size()) + " menus");

		//Ogre::StringVector::iterator It = (*menus).begin();
		//Ogre::StringVector::iterator ItEnd = (*menus).end();
		//for ( ; It < ItEnd; ++It)
		//{
		//	Ogre::String s = *It;
		//	Ogre::LogManager::getSingleton().logMessage("File:: " + s);
		//}

		//this->loadMenu();
	}
	//---------------------------------------------------------
	void MenuState::onExit()
	{
		FastGuiManager::getSingleton().destroyAllWidgets();
	}
	//---------------------------------------------------------
	bool MenuState::onButtonHit(Orkige::Event const & event)
	{
		optr< ::Orkige::FastGuiButton > btn = event.getDataPtr< ::Orkige::FastGuiButton >();

		Ogre::LogManager::getSingleton().logMessage("Button hit: " + btn->getObjectID());
		
		return false;
	}
	//---------------------------------------------------------
	bool MenuState::onCheckBoxToggled(Orkige::Event const & event)
	{
		optr< ::Orkige::FastGuiCheckBox > checkbox = event.getDataPtr< ::Orkige::FastGuiCheckBox >();

		Ogre::LogManager::getSingleton().logMessage("Checkbox hit: " + checkbox->getObjectID());

		return false;
	}
	//---------------------------------------------------------
	bool MenuState::onFrameStarted(Orkige::Event const & event)
	{
		optr<FrameEventData> data = event.getDataPtr<FrameEventData>();

		float timeSinceLastFrame = data->timeSinceLastFrame;

		Ogre::Real speed = 5;
		Ogre::Vector3 trans, strafe, vec;
		Ogre::Quaternion quat;

		quat = this->freeCamAttach->getOrientation();

		vec = Ogre::Vector3(0.0,0.0,-1);
		trans = quat * vec;

		vec = Ogre::Vector3(1,0.0,0.0);
		strafe = quat * vec;

		

		this->freeCamAttach->pitch( Ogre::Degree(Orkige::InputManager::getSingleton().getMouseData()->relY * -speed) * timeSinceLastFrame );
		this->freeCamAttach->yaw( Ogre::Degree(Orkige::InputManager::getSingleton().getMouseData()->relX * -speed) * timeSinceLastFrame, Ogre::SceneNode::TS_WORLD );


		if (Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_UP) || Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_W))
			this->freeCamAttach->translate(trans * timeSinceLastFrame * speed*50);

		if (Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_DOWN) || Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_S))
			this->freeCamAttach->translate((trans * -1) * timeSinceLastFrame * speed*50);

		if (Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_LEFT) || Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_A))
			this->freeCamAttach->translate((strafe * -1) * timeSinceLastFrame * speed*50);

		if (Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_RIGHT) || Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_D))
			this->freeCamAttach->translate(strafe * timeSinceLastFrame * speed*50);

		if (Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_Q))
			this->freeCamAttach->rotate (Ogre::Vector3(0, 1, 0), Ogre::Radian (timeSinceLastFrame * speed * 0.5f), Ogre::SceneNode::TS_WORLD);
		if (Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_E))
			this->freeCamAttach->rotate (Ogre::Vector3(0, 1, 0), Ogre::Radian (-timeSinceLastFrame * speed * 0.5f), Ogre::SceneNode::TS_WORLD);

		return false;
	}
	//---------------------------------------------------------
	bool MenuState::onKeyPressed(Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();

		return false;
	}
	//---------------------------------------------------------
	bool MenuState::onKeyReleased(Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
		
		if(IngameConsole::getSingleton().isVisible()) return false;

		switch(data->key)
		{
		// open scene file
		case KeyEventData::KC_O:
			loadAndPrepareScene ();
			break;
		case KeyEventData::KC_M:
			this->merger->merge ();
			break;
		case KeyEventData::KC_R:
			break;
		case KeyEventData::KC_TAB:
			Orkige::IngameConsole::getSingleton().switchVisible();
			break;
		case KeyEventData::KC_BACK:
			break;
		case KeyEventData::KC_ESCAPE:
			merger->reset();
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
	bool MenuState::onMousePressed(Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		
		// store mouse position
		this->mousePos.x = static_cast<Ogre::Real>(data->absX);
		this->mousePos.y = static_cast<Ogre::Real>(data->absY);

		return false;
	}
	//---------------------------------------------------------
	bool MenuState::onMouseReleased(Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		
		// store mouse position
		Ogre::Vector2 mouseSize(static_cast<Ogre::Real>(data->absX), static_cast<Ogre::Real>(data->absY));

		/*
		char tmp[128];
		if (false)
		{
			mouseSize -= mousePos;

			// convert pixel to percent
			mouseSize.x *= 100.0 / Engine::getSingleton().getRenderWindow()->getWidth();
			mouseSize.y *= 100.0 / Engine::getSingleton().getRenderWindow()->getHeight();
			this->mousePos.x *= 100.0 / Engine::getSingleton().getRenderWindow()->getWidth();
			this->mousePos.y *= 100.0 / Engine::getSingleton().getRenderWindow()->getHeight();

			// center
			this->mousePos.x -= 50.0;
			this->mousePos.y -= 50.0;

			// size = 30% 10%
			// position = -15% -15%
			sprintf(tmp, "size = %d%% %d%%\nposition = %d%% %d%%\n", 
				static_cast<int>(mouseSize.x),
				static_cast<int>(mouseSize.y),
				static_cast<int>(this->mousePos.x),
				static_cast<int>(this->mousePos.y));
		}
		else
		{
			mouseSize.x *= 100.0 / Engine::getSingleton().getRenderWindow()->getWidth();
			mouseSize.y *= 100.0 / Engine::getSingleton().getRenderWindow()->getHeight();

			// position 65% 65%
			sprintf(tmp, "position = %d%% %d%%\n", 
				static_cast<int>(mouseSize.x),
				static_cast<int>(mouseSize.y));
		}

		Ogre::LogManager::getSingleton().logMessage(Orkige::String(tmp));
		*/
		return false;
	}
	//---------------------------------------------------------
	bool MenuState::onMouseMoved(Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		
		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------

	//void MenuState::selectAndLoadMenu()
	//{
		/*
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
		*/
	//}
	//---------------------------------------------------------
	//void MenuState::loadMenu()
	//{
		/*
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
				//Engine::getSingleton().getRenderWindow()->resize(800, 600);
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

			basename += ".";
			basename += extension;

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
		*/
	//}

	void MenuState::loadAndPrepareScene ()
	{
		std::string filename;

		filename = FileUtils::GetFileNameFromPath (FileUtils::DialogBrowseFile ("Choose the .scene file", ".scene", ".scene" ));
		oAssertDesc(Ogre::StringUtil::endsWith(filename, ".scene"), "");
		this->merger->loadScene (filename);
	}

}