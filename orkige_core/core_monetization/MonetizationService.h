/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	MonetizationService.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __MonetizationService_h__3_8_2026__10_00_00__
#define __MonetizationService_h__3_8_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_monetization/AdPlacement.h"
#include "core_monetization/MonetizationProvider.h"
#include "core_monetization/MonetizationTypes.h"
#include "core_monetization/ProductCatalog.h"
#include "core_util/SafeArea.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

namespace Orkige
{
	/** \addtogroup Monetization
	*  @{ */

	//! @brief THE monetization seam: the one object a game, a script or a tool
	//! talks to when it needs to sell something or serve an advert, with every
	//! actual store and every actual ad network behind a plugin.
	//!
	//! ASYNC BY CONSTRUCTION, exactly like HttpClient: nothing here blocks, a
	//! request returns a handle immediately, and its ONE answer is delivered
	//! from update() at a frame boundary. A payment sheet or an advert callback
	//! therefore never lands in the middle of a world update, and a refusal
	//! travels through the SAME callback as a success so a caller has one error
	//! path rather than two.
	//!
	//! THE CALLBACK CONTRACT: every request that returns a non-zero handle gets
	//! EXACTLY ONE callback, on the main thread, from update(). A request the
	//! seam refuses outright still gets a handle and still gets its answer at
	//! the next update().
	//!
	//! EVERY PROVIDER IS A PLUGIN, including the simulated one shipped with the
	//! engine. The service holds a StoreProvider and an AdProvider and knows
	//! nothing else about either; there is no built-in fast path a real
	//! integration would have to go around.
	//!
	//! THE TWO SEAMS MEET AT THE NO-ADS ENTITLEMENT. "Remove ads" is the most
	//! common purchase there is, so owning a product marked
	//! Product::grantsNoAds suppresses ad serving through AdPolicy, and the
	//! suppression is reported honestly (ALR_SUPPRESSED / ASR_SUPPRESSED)
	//! rather than by silently doing nothing.
	//!
	//! ENTITLEMENTS ARE A CACHE, NEVER A SAVE. The platform is the source of
	//! truth and restore() is how ownership comes back after a reinstall or on
	//! a second device. This class deliberately has no persistence at all: a
	//! save file does not travel with the player's account, and one the player
	//! can edit is not a proof of payment.
	//!
	//! @remarks Like SaveStore, TweenManager and HttpClient this is an OWNED
	//! singleton: a runtime that wants monetization creates one, and code that
	//! reaches for it guards on getSingletonPtr(), so a host without one (the
	//! editor's edit mode) leaves the surface an honest no-op.
	//!
	//! @remarks THE INTENDED SCRIPT SURFACE (not yet wired - the binding is a
	//! mechanical follow-up against this class, and the vocabulary below is the
	//! contract it should realise). A global `store` table and a global `ads`
	//! table, both honest no-ops when no service exists:
	//!
	//!     store.products(function(res) ... end)      -- requestProducts
	//!     store.purchase("remove_ads", function(res) -- purchase
	//!         if res.state == "purchased" or res.state == "already_owned" then
	//!         elseif res.state == "pending" then     -- grant NOTHING yet
	//!         elseif res.state == "cancelled" then   -- not an error
	//!         end end)
	//!     store.restore(function(res) ... end)       -- restore
	//!     store.owns("remove_ads")                   -- hasEntitlement
	//!     store.entitlements()                       -- entitlements
	//!     store.finish(transactionId)                -- finishTransaction
	//!
	//!     ads.setConsent{ status = "granted", tracking = true }  -- setConsent
	//!     ads.init(testMode)                         -- initializeAds
	//!     ads.load("rewarded", "level_end", function(res) ... end)
	//!     ads.show("rewarded", "level_end", function(res)
	//!         if res.rewardEarned then ... end end)  -- THE branch
	//!     ads.hideBanner()                           -- hideBanner
	//!     ads.state("banner", "")                    -- adState token
	//!     ads.banner()                               -- bannerGeometry
	//!     ads.adFree()                               -- isAdFree
	//!
	//! Each maps one-to-one onto a method below; the enums already carry the
	//! stable string tokens the table trades in (@see adFormatName,
	//! purchaseStateName, adShowResultName).
	//!
	//! @remarks THE INTENDED MCP SURFACE (not yet wired). Two verbs, both
	//! DEVELOPMENT-ONLY and both driving the SIMULATED provider - an agent
	//! must never be able to reach a real payment surface or a real ad network,
	//! for the same reason no MCP verb performs a git mutation:
	//!
	//!     set_monetization_scenario { key = "loadResult", value = "no_fill" }
	//!         -> SimulatedScenario::apply on the running play session's
	//!            simulated provider, so an agent can pin an unhappy path and
	//!            then assert the game handled it
	//!     get_monetization_state
	//!         -> the honest read-back: consent status, whether ads
	//!            initialized, test mode, per-placement AdState, the banner
	//!            geometry, isAdFree and the entitlement list
	//!
	//! Both are reads/writes of state this class already exposes, so the verb
	//! handler stays a thin adapter with no monetization logic of its own.
	class ORKIGE_CORE_DLL MonetizationService : public Singleton<MonetizationService>
	{
		DECL_OSINGLETON(MonetizationService);
		//--- Types -------------------------------------------
	private:
		//! a product query awaiting its answer
		struct PendingProducts
		{
			ProductQueryCallback	onComplete;
		};
		//! a purchase awaiting its answer (the logical id it was asked for)
		struct PendingPurchase
		{
			String				productId;
			PurchaseCallback	onComplete;
		};
		//! a restore awaiting its answer
		struct PendingRestore
		{
			RestoreCallback		onComplete;
		};
		//! an ad load awaiting its answer
		struct PendingAdLoad
		{
			AdFormat			format = AF_BANNER;
			String				placement;
			AdLoadCallback		onComplete;
		};
		//! an ad show awaiting its answer
		struct PendingAdShow
		{
			AdFormat			format = AF_BANNER;
			String				placement;
			AdShowCallback		onComplete;
		};
		//--- Variables ---------------------------------------
	private:
		ProductCatalog					mCatalog;		//!< what the game sells
		ConsentState					mConsent;		//!< what the player answered
		AdPolicy						mAdPolicy;		//!< what no-ads silences

