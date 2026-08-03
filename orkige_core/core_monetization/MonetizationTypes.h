/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	MonetizationTypes.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __MonetizationTypes_h__3_8_2026__10_00_00__
#define __MonetizationTypes_h__3_8_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/SafeArea.h"
#include "core_util/String.h"

#include <functional>
#include <vector>

namespace Orkige
{
	/** \addtogroup Monetization
	*  @{ */

	//! @brief a submitted store or ad request's handle; 0 is the never-valid
	//! id (the shape HttpRequestId uses, for the same reason: a caller holds a
	//! token and is answered exactly once at a later frame boundary).
	typedef unsigned int MonetizationRequestId;

	//--- storefronts ---------------------------------------------------------

	//! @brief the storefront a product identifier belongs to - ONE COLUMN PER
	//! PLATFORM TARGET, because an app reaches exactly one store per platform
	//! it ships on and each of those stores issues its OWN identifier for the
	//! SAME logical product.
	//! @remarks This is why the catalog is a mapping and not a string: game
	//! code says `purchase("remove_ads")` for the whole life of the project,
	//! while the identifier that actually travels to a storefront is looked up
	//! per platform. Retrofitting that split later means touching every call
	//! site, every saved receipt and every analytics event, which is why it
	//! exists before the first provider does.
	//! @remarks A browser build has no platform store of its own; SF_WEB is the
	//! column for whatever payment surface the page embeds, so the catalog
	//! shape does not change when a game ships there.
	enum StorefrontId
	{
		SF_UNKNOWN = 0,	//!< no storefront (an unconfigured or simulated build)
		SF_IOS,			//!< the iOS store column
		SF_ANDROID,		//!< the Android store column
		SF_MACOS,		//!< the macOS store column
		SF_WINDOWS,		//!< the Windows store column
		SF_WEB,			//!< the browser payment surface column
		SF_SIMULATED	//!< the simulated provider's own column (development)
	};

	//! the stable token a storefront serializes and reports as ("ios", ...)
	ORKIGE_CORE_DLL String const & storefrontName(StorefrontId storefront);
	//! parse a storefront token; SF_UNKNOWN when it names none
	ORKIGE_CORE_DLL StorefrontId storefrontFromName(String const & name);

	//--- products ------------------------------------------------------------

	//! @brief what a product IS, which decides what owning it means.
	enum ProductKind
	{
		//! bought repeatedly and SPENT (a coin pack, a continue). Produces no
		//! lasting entitlement - the game grants the effect from the purchase
		//! callback and finishes the transaction.
		PK_CONSUMABLE = 0,
		//! bought ONCE and owned forever (remove ads, a level pack, the full
		//! version). Survives reinstall through restore().
		PK_NON_CONSUMABLE,
		//! renews and EXPIRES - an entitlement whose `active` is time-dependent
		//! and must be re-checked, never assumed from a past purchase.
		PK_SUBSCRIPTION
	};

	//! the stable token a product kind serializes as ("consumable", ...)
	ORKIGE_CORE_DLL String const & productKindName(ProductKind kind);
	//! parse a product-kind token; false when it names none
	ORKIGE_CORE_DLL bool productKindFromName(String const & name,
		ProductKind & outKind);

	//! @brief one entry of the product catalog: a LOGICAL product plus the
	//! per-storefront identifiers it is sold under, and the display metadata a
	//! storefront fills in once it has been asked.
	//! @remarks Prices are NEVER hard-coded: `displayPrice` is the storefront's
	//! own localised, currency-correct string and is the only price fit to show
	//! a player. Until a product query completes it is empty and `available` is
	//! false, so a store screen shows a placeholder rather than a wrong number.
	struct ORKIGE_CORE_DLL Product
	{
		//! the LOGICAL id game code uses forever ("remove_ads", "coins_500")
		String		id;
		//! what owning it means (@see ProductKind)
		ProductKind	kind = PK_CONSUMABLE;
		//! @brief owning this product suppresses ad serving. THE LINK between
		//! the two seams - "remove ads" is the most common purchase there is,
		//! so it is a catalog fact rather than something each game re-derives.
		bool		grantsNoAds = false;

