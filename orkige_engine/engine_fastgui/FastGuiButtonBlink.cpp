/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiButtonBlink.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiButtonBlink.h"
#include "engine_fastgui/FastGuiManager.h"
#include <core_event/GlobalEventManager.h>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(FastGuiButtonBlink, ButtonHitEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiButtonBlink::FastGuiButtonBlink(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z, bool _nostate, int blinkState) :
		FastGuiButton(id, spriteName, defaultGlyphIndex, text, position, textAlignment, size, atlas, z, _nostate)
	{
		//oAssertDesc(size.x > 0.0 && size.y > 0.0, "Warning: button has invalid size and won't create any events: " << id);

		this->blinkState = (ButtonBlinkState)blinkState;
		this->blinkingTime = 0.5;
	
		this->decor2 = onew(new FastGuiDecorWidget(id + ".decor2", spriteName, position, size, atlas, z+1));

		this->setState(this->state);
	}
	//---------------------------------------------------------
	FastGuiButtonBlink::~FastGuiButtonBlink()
	{
	}
	//---------------------------------------------------------
	bool FastGuiButtonBlink::onFrameStarted(FrameEventData const & data)
	{
		if (this->blinkState != BBLINK_NONE && this->state == BS_UP)
		{
			// global time in seconds, all buttons blink synchronously
			unsigned long currentTime = Ogre::Root::getSingleton().getTimer()->getMilliseconds();
			float time = static_cast<float>(currentTime) * 0.001;

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
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void FastGuiButtonBlink::setState(const FastGuiButtonBlink::ButtonState& bs)
	{
		if (!this->nostate)
		{
			if (this->blinkState == FastGuiButtonBlink::BBLINK_NONE)
			{
				// classic non-blinking behavior
				__super::setState(bs);
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
				if (bs == FastGuiButtonBlink::BS_DOWN 
					|| this->blinkState == FastGuiButtonBlink::BBLINK_HIGHLIGHT
					|| this->blinkState == FastGuiButtonBlink::BBLINK_BASE_AND_HIGHLIGHT)
				{
					this->decor2->setSprite(this->baseSpriteName + "_down");
					this->decor2->getRectangle()->setAlpha(1.0f);
				}
				else if (bs == FastGuiButtonBlink::BS_OVER)
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
	const FastGuiButtonBlink::ButtonBlinkState& FastGuiButtonBlink::getBlinkState()
	{
		return this->blinkState;
	}
	//---------------------------------------------------------
	void FastGuiButtonBlink::setBlinkState(const ButtonBlinkState& blinkState)
	{
		// reset sprites
		__super::setState(FastGuiButton::BS_UP);

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
	OABSTRACT_IMPL(FastGuiButtonBlink)
	OOBJECT_END
}