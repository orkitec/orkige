/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	GuiButtonBlink.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gui/GuiButtonBlink.h"

#include <chrono>
#include "engine_gui/GuiManager.h"
#include <core_event/GlobalEventManager.h>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(GuiButtonBlink, ButtonHitEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	float GuiButtonBlink::blinkingTime = 1.0;

	GuiButtonBlink::GuiButtonBlink(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z, bool _nostate, int blinkState) :
		GuiButton(id, spriteName, defaultGlyphIndex, text, position, textAlignment, size, atlas, z, _nostate)
	{
		//oAssertDesc(size.x > 0.0 && size.y > 0.0, "Warning: button has invalid size and won't create any events: " << id);

		this->blinkState = (ButtonBlinkState)blinkState;
	
		this->decor2 = onew(new GuiDecorWidget(id + ".decor2", spriteName, position, size, atlas, z+1));

		this->setState(this->state);
	}
	//---------------------------------------------------------
	GuiButtonBlink::~GuiButtonBlink()
	{
	}
	//---------------------------------------------------------
	bool GuiButtonBlink::onFrameStarted(FrameEventData const & data)
	{
		if (this->blinkState != BBLINK_NONE && this->state == BS_UP)
		{
			// global time in seconds, all buttons blink synchronously
			// (std::chrono - the Ogre root timer was the classic backend's)
			const unsigned long currentTime = static_cast<unsigned long>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
			float time = static_cast<float>(currentTime % 86400000ul) * 0.001;

			// simple saw tooth, faster than sinus 
			float f = fmod(time / this->blinkingTime, 2.0f);
			if (f > 1.0) f = 2.0 - f;
			oAssert(f >= 0.0 && f <= 1.0);

			if (this->blinkState == BBLINK_BASE) 
			{
				this->decor->getRectangle()->setAlpha(f);
			}
			else if (this->blinkState == BBLINK_HIGHLIGHT) 
			{
				this->decor2->getRectangle()->setAlpha(f);
			}
			else if (this->blinkState == BBLINK_BASE_AND_HIGHLIGHT) 
			{
				this->decor->getRectangle()->setAlpha(f);
				this->decor2->getRectangle()->setAlpha(1.0-f);
			}
		}
		return false;
	}
	//---------------------------------------------------------
	void GuiButtonBlink::setBlinkingTime(float blinkingTime)
	{
		GuiButtonBlink::blinkingTime = blinkingTime;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void GuiButtonBlink::setState(const GuiButtonBlink::ButtonState& bs)
	{
		if (!this->nostate)
		{
			if (this->blinkState == GuiButtonBlink::BBLINK_NONE)
			{
				// classic non-blinking behavior
				GuiButton::setState(bs);
				this->decor->getRectangle()->setAlpha(1.0f);

				// disable
				this->decor2->setSprite("none");
				this->decor2->getRectangle()->setAlpha(0.0f);
			}
			else
			{
				// default
				this->decor->setSprite(this->baseSpriteName);
				this->decor->getRectangle()->setAlpha(1.0f);

				// some blinking modes needs to show both sprites simultaneously 
				if (bs == GuiButtonBlink::BS_DOWN 
					|| this->blinkState == GuiButtonBlink::BBLINK_HIGHLIGHT
					|| this->blinkState == GuiButtonBlink::BBLINK_BASE_AND_HIGHLIGHT)
				{
					this->decor2->setSprite(this->baseSpriteName + "_down");
					this->decor2->getRectangle()->setAlpha(1.0f);
				}
				else if (bs == GuiButtonBlink::BS_OVER)
				{
					this->decor2->setSprite(this->baseSpriteName + "_over");
					this->decor2->getRectangle()->setAlpha(1.0f);
				}
				else
				{
					this->decor2->setSprite("none");
					this->decor2->getRectangle()->setAlpha(0.0f);
				}
			}
		}
		this->state = bs;
	}
	//---------------------------------------------------------
	const GuiButtonBlink::ButtonBlinkState& GuiButtonBlink::getBlinkState()
	{
		return this->blinkState;
	}
	//---------------------------------------------------------
	void GuiButtonBlink::setBlinkState(const ButtonBlinkState& blinkState)
	{
		// reset sprites
		GuiButton::setState(GuiButton::BS_UP);

		if (this->blinkState == BBLINK_NONE && blinkState != BBLINK_NONE)
		{
			// reset alpha
			this->decor->getRectangle()->setAlpha(1.0);
			this->decor2->getRectangle()->setAlpha(1.0);
		}
		else if (this->blinkState != BBLINK_NONE && blinkState == BBLINK_NONE)
		{
			// reset alpha
			this->decor->getRectangle()->setAlpha(1.0);
			this->decor2->getRectangle()->setAlpha(0.0);
		}
		this->blinkState = blinkState;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiButtonBlink)
	OOBJECT_END
}