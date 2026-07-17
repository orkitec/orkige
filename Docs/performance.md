# Performance architecture — static mobility, sprite-run batching, instancing

How the engine turns *knowledge about content* (what never moves, what can
share a draw) into each render backend's native fast path — and how the
resulting structure is guarded so it cannot silently regress.

The governing rule (also in `Docs/render-abstraction.md`): **a facade
contract must be expressible as each backend's native fast path**. The
facade names the *intent* (static, batchable run); each backend realizes it
with its own machinery; neither backend is pessimized to serve the other.

---

## The three mechanisms at a glance

| Facade concept | Where declared | classic OGRE realization | Ogre-Next realization |
|---|---|---|---|
| **Static mobility flag** | `TransformComponent.static` (reflected bool) → `RenderNode::setStatic` | entities on static nodes bake into shared `Ogre::StaticGeometry` regions (fewer draw calls; membership bookkeeping in `StaticBakeClassic.cpp`) | node + attached content migrate into the `SCENE_STATIC` memory managers (no per-frame transform derivation / cull prep — the static managers' whole point) |
| **Sprite-run batching** | automatic per frame (`SpriteBatcher` over the pure `core_util/SpriteRunPlanner`) | one facade `SpriteBatch` per run = one `ManualObject` draw under the run's shared `Sprite/<tex>#<sampler>` material | same facade path over the v2 `ManualObject` + shared HlmsUnlit datablock |
| **Same-mesh instancing** | *(no facade surface)* | **not built — gated out** (see the verdict below) | native: the Hlms auto-instances identical Items with one shared vao/datablock — nothing to declare |

Toggles (live cvars, both default **on**; the honest escape hatches and the
levers the identity tests flip):

- `r.staticScene` — apply the static flag to the renderer. The **editor
  boots it off** (edit mode: gizmo moves must never fight the mobility
  contract; the flag still round-trips through the inspector).
- `r.spriteBatching` — merge sprite runs. Off releases every run live and
  sprites draw individually again.

---

## Pillar 1 — the static mobility flag

### The flag and its home

`TransformComponent` carries the reflected `static` bool (inspector,
serialization, prefab overrides, Lua weak-handle
`getStaticFlag`/`setStaticFlag`, debug protocol and MCP all ride the ONE
property registry; the schema declares it **after** position/orientation/
scale so a scene load *places* an object before *freezing* it).

The flag lives on **TransformComponent, not ModelComponent**, deliberately:

1. The flag is a statement about **the transform** — "this object's world
   pose never changes". TransformComponent owns that pose.
2. One node-level seam catches **every** runtime move path (component
   setters, Lua handles, the debug protocol, teleports) — a mesh-level flag
   could not see its parent node moving.
3. It benefits everything attached to the object — mesh **and** sprite
   **and** vector shape (on next, `Node::setStatic` migrates attached
   movables along with the node; sprite/vector content on static objects
   stops paying per-frame transform/AABB updates too).
4. Ogre-Next's own API agrees: static is a *node* property that cascades to
   attached objects.

The component cascades to the content nodes its sibling components own
(facade children without a user pointer — only TransformComponent tags
nodes, the documented back-mapping convention) and stops at child objects'
transforms: **children flag themselves**. Nodes created *after* the flag
inherit it (`RenderNode::createChild`); content attached late aligns on
attach.

### The hierarchy rule (validated, honest error)

A static node's frozen world transform embeds every ancestor's pose, so:

- `static = true` requires a **static (or absent) parent**;
- `static = false` is refused while **static children** depend on it.

`TransformComponent::staticFlagChangeError` is the pure rule
(`StaticFlagTests`); the setter logs an `oDebugError` and refuses. The scene
loader parents children after parents, so legitimate content always loads.

### THE MOBILITY CONTRACT

**Static means static.** A runtime mutation of a static object stays
*correct* but is a contract violation: the backend logs **one warning per
node** and lands the change through its repair path —

- **next**: `SceneManager::notifyStaticDirty` re-derives the frozen
  transforms next frame. Correct but costly (the exact cost you opted out
  of paying per frame).
- **classic**: the object's baked entities **demote** out of their
  StaticGeometry region (one rebuild, coalesced to the frame boundary) and
  render individually, following the node again. Draw count goes up by the
  demoted entities.

Visibility changes are gentler: on next a static object shows/hides freely
(visibility is not a transform); on classic the bake re-filters its regions
at the next frame boundary (membership follows `entity->getVisible()`) —
correct, but a *blinking* static object rebuilds regions every toggle, so
flag blinkers dynamic.

Editor gizmo moves are exempt by construction: the editor boots
`r.staticScene=0`, so edit mode never applies the flag to the backend.

### The classic bake, in one paragraph

`StaticBakeClassic.cpp` owns only membership bookkeeping around OGRE's own
`StaticGeometry`: registration follows the facade flag and mesh
attach/detach; every change just marks the bake dirty; `renderOneFrame`
flushes **at most one rebuild per frame** (a scene load registering dozens
of entities pays a single build). Two buckets split by the entity's
`castShadows` flag (shadow casting is region-level in OGRE, so per-object
cast flags survive the bake). Baked sources are suppressed via **visibility
flags 0** — never the `visible` flag, which remains the game's own
show/hide state and keeps flowing through node cascades.

### The trade asymmetry (when to flag content static)

| | draw-cost win | rebuild/repair cost |
|---|---|---|
| **classic** | large: N same-bucket entities → ~1 region draw (`fixture_static`: 9 → 5 batches; a real scene's whole prop set collapses) | region rebuild copies vertex buffers — fine at scene-load/edit frequency, **never** per frame |
| **next** | none directly (the Hlms already auto-instances identical items) — the win is **CPU**: static items skip per-frame transform derivation and cull prep entirely | `notifyStaticDirty` is cheap-ish but defeats the point when spammed |

Flag: terrain, buildings, props, backdrops — anything placed once. Don't
flag: anything scripted/tweened/physics-driven, pooled objects that
activate/deactivate per frame (the benchmark's ramped field cubes stay
dynamic for exactly that reason), blinking decorations on classic.

Content shipped flagged: the benchmark's terrain (all three terrain
vignettes), the vista props, Flatland's backdrop sprites. The roller
project ships **unflagged** on purpose: its walls ride sliding tiles (the
"move world" mechanic) — mobile by design, so the honest answer is the
dynamic path plus sprite-run batching.

### Pixel identity

`player_static_contract` (per flavor) renders `fixture_static.oscene` with
the gate on and off and compares framebuffers: **byte-identical on next**
(SCENE_STATIC only skips recomputation — the GPU sees the same matrices);
classic allows isolated 1-pixel float-rounding differences (measured 2 px
of 921600, worst channel delta 64) because StaticGeometry re-expresses
vertices relative to the region origin. The same test moves a static object
mid-run and asserts: exactly one warning, the move lands, the classic batch
delta is exactly +1, and the *moved* frame matches a fully-dynamic run of
the same move (the repair path is correct, not just warned).

## Pillar 2 — sprite-run batching

### The facade contract (grouping is ours, drawing is the backend's)

The pure `core_util/SpriteRunPlanner` owns the painter's contract:

- sprites sort by zOrder, **stable** — registration order (= scene load
  order) breaks ties, so co-planar overlap keeps creation order;
- only **contiguous same-(texture,sampler)** neighbours in that order form
  a run — an interleaved different-material sprite splits the run, because
  merging across it would reorder alpha-blended output;
- runs of one stay solo (their quad already costs exactly one draw);
- dirty tracking: a run whose members and state hashes are unchanged does
  not re-upload; **one moved member re-uploads only its own run**.

The engine-side `SpriteBatcher` (created by the player, ticked once per
frame after all gameplay/debug mutations, right before rendering; the
editor never creates one) realizes each run as ONE facade `SpriteBatch` on
the world root, filled with the members' world-space quads
(`SpriteComponent::buildWorldQuad` — position + orientation · (scale ·
corner), the exact composition the backend applies to the individual quad).
Batched members hide their individual quad (object-level flag only; node
visibility stays the game's state). The run's batch binds the **same**
per-(texture,sampler) material the individual quads use — merging changes
nothing but the draw count.

### Why the classic adapter is the facade SpriteBatch (the BillboardSet audit)

Classic OGRE's `BillboardSet` is built-in batched quads, but it cannot carry
this contract: a billboard is position + size + one rotation angle about the
facing axis — a sprite's full node transform (hierarchy-composed orientation,
non-uniform scale) is not expressible per billboard, and its texcoord
rotation semantics diverge from the quad path. The facade `SpriteBatch`
already exists on both backends as the proven one-draw N-quad primitive with
byte-compatible vertex rules (same winding, same generated materials, same
zOrder window — the 2D particle system ships on it), so the batcher feeds it
and each backend keeps its native realization. That is the
facade-groups/backends-decide boundary in practice — and it means a future
backend gets sprite batching for free by implementing `SpriteBatch` once.

### Costs and behavior

- An unmoved scene uploads nothing per frame; the per-frame cost is reading
  each registered sprite's world transform for the dirty hash.
- A flipbook or continuously-moving member re-uploads its run each frame —
  one buffer refill for the run, which replaces N per-quad refills; heavy
  independent movers in one huge run can be split by giving them distinct
  zOrders if profiling ever demands it.
- `player_spritebatch` pins the structure **exactly** against
  `fixture_sprites.oscene` (painter sequence A,A,A,B,A,A,A,A,A + two more
  at z=1 → 3 runs + 1 solo): classic draws 4 vs 11, the live toggle
  releases and re-forms the runs, a moved member re-uploads, and batching
  on/off renders identical pixels (byte-exact on classic; a 1-LSB edge
  fringe of ~10 px allowed on next, where the merged path bakes the world
  transform on the CPU while the per-quad path applies it in the vertex
  shader — float32 sums round differently).

## Pillar 3 — classic 3D auto-instancing: GATED OUT (the verdict)

The measured gap (Instance Field, ~190 ramped same-mesh cubes, Debug,
Apple M-series): classic ~187 draw batches vs next ~4 — Ogre-Next's Hlms
auto-instances identical Items natively, classic draws each entity. The
InstanceManager promote/demote seam was **not built**, on evidence:

1. **RTSS cannot derive the instanced vertex path.** OGRE 14.5's
   InstanceManager techniques (ShaderBased/VTF/HW) all require materials
   whose vertex programs read per-instance world matrices; the shipped RTSS
   surface (`RTShaderSystem/` headers, `FFPLib_Transform.glsl`) generates no
   such path, and our classic materials are exclusively RTSS-generated
   Cook-Torrance. Hand-writing per-material instanced shaders would break
   the generated-materials discipline (CLAUDE.md) for one flavor.
2. **The numbers don't justify it for real content.** The 187-draw scene
   costs ~1.7 ms/frame in a Debug build on desktop classic — inside any
   frame budget. The classic flavor's real 3D scenes (vista: 16 batches
   before the static bake) never approach the pathological count; classic's
   shipping role is the 2D tier (web/GLES2), where pillar 2 applies.