		std::unique_ptr<StoreProvider>	mStore;			//!< the store plugin
		std::unique_ptr<AdProvider>		mAds;			//!< the ad plugin
		bool							mStoreReady;	//!< the store came up
		bool							mAdsReady;		//!< the ad surface came up
		bool							mTestMode;		//!< ads bound test inventory

		MonetizationRequestId			mNextId;		//!< handle allocator

		std::map<MonetizationRequestId, PendingProducts>	mPendingProducts;
		std::map<MonetizationRequestId, PendingPurchase>	mPendingPurchases;
		std::map<MonetizationRequestId, PendingRestore>		mPendingRestores;
		std::map<MonetizationRequestId, PendingAdLoad>		mPendingAdLoads;
		std::map<MonetizationRequestId, PendingAdShow>		mPendingAdShows;

		//! refusals queued for delivery at the NEXT update()
		std::vector<ProductQueryResult>	mImmediateProducts;
		std::vector<PurchaseResult>		mImmediatePurchases;
		std::vector<RestoreResult>		mImmediateRestores;
		std::vector<AdLoadOutcome>		mImmediateAdLoads;
		std::vector<AdShowOutcome>		mImmediateAdShows;

		//! one unit per (format, placement) pair
		std::map<String, AdPlacement>	mPlacements;
		//! the cached ownership picture (@see the class remarks - never a save)
		std::vector<Entitlement>		mEntitlements;

