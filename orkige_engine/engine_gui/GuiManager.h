/********************************************************************
created:	Wednesday 2010/08/04 at 15:10
filename: 	GuiManager.h
author:		steffen.roemer
notice:		This source file is part of orkige (orkitec Game engine)
For the latest info, see http://www.orkitec.com/
copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __GuiManager_h__4_8_2010__15_10_29__
#define __GuiManager_h__4_8_2010__15_10_29__

#include <core_event/EventHandler.h>
#include <core_util/Singleton.h>
#include "engine_gui/GuiFactory.h"

namespace Orkige
{
	/** \defgroup Gui Gui
	* @{
	* Orkige 3D Gui Modules */

	//! Tray Based Gui Management. 
	//! Main class to manage a cursor, backdrop, trays and widgets.
	class GuiManager : public Ogre::ResourceGroupListener, public Orkige::EventHandler, public Singleton<GuiManager>, public Interface
	{
		OOBJECT(GuiManager, Interface);
		DECL_OSINGLETON(GuiManager);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::String name;												//!< name of this tray system
		Ogre::Overlay* backgroundLayer;									//!< backdrop layer
		Ogre::Overlay* traysLayer;										//!< widget layer
		Ogre::Overlay* priorityLayer;									//!< top priority layer
		Ogre::Overlay* cursorLayer;										//!< cursor layer
		Ogre::OverlayContainer* backdrop;								//!< backdrop
		Ogre::OverlayContainer* trays[Widget::TL_COUNT];				//!< widget trays
		WidgetCollection widgets[Widget::TL_COUNT];						//!< widgets
		WidgetCollection widgetsToDelete;								//!< widget queue for deletion
		Ogre::OverlayContainer* cursor;									//!< cursor
		Ogre::Real widgetPadding[Widget::TL_COUNT];						//!< widget padding
		Ogre::Real widgetSpacing;										//!< widget spacing
		Ogre::Real trayPadding;											//!< tray padding
		bool trayDrag;													//!< a mouse press was initiated on a tray
		SelectMenu* expandedMenu;										//!< top priority expanded menu widget
		Dialog* dialog;													//!< top priority dialog											
		Ogre::OverlayContainer* dialogShade;							//!< top priority dialog shade
		bool cursorWasVisible;											//!< cursor state before showing dialog
		Label* fpsLabel;												//!< FPS label
		ParamsPanel* statsPanel;										//!< frame stats panel
		ProgressBar* loadingBar;										//!< loading bar
		Ogre::Real groupInitProportion;									//!< proportion of load job assigned to initialising one resource group
		Ogre::Real groupLoadProportion;									//!< proportion of load job assigned to loading one resource group
		Ogre::Real loadingIncrement;									//!< loading increment
		Ogre::GuiHorizontalAlignment trayWidgetAlign[Widget::TL_COUNT];	//!< tray widget alignments
		GuiFactory*	guiFactory;											//!< the used Guifactory
		String materialGroup;											//!< material group for interanl ressources
	private:
		//--- Methods -----------------------------------------------
	public:
		//! Creates backdrop, cursor, and trays.
		GuiManager(String const & guiName, String const & materialGroup, GuiFactory* factory);

		//! Destroys background, cursor, widgets, and trays.
		virtual ~GuiManager();

		//! get assigned Guifactory for Widget Creation
		GuiFactory*	getFactory();

		//! enable key and mouse events
		void enableInputEvents();
		//! disable key and mouse events
		void disableInputEvents();

		//! get name of this tray system
		String const & getName();

		//these methods get the underlying overlays and overlay elements
		//! get container for given TrayLocation
		Ogre::OverlayContainer* getTrayContainer(Widget::TrayLocation trayLoc);
		//! get Layer for background image
		Ogre::Overlay* getBackgroundLayer();
		//! get layer for Trays
		Ogre::Overlay* getTraysLayer();
		//! get layer for cursor
		Ogre::Overlay* getCursorLayer();
		//! get overlay cointainer for background image
		Ogre::OverlayContainer* getBackgroundContainer();
		//! get overlay cointainer for cursor
		Ogre::OverlayContainer* getCursorContainer();
		//! get cursor overlay element
		Ogre::OverlayElement* getCursorImage();
		//! get overlay cointainer for dialog shade
		Ogre::OverlayContainer* getDialogShade();

		//! show all backdrop, trays, and cursor
		void showAll();
		//! hide all backdrop, trays, and cursor
		void hideAll();

		//! Displays specified material on backdrop, or the last material used if none specified. Good for pause menus like in the browser.
		void showBackground(String const & materialName = Ogre::StringUtil::BLANK);
		//! hide background image
		void hideBackground();
		//! is background visible?
		bool isBackgroundVisible();

		//! Displays specified material on cursor, or the last material used if none specified. Used to change cursor type.
		void showCursor(String const & materialName = Ogre::StringUtil::BLANK);
		//! hide cursor
		void hideCursor();
		//! is cursor visible?
		bool isCursorVisible();

		//! Updates cursor position based on unbuffered mouse state. This is necessary because if the gui manager has been cut off 
		//! from mouse events for a time, the cursor position will be out of date.
		void refreshCursor();

		//! set traylayer visible
		void showTrays();
		//! hide traylayer
		void hideTrays();
		//! is traylayer visible?
		bool areTraysVisible();

		//! Sets horizontal alignment of a tray's contents.
		void setTrayWidgetAlignment(Widget::TrayLocation trayLoc, Ogre::GuiHorizontalAlignment gha);

		//! set Widget padding for given TrayLocation
		void setWidgetPadding(Ogre::Real padding, Widget::TrayLocation tl);
		//! set Widget spacing for all TrayLocation's
		void setWidgetSpacing(Ogre::Real spacing);
		//! set padding for all trays
		void setTrayPadding(Ogre::Real padding);
		//! get Widget padding for given TrayLocation
		virtual Ogre::Real getWidgetPadding(Widget::TrayLocation tl) const;
		//! get Widget spacing
		virtual Ogre::Real getWidgetSpacing() const;
		//! get Tray padding
		virtual Ogre::Real getTrayPadding() const;


		//! Fits trays to their contents and snaps them to their anchor locations.
		virtual void adjustTrays();

		//! Returns a 3D ray into the scene that is directly underneath the cursor.
		Ogre::Ray getCursorRay(Ogre::Camera* cam);

		//! Shows frame statistics widget set in the specified location.
		void showFrameStats(Widget::TrayLocation trayLoc, int place = -1);
		//! Hides frame statistics widget set.
		void hideFrameStats();
		//! are frame statistics visible?
		bool areFrameStatsVisible();
		//! Toggles visibility of advanced statistics.
		void toggleAdvancedFrameStats();

		
		//! @brief	Shows loading bar. Also takes job settings: the number of resource groups
		//!			to be initialized, the number of resource groups to be loaded, and the
		//!			proportion of the job that will be taken up by initialization. Usually,
		//!			script parsing takes up most time, so the default value is 0.7.
		void showLoadingBar(unsigned int numGroupsInit = 1, unsigned int numGroupsLoad = 1, Ogre::Real initProportion = 0.7);
		//! set text displayed in the LoadingBar
		void setLoadingBarText(String const & text);
		//! hide the LoadingBar
		void hideLoadingBar();
		//! is LoadingBar currently shown?
		bool isLoadingBarVisible();

		//! pops up dialog with given type if type is know to factory
		void showDialog(Dialog::DialogType type, String const & name, const Ogre::DisplayString& caption, const Ogre::DisplayString& message, Ogre::DisplayString const & button1Text, Ogre::DisplayString const & button2Text);
		//! Pops up a message dialog with an OK button.
		void showOkDialog(const Ogre::DisplayString& caption, const Ogre::DisplayString& message, Ogre::DisplayString const & okButtonText = "OK");
		//! Pops up a question dialog with Yes and No buttons.
		void showYesNoDialog(const Ogre::DisplayString& caption, const Ogre::DisplayString& question, Ogre::DisplayString const & yesButtonText, Ogre::DisplayString const & noButtonText);
		//! Hides whatever dialog is currently showing.
		void closeDialog();
		//! Determines if any dialog is currently visible.
		bool isDialogVisible();

		//! Gets a widget from a tray by place.
		Widget* getWidget(Widget::TrayLocation trayLoc, unsigned int place);
		//! Gets a widget from a tray by name.
		Widget* getWidget(Widget::TrayLocation trayLoc, String const & name);
		//! Gets a widget by name.
		Widget* getWidget(String const & name);

		//! Gets the number of widgets in total.
		unsigned int getNumWidgets();
		//! Gets the number of widgets in a tray.
		unsigned int getNumWidgets(Widget::TrayLocation trayLoc);

		//! Gets all the widgets of a specific tray.
		WidgetIterator getWidgetIterator(Widget::TrayLocation trayLoc);

		//! Gets a widget's position in its tray.
		int locateWidgetInTray(Widget* widget);

		//! Destroys given widget
		void destroyWidget(Widget* widget);
		//! destroy widget in given TrayLocation at given place
		void destroyWidget(Widget::TrayLocation trayLoc, unsigned int place);
		//! destroy widget in given TrayLocation with given name
		void destroyWidget(Widget::TrayLocation trayLoc, String const & name);
		//! destroy widget with given name
		void destroyWidget(String const & name);

		//! Destroys all widgets in a tray.
		void destroyAllWidgetsInTray(Widget::TrayLocation trayLoc);

		//! Destroys all widgets.
		void destroyAllWidgets();

		//! add widget to a specified tray and given place
		void moveWidgetToTray(Widget* widget, Widget::TrayLocation trayLoc, int place = -1);
		//! add named widget to a specified tray and given place
		void moveWidgetToTray(String const & name, Widget::TrayLocation trayLoc, unsigned int place = -1);
		//! move widget from given tray to a specified tray and given place
		void moveWidgetToTray(Widget::TrayLocation currentTrayLoc, String const & name, Widget::TrayLocation targetTrayLoc, int place = -1);
		//! move widget from given tray and given place to a specified tray and given place
		void moveWidgetToTray(Widget::TrayLocation currentTrayLoc, unsigned int currentPlace, Widget::TrayLocation targetTrayLoc, int targetPlace = -1);

		//! Removes widget from its tray. Same as moving it to the null tray.
		void removeWidgetFromTray(Widget* widget);
		//! Removes named widget from its tray. Same as moving it to the null tray.
		void removeWidgetFromTray(String const & name);
		//! Removes named widget from given tray. Same as moving it to the null tray.
		void removeWidgetFromTray(Widget::TrayLocation trayLoc, String const & name);
		//! Removes widget at place from given tray. Same as moving it to the null tray.
		void removeWidgetFromTray(Widget::TrayLocation trayLoc, int place);

		//! Removes all widgets from a widget tray.
		void clearTray(Widget::TrayLocation trayLoc);

		//! Removes all widgets from all widget trays.
		void clearAllTrays();

	protected:
		//! Toggles visibility of advanced statistics.
		bool onLabelHit(Orkige::Event const & event);

		//! Process frame events. Updates frame statistics widget set and deletes all widgets queued for destruction.
		bool onFrameRenderingQueued(Orkige::Event const & event);

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

		//! Internal method to prioritise / deprioritise expanded menus.
		void setExpandedMenu(SelectMenu* m);

		//! overload @see Ogre::ResourceGroupListener::resourceGroupScriptingStarted
		virtual void resourceGroupScriptingStarted(String const & groupName, size_t scriptCount);
		//! overload @see Ogre::ResourceGroupListener::scriptParseStarted
		virtual void scriptParseStarted(String const & scriptName, bool& skipThisScript);
		//! overload @see Ogre::ResourceGroupListener::scriptParseEnded
		virtual void scriptParseEnded(String const & scriptName, bool skipped);
		//! overload @see Ogre::ResourceGroupListener::resourceGroupScriptingEnded
		virtual void resourceGroupScriptingEnded(String const & groupName);
		//! overload @see Ogre::ResourceGroupListener::resourceGroupLoadStarted
		virtual void resourceGroupLoadStarted(String const & groupName, size_t resourceCount);
		//! overload @see Ogre::ResourceGroupListener::resourceLoadStarted
		virtual void resourceLoadStarted(const Ogre::ResourcePtr& resource);
		//! overload @see Ogre::ResourceGroupListener::resourceLoadEnded
		virtual void resourceLoadEnded();
		//! overload @see Ogre::ResourceGroupListener::worldGeometryStageStarted
		virtual void worldGeometryStageStarted(String const & description);
		//! overload @see Ogre::ResourceGroupListener::worldGeometryStageEnded
		virtual void worldGeometryStageEnded();
		//! overload @see Ogre::ResourceGroupListener::resourceGroupLoadEnded
		virtual void resourceGroupLoadEnded(String const & groupName);
	private:
	};
	/** @} End of "defgroup Gui".*/

	//---------------------------------------------------------------
}

#endif //__GuiManager_h__4_8_2010__15_10_29__