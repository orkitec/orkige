/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	PlatformStoreApple.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The macOS + iOS in-app purchase surface behind the StoreProvider seam. This
// is the ONE translation unit that touches the platform's purchase framework;
// MonetizationService and every caller above it stay plain C++, the same split
// as core_http/HttpBackendApple.mm and engine_input/HapticBridgeApple.mm.
//
// WHY THE OLDER OF THE PLATFORM'S TWO PURCHASE APIS: the newer one is published
// for a language this engine does not compile and cannot call into from C++ or
// Objective-C++ without adding that toolchain to every Apple target. The older
// API is fully supported, reaches the same storefront, and - the reason it is
// not merely an acceptable fallback here - it is built around a PERSISTENT
// PAYMENT QUEUE, which is precisely the mechanism that closes the
// unacknowledged-purchase window (@see StoreTransactionLedger). Its symbols
// carry deprecation attributes pointing at the newer API, so they are silenced
// for this file alone rather than at the call sites.
//
// WHAT IS DELIBERATELY ABSENT: subscription expiry (the receipt carries it, and
// reading it means parsing a signed container that only a server should trust),
// promotional offers, and price-tier introspection. Each is a real feature; the
// bridge stays a transaction pump until one of them earns its keep.

#include "core_monetization/PlatformStore.h"

#ifdef ORKIGE_STORE_APPLE

#include "core_debug/DebugMacros.h"
#include "core_monetization/StoreTransactionCore.h"

#import <Foundation/Foundation.h>
#import <StoreKit/StoreKit.h>

#include <map>
#include <mutex>
#include <vector>

#if __has_feature(objc_arc)
#	define ORKIGE_STORE_RELEASE(object)	((void)0)
#	define ORKIGE_STORE_RETAIN(object)	(object)
#else
#	define ORKIGE_STORE_RELEASE(object)	[(object) release]
#	define ORKIGE_STORE_RETAIN(object)	[(object) retain]
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

// The pure translation tables in StoreTransactionCore.cpp carry these numbers
// as literals so a unit test can drive them on a machine with no store account.
// These assertions are what keeps that honest: the ONE translation unit that
// has the platform headers checks every constant against them, so a renumbered
// enum is a BUILD FAILURE rather than a purchase reported as a failure.
static_assert(static_cast<int>(SKPaymentTransactionStatePurchasing) == 0,
	"the payment queue renumbered its transaction states");
static_assert(static_cast<int>(SKPaymentTransactionStatePurchased) == 1,
	"the payment queue renumbered its transaction states");
static_assert(static_cast<int>(SKPaymentTransactionStateFailed) == 2,
	"the payment queue renumbered its transaction states");
static_assert(static_cast<int>(SKPaymentTransactionStateRestored) == 3,
	"the payment queue renumbered its transaction states");
static_assert(static_cast<int>(SKPaymentTransactionStateDeferred) == 4,
	"the payment queue renumbered its transaction states");

static_assert(static_cast<int>(SKErrorUnknown) == 0,
	"the store renumbered its error codes");
static_assert(static_cast<int>(SKErrorClientInvalid) == 1,
	"the store renumbered its error codes");
static_assert(static_cast<int>(SKErrorPaymentCancelled) == 2,
	"the store renumbered its error codes");
static_assert(static_cast<int>(SKErrorPaymentInvalid) == 3,
	"the store renumbered its error codes");
static_assert(static_cast<int>(SKErrorPaymentNotAllowed) == 4,
	"the store renumbered its error codes");
static_assert(static_cast<int>(SKErrorStoreProductNotAvailable) == 5,
	"the store renumbered its error codes");
static_assert(static_cast<int>(SKErrorCloudServiceNetworkConnectionFailed) == 7,
	"the store renumbered its error codes");

namespace Orkige
{
	class AppleStoreProvider;
}

//! @brief the payment-queue observer and product-request delegate: a thin
//! forwarder into the provider, which owns it and outlives it.
@interface OrkigeStoreObserver : NSObject
	<SKPaymentTransactionObserver, SKProductsRequestDelegate>
{
@public
	Orkige::AppleStoreProvider * provider;	//!< non-owning, outlives us
}
@end

