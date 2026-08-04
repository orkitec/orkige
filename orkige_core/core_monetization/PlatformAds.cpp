/**************************************************************
	created:	2026/08/04 at 15:00
	filename: 	PlatformAds.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The whole platform half of the AD seam, which is one translation unit rather
// than the store side's per-platform pair for a reason worth stating: there is
// no built-in ad network on ANY platform, so there is nothing to choose
// between. The engine MEDIATES - it sits in front of whatever surface a game
// installs - and a network integration arrives as a provider a project brings
// with it, through the ordinary AdProvider interface.
//
// The absence is a named answer rather than a null pointer at the call site,
// for the same reason PlatformStoreNone.cpp is a whole file: a game asking why
// no advert ever loads deserves a prerequisite it can act on, and a test
// deserves something to assert on.

#include "core_monetization/PlatformAds.h"

namespace Orkige
{
	char const * const ADS_PROVIDER_SETTING_KEY = "ads.provider";

	namespace
	{
		//! the answer a lookup that names nothing falls back to
		const String EMPTY_NAME = "";

		//! the token table (row index == enum value)
		String const * adProviderChoiceNames()
		{
			static const String NAMES[] =
			{
				"platform", "simulated", "none"
			};
			return NAMES;
		}
	}
	//---------------------------------------------------------
	String const & adProviderChoiceName(AdProviderChoice choice)
	{
		const int index = static_cast<int>(choice);
		if(index < 0 || index > static_cast<int>(APC_NONE))
		{
			return EMPTY_NAME;
		}
		return adProviderChoiceNames()[index];
	}
	//---------------------------------------------------------
	bool adProviderChoiceFromName(String const & name,
		AdProviderChoice & outChoice)
	{
		// an unset setting is the SAFE answer, not an error: a project that
		// never thought about advertising gets the platform's own surface -
		// which today is an honest absence - and never the simulator
		if(name.empty())
		{
			outChoice = APC_PLATFORM;
			return true;
		}
		for(int index = 0; index <= static_cast<int>(APC_NONE); ++index)
		{
			if(adProviderChoiceNames()[index] != name) { continue; }
			outChoice = static_cast<AdProviderChoice>(index);
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	bool platformAdsAvailable()
	{
		return false;
	}
	//---------------------------------------------------------
	String platformAdsUnavailableReason()
	{
		return "this build carries no advertising provider - install one, or "
			"run against the simulated surface";
	}
	//---------------------------------------------------------
	AdProvider * createPlatformAdProvider()
	{
		return NULL;
	}
}
