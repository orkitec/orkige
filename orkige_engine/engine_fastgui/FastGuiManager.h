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

#include "engine_fastgui/Gorilla.h"
#include <core_module/OrkigePrerequisites.h>
#include "engine_fastgui/FastGuiFactory.h"
#include "engine_input/InputManager.h"
#include <core_event/EventHandler.h>
#include "engine_fastgui/FastGuiView.h"

namespace Orkige
{
	class FastGuiManager : public Singleton<FastGuiManager>, public Interface, public EventHandler
	{
		OOBJECT(FastGuiManager, Interface);
		DECL_OSINGLETON(FastGuiManager);
		//--- Types -------------------------------------------------
	public:
		typedef std::map<String, optr<FastGuiView> > FastGuiViewMap;
		typedef std::list<optr<FastGuiView> > FastGuiViewList;
		typedef std::map<String, optr<FastGuiWidget> > FastGuiWidgetMap;
		typedef std::list<optr<FastGuiWidget> > FastGuiWidgetList;
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		optr<Gorilla::Silverback> silverback;
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
		void showStats(uint glyphIndex = 9, Ogre::Vector2 const & pos = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, unsigned short markupColorIndex = 0);
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

		//! returns true if given point is over any widget
		bool isPointOverWidget(Ogre::Vector2 const & point);
	protected:
		//! Process frame events. Updates frame statistics widget set and deletes all widgets queued for destruction.
		bool onFrameStarted(Orkige::Event const & event);
		//! Process frame events. Updates frame statistics widget set and deletes all widgets queued for destruction.
		bool onFrameRenderingQueued(Orkige::Event const & event);
		//! after changing a game state (with its possible loading times) reset the worst and best fps
		bool onGameStateChanged(Orkige::Event const & event);

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
		optr<WidgetType> casted_widget = boost::static_pointer_cast<WidgetType>(widget);
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