3. **Static overlap.** Immobile same-mesh fields are already collapsed by
   the pillar-1 StaticGeometry bake on classic; the residual gap is *moving*
   same-mesh crowds, which no shipped or planned content has.

Revisit trigger: a real classic-flavor game with hundreds of moving
same-mesh instances. The facade seam sketched for that day: promote N
same-(mesh,material) `MeshInstance`s above a threshold, demote on any
individualization — but it earns its complexity only with the content.

## The structural budget gate (the standing guard)

`run_benchmark_budget_test.py` + the checked-in
`tests/integration_driver/benchmark_budgets.json` walk every benchmark
scene per flavor (deterministic boots: ramps at ceiling, camera frozen) and
gate the **structure**:

- draw batches above `batchesMax` → fail: a batching/facade regression;
- draw batches below `batchesMin` → fail: something stopped rendering
  (the silent-black class — dramatically under budget is a bug, not a win);
- triangles above `trisMax` → fail: content/LOD regression;
- frame-ms: **reported, never gated** (machine noise).

Budgets change **deliberately**: the commit that moves a number edits the
table in the same change, with the reason — the pixel-probe-baseline
discipline. The current table's numbers are the measured post-optimization
values with headroom for legitimate jitter (ramp pacing, HUD lines).

Known metric caveat: the next flavor's `FrameStats.batchCount` (Metal
`RenderingMetrics`) compresses absolute counts (auto-instanced draws count
once), so next budgets are tighter in absolute terms and mostly guard
*deltas*; classic counts are true draw calls.

