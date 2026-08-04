# Monetization

Selling things and serving adverts, behind one seam with pluggable providers.
Two halves — a **store** side (products, purchases, entitlements, restore) and
an **ads** side (banner, interstitial, rewarded, app-open) — that meet at a
single place: a purchased *no-ads* entitlement suppresses ad serving.

Everything here is **asynchronous by construction**, the discipline
[the HTTP client](http.md) establishes: a request returns a handle immediately
and its one answer is delivered from `MonetizationService::update()` at a frame
boundary. A payment sheet or an advert callback therefore never lands in the
middle of a world update.

The code lives in `orkige_core/core_monetization/`. It is renderer-free,
platform-free and headless, so the whole contract unit-tests without a device.

## Every provider is a plugin

`MonetizationService` holds a `StoreProvider` and an `AdProvider` and knows
nothing else about either. There is no privileged built-in path — the simulated
provider shipped with the engine goes in through the same interface a vendor
integration would, which is what keeps the interface honest. An extension seam
none of our own code has to use is reliably inadequate.

```cpp
MonetizationService & money = MonetizationService::getSingleton();
money.setStoreProvider(std::make_unique<SimulatedStoreProvider>());
money.setAdProvider(std::make_unique<SimulatedAdProvider>());
```

Both interfaces share a threading contract: every method is called on the main
thread, a provider does its work wherever its platform requires, and it
publishes results **only** through `poll()`, which `update()` drains once per
frame.

## Products carry one identifier per storefront

A store console is an independent registry. The identifier a product is sold
under differs per platform: one may be unavailable elsewhere, a product
re-created after a mistake gets a new one, and a game ported later inherits
whatever was already registered. `ProductCatalog` therefore maps one **logical**
id to a per-storefront column:

```cpp
Product removeAds;
removeAds.id           = "remove_ads";   // what game code says, forever
removeAds.kind         = PK_NON_CONSUMABLE;
removeAds.grantsNoAds  = true;           // THE link to the ads side
money.catalog().add(removeAds);
money.catalog().addStoreId("remove_ads", SF_IOS,     "com.example.game.removeads");
money.catalog().addStoreId("remove_ads", SF_ANDROID, "remove_ads_v2");
```

Game code, scenes, scripts and analytics only ever speak the logical id. Both
directions of the mapping are load-bearing: a **restore** hands back a list of
storefront identifiers with no request to correlate them against, so without the
reverse index an entitlement cannot be named at all.

Product kinds decide what owning one means:

| Kind | Meaning |
| --- | --- |
| `PK_CONSUMABLE` | bought repeatedly and **spent** — produces no lasting entitlement |
| `PK_NON_CONSUMABLE` | bought once, owned forever, survives reinstall through restore |
| `PK_SUBSCRIPTION` | renews and **expires** — `active` is time-dependent |

Prices are never hard-coded. `displayPrice` is the storefront's own localised,
currency-correct string and is the only price fit to show a player; it is empty
until a product query completes, so a store screen shows a placeholder rather
than a wrong number.

### The catalog is a file, not code

The identifiers are decided in each store's console by whoever set the products
up, and they change — a product re-created after a mistake gets a new one. So a
project writes them down in a `.ocatalog` config asset named by the manifest
Setting `store.catalog`, which an agent authors over `write_project_file` and
the export ships automatically:

```
version 1

# the most common purchase in existence
product remove_ads
	kind non_consumable
	noads true
	ios com.example.game.removeads
	android remove_ads_v2

product coins_500
	kind consumable
	ios com.example.game.coins500
```

`product` opens a block; `kind` and `noads` describe it; every other line is a
storefront column (`ios`, `android`, `macos`, `windows`, `web`, `simulated`).
Keywords are case-insensitive, identifiers are not. **Anything the parser does
not understand is refused with its line number** — a typo silently ignored here
is an unbuyable product discovered by a player — and a failed parse leaves the
catalog EMPTY rather than half-read, so a game cannot end up selling some
products and silently refusing others. `ProductCatalogFile` is the reader, and
`serialize` is its byte-stable inverse.

**Nothing secret lives here.** Store product identifiers are public: they travel
to the storefront from the player's own device and appear in every receipt.
Signing credentials and API keys follow the split in
[security](security.md) — the manifest carries descriptive keys only.

