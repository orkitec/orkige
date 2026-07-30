/********************************************************************
created:    Tuesday 2010/11/02 at 17:50
filename:   GuiCheckBox.h
author:     hicham.allaoui  
notice:		This source file is part of orkige (orkitec Game engine)
			For the latest info, see http://www.orkitec.com/
copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GUICHECKBOX__h__2_11_2010__17_50_02__
#define __GUICHECKBOX__h__2_11_2010__17_50_02__

#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiLabel.h"

namespace Orkige
{
	class GuiToggleGroup;

	//! @brief a two-state toggle in two SKINS, chosen by the `useCheckbox`
	//! constructor flag (the `.oui` `checkbox` key):
	//!
	//! - BOX skin (useCheckbox = true): a plate sprite carries the row, a
	//!   natural-size square check SYMBOL sits at its trailing edge and the
	//!   caption gets the remaining width, left-aligned and vertically centred -
	//!   the settings-row checkbox. The symbol keeps its own size at any widget
	//!   size (it is a glyph, not a fill), so a wide row never stretches it.
	//! - PLATE skin (useCheckbox = false): the sprite's `_off`/`_on` pair IS the
	//!   whole body and the caption is centred in it by the requested alignment -
	//!   the two-state button (what a tab in a GuiTabBar looks like). Pair a
	//!   stretched plate with setNineSlice(true) to keep its corners crisp.
	//!
	//! Either way the caption is laid out BESIDE or CENTRED IN the art, never
	//! jammed into a corner of it, and both follow the widget's resolved rect.
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
		//! the check symbol's own (density-scaled) square side in pixels - the
		//! BOX skin places it at that size whatever the widget's size is
		Ogre::Real symbolSide;
		//! the caption alignment the author asked for (applied verbatim by the
		//! PLATE skin; the BOX skin owns its caption column and aligns left)
		GuiLabel::LabelAlignment textAlignment;
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
		//! @brief the size this toggle wants: never smaller than its CAPTION
		//! (plus the margins and, in the BOX skin, the symbol column), so a
		//! `fit = preferred` tab / checkbox in a layout group is wide enough to
		//! read. Falls back to the authored size when that is already bigger.
		virtual Ogre::Vector2 getPreferredSize();

		//! draw the plate nine-sliced (crisp corners on a stretched skin) -
		//! the GuiButton::setNineSlice sibling, same `.oui` `nineSlice` key
		void setNineSlice(bool enable);
		//! draw the plate tiled instead of stretched (`.oui` `tiled`)
		void setTiled(bool enable);

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
		virtual bool hasTextStyle() const { return true; }
    protected:
		//! forward the widget's text style to its caption
		virtual void onTextStyleChanged();
		//! dim the box + glyph + label when disabled (no dedicated sprite)
		virtual void onEnabledChanged(bool enable);
    private:
		//! @brief re-place the check symbol + caption against the plate's
		//! current rect (the ONE geometry rule both setPosition and setSize run,
		//! so the parts can never drift apart)
		void arrangeParts();
		//! the symbol's side for the current plate height (never upscaled past
		//! its own size, shrunk only when the row is shorter than it)
		Ogre::Real currentSymbolSide() const;
		//! the density-scaled inner margin the parts sit in
		static Ogre::Real margin();
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