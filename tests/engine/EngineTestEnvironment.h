/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	EngineTestEnvironment.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EngineTestEnvironment_h__7_7_2026__12_00_00__
#define __EngineTestEnvironment_h__7_7_2026__12_00_00__

#include <core_module/OrkigePrerequisites.h>
#include <engine_module/EnginePrerequisites.h>
#include <core_util/Timer.h>
#include <core_event/GlobalEventManager.h>
#include <core_game/GameObjectManager.h>
#include <core_script/ScriptRuntime.h>

namespace Orkige
{
	//! @brief the engine-layer counterpart of tests/core's
	//! CoreTestEnvironment: boots the application singleton set headlessly
	//! and runs BOTH module init functions - no Ogre::Root, no window/GPU.
	//! The engine module init is pure registration (TypeManager, component
	//! factories, Lua usertypes), so it is safe without a booted renderer;
	//! it is exactly what ScriptComponent tests need (registered component
	//! factory + Lua bindings).
	class EngineTestEnvironment
	{
		//--- Variables ---------------------------------------
	public:
		GlobalEventManager	globalEventManager;	//!< global event singleton
		ScriptRuntime		scriptRuntime;		//!< scripting seam singleton
		GameObjectManager	gameObjectManager;	//!< game object singleton
		//--- Methods -----------------------------------------
	public:
		//! get (and on first call boot) the shared test environment
		static EngineTestEnvironment & get()
		{
			// Heap-allocated and intentionally never freed - the same pattern
			// (and the same reason) as CoreTestEnvironment::get(): a test that
			// runs an OrkigeMetaExport AFTER this environment exists (the
			// script-handle probe component) creates sol2's per-usertype name
			// statics LATER than the environment, so a by-value static here
			// would close the Lua state during C++ static destruction AFTER
			// those name strings are already destroyed - lua_close's usertype
			// teardown then reads a freed std::string (a heap-use-after-free
			// at process exit). Never destroying the environment skips that
			// exit-time close; the static pointer keeps the object reachable
			// so LeakSanitizer stays quiet. Production owns an ORDERED
			// teardown (AppHost); static-destruction-order teardown is
			// undefined-order behaviour no application hits. Do not "fix"
			// this back to a by-value static.
			static EngineTestEnvironment * environment =
				new EngineTestEnvironment();
			return *environment;
		}
	private:
		EngineTestEnvironment()
		{
			Timer::initialise();
			init_module_orkige_core();
			init_module_orkige_engine();
		}
	};
}

#endif //__EngineTestEnvironment_h__7_7_2026__12_00_00__
