/********************************************************************
	created:	Wednesday 2010/10/27 at 13:08
	filename: 	FastGuiWidget.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiWidget.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiWidget::FastGuiWidget(String const & id) : IGuiObject(id)
	{
	}
	//---------------------------------------------------------
	FastGuiWidget::~FastGuiWidget()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiWidget)
	OOBJECT_END
}