/********************************************************************
	created:	Tuesday 2010/10/26 at 18:25
	filename: 	GuiManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
	
	purpose:	
*********************************************************************/

#include "engine_gui/GuiManager.h"
#include "engine_gui/FontAtlas.h"
#include "engine_render/RenderSystem.h"
#include <OgreString.h>
#include <OgreStringConverter.h>
#include "engine_input/InputManager.h"
#include "engine_gui/GuiTextEntry.h"
#include "engine_graphic/Engine.h"
#include <core_util/foreach.h>
#include <core_game/GameStateManager.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace Orkige
{
	IMPL_OSINGLETON(GuiManager);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiManager::GuiManager(optr<GuiFactory> _factory, String const & _defaultAtlas, String const & group) : factory(_factory), defaultAtlas(_defaultAtlas), statsMarkupColorIndex(0), cancelInputUpdate(false), scaleStats(false), focusedTextEntry(NULL), textEntryFocusClaimed(false), layoutRootSpace(RS_FullWindow), layoutDirty(true), lastLayoutWidth(0), lastLayoutHeight(0)
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
		this->registerEvent(Orkige::Engine::FrameRenderingQueuedEvent,			&GuiManager::onFrameRenderingQueued,	this);
		this->registerEvent(Orkige::GameStateManager::GameStateChangedEvent,	&GuiManager::onGameStateChanged,		this);
		this->registerEvent(Orkige::Engine::FrameStartedEvent,					&GuiManager::onFrameStarted,			this);
		// (the per-viewport render gating is gone: the DrawLayer2D facade
		// composites onto the main window only, on every render flavor)
	}
	//---------------------------------------------------------
	GuiManager::~GuiManager()
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
	void GuiManager::enableInputEvents()
	{
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,				&GuiManager::onKeyPressed,				this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,				&GuiManager::onKeyReleased,				this);
		this->registerEvent(Orkige::InputManager::TextInputEvent,				&GuiManager::onTextInput,				this);
#if defined(ORKIGE_IPHONE) || defined(__ANDROID__)
		this->registerEvent(Orkige::InputManager::TouchPressedEvent,			&GuiManager::onTouchPressed,			this);
		this->registerEvent(Orkige::InputManager::TouchReleasedEvent,			&GuiManager::onTouchReleased,			this);
		this->registerEvent(Orkige::InputManager::TouchMovedEvent,				&GuiManager::onTouchMoved,				this);
#else
		this->registerEvent(Orkige::InputManager::MousePressedEvent,			&GuiManager::onMousePressed,			this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,			&GuiManager::onMouseReleased,			this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,				&GuiManager::onMouseMoved,				this);