## Measured results (macOS, Apple M-series, Debug trees, 240–300-frame deterministic boots)

Draw batches (avg over the run; the structural number the gate holds):

| Scene | classic before | classic after | next before | next after |
|---|---|---|---|---|
| Terrace Vista | 16.1 | 10.0 | 7.1 | 7.1 |
| Instance Field | 187.2 | 187.4 | 4.1 | 4.1 |
| Flatland | 6.4 | 5.0 | 6.1 | 5.1 |
| fixture_static (9 meshes) | 9.0 | 5.0 | 2.0 | 2.0 |
| fixture_sprites (11 sprites) | 11.0 | 4.0 | 4.0 | 3.0 |

Notes for the reader of the deltas:

- **Vista classic 16→10**: the static bake collapses terrain + five props
  into two region buckets. Next's count is unchanged — its win is the CPU
  side (static items leave the per-frame transform/cull pipeline), which
  vsync-clamped Debug frame times cannot resolve; the structural numbers
  are the honest measure on this rig.
- **Instance Field**: untouched by design — the cubes are pooled/ramped
  (activation churn would thrash the classic bake) and the classic
  instancing pillar is gated out (verdict above). The 187-vs-4 gap *is* the
  documented flavor difference.
- **Flatland / fixtures**: sprite-run batching. Real 2D scenes batch by
  content structure: the roller's tile sprites share one texture and batch
  within their z-layers during play.
- Frame-ms on this rig: classic Debug runs 0.7–1.7 ms/scene (real deltas
  drown in vsync/jitter), next is vsync-clamped near 8.3 ms — which is why
  the gate holds structure, not milliseconds, and why frame-ms stays
  reported-only.
- The **web** flavor (classic GLES2→WebGL) could not be measured in this
  pass: the build host's x86-translation layer was wedged (unkillable
  `wasm-metadce`), so the wasm link never completed. The classic numbers
  are the proxy; draw-call savings matter *most* there, where each call
  crosses the JS/WebGL boundary.

## Test map

| Concern | Test |
|---|---|
| grouping contract + dirty tracking (pure) | `SpriteRunPlannerTests` (unit) |
| static hierarchy rule + schema + round-trip (pure/headless) | `StaticFlagTests` (unit) |
| static toggle pixel identity + draw win + mobility contract | `player_static_contract[_next]` (integration, per flavor) |
| sprite batching exact counts + live toggle + moved member + pixel identity | `player_spritebatch[_next]` (integration, per flavor) |
| per-scene structural budgets over the whole tour | `benchmark_budget[_next]` (integration, per flavor) |
| generator fixtures stay contract-shaped | `make_benchmark_assets_selftest` (unit) |
