/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	StoreTransactionCore.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_monetization/StoreTransactionCore.h"

#include <cstddef>

namespace Orkige
{
	namespace
	{
		//! the answer every lookup that names nothing falls back to
		const String EMPTY_NAME = "";

		//--- the token tables (row index == enum value) ---
		String const * transactionPhaseNames()
		{
			static const String NAMES[] =
			{
				"purchasing", "purchased", "restored", "deferred", "failed"
			};
			return NAMES;
		}
		String const * failureCodeNames()
		{
			static const String NAMES[] =
			{
				"none", "cancelled", "client_not_allowed", "payment_invalid",
				"product_unavailable", "network", "unknown"
			};
			return NAMES;
		}
		String const * failureReasons()
		{
			static const String REASONS[] =
			{
				"",
				"the payment sheet was closed",
				"this device or account is not allowed to make payments",
				"the store refused the payment",
				"the product is not sold in this account's storefront",
				"the store could not be reached",
				"the store reported a failure"
			};
			return REASONS;
		}
	}
	//---------------------------------------------------------
	String const & storeTransactionPhaseName(StoreTransactionPhase phase)
	{
		const int index = static_cast<int>(phase);
		if(index < 0 || index > static_cast<int>(STP_FAILED))
		{
			return EMPTY_NAME;
		}
		return transactionPhaseNames()[index];
	}
	//---------------------------------------------------------
	String const & storeFailureCodeName(StoreFailureCode code)
	{
		const int index = static_cast<int>(code);
		if(index < 0 || index > static_cast<int>(SFC_UNKNOWN))
		{
			return EMPTY_NAME;
		}
		return failureCodeNames()[index];
	}
	//---------------------------------------------------------
	String const & storeFailureReason(StoreFailureCode code)
	{
		const int index = static_cast<int>(code);
		if(index < 0 || index > static_cast<int>(SFC_UNKNOWN))
		{
			return EMPTY_NAME;
		}
		return failureReasons()[index];
	}
	//---------------------------------------------------------
	PurchaseState purchaseStateForPhase(StoreTransactionPhase phase,
		StoreFailureCode failure)
	{
		switch(phase)
		{
		case STP_PURCHASED:
			return PS_PURCHASED;

		case STP_RESTORED:
			// the account paid for this ALREADY. PurchaseResult::owned() reports
			// both of these as ownership, which is what stops a game from
			// showing an error to a player who is simply reinstalling.
			return PS_ALREADY_OWNED;

		case STP_DEFERRED:
			// grants NOTHING yet - it may settle days later, in another session
			return PS_PENDING;

		case STP_PURCHASING:
			// nothing has happened; a provider must not report this at all, and
			// the safest reading if one ever does is "not owned"
			return PS_FAILED;

		case STP_FAILED:
			break;
		}

		switch(failure)
		{
		case SFC_CANCELLED:
			// the player closing the sheet is NOT an error and must not raise one
			return PS_CANCELLED;
		case SFC_CLIENT_NOT_ALLOWED:
		case SFC_PAYMENT_INVALID:
			return PS_DECLINED;
		case SFC_PRODUCT_UNAVAILABLE:
			return PS_UNAVAILABLE;
		case SFC_NETWORK:
		case SFC_UNKNOWN:
		case SFC_NONE:
			break;
		}
		return PS_FAILED;
	}
	//---------------------------------------------------------
	bool appleStorePhaseFromRaw(int rawState, StoreTransactionPhase & outPhase)
	{
		// The payment queue's published transaction states. The bridge TU
		// static_asserts each of these against the SDK, so a renumbering is a
		// build failure rather than a mis-reported purchase.
		switch(rawState)
		{
		case 0: outPhase = STP_PURCHASING;	return true;	// purchasing
		case 1: outPhase = STP_PURCHASED;	return true;	// purchased
		case 2: outPhase = STP_FAILED;		return true;	// failed
		case 3: outPhase = STP_RESTORED;	return true;	// restored
		case 4: outPhase = STP_DEFERRED;	return true;	// deferred
		default: break;
		}
		return false;
	}
	//---------------------------------------------------------
	StoreFailureCode appleStoreFailureFromRaw(int rawCode)
	{
		// The store's published error codes, collapsed onto the coarse
		// vocabulary a game can actually branch on. Anything this build has not
		// seen lands on SFC_UNKNOWN, which reports PS_FAILED - never a silent
		// success, and never a cancellation the game would swallow.
		switch(rawCode)
		{
		case 2:		// the player closed the payment sheet
		case 15:	// the same, from the store's own overlay surface
			return SFC_CANCELLED;

		case 1:		// this client may not issue the request
		case 4:		// this device is not allowed to pay
		case 6:		// permission to the account's service was denied
		case 8:		// that permission was revoked
		case 9:		// the player must acknowledge a privacy policy first
			return SFC_CLIENT_NOT_ALLOWED;

		case 3:		// the payment itself was rejected
		case 10:	// the request carried data this app may not send
		case 11:	// the offer identifier is not valid
		case 12:	// the offer signature is not valid
		case 13:	// the offer is missing parameters
		case 14:	// the offer price is not valid
		case 18:	// this account is not eligible for the offer
		case 21:	// the payment method needs configuring first
			return SFC_PAYMENT_INVALID;

		case 5:		// not sold in this storefront
		case 19:	// not sold on this platform
			return SFC_PRODUCT_UNAVAILABLE;

		case 7:		// the device could not reach the store
			return SFC_NETWORK;

		default:
			break;
		}
		return SFC_UNKNOWN;
	}
	//---------------------------------------------------------
	StoreTransactionLedger::StoreTransactionLedger()
	{
	}
	//---------------------------------------------------------
	bool StoreTransactionLedger::note(StoreTransactionRecord const & record)
	{
		if(record.transactionId.empty())
		{
			// a settled transaction the store issued no handle for cannot be
			// acknowledged at all, so it is never taken into the open set
			return false;
		}
		if(this->isOpen(record.transactionId)) { return false; }

		this->mOpen.push_back(record);
		return true;
	}
	//---------------------------------------------------------
	bool StoreTransactionLedger::isOpen(String const & transactionId) const
	{
		return this->find(transactionId) != NULL;
	}
	//---------------------------------------------------------
	StoreTransactionRecord const * StoreTransactionLedger::find(
		String const & transactionId) const
	{
		for(std::size_t i = 0; i < this->mOpen.size(); ++i)
		{
			if(this->mOpen[i].transactionId == transactionId)
			{
				return &this->mOpen[i];
			}
		}
		return NULL;
	}
	//---------------------------------------------------------
	bool StoreTransactionLedger::acknowledge(String const & transactionId)
	{
		for(std::size_t i = 0; i < this->mOpen.size(); ++i)
		{
			if(this->mOpen[i].transactionId != transactionId) { continue; }
			this->mOpen.erase(this->mOpen.begin()
				+ static_cast<std::ptrdiff_t>(i));
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	void StoreTransactionLedger::clear()
	{
		this->mOpen.clear();
	}
	//---------------------------------------------------------
	StorePurchaseRequestBook::StorePurchaseRequestBook()
	{
	}
	//---------------------------------------------------------
	bool StorePurchaseRequestBook::submit(MonetizationRequestId id,
		String const & storeId)
	{
		if(id == 0) { return false; }
		if(this->isPending(id)) { return false; }

		Entry entry;
		entry.id = id;
		entry.storeId = storeId;
		this->mPending.push_back(entry);
		return true;
	}
	//---------------------------------------------------------
	MonetizationRequestId StorePurchaseRequestBook::claim(String const & storeId)
	{
		for(std::size_t i = 0; i < this->mPending.size(); ++i)
		{
			if(this->mPending[i].storeId != storeId) { continue; }

			const MonetizationRequestId id = this->mPending[i].id;
			this->mPending.erase(this->mPending.begin()
				+ static_cast<std::ptrdiff_t>(i));
			return id;
		}
		// nobody asked for this one: a deferred purchase settling in a later
		// session, a purchase begun outside the app, or a transaction the
		// previous run died before acknowledging
		return 0;
	}
	//---------------------------------------------------------
	bool StorePurchaseRequestBook::retire(MonetizationRequestId id)
	{
		for(std::size_t i = 0; i < this->mPending.size(); ++i)
		{
			if(this->mPending[i].id != id) { continue; }
			this->mPending.erase(this->mPending.begin()
				+ static_cast<std::ptrdiff_t>(i));
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	std::vector<MonetizationRequestId> StorePurchaseRequestBook::pending() const
	{
		std::vector<MonetizationRequestId> ids;
		ids.reserve(this->mPending.size());
		for(std::size_t i = 0; i < this->mPending.size(); ++i)
		{
			ids.push_back(this->mPending[i].id);
		}
		return ids;
	}
	//---------------------------------------------------------
	bool StorePurchaseRequestBook::isPending(MonetizationRequestId id) const
	{
		for(std::size_t i = 0; i < this->mPending.size(); ++i)
		{
			if(this->mPending[i].id == id) { return true; }
		}
		return false;
	}
	//---------------------------------------------------------
	void StorePurchaseRequestBook::clear()
	{
		this->mPending.clear();
	}
}