		//--- filled by a completed product query (empty until then) ---
		//! the storefront's localised title
		String		title;
		//! the storefront's localised description
		String		description;
		//! @brief the storefront's own formatted price string ("4,99 EUR") -
		//! the ONLY price fit to display; formatting a raw number in the game
		//! gets the currency, the separators or the tax rules wrong somewhere
		String		displayPrice;
		//! ISO 4217 currency code the storefront quoted in ("EUR", "USD")
		String		priceCurrency;
		//! the numeric price, for analytics and sorting - NOT for display
		double		priceValue = 0.0;
		//! did the storefront actually return this product
		bool		available = false;
	};

	//--- purchases -----------------------------------------------------------

	//! @brief how a purchase attempt ended. Every one of these happens in the
	//! field; a game that branches only on "bought / did not buy" mishandles
	//! at least three of them.
	enum PurchaseState
	{
		//! the entitlement is granted now
		PS_PURCHASED = 0,
		//! the player dismissed the payment sheet - NOT an error, and must not
		//! raise one in the UI
		PS_CANCELLED,
		//! the store refused the payment (expired card, insufficient funds)
		PS_DECLINED,
		//! @brief DEFERRED: the purchase is neither done nor failed. Parental
		//! approval or an offline payment method can settle it minutes or days
		//! later, in a LATER SESSION, so the game must show a pending state and
		//! grant nothing yet.
		PS_PENDING,
		//! @brief the store says this account already owns it - a SUCCESS path
		//! for the player (they paid once), so the game grants the entitlement
		//! instead of showing an error
		PS_ALREADY_OWNED,
		//! the product is not in this storefront, or the store is unreachable
		PS_UNAVAILABLE,
		//! anything else; `reason` carries it
		PS_FAILED
	};

	//! the stable token a purchase state reports as ("cancelled", ...)
	ORKIGE_CORE_DLL String const & purchaseStateName(PurchaseState state);
	//! parse a purchase-state token; false when it names none
	ORKIGE_CORE_DLL bool purchaseStateFromName(String const & name,
		PurchaseState & outState);

	//! @brief what owning a product looks like once it is owned.
	//! @remarks THE PLATFORM IS THE SOURCE OF TRUTH. An entitlement is a CACHE
	//! of what a purchase or a restore reported; it is never persisted into the
	//! save store, because a save file does not survive a reinstall or move to
	//! the player's other device, and a file the player can edit is not a proof
	//! of payment. restore() is how entitlements come back.
	struct ORKIGE_CORE_DLL Entitlement
	{
		//! the LOGICAL product id (never a storefront identifier)
		String		productId;
		//! what kind of ownership this is
		ProductKind	kind = PK_NON_CONSUMABLE;
		//! @brief is it active RIGHT NOW - always true for a non-consumable,
		//! time-dependent for a subscription
		bool		active = true;
		//! subscription expiry as a Unix timestamp; 0 = never expires
		long long	expiryUnixSeconds = 0;
		//! the storefront's transaction handle ("" when it issued none)
		String		transactionId;
		//! @brief the storefront's opaque receipt / purchase token. Handed to a
		//! SERVER for validation - @see Docs/monetization.md on why an
		//! on-device check is not a proof.
		String		receipt;
	};

	//! @brief the answer to one purchase attempt.
	struct ORKIGE_CORE_DLL PurchaseResult
	{
		//! the request this answers
		MonetizationRequestId	id = 0;
		//! the LOGICAL product id that was asked for
		String					productId;
		//! how it ended
		PurchaseState			state = PS_FAILED;
		//! the storefront's transaction handle ("" when it issued none)
		String					transactionId;
		//! the opaque receipt / purchase token (server-side validation)
		String					receipt;
		//! @brief a one-line, human-readable reason - populated for EVERY
		//! non-purchase, so a game never has to invent an error message
		String					reason;

		//! did this attempt leave the player owning the product
		bool owned() const
		{
			return this->state == PS_PURCHASED || this->state == PS_ALREADY_OWNED;
		}
	};

	//! @brief the answer to a product-metadata query.
	struct ORKIGE_CORE_DLL ProductQueryResult
	{
		MonetizationRequestId	id = 0;
		//! did the storefront answer at all
		bool					completed = false;
		//! the products it knew about, with their metadata filled in
		std::vector<Product>	products;
		//! @brief logical ids the storefront did NOT know - almost always a
		//! console misconfiguration, and worth surfacing loudly in development
		StringVector			unknownProductIds;
		//! one-line reason when `completed` is false
		String					reason;
	};

