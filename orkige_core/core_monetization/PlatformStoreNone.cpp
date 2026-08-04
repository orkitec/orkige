/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	PlatformStoreNone.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The platform-store answer everywhere the engine does not (yet) speak to a
// storefront: an HONEST ABSENCE. It is a whole translation unit rather than a
// null pointer at the call site because the ABSENCE has to be nameable - a game
// asking why it cannot sell anything deserves "this platform has no store
// integration", not silence, and a test deserves something to assert on.

#include "core_monetization/PlatformStore.h"

#ifdef ORKIGE_STORE_NONE

namespace Orkige
{
	//---------------------------------------------------------
	bool platformStoreAvailable()
	{
		return false;
	}
	//---------------------------------------------------------
	String platformStoreUnavailableReason()
	{
		return "this platform has no store integration in this build - install "
			"a store provider, or run against the simulated one";
	}
	//---------------------------------------------------------
	StorefrontId platformStorefront()
	{
		return SF_UNKNOWN;
	}
	//---------------------------------------------------------
	StoreProvider * createPlatformStoreProvider()
	{
		return NULL;
	}
}

#endif //ORKIGE_STORE_NONE
