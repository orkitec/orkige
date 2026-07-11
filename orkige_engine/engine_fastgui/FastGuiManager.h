/********************************************************************
	created:	Tuesday 2010/10/26 at 18:24
	filename: 	FastGuiManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __FastGuiManager_h__26_10_2010__18_24_45__
#define __FastGuiManager_h__26_10_2010__18_24_45__

#include "engine_fastgui/UiRenderer.h"
#include <core_module/OrkigePrerequisites.h>
#include "engine_fastgui/FastGuiFactory.h"
#include <core_event/EventHandler.h>
#include "engine_fastgui/FastGuiView.h"

#include <OgreResourceGroupManager.h>	// the resource-group default arguments

namespace Orkige
{
	class FastGuiTextEntry;
	class FontAtlas;

	class ORKIGE_ENGINE_DLL FastGuiManager : public Singleton<FastGuiManager>, public Interface, public EventHandler
	{
		OOBJECT(FastGuiManager, Interface);
		DECL_OSINGLETON(FastGuiManager);
		//--- Types -------------------------------------------------
	public:
		typedef std::map<String, optr<FastGuiView> > FastGuiViewMap;
		typedef std::list<optr<FastGuiView> > FastGuiViewList;
		typedef std::map<String, optr<FastGuiWidget> > FastGuiWidgetMap;
		typedef std::list<optr<FastGuiWidget> > FastGuiWidgetList;
		//! @brief flattened on-screen layout of one widget for remote
		//! inspection (the MSG_UI_LAYOUT debug readback): id + pixel rect +
		//! effective visibility (its layer's visible flag)
		struct WidgetLayout
		{
			String	id;
			float	left = 0.0f;
			float	top = 0.0f;
			float	width = 0.0f;
			float	height = 0.0f;
			bool	visible = true;
		};
		//! @brief which rect a parentless layout widget resolves against
		enum RootSpace
		{
			RS_FullWindow = 0,	//!< the whole window (0,0,W,H)
			RS_SafeArea			//!< the window minus the notch/home-bar insets
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		//! loaded atlases by name (the views' screens reference them)
		std::map<String, optr<UiAtlas> > atlases;
		//! runtime-baked (TTF/SVG) atlases by name: they OWN the UiAtlas the
		//! screen references, so they must outlive their view; flushed once a
		//! frame so lazily-baked glyphs reach the GPU
		std::map<String, optr<FontAtlas> > fontAtlases;
		optr<FastGuiFactory> factory;
		FastGuiViewMap views;
		FastGuiWidgetMap widgets;
		FastGuiWidgetList sortedWidgets;
		optr<FastGuiDecorWidget> cursor;
		optr<FastGuiTextbox> stats;
		optr<FastGuiTextbox> statsValues;
		unsigned short statsMarkupColorIndex;
		String defaultAtlas;
		bool cancelInputUpdate;
        bool scaleStats;
		//! the single focused text-entry field, or NULL (@see focusTextEntry)
		FastGuiTextEntry* focusedTextEntry;
		//! set true while a cursor press claimed a text field (tap-away blur)
		bool textEntryFocusClaimed;
		//! @brief the rect-anchor layout state (@see the resolve pass in
		//! onFrameStarted). The default policy leaves layoutScale at 1, so a
		//! game that never opts in is unscaled and unaffected.
		LayoutScalePolicy layoutPolicy;
		RootSpace layoutRootSpace;
		//! a layout property changed since the last resolve (or the window
		//! resized): re-run the resolve pass on the next frame, else skip it
		bool layoutDirty;
		unsigned int lastLayoutWidth, lastLayoutHeight;
		//--- Methods -----------------------------------------------
	public:
		FastGuiManager(optr<FastGuiFactory> _factory, String const & defaultAtlas = "fastgui_default", String const & group = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		virtual ~FastGuiManager();
		//! enable key and mouse events
		void enableInputEvents();
		//! disable key and mouse events
		void disableInputEvents();
		//! Displays Cursor
		void showCursor(String const & atlas, String const & sprite);
		//! hide cursor
		void hideCursor();
		//! is cursor visible?
		bool isCursorVisible();
		//! Updates cursor position based on unbuffered mouse state. This is necessary because if the gui manager has been cut off 
		//! from mouse events for a time, the cursor position will be out of date.
		void refreshCursor();
		//! get widget creation factory
		inline woptr<FastGuiFactory> getFactory();
		//! create screen with given atlas asserts if there is already a screen with that atlas
		woptr<FastGuiView> createView(String const & atlas, String const & group = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		//! free resources from given view
		void destroyView(String const & atlas);
		//! free resources from given view and all widgets using its view/atlas
		void destroyViewWithWidgets(String const & atlas);
		//! free all views
		void destroyAllViews(bool keepDefaultAtlas = true);
		//! get screen with given atlas or NULL
		inline woptr<FastGuiView> getView(String const & atlas);
		//! get o create view with given atlas
		inline woptr<FastGuiView> getCreateView(String const & atlas, String const & group = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		//! checks if screen with given id exists
		inline bool hasView(String const & atlas);
		//! hides all views
		void hideAllViews();
		//! inhieds all views
		void showAllViews();
		//! add given widget
		bool addWidget(optr<FastGuiWidget> widget);
		//! destroy given widget
		bool destroyWidget(String const & id);
		//! destroy given widget
		void destroyAllWidgets();
		//! check if widget with given id already exists
		inline bool widgetExists(String const & id);
		//! get widget with given id
		inline woptr<FastGuiWidget> getWidget(String const & id);
		//! get widget of given type with given id
		template<typename WidgetType>
		inline woptr<WidgetType> getWidgetAs(String const & id);
		//! get default texture atlas
		inline String const & getDefaultAtlas();
		//! show frame stats
		void showStats(uint glyphIndex = 9, Ogre::Vector2 const & pos = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, unsigned short markupColorIndex = 0, bool scaleStats = false);
		//! clear worst and best fps and more
		void resetStats();
		//! update statistic
		void updateStats();
		//! hide frame stats
		void hideStats();
		//! reorder the view rendering by their z value
		void reorderViews();

		//! exchanges the texture of an atlas
		void replaceAtlasTexture(String const & atlas, String const & texture);

		//! cancel current input dispatch
		void cancelCurrentInputUpdate();

		//! @brief give a text-entry field input focus (NULL clears focus): blurs
		//! the previously focused field and opens/closes the InputManager
		//! text-input session so exactly ONE field is focused at a time.
		void focusTextEntry(FastGuiTextEntry* entry);
		//! the focused text-entry field, or NULL
		FastGuiTextEntry* getFocusedTextEntry() const { return this->focusedTextEntry; }
		//! @brief a focused field being destroyed clears the focus + input session
		//! (the widget calls this from its destructor)
		void notifyTextEntryDestroyed(FastGuiTextEntry* entry);

		//--- rect-anchor layout (opt-in; @see FastGuiWidget) ---
		//! @brief set the design resolution + match factor the layout scale is
		//! derived from (match 0 = match width, 1 = match height). Zero design
		//! extents disable reference scaling (layout stays 1:1).
		void setDesignResolution(float designWidth, float designHeight,
			float matchWidthHeight = 0.0f);
		//! @brief how the two axis ratios combine (0 match, 1 shrink/shortest-
		//! side, 2 expand/longest-side; @see LayoutScalePolicy::MatchMode)
		void setLayoutMatchMode(int mode);
		//! @brief which rect parentless layout widgets resolve against
		//! ("FullWindow" or "SafeArea"); case-insensitive
		void setRootSpace(String const & space);
		//! @brief the current layout scale (design units -> window pixels) for
		//! the live window size (1 when no design resolution is set)
		float getLayoutScale() const;
		//! @brief mark the layout tree dirty so the next frame re-resolves it
		//! (widgets call this from their layout setters)
		void markLayoutDirty();

		//! returns true if given point is over any widget
		bool isPointOverWidget(Ogre::Vector2 const & point);
		//! @brief snapshot every widget's id, pixel rect and visibility - the
		//! source of the MSG_UI_LAYOUT readback an agent asserts "HUD inside the
		//! safe box" against. Order follows the widget map (stable by id).
		std::vector<WidgetLayout> getWidgetLayouts();
	protected:
		//! Process frame events. Updates frame statistics widget set and deletes all widgets queued for destruction.
		bool onFrameStarted(Orkige::Event const & event);
		//! Process frame events. Updates frame statistics widget set and deletes all widgets queued for destruction.
		bool onFrameRenderingQueued(Orkige::Event const & event);
		//! after changing a game state (with its possible loading times) reset the worst and best fps
		bool onGameStateChanged(Orkige::Event const & event);

		//! @brief resolve every opted-in layout widget to absolute pixels and
		//! push the result into it. Top-down, memoised per frame; parentless
		//! widgets resolve against the (full-window or safe-area) root rect.
		void resolveLayouts();
		//! resolve one widget's rect (recursing to its parent first, memoised)
		LayoutRect resolveWidget(FastGuiWidget* widget, float layoutScale,
			LayoutRect const & fullRoot, LayoutRect const & safeRoot,
			std::map<FastGuiWidget*, LayoutRect> & cache);

		//! process committed text input (routed to the focused text-entry field)
		bool onTextInput(Orkige::Event const & event);
		//! process key pressed events
		bool onKeyPressed(Orkige::Event const & event);
		//! process key released events
		bool onKeyReleased(Orkige::Event const & event);

		//! Processes mouse button down events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onMousePressed(Orkige::Event const & event);
		//! Processes mouse button up events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onMouseReleased(Orkige::Event const & event);
		//! Updates cursor position. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onMouseMoved(Orkige::Event const & event);

		//! Processes touch down events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onTouchPressed(Orkige::Event const & event);
		//! Processes touch up events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onTouchReleased(Orkige::Event const & event);
		//! Processes touch move events. Returns true if the event was consumed and should not be passed on to other handlers.
		bool onTouchMoved(Orkige::Event const & event);

	private:
	};
	//---------------------------------------------------------------
	inline woptr<FastGuiFactory> FastGuiManager::getFactory()
	{
		return this->factory;
	}
	//---------------------------------------------------------------
	inline woptr<FastGuiView> FastGuiManager::getView(String const & atlas)
	{
		FastGuiViewMap::iterator it = this->views.find(atlas);
		if(it != this->views.end())
		{
			return it->second;
		}
		return oNULL(FastGuiView);
	}
	//---------------------------------------------------------------
	inline bool FastGuiManager::hasView(String const & atlas)
	{
		bool screenExists = this->getView(atlas).lock() != NULL;
		return screenExists;
	}
	//---------------------------------------------------------------
	inline woptr<FastGuiView> FastGuiManager::getCreateView(String const & atlas, String const & group)
	{
		if(atlas == StringUtil::BLANK)
		{
			return this->getView(this->defaultAtlas);
		}

		if(this->hasView(atlas))
		{
			return this->getView(atlas);
		}

		return this->createView(atlas, group);
	}
	//---------------------------------------------------------------
	inline bool FastGuiManager::widgetExists(String const & id)
	{
		if(this->widgets.find(id) != this->widgets.end())
		{
			return true;
		}
		return false;
	}
	//---------------------------------------------------------------
	inline woptr<FastGuiWidget> FastGuiManager::getWidget(String const & id)
	{
		FastGuiWidgetMap::iterator it = this->widgets.find(id);
		oAssertDesc(it != this->widgets.end(), "Could not find Widget: " << id << "!");
		return it->second;
	}
	//---------------------------------------------------------------
	template<typename WidgetType>
	inline woptr<WidgetType> FastGuiManager::getWidgetAs(String const & id)
	{
		woptr<FastGuiWidget> weak_widget = this->getWidget(id);
		optr<FastGuiWidget> widget = weak_widget.lock();
		oAssert(widget);
		oAssertDesc(widget->getTypeInfo() == WidgetType::getClassTypeInfo(), "Widget: " << id << " is of type: " << widget->getTypeInfo().getName() << " and not of type: " << WidgetType::getClassTypeInfo().getName() << " !");
		optr<WidgetType> casted_widget = std::static_pointer_cast<WidgetType>(widget);
		oAssert(casted_widget);
		return casted_widget;
	}
	//---------------------------------------------------------------
	inline String const & FastGuiManager::getDefaultAtlas()
	{
		return this->defaultAtlas;
	}
}

#endif //__FastGuiManager_h__26_10_2010__18_24_45__