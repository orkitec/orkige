/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	SimulatedProvider.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __SimulatedProvider_h__3_8_2026__10_00_00__
#define __SimulatedProvider_h__3_8_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_monetization/MonetizationProvider.h"
#include "core_monetization/MonetizationTypes.h"
#include "core_util/String.h"

#include <cstddef>
#include <map>
#include <vector>

namespace Orkige
{
	/** \addtogroup Monetization
	*  @{ */

	//! @brief what the simulated providers should DO next - the whole
	//! development-time surface, as plain data.
	//!
	//! THE POINT OF THE SIMULATOR IS NOT A CONVINCING FAKE ADVERT. It is
	//! making the UNHAPPY paths reachable ON DEMAND. Real stores and real ad
	//! networks produce them rarely and unpredictably, which is precisely why
	//! shipped games mishandle them: no fill is ordinary in a low-traffic
	//! region and never once appears during development; a deferred purchase
	//! settles days later; a player closes a rewarded advert one second before
	//! the reward. Each of those is one field here.
	//!
	//! DETERMINISTIC, WITH NO RANDOMNESS ANYWHERE. A simulator that rolls dice
	//! produces a test that passes sometimes, which is worse than no test. A
	//! scenario is pinned, the run is reproducible, and the assertion is
	//! meaningful.
	//!
	//! @remarks Latency is counted in POLL TICKS, not milliseconds - one tick
	//! is one MonetizationService::update(). A wall clock would reintroduce the
	//! nondeterminism the whole type exists to remove.
	struct ORKIGE_CORE_DLL SimulatedScenario
	{
		//--- ads ---
		//! what the next load answers unless a per-format override says otherwise
		AdLoadResult	loadResult = ALR_LOADED;
		//! per-format override of `loadResult` (a banner may fill where an
		//! interstitial does not - a real and commonly surprising difference)
		std::map<AdFormat, AdLoadResult>	loadResultByFormat;
		//! what the next show answers
		AdShowResult	showResult = ASR_COMPLETED;
		//! which reward a rewarded show grants (ASR_REWARD_EARNED only)
		String			rewardId = "reward";
		//! how much of it (ASR_REWARD_EARNED only)
		double			rewardAmount = 1.0;
		//! does the ad surface refuse to come up at all
		bool			adInitializeFails = false;

		//--- the banner's screen cost ---
		BannerPosition	bannerPosition = BP_BOTTOM;
		unsigned int	bannerWidth = 320;
		unsigned int	bannerHeight = 50;
		//! @see BannerGeometry::insideSafeArea
		bool			bannerInsideSafeArea = true;

		//--- the store ---
		//! what the next purchase answers
		PurchaseState	purchaseState = PS_PURCHASED;
		//! does the store refuse to come up at all
		bool			storeInitializeFails = false;
		//! does a product query come back not-completed
		bool			productsUnavailable = false;
		//! @brief the STOREFRONT identifiers a restore yields - store ids, not
		//! logical ones, because that is what a real store hands back and the
		//! catalog's reverse index is what turns them into names
		StringVector	restoreStoreIds;
		//! does a restore come back not-completed
		bool			restoreFails = false;

		//--- shared ---
		//! how many poll ticks an answer waits before it is delivered
		unsigned int	latencyTicks = 0;
		//! the one-line reason every non-success reports ("" = a derived one)
		String			reason;

		//! @brief apply ONE `key = value` setting - the pure parse the future
		//! cvar and MCP surface both drive, so an agent can pin an unhappy path
		//! by name and a unit test can prove the parse.
		//!
		//! Recognised keys: `loadResult`, `loadResult.<format>`, `showResult`,
		//! `rewardId`, `rewardAmount`, `adInitializeFails`, `bannerPosition`,
		//! `bannerWidth`, `bannerHeight`, `bannerInsideSafeArea`,
		//! `purchaseState`, `storeInitializeFails`, `productsUnavailable`,
		//! `restoreStoreIds` (comma separated), `restoreFails`,
		//! `latencyTicks`, `reason`.
		//!
		//! @param outError filled with a one-line reason when the key or the
		//! value is not recognised
		//! @return false when nothing was applied
		bool apply(String const & key, String const & value, String & outError);
	};