#endif
		this->refreshCursor();
	}
	//---------------------------------------------------------
	void GuiManager::disableInputEvents()
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
	void GuiManager::showCursor(String const & atlas, String const & sprite)
	{
		optr<GuiView> view = this->getCreateView(atlas).lock();
		oAssert(view);
		UiSprite const * spriteObject = view->getScreen()->getAtlas()->getSprite(sprite);
		oAssert(spriteObject);
		this->cursor = onew(new GuiDecorWidget("Cursor", sprite, Ogre::Vector2::ZERO, Ogre::Vector2(spriteObject->spriteWidth, spriteObject->spriteHeight), atlas, 16));
	}
	//---------------------------------------------------------
	void GuiManager::hideCursor()
	{
		this->cursor.reset();
	}
	//---------------------------------------------------------
	bool GuiManager::isCursorVisible()
	{
		if(this->cursor && this->cursor->getLayer()->isVisible())
		{
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	void GuiManager::refreshCursor()
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
	woptr<GuiView> GuiManager::createView(String const & atlas, String const & group)
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
		optr<GuiView> view = onew(new GuiView(screen));
		this->views[atlas] = view;
		return view;
	}
	//---------------------------------------------------------
	void GuiManager::destroyView(String const & atlas)
	{
		woptr<GuiView> view = this->getView(atlas);
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
	void GuiManager::destroyViewWithWidgets(String const & atlas)
	{
		woptr<GuiView> view = this->getView(atlas);
		oAssert(view.lock());
		UiScreen* screen = view.lock()->getScreen();
		
		StringVector names;
		foreach(GuiWidgetMap::value_type const & vt, this->widgets)
		{
			// compare used view/atlas
			//woptr<GuiView> view = vt.second->getView();
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
	void GuiManager::destroyAllViews(bool keepDefaultAtlas)
	{
		this->destroyAllWidgets();
		StringVector names;
		foreach(GuiViewMap::value_type const & vt, this->views)
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
	bool GuiManager::addWidget(optr<GuiWidget> widget)
	{
		if(this->widgets.find(widget->getObjectID()) != this->widgets.end())
		{
			return false;
		}
		this->widgets[widget->getObjectID()] = widget;
		this->sortedWidgets.push_back(widget);
		this->sortedWidgets.sort(GuiWidgetOptrCmp());
		return true;
	}
	//---------------------------------------------------------
	bool GuiManager::destroyWidget(String const & id)
	{
		GuiWidgetMap::iterator it = this->widgets.find(id);
		if(it == this->widgets.end())
		{
			return false;
		}
		GuiWidgetList::iterator it2 = std::find(this->sortedWidgets.begin(), this->sortedWidgets.end(), it->second);
		this->widgets.erase(it);
		this->sortedWidgets.erase(it2);
		return true;
	}
	//---------------------------------------------------------
	void GuiManager::destroyAllWidgets()
	{
		StringVector names;
		foreach(GuiWidgetMap::value_type const & vt, this->widgets)
		{
			names.push_back(vt.first);
		}
		foreach(String const & name, names)
		{
			this->destroyWidget(name);
		}
	}
	//---------------------------------------------------------
	void GuiManager::showStats(uint glyphIndex, Ogre::Vector2 const & pos, String const & atlas, unsigned short markupColorIndex, bool scaleStats)
	{
		if(this->stats)
		{
			this->stats.reset();
			this->statsValues.reset();
		}
        this->scaleStats = scaleStats;
		this->statsMarkupColorIndex = markupColorIndex;
		this->stats = onew(new GuiTextbox("GuiManagerFrameStats", glyphIndex, "", pos, atlas, 15, false));
		this->statsValues = onew(new GuiTextbox("GuiManagerFrameStatsValues", glyphIndex, "", pos, atlas, 15, false));
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
	void GuiManager::hideStats()
	{
		this->stats.reset();
		this->statsValues.reset();
	}
	//---------------------------------------------------------
	void GuiManager::resetStats()
	{
		RenderSystem::get()->resetFrameStats();
	}
	//---------------------------------------------------------
	void GuiManager::updateStats()
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
	void GuiManager::reorderViews()
	{
		GuiViewList orderedViews;
		foreach(GuiViewMap::value_type const & vt, this->views)
		{
			orderedViews.push_back(vt.second);
		}
		orderedViews.sort(GuiViewOptrCmp());

		// same order the RenderQueueListener re-registration produced: the
		// draw layers composite in ascending zOrder
		int order = 0;
		foreach(optr<GuiView> & view, orderedViews)
		{
			view->getScreen()->setZOrder(order++);
		}
	}
	//---------------------------------------------------------
	void GuiManager::hideAllViews()
	{
		foreach(GuiViewMap::value_type const & vt, this->views)
		{
			vt.second->getScreen()->setVisible(false);
		}
	}
	//---------------------------------------------------------
	void GuiManager::showAllViews()
	{
		foreach(GuiViewMap::value_type const & vt, this->views)
		{
			vt.second->getScreen()->setVisible(true);
		}
	}
	//---------------------------------------------------------
	void GuiManager::replaceAtlasTexture(const Ogre::String& atlas, const Ogre::String& texture)
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
		optr<GuiView> view = this->getView(atlas).lock();
		if(view)
		{
			view->getScreen()->requestFullRedraw();
		}
	}
	//---------------------------------------------------------
	void GuiManager::cancelCurrentInputUpdate()
	{
		this->cancelInputUpdate = true;
	}
	//---------------------------------------------------------
	bool GuiManager::isPointOverWidget(Ogre::Vector2 const & point)
	{
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
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
	std::vector<GuiManager::WidgetLayout> GuiManager::getWidgetLayouts()
	{
		std::vector<WidgetLayout> layouts;
		layouts.reserve(this->widgets.size());
		foreach(GuiWidgetMap::value_type const & entry, this->widgets)
		{
			optr<GuiWidget> widget = entry.second;
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
	void GuiManager::setDesignResolution(float designWidth,
		float designHeight, float matchWidthHeight)
	{
		this->layoutPolicy.designWidth = designWidth;
		this->layoutPolicy.designHeight = designHeight;
		this->layoutPolicy.matchWidthHeight = matchWidthHeight;
		this->markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiManager::setLayoutMatchMode(int mode)
	{
		switch(mode)
		{
		case LayoutScalePolicy::MM_SHRINK:
			this->layoutPolicy.mode = LayoutScalePolicy::MM_SHRINK;
			break;
		case LayoutScalePolicy::MM_EXPAND:
			this->layoutPolicy.mode = LayoutScalePolicy::MM_EXPAND;
			break;
		default:
			this->layoutPolicy.mode = LayoutScalePolicy::MM_MATCH;
			break;
		}
		this->markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiManager::setRootSpace(String const & space)
	{
		String key = space;
		Ogre::StringUtil::toLowerCase(key);
		this->layoutRootSpace = (key == "safearea" || key == "safe")
			? RS_SafeArea : RS_FullWindow;
		this->markLayoutDirty();
	}
	//---------------------------------------------------------
	float GuiManager::getLayoutScale() const
	{
		unsigned int windowWidth = 0, windowHeight = 0;
		RenderSystem::get()->getWindowSize(windowWidth, windowHeight);
		return this->layoutPolicy.referenceScale(Real(windowWidth),
			Real(windowHeight));
	}
	//---------------------------------------------------------
	void GuiManager::markLayoutDirty()
	{
		this->layoutDirty = true;
	}
	//---------------------------------------------------------
	void GuiManager::resolveLayouts()
	{
		// re-resolve only when a layout property changed or the window resized
		unsigned int windowWidth = 0, windowHeight = 0;
		RenderSystem::get()->getWindowSize(windowWidth, windowHeight);
		if(windowWidth != this->lastLayoutWidth ||
			windowHeight != this->lastLayoutHeight)
		{
			this->lastLayoutWidth = windowWidth;
			this->lastLayoutHeight = windowHeight;
			this->layoutDirty = true;
		}
		if(!this->layoutDirty)
		{
			return;
		}
		this->layoutDirty = false;

		const float layoutScale = this->layoutPolicy.referenceScale(
			Real(windowWidth), Real(windowHeight));
		LayoutRect fullRoot;
		fullRoot.x = 0.0f;
		fullRoot.y = 0.0f;
		fullRoot.w = Real(windowWidth);
		fullRoot.h = Real(windowHeight);
		// the safe root subsumes the manual "+ safe.mLeft" HUD math: a widget
		// anchored to it stays off the notch/home bar with no script maths
		LayoutRect safeRoot = fullRoot;
		if(Engine::getSingletonPtr())
		{
			const SafeAreaInsets insets = Engine::getSingleton().getSafeAreaInsets();
			safeRoot.x = Real(insets.mLeft);
			safeRoot.y = Real(insets.mTop);
			safeRoot.w = Real(windowWidth) - Real(insets.mLeft) - Real(insets.mRight);
			safeRoot.h = Real(windowHeight) - Real(insets.mTop) - Real(insets.mBottom);
		}

		// build a transient LayoutItem forest from the opted-in widgets. std::map
		// node pointers stay stable across inserts, so children link by address.
		std::map<GuiWidget*, LayoutItem> items;
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
		{
			if(!widget->isLayoutEnabled())
			{
				continue;
			}
			LayoutItem & item = items[widget.get()];
			item.node = widget->getLayoutNode();
			item.group = widget->getLayoutGroup();
			item.fit = widget->getContentFit();
			const Ogre::Vector2 preferred = widget->getPreferredSize();
			item.contentSize.x = preferred.x;
			item.contentSize.y = preferred.y;
			item.scrollOffset = widget->getScrollOffset();
			item.userData = widget.get();
		}
		// link children to their layout parents (a non-layout / absent parent
		// leaves the widget a root, resolved against the screen root below)
		std::set<GuiWidget*> isChild;
		for(std::map<GuiWidget*, LayoutItem>::value_type & entry : items)
		{
			GuiWidget* widget = entry.first;
			optr<GuiWidget> parent = widget->getLayoutParent().lock();
			if(parent && parent->isLayoutEnabled())
			{
				std::map<GuiWidget*, LayoutItem>::iterator pit =
					items.find(parent.get());
				if(pit != items.end())
				{
					pit->second.children.push_back(&entry.second);
					isChild.insert(widget);
				}
			}
		}
		// resolve each root subtree top-down (two-pass inside resolveTree)
		for(std::map<GuiWidget*, LayoutItem>::value_type & entry : items)
		{
			GuiWidget* widget = entry.first;
			if(isChild.count(widget) != 0)
			{
				continue;
			}
			LayoutRect parentRect;
			optr<GuiWidget> parent = widget->getLayoutParent().lock();
			if(parent)
			{
				// a non-layout parent contributes its current pixel rect
				const Ogre::Vector2 pos = parent->getPosition();
				const Ogre::Vector2 size = parent->getSize();
				parentRect.x = pos.x;
				parentRect.y = pos.y;
				parentRect.w = size.x;
				parentRect.h = size.y;
			}
			else
			{
				parentRect = (widget->getUseSafeArea() ||
					this->layoutRootSpace == RS_SafeArea) ? safeRoot : fullRoot;
			}
			resolveTree(entry.second, parentRect, layoutScale);
		}
		// write the resolved rects back, then hand scroll viewports their
		// viewport + content extent (its first child's preferred size)
		for(std::map<GuiWidget*, LayoutItem>::value_type & entry : items)
		{
			GuiWidget* widget = entry.first;
			LayoutItem const & item = entry.second;
			widget->applyResolvedRect(item.resolved.x, item.resolved.y,
				item.resolved.w, item.resolved.h);
		}
		for(std::map<GuiWidget*, LayoutItem>::value_type & entry : items)
		{
			GuiWidget* widget = entry.first;
			LayoutItem const & item = entry.second;
			Ogre::Vector2 viewport(item.resolved.w, item.resolved.h);
			Ogre::Vector2 content = viewport;
			if(!item.children.empty())
			{
				content.x = item.children[0]->preferred.x;
				content.y = item.children[0]->preferred.y;
			}
			widget->onLayoutResolved(viewport, content);
		}
	}
	//---------------------------------------------------------
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool GuiManager::onFrameStarted(Orkige::Event const & event)
	{
		optr<FrameEventData> data = event.getDataPtr<FrameEventData>();
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
		{
			widget->onFrameStarted(*data);
		}
		// resolve the opt-in rect-anchor layout tree to absolute pixels BEFORE
		// the screens rebuild (each resolved widget's setPosition/setSize
		// dirties its screen, which rebuilds just below). O(n) over the layout
		// widgets, and only when a layout property changed or the window
		// resized - steady clean frames skip it entirely.
		this->resolveLayouts();
		// after the widgets updated: rebuild dirty screens and resubmit
		// their vertices to the DrawLayer2D facade for this frame (clean
		// screens return immediately - no rebuild, no upload)
		foreach(GuiViewMap::value_type const & vt, this->views)
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
	bool GuiManager::onFrameRenderingQueued(Orkige::Event const & event)
	{
		this->updateStats();
		return false;
	}
	//---------------------------------------------------------
	bool GuiManager::onGameStateChanged(Orkige::Event const & event)
	{
		this->resetStats();
		return false;
	}
	//---------------------------------------------------------
	bool GuiManager::onKeyPressed(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
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
	bool GuiManager::onKeyReleased(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
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
	bool GuiManager::onTextInput(Orkige::Event const & event)
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
	void GuiManager::focusTextEntry(GuiTextEntry* entry)
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
	void GuiManager::notifyTextEntryDestroyed(GuiTextEntry* entry)
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
	bool GuiManager::onMousePressed(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		// tap-away blur: a press that no field claims clears the focus
		this->textEntryFocusClaimed = false;
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
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
	bool GuiManager::onMouseReleased(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
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
	bool GuiManager::onMouseMoved(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		if(this->cursor)
		{
			this->cursor->setPosition((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		}
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			widget->onCursorMoved(cursorPos);
		}
		// the mouse wheel arrives as a moved event carrying a z delta; route it
		// to scroll viewports (a no-op on every other widget)
		if(data->relZ != 0)
		{
			foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
			{
				widget->onMouseWheel(cursorPos, data->relZ);
			}
		}
		this->cancelInputUpdate = false;
		return false;
	}
	//---------------------------------------------------------
	bool GuiManager::onTouchPressed(Orkige::Event const & event)
	{
		optr<TouchEventData> data = event.getDataPtr<TouchEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		// tap-away blur (same as the mouse path): an unclaimed tap clears focus
		this->textEntryFocusClaimed = false;
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
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
	bool GuiManager::onTouchReleased(Orkige::Event const & event)
	{
		optr<TouchEventData> data = event.getDataPtr<TouchEventData>();
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
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
	bool GuiManager::onTouchMoved(Orkige::Event const & event)
	{
		optr<TouchEventData> data = event.getDataPtr<TouchEventData>();
		if(this->cursor)
		{
			this->cursor->setPosition((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		}
		Ogre::Vector2 cursorPos((Ogre::Real)data->absX, (Ogre::Real)data->absY);
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
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
	OABSTRACT_IMPL(GuiManager)
		// Lua boots the UI: GuiManager(factory, atlas, resourceGroup) -
		// the atlas .ogui/.png pair must live in that resource group (a
		// project's assets/ register in "OrkigeProject")
		OCONSTRUCTOR3(optr<GuiFactory>, String const &, String const &)
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
		// rect-anchor layout policy: design resolution + match factor drive the
		// layout scale; root space decides what parentless widgets pin to
		OFUNC(setDesignResolution)
		OFUNC(setLayoutMatchMode)
		OFUNC(setRootSpace)
		OFUNC(getLayoutScale)
	OOBJECT_END
}