namespace Orkige
{
	namespace
	{
		//! the storefront column this platform's store reads
#if TARGET_OS_IPHONE
		const StorefrontId APPLE_STOREFRONT = SF_IOS;
#else
		const StorefrontId APPLE_STOREFRONT = SF_MACOS;
#endif

		//! NSString -> String, nil-safe
		String toString(NSString * text)
		{
			if(text == nil) { return ""; }
			char const * utf8 = [text UTF8String];
			return (utf8 != NULL) ? String(utf8) : String();
		}

		//! @brief does this process have an app identity at all?
		//! @remarks A store resolves products against the app record the
		//! identifier names. A bare command-line binary (every unit test and
		//! most ctests) has none, which is why the whole surface refuses BY NAME
		//! instead of reaching for a payment queue that can only answer nothing.
		bool hasAppIdentity()
		{
			return [[NSBundle mainBundle] bundleIdentifier] != nil;
		}

		//! @brief the app receipt, base64 encoded - the opaque token a SERVER
		//! validates (@see Docs/monetization.md on why an on-device check is
		//! not a proof of payment). Empty when the app carries none, which is
		//! ordinary in development.
		String appReceipt()
		{
			NSURL * url = [[NSBundle mainBundle] appStoreReceiptURL];
			if(url == nil) { return ""; }

			NSData * data = [NSData dataWithContentsOfURL:url];
			if(data == nil) { return ""; }
			return toString([data base64EncodedStringWithOptions:0]);
		}

		//! the store's own wording for an error, or our neutral fallback
		String reasonForError(NSError * error, StoreFailureCode code)
		{
			const String platformWording = (error != nil)
				? toString([error localizedDescription]) : String();
			if(!platformWording.empty()) { return platformWording; }
			return storeFailureReason(code);
		}

		//! the localised, currency-correct price string the storefront quoted
		String displayPriceFor(SKProduct * product)
		{
			NSNumberFormatter * formatter = [[NSNumberFormatter alloc] init];
			[formatter setFormatterBehavior:NSNumberFormatterBehavior10_4];
			[formatter setNumberStyle:NSNumberFormatterCurrencyStyle];
			[formatter setLocale:[product priceLocale]];
			NSString * text = [formatter stringFromNumber:[product price]];
			const String price = toString(text);
			ORKIGE_STORE_RELEASE(formatter);
			return price;
		}
	}

	//! @brief the platform's purchase surface as an ordinary StoreProvider.
	//!
	//! THE SHAPE THAT MATTERS: this class is a TRANSACTION PUMP, not a record of
	//! what the player owns. The platform's payment queue is the record - a
	//! settled transaction stays in it until it is explicitly finished and is
	//! re-delivered on every launch until then. That is what makes the
	//! unacknowledged-purchase window survivable, and it is why the observer is
	//! attached in initialize() BEFORE anything else happens: transactions a
	//! previous run died before acknowledging arrive immediately, through the
	//! ordinary unsolicited path, and are granted again.
	class AppleStoreProvider : public StoreProvider
	{
		//--- Variables ---------------------------------------
	private:
		//! guards everything below (the platform may answer off the main thread)
		std::mutex						mMutex;
		//! the payment-queue observer, or nil while we are down
		OrkigeStoreObserver *			mObserver;
		//! events awaiting the main thread's poll()
		std::vector<StoreEvent>			mQueue;

		//! the unacknowledged-purchase window, as bookkeeping
		StoreTransactionLedger			mLedger;
		//! which submitted purchase a delivery answers
		StorePurchaseRequestBook		mRequests;
		//! the retained transactions the ledger's open set names
		std::map<String, SKPaymentTransaction *>	mUnfinished;
		//! the products a completed query returned, keyed by storefront id
		std::map<String, SKProduct *>	mProducts;

		//! the product query in flight, or nil
		SKProductsRequest *				mProductsRequest;
		//! the request id that query answers
		MonetizationRequestId			mProductsRequestId;
		//! the restore in flight (0 = none)
		MonetizationRequestId			mRestoreRequestId;
		//! entitlements a restore has collected so far
		std::vector<Entitlement>		mRestoreBatch;