### Which store stands behind the seam

The manifest Setting `store.provider` picks it: `platform` (the default),
`simulated`, or `none`. An unset value means `platform`, and **the simulator is
never a fallback that a missing platform store decays into** — a shipped game
running against it would hand every product out for free and look entirely
correct doing so, so it has to be asked for by name.

## Purchases have six unhappy endings

```cpp
money.purchase("remove_ads", [](PurchaseResult const & result)
{
    switch (result.state)
    {
    case PS_PURCHASED:
    case PS_ALREADY_OWNED:  grant(); break;          // both are ownership
    case PS_PENDING:        showAwaitingApproval(); break;  // grant NOTHING
    case PS_CANCELLED:      break;                   // not an error
    case PS_DECLINED:
    case PS_UNAVAILABLE:
    case PS_FAILED:         report(result.reason); break;
    }
});
```

Three of these are routinely mishandled:

- **`PS_CANCELLED`** is the player closing the payment sheet. It is not an
  error and must not raise one.
- **`PS_ALREADY_OWNED`** is a *success* for the player — they paid once
  already. The entitlement is granted rather than an error shown.
- **`PS_PENDING`** is deferred: parental approval or an offline payment method
  can settle it minutes or days later, **in a later session**. Treating it as a
  failure loses the sale when the approval lands; treating it as a success hands
  out goods nobody paid for. A deferred purchase that settles later arrives as
  an unsolicited event with request id `0`, named through the catalog's reverse
  index.

A settled transaction must be acknowledged with `finishTransaction`. A store
that is never told the goods were delivered will refund the purchase and, for a
consumable, refuse to sell it again.

## The unacknowledged-purchase window

Between "the store charged the player" and "the game acknowledged the purchase"
the app can die — a crash, the player swiping it away, the operating system
reclaiming a backgrounded game. That gap is the classic money bug: a game whose
own memory is the record of what was bought loses the purchase, and the player
paid for nothing.

**The store's queue is the record, not ours.** A settled transaction stays in
the platform's payment queue until it is explicitly finished, and is
re-delivered on every launch until then. So the engine persists nothing to
survive a crash. It lets the queue re-deliver, and the re-delivery arrives
through the ordinary unsolicited path with request id `0`, named through the
catalog's reverse index, exactly as a deferred purchase settling late does. A
game handles both with the same code.

Which makes **the order load-bearing, and it is the one rule to take away**:

> Acknowledge only AFTER the goods are durably granted.

```lua
store.purchase("coins_500", function(res)
    if not res.owned then return end
    save.set("coins", save.getNumber("coins", 0) + 500)
    save.flush()                    -- durable FIRST
    store.finish(res.transactionId) -- acknowledge SECOND
end)
```

Finish first and a crash one instruction later loses the purchase for good — the
queue has already forgotten it. Grant first and the worst case is the store
re-offering something the player already has, which a game absorbs by granting
it again. The asymmetry is deliberate: the failure mode of granting twice is
recoverable, the failure mode of charging without granting is not.

`StoreTransactionLedger` is what makes this provable rather than asserted. It
holds the open set, reports the same transaction arriving twice inside one
session so the game is told once, refuses to acknowledge a transaction that was
never delivered (a silent no-op there would hide a game tracking transactions
the store does not agree with), and is deliberately in-memory — persisting it
would create a second record that can disagree with the store's.

**A failed transaction is finished immediately**, and that asymmetry is the same
reasoning from the other side: there are no goods to grant, so there is no
window to protect, while leaving it in the queue would re-deliver the same
failure on every launch forever.

None of this needs a store to be verified. The ledger, the request-to-delivery
correlation, the transaction-state translation and the error taxonomy are pure
and unit-tested headlessly.

## Entitlements are a cache, never a save

The platform is the source of truth. `MonetizationService` deliberately has no
persistence: a save file does not travel with the player's account to a
reinstall or a second device, and one the player can edit is not a proof of
payment. `restore()` is how ownership comes back — and an **empty** restore is a
success, not a failure, because a player who never bought anything restores
nothing.

## The platform's own store

`createPlatformStoreProvider()` is the one seam a host reaches for. Exactly one
translation unit is compiled per platform, chosen by the build, so no call site
above it carries a platform `#ifdef`:

