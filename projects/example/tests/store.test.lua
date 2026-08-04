-- store.test.lua - the purchase tier end to end, against the SIMULATED store.
--
-- Run it with:  orkige_player --project projects/example --run-tests
--
-- This is the leg a real storefront cannot provide: no CI machine has a signed
-- app, a store account or a device. What it proves is everything above the
-- platform call - the project's own catalog resolves, a purchase reaches the
-- game through the frame boundary, ownership is cached, a restore names what it
-- restored through the catalog's reverse index, and the refusals say why. The
-- platform bridge's own integer translations are proved in the C++ unit suite
-- (tests/core/StoreTransactionCoreTests.cpp).
--
-- The project asks for the simulator BY NAME in its manifest (store.provider);
-- nothing makes a shipped game fall into it.

-- how many frames an answer is given before the test calls it lost. Every
-- store call answers at a frame boundary, so this is generous, not tight.
local ANSWER_FRAMES = 120

-- run one store call and hand back its single answer
local function answer(t, begin)
	local got = nil
	local calls = 0
	t.truthy(begin(function(res) got = res calls = calls + 1 end),
		"the store refused to accept the request at all")
	t.waitUntil(function() return got ~= nil end, ANSWER_FRAMES)
	t.truthy(got, "the store never answered")
	-- THE CALLBACK CONTRACT: exactly one answer per request. A second one would
	-- grant a purchase twice.
	t.eq(calls, 1, "the request was answered more than once")
	return got
end

test("the store comes up with the project's own catalog",
	{ scene = "scenes/main.oscene" }, function(t)
		t.truthy(store.isAvailable(), "the simulated store did not come up")
		t.eq(store.pending(), 0, "something was pending before anything ran")

		local res = answer(t, function(done) return store.products(done) end)
		t.truthy(res.ok, res.reason)
		-- both products in store.ocatalog, named by their LOGICAL ids
		t.eq(res.count, 2, "the catalog did not resolve to two products")
		local byId = {}
		for i = 1, res.count do byId[res[i].id] = res[i] end
		t.truthy(byId["remove_ads"], "remove_ads is not sold here")
		t.truthy(byId["coins_500"], "coins_500 is not sold here")
		t.eq(byId["remove_ads"].kind, "non_consumable")
		t.eq(byId["coins_500"].kind, "consumable")
	end)

test("a purchase grants the entitlement and the no-ads link",
	{ scene = "scenes/main.oscene" }, function(t)
		t.falsy(store.owns("remove_ads"), "it was owned before it was bought")
		t.falsy(store.adFree(), "ads were already suppressed")

		local res = answer(t, function(done)
			return store.purchase("remove_ads", done)
		end)
		t.truthy(res.owned, res.reason)
		t.eq(res.state, "purchased")
		t.eq(res.productId, "remove_ads", "the answer named a storefront id")
		t.truthy(res.transactionId ~= "", "no transaction to acknowledge")

		t.truthy(store.owns("remove_ads"), "the entitlement was not cached")
		-- THE LINK between the two seams: a no-ads product is a catalog fact,
		-- not something each game re-derives
		t.truthy(store.adFree(), "owning remove_ads did not suppress ads")

		-- ACKNOWLEDGE ONLY AFTER THE GOODS ARE DURABLE. Finishing first loses
		-- the purchase if the app dies an instant later.
		save.set("adFree", true)
		save.flush()
		t.truthy(store.finish(res.transactionId), "the store refused the ack")
	end)

test("a product this storefront does not sell is refused by name",
	{ scene = "scenes/main.oscene" }, function(t)
		local res = answer(t, function(done)
			return store.purchase("no_such_product", done)
		end)
		t.falsy(res.owned, "an unknown product was reported as owned")
		t.eq(res.state, "unavailable")
		-- a refusal that does not say what is missing sends a developer
		-- hunting in the wrong console
		t.truthy(res.reason ~= "", "the refusal carried no reason")
		t.falsy(store.owns("no_such_product"))
	end)

test("a restore names what it restored through the catalog",
	{ scene = "scenes/main.oscene" }, function(t)
		local res = answer(t, function(done) return store.restore(done) end)
		-- AN EMPTY RESTORE IS A SUCCESS - a player who never bought anything
		-- restores nothing - so `ok` is the field that says the store answered
		t.truthy(res.ok, res.reason)
		t.truthy(res.count >= 0)
		for i = 1, res.count do
			-- a store hands back ITS identifiers; everything reaching a game
			-- has been mapped back to a logical id
			t.truthy(res[i].productId == "remove_ads"
				or res[i].productId == "coins_500",
				"a restored entitlement kept a storefront id: "
					.. tostring(res[i].productId))
		end
	end)

test("a purchase with no onComplete is refused, not fired and forgotten",
	{ scene = "scenes/main.oscene" }, function(t)
		-- a purchase whose answer nobody reads takes the player's money and
		-- grants nothing, so the seam refuses it outright
		t.falsy(store.purchase("remove_ads"),
			"a purchase with no callback was accepted")
		t.falsy(store.products(), "a query with no callback was accepted")
		t.falsy(store.restore(), "a restore with no callback was accepted")
	end)