		//! is a fullscreen ad covering the app right now
		bool							mTakeoverActive;
		//! which format is covering it (valid while mTakeoverActive)
		AdFormat						mTakeoverFormat;

		//! reused drain buffers (a steady-state frame allocates nothing)
		std::vector<StoreEvent>			mStoreScratch;
		std::vector<AdEvent>			mAdScratch;
		//--- Methods -----------------------------------------
	public:
		MonetizationService();
		//! destructor - drops every pending request WITHOUT calling back
		~MonetizationService();

		//--- the catalog ---
		//! what the game sells (@see ProductCatalog for the per-store id model)
		ProductCatalog & catalog() { return this->mCatalog; }
		//! @see MonetizationService::catalog
		ProductCatalog const & catalog() const { return this->mCatalog; }

		//--- providers (every provider is a plugin) ---
		//! @brief install the store plugin. Replacing one shuts the old one
		//! down; installing NULL leaves the store surface honestly absent.
		void setStoreProvider(std::unique_ptr<StoreProvider> provider);
		//! @brief install the ad plugin (@see setStoreProvider).
		void setAdProvider(std::unique_ptr<AdProvider> provider);
		//! the installed store plugin, or NULL
		StoreProvider * storeProvider() const { return this->mStore.get(); }
		//! the installed ad plugin, or NULL
		AdProvider * adProvider() const { return this->mAds.get(); }

		//--- consent (the ordering constraint) ---
		//! @brief record what the player answered. Called BEFORE ads
		//! initialize; calling it again later reaches a running ad provider
		//! through onConsentChanged, and revoking consent back to
		//! CS_NOT_GATHERED SHUTS THE AD SURFACE DOWN rather than leaving a
		//! network running without permission.
		void setConsent(ConsentState const & consent);
		//! @see MonetizationService::setConsent
		ConsentState const & consent() const { return this->mConsent; }
		//! has the player been asked at all
		bool isConsentGathered() const { return this->mConsent.gathered(); }

		//--- initialization ---
		//! @brief bring the store up. NOT gated on consent (@see StoreProvider
		//! on why a purchase surface must work for a player who declined
		//! tracking).
		//! @return false with an honest log line when there is no provider or
		//! it could not start
		bool initializeStore();
		//! @brief bring the ad surface up.
		//! @param testMode bind the network's TEST inventory (@see AdProvider)
		//! @return false when consent has NOT been gathered - THE ORDERING
		//! CONSTRAINT, refused by name rather than by a silent no-op - or when
		//! there is no provider or it could not start
		bool initializeAds(bool testMode);
		//! did the store come up
		bool isStoreReady() const { return this->mStoreReady; }
		//! did the ad surface come up
		bool isAdsReady() const { return this->mAdsReady; }
		//! are ads bound to test inventory
		bool isTestMode() const { return this->mTestMode; }

		//--- store operations ---
		//! @brief ask the storefront for the metadata of every catalogued
		//! product. @return the handle, or 0 when @p onComplete is empty
		MonetizationRequestId requestProducts(ProductQueryCallback const & onComplete);
		//! @brief buy one LOGICAL product. @return the handle, or 0 when
		//! @p onComplete is empty
		MonetizationRequestId purchase(String const & logicalId,
			PurchaseCallback const & onComplete);
		//! @brief ask the store for everything this account owns - the ONLY way
		//! entitlements survive a reinstall. @return the handle, or 0 when
		//! @p onComplete is empty
		MonetizationRequestId restore(RestoreCallback const & onComplete);
		//! @brief acknowledge a settled transaction (MANDATORY, @see
		//! StoreProvider::finishTransaction)
		void finishTransaction(String const & transactionId);

