/********************************************************************
	created:	Wednesday 2010/08/04 at 15:06
	filename: 	DecorWidget.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __DecorWidget_h__4_8_2010__15_06_50__
#define __DecorWidget_h__4_8_2010__15_06_50__

#include "engine_gui/Widget.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Custom, decorative widget created from a template.
	class DecorWidget : public Widget
	{
		OOBJECT(DecorWidget, Widget);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		//! @brief create a DecorWidget
		//! @copydoc Widget
		DecorWidget(String const & name, String const & materialGroup, String const & templateName);
	protected:
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__DecorWidget_h__4_8_2010__15_06_50__