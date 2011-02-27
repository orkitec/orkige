/********************************************************************
	created:	Wednesday 2010/08/04 at 15:08
	filename: 	Separator.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __Separator_h__4_8_2010__15_08_55__
#define __Separator_h__4_8_2010__15_08_55__

#include "engine_gui/Widget.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! simple seperator Widget
	class Separator : public Widget
	{
		OOBJECT(Separator, Widget);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		bool fitToTray;	//!< fit size to content
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create Separator
		//! @copydoc Widget
		Separator(String const & name, String const & materialGroup, Ogre::Real width);
		//! @copydoc Separator::fitToTray
		bool _isFitToTray();
	protected:
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__Separator_h__4_8_2010__15_08_55__