	//! @brief the answer to a restore.
	//! @remarks An EMPTY list is a SUCCESSFUL restore, not a failure: a player
	//! who never bought anything restores nothing. `completed` is the field
	//! that says whether the store was reached.
	struct ORKIGE_CORE_DLL RestoreResult
	{
		MonetizationRequestId		id = 0;
		//! did the store answer at all
		bool						completed = false;
		//! everything this account owns
		std::vector<Entitlement>	entitlements;
		//! one-line reason when `completed` is false
		String						reason;
	};

	//--- consent -------------------------------------------------------------

	//! @brief whether the player has been ASKED about data use, and what they
	//! answered. CS_NOT_GATHERED is the start state and is NOT a synonym for
	//! "denied" - it means the question has not been put yet, which is the one
	//! state in which an ad provider must not come up at all.
	enum ConsentStatus
	{
		CS_NOT_GATHERED = 0,	//!< nothing asked yet - ads MUST NOT initialize
		CS_GRANTED,				//!< the player agreed to personalised advertising
		CS_DENIED,				//!< the player refused - contextual ads only
		CS_RESTRICTED			//!< an age/region regime allows no tracking at all
	};

	//! the stable token a consent status reports as ("not_gathered", ...)
	ORKIGE_CORE_DLL String const & consentStatusName(ConsentStatus status);
	//! parse a consent-status token; false when it names none
	ORKIGE_CORE_DLL bool consentStatusFromName(String const & name,
		ConsentStatus & outStatus);

	//! @brief the whole consent picture an ad provider needs BEFORE it starts.
	//! @remarks Three INDEPENDENT gates, not one flag, because they come from
	//! three different places and any one of them alone forbids personalisation:
	//! the privacy dialogue the player answered (`status`), the per-app
	//! tracking permission the operating system owns (`trackingAuthorized`) and
	//! whether the app is directed at children (`childDirected`), which is a
	//! property of the PRODUCT rather than of the player and is therefore set
	//! by the game, not asked.
	struct ORKIGE_CORE_DLL ConsentState
	{
		//! what the player answered (CS_NOT_GATHERED until they were asked)
		ConsentStatus	status = CS_NOT_GATHERED;
		//! did the operating system's own tracking permission come back granted
		bool			trackingAuthorized = false;
		//! is this app directed at children (never personalised, never tracked)
		bool			childDirected = false;

		//! @brief has the question been put at all - the ORDERING GATE an ad
		//! provider is not allowed to start before
		bool gathered() const { return this->status != CS_NOT_GATHERED; }

		//! @brief may ads be PERSONALISED? Every gate has to agree; anything
		//! less serves contextual ads, which is a legal outcome rather than an
		//! error.
		bool personalizedAds() const
		{
			return this->status == CS_GRANTED
				&& this->trackingAuthorized
				&& !this->childDirected;
		}
	};

	//--- ads -----------------------------------------------------------------

	//! @brief the ad shapes every mediation surface has in common.
	//! @remarks They share a vocabulary but NOT their consequences for the
	//! engine: a banner is a platform view that OCCUPIES SCREEN SPACE the game
	//! never renders into, while the other three are FULLSCREEN TAKEOVERS that
	//! stop the game. @see AdFormatIsTakeover, BannerGeometry.
	enum AdFormat
	{
		//! a persistent strip overlaid on the window - it eats layout space
		AF_BANNER = 0,
		//! a fullscreen ad between moments of play
		AF_INTERSTITIAL,
		//! a fullscreen ad the player OPTS INTO for a reward
		AF_REWARDED,
		//! a fullscreen ad on returning to the app
		AF_APP_OPEN
	};

	//! the stable token an ad format reports as ("banner", ...)
	ORKIGE_CORE_DLL String const & adFormatName(AdFormat format);
	//! parse an ad-format token; false when it names none
	ORKIGE_CORE_DLL bool adFormatFromName(String const & name, AdFormat & outFormat);

