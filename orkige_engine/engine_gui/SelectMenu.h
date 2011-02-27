/********************************************************************
	created:	Wednesday 2010/08/04 at 15:08
	filename: 	SelectMenu.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __SelectMenu_h__4_8_2010__15_08_40__
#define __SelectMenu_h__4_8_2010__15_08_40__

#include "engine_gui/Widget.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! dropdown
	class SelectMenu : public Widget
	{
		OOBJECT(SelectMenu, Widget);
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a item is selected
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(SelectMenuItemSelectedEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::BorderPanelOverlayElement* smallBox;							//!< box when menu is contracted
		Ogre::BorderPanelOverlayElement* expandedBox;						//!< box when menu is expanded
		Ogre::TextAreaOverlayElement* textArea;								//!< menu caption
		Ogre::TextAreaOverlayElement* smallTextArea;						//!< text when menu is contracted
		Ogre::BorderPanelOverlayElement* scrollTrack;						//!< scroll bar
		Ogre::PanelOverlayElement* scrollHandle;							//!< scroll element
		std::vector<Ogre::BorderPanelOverlayElement*> itemOverlayElements;	//!< menu item elements
		unsigned int maxItemsShown;											//!< max items that are shown
		unsigned int itemsShown;											//!< current number of shown items
		bool cursorOver;													//!< is cursor over?
		bool expanded;														//!< is menu expanded?
		bool fitToContents;													//!< fit menu size to content
		bool dragging;														//!< currently dragging slider?
		Ogre::StringVector items;											//!< item names
		int selectionIndex;													//!< index of selected item
		int highlightIndex;													//!< index for highlight
		int displayIndex;													//!< index for current display item
		Ogre::Real dragOffset;												//!< offset of dragged slider
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create SelectMenu
		//! @copydoc Widget
		SelectMenu(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real boxWidth, unsigned int maxItemsShown);
		//! is menu expanded?
		bool isExpanded();
		//! get menu caption
		const Ogre::DisplayString& getCaption();
		//! set menu caption
		void setCaption(const Ogre::DisplayString& caption);
		//! get all item names
		const Ogre::StringVector& getItems();
		//! get number of items
		unsigned int getNumItems();
		//! set all item names
		void setItems(const Ogre::StringVector& items);
		//! add a single item
		void addItem(const Ogre::DisplayString& item);
		//! remove item with given name
		void removeItem(const Ogre::DisplayString& item);
		//! remove item at index
		void removeItem(unsigned int index);
		//! remove all items
		void clearItems();
		//! select item at index
		void selectItem(unsigned int index, bool notifyListener = true);
		//! select item with given name
		void selectItem(const Ogre::DisplayString& item, bool notifyListener = true);
		//! get name of selection
		Ogre::DisplayString getSelectedItem();
		//! get index of selection
		int getSelectionIndex();

		// widget overloads
		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);
		virtual void onFocusLost();
	protected:
		//! Internal method - sets which item goes at the top of the expanded menu.
		void setDisplayIndex(unsigned int index);

		//! Internal method - cleans up an expanded menu.
		void retract();
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__SelectMenu_h__4_8_2010__15_08_40__