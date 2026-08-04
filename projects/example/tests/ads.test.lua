-- ads.test.lua - the advertising tier end to end, against the SIMULATED surface.
--
-- Run it with:  orkige_player --project projects/example --run-tests
--
-- This is the leg no real network can provide: no CI machine has an ad account,
-- and a real network answers what it feels like answering. What it proves is
-- everything the engine owns - consent gating initialization, the placement
-- lifecycle, the reward arriving ONLY with the earned signal, a no-ads purchase
-- silencing the interruptive formats while leaving rewarded alone, and every
-- refusal naming itself. The per-unit state machine is proved separately and
-- headlessly in tests/core/AdPlacementTests.cpp.
--
-- The project asks for the simulator BY NAME in its manifest (ads.provider);
-- nothing makes a shipped game fall into it. Unhappy paths are pinned through
-- the `ads.sim` cvar, which exists only while the simulator is installed.

-- how many frames an answer is given before the test calls it lost. Every ad
-- call answers at a frame boundary, so this is generous, not tight.
local ANSWER_FRAMES = 120

-- run one ad call and hand back its single answer
local function answer(t, begin)
	local got = nil
	local calls = 0
	t.truthy(begin(function(res) got = res calls = calls + 1 end),
		"the ad surface refused to accept the request at all")
	t.waitUntil(function() return got ~= nil end, ANSWER_FRAMES)
	t.truthy(got, "the ad surface never answered")
	-- THE CALLBACK CONTRACT: exactly one answer per request. A second one
	-- would grant a reward twice.
	t.eq(calls, 1, "the request was answered more than once")
	return got
end

-- bring the surface up the way a game must: consent FIRST, then init.
-- The scenario is reset to its baseline here because the provider outlives an
-- individual test - it belongs to the runtime, not to the world the runner
-- rebuilds - so a path one test pinned would otherwise decide the next one's
-- answers.
local function armAds(t)
	t.truthy(ads.setConsent("granted", true, false), "consent was refused")
	t.truthy(ads.init(true), "the simulated ad surface did not come up")
	t.truthy(cvar.set("ads.sim", "loadResult=loaded"))
	t.truthy(cvar.set("ads.sim", "showResult=completed"))
end

test("consent is an ordering constraint, not a flag",
	{ scene = "scenes/main.oscene" }, function(t)
		-- CS_NOT_GATHERED is the state a fresh runtime starts in, and the
		-- surface must refuse to come up on a permission nobody gave. A
		-- network started without consent is the fault this shape prevents.
		t.falsy(ads.init(true), "ads initialized before consent was gathered")
		t.falsy(ads.isAvailable(), "the surface came up anyway")

		armAds(t)
		t.truthy(ads.isAvailable(), "consent was given and it still did not")
	end)

test("a load answers, and no fill is an ordinary answer",
	{ scene = "scenes/main.oscene" }, function(t)
		armAds(t)
		local res = answer(t, function(done)
			return ads.load("interstitial", "level_end", done)
		end)
		t.truthy(res.ready, res.reason)
		t.eq(res.state, "loaded")
		t.falsy(res.noFill)
		t.truthy(ads.isReady("interstitial", "level_end"),
			"a loaded unit does not report itself ready")

		-- NO FILL is the single most likely thing to break a real game while
		-- never once appearing in development: the request was valid and the
		-- network simply had nothing to give. It is not an error.
		t.truthy(cvar.set("ads.sim", "loadResult=no_fill"),
			"the scenario could not be pinned")
		local empty = answer(t, function(done)
			return ads.load("interstitial", "other", done)
		end)
		t.falsy(empty.ready, "a no-fill load reported inventory")
		t.eq(empty.state, "no_fill")
		t.truthy(empty.noFill, "no fill did not name itself")
		t.falsy(ads.isReady("interstitial", "other"))
	end)

test("SHOW BEFORE READY is refused rather than undefined",
	{ scene = "scenes/main.oscene" }, function(t)
		armAds(t)
		-- nothing was ever loaded for this placement
		local res = answer(t, function(done)
			return ads.show("interstitial", "never_loaded", done)
		end)
		t.falsy(res.rewardEarned)
		t.eq(res.state, "not_ready")
		t.truthy(res.reason ~= "", "the refusal carried no reason")
	end)

