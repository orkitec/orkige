/**************************************************************
	created:	2026/08/04 at 15:00
	filename: 	PlatformAds.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PlatformAds_h__4_8_2026__15_00_00__
#define __PlatformAds_h__4_8_2026__15_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_monetization/MonetizationProvider.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Monetization
	*  @{ */

	//! @brief WHICH ADVERTISING SURFACE stands behind the AdProvider seam, and
	//! the honest answer that there is no built-in one.
	//!
	//! THE ENGINE MEDIATES, IT DOES NOT ADVERTISE. There is no ad network here
	//! and there is not meant to be one: the seam sits IN FRONT of whatever
	//! surface a game chooses, so the whole tier - placements, consent, the
	//! no-ads link, the banner's screen cost - is engine behaviour that a real
	//! integration plugs into rather than reimplements. A network integration
	//! is a provider a project installs; it arrives as a library the project
	//! depends on (@see Docs/android-libraries.md for the packaging side) and
	//! goes in through the SAME AdProvider interface the simulated one uses.
	//!
	//! So this file is deliberately the store's shape with one row missing:
	//!
	//!   the simulated provider   everywhere   deterministic, asked for by name
	//!   a platform provider      nowhere      an honest absence, named
	//!
	//! A BUILD WITH NO AD PROVIDER IS NOT AN ERROR - it is the normal state of
	//! every development machine and of every game that does not advertise.
	//! createPlatformAdProvider() answers NULL and the reason below says so, so
	//! a developer reads a named prerequisite instead of watching every load
	//! answer ALR_ERROR for no stated cause.

	//! @brief which advertising surface a project wants behind the seam.
	//!
	//! THIS EXISTS TO MAKE ONE MISTAKE UNREPRESENTABLE, the same one the store
	//! side guards: a shipped game running against the SIMULATED ad surface
	//! would serve a fake advert, report a reward nobody watched an advert for,
	//! and look entirely correct doing it. So the simulator is never a fallback
	//! that a missing platform surface decays into - it has to be ASKED for, in
	//! the project manifest, by name.
	enum AdProviderChoice
	{
		//! the platform's own advertising surface, or an honest absence where
		//! this build has none (which is every build today)
		APC_PLATFORM = 0,
		//! the simulated ad surface (development) - never reached by accident
		APC_SIMULATED,
		//! no advertising at all: nothing is installed, every load is refused
		APC_NONE
	};

	//! the stable token a provider choice is written as ("platform", ...)
	ORKIGE_CORE_DLL String const & adProviderChoiceName(AdProviderChoice choice);

	//! @brief read the manifest's `ads.provider` value.
	//! @param name the setting's value; an EMPTY value means APC_PLATFORM, so a
	//! project that never thought about advertising gets the safe answer - the
	//! platform's surface where one exists, and nothing at all where none does
	//! @return false when @p name names no choice (the caller keeps
	//! APC_PLATFORM and says what it did not understand)
	ORKIGE_CORE_DLL bool adProviderChoiceFromName(String const & name,
		AdProviderChoice & outChoice);

	//! @brief the manifest Settings key naming the choice above
	//! ("ads.provider").
	//! @remarks There is deliberately no companion key for TEST MODE: it is an
	//! argument to initializeAds, and a second place to say it could disagree
	//! with the first. @see MonetizationService::initializeAds
	ORKIGE_CORE_DLL extern char const * const ADS_PROVIDER_SETTING_KEY;

	//! @brief is a real advertising surface compiled into this build AND usable
	//! on the machine it is running on?
	//! @remarks A RUNTIME question, like its store twin: a build that carries a
	//! provider can still answer false on a device that has no advertising
	//! identifier available to it.
	ORKIGE_CORE_DLL bool platformAdsAvailable();

	//! @brief why there is no usable advertising surface ("" when there is
	//! one) - phrased for a developer, because that is who reads it: it names
	//! the missing prerequisite rather than saying an advert failed to load.
	ORKIGE_CORE_DLL String platformAdsUnavailableReason();

	//! @brief create the platform's ad provider, or NULL when this build has
	//! none. The caller owns the returned object.
	//! @remarks The one seam a real network integration is installed at. It is
	//! declared here, rather than left to each host, so that adding a provider
	//! changes exactly one translation unit and no call site above it.
	ORKIGE_CORE_DLL AdProvider * createPlatformAdProvider();

	/** @} */
}

#endif //__PlatformAds_h__4_8_2026__15_00_00__