		//--- entitlements (a cache of what the platform said) ---
		//! does the player own @p logicalId, and is it active right now
		bool hasEntitlement(String const & logicalId) const;
		//! everything the last purchase/restore reported as owned
		std::vector<Entitlement> const & entitlements() const
		{
			return this->mEntitlements;
		}
		//! @brief does the player own ANY product marked Product::grantsNoAds -
		//! THE LINK between the two seams
		bool isAdFree() const;
		//! drop the cached ownership picture (a sign-out, a test)
		void clearEntitlements();

		//--- ads ---
		//! which formats a no-ads entitlement silences
		void setAdPolicy(AdPolicy const & policy) { this->mAdPolicy = policy; }
		//! @see MonetizationService::setAdPolicy
		AdPolicy const & adPolicy() const { return this->mAdPolicy; }

		//! @brief begin a load. @return the handle, or 0 when @p onComplete is
		//! empty. Every refusal - no provider, consent missing, suppressed by a
		//! no-ads entitlement, already loading - arrives through the callback.
		MonetizationRequestId loadAd(AdFormat format, String const & placement,
			AdLoadCallback const & onComplete);
		//! @brief present a loaded unit. @return the handle, or 0 when
		//! @p onComplete is empty. SHOW BEFORE READY answers ASR_NOT_READY.
		MonetizationRequestId showAd(AdFormat format, String const & placement,
			AdShowCallback const & onComplete);
		//! take the banner down
		void hideBanner();
		//! where one unit stands (AS_IDLE for a placement never touched)
		AdState adState(AdFormat format, String const & placement) const;

		//--- the banner's screen cost ---
		//! @brief the screen the banner occupies right now (an all-zero,
		//! invisible geometry when there is none, so a layout asks
		//! unconditionally)
		BannerGeometry bannerGeometry() const;
		//! @brief THE ONE inset set UI must lay out inside: the display's own
		//! safe-area insets composed with the strip the banner eats.
		SafeAreaInsets layoutInsets(SafeAreaInsets const & displayInsets) const;

		//--- the takeover gate ---
		//! @brief is a FULLSCREEN advert covering the app? While this is true
		//! the host must not advance the sim and should suspend or duck audio -
		//! the same consequences AppLifecycle's background gate carries, applied
		//! by the same host at the same place. This class only REPORTS the
		//! state; it does not pause anything itself, because the loop that owns
		//! the tick order is the one entitled to skip it.
		//! @remarks On Android the system back button must not reach the game
		//! while this holds - the advert owns the gesture.
		bool isTakeoverActive() const { return this->mTakeoverActive; }
		//! which format is covering the app (valid while isTakeoverActive)
		AdFormat takeoverFormat() const { return this->mTakeoverFormat; }

		//--- the frame boundary ---
		//! @brief drain both providers and deliver every due callback on this
		//! thread. Call once per frame.
		void update();

		//! how many requests are still awaiting an answer (diagnostics/tests)
		std::size_t pendingCount() const;
	private:
		//! the key one (format, placement) pair is held under
		static String placementKey(AdFormat format, String const & placement);
		//! the unit for a pair, created idle on first use
		AdPlacement & placementFor(AdFormat format, String const & placement);

		//! deliver one drained store event to its pending entry
		void deliverStoreEvent(StoreEvent const & event);
		//! deliver one drained ad event to its pending entry
		void deliverAdEvent(AdEvent const & event);

		//! record (or refresh) an entitlement, keyed by logical product id
		void applyEntitlement(Entitlement const & entitlement);

		//! queue an ad-load refusal for the next update()
		void refuseLoad(MonetizationRequestId id, AdFormat format,
			String const & placement, AdLoadResult result, String const & reason);
		//! queue an ad-show refusal for the next update()
		void refuseShow(MonetizationRequestId id, AdFormat format,
			String const & placement, AdShowResult result, String const & reason);

		MonetizationService(MonetizationService const &) = delete;
		MonetizationService & operator=(MonetizationService const &) = delete;
	};

	/** @} */
}

#endif //__MonetizationService_h__3_8_2026__10_00_00__
