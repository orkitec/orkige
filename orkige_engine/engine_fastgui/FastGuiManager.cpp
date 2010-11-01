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
#include <core_util/foreach.h>

namespace Orkige
{
	IMPL_OSINGLETON(FastGuiManager);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiManager::FastGuiManager(optr<FastGuiFactory> _factory, String const & _defaultAtlas) : factory(_factory), defaultAtlas(_defaultAtlas)
	{
		oAssert(this->factory);
		this->silverback = onew(new Gorilla::Silverback());
		this->getCreateView(this->defaultAtlas);
		this->registerEvent(Orkige::Engine::FrameRenderingQueuedEvent,	&FastGuiManager::onFrameRenderingQueued,	this);
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
	void FastGuiManager::showCursor(String const & atlas, String const & sprite)
	{
		optr<FastGuiView> view = this->getCreateView(atlas).lock();
		oAssert(view);
		Gorilla::Sprite* spriteObject = view->getScreen()->getAtlas()->getSprite(sprite);
		oAssert(spriteObject);
		this->cursor = onew(new FastGuiDecorWidget("Cursor", sprite, Ogre::Vector2::ZERO, Ogre::Vector2(spriteObject->spriteWidth, spriteObject->spriteHeight), atlas, 16));
	}
	//---------------------------------------------------------
	void FastGuiManager::hideCursor()
	{
		this->cursor.reset();
	}
	//---------------------------------------------------------
	bool FastGuiManager::isCursorVisible()
	{
		if(this->cursor && this->cursor->getLayer()->isVisible())
		{
			return true;
		}
		return false;
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
		if(this->cursor)
		{
			this->cursor->setPosition((Ogre::Real)InputManager::getSingleton().getMouseData()->absX, (Ogre::Real)InputManager::getSingleton().getMouseData()->absY);
		}
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
	bool FastGuiManager::destroyWidget(String const & id)
	{
		FastGuiWidgetMap::iterator it = this->widgets.find(id);
		if(it == this->widgets.end())
		{
			return false;
		}
		this->widgets.erase(it);
		return true;
	}
	//---------------------------------------------------------
	void FastGuiManager::showStats(uint glyphIndex, Ogre::Vector2 const & pos, String const & atlas)
	{
		if(this->stats)
			this->stats.reset();

		this->stats = onew(new FastGuiTextbox("FastGuiManagerFrameStats", glyphIndex, "", pos, atlas, 15));
	}
	//---------------------------------------------------------
	void FastGuiManager::hideStats()
	{
		this->stats.reset();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool FastGuiManager::onFrameRenderingQueued(Orkige::Event const & event)
	{
		if(this->stats)
		{
			Ogre::RenderTarget::FrameStats stats = Engine::getSingleton().getRenderWindow()->getStatistics();
			std::stringstream sstr;
			sstr << "FPS:\t\t\t"	<< std::fixed << std::setprecision(1) << stats.lastFPS	<< std::endl;
			sstr << "Average FPS:\t"<< std::fixed << std::setprecision(1) << stats.avgFPS	<< std::endl;
			sstr << "Best FPS:\t"	<< std::fixed << std::setprecision(1) << stats.bestFPS	<< std::endl;
			sstr << "Worst FPS:\t"	<< std::fixed << std::setprecision(1) << stats.worstFPS << std::endl;
			sstr << "Triangles:\t"	<< Ogre::StringConverter::toString(stats.triangleCount) << std::endl;
			sstr << "Batches:\t"	<< Ogre::StringConverter::toString(stats.batchCount)	<< std::endl;
			this->stats->setText(sstr.str());
		}
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onKeyPressed(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
		foreach(FastGuiWidgetMap::value_type const & vt, this->widgets)
		{
			vt.second->onKeyPressed(*data);
		}
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onKeyReleased(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
		foreach(FastGuiWidgetMap::value_type const & vt, this->widgets)
		{
			vt.second->onKeyReleased(*data);
		}
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onMousePressed(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(FastGuiWidgetMap::value_type const & vt, this->widgets)
		{
			vt.second->onCursorPressed(cursorPos);
		}
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onMouseReleased(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(FastGuiWidgetMap::value_type const & vt, this->widgets)
		{
			vt.second->onCursorReleased(cursorPos);
		}
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onMouseMoved(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		if(this->cursor)
		{
			this->cursor->setPosition((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		}
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(FastGuiWidgetMap::value_type const & vt, this->widgets)
		{
			vt.second->onCursorMoved(cursorPos);
		}
		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiManager)
	OOBJECT_END
}