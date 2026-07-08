/********************************************************************
created:    Tuesday 2010/11/02 at 17:50
filename:   FastGuiCheckBox.h
author:     hicham.allaoui  
notice:		This source file is part of orkige (orkitec Game engine)
			For the latest info, see http://www.orkitec.com/
copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __FASTGUICHECKBOX__h__2_11_2010__17_50_02__
#define __FASTGUICHECKBOX__h__2_11_2010__17_50_02__

#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiLabel.h"

namespace Orkige
{
    class ORKIGE_ENGINE_DLL FastGuiCheckBox : public FastGuiWidget
    {
		OOBJECT(FastGuiCheckBox, FastGuiWidget);
        //-Types--------------------------------------------
    public:
		//! @brief triggered when CheckBox is toggled
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(CheckBoxToggledEvent);
    protected:
    private:
        //-Variables----------------------------------------
    public:
    protected:
		optr<FastGuiLabel> label;			//!< current CheckBox text
		optr<FastGuiDecorWidget> checkbox;	//!< current CheckBox symbol
		optr<FastGuiDecorWidget> decor;		//!< current CheckBox image
		bool checked;						//!< current CheckBox state
		String baseSpriteName;				//!< base name of the CheckBox state sprite;
    private:
        //-Methods------------------------------------------
    public:
		FastGuiCheckBox(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z, bool useCheckbox);
        virtual ~FastGuiCheckBox();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);

		//! is box currently checked?
		inline bool isChecked();
		//! set box checked and trigger CheckBox::CheckBoxToggledEvent if notifyListener = true
		void setChecked(bool checked, bool notifyListener = true);
		//! toggle state and trigger CheckBox::CheckBoxToggledEvent if notifyListener = true
		void toggle(bool notifyListener = true);

		//! get text holding ui element
		inline woptr<FastGuiLabel> getLabel();
		//! get image ui element
		inline woptr<FastGuiDecorWidget> getDecor();
		//! get button text
		String getCaption();
		//! set button text
		void setCaption(String const & text);
    protected:
    private:
    };
    //----------------------------------------------------
	inline woptr<FastGuiLabel> FastGuiCheckBox::getLabel()
	{
		return this->label;
	}
	//---------------------------------------------------------------
	inline woptr<FastGuiDecorWidget> FastGuiCheckBox::getDecor()
	{
		return this->decor;
	}
	//----------------------------------------------------
	bool FastGuiCheckBox::isChecked()
	{
		return this->checked;
	}
}
#endif //__FASTGUICHECKBOX__h__2_11_2010__17_50_02__ 