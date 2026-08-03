/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	MonetizationProvider.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __MonetizationProvider_h__3_8_2026__10_00_00__
#define __MonetizationProvider_h__3_8_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_monetization/MonetizationTypes.h"
#include "core_util/String.h"

#include <vector>

namespace Orkige
{
	/** \addtogroup Monetization
	*  @{ */

	//! @brief one thing a store finished, handed to the main thread.
	//! @remarks A provider NEVER calls into game code. It publishes events, and
	//! MonetizationService::update() turns them into the caller's ONE callback
	//! at a frame boundary - the discipline HttpBackend follows for exactly the
	//! same reason (a platform payment sheet answers on its own queue, and a
	//! world update must not run inside that answer).
	struct ORKIGE_CORE_DLL StoreEvent
	{
		//! which shape of answer this is
		enum Kind
		{
			SE_PRODUCTS = 0,	//!< a product query settled
			SE_PURCHASE,		//!< a purchase attempt settled
			SE_RESTORE			//!< a restore settled
		};

		//! which shape of answer this is
		Kind					kind = SE_PRODUCTS;
		//! @brief the request this answers, or 0 for an UNSOLICITED event -
		//! a deferred purchase settling in a later session arrives with no
		//! request to correlate against, which is why this can be 0
		MonetizationRequestId	id = 0;

		//--- SE_PRODUCTS ---
		//! the products the storefront returned, keyed by STOREFRONT identifier
		std::vector<Product>	products;
		//! storefront identifiers the store did not know
		StringVector			unknownStoreIds;

		//--- SE_PURCHASE ---
		//! the STOREFRONT identifier that was bought (never the logical id)
		String					storeId;
		//! how the attempt ended
		PurchaseState			purchaseState = PS_FAILED;
		//! the storefront's transaction handle
		String					transactionId;
		//! the opaque receipt / purchase token
		String					receipt;

		//--- SE_RESTORE ---
		//! @brief everything the account owns, with `productId` carrying the
		//! STOREFRONT identifier - the service maps them back to logical ids
		//! through the catalog's reverse index
		std::vector<Entitlement>	entitlements;

		//! did the store answer at all (false = `reason` says why)
		bool					completed = false;
		//! a one-line reason for every non-success
		String					reason;
	};

	//! @brief one thing an ad surface finished, handed to the main thread.
	struct ORKIGE_CORE_DLL AdEvent
	{
		//! which shape of answer this is
		enum Kind
		{
			AE_LOAD = 0,	//!< a load settled
			AE_SHOW			//!< a show settled
		};

		Kind					kind = AE_LOAD;
		MonetizationRequestId	id = 0;
		AdFormat				format = AF_BANNER;
		String					placement;

		//--- AE_LOAD ---
		AdLoadResult			loadResult = ALR_ERROR;

		//--- AE_SHOW ---
		AdShowResult			showResult = ASR_ERROR;
		//! which reward is due (ASR_REWARD_EARNED only)
		String					rewardId;
		//! how much of it (ASR_REWARD_EARNED only)
		double					rewardAmount = 0.0;

		//! a one-line reason for every non-success
		String					reason;
	};

	//! @brief THE STORE PLUGIN CONTRACT - everything a purchase surface has to
	//! provide, and nothing about who provides it.
	//!
	//! EVERY store provider is a plugin, INCLUDING the ones shipped with the
	//! engine. There is no privileged built-in path, because an extension
	//! interface none of our own code has to use is reliably inadequate: the
	//! simulated provider goes in through exactly this interface, so anything
	//! it cannot express is a gap a real one would hit too.
	//!
	//! THE THREADING CONTRACT: every method here is called on the MAIN thread.
	//! Where a provider actually does its work - a platform callback queue, a
	//! worker thread, a JNI thread - is its own business; it publishes results
	//! ONLY through poll(), which the main thread drains once per frame.
	//!
	//! ON CONSENT: a store provider is NOT gated on consent, unlike an ad
	//! provider. Privacy consent governs advertising identifiers and
	//! personalised advertising; refusing to bring the purchase surface up
	//! until a player has answered a privacy dialogue would stop a paying
	//! customer from paying, and would be wrong rather than cautious.
	class ORKIGE_CORE_DLL StoreProvider
	{
	public:
		virtual ~StoreProvider();

