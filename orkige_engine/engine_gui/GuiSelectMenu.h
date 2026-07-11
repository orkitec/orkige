/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   GuiSelectMenu.h
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#ifndef __GUISELECTMENU__h__3_11_2010__19_56_54__
#define __GUISELECTMENU__h__3_11_2010__19_56_54__

#include "engine_gui/GuiDecorWidget.h"
#include <OgreStringVector.h>
#include "engine_gui/GuiLabel.h"
#include "engine_gui/GuiButton.h"

namespace Orkige
{
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

    protected:
		void updatePosition();
		void updateSize();
	private:
    };
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