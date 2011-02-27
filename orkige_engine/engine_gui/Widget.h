/********************************************************************
	created:	Wednesday 2010/08/04 at 15:09
	filename: 	Widget.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __Widget_h__4_8_2010__15_09_26__
#define __Widget_h__4_8_2010__15_09_26__

#include "engine_util/OverlayUtil.h"
#include "engine_gui/IGuiObject.h"
#include <core_event/Event.h>

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Abstract base class for all Widgets.
	class Widget : public IGuiObject
	{
		friend class GuiManager;
		OOBJECT(Widget, IGuiObject);
		//--- Types -------------------------------------------------
	public:
		//! enumerator values for widget tray anchoring locations
		enum TrayLocation   
		{
			TL_TOPLEFT = 0,
			TL_TOP,
			TL_TOPRIGHT,
			TL_LEFT,
			TL_CENTER,
			TL_RIGHT,
			TL_BOTTOMLEFT,
			TL_BOTTOM,
			TL_BOTTOMRIGHT,
			TL_NONE,
			TL_COUNT
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::OverlayElement* overlayElement;	//!< Widget base Overlay element
		TrayLocation trayLocation;				//!< Widget tray location
		String materialGroup;					//!< widget material group
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor
		virtual ~Widget();
		//! cleanup widget
		void cleanup();
		//! @see Widget::overlayElement
		Ogre::OverlayElement* getOverlayElement();
		//! get name of the overlayElement
		String const & getName();
		//! @see Widget::trayLocation
		TrayLocation getTrayLocation();
		//! hide the Widget
		void hide();
		//! show the Widget
		void show();
		//! is widget visible?
		bool isVisible();
		
		//! overridable method to do stuff when widget looses Focus
		virtual void onFocusLost();
	protected:
		//! Do not instantiate any widgets directly. Use GuiFactory.
		Widget(String const & name, String const & materialGroup);

		//! internal method used by GuiManager. do not call directly.
		void _assignToTray(TrayLocation trayLoc);
	private:
	};
	//---------------------------------------------------------------
	typedef std::vector<Widget*> WidgetCollection;					//!< collection of Widgets
	//---------------------------------------------------------------
	typedef Ogre::VectorIterator<WidgetCollection> WidgetIterator;	//!< iterator for WidgetCollections
	//---------------------------------------------------------------
	/** @} End of "addtogroup Gui"*/
}

#endif //__Widget_h__4_8_2010__15_09_26__