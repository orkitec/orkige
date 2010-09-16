/********************************************************************
	created:	Friday 2010/08/06 at 19:03
	filename: 	OkDialog.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __OkDialog_h__6_8_2010__19_03_36__
#define __OkDialog_h__6_8_2010__19_03_36__

#include "engine_gui/Dialog.h"
#include "engine_gui/Button.h"
#include <core_event/EventHandler.h>

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! dialog with a textbox and a (OK) button
	class OkDialog : public Orkige::EventHandler, public Dialog
	{
		OOBJECT(OkDialog, Dialog)
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when OkDialog is closed
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(OkDialogClosedEvent);
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Button* okButton;	//!< Dialog Button
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		OkDialog(DialogType type, String const & name, String const & materialGroup, Ogre::DisplayString const & caption, Ogre::DisplayString const & message, Ogre::DisplayString const & okButtonText);
		//! destructor
		virtual ~OkDialog();

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

#endif //__OkDialog_h__6_8_2010__19_03_36__