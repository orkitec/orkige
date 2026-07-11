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
#include "engine_fastgui/FontAtlas.h"
#include "engine_render/RenderSystem.h"
#include <OgreStringConverter.h>
#include "engine_input/InputManager.h"
#include "engine_fastgui/FastGuiTextEntry.h"
#include "engine_graphic/Engine.h"
#include <core_util/foreach.h>
#include <core_game/GameStateManager.h>
#include <algorithm>
#include <cmath>

namespace Orkige
{
	IMPL_OSINGLETON(FastGuiManager);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiManager::FastGuiManager(optr<FastGuiFactory> _factory, String const & _defaultAtlas, String const & group) : factory(_factory), defaultAtlas(_defaultAtlas), statsMarkupColorIndex(0), cancelInputUpdate(false), scaleStats(false), focusedTextEntry(NULL), textEntryFocusClaimed(false)
	{
		oAssert(this->factory);
		// drive the pixel-font UI scale from the display density so a 14px
		// glyph stays ~14pt physical on a retina / phone screen; integer-snap
		// keeps the point-filtered atlas crisp (fractional sampling blurs it).
		// Engine is the app singleton on both flavors; without one (a pure
		// headless atlas-parse test) the scale stays the default 1.
		if(Engine::getSingletonPtr())
		{
			const float contentScale = Engine::getSingleton().getContentScale();
			const float uiScale = std::max(1.0f, std::round(contentScale));
			UiGlyph::scale = Vec2(uiScale, uiScale);
		}
		this->getCreateView(this->defaultAtlas, group);
		this->registerEvent(Orkige::Engine::FrameRenderingQueuedEvent,			&FastGuiManager::onFrameRenderingQueued,	this);
		this->registerEvent(Orkige::GameStateManager::GameStateChangedEvent,	&FastGuiManager::onGameStateChanged,		this);
		this->registerEvent(Orkige::Engine::FrameStartedEvent,					&FastGuiManager::onFrameStarted,			this);
		// (the per-viewport render gating is gone: the DrawLayer2D facade
		// composites onto the main window only, on every render flavor)
	}
	//---------------------------------------------------------
	FastGuiManager::~FastGuiManager()
	{
		this->unregisterEvent(Orkige::Engine::FrameStartedEvent);
		this->unregisterEvent(Orkige::GameStateManager::GameStateChangedEvent);
		this->unregisterEvent(Orkige::Engine::FrameRenderingQueuedEvent);
		// teardown order: widgets touch their layers (owned by the views'
		// screens), the screens reference the atlases - so explicitly:
		// widgets first, then views (each deletes its screen), then atlases
		this->stats.reset();
		this->statsValues.reset();
		this->cursor.reset();
		this->sortedWidgets.clear();
		this->widgets.clear();
		this->views.clear();
		// the runtime atlases own the UiAtlas the screens referenced, so they
		// die only after every view is gone
		this->atlases.clear();
		this->fontAtlases.clear();
	}
	//---------------------------------------------------------
	void FastGuiManager::enableInputEvents()
	{
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,				&FastGuiManager::onKeyPressed,				this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,				&FastGuiManager::onKeyReleased,				this);
		this->registerEvent(Orkige::InputManager::TextInputEvent,				&FastGuiManager::onTextInput,				this);
#if defined(ORKIGE_IPHONE) || defined(__ANDROID__)
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
		this->unregisterEvent(Orkige::InputManager::TextInputEvent);
#if defined(ORKIGE_IPHONE) || defined(__ANDROID__)
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
		UiSprite const * spriteObject = view->getScreen()->getAtlas()->getSprite(sprite);
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
		// OGRE 14: viewport orientation modes are gone, so the old "skip while
		// orientation handling is missing" early-out is gone with them.
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
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
		// runtime-baked atlas (TTF fonts / SVG sprites declared in the .ogui)
		// vs. the classic bitmap .png atlas - one .ogui format, two builders.
		// The FontAtlas OWNS its UiAtlas view; a bitmap atlas is the UiAtlas.
		UiAtlas const * atlasView = NULL;
		const String oguiFile = atlas + ".ogui";
		if(FontAtlas::oguiDeclaresRuntimeContent(oguiFile, group))
		{
			optr<FontAtlas> fontAtlas = onew(new FontAtlas(oguiFile, group));
			this->fontAtlases[atlas] = fontAtlas;
			atlasView = fontAtlas->atlas();
		}
		else
		{
			optr<UiAtlas> uiAtlas = onew(new UiAtlas(oguiFile, group));
			this->atlases[atlas] = uiAtlas;
			atlasView = uiAtlas.get();
		}
		// one screen per atlas = ONE draw batch per view; the compositing
		// order among views is (re)assigned by reorderViews via setZOrder
		UiScreen* screen = new UiScreen(atlasView,
			RenderSystem::get()->createDrawLayer2D());
		optr<FastGuiView> view = onew(new FastGuiView(screen));
		this->views[atlas] = view;
		return view;
	}
	//---------------------------------------------------------
	void FastGuiManager::destroyView(String const & atlas)
	{
		woptr<FastGuiView> view = this->getView(atlas);
		oAssert(view.lock());
		// the view owns (and deletes) its screen; the screen's draw layer
		// dies with it (facade RAII). The atlas texture stays name-cached
		// in the backend (tiny UI atlases; recreating a view with the same
		// atlas reuses it)
		this->views.erase(this->views.find(atlas));
		this->atlases.erase(atlas);
		// the runtime atlas (if this view used one) outlived its screen: safe
		// to drop now the view is gone
		this->fontAtlases.erase(atlas);
	}
	//---------------------------------------------------------
	void FastGuiManager::destroyViewWithWidgets(String const & atlas)
	{
		woptr<FastGuiView> view = this->getView(atlas);
		oAssert(view.lock());
		UiScreen* screen = view.lock()->getScreen();
		
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
	void FastGuiManager::showStats(uint glyphIndex, Ogre::Vector2 const & pos, String const & atlas, unsigned short markupColorIndex, bool scaleStats)
	{
		if(this->stats)
		{
			this->stats.reset();
			this->statsValues.reset();
		}
        this->scaleStats = scaleStats;
		this->statsMarkupColorIndex = markupColorIndex;
		this->stats = onew(new FastGuiTextbox("FastGuiManagerFrameStats", glyphIndex, "", pos, atlas, 15, false));
		this->statsValues = onew(new FastGuiTextbox("FastGuiManagerFrameStatsValues", glyphIndex, "", pos, atlas, 15, false));
#if defined(ORKIGE_ENABLE_MEMORYMANAGER) && defined(WIN32)
		this->stats->setText("%"+Ogre::StringConverter::toString(this->statsMarkupColorIndex)+"FPS: \nAverage FPS: \nBest FPS: \nWorst FPS: \nTriangles: \nBatches: \nTextureMemory: \nOrkigeMemory: \nOrkigeMemoryPeak: \nResolution: ");
#else
		this->stats->setText("%"+Ogre::StringConverter::toString(this->statsMarkupColorIndex)+"FPS: \nAverage FPS: \nBest FPS: \nWorst FPS: \nTriangles: \nBatches: \nTextureMemory: ");
#endif

		// don't scale font by resolution
        Vec2 scaleBackup = UiGlyph::scale;
        if(!this->scaleStats)
        {
            UiGlyph::scale.x = UiGlyph::scale.y = 1.0f;   
        }

		this->stats->getMarkupText()->_calculateCharacters();
		Ogre::Vector2 size = this->stats->getSize();
		size.y = 0.f;
		size += this->stats->getPosition();
		this->statsValues->setPosition(size.x, size.y);

		UiGlyph::scale = scaleBackup;
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
		RenderSystem::get()->resetFrameStats();
	}
	//---------------------------------------------------------
	void FastGuiManager::updateStats()
	{
		if(this->stats)
		{
			RenderSystem::FrameStats stats = RenderSystem::get()->getFrameStats();
			std::stringstream sstr;
			sstr << "%" << this->statsMarkupColorIndex;
			sstr << "   "	<< std::fixed << std::setprecision(1) << stats.lastFPS	<< std::endl;
			sstr << "   "	<< std::fixed << std::setprecision(1) << stats.avgFPS	<< std::endl;
			sstr << "   "	<< std::fixed << std::setprecision(1) << stats.bestFPS	<< std::endl;
			sstr << "   "	<< std::fixed << std::setprecision(1) << stats.worstFPS << std::endl;
			sstr << "   "	<< stats.triangleCount << std::endl;
			sstr << "   "	<< stats.batchCount << std::endl;
			sstr << "   "	<< std::fixed << std::setprecision(4) << (stats.textureMemoryBytes/1024.f)/1024.f<< "  mb" << std::endl;
#ifdef ORKIGE_ENABLE_MEMORYMANAGER
#ifdef WIN32
			unsigned int statsWindowWidth = 0, statsWindowHeight = 0;
			RenderSystem::get()->getWindowSize(statsWindowWidth, statsWindowHeight);
			sstr << "   "	<< std::fixed << std::setprecision(4) << (MemoryManager::getSingleton().getMemoryStatistics().totalActualMemory/1024.f)/1024.f<< "  mb" << std::endl;
			sstr << "   "	<< std::fixed << std::setprecision(4) << (MemoryManager::getSingleton().getMemoryStatistics().peakActualMemory/1024.f)/1024.f<< "  mb" << std::endl;
			sstr << "   "	<< statsWindowWidth << " x " << statsWindowHeight << std::endl;
#endif
#endif

			// don't scale font by resolution
			Vec2 scaleBackup = UiGlyph::scale;
            if(!this->scaleStats)
            {
                UiGlyph::scale.x = UiGlyph::scale.y = 1.0f;   
            }

			this->statsValues->setText(sstr.str());
			this->statsValues->getMarkupText()->_calculateCharacters();

			UiGlyph::scale = scaleBackup;
		}
	}
	//---------------------------------------------------------
	void FastGuiManager::reorderViews()
	{
		FastGuiViewList orderedViews;
		foreach(FastGuiViewMap::value_type const & vt, this->views)
		{
			orderedViews.push_back(vt.second);
		}
		orderedViews.sort(FastGuiViewOptrCmp());

		// same order the RenderQueueListener re-registration produced: the
		// draw layers composite in ascending zOrder
		int order = 0;
		foreach(optr<FastGuiView> & view, orderedViews)
		{
			view->getScreen()->setZOrder(order++);
		}
	}
	//---------------------------------------------------------
	void FastGuiManager::hideAllViews()
	{
		foreach(FastGuiViewMap::value_type const & vt, this->views)
		{
			vt.second->getScreen()->setVisible(false);
		}
	}
	//---------------------------------------------------------
	void FastGuiManager::showAllViews()
	{
		foreach(FastGuiViewMap::value_type const & vt, this->views)
		{
			vt.second->getScreen()->setVisible(true);
		}
	}
	//---------------------------------------------------------
	void FastGuiManager::replaceAtlasTexture(const Ogre::String& atlas, const Ogre::String& texture)
	{
		oAssertDesc(this->hasView(atlas), "replaceAtlasTexture: atlas not found");
		std::map<String, optr<UiAtlas> >::iterator it = this->atlases.find(atlas);
		if(it == this->atlases.end())
		{
			return;
		}
		it->second->replaceTexture(texture);
		// the batch binds the texture by name: force the atlas' screen to
		// resubmit
		optr<FastGuiView> view = this->getView(atlas).lock();
		if(view)
		{
			view->getScreen()->requestFullRedraw();
		}
	}
	//---------------------------------------------------------
	void FastGuiManager::cancelCurrentInputUpdate()
	{
		this->cancelInputUpdate = true;
	}
	//---------------------------------------------------------
	bool FastGuiManager::isPointOverWidget(Ogre::Vector2 const & point)
	{
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			Ogre::Vector2 pos = widget->getPosition();
			Ogre::Vector2 size = widget->getSize();
			Ogre::Real left = pos.x;
			Ogre::Real top = pos.y;
			Ogre::Real right = pos.x + size.x;
			Ogre::Real bottom = pos.y + size.y;
			if(point.x >= left && point.x <= right && point.y >= top && point.y <= bottom)
			{
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	std::vector<FastGuiManager::WidgetLayout> FastGuiManager::getWidgetLayouts()
	{
		std::vector<WidgetLayout> layouts;
		layouts.reserve(this->widgets.size());
		foreach(FastGuiWidgetMap::value_type const & entry, this->widgets)
		{
			optr<FastGuiWidget> widget = entry.second;
			if(!widget)
			{
				continue;
			}
			WidgetLayout layout;
			layout.id = entry.first;
			Ogre::Vector2 position = widget->getPosition();
			Ogre::Vector2 size = widget->getSize();
			layout.left = position.x;
			layout.top = position.y;
			layout.width = size.x;
			layout.height = size.y;
			// on-screen visibility rides on the widget's z layer (games toggle
			// whole screens by their layer, @see the win-screen trick)
			layout.visible = widget->getLayer() != NULL
				? widget->getLayer()->isVisible() : true;
			layouts.push_back(layout);
		}
		return layouts;
	}
	//---------------------------------------------------------
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
		// after the widgets updated: rebuild dirty screens and resubmit
		// their vertices to the DrawLayer2D facade for this frame (clean
		// screens return immediately - no rebuild, no upload)
		foreach(FastGuiViewMap::value_type const & vt, this->views)
		{
			vt.second->getScreen()->update();
		}
		// screen rebuilds above may have baked new glyphs on demand into a
		// runtime atlas' page; push any changed page to the GPU before the
		// frame renders (a no-op when nothing was baked this frame)
		for(auto const & entry : this->fontAtlases)
		{
			entry.second->flush();
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
	bool FastGuiManager::onTextInput(Orkige::Event const & event)
	{
		// route committed text only to the focused field (a no-op when none is)
		if(this->focusedTextEntry)
		{
			optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
			this->focusedTextEntry->onTextInput(data->textInput);
		}
		return false;
	}
	//---------------------------------------------------------
	void FastGuiManager::focusTextEntry(FastGuiTextEntry* entry)
	{
		// a field claimed focus this press (used by the press handlers below to
		// distinguish a tap-on-field from a tap-away)
		if(entry)
		{
			this->textEntryFocusClaimed = true;
		}
		if(entry == this->focusedTextEntry)
		{
			return;
		}
		if(this->focusedTextEntry)
		{
			this->focusedTextEntry->setFocusedState(false);
		}
		this->focusedTextEntry = entry;
		if(entry)
		{
			entry->setFocusedState(true);
			if(InputManager::getSingletonPtr())
			{
				InputManager::getSingleton().startTextInput();
			}
		}
		else if(InputManager::getSingletonPtr())
		{
			InputManager::getSingleton().stopTextInput();
		}
	}
	//---------------------------------------------------------
	void FastGuiManager::notifyTextEntryDestroyed(FastGuiTextEntry* entry)
	{
		if(this->focusedTextEntry == entry)
		{
			this->focusedTextEntry = NULL;
			if(InputManager::getSingletonPtr())
			{
				InputManager::getSingleton().stopTextInput();
			}
		}
	}
	//---------------------------------------------------------
	bool FastGuiManager::onMousePressed(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		// tap-away blur: a press that no field claims clears the focus
		this->textEntryFocusClaimed = false;
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onCursorPressed(cursorPos);
		}
		if(!this->textEntryFocusClaimed && this->focusedTextEntry)
		{
			this->focusTextEntry(NULL);
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
		// tap-away blur (same as the mouse path): an unclaimed tap clears focus
		this->textEntryFocusClaimed = false;
		foreach(optr<FastGuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onCursorPressed(cursorPos);
		}
		if(!this->textEntryFocusClaimed && this->focusedTextEntry)
		{
			this->focusTextEntry(NULL);
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
		// Lua boots the UI: FastGuiManager(factory, atlas, resourceGroup) -
		// the atlas .ogui/.png pair must live in that resource group (a
		// project's assets/ register in "OrkigeProject")
		OCONSTRUCTOR3(optr<FastGuiFactory>, String const &, String const &)
		OSINGLETON()
		OFUNCWEAK(getFactory)
		OFUNC(enableInputEvents)
		OFUNC(disableInputEvents)
		OFUNC(widgetExists)
		OFUNC(destroyWidget)
		OFUNC(destroyAllWidgets)
		OFUNC(hideAllViews)
		OFUNC(showAllViews)
		OFUNC(isPointOverWidget)
	OOBJECT_END
}