| Platform | What is compiled |
| --- | --- |
| macOS + iOS | the system in-app purchase framework (StoreKit) |
| everywhere else | an honest, named absence |

**A platform with no store is not an error.** Most development happens on one.
`platformStoreAvailable()` answers the runtime question and
`platformStoreUnavailableReason()` names what is missing, so a refusal says
"this process has no app identity" rather than "the purchase failed".

The Apple provider uses the framework's older, Objective-C-reachable API. That
is not a fallback: the newer one is published for a language this engine does
not compile, and the older one is built around the **persistent payment queue**
that closes the window described above. It is a transaction pump and nothing
more — the observer is attached in `initialize()` before anything else happens,
so whatever a previous run left unacknowledged arrives immediately.

Its refusals are specific rather than generic, which is why `initialize()`
always succeeds on the platform: a `false` there would collapse several very
different developer-facing situations into the service's one
"the store is not available".

| Situation | What comes back |
| --- | --- |
| no app identity (a bare executable, not the packaged app) | every call refuses, naming it |
| no products in the catalog for this storefront | `completed = false`, "no products are configured for the ios storefront" |
| payments restricted on the device | `PS_DECLINED`, "this device is not allowed to make payments" |
| a purchase before its product query completed | `PS_UNAVAILABLE`, naming the identifier |
| a transaction state this build does not know | left **unacknowledged**, never finished, one log line |

The whole platform-specific decision surface is two integer translations
(`appleStorePhaseFromRaw`, `appleStoreFailureFromRaw`) living in pure code the
unit suite drives. The bridge that owns the framework `static_assert`s every
constant against the SDK it compiles against, so a renumbered enum is a build
failure rather than a mis-reported purchase.

Subscription expiry, promotional offers and price-tier introspection sit out;
each is a real feature, and the bridge stays a transaction pump until one of
them earns its keep.

## Buying something from Lua

```lua
store.products(function(res)
    if not res.ok then print(res.reason) return end
    for i = 1, res.count do
        showRow(res[i].title, res[i].displayPrice)   -- never a formatted number
    end
end)

store.purchase("remove_ads", function(res)
    if res.owned then                       -- purchased AND already_owned
        save.set("adFree", true) save.flush()
        store.finish(res.transactionId)
    elseif res.state == "pending" then      -- grant NOTHING yet
        showAwaitingApproval()
    elseif res.state == "cancelled" then    -- not an error
    else
        showError(res.reason)
    end
end)

store.restore(function(res) ... end)        -- THE reinstall path
store.owns("remove_ads")                    -- right now, from the platform
store.entitlements()                        -- the logical ids owned
store.adFree()                              -- the link to the ads side
```

`res.owned` is the field to branch on: it is true for a fresh purchase and for
one this account already paid for, so the commonest mistake — showing an error
to a player who is simply reinstalling — is not reachable through the obvious
code. `res.state` carries the token when a game wants the distinction.

