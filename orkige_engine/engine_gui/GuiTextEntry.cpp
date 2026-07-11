/********************************************************************
	created:	Friday 2026/07/11 at 12:00
	filename: 	GuiTextEntry.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gui/GuiTextEntry.h"
#include "engine_gui/GuiTextEdit.h"
#include "engine_gui/GuiManager.h"

#include <cmath>

namespace Orkige
{
	namespace
	{
		//! inner padding between the field body and the text (pixels)
		const Ogre::Real TEXT_ENTRY_PADDING = 6.0f;
		//! caret bar width (pixels) and blink half-period (seconds)
		const Ogre::Real TEXT_ENTRY_CARET_WIDTH = 2.0f;
		const float TEXT_ENTRY_BLINK_PERIOD = 0.5f;
		//! text / placeholder colours (straight RGBA)
		Color textColour() { return Color(1.0f, 1.0f, 1.0f, 1.0f); }
		Color placeholderColour() { return Color(0.6f, 0.6f, 0.6f, 1.0f); }
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiTextEntry::GuiTextEntry(String const & id,
		String const & spriteName, uint defaultGlyphIndex,
		String const & placeholder, Ogre::Vector2 const & position,
		Ogre::Vector2 const & size, String const & atlas, uint z, uint maxLength)
		: GuiWidget(id, atlas, z)
		, mText(""), mPlaceholder(placeholder), mCaretByte(0)
		, mMaxLength(maxLength), mFocused(false), mSubmitted(false)
		, mBlinkTimer(0.0f), mCaretVisible(true), mFontIndex(defaultGlyphIndex)
	{
		this->mBackground = this->layer->createRectangle(position, size);
		if(spriteName.empty() || spriteName == "none")
		{
			// no sprite: a subtle dark translucent fill so the field reads as a box
			this->mBackground->background_colour(Color(0.0f, 0.0f, 0.0f, 0.4f));
		}
		else
		{
			this->mBackground->background_image(spriteName);
		}
		this->mCaption = this->layer->createCaption(defaultGlyphIndex,
			position.x + TEXT_ENTRY_PADDING, position.y + TEXT_ENTRY_PADDING, "");
		// the measuring caption is invisible (alpha 0) - it only exists so the
		// caret x can be found from the width of the text before the caret
		this->mMeasure = this->layer->createCaption(defaultGlyphIndex,
			position.x + TEXT_ENTRY_PADDING, position.y + TEXT_ENTRY_PADDING, "");
		this->mMeasure->colour(Color(0.0f, 0.0f, 0.0f, 0.0f));
		this->mCaret = this->layer->createRectangle(
			position.x + TEXT_ENTRY_PADDING, position.y + TEXT_ENTRY_PADDING,
			TEXT_ENTRY_CARET_WIDTH, size.y - 2.0f * TEXT_ENTRY_PADDING);
		this->mCaret->background_colour(textColour());
		this->mCaret->setAlpha(0.0f);	// hidden until focused
		this->relayout();
		this->refresh();
	}
	//---------------------------------------------------------
	GuiTextEntry::~GuiTextEntry()
	{
		// a destroyed focused field must release the input session it holds
		if(this->mFocused && GuiManager::getSingletonPtr())
		{
			GuiManager::getSingleton().notifyTextEntryDestroyed(this);
		}
		this->layer->destroyRectangle(this->mCaret);
		this->layer->destroyCaption(this->mMeasure);
		this->layer->destroyCaption(this->mCaption);
		this->layer->destroyRectangle(this->mBackground);
	}
	//---------------------------------------------------------
	void GuiTextEntry::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->mBackground->position(left, top);
		this->relayout();
		this->refresh();
	}
	//---------------------------------------------------------
	void GuiTextEntry::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->mBackground->width(width);
		this->mBackground->height(height);
		this->relayout();
		this->refresh();
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiTextEntry::getSize()
	{
		return Ogre::Vector2(this->mBackground->width(),
			this->mBackground->height());
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiTextEntry::getPosition()
	{
		return Ogre::Vector2(this->mBackground->left(), this->mBackground->top());
	}
	//---------------------------------------------------------
	void GuiTextEntry::setText(String const & text)
	{
		this->mText = text;
		this->mCaretByte = this->mText.size();	// caret to the end
		this->refresh();
	}
	//---------------------------------------------------------
	void GuiTextEntry::setPlaceholder(String const & placeholder)
	{
		this->mPlaceholder = placeholder;
		this->refresh();
	}
	//---------------------------------------------------------
	void GuiTextEntry::setMaxLength(uint maxLength)
	{
		this->mMaxLength = maxLength;
		// truncate to the new budget from the end
		if(maxLength > 0)
		{
			while(TextEntryEdit::codepointCount(this->mText) > maxLength &&
				!this->mText.empty())
			{
				this->mCaretByte = this->mText.size();
				TextEntryEdit::backspace(this->mText, this->mCaretByte);
			}
		}
		this->refresh();
	}
	//---------------------------------------------------------
	bool GuiTextEntry::wasSubmitted()
	{
		const bool was = this->mSubmitted;
		this->mSubmitted = false;
		return was;
	}
	//---------------------------------------------------------
	void GuiTextEntry::setFocusedState(bool focused)
	{
		if(this->mFocused == focused)
		{
			return;
		}
		this->mFocused = focused;
		// a fresh focus shows the caret immediately (no half-period wait)
		this->mCaretVisible = true;
		this->mBlinkTimer = 0.0f;
		this->refresh();
	}
	//---------------------------------------------------------
	void GuiTextEntry::onCursorPressed(Ogre::Vector2 const & cursorPos)
	{
		// tap inside -> request focus (GuiManager blurs any other field and
		// opens the text-input session). A tap outside is handled by the manager
		// (it blurs when a press claimed no field).
		if(this->mBackground->intersects(cursorPos) &&
			GuiManager::getSingletonPtr())
		{
			GuiManager::getSingleton().focusTextEntry(this);
		}
	}
	//---------------------------------------------------------
	void GuiTextEntry::onTextInput(String const & text)
	{
		if(!this->mFocused || text.empty())
		{
			return;
		}
		if(TextEntryEdit::insert(this->mText, this->mCaretByte, text,
			this->mMaxLength))
		{
			this->mCaretVisible = true;
			this->mBlinkTimer = 0.0f;
			this->refresh();
		}
	}
	//---------------------------------------------------------
	bool GuiTextEntry::onKeyPressed(KeyEventData const & data)
	{
		if(!this->mFocused)
		{
			return false;
		}
		bool handled = true;
		switch(data.key)
		{
		case KeyEventData::KC_BACK:
			TextEntryEdit::backspace(this->mText, this->mCaretByte);
			break;
		case KeyEventData::KC_DELETE:
			TextEntryEdit::del(this->mText, this->mCaretByte);
			break;
		case KeyEventData::KC_LEFT:
			TextEntryEdit::moveLeft(this->mText, this->mCaretByte);
			break;
		case KeyEventData::KC_RIGHT:
			TextEntryEdit::moveRight(this->mText, this->mCaretByte);
			break;
		case KeyEventData::KC_HOME:
			this->mCaretByte = 0;
			break;
		case KeyEventData::KC_END:
			this->mCaretByte = this->mText.size();
			break;
		case KeyEventData::KC_RETURN:
		case KeyEventData::KC_NUMPADENTER:
			// submit: latch the poll flag and blur (closes the input session)
			this->mSubmitted = true;
			if(GuiManager::getSingletonPtr())
			{
				GuiManager::getSingleton().focusTextEntry(NULL);
			}
			break;
		default:
			handled = false;	// let other handlers see keys we do not edit with
			break;
		}
		if(handled)
		{
			this->mCaretVisible = true;
			this->mBlinkTimer = 0.0f;
			this->refresh();
		}
		return handled;
	}
	//---------------------------------------------------------
	bool GuiTextEntry::onFrameStarted(FrameEventData const & data)
	{
		// caret blink on wall-clock time; hidden entirely while unfocused
		this->mBlinkTimer += data.timeSinceLastFrame;
		if(this->mBlinkTimer >= TEXT_ENTRY_BLINK_PERIOD)
		{
			this->mBlinkTimer -= TEXT_ENTRY_BLINK_PERIOD;
			this->mCaretVisible = !this->mCaretVisible;
		}
		// only touch the caret when its opacity ACTUALLY changes - setAlpha dirties
		// the screen, and an unfocused (or steadily-blinked) field must not force a
		// per-frame rebuild (the dirty-tracking contract; @see the gui-perf check)
		const float caretAlpha =
			(this->mFocused && this->mCaretVisible) ? 1.0f : 0.0f;
		if(this->mCaret->colour().a != caretAlpha)
		{
			this->mCaret->setAlpha(caretAlpha);
		}
		return false;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void GuiTextEntry::relayout()
	{
		// Caption asserts on subpixel positions - floor everything
		const Ogre::Real left = std::floor(this->mBackground->left());
		const Ogre::Real top = std::floor(this->mBackground->top());
		const Ogre::Real height = this->mBackground->height();
		const Ogre::Real textLeft = std::floor(left + TEXT_ENTRY_PADDING);
		const Ogre::Real textTop = std::floor(top + TEXT_ENTRY_PADDING);
		this->mCaption->left(textLeft);
		this->mCaption->top(textTop);
		this->mMeasure->left(textLeft);
		this->mMeasure->top(textTop);
		this->mCaret->top(textTop);
		this->mCaret->width(TEXT_ENTRY_CARET_WIDTH);
		this->mCaret->height(std::max(height - 2.0f * TEXT_ENTRY_PADDING, 4.0f));
	}
	//---------------------------------------------------------
	void GuiTextEntry::onEnabledChanged(bool enable)
	{
		// input is gated in the manager; just fade the body + text so a
		// disabled field reads as inactive
		this->mBackground->setAlpha(enable ? 1.0f : GuiWidget::DISABLED_ALPHA);
		Color colour = this->mCaption->colour();
		colour.a = enable ? 1.0f : GuiWidget::DISABLED_ALPHA;
		this->mCaption->colour(colour);
	}
	//---------------------------------------------------------
	void GuiTextEntry::refresh()
	{
		const bool showPlaceholder = this->mText.empty() && !this->mFocused;
		this->mCaption->text(showPlaceholder ? this->mPlaceholder : this->mText);
		this->mCaption->colour(showPlaceholder ? placeholderColour()
			: textColour());
		// caret x = text origin + the measured width of the text before the caret
		this->mMeasure->text(this->mText.substr(0, this->mCaretByte));
		Vec2 measured(0.0f, 0.0f);
		this->mMeasure->_calculateDrawSize(measured);
		this->mCaret->left(std::floor(this->mCaption->left() + measured.x));
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void GuiTextEntry::applyRenderTransform(Ui2DTransform const & transform)
	{
		// the field body, the visible caption and the caret transform together;
		// the hidden measuring caption never draws, so it is left alone
		if(this->mBackground)	this->mBackground->renderTransform(transform);
		if(this->mCaption)		this->mCaption->renderTransform(transform);
		if(this->mCaret)		this->mCaret->renderTransform(transform);
	}
	//---------------------------------------------------------
	void GuiTextEntry::applyRenderAlpha(float alphaMultiplier)
	{
		if(this->mBackground)	this->mBackground->renderAlpha(alphaMultiplier);
		if(this->mCaption)		this->mCaption->renderAlpha(alphaMultiplier);
		if(this->mCaret)		this->mCaret->renderAlpha(alphaMultiplier);
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiTextEntry)
		OFUNC(setPosition)
		OFUNC(setSize)
		OFUNC(getSize)
		OFUNC(getPosition)
		OFUNCCR(getText)
		OFUNC(setText)
		OFUNC(setPlaceholder)
		OFUNC(setMaxLength)
		OFUNC(getMaxLength)
		OFUNC(isFocused)
		OFUNC(wasSubmitted)
	OOBJECT_END
}
