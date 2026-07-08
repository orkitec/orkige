/**************************************************************
	created:	2026/07/07 at 14:00
	filename: 	CoreTestEnvironment.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __CoreTestEnvironment_h__7_7_2026__14_00_00__
#define __CoreTestEnvironment_h__7_7_2026__14_00_00__

#include <core_module/OrkigePrerequisites.h>
#include <core_util/Timer.h>
#include <core_event/GlobalEventManager.h>
#include <core_game/GameObjectManager.h>
#include <core_script/ScriptRuntime.h>

namespace Orkige
{
	//! @brief boots the singleton set an Orkige application normally provides
	//! (mirrors samples/hello_orkige and tools/player) once per test process,
	//! completely headless. Every test case calls CoreTestEnvironment::get()
	//! first; the singletons stay alive for the rest of the process.
	//! @remarks the ScriptRuntime member is constructed BEFORE the ctor body
	//! runs init_module_orkige_core(), so the OrkigeMetaExport macros target
	//! the real backend state (without it they would still work against the
	//! metaExportState() fallback - TypeManager and component factory
	//! registration do not depend on the scripting state - but the
	//! script-facing tests need the real one).
	class CoreTestEnvironment
	{
		//--- Variables ---------------------------------------
	public:
		GlobalEventManager	globalEventManager;	//!< global event singleton
		ScriptRuntime		scriptRuntime;		//!< scripting seam singleton
		GameObjectManager	gameObjectManager;	//!< game object singleton
		//--- Methods -----------------------------------------
	public:
		//! get (and on first call boot) the shared test environment
		static CoreTestEnvironment & get()
		{
			static CoreTestEnvironment environment;
			return environment;
		}
	private:
		CoreTestEnvironment()
		{
			Timer::initialise();
			init_module_orkige_core();
		}
	};
}

#endif //__CoreTestEnvironment_h__7_7_2026__14_00_00__