	//! @brief the simulated STORE plugin: a purchase surface that answers
	//! whatever the scenario says, at a deterministic tick.
	//! @remarks It goes in through the ordinary StoreProvider interface, with
	//! no privileged path of its own - which is the point. Anything the
	//! simulator cannot express through this interface is a gap a real store
	//! integration would hit too.
	class ORKIGE_CORE_DLL SimulatedStoreProvider : public StoreProvider
	{
		//--- Types -------------------------------------------
	private:
		//! an answer waiting out its latency
		struct Scheduled
		{
			unsigned int	remainingTicks = 0;
			StoreEvent		event;
		};
		//--- Variables ---------------------------------------
	private:
		SimulatedScenario		mScenario;	//!< what to answer next
		StorefrontId			mStorefront;//!< which catalog column it reads
		bool					mStarted;	//!< initialize() succeeded
		std::vector<Scheduled>	mQueue;		//!< answers awaiting their tick
		//! transactions finishTransaction() acknowledged (assertions/tests)
		StringVector			mFinished;
		//! how many transactions it has issued (the id counter)
		unsigned int			mTransactionCounter;
		//--- Methods -----------------------------------------
	public:
		//! @param storefront which catalog column to resolve against -
		//! SF_SIMULATED by default, but settable so a test can drive the real
		//! per-platform identifier columns
		explicit SimulatedStoreProvider(StorefrontId storefront = SF_SIMULATED);
		virtual ~SimulatedStoreProvider();

		//! what it answers next
		SimulatedScenario & scenario() { return this->mScenario; }
		//! @see SimulatedStoreProvider::scenario
		SimulatedScenario const & scenario() const { return this->mScenario; }
		//! replace the whole scenario
		void setScenario(SimulatedScenario const & scenario)
		{
			this->mScenario = scenario;
		}
		//! transactions the game acknowledged (a consumable that is never
		//! finished cannot be bought again on a real store)
		StringVector const & finishedTransactions() const
		{
			return this->mFinished;
		}

		//--- StoreProvider ---
		virtual char const * name() const;
		virtual StorefrontId storefront() const;
		virtual bool initialize();
		virtual void shutdown();
		virtual void requestProducts(MonetizationRequestId id,
			StringVector const & storeIds);
		virtual void purchase(MonetizationRequestId id, String const & storeId);
		virtual void restore(MonetizationRequestId id);
		virtual void finishTransaction(String const & transactionId);
		virtual void poll(std::vector<StoreEvent> & out);
	private:
		//! queue an answer for delivery `latencyTicks` polls from now
		void schedule(StoreEvent const & event);
	};

	//! @brief the simulated AD plugin: an advertising surface that answers
	//! whatever the scenario says, at a deterministic tick, and reports a
	//! banner geometry so a layout can be developed against a strip that no
	//! real network is filling yet.
	class ORKIGE_CORE_DLL SimulatedAdProvider : public AdProvider
	{
		//--- Types -------------------------------------------
	private:
		//! an answer waiting out its latency
		struct Scheduled
		{
			unsigned int	remainingTicks = 0;
			AdEvent			event;
		};
		//--- Variables ---------------------------------------
	private:
		SimulatedScenario		mScenario;		//!< what to answer next
		bool					mStarted;		//!< initialize() succeeded
		bool					mTestMode;		//!< bound test inventory
		ConsentState			mConsent;		//!< what it was started with
		bool					mBannerVisible;	//!< a banner is on screen
		std::vector<Scheduled>	mQueue;			//!< answers awaiting their tick
		//--- Methods -----------------------------------------
	public:
		SimulatedAdProvider();
		virtual ~SimulatedAdProvider();

		//! what it answers next
		SimulatedScenario & scenario() { return this->mScenario; }
		//! @see SimulatedAdProvider::scenario
		SimulatedScenario const & scenario() const { return this->mScenario; }
		//! replace the whole scenario
		void setScenario(SimulatedScenario const & scenario)
		{
			this->mScenario = scenario;
		}
		//! @brief the consent it was started with - the ORDERING PROOF a test
		//! asserts against (a provider that came up at all was given one)
		ConsentState const & startedWithConsent() const { return this->mConsent; }
		//! is it bound to test inventory
		bool isTestMode() const { return this->mTestMode; }

		//--- AdProvider ---
		virtual char const * name() const;
		virtual bool initialize(ConsentState const & consent, bool testMode);
		virtual void shutdown();
		virtual void onConsentChanged(ConsentState const & consent);
		virtual void load(MonetizationRequestId id, AdFormat format,
			String const & placement);
		virtual void show(MonetizationRequestId id, AdFormat format,
			String const & placement);
		virtual void hideBanner();
		virtual BannerGeometry bannerGeometry() const;
		virtual void poll(std::vector<AdEvent> & out);
	private:
		//! queue an answer for delivery `latencyTicks` polls from now
		void schedule(AdEvent const & event);
		//! the load verdict for @p format (the per-format override, or the default)
		AdLoadResult loadResultFor(AdFormat format) const;
	};

	/** @} */
}

#endif //__SimulatedProvider_h__3_8_2026__10_00_00__
