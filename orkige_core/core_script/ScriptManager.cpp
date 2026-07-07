/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	ScriptManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptManager.h"

namespace Orkige
{
	IMPL_OSINGLETON(ScriptManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ScriptManager::ScriptManager()
	{
		this->luaState.open_libraries(
			sol::lib::base,
			sol::lib::package,
			sol::lib::string,
			sol::lib::table,
			sol::lib::math);
	}
	//---------------------------------------------------------
	ScriptManager::~ScriptManager()
	{
	}
	//---------------------------------------------------------
	sol::state & ScriptManager::metaExportState()
	{
		if (ScriptManager::getSingletonPtr() != NULL)
		{
			return ScriptManager::getSingleton().state();
		}
		// no scripting booted: keep meta export working against a private
		// state that is simply never used for script execution
		static sol::state fallbackState;
		return fallbackState;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
