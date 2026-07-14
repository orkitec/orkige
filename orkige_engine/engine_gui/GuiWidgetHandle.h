/********************************************************************
	created:	Tuesday 2026/07/15
	filename: 	GuiWidgetHandle.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __GuiWidgetHandle_h__15_7_2026__
#define __GuiWidgetHandle_h__15_7_2026__

//! Weak Lua widget handles (option C): every GuiWidget descendant returned by an
//! OFUNCWEAK accessor (the finders + factory create*) hands Lua the ONE shared
//! WidgetHandle - a weak proxy over the base GuiWidget that locks per method call
//! and dynamic_casts to the leaf. This header emits the family -> handle-base
//! mapping for the whole widget subtree; include it in every TU that registers a
//! widget-returning OFUNCWEAK accessor (GuiManager.cpp, engine_module/module.cpp)
//! so the mapping is visible - and consistent - at each registration. The
//! WidgetHandle usertype itself (the method surface) is built in module.cpp.

#include <type_traits>

#include "core_base/Meta.h"
#include "engine_gui/GuiWidget.h"

OHANDLE_BASE(Orkige::GuiWidget)

#endif
