/********************************************************************
	created:	Friday 2026/07/11 at 12:00
	filename: 	GuiTextEntry.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiTextEntry_h__11_7_2026__12_00_00__
#define __GuiTextEntry_h__11_7_2026__12_00_00__

#include "engine_module/EnginePrerequisites.h"
#include "engine_gui/GuiWidget.h"

namespace Orkige
{
	//! @brief a text input field: SDL text-input driven glyph entry with a
	//! blinking caret, in-line editing (backspace / delete / left / right /
	//! home / end), a focus model (tap to focus + raise the mobile keyboard,
	//! tap-away or Return to blur), a max length and dimmed placeholder text.
	//! Renders on the atlas font through the gui DrawLayer2D facade, so it works
	//! on BOTH render flavors.
	//! @remarks Focus is coordinated by GuiManager (one field focused at a
	//! time; it opens/closes the InputManager text-input session). Text arrives
	//! via onTextInput (committed UTF-8 from SDL); editing/navigation keys via
	//! onKeyPressed; the caret blinks off the per-frame onFrameStarted tick. The
	//! pure editing model lives in GuiTextEdit.h (headless-tested).
	//!
	//! MULTI-LINE (opt-in, @see setMultiline) turns the field into a small text
	//! area: Return INSERTS a line break instead of submitting (so wasSubmitted
	//! stays a single-line concept - a multi-line field has no submit and games
	//! add their own send button; blur still ends the SDL text-input session
	//! exactly as before, so the mobile keyboard flow is unchanged), up/down
	//! walk the logical lines keeping the code-point column, and home/end work
	//! within a line. The text SOFT-WRAPS to the field width through the one
	//! shared wrap core (@see TextWrap) - the same breaker a wrapped label uses -
	//! and the field VERTICALLY SCROLLS to keep the caret visible: it renders
	//! only the visible window of wrapped lines (a whole-line clip that costs no
	//! extra draw batch and never claims its layer's scissor rect, so a field can
	//! sit on a shared layer). Everything is byte-identical to the single-line
	//! field until setMultiline(true) is called.
	class ORKIGE_ENGINE_DLL GuiTextEntry : public GuiWidget
	{
		OOBJECT(GuiTextEntry, GuiWidget);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		UiRect*		mBackground;	//!< the field body (sprite or solid fill)
		UiCaption*	mCaption;		//!< the visible text / placeholder
		UiCaption*	mMeasure;		//!< transparent helper: measures the caret prefix
		UiRect*		mCaret;			//!< the blinking insertion caret
		String		mText;			//!< the current UTF-8 contents
		String		mPlaceholder;	//!< dimmed hint shown when empty and unfocused
		size_t		mCaretByte;		//!< caret position as a byte index into mText
		size_t		mMaxLength;		//!< max code points (0 = unlimited)
		bool		mFocused;		//!< has input focus
		bool		mSubmitted;		//!< a Return since the last wasSubmitted poll
		float		mBlinkTimer;	//!< seconds accumulated toward the next caret toggle
		bool		mCaretVisible;	//!< current caret blink phase
		uint		mFontIndex;		//!< atlas font glyph set
		bool		mMultiline;		//!< multi-line mode (@see setMultiline)
		int			mFirstLine;		//!< first VISIBLE wrapped line (vertical scroll)
		int			mLineCount;		//!< wrapped line count of the current text
		int			mVisibleLines;	//!< wrapped lines the field body shows at once
		//--- Methods -----------------------------------------------
	public:
		GuiTextEntry(String const & id, String const & spriteName,
			uint defaultGlyphIndex, String const & placeholder,
			Ogre::Vector2 const & position, Ogre::Vector2 const & size,
			String const & atlas, uint z, uint maxLength);
		virtual ~GuiTextEntry();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		//! the current text
		String const & getText() const { return this->mText; }
		//! replace the text (clamps the caret to the end)
		void setText(String const & text);
		//! set the dimmed placeholder hint (shown when empty and unfocused)
		void setPlaceholder(String const & placeholder);
		//! set the maximum code-point length (0 = unlimited); truncates if shorter
		void setMaxLength(uint maxLength);
		//! max code-point length (0 = unlimited)
		uint getMaxLength() const { return static_cast<uint>(this->mMaxLength); }
		//! has focus (receiving text input)
		bool isFocused() const { return this->mFocused; }
		//! @brief poll-and-consume the submit state: true once after every Return
		//! press while focused (the polled idiom, like GuiButton::wasClicked).
		//! Always false in multi-line mode - Return inserts a newline there.
		bool wasSubmitted();

		//! @brief turn the field into a multi-line text area (soft wrap to the
		//! field width, Return inserts a line break, up/down walk lines, the view
		//! scrolls to follow the caret). Off by default. @see the class remarks.
		void setMultiline(bool multiline);
		//! @brief is the field in multi-line mode?
		inline bool isMultiline() const { return this->mMultiline; }
		//! @brief the number of WRAPPED (visual) lines the current text occupies -
		//! 1 for a single-line field, and what a test asserts against
		inline int getLineCount() const { return this->mLineCount; }
		//! @brief the first wrapped line currently visible (the vertical scroll
		//! position, in lines); 0 for a single-line field
		inline int getFirstVisibleLine() const { return this->mFirstLine; }

		//! @brief flip the focused state WITHOUT touching the SDL text-input
		//! session - GuiManager::focusTextEntry owns the session and calls
		//! this so exactly one field is focused at a time
		void setFocusedState(bool focused);

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onTextInput(String const & text);
		virtual bool onKeyPressed(KeyEventData const & data);
		virtual bool onFrameStarted(FrameEventData const & data);
		virtual void applyRenderTransform(Ui2DTransform const & transform);
		virtual void applyRenderAlpha(float alphaMultiplier);
	protected:
		//! dim the field body + text when disabled (input is gated in the manager)
		virtual void onEnabledChanged(bool enable);
		//! reposition the body / caption / caret for the current position+size
		void relayout();
		//! the font's line height in device pixels (0 without a baked font)
		Ogre::Real lineHeight() const;
		//! refresh the caption text/colour and the caret x for the current state
		void refresh();
		//! @brief the multi-line refresh: wrap the text to the field width, scroll
		//! the line window so the caret stays visible, feed the caption only that
		//! window and place the caret inside it (@see refresh)
		void refreshMultiline();
	private:
	};
}

#endif //__GuiTextEntry_h__11_7_2026__12_00_00__
