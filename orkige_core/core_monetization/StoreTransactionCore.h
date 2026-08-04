/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	StoreTransactionCore.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __StoreTransactionCore_h__4_8_2026__10_00_00__
#define __StoreTransactionCore_h__4_8_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_monetization/MonetizationTypes.h"
#include "core_util/String.h"

#include <cstddef>
#include <vector>

namespace Orkige
{
	/** \addtogroup Monetization
	*  @{ */

	//! @brief where ONE transaction stands, in the engine's own words.
	//!
	//! THE REASON THIS VOCABULARY EXISTS: a platform store hands its payment
	//! queue over as opaque integers, and translating them is the single most
	//! consequential decision a store provider makes - mistake "restored" for
	//! "failed" and a paying player loses what they bought. That decision must
	//! therefore be provable WITHOUT the platform: the numbers are translated
	//! into this enum by a pure function a unit test can drive, and the
	//! platform bridge above it does nothing but pass the number along.
	enum StoreTransactionPhase
	{
		//! in flight - the store has not decided yet, and NOTHING is reported
		STP_PURCHASING = 0,
		//! the money changed hands. MUST be acknowledged (@see
		//! StoreTransactionLedger) or the store refunds it.
		STP_PURCHASED,
		//! @brief the account already paid for this once and the store is
		//! handing it back - a SUCCESS for the player, not a repeat charge
		STP_RESTORED,
		//! @brief neither done nor failed: waiting on someone else (a parent's
		//! approval, an offline payment method), possibly for days
		STP_DEFERRED,
		//! it ended without a purchase; a StoreFailureCode says why
		STP_FAILED
	};

	//! the stable token a transaction phase reports as ("purchased", ...)
	ORKIGE_CORE_DLL String const & storeTransactionPhaseName(
		StoreTransactionPhase phase);

	//! @brief why a transaction failed, in the engine's own words.
	//! @remarks Deliberately COARSE. A store publishes dozens of error codes and
	//! a game can act on about five of them; collapsing them here (rather than
	//! at every call site) is what keeps the same five branches meaningful on
	//! every platform. The store's own wording still travels in `reason`.
	enum StoreFailureCode
	{
		SFC_NONE = 0,			//!< it did not fail
		//! the player closed the payment sheet - NOT an error
		SFC_CANCELLED,
		//! @brief this device or account may not pay at all (parental
		//! restrictions, a managed device, a revoked service)
		SFC_CLIENT_NOT_ALLOWED,
		//! the store refused the payment itself (a bad card, a bad offer)
		SFC_PAYMENT_INVALID,
		//! the product is not sold in this account's storefront
		SFC_PRODUCT_UNAVAILABLE,
		//! the store could not be reached
		SFC_NETWORK,
		//! anything else
		SFC_UNKNOWN
	};

	//! the stable token a failure code reports as ("cancelled", ...)
	ORKIGE_CORE_DLL String const & storeFailureCodeName(StoreFailureCode code);

	//! @brief a one-line, player-readable reason for a failure code - the
	//! fallback used when the store volunteered no wording of its own, so a
	//! game never has to invent an error message.
	ORKIGE_CORE_DLL String const & storeFailureReason(StoreFailureCode code);

	//! @brief THE TRANSLATION the whole store side turns on: what one phase (and
	//! its failure code, when it failed) means to game code.
	//!
	//! Two of these mappings are the ones shipped games get wrong:
	//! - STP_RESTORED becomes PS_ALREADY_OWNED, which
	//!   PurchaseResult::owned() reports as ownership. The player paid once
	//!   already; charging them again or showing an error would both be wrong.
	//! - STP_DEFERRED becomes PS_PENDING and grants NOTHING. Treating it as a
	//!   failure loses the sale when the approval lands; treating it as a
	//!   success hands out goods nobody has paid for.
	ORKIGE_CORE_DLL PurchaseState purchaseStateForPhase(
		StoreTransactionPhase phase, StoreFailureCode failure);

	//--- the Apple payment queue's own numbers -------------------------------
	// The two functions below are the ENTIRE platform-specific decision surface
	// of the Apple store provider, deliberately expressed as pure integer
	// translation so the unit suite proves them on a machine with no store
	// account, no signing identity and no device. The bridge that owns the
	// framework passes the raw value through and does nothing else with it, and
	// it static_asserts every constant below against the SDK it compiles
	// against - so a platform that renumbered its enum breaks the BUILD rather
	// than silently mis-reporting a purchase.

	//! @brief translate the platform payment queue's transaction-state value.
	//! @return false when the platform reported a state this build does not
	//! know - refused by name rather than guessed at
	ORKIGE_CORE_DLL bool appleStorePhaseFromRaw(int rawState,
		StoreTransactionPhase & outPhase);

	//! @brief translate the platform store's error code into the coarse
	//! vocabulary above. An unrecognised code is SFC_UNKNOWN, never a crash and
	//! never a silent success.
	ORKIGE_CORE_DLL StoreFailureCode appleStoreFailureFromRaw(int rawCode);

	//--- the unacknowledged-purchase window ----------------------------------

	//! @brief one transaction the store delivered and the game has NOT yet
	//! acknowledged.
	struct ORKIGE_CORE_DLL StoreTransactionRecord
	{
		//! the storefront's own transaction handle - the key finishing uses
		String					transactionId;
		//! the STOREFRONT product identifier (never the logical id)
		String					storeId;
		//! @brief the purchase request this answers, or 0 when the store
		//! volunteered it (a deferred purchase settling later, or a
		//! transaction re-delivered at launch because a previous run died
		//! before acknowledging it)
		MonetizationRequestId	requestId = 0;
		//! where it stands
		StoreTransactionPhase	phase = STP_PURCHASED;
	};

