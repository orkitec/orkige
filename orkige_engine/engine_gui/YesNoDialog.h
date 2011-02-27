/********************************************************************
	created:	Friday 2010/08/06 at 19:03
	filename: 	YesNoDialog.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __YesNoDialog_h__6_8_2010__19_03_50__
#define __YesNoDialog_h__6_8_2010__19_03_50__

#include "engine_gui/Dialog.h"
#include "engine_gui/Button.h"
#include <core_event/EventHandler.h>

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! simple dialog with 2 buttons (yes/no) and textfield
	class YesNoDialog : public Orkige::EventHandler, public Dialog
	{
		OOBJECT(YesNoDialog, Dialog)
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when button1 (yes) is pressed
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(YesNoDialogClosedEvent);
		//! @brief triggered when button 2 (no) is pressed
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(YesNoDialogAbortEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Button* yesButton;		//!< button1
		Button* noButton;		//!< button2
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		YesNoDialog(DialogType type, String const & name, String const & materialGroup, Ogre::DisplayString const & caption, Ogre::DisplayString const & message, const Ogre::DisplayString& yesButtontext,const Ogre::DisplayString& noButtontext);
		//! destructor
		virtual ~YesNoDialog();

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);
	protected:
		//! triggered whenbutton was pressed
		bool onButtonHit(Orkige::Event const & event);
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__YesNoDialog_h__6_8_2010__19_03_50__