		//! the provider's name for logs and diagnostics
		virtual char const * name() const = 0;
		//! @brief which catalog column this provider reads. The service resolves
		//! logical ids through it, so a provider that answers SF_UNKNOWN can
		//! never be sent a product.
		virtual StorefrontId storefront() const = 0;

		//! @brief bring the purchase surface up. Called ONCE.
		//! @return false when it cannot come up (the service then refuses every
		//! purchase honestly instead of hanging on a store that is not there)
		virtual bool initialize() = 0;
		//! tear it down, dropping every in-flight request without answering
		virtual void shutdown() = 0;

		//! @brief ask the storefront for the display metadata of these
		//! STOREFRONT identifiers; publishes ONE SE_PRODUCTS event for @p id
		virtual void requestProducts(MonetizationRequestId id,
			StringVector const & storeIds) = 0;

		//! @brief begin a purchase of one STOREFRONT identifier; publishes ONE
		//! SE_PURCHASE event for @p id - including for the deferred case, which
		//! reports PS_PENDING now and may publish an UNSOLICITED settlement
		//! later
		virtual void purchase(MonetizationRequestId id, String const & storeId) = 0;

		//! @brief ask the store for everything this account owns; publishes ONE
		//! SE_RESTORE event for @p id. THE ONLY way entitlements come back after
		//! a reinstall or on the player's second device.
		virtual void restore(MonetizationRequestId id) = 0;

		//! @brief acknowledge a settled transaction. MANDATORY: a store that is
		//! never told the goods were delivered will refund the purchase and,
		//! for a consumable, refuse to sell it again.
		virtual void finishTransaction(String const & transactionId) = 0;

		//! main-thread drain of whatever settled since the last call
		virtual void poll(std::vector<StoreEvent> & out) = 0;
	};

	//! @brief THE AD PLUGIN CONTRACT - everything an advertising surface has to
	//! provide, and nothing about who provides it.
	//!
	//! CONSENT IS AN ORDERING CONSTRAINT, NOT A FLAG, and it is encoded in the
	//! shape here: initialize() is the ONLY entry point, and it TAKES the
	//! gathered consent, so a provider cannot come up without one. The service
	//! additionally refuses to call it while the status is CS_NOT_GATHERED. The
	//! wrong order - start the network, then ask the player - is the order that
	//! collects an advertising identifier before there was permission to, and
	//! it is the order this interface has no way to express.
	//!
	//! TEST MODE IS FIRST-CLASS for the same reason it is an initialize()
	//! argument on every real mediation surface: test inventory is bound when
	//! the network starts, and a development build that serves LIVE adverts
	//! generates invalid traffic against the account that owns them.
	//!
	//! THE THREADING CONTRACT is the store provider's: main-thread calls,
	//! results published only through poll().
	class ORKIGE_CORE_DLL AdProvider
	{
	public:
		virtual ~AdProvider();

		//! the provider's name for logs and diagnostics
		virtual char const * name() const = 0;

		//! @brief bring the ad surface up - the ONLY entry point, and it needs
		//! the gathered consent to exist.
		//! @param consent what the player answered; a provider serves
		//! contextual-only inventory unless consent.personalizedAds()
		//! @param testMode bind the network's TEST inventory rather than the
		//! account's live units
		//! @return false when it cannot come up
		virtual bool initialize(ConsentState const & consent, bool testMode) = 0;
		//! tear it down
		virtual void shutdown() = 0;

		//! @brief the player changed their mind in a settings screen. A
		//! provider that cannot honour a change without a restart says so in
		//! its own documentation; the seam always tells it.
		virtual void onConsentChanged(ConsentState const & consent) = 0;

		//! @brief begin a load; publishes ONE AE_LOAD event for @p id
		virtual void load(MonetizationRequestId id, AdFormat format,
			String const & placement) = 0;
		//! @brief present a loaded unit; publishes ONE AE_SHOW event for @p id
		virtual void show(MonetizationRequestId id, AdFormat format,
			String const & placement) = 0;
		//! take the banner down (no-op when none is up)
		virtual void hideBanner() = 0;

		//! @brief the screen the banner currently occupies. Reported even when
		//! nothing is on screen (an all-zero, invisible geometry), so a layout
		//! can ask unconditionally.
		virtual BannerGeometry bannerGeometry() const = 0;

		//! main-thread drain of whatever settled since the last call
		virtual void poll(std::vector<AdEvent> & out) = 0;
	};

	/** @} */
}

#endif //__MonetizationProvider_h__3_8_2026__10_00_00__