	//! @brief THE ANSWER TO THE CLASSIC MONEY BUG: the set of transactions the
	//! store considers settled and this process has not yet acknowledged.
	//!
	//! THE WINDOW. Between "the store charged the player" and "the game finished
	//! the transaction" the app can be killed - by a crash, by the player, by
	//! the operating system reclaiming a backgrounded game. If the app treats
	//! its own memory as the record of what was bought, that purchase is lost
	//! and the player paid for nothing.
	//!
	//! THE RULE THAT CLOSES IT: the STORE's queue is the record, not ours. A
	//! settled transaction stays in the platform queue until it is explicitly
	//! finished, and is re-delivered on every launch until then. So the engine
	//! never persists a purchase to survive a crash - it lets the queue
	//! re-deliver, and the re-delivery arrives through the ordinary unsolicited
	//! path (@see StoreEvent::id) as if it had just happened.
	//!
	//! WHICH MAKES THE ORDER LOAD-BEARING: acknowledge only AFTER the goods are
	//! durably granted. Finish first and a crash one instruction later loses the
	//! purchase for good, because the queue has already forgotten it. Grant
	//! first and the worst case is a re-delivery of something the player already
	//! has, which a game absorbs by granting it again.
	//!
	//! This ledger is what makes that provable: it tracks the open set, refuses
	//! to acknowledge a transaction that was never delivered (a bug, and one
	//! that would otherwise be a silent no-op), and reports the same transaction
	//! arriving twice inside ONE session so the game is not told twice.
	//!
	//! @remarks Pure and in-memory ON PURPOSE. Persisting it would create a
	//! second record of what was bought that can disagree with the store's, and
	//! a file the player can edit is not a proof of payment.
	class ORKIGE_CORE_DLL StoreTransactionLedger
	{
		//--- Variables ---------------------------------------
	private:
		//! everything delivered and not yet acknowledged, in arrival order
		std::vector<StoreTransactionRecord>	mOpen;
		//--- Methods -----------------------------------------
	public:
		StoreTransactionLedger();

		//! @brief record a delivery.
		//! @return false when a transaction with this id is ALREADY open - the
		//! platform re-delivering inside one session, which must produce
		//! exactly one report to the game, not two
		bool note(StoreTransactionRecord const & record);

		//! is this transaction still waiting to be acknowledged
		bool isOpen(String const & transactionId) const;

		//! the open record for @p transactionId, or NULL
		StoreTransactionRecord const * find(String const & transactionId) const;

		//! @brief acknowledge one transaction.
		//! @return false when nothing is open under that id. Finishing a
		//! transaction that was never delivered is a caller bug, and answering
		//! it honestly is what lets the provider log it instead of quietly
		//! doing nothing.
		bool acknowledge(String const & transactionId);

		//! everything still awaiting acknowledgement, in arrival order
		std::vector<StoreTransactionRecord> const & open() const
		{
			return this->mOpen;
		}
		//! how many purchases are inside the unacknowledged window right now
		std::size_t openCount() const { return this->mOpen.size(); }

		//! drop the bookkeeping (a teardown - the STORE's queue is unaffected,
		//! which is the whole point)
		void clear();
	};

	//--- correlating a delivery with the tap that asked for it ---------------

	//! @brief the in-flight purchase requests, keyed by storefront identifier.
	//!
	//! WHY THIS IS NOT TRIVIAL: a platform payment queue delivers transactions,
	//! not answers. Nothing in a delivery says which `purchase()` call it
	//! belongs to - the correlation the rest of the engine takes for granted
	//! (@see MonetizationRequestId) simply does not exist at that layer, and the
	//! same product can legitimately be delivered with NO request behind it at
	//! all. This book is the one place that guess is made, so it can be tested:
	//! the oldest outstanding request for that identifier claims the delivery,
	//! and when there is none the delivery is unsolicited and travels with id 0.
	class ORKIGE_CORE_DLL StorePurchaseRequestBook
	{
		//--- Types -------------------------------------------
	private:
		//! one submitted purchase awaiting a delivery
		struct Entry
		{
			MonetizationRequestId	id = 0;
			String					storeId;
		};
		//--- Variables ---------------------------------------
	private:
		//! outstanding requests, in submission order
		std::vector<Entry>	mPending;
		//--- Methods -----------------------------------------
	public:
		StorePurchaseRequestBook();

		//! @brief record a submitted purchase.
		//! @return false when @p id is 0 or already outstanding
		bool submit(MonetizationRequestId id, String const & storeId);

		//! @brief claim the request a delivery of @p storeId answers.
		//! @return the request id, or 0 for an UNSOLICITED delivery. The OLDEST
		//! outstanding request wins, because a queue delivers in the order it
		//! accepted payments.
		MonetizationRequestId claim(String const & storeId);

		//! @brief drop one outstanding request by id (it was answered another
		//! way, or the whole surface went down).
		//! @return false when it was not outstanding
		bool retire(MonetizationRequestId id);

		//! every outstanding request id, in submission order
		std::vector<MonetizationRequestId> pending() const;
		//! how many purchases are waiting for a delivery
		std::size_t pendingCount() const { return this->mPending.size(); }
		//! is @p id still waiting
		bool isPending(MonetizationRequestId id) const;

		//! drop everything (a shutdown answers nobody)
		void clear();
	};

	/** @} */
}

#endif //__StoreTransactionCore_h__4_8_2026__10_00_00__
