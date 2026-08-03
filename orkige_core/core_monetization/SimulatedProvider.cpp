/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	SimulatedProvider.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_monetization/SimulatedProvider.h"

#include <cstddef>
#include <sstream>
#include <string>

namespace Orkige
{
	namespace
	{
		//! parse a boolean setting value; false when it is not one
		bool parseBool(String const & value, bool & outValue)
		{
			if(value == "true" || value == "1" || value == "on"
				|| value == "yes")
			{
				outValue = true;
				return true;
			}
			if(value == "false" || value == "0" || value == "off"
				|| value == "no")
			{
				outValue = false;
				return true;
			}
			return false;
		}

		//! parse a non-negative integer setting value; false when it is not one
		bool parseUInt(String const & value, unsigned int & outValue)
		{
			if(value.empty()) { return false; }
			unsigned int parsed = 0;
			for(std::size_t i = 0; i < value.size(); ++i)
			{
				const char digit = value[i];
				if(digit < '0' || digit > '9') { return false; }
				parsed = parsed * 10u + static_cast<unsigned int>(digit - '0');
			}
			outValue = parsed;
			return true;
		}

		//! parse a real setting value; false when it is not one
		bool parseDouble(String const & value, double & outValue)
		{
			if(value.empty()) { return false; }
			std::istringstream stream(value);
			double parsed = 0.0;
			stream >> parsed;
			if(stream.fail()) { return false; }
			// a trailing remainder means it was not a number
			std::string rest;
			stream >> rest;
			if(!rest.empty()) { return false; }
			outValue = parsed;
			return true;
		}

		//! split a comma-separated list, dropping empty entries
		StringVector splitList(String const & value)
		{
			StringVector items;
			std::string current;
			for(std::size_t i = 0; i < value.size(); ++i)
			{
				if(value[i] == ',')
				{
					if(!current.empty()) { items.push_back(current); }
					current.clear();
					continue;
				}
				current += value[i];
			}
			if(!current.empty()) { items.push_back(current); }
			return items;
		}

