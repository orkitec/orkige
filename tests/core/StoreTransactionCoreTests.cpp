/**************************************************************
	created:	2026/08/04 at 10:00
	filename: 	StoreTransactionCoreTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the whole platform-independent half of a real store
	provider. A store SDK cannot be driven in CI - it needs a signed app, a
	store account and a device - so the decisions that actually cost money are
	pulled out of the platform bridge and proved here: the transaction-state
	translation, the error-code taxonomy, the mapping onto PurchaseState
	(restored IS ownership, deferred grants nothing), the unacknowledged-purchase
	ledger that closes the crash window, and the request/delivery correlation a
	payment queue does not do for us.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_monetization/StoreTransactionCore.h>

using Orkige::MonetizationRequestId;
using Orkige::PurchaseState;
using Orkige::StoreFailureCode;
using Orkige::StorePurchaseRequestBook;
using Orkige::StoreTransactionLedger;
using Orkige::StoreTransactionPhase;
using Orkige::StoreTransactionRecord;
using Orkige::String;

namespace
{
	//! a settled record under one transaction id
	StoreTransactionRecord settled(char const * transactionId,
		char const * storeId, MonetizationRequestId requestId = 0)
	{
		StoreTransactionRecord record;
		record.transactionId = transactionId;
		record.storeId = storeId;
		record.requestId = requestId;
		record.phase = Orkige::STP_PURCHASED;
		return record;
	}
}

TEST_CASE("transaction phases carry stable tokens", "[monetization][store]")
{
	CHECK(Orkige::storeTransactionPhaseName(Orkige::STP_PURCHASING)
		== "purchasing");
	CHECK(Orkige::storeTransactionPhaseName(Orkige::STP_PURCHASED)
		== "purchased");
	CHECK(Orkige::storeTransactionPhaseName(Orkige::STP_RESTORED) == "restored");
	CHECK(Orkige::storeTransactionPhaseName(Orkige::STP_DEFERRED) == "deferred");
	CHECK(Orkige::storeTransactionPhaseName(Orkige::STP_FAILED) == "failed");

	CHECK(Orkige::storeFailureCodeName(Orkige::SFC_CANCELLED) == "cancelled");
	CHECK(Orkige::storeFailureCodeName(Orkige::SFC_PRODUCT_UNAVAILABLE)
		== "product_unavailable");

	// every failure code names itself for a player; "none" deliberately does not
	CHECK(Orkige::storeFailureReason(Orkige::SFC_NONE).empty());
	CHECK_FALSE(Orkige::storeFailureReason(Orkige::SFC_CANCELLED).empty());
	CHECK_FALSE(Orkige::storeFailureReason(Orkige::SFC_NETWORK).empty());
	CHECK_FALSE(Orkige::storeFailureReason(Orkige::SFC_UNKNOWN).empty());
}

TEST_CASE("a settled phase becomes the purchase state game code branches on",
	"[monetization][store]")
{
	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_PURCHASED,
		Orkige::SFC_NONE) == Orkige::PS_PURCHASED);

	// THE ONE GAMES GET WRONG: a restored transaction is the account handing
	// back something it already paid for, which is ownership, not an error
	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_RESTORED,
		Orkige::SFC_NONE) == Orkige::PS_ALREADY_OWNED);

	// deferred grants NOTHING and is not a failure either
	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_DEFERRED,
		Orkige::SFC_NONE) == Orkige::PS_PENDING);

	// in flight is not reportable; if a provider ever does, it is not ownership
	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_PURCHASING,
		Orkige::SFC_NONE) == Orkige::PS_FAILED);
}

TEST_CASE("a failed phase reports the reason a game can act on",
	"[monetization][store]")
{
	// closing the sheet is NOT an error and must never be reported as one
	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_FAILED,
		Orkige::SFC_CANCELLED) == Orkige::PS_CANCELLED);

	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_FAILED,
		Orkige::SFC_CLIENT_NOT_ALLOWED) == Orkige::PS_DECLINED);
	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_FAILED,
		Orkige::SFC_PAYMENT_INVALID) == Orkige::PS_DECLINED);
	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_FAILED,
		Orkige::SFC_PRODUCT_UNAVAILABLE) == Orkige::PS_UNAVAILABLE);
	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_FAILED,
		Orkige::SFC_NETWORK) == Orkige::PS_FAILED);
	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_FAILED,
		Orkige::SFC_UNKNOWN) == Orkige::PS_FAILED);
}

TEST_CASE("the platform's transaction-state numbers translate exactly",
	"[monetization][store]")
{
	StoreTransactionPhase phase = Orkige::STP_FAILED;

	REQUIRE(Orkige::appleStorePhaseFromRaw(0, phase));
	CHECK(phase == Orkige::STP_PURCHASING);
	REQUIRE(Orkige::appleStorePhaseFromRaw(1, phase));
	CHECK(phase == Orkige::STP_PURCHASED);
	REQUIRE(Orkige::appleStorePhaseFromRaw(2, phase));
	CHECK(phase == Orkige::STP_FAILED);
	REQUIRE(Orkige::appleStorePhaseFromRaw(3, phase));
	CHECK(phase == Orkige::STP_RESTORED);
	REQUIRE(Orkige::appleStorePhaseFromRaw(4, phase));
	CHECK(phase == Orkige::STP_DEFERRED);

	// a state this build has never seen is REFUSED, never guessed at - the
	// bridge leaves such a transaction unacknowledged rather than telling the
	// store goods were delivered
	phase = Orkige::STP_PURCHASED;
	CHECK_FALSE(Orkige::appleStorePhaseFromRaw(5, phase));
	CHECK_FALSE(Orkige::appleStorePhaseFromRaw(-1, phase));
	CHECK(phase == Orkige::STP_PURCHASED);	// left untouched
}

TEST_CASE("the platform's error numbers collapse onto the coarse taxonomy",
	"[monetization][store]")
{
	// the player closed the sheet - from the payment sheet and from the
	// store's own overlay surface alike
	CHECK(Orkige::appleStoreFailureFromRaw(2) == Orkige::SFC_CANCELLED);
	CHECK(Orkige::appleStoreFailureFromRaw(15) == Orkige::SFC_CANCELLED);

	CHECK(Orkige::appleStoreFailureFromRaw(1)
		== Orkige::SFC_CLIENT_NOT_ALLOWED);
	CHECK(Orkige::appleStoreFailureFromRaw(4)
		== Orkige::SFC_CLIENT_NOT_ALLOWED);
	CHECK(Orkige::appleStoreFailureFromRaw(3) == Orkige::SFC_PAYMENT_INVALID);
	CHECK(Orkige::appleStoreFailureFromRaw(5)
		== Orkige::SFC_PRODUCT_UNAVAILABLE);
	CHECK(Orkige::appleStoreFailureFromRaw(19)
		== Orkige::SFC_PRODUCT_UNAVAILABLE);
	CHECK(Orkige::appleStoreFailureFromRaw(7) == Orkige::SFC_NETWORK);

	// an unrecognised code lands on UNKNOWN, which reports PS_FAILED. It must
	// never land on CANCELLED, which a game silently swallows.
	CHECK(Orkige::appleStoreFailureFromRaw(0) == Orkige::SFC_UNKNOWN);
	CHECK(Orkige::appleStoreFailureFromRaw(9999) == Orkige::SFC_UNKNOWN);
	CHECK(Orkige::purchaseStateForPhase(Orkige::STP_FAILED,
		Orkige::appleStoreFailureFromRaw(9999)) == Orkige::PS_FAILED);
}

TEST_CASE("the ledger holds the unacknowledged-purchase window open",
	"[monetization][store]")
{
	StoreTransactionLedger ledger;
	CHECK(ledger.openCount() == 0);

	REQUIRE(ledger.note(settled("t1", "coins_500", 7)));
	CHECK(ledger.openCount() == 1);
	CHECK(ledger.isOpen("t1"));

	StoreTransactionRecord const * record = ledger.find("t1");
	REQUIRE(record != NULL);
	CHECK(record->storeId == "coins_500");
	CHECK(record->requestId == 7);

	// THE ORDER THAT CLOSES THE WINDOW: the purchase stays open until the game
	// says the goods are durably granted
	REQUIRE(ledger.acknowledge("t1"));
	CHECK(ledger.openCount() == 0);
	CHECK_FALSE(ledger.isOpen("t1"));
	CHECK(ledger.find("t1") == NULL);
}

TEST_CASE("the ledger reports a re-delivery instead of paying out twice",
	"[monetization][store]")
{
	StoreTransactionLedger ledger;

	REQUIRE(ledger.note(settled("t1", "remove_ads")));
	// the platform queue re-offering the SAME transaction inside one session
	// must produce exactly one report to the game
	CHECK_FALSE(ledger.note(settled("t1", "remove_ads")));
	CHECK(ledger.openCount() == 1);

	// a DIFFERENT transaction for the same product is a second purchase and is
	// its own record (a consumable bought twice)
	REQUIRE(ledger.note(settled("t2", "remove_ads")));
	CHECK(ledger.openCount() == 2);

	// once acknowledged, the same id may legitimately arrive again in a later
	// session - the ledger is per-session bookkeeping, never a save
	REQUIRE(ledger.acknowledge("t1"));
	CHECK(ledger.note(settled("t1", "remove_ads")));
}

TEST_CASE("acknowledging something never delivered is refused, not ignored",
	"[monetization][store]")
{
	StoreTransactionLedger ledger;

	// a silent no-op here hides a game that is tracking transactions the store
	// does not agree with
	CHECK_FALSE(ledger.acknowledge("never-happened"));

	REQUIRE(ledger.note(settled("t1", "coins_500")));
	REQUIRE(ledger.acknowledge("t1"));
	CHECK_FALSE(ledger.acknowledge("t1"));	// twice is a bug too
}

TEST_CASE("a transaction with no handle never enters the ledger",
	"[monetization][store]")
{
	StoreTransactionLedger ledger;

	StoreTransactionRecord record = settled("", "coins_500");
	// it could never be acknowledged, so taking it in would mean an entry that
	// can only ever leak
	CHECK_FALSE(ledger.note(record));
	CHECK(ledger.openCount() == 0);
}

TEST_CASE("clearing the ledger forgets bookkeeping, not purchases",
	"[monetization][store]")
{
	StoreTransactionLedger ledger;
	REQUIRE(ledger.note(settled("t1", "coins_500")));
	REQUIRE(ledger.note(settled("t2", "remove_ads")));

	ledger.clear();
	CHECK(ledger.openCount() == 0);
	// the STORE still holds both - which is why a re-delivery after a teardown
	// is accepted as new
	CHECK(ledger.note(settled("t1", "coins_500")));
}

TEST_CASE("a delivery claims the oldest request for its product",
	"[monetization][store]")
{
	StorePurchaseRequestBook book;

	REQUIRE(book.submit(1, "coins_500"));
	REQUIRE(book.submit(2, "remove_ads"));
	REQUIRE(book.submit(3, "coins_500"));
	CHECK(book.pendingCount() == 3);

	// a payment queue delivers in the order it accepted payments
	CHECK(book.claim("coins_500") == 1);
	CHECK(book.claim("coins_500") == 3);
	CHECK(book.pendingCount() == 1);
	CHECK(book.isPending(2));
}

TEST_CASE("a delivery nobody asked for is unsolicited, not an error",
	"[monetization][store]")
{
	StorePurchaseRequestBook book;

	// a deferred purchase settling in a later session, a purchase begun outside
	// the app, or a transaction the previous run died before acknowledging: the
	// seam names those with request id 0
	CHECK(book.claim("remove_ads") == 0);

	REQUIRE(book.submit(9, "remove_ads"));
	CHECK(book.claim("remove_ads") == 9);
	// the request is consumed - a second delivery is unsolicited again
	CHECK(book.claim("remove_ads") == 0);
}

TEST_CASE("the request book refuses ids that could not be answered",
	"[monetization][store]")
{
	StorePurchaseRequestBook book;

	CHECK_FALSE(book.submit(0, "coins_500"));	// 0 is the never-valid handle
	REQUIRE(book.submit(4, "coins_500"));
	CHECK_FALSE(book.submit(4, "coins_500"));	// already outstanding

	CHECK(book.retire(4));
	CHECK_FALSE(book.retire(4));
	CHECK(book.pendingCount() == 0);
}

TEST_CASE("the request book lists outstanding purchases in submission order",
	"[monetization][store]")
{
	StorePurchaseRequestBook book;
	REQUIRE(book.submit(5, "a"));
	REQUIRE(book.submit(6, "b"));
	REQUIRE(book.submit(7, "c"));

	const std::vector<MonetizationRequestId> pending = book.pending();
	REQUIRE(pending.size() == 3);
	CHECK(pending[0] == 5);
	CHECK(pending[1] == 6);
	CHECK(pending[2] == 7);

	book.clear();
	CHECK(book.pendingCount() == 0);
	CHECK(book.pending().empty());
}