		//! initialize() ran
		bool							mStarted;
		//! @brief why this process cannot reach the store at all ("" = it can).
		//! Set ONCE at initialize() and repeated verbatim in every refusal.
		String							mBlockedReason;
		//--- Methods -----------------------------------------
	public:
		AppleStoreProvider()
			: mObserver(nil)
			, mProductsRequest(nil)
			, mProductsRequestId(0)
			, mRestoreRequestId(0)
			, mStarted(false)
		{
		}
		virtual ~AppleStoreProvider()
		{
			this->shutdown();
		}

		virtual char const * name() const { return "apple-store"; }
		virtual StorefrontId storefront() const { return APPLE_STOREFRONT; }

		//! @brief bring the purchase surface up.
		//!
		//! THIS ALWAYS SUCCEEDS ON THIS PLATFORM, and that is deliberate. A
		//! false here would leave the service refusing every call with its one
		//! generic "the store is not available", collapsing several very
		//! different developer-facing situations - no app identity, payments
		//! restricted on the device, no products configured, a product not
		//! fetched yet - into a single unactionable sentence. Coming up and
		//! refusing each operation BY NAME is what makes those distinguishable.
		virtual bool initialize()
		{
			if(this->mStarted) { return true; }
			this->mStarted = true;

			if(!hasAppIdentity())
			{
				// no payment queue is touched at all in this state: it could
				// only answer nothing, and reaching for it from a bare binary
				// buys a confusing platform log line instead of our reason
				this->mBlockedReason = "this process has no app identity, so "
					"the store cannot resolve any product - run the packaged "
					"app rather than a bare executable";
				oDebugWarn("monetization", 0, "the platform store is up but "
					"blocked: " << this->mBlockedReason);
				return true;
			}

			// THE OBSERVER GOES ON FIRST, BEFORE ANY PURCHASE IS MADE. Whatever
			// the previous run left unacknowledged is re-delivered the moment it
			// attaches, which is the whole crash-safety story.
			this->mObserver = [[OrkigeStoreObserver alloc] init];
			this->mObserver->provider = this;
			[[SKPaymentQueue defaultQueue] addTransactionObserver:this->mObserver];
			return true;
		}

		virtual void shutdown()
		{
			if(!this->mStarted) { return; }
			this->mStarted = false;

			OrkigeStoreObserver * observer = nil;
			SKProductsRequest * productsRequest = nil;
			std::vector<SKPaymentTransaction *> transactions;
			std::vector<SKProduct *> products;
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				observer = this->mObserver;
				this->mObserver = nil;
				productsRequest = this->mProductsRequest;
				this->mProductsRequest = nil;
				this->mProductsRequestId = 0;
				this->mRestoreRequestId = 0;
				this->mRestoreBatch.clear();
				this->mQueue.clear();
				this->mRequests.clear();

				// the LEDGER is dropped; the platform's queue is NOT. Every
				// transaction still open stays open at the store and is
				// re-delivered next launch - which is exactly the behaviour a
				// crash relies on, so teardown must not differ from it.
				this->mLedger.clear();
				for(std::map<String, SKPaymentTransaction *>::iterator it =
					this->mUnfinished.begin(); it != this->mUnfinished.end();
					++it)
				{
					transactions.push_back(it->second);
				}
				this->mUnfinished.clear();
				for(std::map<String, SKProduct *>::iterator it =
					this->mProducts.begin(); it != this->mProducts.end(); ++it)
				{
					products.push_back(it->second);
				}
				this->mProducts.clear();
			}

			if(observer != nil)
			{
				[[SKPaymentQueue defaultQueue]
					removeTransactionObserver:observer];
				observer->provider = NULL;
				ORKIGE_STORE_RELEASE(observer);
			}
			if(productsRequest != nil)
			{
				[productsRequest setDelegate:nil];
				[productsRequest cancel];
				ORKIGE_STORE_RELEASE(productsRequest);
			}
			for(std::size_t i = 0; i < transactions.size(); ++i)
			{
				ORKIGE_STORE_RELEASE(transactions[i]);
			}
			for(std::size_t i = 0; i < products.size(); ++i)
			{
				ORKIGE_STORE_RELEASE(products[i]);
			}
		}

