/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiView.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiView.h"
#include "engine_fastgui/FastGuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	//---------------------------------------------------------------
	FastGuiView::FastGuiView(Gorilla::Screen* _screen) : screen(_screen) 
	{

	}
	//---------------------------------------------------------
	FastGuiView::~FastGuiView()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiView)
	OOBJECT_END
}