test("THE REWARD ARRIVES ONLY WITH THE EARNED SIGNAL",
	{ scene = "scenes/main.oscene" }, function(t)
		armAds(t)
		-- A mediation surface reports "the advert closed" and "the reward was
		-- earned" as TWO SEPARATE SIGNALS, and a game that pays out on the
		-- close pays for an advert nobody watched. The simulator reports a
		-- reward on every show outcome exactly as a real one does, so this
		-- proves the SEAM is what withholds it - not the provider being tidy.
		t.truthy(cvar.set("ads.sim", "showResult=dismissed"))
		t.truthy(cvar.set("ads.sim", "rewardAmount=25"))
		t.truthy(cvar.set("ads.sim", "rewardId=coins"))

		answer(t, function(done) return ads.load("rewarded", "bonus", done) end)
		local closed = answer(t, function(done)
			return ads.show("rewarded", "bonus", done)
		end)
		t.eq(closed.state, "dismissed")
		t.falsy(closed.rewardEarned, "a dismissal was reported as earned")
		-- the amount must be ZERO on the wrong branch, so a game that reads
		-- it anyway still grants nothing
		t.eq(closed.rewardAmount, 0, "a dismissal carried a reward amount")
		t.eq(closed.rewardId, "", "a dismissal carried a reward id")

		-- and now the branch that DOES pay
		t.truthy(cvar.set("ads.sim", "showResult=reward_earned"))
		-- a fullscreen unit is CONSUMED by its show, so it must be loaded again
		t.falsy(ads.isReady("rewarded", "bonus"),
			"a watched fullscreen unit stayed ready")
		answer(t, function(done) return ads.load("rewarded", "bonus", done) end)
		local earned = answer(t, function(done)
			return ads.show("rewarded", "bonus", done)
		end)
		t.truthy(earned.rewardEarned, "the earned signal did not arrive")
		t.eq(earned.state, "reward_earned")
		t.eq(earned.rewardAmount, 25)
		t.eq(earned.rewardId, "coins")

		-- GRANT DURABLY INSIDE THE CALLBACK. Unlike a purchase there is no
		-- store queue to re-deliver a reward, so this callback is the only
		-- chance the game gets - the flush is the whole crash defence.
		save.set("coins", save.getNumber("coins", 0) + earned.rewardAmount)
		save.flush()
		t.eq(save.getNumber("coins", 0), 25, "the reward was not made durable")
	end)

test("owning a no-ads product silences the interruptive formats only",
	{ scene = "scenes/main.oscene" }, function(t)
		armAds(t)
		t.falsy(store.adFree(), "ads were already suppressed")

		-- an interstitial loads perfectly well before the purchase
		local before = answer(t, function(done)
			return ads.load("interstitial", "gate", done)
		end)
		t.truthy(before.ready, before.reason)

		-- THE LINK BETWEEN THE TWO SEAMS: remove_ads carries noads in the
		-- project's own catalog, so buying it suppresses ad serving without
		-- the game wiring anything up
		local bought = answer(t, function(done)
			return store.purchase("remove_ads", done)
		end)
		t.truthy(bought.owned, bought.reason)
		save.set("adFree", true)
		save.flush()
		store.finish(bought.transactionId)
		t.truthy(ads.adFree(), "the ad side does not see the entitlement")

		-- LOADING is refused as well as showing, so a paying player's data is
		-- never spent on inventory that could never be presented. And the
		-- refusal is NAMED - silence is indistinguishable from a slow network.
		local suppressed = answer(t, function(done)
			return ads.load("interstitial", "after", done)
		end)
		t.falsy(suppressed.ready, "an interstitial loaded for a paying player")
		t.eq(suppressed.state, "suppressed")
		t.truthy(suppressed.suppressed, "the suppression did not name itself")

		-- REWARDED IS DELIBERATELY LEFT RUNNING: it is an advert the player
		-- CHOSE to watch in exchange for something, so silencing it for a
		-- paying player removes a mechanic they still want.
		local rewarded = answer(t, function(done)
			return ads.load("rewarded", "still_here", done)
		end)
		t.truthy(rewarded.ready,
			"a paying player lost the opt-in rewarded advert too")
	end)

test("a request with no onComplete is refused, not fired and forgotten",
	{ scene = "scenes/main.oscene" }, function(t)
		armAds(t)
		-- a rewarded advert whose answer nobody reads takes the player's time
		-- and grants nothing
		t.falsy(ads.load("rewarded", "x"), "a load with no callback was taken")
		t.falsy(ads.show("rewarded", "x"), "a show with no callback was taken")
		-- and a format nobody can name is refused rather than guessed at
		t.falsy(ads.load("billboard", "x", function() end),
			"an unknown ad format was accepted")
	end)