	//! @brief where one ad unit stands. Every transition between these is
	//! explicit in AdPlacement - there is no "probably ready" reading.
	enum AdState
	{
		AS_IDLE = 0,	//!< never loaded, or consumed by a show
		AS_LOADING,		//!< a load is in flight
		AS_READY,		//!< inventory is held and show() will work
		AS_SHOWING,		//!< on screen (a banner stays here until hidden)
		AS_FAILED		//!< the last load did not produce inventory
	};

	//! the stable token an ad state reports as ("ready", ...)
	ORKIGE_CORE_DLL String const & adStateName(AdState state);

	//! @brief how a load ended. NO FILL IS THE IMPORTANT ONE: the request was
	//! perfectly valid and the network simply had no advert to give, which is
	//! ordinary in low-traffic regions, is not an error, and must not be
	//! retried in a tight loop.
	enum AdLoadResult
	{
		ALR_LOADED = 0,			//!< inventory held, show() will work
		ALR_NO_FILL,			//!< no advert available - normal, try later
		ALR_ERROR,				//!< the network reported a failure
		ALR_TIMEOUT,			//!< no answer within the request's budget
		ALR_NOT_INITIALIZED,	//!< no ad provider, or it never came up
		ALR_CONSENT_MISSING,	//!< consent has not been gathered
		ALR_SUPPRESSED,			//!< a no-ads entitlement suppresses this format
		ALR_BUSY				//!< already loading, or already holding inventory
	};

	//! the stable token a load result reports as ("no_fill", ...)
	ORKIGE_CORE_DLL String const & adLoadResultName(AdLoadResult result);
	//! parse a load-result token; false when it names none
	ORKIGE_CORE_DLL bool adLoadResultFromName(String const & name,
		AdLoadResult & outResult);

	//! @brief how a show ended.
	//! @remarks ASR_DISMISSED and ASR_REWARD_EARNED are MUTUALLY EXCLUSIVE
	//! VALUES OF ONE ENUM on purpose. Mediation surfaces report "the ad closed"
	//! and "the reward was earned" as two separate signals, and a game that
	//! grants on close pays out for an advert nobody watched. Collapsing them
	//! into one outcome makes that mistake unrepresentable.
	enum AdShowResult
	{
		ASR_COMPLETED = 0,	//!< a non-rewarded unit finished, or a banner attached
		ASR_DISMISSED,		//!< the player closed it - rewarded: WITHOUT a reward
		ASR_REWARD_EARNED,	//!< rewarded only: the reward is due
		ASR_NOT_READY,		//!< show() before the unit was ready - an ERROR
		ASR_SUPPRESSED,		//!< a no-ads entitlement suppresses this format
		ASR_ERROR			//!< the network failed to present; `reason` carries it
	};

	//! the stable token a show result reports as ("reward_earned", ...)
	ORKIGE_CORE_DLL String const & adShowResultName(AdShowResult result);
	//! parse a show-result token; false when it names none
	ORKIGE_CORE_DLL bool adShowResultFromName(String const & name,
		AdShowResult & outResult);

	//! @brief is this format a fullscreen takeover (it stops the game) rather
	//! than an overlay (it takes layout space)?
	ORKIGE_CORE_DLL bool adFormatIsTakeover(AdFormat format);

	//! @brief the answer to one load.
	struct ORKIGE_CORE_DLL AdLoadOutcome
	{
		MonetizationRequestId	id = 0;
		AdFormat				format = AF_BANNER;
		//! the placement name the game asked for ("" = the format's default)
		String					placement;
		AdLoadResult			result = ALR_ERROR;
		//! one-line reason, populated for every non-ALR_LOADED result
		String					reason;

		//! did this load leave inventory ready to show
		bool ready() const { return this->result == ALR_LOADED; }
	};

	//! @brief the answer to one show, including the reward branch.
	struct ORKIGE_CORE_DLL AdShowOutcome
	{
		MonetizationRequestId	id = 0;
		AdFormat				format = AF_BANNER;
		String					placement;
		AdShowResult			result = ASR_ERROR;
		//! @brief which reward is due - rewarded format, ASR_REWARD_EARNED only
		String					rewardId;
		//! @brief how much of it - rewarded format, ASR_REWARD_EARNED only.
		//! ALWAYS 0 on every other result, so a game cannot read an amount out
		//! of a dismissal.
		double					rewardAmount = 0.0;
		//! one-line reason, populated for every error result
		String					reason;

