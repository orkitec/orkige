/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	MonetizationService.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_monetization/MonetizationService.h"

#include "core_debug/DebugMacros.h"

#include <memory>
#include <utility>

namespace Orkige
{
	IMPL_OSINGLETON(MonetizationService);
	//---------------------------------------------------------
	MonetizationService::MonetizationService()
		: mStoreReady(false)
		, mAdsReady(false)
		, mTestMode(false)
		, mNextId(1)
		, mTakeoverActive(false)
		, mTakeoverFormat(AF_INTERSTITIAL)
	{
	}
	//---------------------------------------------------------
	MonetizationService::~MonetizationService()
	{
		// teardown drops everything in flight WITHOUT calling back: the owners
		// of those callbacks are going away with us
		if(this->mStore) { this->mStore->shutdown(); }
		if(this->mAds) { this->mAds->shutdown(); }
	}
	//---------------------------------------------------------
	void MonetizationService::setStoreProvider(
		std::unique_ptr<StoreProvider> provider)
	{
		if(this->mStore) { this->mStore->shutdown(); }
		this->mStore = std::move(provider);
		this->mStoreReady = false;
	}
	//---------------------------------------------------------
	void MonetizationService::setAdProvider(std::unique_ptr<AdProvider> provider)
	{
		if(this->mAds) { this->mAds->shutdown(); }
		this->mAds = std::move(provider);
		this->mAdsReady = false;
		this->mTakeoverActive = false;
		this->mPlacements.clear();
	}
	//---------------------------------------------------------
	void MonetizationService::setConsent(ConsentState const & consent)
	{
		this->mConsent = consent;

		if(!this->mAds) { return; }

		if(!consent.gathered() && this->mAdsReady)
		{
			// consent was WITHDRAWN back to "never asked". Leaving a network
			// running would keep serving on a permission that no longer
			// exists, so the surface goes down and every unit forgets its
			// inventory.
			oDebugWarn("monetization", 0, "consent was withdrawn - the ad "
				"surface is shutting down");
			this->mAds->shutdown();
			this->mAdsReady = false;
			this->mTakeoverActive = false;
			this->mPlacements.clear();
			return;
		}

		if(this->mAdsReady)
		{
			this->mAds->onConsentChanged(consent);
		}
	}
	//---------------------------------------------------------
	bool MonetizationService::initializeStore()
	{
		if(!this->mStore)
		{
			oDebugWarn("monetization", 0,
				"no store provider is installed - purchases will be refused");
			return false;
		}
		if(this->mStoreReady) { return true; }

		this->mStoreReady = this->mStore->initialize();
		if(!this->mStoreReady)
		{
			oDebugWarn("monetization", 0, "the store provider '"
				<< this->mStore->name() << "' did not come up");
		}
		return this->mStoreReady;
	}
	//---------------------------------------------------------
	bool MonetizationService::initializeAds(bool testMode)
	{
		// THE ORDERING CONSTRAINT. A network that starts before the player has
		// been asked collects an advertising identifier it had no permission
		// to collect, so this is refused BY NAME rather than by quietly
		// serving nothing.
		if(!this->mConsent.gathered())
		{
			oDebugWarn("monetization", 0, "ads cannot initialize before consent "
				"has been gathered - call setConsent first");
			return false;
		}
		if(!this->mAds)
		{
			oDebugWarn("monetization", 0,
				"no ad provider is installed - ad requests will be refused");
			return false;
		}
		if(this->mAdsReady) { return true; }

		this->mTestMode = testMode;
		this->mAdsReady = this->mAds->initialize(this->mConsent, testMode);
		if(!this->mAdsReady)
		{
			oDebugWarn("monetization", 0, "the ad provider '"
				<< this->mAds->name() << "' did not come up");
		}
		return this->mAdsReady;
	}
	//---------------------------------------------------------
	MonetizationRequestId MonetizationService::requestProducts(
		ProductQueryCallback const & onComplete)
	{
		if(!onComplete)
		{
			oDebugWarn("monetization", 0,
				"a product query with no completion callback was ignored");
			return 0;
		}
		const MonetizationRequestId id = this->mNextId++;

		if(!this->mStore || !this->mStoreReady)
		{
			ProductQueryResult refusal;
			refusal.id = id;
			refusal.completed = false;
			refusal.reason = "the store is not available";
			this->mPendingProducts[id].onComplete = onComplete;
			this->mImmediateProducts.push_back(refusal);
			return id;
		}

		this->mPendingProducts[id].onComplete = onComplete;
		this->mStore->requestProducts(id,
			this->mCatalog.storeIdsFor(this->mStore->storefront()));
		return id;
	}
	//---------------------------------------------------------
	MonetizationRequestId MonetizationService::purchase(String const & logicalId,
		PurchaseCallback const & onComplete)
	{
		if(!onComplete)
		{
			oDebugWarn("monetization", 0,
				"a purchase with no completion callback was ignored");
			return 0;
		}
		const MonetizationRequestId id = this->mNextId++;
		this->mPendingPurchases[id].productId = logicalId;
		this->mPendingPurchases[id].onComplete = onComplete;

		PurchaseResult refusal;
		refusal.id = id;
		refusal.productId = logicalId;
		refusal.state = PS_UNAVAILABLE;

		if(!this->mStore || !this->mStoreReady)
		{
			refusal.reason = "the store is not available";
			this->mImmediatePurchases.push_back(refusal);
			return id;
		}

		// the LOGICAL id becomes the storefront's own identifier here, and
		// nowhere else - a product with no column for this storefront is
		// refused by name rather than sent as an empty string
		const String storeId = this->mCatalog.storeIdFor(logicalId,
			this->mStore->storefront());
		if(storeId.empty())
		{
			refusal.reason = "'" + logicalId + "' is not sold on the "
				+ storefrontName(this->mStore->storefront()) + " storefront";
			this->mImmediatePurchases.push_back(refusal);
			return id;
		}

		this->mStore->purchase(id, storeId);
		return id;
	}
	//---------------------------------------------------------
	MonetizationRequestId MonetizationService::restore(
		RestoreCallback const & onComplete)
	{
		if(!onComplete)
		{
			oDebugWarn("monetization", 0,
				"a restore with no completion callback was ignored");
			return 0;
		}
		const MonetizationRequestId id = this->mNextId++;
		this->mPendingRestores[id].onComplete = onComplete;

		if(!this->mStore || !this->mStoreReady)
		{
			RestoreResult refusal;
			refusal.id = id;
			refusal.completed = false;
			refusal.reason = "the store is not available";
			this->mImmediateRestores.push_back(refusal);
			return id;
		}

		this->mStore->restore(id);
		return id;
	}
	//---------------------------------------------------------
	void MonetizationService::finishTransaction(String const & transactionId)
	{
		if(!this->mStore || transactionId.empty()) { return; }
		this->mStore->finishTransaction(transactionId);
	}
	//---------------------------------------------------------
	bool MonetizationService::hasEntitlement(String const & logicalId) const
	{
		for(std::size_t i = 0; i < this->mEntitlements.size(); ++i)
		{
			Entitlement const & owned = this->mEntitlements[i];
			if(owned.productId == logicalId) { return owned.active; }
		}
		return false;
	}
	//---------------------------------------------------------
	bool MonetizationService::isAdFree() const
	{
		for(std::size_t i = 0; i < this->mEntitlements.size(); ++i)
		{
			Entitlement const & owned = this->mEntitlements[i];
			if(!owned.active) { continue; }
			if(this->mCatalog.grantsNoAds(owned.productId)) { return true; }
		}
		return false;
	}
	//---------------------------------------------------------
	void MonetizationService::clearEntitlements()
	{
		this->mEntitlements.clear();
	}
	//---------------------------------------------------------
	void MonetizationService::applyEntitlement(Entitlement const & entitlement)
	{
		// a CONSUMABLE is spent, not owned: the game grants its effect from the
		// purchase callback and finishes the transaction. Recording one here
		// would make a coin pack look like a permanent unlock.
		if(entitlement.kind == PK_CONSUMABLE) { return; }

		for(std::size_t i = 0; i < this->mEntitlements.size(); ++i)
		{
			if(this->mEntitlements[i].productId != entitlement.productId)
			{
				continue;
			}
			this->mEntitlements[i] = entitlement;
			return;
		}
		this->mEntitlements.push_back(entitlement);
	}
	//---------------------------------------------------------
	String MonetizationService::placementKey(AdFormat format,
		String const & placement)
	{
		return adFormatName(format) + "|" + placement;
	}
	//---------------------------------------------------------
	AdPlacement & MonetizationService::placementFor(AdFormat format,
		String const & placement)
	{
		const String key = MonetizationService::placementKey(format, placement);
		std::map<String, AdPlacement>::iterator it = this->mPlacements.find(key);
		if(it != this->mPlacements.end()) { return it->second; }

		this->mPlacements[key] = AdPlacement(format, placement);
		return this->mPlacements[key];
	}
	//---------------------------------------------------------
	AdState MonetizationService::adState(AdFormat format,
		String const & placement) const
	{
		const String key = MonetizationService::placementKey(format, placement);
		std::map<String, AdPlacement>::const_iterator it =
			this->mPlacements.find(key);
		return (it == this->mPlacements.end()) ? AS_IDLE : it->second.state();
	}
	//---------------------------------------------------------
	void MonetizationService::refuseLoad(MonetizationRequestId id,
		AdFormat format, String const & placement, AdLoadResult result,
		String const & reason)
	{
		AdLoadOutcome refusal;
		refusal.id = id;
		refusal.format = format;
		refusal.placement = placement;
		refusal.result = result;
		refusal.reason = reason;
		this->mImmediateAdLoads.push_back(refusal);
	}
	//---------------------------------------------------------
	void MonetizationService::refuseShow(MonetizationRequestId id,
		AdFormat format, String const & placement, AdShowResult result,
		String const & reason)
	{
		AdShowOutcome refusal;
		refusal.id = id;
		refusal.format = format;
		refusal.placement = placement;
		refusal.result = result;
		refusal.reason = reason;
		this->mImmediateAdShows.push_back(refusal);
	}
	//---------------------------------------------------------
	MonetizationRequestId MonetizationService::loadAd(AdFormat format,
		String const & placement, AdLoadCallback const & onComplete)
	{
		if(!onComplete)
		{
			oDebugWarn("monetization", 0,
				"an ad load with no completion callback was ignored");
			return 0;
		}
		const MonetizationRequestId id = this->mNextId++;
		this->mPendingAdLoads[id].format = format;
		this->mPendingAdLoads[id].placement = placement;
		this->mPendingAdLoads[id].onComplete = onComplete;

		// THE LINK between the two seams: a player who paid to be rid of
		// adverts does not get one fetched for them either - loading would
		// spend their data on inventory that can never be shown
		if(this->isAdFree() && this->mAdPolicy.suppresses(format))
		{
			this->refuseLoad(id, format, placement, ALR_SUPPRESSED,
				"a no-ads entitlement suppresses the "
					+ adFormatName(format) + " format");
			return id;
		}
		if(!this->mConsent.gathered())
		{
			this->refuseLoad(id, format, placement, ALR_CONSENT_MISSING,
				"consent has not been gathered");
			return id;
		}
		if(!this->mAds || !this->mAdsReady)
		{
			this->refuseLoad(id, format, placement, ALR_NOT_INITIALIZED,
				"the ad surface is not available");
			return id;
		}

		AdPlacement & unit = this->placementFor(format, placement);
		AdLoadResult refusal = ALR_BUSY;
		String reason;
		if(!unit.beginLoad(refusal, reason))
		{
			this->refuseLoad(id, format, placement, refusal, reason);
			return id;
		}

		this->mAds->load(id, format, placement);
		return id;
	}
	//---------------------------------------------------------
	MonetizationRequestId MonetizationService::showAd(AdFormat format,
		String const & placement, AdShowCallback const & onComplete)
	{
		if(!onComplete)
		{
			oDebugWarn("monetization", 0,
				"an ad show with no completion callback was ignored");
			return 0;
		}
		const MonetizationRequestId id = this->mNextId++;
		this->mPendingAdShows[id].format = format;
		this->mPendingAdShows[id].placement = placement;
		this->mPendingAdShows[id].onComplete = onComplete;

		if(this->isAdFree() && this->mAdPolicy.suppresses(format))
		{
			this->refuseShow(id, format, placement, ASR_SUPPRESSED,
				"a no-ads entitlement suppresses the "
					+ adFormatName(format) + " format");
			return id;
		}
		if(!this->mAds || !this->mAdsReady)
		{
			this->refuseShow(id, format, placement, ASR_ERROR,
				"the ad surface is not available");
			return id;
		}
		if(this->mTakeoverActive)
		{
			// two fullscreen adverts at once is a black screen on some
			// surfaces and a lost callback on the rest
			this->refuseShow(id, format, placement, ASR_ERROR,
				"a fullscreen ad is already showing");
			return id;
		}

		AdPlacement & unit = this->placementFor(format, placement);
		if(!unit.beginShow())
		{
			// SHOW BEFORE READY IS AN ERROR STATE, not undefined behaviour
			this->refuseShow(id, format, placement, ASR_NOT_READY,
				"this placement holds no inventory (state: "
					+ adStateName(unit.state()) + ")");
			return id;
		}

		if(adFormatIsTakeover(format))
		{
			this->mTakeoverActive = true;
			this->mTakeoverFormat = format;
		}
		this->mAds->show(id, format, placement);
		return id;
	}
	//---------------------------------------------------------
	void MonetizationService::hideBanner()
	{
		if(this->mAds) { this->mAds->hideBanner(); }

		// every banner placement, whatever it was named, comes down with it
		for(std::map<String, AdPlacement>::iterator it = this->mPlacements.begin();
			it != this->mPlacements.end(); ++it)
		{
			if(it->second.format() != AF_BANNER) { continue; }
			it->second.hide();
		}
	}
	//---------------------------------------------------------
	BannerGeometry MonetizationService::bannerGeometry() const
	{
		// reported unconditionally: a layout asks whether or not this build
		// serves advertising at all
		if(!this->mAds) { return BannerGeometry(); }
		return this->mAds->bannerGeometry();
	}
	//---------------------------------------------------------
	SafeAreaInsets MonetizationService::layoutInsets(
		SafeAreaInsets const & displayInsets) const
	{
		return this->bannerGeometry().composeWith(displayInsets);
	}
	//---------------------------------------------------------
	void MonetizationService::deliverStoreEvent(StoreEvent const & event)
	{
		switch(event.kind)
		{
		case StoreEvent::SE_PRODUCTS:
		{
			std::map<MonetizationRequestId, PendingProducts>::iterator it =
				this->mPendingProducts.find(event.id);
			if(it == this->mPendingProducts.end()) { return; }

			ProductQueryResult result;
			result.id = event.id;
			result.completed = event.completed;
			result.reason = event.reason;

			// the storefront answered in ITS identifiers; the catalog turns
			// them back into logical ids and keeps the metadata
			for(std::size_t i = 0; i < event.products.size(); ++i)
			{
				Product product = event.products[i];
				const String logicalId = this->mCatalog.logicalIdFor(
					this->mStore ? this->mStore->storefront() : SF_UNKNOWN,
					product.id);
				if(logicalId.empty())
				{
					// a storefront volunteering a product we never declared
					result.unknownProductIds.push_back(product.id);
					continue;
				}
				product.id = logicalId;
				Product * known = this->mCatalog.findMutable(logicalId);
				if(known != NULL)
				{
					// the catalog owns kind and grantsNoAds; the store owns
					// title, description and price
					product.kind = known->kind;
					product.grantsNoAds = known->grantsNoAds;
					known->title = product.title;
					known->description = product.description;
					known->displayPrice = product.displayPrice;
					known->priceCurrency = product.priceCurrency;
					known->priceValue = product.priceValue;
					known->available = product.available;
				}
				result.products.push_back(product);
			}
			for(std::size_t i = 0; i < event.unknownStoreIds.size(); ++i)
			{
				result.unknownProductIds.push_back(event.unknownStoreIds[i]);
			}

			const ProductQueryCallback callback = it->second.onComplete;
			this->mPendingProducts.erase(it);
			if(callback) { callback(result); }
			return;
		}
		case StoreEvent::SE_PURCHASE:
		{
			std::map<MonetizationRequestId, PendingPurchase>::iterator it =
				this->mPendingPurchases.find(event.id);

			PurchaseResult result;
			result.id = event.id;
			result.state = event.purchaseState;
			result.transactionId = event.transactionId;
			result.receipt = event.receipt;
			result.reason = event.reason;
			result.productId = (it != this->mPendingPurchases.end())
				? it->second.productId
				// an UNSOLICITED settlement (a deferred purchase approved in a
				// later session) has no request to correlate against, so the
				// reverse index is the only way to name it
				: this->mCatalog.logicalIdFor(
					this->mStore ? this->mStore->storefront() : SF_UNKNOWN,
					event.storeId);

			if(result.owned())
			{
				Entitlement granted;
				granted.productId = result.productId;
				Product const * product = this->mCatalog.find(result.productId);
				granted.kind = (product != NULL)
					? product->kind
					: PK_NON_CONSUMABLE;
				granted.active = true;
				granted.transactionId = result.transactionId;
				granted.receipt = result.receipt;
				this->applyEntitlement(granted);
			}

			if(it == this->mPendingPurchases.end())
			{
				// nothing to answer, but the entitlement above still landed
				return;
			}
			const PurchaseCallback callback = it->second.onComplete;
			this->mPendingPurchases.erase(it);
			if(callback) { callback(result); }
			return;
		}
		case StoreEvent::SE_RESTORE:
		{
			std::map<MonetizationRequestId, PendingRestore>::iterator it =
				this->mPendingRestores.find(event.id);
			if(it == this->mPendingRestores.end()) { return; }

			RestoreResult result;
			result.id = event.id;
			result.completed = event.completed;
			result.reason = event.reason;

			for(std::size_t i = 0; i < event.entitlements.size(); ++i)
			{
				Entitlement owned = event.entitlements[i];
				// the store listed ITS identifiers with nothing to correlate
				// them against - the reverse index is what makes them nameable
				const String logicalId = this->mCatalog.logicalIdFor(
					this->mStore ? this->mStore->storefront() : SF_UNKNOWN,
					owned.productId);
				if(logicalId.empty())
				{
					oDebugWarn("monetization", 0, "the store restored the "
						"unknown product '" << owned.productId << "'");
					continue;
				}
				owned.productId = logicalId;
				Product const * product = this->mCatalog.find(logicalId);
				if(product != NULL) { owned.kind = product->kind; }
				result.entitlements.push_back(owned);
				this->applyEntitlement(owned);
			}

			const RestoreCallback callback = it->second.onComplete;
			this->mPendingRestores.erase(it);
			if(callback) { callback(result); }
			return;
		}
		}
	}
	//---------------------------------------------------------
	void MonetizationService::deliverAdEvent(AdEvent const & event)
	{
		if(event.kind == AdEvent::AE_LOAD)
		{
			AdPlacement & unit = this->placementFor(event.format,
				event.placement);
			unit.completeLoad(event.loadResult, event.reason);

			std::map<MonetizationRequestId, PendingAdLoad>::iterator it =
				this->mPendingAdLoads.find(event.id);
			if(it == this->mPendingAdLoads.end()) { return; }

			AdLoadOutcome outcome;
			outcome.id = event.id;
			outcome.format = event.format;
			outcome.placement = event.placement;
			outcome.result = event.loadResult;
			outcome.reason = event.reason;

			const AdLoadCallback callback = it->second.onComplete;
			this->mPendingAdLoads.erase(it);
			if(callback) { callback(outcome); }
			return;
		}

		AdPlacement & unit = this->placementFor(event.format, event.placement);
		unit.completeShow(event.showResult, event.reason);

		if(adFormatIsTakeover(event.format))
		{
			// the app is ours again
			this->mTakeoverActive = false;
		}

		std::map<MonetizationRequestId, PendingAdShow>::iterator it =
			this->mPendingAdShows.find(event.id);
		if(it == this->mPendingAdShows.end()) { return; }

		AdShowOutcome outcome;
		outcome.id = event.id;
		outcome.format = event.format;
		outcome.placement = event.placement;
		outcome.result = event.showResult;
		outcome.reason = event.reason;
		if(event.showResult == ASR_REWARD_EARNED)
		{
			// the reward travels ONLY with the earned result, so a dismissal
			// can never carry an amount a game might read
			outcome.rewardId = event.rewardId;
			outcome.rewardAmount = event.rewardAmount;
		}

		const AdShowCallback callback = it->second.onComplete;
		this->mPendingAdShows.erase(it);
		if(callback) { callback(outcome); }
	}
	//---------------------------------------------------------
	void MonetizationService::update()
	{
		if(this->mStore && this->mStoreReady)
		{
			this->mStoreScratch.clear();
			this->mStore->poll(this->mStoreScratch);
			for(std::size_t i = 0; i < this->mStoreScratch.size(); ++i)
			{
				this->deliverStoreEvent(this->mStoreScratch[i]);
			}
		}
		if(this->mAds && this->mAdsReady)
		{
			this->mAdScratch.clear();
			this->mAds->poll(this->mAdScratch);
			for(std::size_t i = 0; i < this->mAdScratch.size(); ++i)
			{
				this->deliverAdEvent(this->mAdScratch[i]);
			}
		}

		// the queued refusals are swapped OUT first, so a callback that
		// submits a new request has its own refusal delivered next frame
		// rather than re-entering this loop
		if(!this->mImmediateProducts.empty())
		{
			std::vector<ProductQueryResult> due;
			due.swap(this->mImmediateProducts);
			for(std::size_t i = 0; i < due.size(); ++i)
			{
				std::map<MonetizationRequestId, PendingProducts>::iterator it =
					this->mPendingProducts.find(due[i].id);
				if(it == this->mPendingProducts.end()) { continue; }
				const ProductQueryCallback callback = it->second.onComplete;
				this->mPendingProducts.erase(it);
				if(callback) { callback(due[i]); }
			}
		}
		if(!this->mImmediatePurchases.empty())
		{
			std::vector<PurchaseResult> due;
			due.swap(this->mImmediatePurchases);
			for(std::size_t i = 0; i < due.size(); ++i)
			{
				std::map<MonetizationRequestId, PendingPurchase>::iterator it =
					this->mPendingPurchases.find(due[i].id);
				if(it == this->mPendingPurchases.end()) { continue; }
				const PurchaseCallback callback = it->second.onComplete;
				this->mPendingPurchases.erase(it);
				if(callback) { callback(due[i]); }
			}
		}
		if(!this->mImmediateRestores.empty())
		{
			std::vector<RestoreResult> due;
			due.swap(this->mImmediateRestores);
			for(std::size_t i = 0; i < due.size(); ++i)
			{
				std::map<MonetizationRequestId, PendingRestore>::iterator it =
					this->mPendingRestores.find(due[i].id);
				if(it == this->mPendingRestores.end()) { continue; }
				const RestoreCallback callback = it->second.onComplete;
				this->mPendingRestores.erase(it);
				if(callback) { callback(due[i]); }
			}
		}
		if(!this->mImmediateAdLoads.empty())
		{
			std::vector<AdLoadOutcome> due;
			due.swap(this->mImmediateAdLoads);
			for(std::size_t i = 0; i < due.size(); ++i)
			{
				std::map<MonetizationRequestId, PendingAdLoad>::iterator it =
					this->mPendingAdLoads.find(due[i].id);
				if(it == this->mPendingAdLoads.end()) { continue; }
				const AdLoadCallback callback = it->second.onComplete;
				this->mPendingAdLoads.erase(it);
				if(callback) { callback(due[i]); }
			}
		}
		if(!this->mImmediateAdShows.empty())
		{
			std::vector<AdShowOutcome> due;
			due.swap(this->mImmediateAdShows);
			for(std::size_t i = 0; i < due.size(); ++i)
			{
				std::map<MonetizationRequestId, PendingAdShow>::iterator it =
					this->mPendingAdShows.find(due[i].id);
				if(it == this->mPendingAdShows.end()) { continue; }
				const AdShowCallback callback = it->second.onComplete;
				this->mPendingAdShows.erase(it);
				if(callback) { callback(due[i]); }
			}
		}
	}
	//---------------------------------------------------------
	std::size_t MonetizationService::pendingCount() const
	{
		return this->mPendingProducts.size()
			+ this->mPendingPurchases.size()
			+ this->mPendingRestores.size()
			+ this->mPendingAdLoads.size()
			+ this->mPendingAdShows.size();
	}
}
