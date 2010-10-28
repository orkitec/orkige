/********************************************************************
	created:	Wednesday 2010/10/27 at 13:08
	filename: 	FastGuiWidget.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __FastGuiWidget_h__27_10_2010__13_08_39__
#define __FastGuiWidget_h__27_10_2010__13_08_39__

#include "engine_gui/IGuiObject.h"

namespace Orkige
{
	class FastGuiWidget : public IGuiObject
	{
		OOBJECT(FastGuiWidget, IGuiObject);
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
		FastGuiWidget(String const & id);
		virtual ~FastGuiWidget();
	protected:
	private:
	};
	//---------------------------------------------------------------
}

#endif //__FastGuiWidget_h__27_10_2010__13_08_39__