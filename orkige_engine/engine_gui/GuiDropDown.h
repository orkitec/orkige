/********************************************************************
	created:	Saturday 2026/07/11 at 19:15
	filename: 	GuiDropDown.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiDropDown_h__11_7_2026__19_15_00__
#define __GuiDropDown_h__11_7_2026__19_15_00__

#include "engine_gui/GuiButton.h"
#include <OgreStringVector.h>

namespace Orkige
{
	//! @brief a button that, when tapped, opens a scrollable list of its options
	//! on a light-dismiss modal layer (tap outside to close). Picking an option
	//! sets the value, updates the button caption and closes the list. Same value
	//! API as GuiSelectMenu (getSelectedIndex / setItems) but a dropped list
	//! rather than a cycler - use it when the option set is long. The list reuses
	//! the modal scrim (P1) for outside-tap dismiss and a GuiScrollView for the
	//! options. All list widgets are created/destroyed at the frame boundary via
	//! GuiManager::runDeferred, never inside the input dispatch.
	class ORKIGE_ENGINE_DLL GuiDropDown : public GuiButton
	{
		OOBJECT(GuiDropDown, GuiButton);
		//--- Variables ---------------------------------------------
	protected:
		Ogre::StringVector	items;			//!< the option labels
		std::size_t			selectedIndex;	//!< current selection (0 when empty)
		uint				fontIndex;		//!< option-label font
		bool				menuOpen;		//!< the list is up
		bool				wantOpen;		//!< a tap asked to open (deferred)
		String				menuModalId;	//!< the open list's modal id
		std::vector<String>	optionIds;		//!< the open list's option button ids
		//--- Methods -----------------------------------------------
	public:
		GuiDropDown(String const & id, String const & spriteName,
			uint defaultGlyphIndex, String const & text,
			Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment,
			Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~GuiDropDown();

		//! set the option labels (resets the selection to 0)
		void setItems(Ogre::StringVector const & items);
		//! @brief set the option labels from a pipe-delimited string ("A | B | C")
		//! - the SCRIPT-friendly setter (the seam cannot pass a Lua table as a
		//! vector). @see GuiSelectMenu::setItemsString
		void setItemsString(String const & pipeDelimited);
		Ogre::StringVector const & getItems() const { return this->items; }
		//! the selected option index
		std::size_t getSelectedIndex() const { return this->selectedIndex; }
		//! the selected option label, or "" when empty
		String getSelectedItem() const;
		//! @brief select an option by index (updates the button caption); a
		//! programmatic set, no list needed
		void selectIndex(std::size_t index);
		//! is the option list currently open?
		bool isMenuOpen() const { return this->menuOpen; }

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual bool onFrameStarted(FrameEventData const & data);
	protected:
		//! build the option list on a light-dismiss modal (frame-boundary safe)
		void openMenu();
		//! request the option list be torn down (deferred)
		void closeMenu();
		//! poll the open options; a pick sets the value and closes the list
		void pollOptions();
	private:
	};
}

#endif //__GuiDropDown_h__11_7_2026__19_15_00__
