/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	MonetizationTypes.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_monetization/MonetizationTypes.h"

#include <cstddef>

namespace Orkige
{
	namespace
	{
		//! the answer every lookup that names nothing falls back to
		const String EMPTY_NAME = "";

		//--- the token tables (row index == enum value) ---
		// Every enum in this module starts at 0 and is contiguous, so the enum
		// value IS the row index and a lookup needs no search.
		String const * storefrontNames()
		{
			static const String NAMES[] =
			{
				"unknown", "ios", "android", "macos", "windows", "web",
				"simulated"
			};
			return NAMES;
		}
		String const * productKindNames()
		{
			static const String NAMES[] =
			{
				"consumable", "non_consumable", "subscription"
			};
			return NAMES;
		}
		String const * purchaseStateNames()
		{
			static const String NAMES[] =
			{
				"purchased", "cancelled", "declined", "pending",
				"already_owned", "unavailable", "failed"
			};
			return NAMES;
		}
		String const * consentStatusNames()
		{
			static const String NAMES[] =
			{
				"not_gathered", "granted", "denied", "restricted"
			};
			return NAMES;
		}
		String const * adFormatNames()
		{
			static const String NAMES[] =
			{
				"banner", "interstitial", "rewarded", "app_open"
			};
			return NAMES;
		}
		String const * adStateNames()
		{
			static const String NAMES[] =
			{
				"idle", "loading", "ready", "showing", "failed"
			};
			return NAMES;
		}
		String const * adLoadResultNames()
		{
			static const String NAMES[] =
			{
				"loaded", "no_fill", "error", "timeout", "not_initialized",
				"consent_missing", "suppressed", "busy"
			};
			return NAMES;
		}
		String const * adShowResultNames()
		{
			static const String NAMES[] =
			{
				"completed", "dismissed", "reward_earned", "not_ready",
				"suppressed", "error"
			};
			return NAMES;
		}
		String const * bannerPositionNames()
		{
			static const String NAMES[] = { "top", "bottom" };
			return NAMES;
		}

		//! how many rows each table carries (kept beside the tables above)
		const std::size_t STOREFRONT_COUNT		= 7;
		const std::size_t PRODUCT_KIND_COUNT	= 3;
		const std::size_t PURCHASE_STATE_COUNT	= 7;
		const std::size_t CONSENT_STATUS_COUNT	= 4;
		const std::size_t AD_FORMAT_COUNT		= 4;
		const std::size_t AD_STATE_COUNT		= 5;
		const std::size_t AD_LOAD_COUNT			= 8;
		const std::size_t AD_SHOW_COUNT			= 6;
		const std::size_t BANNER_POSITION_COUNT	= 2;

		//! the token at @p value, or "" when the table has no such row
		String const & nameIn(String const * names, std::size_t count, int value)
		{
			if(value < 0 || static_cast<std::size_t>(value) >= count)
			{
				return EMPTY_NAME;
			}
			return names[static_cast<std::size_t>(value)];
		}

