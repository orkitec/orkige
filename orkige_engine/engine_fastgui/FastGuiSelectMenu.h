/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   FastGuiSelectMenu.h
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#ifndef __FASTGUISELECTMENU__h__3_11_2010__19_56_54__
#define __FASTGUISELECTMENU__h__3_11_2010__19_56_54__

#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiLabel.h"
#include "engine_fastgui/FastGuiButton.h"

namespace Orkige
{
    class FastGuiSelectMenu : public FastGuiWidget
    {
		OOBJECT(FastGuiSelectMenu, FastGuiWidget);
        //-Types--------------------------------------------
    public:
    protected:
    private:
        //-Variables----------------------------------------

		Ogre::StringVector items;		//!< item names
		std::size_t selectedIndex;

    public:
    protected:
		optr<FastGuiLabel> label;					//!< current SelectMenu Title text
		optr<FastGuiDecorWidget> decor;				//!< back decore for the selectMenu
		optr<FastGuiDecorWidget> leftArrow;			//!< back decore for the selectMenu
		optr<FastGuiDecorWidget> rightArrow;		//!< back decore for the selectMenu
		optr<FastGuiButton> buttonMainSelection;	//!< the selected item field
    private:
        //-Methods------------------------------------------
    public:
        FastGuiSelectMenu(String const & id,String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~FastGuiSelectMenu();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);

		//! set all item names
		void setItems(const Ogre::StringVector& items);
		void showItem();
		
		//! selection
		unsigned int getSelectedItemIndex();
		String getSelectedItem();
		void selectItemIndex(unsigned int index);
		void selectItem(String item);

		//! get text holding ui element
		inline woptr<FastGuiLabel> getLabel();
		//! get image ui element
		inline woptr<FastGuiDecorWidget> getDecor();
		//! get title text
		String getCaption();
		//! set title text
		void setCaption(String const & text);

    protected:
    private:
		void updatePosition();
		void updateSize();
    };
	//---------------------------------------------------------------
	inline woptr<FastGuiLabel> FastGuiSelectMenu::getLabel()
	{
		return this->label;
	}
	//---------------------------------------------------------------
	inline woptr<FastGuiDecorWidget> FastGuiSelectMenu::getDecor()
	{
		return this->decor;
	}
	//---------------------------------------------------------------
	inline unsigned int FastGuiSelectMenu::getSelectedItemIndex()
	{
		return this->selectedIndex;
	}

}
#endif //__FASTGUISELECTMENU__h__3_11_2010__19_56_54__ 