		//! the reason to report, preferring the scenario's own wording
		String reasonOr(String const & scenarioReason, char const * derived)
		{
			return scenarioReason.empty() ? String(derived) : scenarioReason;
		}
	}
	//---------------------------------------------------------
	bool SimulatedScenario::apply(String const & key, String const & value,
		String & outError)
	{
		// a per-format load override: "loadResult.<format>"
		const String loadPrefix = "loadResult.";
		if(key.size() > loadPrefix.size()
			&& key.compare(0, loadPrefix.size(), loadPrefix) == 0)
		{
			const String formatToken = key.substr(loadPrefix.size());
			AdFormat format = AF_BANNER;
			if(!adFormatFromName(formatToken, format))
			{
				outError = "'" + formatToken + "' is not an ad format";
				return false;
			}
			AdLoadResult result = ALR_LOADED;
			if(!adLoadResultFromName(value, result))
			{
				outError = "'" + value + "' is not a load result";
				return false;
			}
			this->loadResultByFormat[format] = result;
			return true;
		}

		if(key == "loadResult")
		{
			AdLoadResult result = ALR_LOADED;
			if(!adLoadResultFromName(value, result))
			{
				outError = "'" + value + "' is not a load result";
				return false;
			}
			this->loadResult = result;
			return true;
		}
		if(key == "showResult")
		{
			AdShowResult result = ASR_COMPLETED;
			if(!adShowResultFromName(value, result))
			{
				outError = "'" + value + "' is not a show result";
				return false;
			}
			this->showResult = result;
			return true;
		}
		if(key == "purchaseState")
		{
			PurchaseState state = PS_PURCHASED;
			if(!purchaseStateFromName(value, state))
			{
				outError = "'" + value + "' is not a purchase state";
				return false;
			}
			this->purchaseState = state;
			return true;
		}
		if(key == "bannerPosition")
		{
			BannerPosition position = BP_BOTTOM;
			if(!bannerPositionFromName(value, position))
			{
				outError = "'" + value + "' is not a banner position";
				return false;
			}
			this->bannerPosition = position;
			return true;
		}
		if(key == "rewardId")
		{
			this->rewardId = value;
			return true;
		}
		if(key == "reason")
		{
			this->reason = value;
			return true;
		}
		if(key == "restoreStoreIds")
		{
			this->restoreStoreIds = splitList(value);
			return true;
		}
		if(key == "rewardAmount")
		{
			double amount = 0.0;
			if(!parseDouble(value, amount))
			{
				outError = "'" + value + "' is not a number";
				return false;
			}
			this->rewardAmount = amount;
			return true;
		}

		unsigned int number = 0;
		if(key == "latencyTicks" || key == "bannerWidth"
			|| key == "bannerHeight")
		{
			if(!parseUInt(value, number))
			{
				outError = "'" + value + "' is not a non-negative integer";
				return false;
			}
			if(key == "latencyTicks") { this->latencyTicks = number; }
			else if(key == "bannerWidth") { this->bannerWidth = number; }
			else { this->bannerHeight = number; }
			return true;
		}

		bool flag = false;
		if(key == "adInitializeFails" || key == "storeInitializeFails"
			|| key == "productsUnavailable" || key == "restoreFails"
			|| key == "bannerInsideSafeArea")
		{
			if(!parseBool(value, flag))
			{
				outError = "'" + value + "' is not a boolean";
				return false;
			}
			if(key == "adInitializeFails") { this->adInitializeFails = flag; }
			else if(key == "storeInitializeFails")
			{
				this->storeInitializeFails = flag;
			}
			else if(key == "productsUnavailable")
			{
				this->productsUnavailable = flag;
			}
			else if(key == "restoreFails") { this->restoreFails = flag; }
			else { this->bannerInsideSafeArea = flag; }
			return true;
		}

		outError = "'" + key + "' is not a scenario setting";
		return false;
	}
	//---------------------------------------------------------
	//--- the simulated store -------------------------------------------------
	//---------------------------------------------------------
	SimulatedStoreProvider::SimulatedStoreProvider(StorefrontId storefront)
		: mStorefront(storefront)
		, mStarted(false)
		, mTransactionCounter(0)
	{
	}
	//---------------------------------------------------------
	SimulatedStoreProvider::~SimulatedStoreProvider()
	{
	}
	//---------------------------------------------------------
	char const * SimulatedStoreProvider::name() const
	{
		return "simulated";
	}
	//---------------------------------------------------------
	StorefrontId SimulatedStoreProvider::storefront() const
	{
		return this->mStorefront;
	}
	//---------------------------------------------------------
	bool SimulatedStoreProvider::initialize()
	{
		if(this->mScenario.storeInitializeFails)
		{
			this->mStarted = false;
			return false;
		}
		this->mStarted = true;
		return true;
	}
	//---------------------------------------------------------
	void SimulatedStoreProvider::shutdown()
	{
		this->mStarted = false;
		this->mQueue.clear();
	}
	//---------------------------------------------------------
	void SimulatedStoreProvider::schedule(StoreEvent const & event)
	{
		Scheduled entry;
		entry.remainingTicks = this->mScenario.latencyTicks;
		entry.event = event;
		this->mQueue.push_back(entry);
	}
	//---------------------------------------------------------
	void SimulatedStoreProvider::requestProducts(MonetizationRequestId id,
		StringVector const & storeIds)
	{
		StoreEvent event;
		event.kind = StoreEvent::SE_PRODUCTS;
		event.id = id;

		if(this->mScenario.productsUnavailable)
		{
			event.completed = false;
			event.reason = reasonOr(this->mScenario.reason,
				"the storefront could not be reached");
			this->schedule(event);
			return;
		}

		event.completed = true;
		for(std::size_t i = 0; i < storeIds.size(); ++i)
		{
			// a storefront answers in ITS identifiers, with ITS localised
			// metadata - the whole reason the catalog keeps a reverse index
			Product product;
			product.id = storeIds[i];
			product.title = storeIds[i];
			product.description = "a simulated product";
			product.displayPrice = "0.99";
			product.priceCurrency = "USD";
			product.priceValue = 0.99;
			product.available = true;
			event.products.push_back(product);
		}
		this->schedule(event);
	}
	//---------------------------------------------------------
	void SimulatedStoreProvider::purchase(MonetizationRequestId id,
		String const & storeId)
	{
		StoreEvent event;
		event.kind = StoreEvent::SE_PURCHASE;
		event.id = id;
		event.storeId = storeId;
		event.purchaseState = this->mScenario.purchaseState;
		event.completed = true;

		switch(event.purchaseState)
		{
		case PS_PURCHASED:
		case PS_ALREADY_OWNED:
		{
			std::ostringstream handle;
			handle << "sim-txn-" << (++this->mTransactionCounter);
			event.transactionId = handle.str();
			event.receipt = "sim-receipt:" + storeId;
			break;
		}
		case PS_PENDING:
			// a deferred purchase HAS a transaction: it exists, it is simply
			// not settled, and a game that treats it as a failure loses the
			// sale when the approval lands
			{
				std::ostringstream handle;
				handle << "sim-txn-" << (++this->mTransactionCounter);
				event.transactionId = handle.str();
			}
			event.reason = reasonOr(this->mScenario.reason,
				"the purchase is awaiting approval");
			break;
		case PS_CANCELLED:
			event.reason = reasonOr(this->mScenario.reason,
				"the player dismissed the payment sheet");
			break;
		case PS_DECLINED:
			event.reason = reasonOr(this->mScenario.reason,
				"the payment was declined");
			break;
		case PS_UNAVAILABLE:
			event.reason = reasonOr(this->mScenario.reason,
				"the product is not available on this storefront");
			break;
		case PS_FAILED:
		default:
			event.reason = reasonOr(this->mScenario.reason,
				"the purchase failed");
			break;
		}
		this->schedule(event);
	}
	//---------------------------------------------------------
	void SimulatedStoreProvider::restore(MonetizationRequestId id)
	{
		StoreEvent event;
		event.kind = StoreEvent::SE_RESTORE;
		event.id = id;

		if(this->mScenario.restoreFails)
		{
			event.completed = false;
			event.reason = reasonOr(this->mScenario.reason,
				"the store could not be reached");
			this->schedule(event);
			return;
		}

		event.completed = true;
		for(std::size_t i = 0; i < this->mScenario.restoreStoreIds.size(); ++i)
		{
			Entitlement owned;
			// STOREFRONT identifiers, as a real store hands them back
			owned.productId = this->mScenario.restoreStoreIds[i];
			owned.kind = PK_NON_CONSUMABLE;
			owned.active = true;
			owned.transactionId = "sim-restored-" + owned.productId;
			owned.receipt = "sim-receipt:" + owned.productId;
			event.entitlements.push_back(owned);
		}
		this->schedule(event);
	}
	//---------------------------------------------------------
	void SimulatedStoreProvider::finishTransaction(String const & transactionId)
	{
		this->mFinished.push_back(transactionId);
	}
	//---------------------------------------------------------
	void SimulatedStoreProvider::poll(std::vector<StoreEvent> & out)
	{
		if(!this->mStarted) { return; }

		std::vector<Scheduled> waiting;
		waiting.reserve(this->mQueue.size());
		for(std::size_t i = 0; i < this->mQueue.size(); ++i)
		{
			if(this->mQueue[i].remainingTicks == 0)
			{
				out.push_back(this->mQueue[i].event);
				continue;
			}
			Scheduled entry = this->mQueue[i];
			--entry.remainingTicks;
			waiting.push_back(entry);
		}
		this->mQueue.swap(waiting);
	}
	//---------------------------------------------------------
	//--- the simulated ad surface --------------------------------------------
	//---------------------------------------------------------
	SimulatedAdProvider::SimulatedAdProvider()
		: mStarted(false)
		, mTestMode(false)
		, mBannerVisible(false)
	{
	}
	//---------------------------------------------------------
	SimulatedAdProvider::~SimulatedAdProvider()
	{
	}
	//---------------------------------------------------------
	char const * SimulatedAdProvider::name() const
	{
		return "simulated";
	}
	//---------------------------------------------------------
	bool SimulatedAdProvider::initialize(ConsentState const & consent,
		bool testMode)
	{
		// the consent is RECORDED, so a test can prove the surface was never
		// brought up uninformed
		this->mConsent = consent;
		this->mTestMode = testMode;
		if(this->mScenario.adInitializeFails)
		{
			this->mStarted = false;
			return false;
		}
		this->mStarted = true;
		return true;
	}
	//---------------------------------------------------------
	void SimulatedAdProvider::shutdown()
	{
		this->mStarted = false;
		this->mBannerVisible = false;
		this->mQueue.clear();
	}
	//---------------------------------------------------------
	void SimulatedAdProvider::onConsentChanged(ConsentState const & consent)
	{
		this->mConsent = consent;
	}
	//---------------------------------------------------------
	void SimulatedAdProvider::schedule(AdEvent const & event)
	{
		Scheduled entry;
		entry.remainingTicks = this->mScenario.latencyTicks;
		entry.event = event;
		this->mQueue.push_back(entry);
	}
	//---------------------------------------------------------
	AdLoadResult SimulatedAdProvider::loadResultFor(AdFormat format) const
	{
		std::map<AdFormat, AdLoadResult>::const_iterator it =
			this->mScenario.loadResultByFormat.find(format);
		return (it == this->mScenario.loadResultByFormat.end())
			? this->mScenario.loadResult
			: it->second;
	}
	//---------------------------------------------------------
	void SimulatedAdProvider::load(MonetizationRequestId id, AdFormat format,
		String const & placement)
	{
		AdEvent event;
		event.kind = AdEvent::AE_LOAD;
		event.id = id;
		event.format = format;
		event.placement = placement;
		event.loadResult = this->loadResultFor(format);

		switch(event.loadResult)
		{
		case ALR_LOADED:
			break;
		case ALR_NO_FILL:
			// THE ONE that breaks shipped games: a perfectly valid request the
			// network simply had nothing to answer with
			event.reason = reasonOr(this->mScenario.reason,
				"no advert was available for this request");
			break;
		case ALR_TIMEOUT:
			event.reason = reasonOr(this->mScenario.reason,
				"the network did not answer in time");
			break;
		default:
			event.reason = reasonOr(this->mScenario.reason,
				"the network reported a failure");
			break;
		}
		this->schedule(event);
	}
	//---------------------------------------------------------
	void SimulatedAdProvider::show(MonetizationRequestId id, AdFormat format,
		String const & placement)
	{
		AdEvent event;
		event.kind = AdEvent::AE_SHOW;
		event.id = id;
		event.format = format;
		event.placement = placement;
		event.showResult = this->mScenario.showResult;

		// the configured reward is reported on EVERY outcome, exactly as a real
		// mediation surface does: the reward is a property of the ad unit, and
		// "closed" and "rewarded" arrive as SEPARATE signals there. Deciding
		// which outcome is allowed to carry it is the SEAM's job, not the
		// provider's - so the guard has one home and this simulator can prove
		// it works rather than quietly doing it a second time.
		event.rewardId = this->mScenario.rewardId;
		event.rewardAmount = this->mScenario.rewardAmount;
		if(event.showResult == ASR_ERROR)
		{
			event.reason = reasonOr(this->mScenario.reason,
				"the advert could not be presented");
		}

		if(format == AF_BANNER && event.showResult != ASR_ERROR)
		{
			// the strip is on screen from here on, and the geometry says so
			// even though nothing was ever really drawn into it
			this->mBannerVisible = true;
		}
		this->schedule(event);
	}
	//---------------------------------------------------------
	void SimulatedAdProvider::hideBanner()
	{
		this->mBannerVisible = false;
	}
	//---------------------------------------------------------
	BannerGeometry SimulatedAdProvider::bannerGeometry() const
	{
		BannerGeometry geometry;
		geometry.visible = this->mBannerVisible;
		geometry.position = this->mScenario.bannerPosition;
		geometry.insideSafeArea = this->mScenario.bannerInsideSafeArea;
		if(this->mBannerVisible)
		{
			geometry.width = this->mScenario.bannerWidth;
			geometry.height = this->mScenario.bannerHeight;
		}
		return geometry;
	}
	//---------------------------------------------------------
	void SimulatedAdProvider::poll(std::vector<AdEvent> & out)
	{
		if(!this->mStarted) { return; }

		std::vector<Scheduled> waiting;
		waiting.reserve(this->mQueue.size());
		for(std::size_t i = 0; i < this->mQueue.size(); ++i)
		{
			if(this->mQueue[i].remainingTicks == 0)
			{
				out.push_back(this->mQueue[i].event);
				continue;
			}
			Scheduled entry = this->mQueue[i];
			--entry.remainingTicks;
			waiting.push_back(entry);
		}
		this->mQueue.swap(waiting);
	}
}
