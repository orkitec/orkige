/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	PlatformStore.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PlatformStore_h__4_8_2026__10_00_00__
#define __PlatformStore_h__4_8_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_monetization/MonetizationProvider.h"
#include "core_monetization/MonetizationTypes.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Monetization
	*  @{ */

	//! @brief THE PLATFORM'S OWN PURCHASE SURFACE, behind the ordinary
	//! StoreProvider seam - exactly one implementation is compiled per platform
	//! (the CMake picks the translation unit, the HttpBackend split), so no call
	//! site above this carries a platform #ifdef:
	//!
	//!   PlatformStoreApple.mm   macOS + iOS   the system in-app purchase
	//!                                         framework (StoreKit)
	//!   PlatformStoreNone.cpp   everywhere    an honest absence, named
	//!                           else
	//!
	//! A PLATFORM WITH NO STORE IS NOT AN ERROR. Most development happens on
	//! one: a desktop build has no store account, a browser build has no
	//! platform store at all, and a device build that was never signed cannot
	//! reach one either. Every one of those answers false below WITH A REASON,
	//! and the game's purchase calls are then refused by name through the
	//! ordinary callback rather than hanging on a surface that is not there.

	//! @brief which store a project wants behind the seam.
	//!
	//! THIS EXISTS TO MAKE ONE MISTAKE UNREPRESENTABLE: a shipped game running
	//! against the SIMULATED store would hand every product out for free, and
	//! nothing about it would look wrong from inside the game. So the simulator
	//! is never a fallback that a missing platform store silently decays into -
	//! it has to be ASKED for, in the project manifest, by name.
	enum StoreProviderChoice
	{
		//! the platform's own store, or an honest absence where there is none
		SPC_PLATFORM = 0,
		//! the simulated store (development) - never reached by accident
		SPC_SIMULATED,
		//! no store at all: every purchase is refused, nothing is installed
		SPC_NONE
	};

	//! the stable token a provider choice is written as ("platform", ...)
	ORKIGE_CORE_DLL String const & storeProviderChoiceName(
		StoreProviderChoice choice);

	//! @brief read the manifest's `store.provider` value.
	//! @param name the setting's value; an EMPTY value means SPC_PLATFORM, so a
	//! project that never thought about it gets the safe answer
	//! @return false when @p name names no choice (the caller keeps
	//! SPC_PLATFORM and says what it did not understand)
	ORKIGE_CORE_DLL bool storeProviderChoiceFromName(String const & name,
		StoreProviderChoice & outChoice);

	//! the manifest Settings key naming the choice above ("store.provider")
	ORKIGE_CORE_DLL extern char const * const STORE_PROVIDER_SETTING_KEY;

	//! @brief is a real platform store compiled into this build AND usable on
	//! the machine it is running on?
	//! @remarks This is a RUNTIME question, not just a build-time one: a signed
	//! device with payments disabled by a parental restriction answers false
	//! here, and so does an unsandboxed desktop binary.
	ORKIGE_CORE_DLL bool platformStoreAvailable();

	//! @brief why there is no usable platform store ("" when there is one) -
	//! phrased for a developer, because that is who reads it: it names the
	//! missing prerequisite rather than saying the purchase failed.
	ORKIGE_CORE_DLL String platformStoreUnavailableReason();

	//! @brief which catalog column the platform store reads, or SF_UNKNOWN when
	//! this build has none. @see ProductCatalog
	ORKIGE_CORE_DLL StorefrontId platformStorefront();

	//! @brief create the platform's store provider, or NULL when this build has
	//! none. DEFINED ONCE per platform in the translation unit the build
	//! selected; the caller owns the returned object.
	//! @remarks It is created even when platformStoreAvailable() is false, so
	//! that initialize() can refuse with the reason above through the ordinary
	//! seam. A caller that wants no store at all installs no provider.
	ORKIGE_CORE_DLL StoreProvider * createPlatformStoreProvider();

	/** @} */
}

#endif //__PlatformStore_h__4_8_2026__10_00_00__
