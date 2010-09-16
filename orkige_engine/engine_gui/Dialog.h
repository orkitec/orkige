/**************************************************************
	created:	2010/08/07 at 3:36
	filename: 	Dialog.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __Dialog_h__7_8_2010__3_36_38__
#define __Dialog_h__7_8_2010__3_36_38__

#include "engine_gui/TextBox.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Abstract Dialog Base Class
	class Dialog : public IGuiObject
	{
		OOBJECT(Dialog, IGuiObject);
		//--- Types -------------------------------------------
	public:
		typedef int DialogType;	//!< identfier for different Dialog implementations
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		TextBox* text;			//!< Dialog textbox
		DialogType dialogType;	//!< type of this dialog
	private:
		//--- Methods -----------------------------------------
	public:
		//! destructor
		virtual ~Dialog();
		//! get type of this Dialog
		DialogType getDialogType();
	protected:
		//! protected constructor to prevent direct construction
		Dialog(DialogType type, String const & name, String const & materialGroup, Ogre::DisplayString const & caption, Ogre::DisplayString const & message);
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------
}

#endif //__Dialog_h__7_8_2010__3_36_38__