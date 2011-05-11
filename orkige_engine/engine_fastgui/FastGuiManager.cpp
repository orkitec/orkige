/********************************************************************
	created:	Tuesday 2010/10/26 at 18:25
	filename: 	FastGuiManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
	
	purpose:	
*********************************************************************/

#include "engine_fastgui/FastGuiManager.h"
#include "engine_graphic/Engine.h"
#include <core_util/foreach.h>
#include <core_game/GameStateManager.h>

namespace Orkige
{
	IMPL_OSINGLETON(FastGuiManager);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiManager::FastGuiManager(optr<FastGuiFactory> _factory, String const & _defaultAtlas, String const & group) : factory(_factory), defaultAtlas(_defaultAtlas), statsMarkupColorIndex(0), cancelInputUpdate(false)
	{
		oAssert(this->factory);
		this->silverback = onew(new Gorilla::Silverback());
		this->getCreateView(this->defaultAtlas, group);
		this->registerEvent(Orkige::Engine::FrameRenderingQueuedEvent,			&FastGuiManager::onFrameRenderingQueued,	this);
		this->registerEvent(Orkige::GameStateManager::GameStateChangedEvent,	&FastGuiManager::onGameStateChanged,		this);
		this->registerEvent(Orkige::Engine::FrameStartedEvent,					&FastGuiManager::onFrameStarted,			this);
	}
	//---------------------------------------------------------
	FastGuiManager::~FastGuiManager()
	{
		this->unregisterEvent(Orkige::Engine::FrameStartedEvent);
		this->unregisterEvent(Orkige::GameStateManager::GameStateChangedEvent);
		this->unregisterEvent(Orkige::Engine::FrameRenderingQueuedEvent);
	}
	//---------------------------------------------------------
	void FastGuiManager::enableInputEvents()
	{
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,				&FastGuiManager::onKeyPressed,				this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,				&FastGuiManager::onKeyReleased,				this);
#ifdef ORKIGE_IPHONE
		this->registerEvent(Orkige::InputManager::TouchPressedEvent,			&FastGuiManager::onTouchPressed,			this);
		this->registerEvent(Orkige::InputManager::TouchReleasedEvent,			&FastGuiManager::onTouchReleased,			this);
		this->registerEvent(Orkige::InputManager::TouchMovedEvent,				&FastGuiManager::onTouchMoved,				this);
#else
		this->registerEvent(Orkige::InputManager::MousePressedEvent,			&FastGuiManager::onMousePressed,			this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,			&FastGuiManager::onMouseReleased,			this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,				&FastGuiManager::onMouseMoved,				this);
#endif
		this->refreshCursor();
	}
	//---------------------------------------------------------
	void FastGuiManager::disableInputEvents()
	{
		this->unregisterEvent(Orkige::InputManager::KeyPressedEvent);
		this->unregisterEvent(Orkige::InputManager::KeyReleasedEvent);
#ifdef ORKIGE_IPHONE
		this->unregisterEvent(Orkige::InputManager::TouchPressedEvent);
		this->unregisterEvent(Orkige::InputManager::TouchReleasedEvent);
		this->unregisterEvent(Orkige::InputManager::TouchMovedEvent);
#else
		this->unregisterEvent(Orkige::InputManager::MousePressedEvent);
		this->unregisterEvent(Orkige::InputManager::MouseReleasedEvent);
		this->unregisterEvent(Orkige::InputManager::MouseMovedEvent);
#endif
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
		if(this->cursor)
		{
			this->cursor->setPosition((Ogre::Real)InputManager::getSingleton().getLastTouchData()->absX, (Ogre::Real)InputManager::getSingleton().getLastTouchData()->absY);
		}
#else
		if(this->cursor)
		{
			this->cursor->setPosition((Ogre::Real)InputManager::getSingleton().getMouseData()->absX, (Ogre::Real)InputManager::getSingleton().getMouseData()->absY);
		}
#endif
	}
	//---------------------------------------------------------
	woptr<FastGuiView> FastGuiManager::createView(String const & atlas, String const & group)
	{
		oAssertDesc(!this->hasView(atlas), "Screen with atlas: " << atlas << " already exists!");
		this->silverback->loadAtlas(atlas, group);
		Ogre::Viewport* viewport = Engine::getSingleton().getViewport();
		oAssert(viewport);
		Gorilla::Screen* screen = this->silverback->createScreen(viewport, atlas);
		oAssert(screen);
#if OGRE_NO_VIEWPORT_ORIENTATIONMODE == 0
		screen->setOrientation(viewport->getOrientationMode());
#endif
		optr<FastGuiView> view = onew(new FastGuiView(screen));
		this->views[atlas] = view;
		return view;
	}
	//---------------------------------------------------------
	void FastGuiManager::destroyView(String const & atlas)
	{
		woptr<FastGuiView> view = this->getView(atlas);
		oAssert(view.lock());
		Gorilla::Screen* screen = view.lock()->getScreen();
		this->views.erase(this->views.find(atlas));

		Ogre::TextureManager::getSingletonPtr()->remove(screen->getAtlas()->getTexture()->getName());
		Ogre::MaterialManager::getSingletonPtr()->remove(screen->getAtlas()->get2DMaterial()->getName());
		Ogre::MaterialManager::getSingletonPtr()->remove(screen->getAtlas()->get3DMaterial()->getName());

		this->silverback->destroyScreen(screen);
		this->silverback->destroyAtlas(atlas);
	}
	//---------------------------------------------------------
	void FastGuiManager::destroyViewWithWidgets(String const & atlas)
	{
		woptr<FastGuiView> view = this->getView(atlas);
		oAssert(view.lock());
		Gorilla::Screen* screen = view.lock()->getScreen();
		
		StringVector names;
		foreach(FastGuiWidgetMap::value_type const & vt, this->widgets)
		{
			// compare used view/atlas
			//woptr<FastGuiView> view = vt.second->getView();
			if (vt.second->getView().lock()->getScreen() == screen)
			{
				names.push_back(vt.first);
			}
		}
		foreach(String const & name, names)
		{
			this->destroyWidget(name);
		}

		this->destroyView(atlas);
	}	
	//---------------------------------------------------------
	void FastGuiManager::destroyAllViews(bool keepDefaultAtlas)
	{
		this->destroyAllWidgets();
		StringVector names;
		foreach(FastGuiViewMap::value_type const & vt, this->views)
		{
			if(keepDefaultAtlas && vt.first == this->defaultAtlas)
			{
				continue;
			}
			
			names.push_back(vt.first);
		}
		foreach(String const & name, names)
		{
			this->destroyView(name);
		}
	}
	//---------------------------------------------------------
	bool FastGuiManager::addWidget(optr<FastGuiWidget> widget)
	{
		if(this->widgets.find(widget->getObjectID()) != this->widgets.end())
		{
			return false;
		}
		this->widgets[widget->getObjectID()] = widget;
		this->sortedWidgets.push_back(widget);
		this->sortedWidgets.sort(FastGuiWidgetOptrCmp());
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
		FastGuiWidgetList::iterator it2 = std::find(this->sortedWidgets.begin(), this->sortedWidgets.end(), it->second);
		this->widgets.erase(it);
		this->sortedWidgets.erase(it2);
		return true;
	}
	//---------------------------------------------------------
	void FastGuiManager::destroyAllWidgets()
	{
		StringVector names;
		foreach(FastGuiWidgetMap::value_type const & vt, this->widgets)
		{
			names.push_back(vt.first);
		}
		foreach(String const & name, names)
		{
			this->destroyWidget(name);
		}
	}
	//---------------------------------------------------------
	void FastGuiManager::showStats(uint glyphIndex, Ogre::Vector2 const & pos, String const & atlas, unsigned short markupColorIndex)
	{
		if(this->stats)
		{
			this->stats.reset();
			this->statsValues.reset();
		}
	
		this->statsMarkupColorIndex = markupColorIndex;
		this->stats = onew(new FastGuiTextbox("FastGuiManagerFrameStats", glyphIndex, "", pos, atlas, 15));
		this->statsValues = onew(new FastGuiTextbox("FastGuiManagerFrameStatsValues", glyphIndex, "", pos, atlas, 15));
#if defined(ORKIGE_ENABLE_MEMORYMANAGER) && defined(WIN32)
		this->stats->setText("%"+Ogre::StringConverter::toString(this->statsMarkupColorIndex)+"FPS: \nAverage FPS: \nBest FPS: \nWorst FPS: \nTriangles: \nBatches: \nTextureMemory: \nOrkigeMemory: \nOrkigeMemoryPeak: ");
#else
		this->stats->setText("%"+Ogre::StringConverter::toString(this->statsMarkupColorIndex)+"FPS: \nAverage FPS: \nBest FPS: \nWorst FPS: \nTriangles: \nBatches: \nTextureMemory: ");
#endif

		this->stats->getMarkupText()->_calculateCharacters();
		Ogre::Vector2 size = this->stats->getSize();
		size.y = 0.f;
		size += this->stats->getPosition();
		this->statsValues->setPosition(size.x, size.y);
	}
	//---------------------------------------------------------
	void FastGuiManager::hideStats()
	{
		this->stats.reset();
		this->statsValues.reset();
	}
	//---------------------------------------------------------
	void FastGuiManager::resetStats()
	{
		Engine::getSingleton().getRenderWindow()->resetStatistics();
	}
	//---------------------------------------------------------
	void FastGuiManager::updateStats()
	{
		if(this->stats)
		{
			Ogre::RenderTarget::FrameStats stats = Engine::getSingleton().getRenderWindow()->getStatistics();
			std::stringstream sstr;
			sstr << "%" << this->statsMarkupColorIndex;
			sstr << "   "	<< std::fixed << std::setprecision(1) << stats.lastFPS	<< std::endl;
			sstr << "   "	<< std::fixed << std::setprecision(1) << stats.avgFPS	<< std::endl;
			sstr << "   "	<< std::fixed << std::setprecision(1) << stats.bestFPS	<< std::endl;
			sstr << "   "	<< std::fixed << std::setprecision(1) << stats.worstFPS << std::endl;
			sstr << "   "	<< stats.triangleCount << std::endl;
			sstr << "   "	<< stats.batchCount << std::endl;
			sstr << "   "	<< std::fixed << std::setprecision(4) << (Ogre::TextureManager::getSingleton().getMemoryUsage()/1024.f)/1024.f<< "  mb" << std::endl;
#ifdef ORKIGE_ENABLE_MEMORYMANAGER
#ifdef WIN32
			sstr << "   "	<< std::fixed << std::setprecision(4) << (MemoryManager::getSingleton().getMemoryStatistics().totalActualMemory/1024.f)/1024.f<< "  mb" << std::endl;
			sstr << "   "	<< std::fixed << std::setprecision(4) << (MemoryManager::getSingleton().getMemoryStatistics().peakActualMemory/1024.f)/1024.f<< "  mb" << std::endl;
#endif
#endif

			this->statsValues->setText(sstr.str());
		}
	}
	//---------------------------------------------------------
	void FastGuiManager::reorderViews()
	{
		FastGuiViewList orderedViews;
		foreach(FastGuiViewMap::value_type const & vt, this->views)
		{
			orderedViews.push_back(vt.second);
			Engine::getSingleton().getSceneManager()->removeRenderQueueListener(vt.second->getScreen());
		}
		orderedViews.sort(FastGuiViewOptrCmp());

		foreach(optr<FastGuiView> & view, orderedViews)
		{
			//oDebugMsg("core", 0, "View: " << view.getScreen()->getAtlas()->get2DMaterialName());
			Gorilla::Screen* screen = view->getScreen();
			Engine::getSingleton().getSceneManager()->addRenderQueueListener(screen);
		}
	}
	//---------------------------------------------------------
	void FastGuiManager::replaceAtlasTexture(const Ogre::String& atlas, const Ogre::String& texture)
	{
		oAssertDesc(this->hasView(atlas), "replaceAtlasTexture: atlas not found");
		this->silverback->replaceTexture(atlas, texture);
	}
	//---------------------------------------------------------
	void FastGuiManager::cancelCurrentInputUpdate()
	{
		this->cancelInputUpdate = true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool FastGuiManager::onFrameStarted(Orkige::Event const & event)
	{
		optr<FrameEventData> data = event.getDataPtr<FrameEventData>();
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			widget->onFrameStarted(*data);
		}
		return false;
	}	
	//---------------------------------------------------------
	bool FastGuiManager::onFrameRenderingQueued(Orkige::Event const & event)
	{
		this->updateStats();
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onGameStateChanged(Orkige::Event const & event)
	{
		this->resetStats();
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onKeyPressed(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onKeyPressed(*data);
		}
		this->cancelInputUpdate = false;
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onKeyReleased(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onKeyReleased(*data);
		}
		this->cancelInputUpdate = false;
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onMousePressed(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onCursorPressed(cursorPos);
		}
		this->cancelInputUpdate = false;
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onMouseReleased(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onCursorReleased(cursorPos);
		}
		this->cancelInputUpdate = false;
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
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onCursorMoved(cursorPos);
		}
		this->cancelInputUpdate = false;
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onTouchPressed(Orkige::Event const & event)
	{
		optr<TouchEventData> data = event.getDataPtr<TouchEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onCursorPressed(cursorPos);
		}
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onTouchReleased(Orkige::Event const & event)
	{
		optr<TouchEventData> data = event.getDataPtr<TouchEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onCursorReleased(cursorPos);
		}
		this->cancelInputUpdate = false;
		return false;
	}
	//---------------------------------------------------------
	bool FastGuiManager::onTouchMoved(Orkige::Event const & event)
	{
		optr<TouchEventData> data = event.getDataPtr<TouchEventData>();
		if(this->cursor)
		{
			this->cursor->setPosition((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		}
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onCursorMoved(cursorPos);
		}
		this->cancelInputUpdate = false;
		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiManager)
	OOBJECT_END
}