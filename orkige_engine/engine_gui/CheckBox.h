/********************************************************************
	created:	Wednesday 2010/08/04 at 15:05
	filename: 	CheckBox.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __CheckBox_h__4_8_2010__15_05_28__
#define __CheckBox_h__4_8_2010__15_05_28__

#include "engine_gui/Widget.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Basic check box widget.
	class CheckBox : public Widget
	{
		OOBJECT(CheckBox, Widget);
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when CheckBox is toggled
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(CheckBoxToggledEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Ogre::TextAreaOverlayElement* textArea;		//!< box text field
		Ogre::BorderPanelOverlayElement* square;	//!< box border
		Ogre::OverlayElement* checkElement;			//!< box checker
		bool fitToContents;							//!< if true size depends on content
		bool cursorOver;							//!< is cursor currently over?
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create a CheckBox
		//! @copydoc Widget
		CheckBox(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width);
		//! get checkbox caption
		const Ogre::DisplayString& getCaption();
		//! set caption
		void setCaption(const Ogre::DisplayString& caption);
		//! is box currently checked?
		bool isChecked();
		//! set box checked and trigger CheckBox::CheckBoxToggledEvent if notifyListener = true
		void setChecked(bool checked, bool notifyListener = true);
		//! toggle state and trigger CheckBox::CheckBoxToggledEvent if notifyListener = true
		void toggle(bool notifyListener = true);
		
		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);
		virtual void onFocusLost();
	protected:
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__CheckBox_h__4_8_2010__15_05_28__