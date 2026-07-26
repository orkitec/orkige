/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	TestRunListener.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Registers the shared run listener (see tests/OrkigeTestListener.h): clears
// the game-object world before static teardown to prevent the exit-time
// destruction-order segfault.
#include "../OrkigeTestListener.h"
CATCH_REGISTER_LISTENER(Orkige::OrkigeTestRunListener)