		virtual void requestProducts(MonetizationRequestId id,
			StringVector const & storeIds)
		{
			if(!this->mBlockedReason.empty())
			{
				this->publishProductsRefusal(id, this->mBlockedReason);
				return;
			}
			if(storeIds.empty())
			{
				// THE "nothing is configured" REFUSAL. An empty answer here
				// looks exactly like a store that knows none of our products,
				// and a developer would go hunting in the wrong console.
				this->publishProductsRefusal(id, "no products are configured "
					"for the " + storefrontName(APPLE_STOREFRONT) + " storefront"
					" - add them to the project's product catalog");
				return;
			}
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				if(this->mProductsRequest != nil)
				{
					this->publishLocked(this->productsRefusal(id,
						"a product query is already in flight"));
					return;
				}
			}

			NSMutableSet * identifiers = [NSMutableSet setWithCapacity:
				storeIds.size()];
			for(std::size_t i = 0; i < storeIds.size(); ++i)
			{
				[identifiers addObject:[NSString
					stringWithUTF8String:storeIds[i].c_str()]];
			}

			SKProductsRequest * request = [[SKProductsRequest alloc]
				initWithProductIdentifiers:identifiers];
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				this->mProductsRequest = request;
				this->mProductsRequestId = id;
			}
			[request setDelegate:this->mObserver];
			[request start];
		}

		virtual void purchase(MonetizationRequestId id, String const & storeId)
		{
			if(!this->mBlockedReason.empty())
			{
				this->publishPurchaseRefusal(id, storeId, PS_UNAVAILABLE,
					this->mBlockedReason);
				return;
			}
			if(![SKPaymentQueue canMakePayments])
			{
				// a device restriction, a managed account - the player is not
				// at fault and neither is the product, so it is named as itself
				this->publishPurchaseRefusal(id, storeId, PS_DECLINED,
					"this device is not allowed to make payments");
				return;
			}

			SKProduct * product = nil;
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				std::map<String, SKProduct *>::iterator it =
					this->mProducts.find(storeId);
				if(it != this->mProducts.end()) { product = it->second; }
			}
			if(product == nil)
			{
				// a payment is made against a product the STORE returned, so a
				// product query has to have completed first. Saying so beats
				// the store's own generic failure minutes later.
				this->publishPurchaseRefusal(id, storeId, PS_UNAVAILABLE,
					"'" + storeId + "' has not been fetched from the store yet"
					" - complete a product query before buying");
				return;
			}

			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				this->mRequests.submit(id, storeId);
			}
			[[SKPaymentQueue defaultQueue]
				addPayment:[SKPayment paymentWithProduct:product]];
		}

		virtual void restore(MonetizationRequestId id)
		{
			if(!this->mBlockedReason.empty())
			{
				this->publishRestoreRefusal(id, this->mBlockedReason);
				return;
			}
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				if(this->mRestoreRequestId != 0)
				{
					this->publishLocked(this->restoreRefusal(id,
						"a restore is already in flight"));
					return;
				}
				this->mRestoreRequestId = id;
				this->mRestoreBatch.clear();
			}
			[[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
		}

		//! @brief acknowledge a settled transaction - THE LAST STEP, and only
		//! ever after the goods are durably granted (@see StoreTransactionLedger
		//! on why the order is load-bearing).
		virtual void finishTransaction(String const & transactionId)
		{
			SKPaymentTransaction * transaction = nil;
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				if(!this->mLedger.acknowledge(transactionId))
				{
					// never a silent no-op: finishing something that was never
					// delivered means the game is tracking transactions the
					// store does not agree with
					oDebugWarn("monetization", 0, "finishTransaction('"
						<< transactionId << "'): no transaction is waiting to "
						"be acknowledged under that id");
					return;
				}
				std::map<String, SKPaymentTransaction *>::iterator it =
					this->mUnfinished.find(transactionId);
				if(it != this->mUnfinished.end())
				{
					transaction = it->second;
					this->mUnfinished.erase(it);
				}
			}
			if(transaction == nil) { return; }

			[[SKPaymentQueue defaultQueue] finishTransaction:transaction];
			ORKIGE_STORE_RELEASE(transaction);
		}

		virtual void poll(std::vector<StoreEvent> & out)
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			out.swap(this->mQueue);
			this->mQueue.clear();
		}

		//--- the observer's forwarding surface ---------------

		//! one batch of transaction updates from the payment queue
		void onTransactionsUpdated(NSArray<SKPaymentTransaction *> * transactions)
		{
			for(SKPaymentTransaction * transaction in transactions)
			{
				this->onTransaction(transaction);
			}
		}

		//! a product query answered
		void onProductsReceived(SKProductsResponse * response)
		{
			StoreEvent event;
			event.kind = StoreEvent::SE_PRODUCTS;
			event.completed = true;

			std::vector<SKProduct *> retained;
			for(SKProduct * product in [response products])
			{
				Product entry;
				entry.id = toString([product productIdentifier]);
				entry.title = toString([product localizedTitle]);
				entry.description = toString([product localizedDescription]);
				entry.displayPrice = displayPriceFor(product);
				entry.priceCurrency = toString([[product priceLocale]
					objectForKey:NSLocaleCurrencyCode]);
				entry.priceValue = [[product price] doubleValue];
				entry.available = true;
				// kind and grantsNoAds belong to the CATALOG, which the service
				// re-applies - a storefront has no opinion about either
				event.products.push_back(entry);
				retained.push_back(ORKIGE_STORE_RETAIN(product));
			}
			for(NSString * unknown in [response invalidProductIdentifiers])
			{
				// almost always a console misconfiguration, and worth being
				// loud about while there is still a developer watching
				event.unknownStoreIds.push_back(toString(unknown));
			}

			std::vector<SKProduct *> replaced;
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				event.id = this->mProductsRequestId;
				this->mProductsRequestId = 0;
				this->clearProductsRequestLocked();

				for(std::size_t i = 0; i < retained.size(); ++i)
				{
					const String storeId = toString(
						[retained[i] productIdentifier]);
					std::map<String, SKProduct *>::iterator it =
						this->mProducts.find(storeId);
					if(it != this->mProducts.end())
					{
						replaced.push_back(it->second);
						it->second = retained[i];
						continue;
					}
					this->mProducts[storeId] = retained[i];
				}
				this->publishLocked(event);
			}
			for(std::size_t i = 0; i < replaced.size(); ++i)
			{
				ORKIGE_STORE_RELEASE(replaced[i]);
			}
		}

		//! a product query failed outright
		void onProductsFailed(NSError * error)
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			const MonetizationRequestId id = this->mProductsRequestId;
			this->mProductsRequestId = 0;
			this->clearProductsRequestLocked();
			if(id == 0) { return; }
			this->publishLocked(this->productsRefusal(id,
				reasonForError(error, SFC_UNKNOWN)));
		}

		//! a restore finished walking the account's purchase history
		void onRestoreFinished()
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			const MonetizationRequestId id = this->mRestoreRequestId;
			this->mRestoreRequestId = 0;
			if(id == 0) { return; }

			StoreEvent event;
			event.kind = StoreEvent::SE_RESTORE;
			event.id = id;
			// AN EMPTY RESTORE IS A SUCCESS: a player who never bought anything
			// restores nothing, and reporting that as a failure teaches games to
			// show an error to an innocent player
			event.completed = true;
			event.entitlements = this->mRestoreBatch;
			this->mRestoreBatch.clear();
			this->publishLocked(event);
		}

		//! a restore could not be performed
		void onRestoreFailed(NSError * error)
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			const MonetizationRequestId id = this->mRestoreRequestId;
			this->mRestoreRequestId = 0;
			this->mRestoreBatch.clear();
			if(id == 0) { return; }

			const StoreFailureCode code = failureFromError(error);
			this->publishLocked(this->restoreRefusal(id,
				reasonForError(error, code)));
		}
	private:
		//! the store's error code in the engine's own vocabulary
		static StoreFailureCode failureFromError(NSError * error)
		{
			if(error == nil) { return SFC_UNKNOWN; }
			if(![[error domain] isEqualToString:SKErrorDomain])
			{
				// not the store's own domain: a transport or a system error
				return SFC_UNKNOWN;
			}
			return appleStoreFailureFromRaw(static_cast<int>([error code]));
		}

		//! ONE delivered transaction, in whatever state the queue reports it
		void onTransaction(SKPaymentTransaction * transaction)
		{
			StoreTransactionPhase phase = STP_PURCHASING;
			const int rawState = static_cast<int>([transaction transactionState]);
			if(!appleStorePhaseFromRaw(rawState, phase))
			{
				// NEVER finished: acknowledging a transaction this build does
				// not understand would tell the store goods were delivered that
				// nobody granted
				oDebugWarn("monetization", 0, "the payment queue reported the "
					"unknown transaction state " << rawState
					<< " - it is left unacknowledged");
				return;
			}
			if(phase == STP_PURCHASING) { return; }

			const String storeId = toString(
				[[transaction payment] productIdentifier]);

			if(phase == STP_FAILED)
			{
				this->onTransactionFailed(transaction, storeId);
				return;
			}
			if(phase == STP_DEFERRED)
			{
				// DEFERRED grants nothing and finishes nothing: it may settle
				// minutes or days later, in a later session, and will arrive
				// then as an unsolicited settlement
				StoreEvent event;
				event.kind = StoreEvent::SE_PURCHASE;
				event.storeId = storeId;
				event.purchaseState = PS_PENDING;
				event.completed = true;
				event.reason = "the purchase is waiting for approval";
				{
					std::lock_guard<std::mutex> lock(this->mMutex);
					event.id = this->mRequests.claim(storeId);
					this->publishLocked(event);
				}
				return;
			}

			this->onTransactionSettled(transaction, storeId, phase);
		}

		//! a transaction that ended without a purchase
		void onTransactionFailed(SKPaymentTransaction * transaction,
			String const & storeId)
		{
			NSError * error = [transaction error];
			const StoreFailureCode code = failureFromError(error);

			StoreEvent event;
			event.kind = StoreEvent::SE_PURCHASE;
			event.storeId = storeId;
			event.purchaseState = purchaseStateForPhase(STP_FAILED, code);
			event.completed = true;
			event.reason = reasonForError(error, code);
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				event.id = this->mRequests.claim(storeId);
				this->publishLocked(event);
			}

			// A FAILURE IS FINISHED IMMEDIATELY, and that asymmetry is the
			// point: there are no goods to grant, so there is no window to
			// protect - while leaving it in the queue would re-deliver the same
			// failure on every launch forever.
			[[SKPaymentQueue defaultQueue] finishTransaction:transaction];
		}

		//! a transaction the store settled: money changed hands, or it is
		//! handing back something this account already paid for
		void onTransactionSettled(SKPaymentTransaction * transaction,
			String const & storeId, StoreTransactionPhase phase)
		{
			const String transactionId = toString(
				[transaction transactionIdentifier]);
			if(transactionId.empty())
			{
				oDebugWarn("monetization", 0, "the store settled '" << storeId
					<< "' with no transaction identifier - it cannot be "
					"acknowledged and is left in the queue");
				return;
			}

			StoreTransactionRecord record;
			record.transactionId = transactionId;
			record.storeId = storeId;
			record.phase = phase;

			StoreEvent event;
			event.kind = StoreEvent::SE_PURCHASE;
			event.storeId = storeId;
			event.transactionId = transactionId;
			event.receipt = appReceipt();
			event.purchaseState = purchaseStateForPhase(phase, SFC_NONE);
			event.completed = true;

			SKPaymentTransaction * retained = ORKIGE_STORE_RETAIN(transaction);
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				record.requestId = this->mRequests.claim(storeId);
				event.id = record.requestId;

				if(!this->mLedger.note(record))
				{
					// the SAME transaction delivered twice inside one session.
					// The game hears about it once; the retained copy it is
					// already holding stays the one it will finish.
					ORKIGE_STORE_RELEASE(retained);
					return;
				}
				this->mUnfinished[transactionId] = retained;

				if(phase == STP_RESTORED && this->mRestoreRequestId != 0)
				{
					// part of the restore that is running: it belongs in that
					// answer, not in a purchase nobody asked for
					Entitlement owned;
					owned.productId = storeId;
					owned.active = true;
					owned.transactionId = transactionId;
					owned.receipt = event.receipt;
					this->mRestoreBatch.push_back(owned);
					return;
				}
				this->publishLocked(event);
			}
		}

		//! CALLER HOLDS mMutex
		void clearProductsRequestLocked()
		{
			if(this->mProductsRequest == nil) { return; }
			[this->mProductsRequest setDelegate:nil];
			ORKIGE_STORE_RELEASE(this->mProductsRequest);
			this->mProductsRequest = nil;
		}

		//! CALLER HOLDS mMutex
		void publishLocked(StoreEvent const & event)
		{
			this->mQueue.push_back(event);
		}

		StoreEvent productsRefusal(MonetizationRequestId id,
			String const & reason) const
		{
			StoreEvent event;
			event.kind = StoreEvent::SE_PRODUCTS;
			event.id = id;
			event.completed = false;
			event.reason = reason;
			return event;
		}
		StoreEvent restoreRefusal(MonetizationRequestId id,
			String const & reason) const
		{
			StoreEvent event;
			event.kind = StoreEvent::SE_RESTORE;
			event.id = id;
			event.completed = false;
			event.reason = reason;
			return event;
		}
		void publishProductsRefusal(MonetizationRequestId id,
			String const & reason)
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			this->publishLocked(this->productsRefusal(id, reason));
		}
		void publishRestoreRefusal(MonetizationRequestId id,
			String const & reason)
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			this->publishLocked(this->restoreRefusal(id, reason));
		}
		void publishPurchaseRefusal(MonetizationRequestId id,
			String const & storeId, PurchaseState state, String const & reason)
		{
			StoreEvent event;
			event.kind = StoreEvent::SE_PURCHASE;
			event.id = id;
			event.storeId = storeId;
			event.purchaseState = state;
			event.completed = true;
			event.reason = reason;

			std::lock_guard<std::mutex> lock(this->mMutex);
			this->publishLocked(event);
		}

		AppleStoreProvider(AppleStoreProvider const &) = delete;
		AppleStoreProvider & operator=(AppleStoreProvider const &) = delete;
	};
	//---------------------------------------------------------
	bool platformStoreAvailable()
	{
		return hasAppIdentity() && [SKPaymentQueue canMakePayments];
	}
	//---------------------------------------------------------
	String platformStoreUnavailableReason()
	{
		if(!hasAppIdentity())
		{
			return "this process has no app identity, so the store cannot "
				"resolve any product - run the packaged app rather than a bare "
				"executable";
		}
		if(![SKPaymentQueue canMakePayments])
		{
			return "this device is not allowed to make payments";
		}
		return "";
	}
	//---------------------------------------------------------
	StorefrontId platformStorefront()
	{
		return APPLE_STOREFRONT;
	}
	//---------------------------------------------------------
	StoreProvider * createPlatformStoreProvider()
	{
		return new AppleStoreProvider();
	}
}

