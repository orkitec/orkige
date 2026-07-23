/********************************************************************
	created:	Tuesday 2010/10/26 at 18:25
	filename: 	GuiManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
	
	purpose:	
*********************************************************************/

#include "engine_gui/GuiManager.h"
#include "engine_gui/GuiWidgetHandle.h"	// widget -> WidgetHandle base mapping for the finders' OFUNCWEAK
#include "engine_gui/FontAtlas.h"
#include "engine_render/RenderSystem.h"
#include <OgreString.h>
#include <OgreStringConverter.h>
#include "engine_input/InputManager.h"
#include "engine_gui/GuiTextEntry.h"
#include "engine_gui/GuiModalScrim.h"
#include "engine_gui/GuiToggleGroup.h"
#include "engine_gui/GuiToast.h"
#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiLabel.h"
#include "engine_gui/GuiButton.h"
#include "engine_gui/GuiCheckBox.h"
#include "engine_gui/GuiSlider.h"
#include "engine_gui/GuiSelectMenu.h"
#include "engine_gui/GuiProgressBar.h"
#include "engine_gui/GuiScrollView.h"
#include "engine_graphic/Engine.h"
#include <core_util/foreach.h>
#include <core_util/StringTable.h>
#include <core_script/ScriptEventBus.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace Orkige
{
	IMPL_OSINGLETON(GuiManager);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiManager::GuiManager(optr<GuiFactory> _factory, String const & _defaultAtlas, String const & group, PreviewSurface const * previewSurface) : factory(_factory), defaultAtlas(_defaultAtlas), statsMarkupColorIndex(0), cancelInputUpdate(false), inputEventsEnabled(false), inputExplicitlySet(false), scaleStats(false), focusedTextEntry(NULL), modalSavedFocus(NULL), textEntryFocusClaimed(false), layoutRootSpace(RS_FullWindow), layoutDirty(true), lastLayoutWidth(0), lastLayoutHeight(0), modalSerial(0), groupAlphaDirty(false), screenExiting(false), previewActive(false), previewWidth(0), previewHeight(0)
	{
		oAssert(this->factory);
		// the editor GUI Preview stage points this whole manager at an offscreen
		// target and a simulated device context; a game leaves it null (the live
		// window). Set BEFORE the default view is created so its layer composites
		// into the right surface (@see createView).
		if(previewSurface && previewSurface->target &&
			previewSurface->width > 0 && previewSurface->height > 0)
		{
			this->previewActive = true;
			this->previewTarget = previewSurface->target;
			this->previewWidth = previewSurface->width;
			this->previewHeight = previewSurface->height;
			this->previewSafeArea = previewSurface->safeArea;
		}
		// drive the pixel-font UI scale from the display density so a 14px
		// glyph stays ~14pt physical on a retina / phone screen; integer-snap
		// keeps the point-filtered atlas crisp (fractional sampling blurs it).
		// The preview stage simulates a device density; otherwise Engine's
		// live content scale drives it (default 1 without an Engine - a pure
		// headless atlas-parse test).
		{
			float contentScale = 1.0f;
			if(this->previewActive)
			{
				contentScale = previewSurface->contentScale;
			}
			else if(Engine::getSingletonPtr())
			{
				contentScale = Engine::getSingleton().getContentScale();
			}
			const float uiScale = std::max(1.0f, std::round(contentScale));
			UiGlyph::scale = Vec2(uiScale, uiScale);
		}
		this->getCreateView(this->defaultAtlas, group);
		this->registerEvent(Orkige::Engine::FrameRenderingQueuedEvent,			&GuiManager::onFrameRenderingQueued,	this);
		this->registerEvent(Orkige::Engine::FrameStartedEvent,					&GuiManager::onFrameStarted,			this);
		// (the per-viewport render gating is gone: the DrawLayer2D facade
		// composites onto the main window only, on every render flavor)
	}
	//---------------------------------------------------------
	GuiManager::~GuiManager()
	{
		this->unregisterEvent(Orkige::Engine::FrameStartedEvent);
		this->unregisterEvent(Orkige::Engine::FrameRenderingQueuedEvent);
		// teardown order: widgets touch their layers (owned by the views'
		// screens), the screens reference the atlases - so explicitly:
		// widgets first, then views (each deletes its screen), then atlases
		this->stats.reset();
		this->statsValues.reset();
		this->cursor.reset();
		this->toast.reset();
		this->toggleGroups.clear();
		this->modalRecords.clear();
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
		// an explicit choice: from here the auto-enable stands down (a later
		// interactive widget will not re-toggle the state) and a subsequent
		// disableInputEvents still wins
		this->inputExplicitlySet = true;
		this->applyInputEnabled(true);
	}
	//---------------------------------------------------------
	void GuiManager::disableInputEvents()
	{
		this->inputExplicitlySet = true;
		this->applyInputEnabled(false);
	}
	//---------------------------------------------------------
	void GuiManager::applyInputEnabled(bool enable)
	{
		// the editor GUI Preview stage never runs game input - keep it exactly as
		// inert as it was before the auto-enable seam existed
		if(this->previewActive)
		{
			return;
		}
		if(enable == this->inputEventsEnabled)
		{
			return;	// idempotent: never double-register / double-unregister
		}
		this->inputEventsEnabled = enable;
		if(enable)
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
		else
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
		// order among views is (re)assigned by reorderViews via setZOrder.
		// The preview stage composites into its offscreen target at the
		// simulated device size instead of the live window.
		optr<DrawLayer2D> drawLayer = this->previewActive
			? this->previewTarget->createLayer()
			: RenderSystem::get()->createDrawLayer2D();
		UiScreen* screen = new UiScreen(atlasView, drawLayer);
		if(this->previewActive)
		{
			screen->setSurfaceSize(Real(this->previewWidth),
				Real(this->previewHeight));
		}
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
		// cache the widget's own weak self-handle so a Lua binding can recover a
		// shared_ptr from a base reference (the setParent seam)
		widget->bindWeakSelf(widget);
		this->widgets[widget->getObjectID()] = widget;
		this->sortedWidgets.push_back(widget);
		this->sortedWidgets.sort(GuiWidgetOptrCmp());
		// auto-enable input the first time a screen gains an interactive widget,
		// unless something already chose the input state explicitly (a Lua/C++
		// enable/disable or an .oui `input` key). A label/decor-only screen never
		// takes this path, so a pure HUD pays nothing. Explicit beats auto: once
		// inputExplicitlySet is true this never fires, so a deliberate `input off`
		// keeps sticking even as more interactive widgets are added.
		if(!this->inputExplicitlySet && !this->inputEventsEnabled &&
			widget->isInteractive())
		{
			this->applyInputEnabled(true);
		}
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
		// auto-kill: a destroyed widget's running animations stop with it (its
		// tween apply-closures re-fetch by id and would otherwise find nothing)
		this->cancelWidgetTweens(id);
		GuiWidgetList::iterator it2 = std::find(this->sortedWidgets.begin(), this->sortedWidgets.end(), it->second);
		this->widgets.erase(it);
		this->sortedWidgets.erase(it2);
		return true;
	}
	//---------------------------------------------------------
	woptr<GuiWidget> GuiManager::findWidget(String const & id)
	{
		GuiWidgetMap::iterator it = this->widgets.find(id);
		if(it != this->widgets.end())
		{
			return it->second;
		}
		return oNULL(GuiWidget);
	}
	//---------------------------------------------------------
	//--- typed finders: findWidget + a leaf-type requirement --
	// A dynamic_pointer_cast that yields empty for both an absent id and a
	// present-but-wrong-type id, so the returned handle is either the leaf a
	// script can drive or nil - never a base handle that silently lacks the
	// method the script is about to call.
	namespace
	{
		template<typename WidgetType>
		woptr<WidgetType> findWidgetLeaf(GuiManager & manager, String const & id)
		{
			optr<GuiWidget> widget = manager.findWidget(id).lock();
			return std::dynamic_pointer_cast<WidgetType>(widget);
		}
	}
	woptr<GuiLabel> GuiManager::findLabel(String const & id)
	{ return findWidgetLeaf<GuiLabel>(*this, id); }
	woptr<GuiButton> GuiManager::findButton(String const & id)
	{ return findWidgetLeaf<GuiButton>(*this, id); }
	woptr<GuiCheckBox> GuiManager::findCheckBox(String const & id)
	{ return findWidgetLeaf<GuiCheckBox>(*this, id); }
	woptr<GuiSlider> GuiManager::findSlider(String const & id)
	{ return findWidgetLeaf<GuiSlider>(*this, id); }
	woptr<GuiSelectMenu> GuiManager::findSelectMenu(String const & id)
	{ return findWidgetLeaf<GuiSelectMenu>(*this, id); }
	woptr<GuiProgressBar> GuiManager::findProgressBar(String const & id)
	{ return findWidgetLeaf<GuiProgressBar>(*this, id); }
	woptr<GuiDecorWidget> GuiManager::findDecor(String const & id)
	{ return findWidgetLeaf<GuiDecorWidget>(*this, id); }
	woptr<GuiTextEntry> GuiManager::findTextEntry(String const & id)
	{ return findWidgetLeaf<GuiTextEntry>(*this, id); }
	woptr<GuiScrollView> GuiManager::findScrollView(String const & id)
	{ return findWidgetLeaf<GuiScrollView>(*this, id); }
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
		this->stats->setText("%"+Ogre::StringConverter::toString(this->statsMarkupColorIndex)+"FPS: \nAverage FPS: \nBest FPS: \nWorst FPS: \nTriangles: \nBatches: \nTextureMemory: ");

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
	bool GuiManager::reloadLayout(String const & file, String & error)
	{
		if(!this->factory)
		{
			error = "no gui factory";
			return false;
		}
		if(!this->factory->reloadLayout(file, error))
		{
			return false;	// parse/read failure - the old screen stays up
		}
		// if the live (materialized) screen is exactly this .oui, refresh the
		// router's teardown set to the freshly built widget ids so a later
		// navigation still tears the whole (possibly grown/shrunk) screen down.
		if(!this->materializedScreen.empty())
		{
			String ouiPath;
			std::map<String, ScreenDef>::const_iterator it =
				this->screenDefs.find(this->materializedScreen);
			if(it != this->screenDefs.end())
			{
				ouiPath = it->second.ouiPath;
			}
			else if(this->materializedScreen.size() > 4 &&
				this->materializedScreen.compare(
					this->materializedScreen.size() - 4, 4, ".oui") == 0)
			{
				ouiPath = this->materializedScreen;	// an unregistered ".oui" is its own path
			}
			if(ouiPath == file)
			{
				this->screenWidgetIds = this->factory->layoutWidgetIds(file);
			}
		}
		return true;
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
	void GuiManager::runDeferred(std::function<void()> const & action)
	{
		if(action)
		{
			this->deferredActions.push_back(action);
		}
	}
	//---------------------------------------------------------
	namespace
	{
		//! resolve authored text: a leading '@' looks the rest up in the
		//! StringTable (backend-neutral localisation), else it is literal
		String resolveDialogText(String const & value)
		{
			if(!value.empty() && value[0] == '@')
			{
				const String key = value.substr(1);
				if(StringTable::getSingletonPtr() != NULL)
				{
					return StringTable::getSingleton().get(key);
				}
				return key;
			}
			return value;
		}

		//! @brief read a widget's current value on an animation channel (the tween
		//! `from`); @p count receives the channel count. Layout-driven widgets read
		//! their LAYOUT INPUTS (anchoredPosition / sizeDelta) so a tween composes
		//! with the resolver instead of fighting it.
		void readWidgetChannel(GuiWidget* widget, int channel, float* out,
			int& count)
		{
			switch(channel)
			{
			case GuiManager::WTC_Alpha:
				out[0] = widget->getGroupAlpha(); count = 1; break;
			case GuiManager::WTC_Scale:
				out[0] = widget->getRenderScaleX();
				out[1] = widget->getRenderScaleY(); count = 2; break;
			case GuiManager::WTC_Rotation:
				out[0] = widget->getRenderRotation(); count = 1; break;
			case GuiManager::WTC_Position:
				if(widget->isLayoutEnabled())
				{
					const LayoutVec2 ap = widget->getLayoutNode().anchoredPosition();
					out[0] = ap.x; out[1] = ap.y;
				}
				else
				{
					const Ogre::Vector2 p = widget->getPosition();
					out[0] = p.x; out[1] = p.y;
				}
				count = 2; break;
			case GuiManager::WTC_Size:
				if(widget->isLayoutEnabled())
				{
					const LayoutVec2 sd = widget->getLayoutNode().sizeDelta();
					out[0] = sd.x; out[1] = sd.y;
				}
				else
				{
					const Ogre::Vector2 s = widget->getSize();
					out[0] = s.x; out[1] = s.y;
				}
				count = 2; break;
			case GuiManager::WTC_Color:
			{
				GuiDecorWidget* decor = dynamic_cast<GuiDecorWidget*>(widget);
				const Color c = decor ? decor->getColour() : Color(1, 1, 1, 1);
				out[0] = c.r; out[1] = c.g; out[2] = c.b; out[3] = c.a;
				count = 4; break;
			}
			default:
				out[0] = 0.0f; count = 1; break;
			}
		}

		//! @brief write the eased channel value(s) back into the widget (the tween
		//! apply). Mirrors readWidgetChannel (layout inputs for a layout widget).
		void applyWidgetChannel(GuiWidget* widget, int channel,
			float const* values, int n)
		{
			switch(channel)
			{
			case GuiManager::WTC_Alpha:
				widget->setGroupAlpha(values[0]); break;
			case GuiManager::WTC_Scale:
				widget->setRenderScale(values[0], n > 1 ? values[1] : values[0]);
				break;
			case GuiManager::WTC_Rotation:
				widget->setRenderRotation(values[0]); break;
			case GuiManager::WTC_Position:
				if(widget->isLayoutEnabled())
					widget->setAnchoredPosition(values[0], values[1]);
				else
					widget->setPosition(values[0], values[1]);
				break;
			case GuiManager::WTC_Size:
				if(widget->isLayoutEnabled())
					widget->setSizeDelta(values[0], values[1]);
				else
					widget->setSize(values[0], values[1]);
				break;
			case GuiManager::WTC_Color:
			{
				GuiDecorWidget* decor = dynamic_cast<GuiDecorWidget*>(widget);
				if(decor)
				{
					decor->setColour(values[0], values[1], values[2],
						n > 3 ? values[3] : 1.0f);
				}
				break;
			}
			default: break;
			}
		}
	}
	//---------------------------------------------------------
	void GuiManager::markGroupAlphaDirty()
	{
		this->groupAlphaDirty = true;
	}
	//---------------------------------------------------------
	void GuiManager::resolveGroupAlpha()
	{
		if(!this->groupAlphaDirty)
		{
			return;
		}
		this->groupAlphaDirty = false;
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
		{
			widget->applyRenderAlpha(widget->getEffectiveAlpha());
		}
	}
	//---------------------------------------------------------
	void GuiManager::cancelWidgetTween(String const & widgetId, int channel)
	{
		std::map<String, std::map<int, TweenManager::TweenId> >::iterator it =
			this->widgetTweens.find(widgetId);
		if(it == this->widgetTweens.end())
		{
			return;
		}
		std::map<int, TweenManager::TweenId>::iterator cit =
			it->second.find(channel);
		if(cit == it->second.end())
		{
			return;
		}
		if(TweenManager::getSingletonPtr())
		{
			TweenManager::getSingleton().cancelTween(cit->second);
		}
		it->second.erase(cit);
		if(it->second.empty())
		{
			this->widgetTweens.erase(it);
		}
	}
	//---------------------------------------------------------
	void GuiManager::cancelWidgetTweens(String const & widgetId)
	{
		std::map<String, std::map<int, TweenManager::TweenId> >::iterator it =
			this->widgetTweens.find(widgetId);
		if(it == this->widgetTweens.end())
		{
			return;
		}
		if(TweenManager::getSingletonPtr())
		{
			for(std::map<int, TweenManager::TweenId>::value_type const & entry :
				it->second)
			{
				TweenManager::getSingleton().cancelTween(entry.second);
			}
		}
		this->widgetTweens.erase(it);
	}
	//---------------------------------------------------------
	TweenManager::TweenId GuiManager::tweenWidget(String const & widgetId,
		int channel, float const * toValues, float duration,
		Ease::Function ease, float delay,
		TweenManager::CompleteFunction const & onComplete)
	{
		TweenManager* manager = TweenManager::getSingletonPtr();
		if(!manager || !this->widgetExists(widgetId))
		{
			return 0;	// no tween tick (editor) or the widget is gone
		}
		optr<GuiWidget> widget = this->getWidget(widgetId).lock();
		if(!widget)
		{
			return 0;
		}
		float from[TweenManager::MAX_CHANNELS];
		int channels = 1;
		readWidgetChannel(widget.get(), channel, from, channels);

		// last-wins retarget: a running tween on this channel is replaced
		this->cancelWidgetTween(widgetId, channel);

		const String wid = widgetId;
		const int ch = channel;
		const TweenManager::TweenId id = manager->startTween(from, toValues,
			channels, duration, ease,
			[wid, ch](float const * values, int n) -> bool
			{
				// re-fetch by id every step (the widget may be destroyed between
				// frames); a missing widget stops the tween
				GuiManager* gui = GuiManager::getSingletonPtr();
				if(!gui || !gui->widgetExists(wid))
				{
					return false;
				}
				optr<GuiWidget> w = gui->getWidget(wid).lock();
				if(!w)
				{
					return false;
				}
				applyWidgetChannel(w.get(), ch, values, n);
				return true;
			},
			onComplete, delay, StringUtil::BLANK);
		this->widgetTweens[widgetId][channel] = id;
		return id;
	}
	//---------------------------------------------------------
	bool GuiManager::playWidgetTransition(String const & widgetId, bool entering)
	{
		if(!this->widgetExists(widgetId))
		{
			return false;
		}
		optr<GuiWidget> widget = this->getWidget(widgetId).lock();
		if(!widget)
		{
			return false;
		}
		const UiTransitionSpec spec = widget->getTransition();
		const Ogre::Vector2 size = widget->getSize();
		const UiTransitionPlan plan = planTransition(spec, entering,
			size.x > 0.0f ? size.x : 64.0f, size.y > 0.0f ? size.y : 64.0f);

		// snap (no transition, or no runtime ticks tweens - the editor): jump to
		// the end state. A show ends fully visible/upright; a hide parks hidden.
		if(spec.isNone() || TweenManager::getSingletonPtr() == 0)
		{
			if(entering)
			{
				widget->setGroupAlpha(1.0f);
				widget->setRenderScale(1.0f, 1.0f);
			}
			else
			{
				widget->setGroupAlpha(0.0f);
			}
			return true;
		}

		Ease::Function ease = Ease::byName(plan.ease);
		// a hide parks the widget at effective-invisible once the exit finishes so
		// it stops drawing AND hit-testing (a slide/pop leaves alpha 1 otherwise)
		TweenManager::CompleteFunction park;
		if(!entering)
		{
			const String wid = widgetId;
			park = [wid]()
			{
				GuiManager* gui = GuiManager::getSingletonPtr();
				if(gui && gui->widgetExists(wid))
				{
					optr<GuiWidget> w = gui->getWidget(wid).lock();
					if(w)
					{
						w->setGroupAlpha(0.0f);
					}
				}
			};
		}
		// a non-fade show must start fully opaque (it may have been parked hidden)
		if(entering && !plan.animatesAlpha)
		{
			widget->setGroupAlpha(1.0f);
		}
		bool parkAttached = false;
		if(plan.animatesAlpha)
		{
			widget->setGroupAlpha(plan.alphaFrom);
			float to = plan.alphaTo;
			this->tweenWidget(widgetId, WTC_Alpha, &to, plan.duration, ease, 0.0f,
				park);
			parkAttached = true;
		}
		if(plan.animatesScale)
		{
			widget->setRenderScale(plan.scaleFrom, plan.scaleFrom);
			float to[2] = { plan.scaleTo, plan.scaleTo };
			this->tweenWidget(widgetId, WTC_Scale, to, plan.duration, ease, 0.0f,
				parkAttached ? TweenManager::CompleteFunction() : park);
			parkAttached = true;
		}
		if(plan.animatesOffset)
		{
			// slide: read the rest position, jump to rest+awayOffset, tween back
			float rest[2];
			if(widget->isLayoutEnabled())
			{
				const LayoutVec2 ap = widget->getLayoutNode().anchoredPosition();
				rest[0] = ap.x; rest[1] = ap.y;
			}
			else
			{
				const Ogre::Vector2 p = widget->getPosition();
				rest[0] = p.x; rest[1] = p.y;
			}
			const float startX = rest[0] + plan.offsetFromX;
			const float startY = rest[1] + plan.offsetFromY;
			if(widget->isLayoutEnabled())
			{
				widget->setAnchoredPosition(startX, startY);
			}
			else
			{
				widget->setPosition(startX, startY);
			}
			float to[2] = { rest[0] + plan.offsetToX, rest[1] + plan.offsetToY };
			this->tweenWidget(widgetId, WTC_Position, to, plan.duration, ease,
				0.0f, parkAttached ? TweenManager::CompleteFunction() : park);
			parkAttached = true;
		}
		return true;
	}
	//---------------------------------------------------------
	//--- the screen router: a whole-screen navigation stack layered on top of
	//--- the widget/transition machinery. The pure name stack (ScreenStack) is
	//--- the intent; widget lifecycles follow it one exit transition at a time
	//--- through reconcileScreens. The .oui / builder is the source of truth -
	//--- a screen is destroyed when it leaves the top and rebuilt when revealed,
	//--- so no transient widget state carries across navigation.
	//---------------------------------------------------------
	void GuiManager::registerScreen(String const & name, String const & ouiPath)
	{
		ScreenDef def;
		def.ouiPath = ouiPath;
		this->screenDefs[name] = def;
	}
	//---------------------------------------------------------
	void GuiManager::registerScreenBuilder(String const & name,
		ScreenBuilder const & builder)
	{
		ScreenDef def;
		def.builder = builder;
		this->screenDefs[name] = def;
	}
	//---------------------------------------------------------
	bool GuiManager::hasScreen(String const & name) const
	{
		return this->screenDefs.find(name) != this->screenDefs.end();
	}
	//---------------------------------------------------------
	void GuiManager::pushScreen(String const & name)
	{
		this->screenStack.push(name);
		this->reconcileScreens();	// settle immediately when there is no transition
		this->emitGuiScreenPushed(name);
	}
	//---------------------------------------------------------
	void GuiManager::replaceScreen(String const & name)
	{
		this->screenStack.replace(name);
		this->reconcileScreens();
	}
	//---------------------------------------------------------
	String GuiManager::popScreen()
	{
		const String popped = this->screenStack.pop();
		this->reconcileScreens();
		if (!popped.empty())
		{
			this->emitGuiScreenPopped(popped);
		}
		return popped;
	}
	//---------------------------------------------------------
	void GuiManager::clearScreens()
	{
		this->screenStack.clear();
		this->reconcileScreens();
	}
	//---------------------------------------------------------
	void GuiManager::setScreenBackInterceptor(
		ScreenBackInterceptor const & interceptor)
	{
		this->screenBackInterceptor = interceptor;
	}
	//---------------------------------------------------------
	//--- message-bus mirrors ---------------------------------
	//---------------------------------------------------------
	void GuiManager::emitGuiClicked(String const & widgetId)
	{
		ScriptEventPayload payload;
		payload.setString("id", widgetId);
		ScriptEventBus::getSingleton().emit("gui.clicked", payload);
	}
	//---------------------------------------------------------
	void GuiManager::emitGuiToggled(String const & widgetId, bool state)
	{
		ScriptEventPayload payload;
		payload.setString("id", widgetId);
		payload.setBool("state", state);
		ScriptEventBus::getSingleton().emit("gui.toggled", payload);
	}
	//---------------------------------------------------------
	void GuiManager::emitGuiSubmitted(String const & widgetId, String const & text)
	{
		ScriptEventPayload payload;
		payload.setString("id", widgetId);
		payload.setString("text", text);
		ScriptEventBus::getSingleton().emit("gui.submitted", payload);
	}
	//---------------------------------------------------------
	void GuiManager::emitGuiValueChanged(String const & widgetId, double value)
	{
		ScriptEventPayload payload;
		payload.setString("id", widgetId);
		payload.setNumber("value", value);
		ScriptEventBus::getSingleton().emit("gui.valueChanged", payload);
	}
	//---------------------------------------------------------
	void GuiManager::emitGuiDialogResult(String const & modalId, int result)
	{
		ScriptEventPayload payload;
		payload.setString("id", modalId);
		payload.setNumber("result", static_cast<double>(result));
		ScriptEventBus::getSingleton().emit("gui.dialogResult", payload);
	}
	//---------------------------------------------------------
	void GuiManager::emitGuiScreenPushed(String const & name)
	{
		ScriptEventPayload payload;
		payload.setString("name", name);
		ScriptEventBus::getSingleton().emit("gui.screenPushed", payload);
	}
	//---------------------------------------------------------
	void GuiManager::emitGuiScreenPopped(String const & name)
	{
		ScriptEventPayload payload;
		payload.setString("name", name);
		ScriptEventBus::getSingleton().emit("gui.screenPopped", payload);
	}
	//---------------------------------------------------------
	void GuiManager::emitGuiToastShown(String const & text)
	{
		ScriptEventPayload payload;
		payload.setString("text", text);
		ScriptEventBus::getSingleton().emit("gui.toastShown", payload);
	}
	//---------------------------------------------------------
	bool GuiManager::handleScreenBack()
	{
		if(this->screenStack.empty())
		{
			return false;	// no stack in play - let the app-level back proceed
		}
		// the current screen's own hook wins first (it may show a confirm, pop
		// manually, or veto). A true return means it consumed the back.
		if(this->screenBackInterceptor && this->screenBackInterceptor())
		{
			return true;
		}
		// default: return to the screen beneath, but never pop the root empty -
		// at depth 1 the back is not ours (Android backgrounds the app instead)
		if(this->screenDepth() > 1)
		{
			this->popScreen();
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	void GuiManager::reconcileScreens()
	{
		const String target = this->screenStack.current();
		if(this->screenExiting)
		{
			// navigated straight back onto the screen that is animating out:
			// cancel the exit and play it back in (no rebuild, no id churn)
			if(target == this->materializedScreen)
			{
				this->screenExiting = false;
				foreach(String const & id, this->screenWidgetIds)
				{
					this->playWidgetTransition(id, true);
				}
				return;
			}
			if(this->anyScreenExitTweenActive())
			{
				return;	// still sliding/fading out
			}
			this->finishScreenExit();	// clears materializedScreen; fall through to build
		}
		if(target == this->materializedScreen)
		{
			return;	// widgets already match the stack top
		}
		if(!this->materializedScreen.empty())
		{
			this->beginScreenExit();
			if(!this->materializedScreen.empty())
			{
				return;	// exit is animating; the new screen builds on a later tick
			}
		}
		if(!target.empty())
		{
			this->materializeScreen(target);
		}
	}
	//---------------------------------------------------------
	void GuiManager::materializeScreen(String const & name)
	{
		ScreenDef def;
		std::map<String, ScreenDef>::const_iterator it = this->screenDefs.find(name);
		if(it != this->screenDefs.end())
		{
			def = it->second;
		}
		else if(name.size() > 4 && name.compare(name.size() - 4, 4, ".oui") == 0)
		{
			def.ouiPath = name;	// convenience: an unregistered ".oui" is its own path
		}
		else
		{
			oDebugMsg("gui", 0, "pushScreen: unknown screen '" << name
				<< "' (register it with registerScreen / registerScreenBuilder)");
			return;
		}

		// diff the widget map across the build so we know exactly which widgets
		// this screen owns (and must tear down when it leaves the top)
		std::set<String> before;
		foreach(GuiWidgetMap::value_type const & vt, this->widgets)
		{
			before.insert(vt.first);
		}
		if(def.builder)
		{
			def.builder();
		}
		else if(!def.ouiPath.empty() && this->factory)
		{
			this->factory->loadLayout(def.ouiPath);
		}
		this->screenWidgetIds.clear();
		foreach(GuiWidgetMap::value_type const & vt, this->widgets)
		{
			if(before.find(vt.first) == before.end())
			{
				this->screenWidgetIds.push_back(vt.first);
			}
		}
		this->materializedScreen = name;
		// a fresh screen opts into back handling anew; a stale hook must not linger
		this->screenBackInterceptor = ScreenBackInterceptor();
		foreach(String const & id, this->screenWidgetIds)
		{
			this->playWidgetTransition(id, true);
		}
	}
	//---------------------------------------------------------
	void GuiManager::beginScreenExit()
	{
		foreach(String const & id, this->screenWidgetIds)
		{
			this->playWidgetTransition(id, false);
		}
		// a real transition started tweens - wait for them; a snap (no transition,
		// or no TweenManager in the editor) leaves none active, so tear down now
		if(this->anyScreenExitTweenActive())
		{
			this->screenExiting = true;
		}
		else
		{
			this->finishScreenExit();
		}
	}
	//---------------------------------------------------------
	void GuiManager::finishScreenExit()
	{
		foreach(String const & id, this->screenWidgetIds)
		{
			this->destroyWidget(id);
		}
		this->screenWidgetIds.clear();
		this->materializedScreen = "";
		this->screenExiting = false;
	}
	//---------------------------------------------------------
	bool GuiManager::anyScreenExitTweenActive() const
	{
		TweenManager* manager = TweenManager::getSingletonPtr();
		if(!manager)
		{
			return false;
		}
		foreach(String const & id, this->screenWidgetIds)
		{
			std::map<String, std::map<int, TweenManager::TweenId> >::const_iterator
				it = this->widgetTweens.find(id);
			if(it == this->widgetTweens.end())
			{
				continue;
			}
			for(std::map<int, TweenManager::TweenId>::value_type const & entry :
				it->second)
			{
				if(manager->isTweenActive(entry.second))
				{
					return true;
				}
			}
		}
		return false;
	}
	//---------------------------------------------------------
	String GuiManager::showModal(String const & id, bool lightDismiss)
	{
		String modalId = id;
		if(modalId.empty())
		{
			modalId = "modal#" + Ogre::StringConverter::toString(++this->modalSerial);
		}
		if(this->modalRecords.find(modalId) != this->modalRecords.end())
		{
			return modalId;	// already raised
		}
		// focus transfer: the first modal blurs and remembers the field focused
		// below it, so typed text does not leak past the scrim; the last dismiss
		// restores it (@see drainModalDismissals)
		if(this->modalStack.empty() && this->focusedTextEntry != NULL)
		{
			this->modalSavedFocus = this->focusedTextEntry;
			this->focusTextEntry(NULL);
		}
		ModalStack::Entry entry = this->modalStack.push(modalId);
		ModalRecord record;
		record.layers = entry;
		record.lightDismiss = lightDismiss;
		// the consuming backdrop (semi-transparent black); the dialog's own
		// widgets sit on entry.contentZ, one layer above this scrim
		const String scrimId = modalId + ".scrim";
		optr<GuiModalScrim> scrim = onew(new GuiModalScrim(scrimId, modalId,
			this->defaultAtlas, entry.scrimZ, Color(0.0f, 0.0f, 0.0f, 0.5f),
			lightDismiss));
		this->addWidget(scrim);
		record.widgetIds.push_back(scrimId);
		this->modalRecords[modalId] = record;
		this->reorderViews();
		return modalId;
	}
	//---------------------------------------------------------
	uint GuiManager::getModalContentZ(String const & id) const
	{
		std::map<String, ModalRecord>::const_iterator it =
			this->modalRecords.find(id);
		return it != this->modalRecords.end() ? it->second.layers.contentZ : 0;
	}
	//---------------------------------------------------------
	void GuiManager::registerModalWidget(String const & modalId,
		String const & widgetId)
	{
		std::map<String, ModalRecord>::iterator it =
			this->modalRecords.find(modalId);
		if(it != this->modalRecords.end())
		{
			it->second.widgetIds.push_back(widgetId);
		}
	}
	//---------------------------------------------------------
	bool GuiManager::dismissModal(String const & id)
	{
		if(this->modalRecords.find(id) == this->modalRecords.end())
		{
			return false;
		}
		// deferred: the request may come from inside the input dispatch (a scrim
		// tap), so we tear down at the next frame boundary, never mid-loop
		if(std::find(this->pendingDismiss.begin(), this->pendingDismiss.end(),
			id) == this->pendingDismiss.end())
		{
			this->pendingDismiss.push_back(id);
		}
		return true;
	}
	//---------------------------------------------------------
	void GuiManager::dismissTopModal()
	{
		if(!this->modalStack.empty())
		{
			this->dismissModal(this->modalStack.topId());
		}
	}
	//---------------------------------------------------------
	void GuiManager::removeModalNow(String const & id)
	{
		std::map<String, ModalRecord>::iterator it = this->modalRecords.find(id);
		if(it == this->modalRecords.end())
		{
			return;
		}
		// synchronous mirror of drainModalDismissals' per-id teardown: destroy the
		// scrim + every registered dialog widget, pop the stack entry and drop the
		// record. Also clear any pending deferred dismissal so the drain does not
		// double-process this id.
		for(String const & widgetId : it->second.widgetIds)
		{
			this->destroyWidget(widgetId);
		}
		this->modalStack.remove(id);
		this->modalRecords.erase(it);
		this->pendingDismiss.erase(std::remove(this->pendingDismiss.begin(),
			this->pendingDismiss.end(), id), this->pendingDismiss.end());
		if(this->modalStack.empty() && this->modalSavedFocus != NULL)
		{
			GuiTextEntry* restore = this->modalSavedFocus;
			this->modalSavedFocus = NULL;
			this->focusTextEntry(restore);
		}
		this->reorderViews();
	}
	//---------------------------------------------------------
	void GuiManager::dismissAllModals()
	{
		for(std::map<String, ModalRecord>::value_type const & entry :
			this->modalRecords)
		{
			this->dismissModal(entry.first);
		}
	}
	//---------------------------------------------------------
	void GuiManager::drainModalDismissals()
	{
		if(this->pendingDismiss.empty())
		{
			return;
		}
		std::vector<String> ids;
		ids.swap(this->pendingDismiss);
		for(String const & id : ids)
		{
			std::map<String, ModalRecord>::iterator it =
				this->modalRecords.find(id);
			if(it == this->modalRecords.end())
			{
				continue;
			}
			// destroy the scrim + every registered dialog widget
			for(String const & widgetId : it->second.widgetIds)
			{
				this->destroyWidget(widgetId);
			}
			this->modalStack.remove(id);
			this->modalRecords.erase(it);
		}
		// the last modal closed: restore the field that was focused below it
		if(this->modalStack.empty() && this->modalSavedFocus != NULL)
		{
			GuiTextEntry* restore = this->modalSavedFocus;
			this->modalSavedFocus = NULL;
			this->focusTextEntry(restore);
		}
		this->reorderViews();
	}
	//---------------------------------------------------------
	void GuiManager::pollDialogButtons()
	{
		if(this->modalRecords.empty())
		{
			return;
		}
		// snapshot the ids: resolving a dialog queues a dismissal that mutates
		// modalRecords, so iterate a copy of the keys
		std::vector<String> dialogIds;
		for(std::map<String, ModalRecord>::value_type const & entry :
			this->modalRecords)
		{
			if(entry.second.isDialog)
			{
				dialogIds.push_back(entry.first);
			}
		}
		for(String const & id : dialogIds)
		{
			std::map<String, ModalRecord>::iterator it =
				this->modalRecords.find(id);
			if(it == this->modalRecords.end())
			{
				continue;
			}
			ModalRecord const & record = it->second;
			int result = DR_NONE;
			if(!record.yesButtonId.empty() &&
				this->widgetExists(record.yesButtonId))
			{
				optr<GuiButton> yes = this->getWidgetAs<GuiButton>(
					record.yesButtonId).lock();
				if(yes && yes->wasClicked())
				{
					result = DR_YES;
				}
			}
			if(result == DR_NONE && !record.noButtonId.empty() &&
				this->widgetExists(record.noButtonId))
			{
				optr<GuiButton> no = this->getWidgetAs<GuiButton>(
					record.noButtonId).lock();
				if(no && no->wasClicked())
				{
					result = DR_NO;
				}
			}
			if(result != DR_NONE)
			{
				this->dialogResults[id] = result;
				// bus mirror (additive; getDialogResult polling stays valid)
				this->emitGuiDialogResult(id, result);
				this->dismissModal(id);
			}
		}
	}
	//---------------------------------------------------------
	int GuiManager::getDialogResult(String const & modalId)
	{
		std::map<String, int>::iterator it = this->dialogResults.find(modalId);
		if(it == this->dialogResults.end())
		{
			return DR_NONE;
		}
		const int result = it->second;
		this->dialogResults.erase(it);	// poll-once
		return result;
	}
	//---------------------------------------------------------
	String GuiManager::showConfirm(String const & title, String const & message,
		String const & yesText, String const & noText)
	{
		return this->buildDialogModal(String(), title, message, yesText, noText,
			true);
	}
	//---------------------------------------------------------
	String GuiManager::showAlert(String const & title, String const & message,
		String const & okText)
	{
		return this->buildDialogModal(String(), title, message, okText,
			String(), false);
	}
	//---------------------------------------------------------
	String GuiManager::buildDialogModal(String const & id, String const & title,
		String const & message, String const & yesText, String const & noText,
		bool twoButtons)
	{
		const String modalId = this->showModal(id, false);
		const uint z = this->getModalContentZ(modalId);
		const uint fontIndex = 9;
		const float uiScale = UiGlyph::scale.x >= 1.0f ? UiGlyph::scale.x : 1.0f;
		unsigned int winW = 0, winH = 0;
		this->surfaceSize(winW, winH);

		optr<GuiFactory> f = this->factory;
		const float margin = 20.0f * uiScale;

		// backing panel (nine-sliced), centred
		optr<GuiDecorWidget> panel = f->createDecorWidget(modalId + ".panel",
			"panel", Ogre::Vector2::ZERO, Ogre::Vector2(400.0f, 240.0f),
			this->defaultAtlas, z).lock();
		if(panel)
		{
			panel->setNineSlice(true);
		}
		const Ogre::Vector2 panelSize = panel ? panel->getSize()
			: Ogre::Vector2(400.0f, 240.0f);
		const Ogre::Vector2 panelPos(
			Ogre::Math::Floor((Real(winW) - panelSize.x) * 0.5f),
			Ogre::Math::Floor((Real(winH) - panelSize.y) * 0.5f));
		if(panel)
		{
			panel->setPosition(panelPos.x, panelPos.y);
		}
		this->registerModalWidget(modalId, modalId + ".panel");

		// title + message text
		f->createTextbox(modalId + ".title", fontIndex, resolveDialogText(title),
			Ogre::Vector2(panelPos.x + margin, panelPos.y + margin),
			this->defaultAtlas, z, true);
		this->registerModalWidget(modalId, modalId + ".title");
		f->createTextbox(modalId + ".message", fontIndex,
			resolveDialogText(message),
			Ogre::Vector2(panelPos.x + margin, panelPos.y + margin * 3.0f),
			this->defaultAtlas, z, true);
		this->registerModalWidget(modalId, modalId + ".message");

		// buttons along the bottom of the panel
		optr<GuiButton> yes = f->createButton(modalId + ".yes", "button",
			fontIndex, resolveDialogText(yesText), Ogre::Vector2::ZERO,
			GuiLabel::LA_CENTER, Ogre::Vector2(140.0f, 44.0f), this->defaultAtlas,
			z, false, GuiButtonBlink::BBLINK_NONE).lock();
		const Ogre::Vector2 btnSize = yes ? yes->getSize()
			: Ogre::Vector2(140.0f, 44.0f);
		const float btnY = panelPos.y + panelSize.y - btnSize.y - margin;
		if(twoButtons)
		{
			optr<GuiButton> no = f->createButton(modalId + ".no", "button",
				fontIndex, resolveDialogText(noText), Ogre::Vector2::ZERO,
				GuiLabel::LA_CENTER, Ogre::Vector2(140.0f, 44.0f),
				this->defaultAtlas, z, false, GuiButtonBlink::BBLINK_NONE).lock();
			if(no)
			{
				no->setPosition(panelPos.x + margin, btnY);
			}
			if(yes)
			{
				yes->setPosition(panelPos.x + panelSize.x - btnSize.x - margin,
					btnY);
			}
			this->registerModalWidget(modalId, modalId + ".no");
		}
		else if(yes)
		{
			yes->setPosition(
				panelPos.x + (panelSize.x - btnSize.x) * 0.5f, btnY);
		}
		this->registerModalWidget(modalId, modalId + ".yes");

		ModalRecord & record = this->modalRecords[modalId];
		record.isDialog = true;
		record.yesButtonId = modalId + ".yes";
		record.noButtonId = twoButtons ? (modalId + ".no") : String();
		this->reorderViews();
		return modalId;
	}
	//---------------------------------------------------------
	woptr<GuiToggleGroup> GuiManager::createToggleGroup(String const & id)
	{
		std::map<String, optr<GuiToggleGroup> >::iterator it =
			this->toggleGroups.find(id);
		if(it != this->toggleGroups.end())
		{
			return it->second;
		}
		optr<GuiToggleGroup> group = onew(new GuiToggleGroup(id));
		this->toggleGroups[id] = group;
		return group;
	}
	//---------------------------------------------------------
	woptr<GuiToggleGroup> GuiManager::getToggleGroup(String const & id)
	{
		std::map<String, optr<GuiToggleGroup> >::iterator it =
			this->toggleGroups.find(id);
		if(it != this->toggleGroups.end())
		{
			return it->second;
		}
		return woptr<GuiToggleGroup>();
	}
	//---------------------------------------------------------
	void GuiManager::destroyToggleGroup(String const & id)
	{
		this->toggleGroups.erase(id);
	}
	//---------------------------------------------------------
	void GuiManager::showToast(String const & text, float seconds)
	{
		const String resolved = resolveDialogText(text);
		this->toastQueue.enqueue(resolved, seconds);
		this->emitGuiToastShown(resolved);
	}
	//---------------------------------------------------------
	void GuiManager::updateToast(float delta)
	{
		this->toastQueue.update(delta);
		if(!this->toastQueue.hasActive())
		{
			if(this->toast)
			{
				this->toast->getLayer()->setVisible(false);
			}
			return;
		}
		const float uiScale = UiGlyph::scale.x >= 1.0f ? UiGlyph::scale.x : 1.0f;
		unsigned int winW = 0, winH = 0;
		this->surfaceSize(winW, winH);
		const Ogre::Vector2 size(300.0f * uiScale, 56.0f * uiScale);
		unsigned int safeBottom = this->surfaceSafeArea().mBottom;
		const float x = Ogre::Math::Floor((Real(winW) - size.x) * 0.5f);
		const float y = Ogre::Math::Floor(Real(winH) - size.y - 40.0f * uiScale
			- Real(safeBottom));
		if(!this->toast)
		{
			// lazily create the single toast widget on a high layer, above the
			// HUD but below any modal (modals start at z 1000)
			this->toast = onew(new GuiToast("gui.toast", "panel", 9,
				this->toastQueue.activeText(),
				Ogre::Vector2(x, y), size, this->defaultAtlas, 950));
			this->reorderViews();
		}
		this->toast->getLayer()->setVisible(true);
		this->toast->setText(this->toastQueue.activeText());
		this->toast->setPosition(x, y);
		this->toast->setSize(size.x, size.y);
		this->toast->setToastAlpha(this->toastQueue.activeAlpha());
	}
	//---------------------------------------------------------
	bool GuiManager::isToastVisible() const
	{
		return this->toast && this->toast->getLayer() != NULL &&
			this->toast->getLayer()->isVisible();
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
		// the set of widget ids that belong to an active modal (scrim + dialog);
		// used to flag them so an agent can assert "a dialog is up"
		std::set<String> modalWidgetIds;
		for(std::map<String, ModalRecord>::value_type const & rec :
			this->modalRecords)
		{
			for(String const & wid : rec.second.widgetIds)
			{
				modalWidgetIds.insert(wid);
			}
		}
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
			layout.enabled = widget->isEffectivelyEnabled();
			layout.modal = modalWidgetIds.count(entry.first) != 0;
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
		this->surfaceSize(windowWidth, windowHeight);
		return this->layoutPolicy.referenceScale(Real(windowWidth),
			Real(windowHeight));
	}
	//---------------------------------------------------------
	float GuiManager::getUiScale() const
	{
		return UiGlyph::scale.x;
	}
	//---------------------------------------------------------
	void GuiManager::surfaceSize(unsigned int & width, unsigned int & height) const
	{
		if(this->previewActive)
		{
			width = this->previewWidth;
			height = this->previewHeight;
			return;
		}
		width = 0;
		height = 0;
		RenderSystem::get()->getWindowSize(width, height);
	}
	//---------------------------------------------------------
	SafeAreaInsets GuiManager::surfaceSafeArea() const
	{
		if(this->previewActive)
		{
			return this->previewSafeArea;
		}
		if(Engine::getSingletonPtr())
		{
			return Engine::getSingleton().getSafeAreaInsets();
		}
		return SafeAreaInsets();
	}
	//---------------------------------------------------------
	void GuiManager::markLayoutDirty()
	{
		this->layoutDirty = true;
	}
	//---------------------------------------------------------
	void GuiManager::resolveLayouts()
	{
		// re-resolve only when a layout property changed or the surface resized
		unsigned int windowWidth = 0, windowHeight = 0;
		this->surfaceSize(windowWidth, windowHeight);
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
		if(this->previewActive || Engine::getSingletonPtr())
		{
			const SafeAreaInsets insets = this->surfaceSafeArea();
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
		this->tickFrame(*data);
		return false;
	}
	//---------------------------------------------------------
	void GuiManager::profileTick(float delta)
	{
		// run the exact per-frame gui pipeline once, off the event bus, so a
		// selfcheck can TIME it (layout resolve + widget ticks + screen rebuild +
		// submission) - the perf-budget probe
		FrameEventData data;
		data.timeSinceLastEvent = delta;
		data.timeSinceLastFrame = delta;
		this->tickFrame(data);
	}
	//---------------------------------------------------------
	size_t GuiManager::getLastBatchCount() const
	{
		// draw submissions across every visible screen (one atlas = one screen)
		size_t batches = 0;
		for(GuiViewMap::value_type const & vt : this->views)
		{
			UiScreen* screen = vt.second->getScreen();
			if(screen && screen->isVisible())
			{
				batches += screen->getLastBatchCount();
			}
		}
		return batches;
	}
	//---------------------------------------------------------
	size_t GuiManager::getRebuildCount() const
	{
		size_t rebuilds = 0;
		for(GuiViewMap::value_type const & vt : this->views)
		{
			if(vt.second->getScreen())
			{
				rebuilds += vt.second->getScreen()->getRebuildCount();
			}
		}
		return rebuilds;
	}
	//---------------------------------------------------------
	size_t GuiManager::getGeometryRebuildCount() const
	{
		size_t rebuilds = 0;
		for(GuiViewMap::value_type const & vt : this->views)
		{
			if(vt.second->getScreen())
			{
				rebuilds += vt.second->getScreen()->getGeometryRebuildCount();
			}
		}
		return rebuilds;
	}
	//---------------------------------------------------------
	size_t GuiManager::getScratchCapacity() const
	{
		size_t capacity = 0;
		for(GuiViewMap::value_type const & vt : this->views)
		{
			if(vt.second->getScreen())
			{
				capacity += vt.second->getScreen()->getScratchCapacity();
			}
		}
		return capacity;
	}
	//---------------------------------------------------------
	void GuiManager::tickFrame(FrameEventData & frameData)
	{
		FrameEventData* const data = &frameData;
		// frame-boundary work, all OUTSIDE any input-dispatch loop so widgets may
		// be created/destroyed safely. Run queued actions first (popups opening,
		// so their widgets exist for this frame's loop + resolve), then let every
		// widget tick - a dropdown reads its option clicks in its tick, so the
		// modal teardown must come AFTER the loop (else a light-dismiss scrim
		// tears the list down before the pick is read).
		if(!this->deferredActions.empty())
		{
			std::vector<std::function<void()> > actions;
			actions.swap(this->deferredActions);
			for(std::function<void()> const & action : actions)
			{
				action();
			}
		}
		this->pollDialogButtons();
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
		{
			widget->onFrameStarted(*data);
		}
		// tear down any modal asked to close this frame, then advance the toast
		this->drainModalDismissals();
		this->updateToast(data->timeSinceLastFrame);
		// drive the screen router: tear the old screen down once its exit
		// transition tweens finish, then build the next screen
		this->reconcileScreens();
		// resolve the opt-in rect-anchor layout tree to absolute pixels BEFORE
		// the screens rebuild (each resolved widget's setPosition/setSize
		// dirties its screen, which rebuilds just below). O(n) over the layout
		// widgets, and only when a layout property changed or the window
		// resized - steady clean frames skip it entirely.
		this->resolveLayouts();
		// re-apply cascaded group alpha when a fade changed it (a parent fade dims
		// its subtree); a no-op on a steady UI
		this->resolveGroupAlpha();
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
	}
	//---------------------------------------------------------
	bool GuiManager::onFrameRenderingQueued(Orkige::Event const & event)
	{
		this->updateStats();
		return false;
	}
	//---------------------------------------------------------
	bool GuiManager::onKeyPressed(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
		// Escape (desktop) and the Android back button are the same "go back"
		// gesture. Priority: a live modal first, then the screen stack.
		const bool isBack = (data->key == KeyEventData::KC_ESCAPE ||
			data->key == KeyEventData::KC_WEBBACK);
		// a modal up: dismiss the TOP one, but only when it is light-dismissable
		// (a menu/dropdown); a confirm/alert must be answered by a button. Consumes
		// the key either way while a modal is up so nothing below reacts.
		if(isBack && this->isModalActive())
		{
			std::map<String, ModalRecord>::const_iterator top =
				this->modalRecords.find(this->modalStack.topId());
			if(top != this->modalRecords.end() && top->second.lightDismiss)
			{
				this->dismissTopModal();
			}
			return false;
		}
		// no modal: let the screen router pop the stack (or a screen's back hook
		// veto). When it consumes the back, do not dispatch it to the widgets.
		if(isBack && this->handleScreenBack())
		{
			return false;
		}
		foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
		{
			if(this->cancelInputUpdate)
			{
				break;
			}
			// input-inert widgets are skipped: a disabled widget OR a faded-out
			// (effectively invisible) subtree neither reacts nor consumes
			if(!widget->acceptsInput())
			{
				continue;
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
			// input-inert widgets are skipped: a disabled widget OR a faded-out
			// (effectively invisible) subtree neither reacts nor consumes
			if(!widget->acceptsInput())
			{
				continue;
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
		// a field destroyed while remembered for post-modal restore must not be
		// refocused (dangling pointer)
		if(this->modalSavedFocus == entry)
		{
			this->modalSavedFocus = NULL;
		}
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
			// input-inert widgets are skipped: a disabled widget OR a faded-out
			// (effectively invisible) subtree neither reacts nor consumes
			if(!widget->acceptsInput())
			{
				continue;
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
			// input-inert widgets are skipped: a disabled widget OR a faded-out
			// (effectively invisible) subtree neither reacts nor consumes
			if(!widget->acceptsInput())
			{
				continue;
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
			// input-inert widgets are skipped: a disabled widget OR a faded-out
			// (effectively invisible) subtree neither reacts nor consumes
			if(!widget->acceptsInput())
			{
				continue;
			}
			widget->onCursorMoved(cursorPos);
		}
		// the mouse wheel arrives as a moved event carrying a z delta; route it
		// to scroll viewports (a no-op on every other widget)
		if(data->relZ != 0)
		{
			foreach(optr<GuiWidget> const & widget, this->sortedWidgets)
			{
				if(!widget->acceptsInput())
				{
					continue;
				}
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
			// input-inert widgets are skipped: a disabled widget OR a faded-out
			// (effectively invisible) subtree neither reacts nor consumes
			if(!widget->acceptsInput())
			{
				continue;
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
			// input-inert widgets are skipped: a disabled widget OR a faded-out
			// (effectively invisible) subtree neither reacts nor consumes
			if(!widget->acceptsInput())
			{
				continue;
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
			// input-inert widgets are skipped: a disabled widget OR a faded-out
			// (effectively invisible) subtree neither reacts nor consumes
			if(!widget->acceptsInput())
			{
				continue;
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
		// singleton-aware: constructing while the previous scene's instance
		// is still alive raises a catchable Lua error (never an engine abort)
		OSINGLETON_CONSTRUCTOR3(optr<GuiFactory>, String const &, String const &)
		OSINGLETON()
		OFUNCWEAK(getFactory)
		OFUNC(enableInputEvents)
		OFUNC(disableInputEvents)
		OFUNC(widgetExists)
		// the .oui <-> Lua bridge: find a declaratively-authored widget by id and
		// wire behavior (setters, guitween.*, transitions) onto it
		OFUNCWEAK(findWidget)
		// typed companions: acquisition-time TYPE FILTERS - each returns a
		// WidgetHandle (or nil on an absent / wrong-type id) so a script grabs a
		// widget by kind. Once held, the WidgetHandle exposes the whole widget
		// surface gated by the live type (findWidget hands the SAME handle), so
		// findLabel(id):setText works and findButton(id):setText errors distinctly.
		OFUNCWEAK(findLabel)
		OFUNCWEAK(findButton)
		OFUNCWEAK(findCheckBox)
		OFUNCWEAK(findSlider)
		OFUNCWEAK(findSelectMenu)
		OFUNCWEAK(findProgressBar)
		OFUNCWEAK(findDecor)
		OFUNCWEAK(findTextEntry)
		OFUNCWEAK(findScrollView)
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
		// the engine's real device ui scale (what getContentScale rounds to);
		// scripts sizing UI by hand want this, but measuring getSize() is better
		OFUNC(getUiScale)
		// modal dialogs: showConfirm(title,message,yes,no) /
		// showAlert(title,message,ok) build a dialog and return its modal id;
		// poll getDialogResult(id) (1=yes/ok, 2=no, 0=pending). showModal(id,
		// lightDismiss) raises a bare consuming scrim for a custom modal.
		OFUNC(showModal)
		OFUNC(getModalContentZ)
		OFUNC(registerModalWidget)
		OFUNC(dismissModal)
		OFUNC(dismissTopModal)
		OFUNC(dismissAllModals)
		OFUNC(isModalActive)
		OFUNC(getTopModalId)
		OFUNC(getModalCount)
		OFUNC(showConfirm)
		OFUNC(showAlert)
		OFUNC(getDialogResult)
		OFUNC(cancelCurrentInputUpdate)
		// single-selection groups: createToggleGroup(id):addMember(checkbox);
		// poll group:getSelected() / group:pollChanged()
		OFUNCWEAK(createToggleGroup)
		OFUNCWEAK(getToggleGroup)
		OFUNC(destroyToggleGroup)
		// timed notification: showToast(text, seconds); poll isToastVisible()
		OFUNC(showToast)
		OFUNC(isToastVisible)
		// performance-contract probes (the enforceable "1 draw/atlas, dirty-
		// tracked, no steady-state alloc" promise; read deltas across frames)
		OFUNC(getLastBatchCount)
		OFUNC(getRebuildCount)
		OFUNC(getGeometryRebuildCount)
		OFUNC(getScratchCapacity)
	OOBJECT_END
}