Requests are async exactly like `http`: the call returns immediately and its one
answer arrives in `onComplete` at a frame boundary, drained in the same tick-order
slot the HTTP client's answers are, inside the pause fence. A host that owns no
store (the editor's edit mode) makes the whole table an honest no-op. The full
signature list is in [the Lua reference](lua-api.md).

## What an agent can reach

The authoring half needs no verb of its own: a `.ocatalog` and the manifest
settings that name it are ordinary project files (`write_project_file`), and a
game's purchase code is ordinary Lua. The read-back rides the existing
`MSG_STATS` stream, so [`get_state`](mcp.md) carries `store_provider`,
`store_ready`, `store_pending`, `store_ad_free` and `store_owned` while a play
session runs — the same route the streamed-music snapshot takes, because a
second channel for state the seam already holds would be a second thing to keep
in step.

What an agent deliberately cannot do is drive a real payment surface, for the
same reason no verb performs a git mutation. Pinning an unhappy path is the
simulated provider's job, and a project asks for it by name.

## Consent is an ordering constraint

Consent must be gathered **before** an ad provider initializes. That is encoded
in the shape rather than left to discipline: `AdProvider::initialize` is the only
entry point and it *takes* the gathered consent, and the service refuses to call
it at all while the status is `CS_NOT_GATHERED`.

```cpp
ConsentState consent;
consent.status             = CS_GRANTED;  // what the player answered
consent.trackingAuthorized = true;        // the OS permission, a separate gate
consent.childDirected      = false;       // a property of the PRODUCT
money.setConsent(consent);
money.initializeAds(/*testMode*/ true);   // refuses without the line above
```

Three independent gates, not one flag, because they come from three different
places and any one alone forbids personalisation. `CS_NOT_GATHERED` is **not** a
synonym for denied — a player who refused has been asked, so the surface comes
up and serves contextual inventory. Confusing the two switches advertising off
for everyone who declined tracking.

Withdrawing consent back to `CS_NOT_GATHERED` shuts the ad surface down rather
than leaving a network running on a permission that no longer exists.

The store side is **not** gated on consent. Privacy consent governs advertising
identifiers and personalised advertising, not payment; refusing to bring the
purchase surface up until a player answered a privacy dialogue would stop a
paying customer from paying.

**Test mode is first-class** and is an `initialize` argument for the same reason
every real mediation surface makes it one: test inventory is bound when the
network starts, and a development build serving live adverts generates invalid
traffic against the account that owns them.

## The four ad formats share names but not consequences

```cpp
money.loadAd(AF_REWARDED, "level_end", [](AdLoadOutcome const & r) { ... });
money.showAd(AF_REWARDED, "level_end", [](AdShowOutcome const & r)
{
    if (r.rewardEarned()) { grantCoins(r.rewardAmount); }   // THE branch
});
```

A **banner** is a platform view laid over the window. The engine never renders
into that strip and never learns about it from the render target, so a HUD
anchored to the bottom of the screen sits *underneath* the advert — and, because
no advert exists in development, the fault only appears on a device with live
inventory, usually after release. The seam therefore reports the geometry:

```cpp
SafeAreaInsets layout = money.layoutInsets(engine->getSafeAreaInsets());
```

`BannerGeometry::composeWith` folds the strip into the display's own safe-area
insets ([the safe-area model](gui.md)). A banner parked inside the safe area
adds its whole height to that edge; an edge-to-edge banner already covers the
display's inset, so only the part reaching beyond it takes new space. With no
banner on screen the insets come back unchanged, so a build that serves no
advertising lays out exactly as it would with no monetization at all.

**Interstitial, rewarded and app-open** are fullscreen takeovers. While one is
up, `isTakeoverActive()` reports it, and the host applies the same consequences
the backgrounding gate in `core_game/AppLifecycle` carries: stop
advancing the sim, suspend or duck audio, and on Android keep the system back
button away from the game — the advert owns the gesture. The service only
*reports* the state; the loop that owns the tick order is the one entitled to
skip it.

`AdPlacement` is the per-unit lifecycle, and every refusal is an explicit state:

| From | On | To |
| --- | --- | --- |
| idle / failed | `beginLoad` | loading |
| loading | `completeLoad(ALR_LOADED)` | ready |
| loading | any other verdict | failed (reason kept) |
| loading / ready / showing | `beginLoad` | **refused** `ALR_BUSY` |
| not ready | `beginShow` | **refused** `ASR_NOT_READY` |
| ready | `beginShow` | showing |
| showing (fullscreen) | any result | idle — **consumed** |
| showing (banner) | any non-error result | showing, until `hide()` |

Two differences matter. A fullscreen unit is **consumed** by being watched, so
the next show needs a fresh load; a game that assumes otherwise silently stops
showing adverts. And **show before ready is an error state**, never undefined
behaviour.

`ALR_NO_FILL` deserves its own mention: the request was perfectly valid and the
network simply had no advert to give. It is ordinary in low-traffic regions, is
not an error, must not be retried in a tight loop — and is the single most
likely thing to break a real game while never once appearing in development.

The **rewarded** branch is the one games get wrong. Mediation surfaces report
"the ad closed" and "the reward was earned" as two separate signals, and a game
that grants on close pays out for an advert nobody watched. `ASR_DISMISSED` and
`ASR_REWARD_EARNED` are mutually exclusive values of one enum, and the reward
amount travels only with the earned result — a dismissal always carries `0`.

## The two seams meet at no-ads

"Remove ads" is the most common purchase in existence, so owning any product
marked `grantsNoAds` suppresses ad serving. Which formats go quiet is
`AdPolicy`:

| Format | Suppressed when ad-free |
| --- | --- |
| banner, interstitial, app-open | yes |
| rewarded | **no** |

Rewarded is deliberately left running: it is an advert the player *chose* to
watch in exchange for something, so silencing it for a paying player removes a
mechanic they still want. A game that disagrees flips `suppressRewarded`.

Suppression is reported honestly — `ALR_SUPPRESSED` / `ASR_SUPPRESSED` — rather
than by silently doing nothing, which a game cannot distinguish from a network
that is merely slow. Loading is refused as well as showing, so a paying player's
data is never spent on inventory that can never be presented.

## The simulated provider

Its value is not a convincing fake advert. It is making the **unhappy paths
reachable on demand**, because real networks produce them rarely and
unpredictably — which is exactly why shipped games mishandle them.

It is **deterministic, with no randomness anywhere**. A simulator that rolled
dice would produce a test that passes sometimes, which is worse than no test.
Latency is counted in **poll ticks** (one tick = one `update()`), never in
wall-clock milliseconds, for the same reason.

Scenarios are plain data, settable through `SimulatedScenario::apply(key, value)`
— the pure parse a cvar or an agent-facing verb drives:

| Key | Values |
| --- | --- |
| `loadResult`, `loadResult.<format>` | `loaded`, `no_fill`, `error`, `timeout`, `busy`, … |
| `showResult` | `completed`, `dismissed`, `reward_earned`, `not_ready`, `error` |
| `rewardId`, `rewardAmount` | the reward a rewarded unit grants |
| `purchaseState` | `purchased`, `cancelled`, `declined`, `pending`, `already_owned`, … |
| `restoreStoreIds` | comma-separated storefront identifiers a restore yields |
| `adInitializeFails`, `storeInitializeFails`, `productsUnavailable`, `restoreFails` | booleans |
| `bannerPosition`, `bannerWidth`, `bannerHeight`, `bannerInsideSafeArea` | the strip a banner occupies |
| `latencyTicks` | how many `update()` calls an answer waits |
| `reason` | the one-line reason every non-success reports |

`loadResult.<format>` exists because fill genuinely differs per format — a
banner fills where an interstitial does not.

The simulator reports the reward on **every** show outcome, exactly as a real
surface does, and lets the seam decide which outcome may carry it. That keeps
the guard in one place and lets a test prove it works instead of the simulator
quietly doing the same job twice.

## Honest limits

- **Receipt validation that resists tampering needs a server.** Every on-device
  check is spoofable: the process can be patched, the store client can be
  replaced and the response can be forged. `PurchaseResult::receipt` and
  `Entitlement::receipt` carry the storefront's opaque token so a game *can*
  hand it to a backend that validates it against the store's own service. The
  engine does not solve this and does not pretend to; anything of real value
  should be gated server-side.
- **Products must be configured in each store's console**, per platform, before
  any identifier resolves. The catalog maps names; it cannot create products.
- **What a real store account proves is untestable in CI.** A live product
  query, a real payment sheet, a genuine deferred approval and a purchase
  surviving a crash all need a signed app, a store account and a device; no
  automated run has any of them. What IS proved headlessly is everything above
  the framework call - the ledger, the request-to-delivery correlation, the
  state and error translation, the catalog parse, and that every refusal names
  itself and arrives exactly once. The seam is shaped so that is the whole
  remainder: the bridge passes integers to pure functions and objects to the
  queue, and nothing else.
- **The simulator is deterministic and a platform store is not.** A simulated
  run proves a game handles each outcome; it does not prove the platform
  produces them. Both statements are needed and neither substitutes for the
  other.
- **Only the STORE side has a platform provider, and only on Apple platforms.**
  The ad side is the simulated provider alone. On Android the platform half of
  either arrives as a library archive the project depends on -
  [android-libraries.md](android-libraries.md) is the packaging side of that.
- **Subscription expiry is carried, not enforced.** `Entitlement::active` and
  `expiryUnixSeconds` are whatever the platform last reported; the engine runs
  no clock against them and re-checking is a restore.
- **The banner strip can still make a HUD jump** when an advert fills after the
  layout settled. The geometry is reported the moment the banner attaches; a
  game that wants no movement reserves the space itself and uses the simulator
  to develop against it.
- **A browser build has no platform store of its own.** `SF_WEB` is a catalog
  column for whatever payment surface the page embeds, so the model does not
  change when a game ships there.
