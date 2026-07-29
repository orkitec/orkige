/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   GuiSelectMenu.h
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#ifndef __GUISELECTMENU__h__3_11_2010__19_56_54__
#define __GUISELECTMENU__h__3_11_2010__19_56_54__

#include "engine_gui/GuiDecorWidget.h"
#include <OgreStringVector.h>
#include "engine_gui/GuiLabel.h"
#include "engine_gui/GuiButton.h"

namespace Orkige
{
	//! @brief a stepped value control: one row carrying a TITLE on its leading
	//! side and, on the trailing side, a previous arrow, the current value and a
	//! next arrow. Every part is placed inside the row's own rect (@see
	//! arrangeParts), so the widget looks exactly like the rectangle the layout
	//! resolver hands it. GuiSlider is the same row with a draggable grip.
    class ORKIGE_ENGINE_DLL GuiSelectMenu : public GuiWidget
    {
		OOBJECT(GuiSelectMenu, GuiWidget);
        //-Types--------------------------------------------
    public:
		//! @brief triggered when selection is changed
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(SelectMenuEvent);
	protected:
    private:
        //-Variables----------------------------------------
    public:
    protected:
		Ogre::StringVector items;					//!< item names
		std::size_t selectedIndex;

		optr<GuiLabel> label;					//!< current SelectMenu Title text
		optr<GuiDecorWidget> decor;				//!< back decor for the selectMenu
		optr<GuiDecorWidget> leftArrow;			//!< back decor for the selectMenu
		optr<GuiDecorWidget> rightArrow;		//!< back decor for the selectMenu
		optr<GuiButton> buttonMainSelection;	//!< the selected item field
    private:
        //-Methods------------------------------------------
    public:
        GuiSelectMenu(String const & id,String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~GuiSelectMenu();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);

		//! set all item names
		virtual void setItems(const Ogre::StringVector& items);
		//! @brief set the item names from a single pipe-delimited string
		//! ("A | B | C", spaces trimmed) - the SCRIPT-friendly setter, since the
		//! seam does not convert a Lua table to a string vector
		void setItemsString(String const & pipeDelimited);
		inline Ogre::StringVector& getItems();
		virtual void showItem();
		
		//! selection
		std::size_t getSelectedItemIndex();
		String getSelectedItem();
		void selectItemIndex(std::size_t index, bool throwEvent=true);
		void selectItem(String item);

		//! get text holding ui element
		inline woptr<GuiLabel> getLabel();
		//! get image ui element
		inline woptr<GuiDecorWidget> getDecor();
		//! get title text
		String getCaption();
		//! set title text
		void setCaption(String const & text);
		virtual void applyRenderTransform(Ui2DTransform const & transform);
		virtual void applyRenderAlpha(float alphaMultiplier);

    protected:
		//! @brief re-place every part INSIDE the field's current rect: the title
		//! on the leading side, then the previous arrow, the value field and the
		//! next arrow on the trailing side, all vertically centred. Every part is
		//! derived from the field rect ABSOLUTELY (never from its own last
		//! position), so a relayout can be run any number of times and the widget
		//! never drifts - and it never draws outside the rect the layout resolver
		//! gave it. @return the value field's rect (a slider's pin track).
		Ogre::Vector4 arrangeParts();
		//! dim the frame, arrows, value field and label when disabled
		virtual void onEnabledChanged(bool enable);
	private:
    };
	//! the value field's caption while the menu holds no items - also its INITIAL
	//! caption, because a button built with an empty one owns no label at all
	static char const * const EMPTY_VALUE_CAPTION = "Empty!";
	//---------------------------------------------------------------
	inline woptr<GuiLabel> GuiSelectMenu::getLabel()
	{
		return this->label;
	}
	//---------------------------------------------------------------
	inline woptr<GuiDecorWidget> GuiSelectMenu::getDecor()
	{
		return this->decor;
	}
	//---------------------------------------------------------------
	inline std::size_t GuiSelectMenu::getSelectedItemIndex()
	{
		return this->selectedIndex;
	}
	//---------------------------------------------------------------
	inline Ogre::StringVector& GuiSelectMenu::getItems() 
	{ 
		return this->items; 
	}


}
#endif //__GUISELECTMENU__h__3_11_2010__19_56_54__ 