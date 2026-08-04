/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	PlatformStore.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The platform-NEUTRAL half of the store seam: which provider a project asked
// for. Compiled everywhere, unlike the per-platform translation units beside it.

#include "core_monetization/PlatformStore.h"

namespace Orkige
{
	char const * const STORE_PROVIDER_SETTING_KEY = "store.provider";

	namespace
	{
		//! the answer a lookup that names nothing falls back to
		const String EMPTY_NAME = "";

		//! the token table (row index == enum value)
		String const * providerChoiceNames()
		{
			static const String NAMES[] =
			{
				"platform", "simulated", "none"
			};
			return NAMES;
		}
	}
	//---------------------------------------------------------
	String const & storeProviderChoiceName(StoreProviderChoice choice)
	{
		const int index = static_cast<int>(choice);
		if(index < 0 || index > static_cast<int>(SPC_NONE))
		{
			return EMPTY_NAME;
		}
		return providerChoiceNames()[index];
	}
	//---------------------------------------------------------
	bool storeProviderChoiceFromName(String const & name,
		StoreProviderChoice & outChoice)
	{
		// an unset setting is the SAFE answer, not an error: a project that
		// never thought about monetization gets the platform's own store (or
		// the honest absence), never the simulator
		if(name.empty())
		{
			outChoice = SPC_PLATFORM;
			return true;
		}
		for(int index = 0; index <= static_cast<int>(SPC_NONE); ++index)
		{
			if(providerChoiceNames()[index] != name) { continue; }
			outChoice = static_cast<StoreProviderChoice>(index);
			return true;
		}
		return false;
	}
}
