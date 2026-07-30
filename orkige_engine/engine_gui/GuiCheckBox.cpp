/********************************************************************
created:    Tuesday 2010/11/02 at 17:50
filename:   GuiCheckBox.cpp
author:     hicham.allaoui  
notice:		This source file is part of orkige (orkitec Game engine)
			For the latest info, see http://www.orkitec.com/
copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiCheckBox.h"
#include "engine_gui/GuiManager.h"
#include "engine_gui/GuiToggleGroup.h"
#include "engine_gui/UiAtlas.h"
#include <core_event/GlobalEventManager.h>
#include <algorithm>

#define GUICHECKBOX_MARGING 10.f

namespace Orkige
{
	namespace
	{
		//! the shared check SYMBOL sprite pair: the BOX skin draws this glyph on
		//! top of whatever plate sprite the author picked, so the two states are
		//! named here once instead of derived from the plate's name
		char const * const CHECK_SYMBOL_OFF = "checkbox_off";
		char const * const CHECK_SYMBOL_ON = "checkbox_on";
	}
	IMPL_OWNED_EVENTTYPE(GuiCheckBox, CheckBoxToggledEvent);
    //----------------------------------------------------
    //- public: ------------------------------------------
    //----------------------------------------------------
    GuiCheckBox::GuiCheckBox(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z, bool useCheckbox) : 
		GuiWidget(id, atlas, z),
		checked(false),
		symbolSide(0.f),
		textAlignment(textAlignment),
		baseSpriteName(spriteName),
		toggleGroup(NULL)
    {
		this->markInteractive();	// a checkbox consumes press input

		if (useCheckbox)
		{
			// BOX skin: the author's sprite is the row plate, the shared check
			// symbol draws on top of it. Built at ZERO size so it reports its
			// OWN sprite size, which arrangeParts keeps (density-scaled) - a
			// wide row must never stretch a glyph.
			this->decor = onew(new GuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
			this->checkbox = onew(new GuiDecorWidget(id + ".symbol", CHECK_SYMBOL_OFF, position, Ogre::Vector2::ZERO, atlas, z+2));
			this->symbolSide = std::max(this->checkbox->getSize().x,
				this->checkbox->getSize().y) * UiGlyph::scale.x;
		}
		else
		{
			// PLATE skin: the sprite's own "_off"/"_on" pair IS the body
			this->decor = onew(new GuiDecorWidget(id + ".decor", spriteName + "_off", position, size, atlas, z));
		}

		this->label = onew(new GuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z, true));
		// the BOX skin owns a caption COLUMN beside the symbol (left-aligned,
		// vertically centred); the PLATE skin is a two-state button and honours
		// the alignment the author asked for (centred by default)
		this->label->setAlignment(this->checkbox != NULL
			? GuiLabel::LA_LEFT : this->textAlignment);
		this->initFontIndex(defaultGlyphIndex);
		this->arrangeParts();
    }
    //----------------------------------------------------
    GuiCheckBox::~GuiCheckBox()
    {
	}   
	//----------------------------------------------------
	void GuiCheckBox::setPosition( Ogre::Real left, Ogre::Real top )
	{
		// move the backing plate, then re-place the symbol + caption against it
		// (the layout resolver / a scroll viewport drive this each relayout)
		this->decor->setPosition(left, top);
		this->arrangeParts();
	}
	//----------------------------------------------------
	void GuiCheckBox::setSize( Ogre::Real width, Ogre::Real height )
	{
		this->decor->setSize(width, height);
		this->arrangeParts();
	}
	//----------------------------------------------------
	Ogre::Real GuiCheckBox::margin()
	{
		// the inner margin is a DEVICE length like every authored size: it grows
		// with the display density so the parts keep their physical spacing
		return GUICHECKBOX_MARGING * (UiGlyph::scale.x >= 1.f
			? UiGlyph::scale.x : 1.f);
	}
	//----------------------------------------------------
	Ogre::Real GuiCheckBox::currentSymbolSide() const
	{
		if (this->checkbox == NULL)
		{
			return 0.f;
		}
		// the symbol keeps its own size; only a row SHORTER than the symbol
		// shrinks it (so it never spills out of the plate)
		return std::max(1.f, std::min(this->symbolSide,
			this->decor->getSize().y));
	}
	//----------------------------------------------------
	void GuiCheckBox::arrangeParts()
	{
		const Ogre::Vector2 origin = this->decor->getPosition();
		const Ogre::Vector2 size = this->decor->getSize();
		const Ogre::Real inset = margin();
		if (this->checkbox != NULL)
		{
			// BOX skin: symbol at the trailing edge, vertically centred; the
			// caption takes the width that is left, so its own alignment and
			// clipping happen inside a column that never overlaps the symbol
			const Ogre::Real side = this->currentSymbolSide();
			this->checkbox->setSize(side, side);
			this->checkbox->setPosition(origin.x + size.x - side - inset,
				origin.y + (size.y - side) * 0.5f);
			this->label->setPosition(origin.x + inset, origin.y);
			this->label->setSize(std::max(0.f,
				size.x - side - 3.f * inset), size.y);
		}
		else
		{
			// PLATE skin: the caption box IS the plate (its alignment centres it)
			this->label->setPosition(origin.x, origin.y);
			this->label->setSize(size.x, size.y);
		}
	}
	//----------------------------------------------------
	Ogre::Vector2 GuiCheckBox::getPreferredSize()
	{
		const Ogre::Vector2 size = this->decor->getSize();
		if (!this->label)
		{
			return size;
		}
		const Ogre::Vector2 caption = this->label->getPreferredSize();
		const Ogre::Real inset = margin();
		// the caption plus its margins, and in the BOX skin the symbol column
		Ogre::Real wanted = caption.x + 2.f * inset;
		if (this->checkbox != NULL)
		{
			wanted += this->currentSymbolSide() + inset;
		}
		return Ogre::Vector2(std::max(size.x, wanted),
			std::max(size.y, caption.y));
	}
	//----------------------------------------------------
	void GuiCheckBox::setNineSlice( bool enable )
	{
		if (this->decor)
		{
			this->decor->setNineSlice(enable);
		}
	}
	//----------------------------------------------------
	void GuiCheckBox::setTiled( bool enable )
	{
		if (this->decor)
		{
			this->decor->setTiled(enable);
		}
	}
	//----------------------------------------------------
	Ogre::Vector2 GuiCheckBox::getSize()
	{
		return this->decor->getSize();
	}
	//----------------------------------------------------
	Ogre::Vector2 GuiCheckBox::getPosition()
	{
		return this->decor->getPosition();
	}
	//----------------------------------------------------
	void GuiCheckBox::onCursorPressed( Ogre::Vector2 const & cursorPos )
	{
		if (this->decor->getRectangle()->intersects(cursorPos))
		{
			if (this->toggleGroup != NULL)
			{
				// single-selection: the group decides which member ends up
				// checked (and clears the siblings)
				this->toggleGroup->handleMemberTapped(this);
			}
			else
			{
				this->toggle();
			}
		}
	}
	//----------------------------------------------------
	void GuiCheckBox::onCursorReleased( Ogre::Vector2 const & cursorPos )
	{

	}
	//----------------------------------------------------
	void GuiCheckBox::setChecked( bool checked, bool notifyListener /*= true*/ )
	{
		this->checked = checked;

		if (this->checkbox != NULL)
		{
			this->checkbox->setSprite(this->checked
				? CHECK_SYMBOL_ON : CHECK_SYMBOL_OFF);
		}
		else
		{
			Orkige::String spriteName(this->baseSpriteName);
			spriteName += (this->checked ? "_on" : "_off");
			this->decor->setSprite(spriteName);
		}
		if (notifyListener)
		{
			// gui.toggled onto the ONE engine event bus (queued on
			// GlobalEventManager); the checkbox isChecked() poll stays valid
			GuiManager::getSingleton().emitGuiToggled(this->getObjectID(),
				this->checked);
		}
	}
	//----------------------------------------------------
	void GuiCheckBox::toggle( bool notifyListener /*= true*/ )
	{
		this->setChecked(!this->isChecked(), notifyListener);
	}
	//----------------------------------------------------
	void GuiCheckBox::onEnabledChanged( bool enable )
	{
		const float alpha = enable ? 1.0f : GuiWidget::DISABLED_ALPHA;
		if(this->decor)
		{
			this->decor->setAlpha(alpha);
		}
		if(this->checkbox)
		{
			this->checkbox->setAlpha(alpha);
		}
		if(this->label)
		{
			this->label->setAlpha(alpha);
		}
	}

    //----------------------------------------------------
    //- protected: ---------------------------------------
    //----------------------------------------------------

    //----------------------------------------------------
    //- private: -----------------------------------------
    //----------------------------------------------------
	void GuiCheckBox::applyRenderTransform(Ui2DTransform const & transform)
	{
		if(this->decor)		this->decor->applyRenderTransform(transform);
		if(this->checkbox)	this->checkbox->applyRenderTransform(transform);
		if(this->label)		this->label->applyRenderTransform(transform);
	}
	//----------------------------------------------------
	void GuiCheckBox::applyRenderAlpha(float alphaMultiplier)
	{
		if(this->decor)		this->decor->applyRenderAlpha(alphaMultiplier);
		if(this->checkbox)	this->checkbox->applyRenderAlpha(alphaMultiplier);
		if(this->label)		this->label->applyRenderAlpha(alphaMultiplier);
	}
	//----------------------------------------------------
	//---------------------------------------------------------
	void GuiCheckBox::onTextStyleChanged()
	{
		this->forwardTextStyle(this->label);
		// the caption's measured width feeds the BOX skin's column, so a
		// font/scale change has to re-place the parts
		this->arrangeParts();
	}
	OABSTRACT_IMPL(GuiCheckBox)
		// settings toggle: scripts poll isChecked() and drive setChecked/toggle
		// (position/size accessors are inherited from GuiWidget)
		OFUNC(isChecked)
		OFUNC(setChecked)
		OFUNC(toggle)
	OOBJECT_END
}