		//! the enum value whose token is @p token; false when there is none
		bool valueIn(String const * names, std::size_t count,
			String const & token, int & outValue)
		{
			for(std::size_t i = 0; i < count; ++i)
			{
				if(names[i] != token) { continue; }
				outValue = static_cast<int>(i);
				return true;
			}
			return false;
		}
	}
	//---------------------------------------------------------
	String const & storefrontName(StorefrontId storefront)
	{
		return nameIn(storefrontNames(), STOREFRONT_COUNT,
			static_cast<int>(storefront));
	}
	//---------------------------------------------------------
	StorefrontId storefrontFromName(String const & name)
	{
		int value = SF_UNKNOWN;
		if(!valueIn(storefrontNames(), STOREFRONT_COUNT, name, value))
		{
			return SF_UNKNOWN;
		}
		return static_cast<StorefrontId>(value);
	}
	//---------------------------------------------------------
	String const & productKindName(ProductKind kind)
	{
		return nameIn(productKindNames(), PRODUCT_KIND_COUNT,
			static_cast<int>(kind));
	}
	//---------------------------------------------------------
	bool productKindFromName(String const & name, ProductKind & outKind)
	{
		int value = 0;
		if(!valueIn(productKindNames(), PRODUCT_KIND_COUNT, name, value))
		{
			return false;
		}
		outKind = static_cast<ProductKind>(value);
		return true;
	}
	//---------------------------------------------------------
	String const & purchaseStateName(PurchaseState state)
	{
		return nameIn(purchaseStateNames(), PURCHASE_STATE_COUNT,
			static_cast<int>(state));
	}
	//---------------------------------------------------------
	bool purchaseStateFromName(String const & name, PurchaseState & outState)
	{
		int value = 0;
		if(!valueIn(purchaseStateNames(), PURCHASE_STATE_COUNT, name, value))
		{
			return false;
		}
		outState = static_cast<PurchaseState>(value);
		return true;
	}
	//---------------------------------------------------------
	String const & consentStatusName(ConsentStatus status)
	{
		return nameIn(consentStatusNames(), CONSENT_STATUS_COUNT,
			static_cast<int>(status));
	}
	//---------------------------------------------------------
	bool consentStatusFromName(String const & name, ConsentStatus & outStatus)
	{
		int value = 0;
		if(!valueIn(consentStatusNames(), CONSENT_STATUS_COUNT, name, value))
		{
			return false;
		}
		outStatus = static_cast<ConsentStatus>(value);
		return true;
	}
	//---------------------------------------------------------
	String const & adFormatName(AdFormat format)
	{
		return nameIn(adFormatNames(), AD_FORMAT_COUNT,
			static_cast<int>(format));
	}
	//---------------------------------------------------------
	bool adFormatFromName(String const & name, AdFormat & outFormat)
	{
		int value = 0;
		if(!valueIn(adFormatNames(), AD_FORMAT_COUNT, name, value))
		{
			return false;
		}
		outFormat = static_cast<AdFormat>(value);
		return true;
	}
	//---------------------------------------------------------
	String const & adStateName(AdState state)
	{
		return nameIn(adStateNames(), AD_STATE_COUNT, static_cast<int>(state));
	}
	//---------------------------------------------------------
	String const & adLoadResultName(AdLoadResult result)
	{
		return nameIn(adLoadResultNames(), AD_LOAD_COUNT,
			static_cast<int>(result));
	}
	//---------------------------------------------------------
	bool adLoadResultFromName(String const & name, AdLoadResult & outResult)
	{
		int value = 0;
		if(!valueIn(adLoadResultNames(), AD_LOAD_COUNT, name, value))
		{
			return false;
		}
		outResult = static_cast<AdLoadResult>(value);
		return true;
	}
	//---------------------------------------------------------
	String const & adShowResultName(AdShowResult result)
	{
		return nameIn(adShowResultNames(), AD_SHOW_COUNT,
			static_cast<int>(result));
	}
	//---------------------------------------------------------
	bool adShowResultFromName(String const & name, AdShowResult & outResult)
	{
		int value = 0;
		if(!valueIn(adShowResultNames(), AD_SHOW_COUNT, name, value))
		{
			return false;
		}
		outResult = static_cast<AdShowResult>(value);
		return true;
	}
	//---------------------------------------------------------
	String const & bannerPositionName(BannerPosition position)
	{
		return nameIn(bannerPositionNames(), BANNER_POSITION_COUNT,
			static_cast<int>(position));
	}
	//---------------------------------------------------------
	bool bannerPositionFromName(String const & name, BannerPosition & outPosition)
	{
		int value = 0;
		if(!valueIn(bannerPositionNames(), BANNER_POSITION_COUNT, name, value))
		{
			return false;
		}
		outPosition = static_cast<BannerPosition>(value);
		return true;
	}
	//---------------------------------------------------------
	bool adFormatIsTakeover(AdFormat format)
	{
		// a banner is an overlay that costs layout space; the other three stop
		// the game while they are up
		return format != AF_BANNER;
	}
	//---------------------------------------------------------
	SafeAreaInsets BannerGeometry::composeWith(SafeAreaInsets const & display) const
	{
		SafeAreaInsets composed = display;
		if(!this->visible || this->height == 0)
		{
			// nothing on screen: a build that serves no advertising lays out
			// exactly as it would with no monetization at all
			return composed;
		}

		unsigned int & edge = (this->position == BP_TOP)
			? composed.mTop
			: composed.mBottom;

		if(this->insideSafeArea)
		{
			// the normal placement: the strip is parked against the safe edge,
			// so its whole height is space the UI additionally cannot use
			edge += this->height;
		}
		else
		{
			// edge-to-edge: the strip starts at the physical window edge and
			// already covers the display's own inset, so only the part that
			// reaches beyond it takes NEW space
			if(this->height > edge) { edge = this->height; }
		}
		return composed;
	}
	//---------------------------------------------------------
	bool AdPolicy::suppresses(AdFormat format) const
	{
		switch(format)
		{
		case AF_BANNER:			return this->suppressBanner;
		case AF_INTERSTITIAL:	return this->suppressInterstitial;
		case AF_REWARDED:		return this->suppressRewarded;
		case AF_APP_OPEN:		return this->suppressAppOpen;
		}
		return false;
	}
}