		//! @brief THE branch a rewarded ad exists for. Grant on this and
		//! nothing else.
		bool rewardEarned() const { return this->result == ASR_REWARD_EARNED; }
	};

	//! where a banner sits against the window
	enum BannerPosition
	{
		BP_TOP = 0,
		BP_BOTTOM
	};

	//! the stable token a banner position reports as ("top"/"bottom")
	ORKIGE_CORE_DLL String const & bannerPositionName(BannerPosition position);
	//! parse a banner-position token; false when it names none
	ORKIGE_CORE_DLL bool bannerPositionFromName(String const & name,
		BannerPosition & outPosition);

	//! @brief the screen a banner occupies, IN WINDOW PIXELS.
	//!
	//! THE REASON THIS TYPE EXISTS: a banner is a platform view laid over the
	//! window. The engine never renders into that strip and never learns about
	//! it from the render target, so a HUD anchored to the bottom of the screen
	//! sits UNDERNEATH the advert - and, because no advert exists in
	//! development, the fault only appears on a device with live inventory,
	//! usually after release. Reporting the geometry through the seam lets a
	//! game lay out against the strip whether or not an advert was ever served.
	struct ORKIGE_CORE_DLL BannerGeometry
	{
		//! is a banner actually on screen right now
		bool			visible = false;
		//! which edge it hugs
		BannerPosition	position = BP_BOTTOM;
		//! its extent in window pixels (0 when nothing is shown)
		unsigned int	width = 0;
		unsigned int	height = 0;
		//! @brief does the strip sit INSIDE the display's safe area (below the
		//! notch, above the home indicator)? True is the normal placement, and
		//! makes the banner's height ADD to the safe-area inset. False means an
		//! edge-to-edge banner that overlaps the unsafe strip instead, where
		//! only the part beyond the existing inset takes new space.
		bool			insideSafeArea = true;

		//! @brief compose this banner with the display's own safe-area insets
		//! into the ONE inset set UI has to lay out inside. An invisible or
		//! zero-height banner returns @p display unchanged, so a build with no
		//! advertising lays out exactly as it did before.
		SafeAreaInsets composeWith(SafeAreaInsets const & display) const;
	};

	//! @brief which formats a no-ads entitlement actually silences.
	//! @remarks REWARDED IS NOT SUPPRESSED BY DEFAULT, and that is a deliberate
	//! product decision rather than an oversight: a rewarded advert is one the
	//! player chooses to watch in exchange for something, so silencing it for a
	//! paying player removes a game mechanic they still want. The three
	//! INTERRUPTIVE formats - the ones a player paid to be rid of - are the
	//! ones that go quiet. A game that disagrees flips the field.
	struct ORKIGE_CORE_DLL AdPolicy
	{
		bool suppressBanner = true;			//!< silence banners when ad-free
		bool suppressInterstitial = true;	//!< silence interstitials when ad-free
		bool suppressAppOpen = true;		//!< silence app-open ads when ad-free
		bool suppressRewarded = false;		//!< keep opt-in rewarded ads (@see above)

		//! is @p format silenced for a player who owns a no-ads product
		bool suppresses(AdFormat format) const;
	};

	//--- callbacks -----------------------------------------------------------
	// Every one of these is invoked EXACTLY ONCE per request, on the main
	// thread, from MonetizationService::update() - the discipline HttpClient
	// established, so a store or advert callback can never land in the middle
	// of a world update.

	//! product-metadata query completion
	typedef std::function<void(ProductQueryResult const &)> ProductQueryCallback;
	//! purchase completion (every PurchaseState arrives through here)
	typedef std::function<void(PurchaseResult const &)> PurchaseCallback;
	//! restore completion
	typedef std::function<void(RestoreResult const &)> RestoreCallback;
	//! ad load completion
	typedef std::function<void(AdLoadOutcome const &)> AdLoadCallback;
	//! ad show completion (the reward branch arrives through here)
	typedef std::function<void(AdShowOutcome const &)> AdShowCallback;

	/** @} */
}

#endif //__MonetizationTypes_h__3_8_2026__10_00_00__
