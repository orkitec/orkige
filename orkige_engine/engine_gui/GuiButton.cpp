/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	GuiButton.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gui/GuiButton.h"
#include "engine_gui/GuiManager.h"
#include <core_event/GlobalEventManager.h>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(GuiButton, ButtonHitEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiButton::GuiButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z, bool _nostate) :
		GuiWidget(id, atlas, z)
	{
		//oAssertDesc(size.x > 0.0 && size.y > 0.0, "Warning: button has invalid size and won't create any events: " << id);

		this->nostate = _nostate;
		this->clicked = false;
		this->pressFeedback = false;
		this->decor = onew(new GuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		
		if(!text.empty())
		{
			this->label = onew(new GuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z, true));
			this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
			this->label->setAlignment(textAlignment);
		}

		this->state = BS_UP;
		this->baseSpriteName = spriteName;
	}
	//---------------------------------------------------------
	GuiButton::~GuiButton()
	{
	}
	//---------------------------------------------------------
	const GuiButton::ButtonState& GuiButton::getState() 
	{ 
		return this->state; 
	}
	//---------------------------------------------------------
	void GuiButton::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->decor->setPosition(left, top);
		if(label) 
			this->label->setPosition(left, top);
	}
	//---------------------------------------------------------
	void GuiButton::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->decor->setSize(width, height);
		if(label) 
			this->label->setSize(width, height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiButton::getSize()
	{
		return this->decor->getSize();
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiButton::getPosition()
	{
		return this->decor->getPosition();
	}
	//---------------------------------------------------------
	void GuiButton::onCursorPressed(Ogre::Vector2 const & cursorPos)
	{
		// a button on a hidden layer is not on screen - it must neither
		// swallow clicks nor collect a stale clicked flag (found when the
		// Lua jumper put its START/AGAIN buttons on toggled layers)
		if (!this->layer->isVisible())
		{
			return;
		}
		if (this->state != BS_DISABLED)
		{
			if (this->decor->getRectangle()->intersects(cursorPos)) 
			{
				this->setState(GuiButton::BS_DOWN);
			}
		}
	}
	//---------------------------------------------------------
	void GuiButton::onCursorReleased(Ogre::Vector2 const & cursorPos)
	{
		if (!this->layer->isVisible())
		{
			return;
		}
		if (this->state != BS_DISABLED)
		{
			if (this->state == GuiButton::BS_DOWN)
			{
				this->setState(GuiButton::BS_OVER);
				this->clicked = true;
				// the click goes onto the ONE engine event bus as gui.clicked
				// (queued on GlobalEventManager, drained in the script phase) -
				// C++ and Lua both subscribe. wasClicked() polling is a separate
				// latch (set above) and stays valid.
				if(GuiManager::getSingletonPtr())
				{
					GuiManager::getSingleton().emitGuiClicked(this->getObjectID());
				}
			}
		}
	}
	//---------------------------------------------------------
	void GuiButton::onCursorMoved(Ogre::Vector2 const & cursorPos)
	{
		if (!this->layer->isVisible())
		{
			return;
		}
		if (this->state != BS_DISABLED)
		{
			if(this->decor->getRectangle()->intersects(cursorPos))
			{
				if (this->state == GuiButton::BS_UP) 
				{
					setState(GuiButton::BS_OVER);
				}
			}
			else
			{
				if (this->state != GuiButton::BS_UP)
				{
					setState(GuiButton::BS_UP);
				}
			}
		}
	}
	//---------------------------------------------------------
	String GuiButton::getCaption()
	{
		if(label)
			return this->label->getCaption()->text();	
		else
			return StringUtil::BLANK;
	}
	//---------------------------------------------------------
	void GuiButton::setCaption(String const & text)
	{
		if(label)
			this->label->setText(text);
	}
	//---------------------------------------------------------
	bool GuiButton::wasClicked()
	{
		bool wasHit = this->clicked;
		this->clicked = false;
		return wasHit;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void GuiButton::setPressFeedback(bool enable)
	{
		this->pressFeedback = enable;
	}
	//---------------------------------------------------------
	void GuiButton::setState(const GuiButton::ButtonState& bs)
	{
		// press-feedback juice: snap smaller entering the pressed state, spring
		// back (a slight overshoot) leaving it. Runs through the shared tween
		// path so it retarget-replaces and auto-kills like any widget animation.
		if (this->pressFeedback && bs != this->state)
		{
			if (bs == GuiButton::BS_DOWN)
			{
				GuiManager::getSingleton().cancelWidgetTween(this->getObjectID(),
					GuiManager::WTC_Scale);
				this->setRenderScale(0.94f, 0.94f);
			}
			else if (this->state == GuiButton::BS_DOWN)
			{
				const float to[2] = { 1.0f, 1.0f };
				GuiManager::getSingleton().tweenWidget(this->getObjectID(),
					GuiManager::WTC_Scale, to, 0.18f, &Ease::backOut);
			}
		}
		if (!this->nostate)
		{
			if (bs == GuiButton::BS_UP)
			{
				this->decor->setSprite(this->baseSpriteName);
			}
			else if (bs == GuiButton::BS_OVER)
			{
				this->decor->setSprite(this->baseSpriteName + "_over");
			}
			else if (bs == GuiButton::BS_DOWN)
			{
				this->decor->setSprite(this->baseSpriteName + "_down");
			}
			else if (bs == GuiButton::BS_DISABLED)
			{
				this->decor->setSprite(this->baseSpriteName + "_disabled");
			}
		}
		this->state = bs;
	}
	//---------------------------------------------------------
	void GuiButton::onEnabledChanged(bool enable)
	{
		// reuse the existing state machine: the DISABLED state already swaps to
		// the `_disabled` sprite (and dims the label with it on a stateful skin)
		this->setState(enable ? GuiButton::BS_UP : GuiButton::BS_DISABLED);
		if(this->label)
		{
			this->label->setAlpha(enable ? 1.0f : GuiWidget::DISABLED_ALPHA);
		}
	}
	//---------------------------------------------------------
	void GuiButton::setNineSlice(bool enable)
	{
		if(this->decor)
		{
			this->decor->setNineSlice(enable);
		}
	}
	//---------------------------------------------------------
	void GuiButton::setTiled(bool enable)
	{
		if(this->decor)
		{
			this->decor->setTiled(enable);
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void GuiButton::applyRenderTransform(Ui2DTransform const & transform)
	{
		// decor + label transform as one unit about the button centre (the
		// transform pivot is shared, computed by the owning widget)
		if(this->decor)	this->decor->applyRenderTransform(transform);
		if(this->label)	this->label->applyRenderTransform(transform);
	}
	//---------------------------------------------------------
	void GuiButton::applyRenderAlpha(float alphaMultiplier)
	{
		if(this->decor)	this->decor->applyRenderAlpha(alphaMultiplier);
		if(this->label)	this->label->applyRenderAlpha(alphaMultiplier);
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiButton)
		OFUNC(getCaption)
		OFUNC(setCaption)
		OFUNC(getState)
		OFUNC(wasClicked)
		OFUNC(setNineSlice)
		OFUNC(setTiled)
		OFUNC(setPressFeedback)
		OENUM_START(ButtonState)
			OENUM_VALUE(BS_DISABLED)
			OENUM_VALUE(BS_UP)
			OENUM_VALUE(BS_OVER)
			OENUM_VALUE(BS_DOWN)
		OENUM_END
	OOBJECT_END
}