@implementation OrkigeStoreObserver
//---------------------------------------------------------
- (void)paymentQueue:(SKPaymentQueue *)queue
	updatedTransactions:(NSArray<SKPaymentTransaction *> *)transactions
{
	(void)queue;
	if(provider == NULL) { return; }
	provider->onTransactionsUpdated(transactions);
}
//---------------------------------------------------------
- (void)paymentQueueRestoreCompletedTransactionsFinished:(SKPaymentQueue *)queue
{
	(void)queue;
	if(provider == NULL) { return; }
	provider->onRestoreFinished();
}
//---------------------------------------------------------
- (void)paymentQueue:(SKPaymentQueue *)queue
	restoreCompletedTransactionsFailedWithError:(NSError *)error
{
	(void)queue;
	if(provider == NULL) { return; }
	provider->onRestoreFailed(error);
}
//---------------------------------------------------------
- (void)productsRequest:(SKProductsRequest *)request
	didReceiveResponse:(SKProductsResponse *)response
{
	(void)request;
	if(provider == NULL) { return; }
	provider->onProductsReceived(response);
}
//---------------------------------------------------------
- (void)request:(SKRequest *)request didFailWithError:(NSError *)error
{
	(void)request;
	if(provider == NULL) { return; }
	provider->onProductsFailed(error);
}
@end

#pragma clang diagnostic pop

#endif //ORKIGE_STORE_APPLE
