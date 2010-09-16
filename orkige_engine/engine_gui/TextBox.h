/********************************************************************
	created:	Wednesday 2010/08/04 at 15:09
	filename: 	TextBox.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __TextBox_h__4_8_2010__15_09_17__
#define __TextBox_h__4_8_2010__15_09_17__

#include "engine_gui/Widget.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Box widget with text in it
	class TextBox : public Widget
	{
		OOBJECT(TextBox, Widget);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::TextAreaOverlayElement* textArea;			//!< area for text
		Ogre::BorderPanelOverlayElement* captionBar;	//!< area for caption
		Ogre::TextAreaOverlayElement* captionTextArea;	//!< actual caption
		Ogre::BorderPanelOverlayElement* scrollTrack;	//!< for scrolling
		Ogre::PanelOverlayElement* scrollHandle;		//!< scrolling handle
		Ogre::DisplayString text;						//!< actual text
		Ogre::StringVector lines;						//!< number of lines
		Ogre::Real padding;								//!< text padding
		bool dragging;									//!< slider dragging?
		Ogre::Real scrollPercentage;					//!< scroll pos
		Ogre::Real dragOffset;							//!< current dragged offset
		unsigned int startingLine;						//!< line where text starts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create TextBox
		//! @copydoc Widget
		TextBox(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real height);
		//! set Text padding
		void setPadding(Ogre::Real padding);
		//! get text padding
		Ogre::Real getPadding();
		//! get textbox caption
		const Ogre::DisplayString& getCaption();
		//! set textbox caption
		void setCaption(const Ogre::DisplayString& caption);
		//! get actual text
		const Ogre::DisplayString& getText();

		//! Sets text box content. Most of this method is for wordwrap.
		void setText(const Ogre::DisplayString& text);

		//! Sets text box content horizontal alignment.
		void setTextAlignment(Ogre::TextAreaOverlayElement::Alignment ta);
		//! clear all text
		void clearText();
		//! append text to current text
		void appendText(const Ogre::DisplayString& text);

		//! Makes adjustments based on new padding, size, or alignment info.
		void refitContents();

		//! Sets how far scrolled down the text is as a percentage.
		void setScrollPercentage(Ogre::Real percentage);

		//! Gets how far scrolled down the text is as a percentage.
		Ogre::Real getScrollPercentage();

		//! Gets how many lines of text can fit in this window.
		unsigned int getHeightInLines();

		// widget overloads
		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);
		virtual void onFocusLost();
	protected:

		//! Decides which lines to show.
		void filterLines();

	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__TextBox_h__4_8_2010__15_09_17__