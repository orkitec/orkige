/**************************************************************
	created:	2026/07/08
	filename: 	OrkigeTestListener.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __OrkigeTestListener_h__8_7_2026__
#define __OrkigeTestListener_h__8_7_2026__

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <core_game/GameObjectManager.h>

namespace Orkige
{
	//! @brief clears the game-object world at the END of the test run, while
	//! every static (TypeInfo instances especially) is still alive.
	//!
	//! Without this, leftover GameObjects are destroyed during C++ static
	//! teardown by the test environment's ~GameObjectManager - in unspecified
	//! order relative to the function-local TypeInfo statics that component
	//! removal consults (TypeInfo::operator< on destroyed statics =
	//! intermittent exit-time segfault, seen under parallel ctest load).
	//!
	//! Every test executable that boots a *TestEnvironment must compile ONE
	//! translation unit registering this listener:
	//!   #include "OrkigeTestListener.h"
	//!   CATCH_REGISTER_LISTENER(Orkige::OrkigeTestRunListener)
	class OrkigeTestRunListener : public Catch::EventListenerBase
	{
	public:
		using Catch::EventListenerBase::EventListenerBase;

		void testRunEnded(Catch::TestRunStats const &) override
		{
			if (GameObjectManager::getSingletonPtr() != NULL)
			{
				GameObjectManager::getSingletonPtr()->clear();
			}
		}
	};
}

#endif //__OrkigeTestListener_h__8_7_2026__
