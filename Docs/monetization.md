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

## Entitlements are a cache, never a save

The platform is the source of truth. `MonetizationService` deliberately has no
persistence: a save file does not travel with the player's account to a
reinstall or a second device, and one the player can edit is not a proof of
payment. `restore()` is how ownership comes back — and an **empty** restore is a
success, not a failure, because a player who never bought anything restores
nothing.

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
- **No vendor integration ships yet.** The simulated provider is the only
  provider. The interfaces are designed against published platform behaviour,
  not against a running integration, so the first real provider is also the
  first real test of the seam's shape.
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
