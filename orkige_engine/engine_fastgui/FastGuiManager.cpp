/********************************************************************
	created:	Tuesday 2010/10/26 at 18:25
	filename: 	FastGuiManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
	
	purpose:	
*********************************************************************/

#include "engine_fastgui/FastGuiManager.h"
#include "engine_graphic/Engine.h"

namespace Orkige
{
	IMPL_OSINGLETON(FastGuiManager);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiManager::FastGuiManager(optr<FastGuiFactory> _factory) : factory(_factory)
	{
		oAssert(this->factory);
		this->silverback = onew(new Gorilla::Silverback());
	}
	//---------------------------------------------------------
	FastGuiManager::~FastGuiManager()
	{

	}
	//---------------------------------------------------------
	void FastGuiManager::enableInputEvents()
	{
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,				&FastGuiManager::onKeyPressed,				this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,				&FastGuiManager::onKeyReleased,				this);
		this->registerEvent(Orkige::InputManager::MousePressedEvent,			&FastGuiManager::onMousePressed,			this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,			&FastGuiManager::onMouseReleased,			this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,				&FastGuiManager::onMouseMoved,				this);
		this->refreshCursor();
	}
	//---------------------------------------------------------
	void FastGuiManager::disableInputEvents()
	{
		this->unregisterEvent(Orkige::InputManager::KeyPressedEvent);
		this->unregisterEvent(Orkige::InputManager::KeyReleasedEvent);
		this->unregisterEvent(Orkige::InputManager::MousePressedEvent);
		this->unregisterEvent(Orkige::InputManager::MouseReleasedEvent);
		this->unregisterEvent(Orkige::InputManager::MouseMovedEvent);
	}
	//---------------------------------------------------------
	void FastGuiManager::refreshCursor()
	{
#if OGRE_NO_VIEWPORT_ORIENTATIONMODE == 0
		// TODO:
		// the position should be based on the orientation, for now simply return
		return;
#endif
#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		/*		std::vector<OIS::MultiTouchState> states = InputManager::getSingleton().getMouse()->getMultiTouchStates();
		if(states.size() > 0)
		this->cursor->setPosition(states[0].X.abs, states[0].Y.abs);
		*/
#else
		//this->cursor->setPosition((Ogre::Real)InputManager::getSingleton().getMouseData()->absX, (Ogre::Real)InputManager::getSingleton().getMouseData()->absY);
#endif
	}
	//---------------------------------------------------------
	woptr<FastGuiView> FastGuiManager::createView(String const & atlas)
	{
		oAssertDesc(!this->hasView(atlas), "Screen with atlas: " << atlas << " already exists!");
		this->silverback->loadAtlas(atlas);
		Gorilla::Screen* screen = this->silverback->createScreen(Engine::getSingleton().getViewort(), atlas);
		oAssert(screen);
		optr<FastGuiView> view = onew(new FastGuiView(screen));
		this->views[atlas] = view;
		return view;
	}
	//---------------------------------------------------------
	bool FastGuiManager::addWidget(optr<FastGuiWidget> widget)
	{
		if(this->widgets.find(widget->getObjectID()) != this->widgets.end())
		{
			return false;
		}
		this->widgets[widget->getObjectID()] = widget;
		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool FastGuiManager::onFrameRenderingQueued(Orkige::Event const & event)
	{
		
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onKeyPressed(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();

		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onKeyReleased(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();


		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onMousePressed(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);

		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onMouseReleased(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onMouseMoved(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		
		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiManager)
	OOBJECT_END
}