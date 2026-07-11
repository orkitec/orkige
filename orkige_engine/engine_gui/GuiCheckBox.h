/********************************************************************
created:    Tuesday 2010/11/02 at 17:50
filename:   GuiCheckBox.h
author:     hicham.allaoui  
notice:		This source file is part of orkige (orkitec Game engine)
			For the latest info, see http://www.orkitec.com/
copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __GUICHECKBOX__h__2_11_2010__17_50_02__
#define __GUICHECKBOX__h__2_11_2010__17_50_02__

#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiLabel.h"

namespace Orkige
{
	class GuiToggleGroup;

    class ORKIGE_ENGINE_DLL GuiCheckBox : public GuiWidget
    {
		OOBJECT(GuiCheckBox, GuiWidget);
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
		optr<GuiLabel> label;			//!< current CheckBox text
		optr<GuiDecorWidget> checkbox;	//!< current CheckBox symbol
		optr<GuiDecorWidget> decor;		//!< current CheckBox image
		bool checked;						//!< current CheckBox state
		String baseSpriteName;				//!< base name of the CheckBox state sprite;
		//! when part of a toggle group, a tap routes there (single-selection)
		//! instead of a plain local toggle. Not owned - the group outlives it.
		GuiToggleGroup* toggleGroup;
    private:
        //-Methods------------------------------------------
    public:
		GuiCheckBox(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z, bool useCheckbox);
        virtual ~GuiCheckBox();

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

		//! @brief attach/detach the single-selection group a tap routes through
		//! (NULL detaches). Set by GuiToggleGroup::addMember.
		void setToggleGroup(GuiToggleGroup* group) { this->toggleGroup = group; }

		//! get text holding ui element
		inline woptr<GuiLabel> getLabel();
		//! get image ui element
		inline woptr<GuiDecorWidget> getDecor();
		//! get button text
		String getCaption();
		//! set button text
		void setCaption(String const & text);
		virtual void applyRenderTransform(Ui2DTransform const & transform);
		virtual void applyRenderAlpha(float alphaMultiplier);
    protected:
		//! dim the box + glyph + label when disabled (no dedicated sprite)
		virtual void onEnabledChanged(bool enable);
    private:
    };
    //----------------------------------------------------
	inline woptr<GuiLabel> GuiCheckBox::getLabel()
	{
		return this->label;
	}
	//---------------------------------------------------------------
	inline woptr<GuiDecorWidget> GuiCheckBox::getDecor()
	{
		return this->decor;
	}
	//----------------------------------------------------
	bool GuiCheckBox::isChecked()
	{
		return this->checked;
	}
}
#endif //__GUICHECKBOX__h__2_11_2010__17_50_02__ 