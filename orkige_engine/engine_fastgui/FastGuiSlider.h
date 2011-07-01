/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   FastGuiSlider.h
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#ifndef __FastGuiSlider__h__3_11_2010__19_56_54__
#define __FastGuiSlider__h__3_11_2010__19_56_54__

#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiSelectMenu.h"

namespace Orkige
{
    class FastGuiSlider : public FastGuiSelectMenu
    {
		OOBJECT(FastGuiSlider, FastGuiSelectMenu);
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
		optr<FastGuiDecorWidget> pin;			//!< decor for the grip element
		optr<FastGuiDecorWidget> pin_area;		//!< decor for the grip element
    private:
        //-Methods------------------------------------------
    public:
        FastGuiSlider(String const & id, String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~FastGuiSlider();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);

		virtual void showItem();
		virtual void setItems(const Ogre::StringVector& items);

    protected:
    private:
		bool pinActive;
		std::vector<Ogre::Vector2> itemsPinSnap;
    };

}
#endif //__FastGuiSlider__h__3_11_2010__19_56_54__ 