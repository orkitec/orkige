# Render abstraction (`engine_render`) — audit, design, plan

Phase A0 deliverable (2026-07-08). Owner directive: dual backend **classic OGRE 14**
(what ships today) + **Ogre-Next**, extensible to **Filament** as backend #3 later.
Backend selection is **build-time only**: classic OGRE and Ogre-Next define the same
`Ogre::` symbols, so linking both into one binary violates the ODR — there is
deliberately no runtime switch (`ORKIGE_RENDER_BACKEND`, see "Build flavors").

This phase produced the facade **interface headers** in `orkige_engine/engine_render/`
(compiling, backend-free, no implementation yet) and this document. No behavior change.

---

## Open design questions — all DECIDED (owner sign-off 2026-07-08)

1. **AnimationComponent root-motion extraction** digs into `Ogre::Bone` /
   `NodeAnimationTrack` / `TransformKeyFrame` (backing up and restoring keyframes of the
   driving bone). That is far below scene-graph level and has no 1:1 Ogre-Next/Filament
   shape. Proposal: keep it a **classic-only backdoor** inside the classic backend and
   add a facade `getBoneWorldTransform(name)`-style API only when a game actually needs
   root motion on another backend. Accept?
   **DECIDED: yes — root motion stays a classic-only backdoor; no facade bone API until a real cross-backend need.**
2. **HUD strategy** (detail in the mapping table): recommendation is **Gorilla stays
   classic-only**; the future cross-backend HUD is built on facade `SpriteQuad` +
   screen-space camera. That means fastgui HUDs (jumper, roller) do not run on the
   Ogre-Next backend until the facade HUD exists (A3). Acceptable sequencing?
   **DECIDED: yes — Gorilla/fastgui HUDs are classic-only until the A3 facade HUD lands.**
3. **Which backend runs the editor?** ImGuiOverlay, RTSS probes and the OverlaySystem
   wiring are classic glue. Cheapest path: the editor stays a classic-backend app
   indefinitely; games choose their backend; RenderTexture/picking still go through the
   facade so an eventual editor-on-Next is unblocked but unscheduled. Confirm.
   **DECIDED: confirmed — the editor stays a classic-backend app; games pick their backend.**
4. **Math alias tradeoff**: with "Orkige math = Ogre math" the facade headers
   transitively include Ogre *math* headers (only math — no scene/render types). Zero
   churn for classic+Next; must be swapped to engine-owned types before Filament.
   Accept the deferred cost? (Recommended yes — see "Math types".)
   **DECIDED: yes — alias now (`RenderMath.h` is the swap point), engine-owned types before any Filament work starts.**
5. **`RenderCamera::setWireframe`** (Engine wireframe debug mode) has no Filament
   equivalent (Filament has no polygon-mode toggle). Documented no-op there?
   **DECIDED: yes — documented no-op on Filament; classic/Next keep the polygon-mode toggle.**
6. **Dead legacy renderables**: `ColoredBoundingBox`, `LightMap`, `CameraUtil.h`,
   `OverlayUtil.h`, `SerializationUtil.*` have **zero callers** (MovableText's only
   caller is the unbuilt sceneoptimizer). Proposal: drop them from the build in A1
   instead of dragging them through the abstraction (recoverable from git).
   **DECIDED: yes — unbuilt AND deleted in A1 (incl. MovableText; recoverable from git). Done in WP-A1.1.**
7. **Multi-window**: Engine carries an 8-window array; every call site uses window 0.
   The facade models exactly one main window. OK to freeze that until a real
   multi-window need appears?
   **DECIDED: yes — single main window frozen; the facade models exactly one until a real need appears.**

---

## Audit

Counted on 2026-07-08 (`grep -rEo '\bOgre::'`, `.h/.cpp/.mm`): **2882 references in
99 files** above `engine_graphic` (engine modules w/o engine_graphic + tools + samples
+ tests + projects), plus 619 in `engine_graphic` itself (which *is* the classic
backend and is allowed to be Ogre).

| Area | refs | files | character |
|---|---|---|---|
| engine_fastgui (incl. Gorilla) | 1177 | 33 | Gorilla = deep render coupling; widgets = math + Gorilla calls |
| engine_util | 690 | 12 | SceneNodeGuard mirror, mesh/hw-buffer helpers, converters |
| tools/editor | 211 | 8 | RTT, picking, gizmo matrices, ImGuiOverlay, stats — plus math |
| engine_gocomponent | 189 | 14 | components: scene handles + math + AnimationState |
| engine_physic | 176 | 4 | PhysicsWorld math-only; CollisionTools = legacy RaySceneQuery |
| samples + projects | 165 | 5 | Engine facade + ManualObject demo geometry + math |
| engine_filesystem | 88 | 4 | `Ogre::Archive`/`ArchiveFactory` subclasses |
| tests | 79 | 7 | math values + headless ConfigFile parsing only |
| sound/input/base/module/runtime | 80 | 11 | math-only leaks + ConfigFile + Lua exports |
| tools/player | 27 | 1 | Engine facade + services + math |

### Buckets

**D — math types (~54%, 1563 refs).** `Real` 457, `Vector3` 438, `Vector2` 342,
`ColourValue` 171, `Quaternion` 77, `Radian` 20, `Degree` 14, `Matrix4` 13, `Ray` 11,
`Matrix3` 9, `Vector4` 8, `AxisAlignedBox` 3. Pervades component public APIs, Lua
bindings (`module.cpp` registers `Ogre::Vector3/Vector2/Quaternion` usertypes),
serialization (`TransformComponent::save` writes vec/quat fields) and all tests.
→ handled by the **math decision** below, not by the facade classes.

**A — facade-mappable (the scene-graph surface).** Everything the new headers cover:
node transforms/hierarchy (SceneNodeGuard *usage* — the guard mirrors ~60 SceneNode
methods, callers use ~25), entities (`ModelComponent`), sprite quads
(`SpriteComponent`), cameras + projection (`CameraComponent`, Engine ortho helpers),
ambient light, AABB-level ray picking (editor), RTT (editor scene panel), screenshots
(5 call sites), frame stats (editor + hello_orkige + FrameStatsUtil), resource
locations (every app), window resize/background, `AnimationState` control surface.

**B — needed API the old facade lacked** (apps reached for raw Ogre *only because
`Engine` had no method*): resource-location registration, ambient light, screenshots,
stats, RTT, picking, unlit-vertex-colour fixup, project/unproject + view/proj matrices
(gizmo). All now have facade homes (see "Facade surface").

**C — classic-only zones** (implementation detail of the classic backend, NOT to be
abstracted): see per-file table below.

### Per-file recommendation — classic-only candidates

| File(s) | Verdict | Rationale |
|---|---|---|
| `engine_fastgui/Gorilla.{h,cpp}` | **backend-private (classic-only)** | Bundled fork; manual `RenderOperation` + `HardwareBuffer::lock` + `RenderQueueListener::renderQueueEnded` → `manualRender`, programmatic `Pass` materials, `SimpleRenderable`, texel offsets — every hard Ogre-Next break at once. Porting it = rewriting it; the cross-backend HUD is the facade-sprite HUD instead. |
| `engine_fastgui/FastGui*` widgets/view | **keep, de-Ogre opportunistically** | Math + Gorilla calls only; survive as-is wherever Gorilla runs. When the facade HUD lands (A3), widgets get a draw-surface seam instead of `Gorilla::Layer*`. |
| `engine_fastgui/FastGuiManager` | **classic-only edges isolated** | `RenderTargetListener` + Material/TextureManager cleanup + `getStatistics` are thin; they move behind the backend seam with Gorilla. |
| `engine_fastgui/FastGuiFactory`, `engine_base/Localisation` | **de-Ogre in passing** | Subclass `Ogre::ConfigFile` for INI parsing only — replace with a small engine parser (or keep classic-only; nothing renders here). |
| `tools/editor` ImGuiOverlay/OverlaySystem glue | **backend-private editor glue** | `Ogre::ImGuiOverlay` is a classic-OGRE component; Ogre-Next has its own imgui integration pattern, Filament its own renderer backend for imgui. Isolate behind a small `EditorImGuiBackend` seam *inside the editor* in A1; do not put imgui into engine_render. |
| `engine_graphic/IngameConsole` | **classic-only, candidate to unbuild** | `Rectangle2D` + Overlay; live users are only `module.cpp` (Lua export) and an InputManager toggle. Keep classic-only; revisit when a cross-backend console is wanted (could be rebuilt on fastgui/facade sprites). |
| `engine_graphic/MovableText`, `DynamicLines`, `DynamicRenderable` | **classic-only; MovableText candidate to unbuild** | Ogre `SimpleRenderable`/`MovableObject` subclasses = per-backend by nature. DynamicLines' only user is the unused ColoredBoundingBox; MovableText's only user is the unbuilt sceneoptimizer. |
| `engine_graphic/ColoredBoundingBox`, `LightMap` | **unbuild (question #6)** | Zero callers. |
| `engine_util/SceneNodeGuard` | **superseded by `RenderNode`** | The facade carries only the used ~40% of its mirror. WP-A1.2 reshaped the guard into the components' facade-node-owner base (holds `optr<RenderNode>`, forwards ~15 used methods); deleted at the end of A1 (WP-A1.5) when components hold the optr directly. |
| `engine_util/MeshUtil` | **backend-private** | Raw vertex/index buffer extraction; only caller is CollisionTools' triangle raycast (being superseded by PhysicsWorld) + unbuilt sceneoptimizer. Moves behind the classic seam; Ogre-Next equivalent only if triangle-accurate *render-mesh* picking is ever needed there. |
| `engine_util/PrimitiveUtil` | **split** | "EditorCube" mesh + vertex-colour-unlit fixup are wanted on every backend → facade (`MeshInstance::setVertexColourUnlit`; cube-mesh factory becomes a backend service in A1). ManualObject guts stay backend-private. |
| `engine_util/StringConverter` | **keep (math-adjacent)** | Converts math types + scalars; follows whatever the math decision says (aliases keep it working on both Ogre backends; own-types rewrite it). |
| `engine_util/CameraUtil.h`, `OverlayUtil.h`, `SerializationUtil.*` | **unbuild (question #6)** | Zero callers; SerializationUtil's Light/Entity round-trip is superseded by component save/load. |
| `engine_util/NodeUtil` | **absorbed by facade RAII** | Its recursive destroy dance exists because raw SceneNodes have no ownership; `RenderNode`/`MeshInstance` handles are RAII. `getGameObjectFromNode` → `RenderNode::setUserPointer`/`findUserPointerUpwards` (used by picking). |
| `engine_physic/CollisionTools` | **retire in A1** | Legacy `RaySceneQuery` + triangle tests; already superseded by `PhysicsWorld::castRay` (physics) and `RenderWorld::queryRay` (editor AABB picking). Live callers: editor main.cpp (migrates), CameraDefaultModes (terrain follow — stub anyway), unbuilt tools. |
| `engine_filesystem/BigZip*` | **backend-facing but portable to Next** | `Ogre::Archive`/`ArchiveFactory` exist in Ogre-Next too (minor API drift). Stays as-is for both Ogre backends behind `RenderSystem::addResourceLocation(LT_BIGZIP)`; Filament gets an impl-side VFS. |
| `engine_sound`, `engine_input` | **math-only leak** | `Ogre::Vector3`/`Ogre::Camera*` listener. Math: alias handles it. `SoundManager::setListener(Ogre::Camera*)` → take `optr<RenderCamera>` or a node in A1 (one-line seam). |
| `engine_runtime/PlayerRuntime` | **math-only leak + LogListener** | Wire-format vec/quat formatting (math alias) and an `Ogre::LogListener` (duplicated in the editor) — fold log forwarding into a small engine service in A1 (not part of the render facade; OGRE's LogManager is incidentally also present in Next). |
| `engine_module/module.cpp` Lua exports | **migrate in A1** | Currently registers `Ogre::SceneNode/SceneManager/Viewport/Camera` usertypes. Re-target the same Lua-facing names at `RenderNode`/`RenderWorld`/`RenderCamera` (optr binds natively in sol2). Math usertypes follow the math decision (aliases = unchanged today). |
| tests | **math + headless parsing only** | No test touches a render backend; FastGuiAtlasTests' `Ogre::ConfigFile` follows FastGuiFactory's fate. |

---

## Facade surface (headers in `orkige_engine/engine_render/`)

All headers are backend-free (no `Ogre::` names; math comes through the
`RenderMath.h` vocabulary), compile stand-alone (enforced by
`RenderFacadeCheck.cpp`, which is deliberately excluded from the target PCH), and
carry per-method mapping comments for classic OGRE, Ogre-Next and Filament.

| Header | Class | Covers (audit-derived) |
|---|---|---|
| `RenderPrerequisites.h` | — | export macro, facade forward decls, backend/ODR ground rules |
| `RenderMath.h` | — | the math vocabulary + THE swap point (see math decision) |
| `RenderSystem.h` | `RenderSystem` | frame loop, main-window camera/background/resize/size (`showCameraOnWindow` + `getWindowCamera` - the latter added in WP-A1.2 so CameraComponent can take over the window camera while apps still set it up through Engine), screenshots, `FrameStats` (fps/triangles/batches), resource locations (FileSystem/Zip/BigZip), `createRenderTexture`, `getWorld` |
| `RenderWorld.h` | `RenderWorld` | root node, node/content factories, ambient light, `queryRay` AABB picking (`RayQueryHit`: distance/node/userPointer) |
| `RenderNode.h` | `RenderNode` | transform get/set (local + world), translate/yaw/pitch/roll/lookAt/setDirection/fixedYawAxis, child creation/re-parenting/navigation, visibility, world bounds, user-pointer back-mapping |
| `MeshInstance.h` | `MeshInstance` | ModelComponent needs (load/attach/visible/shadows/bounds/query flags), vertex-colour-unlit fixup + sub-mesh introspection (self-checks), AnimationComponent's AnimationState control surface (names/enable/loop/time/length/ended) |
| `SpriteQuad.h` | `SpriteQuad` | SpriteComponent needs: texture + texel size, size, UV rect, tint, flips, zOrder (painter's sorting), visibility |
| `RenderCamera.h` | `RenderCamera` | perspective/ortho (vertical half-extent), FOVy, aspect, near/far clip getters (added in WP-A1.2 so projection switchers preserve the clips), viewport ray + project point, view/projection matrices (gizmo), wireframe toggle |
| `RenderLight.h` | `RenderLight` | deliberate minimum + room (type/colour/range/spot/shadows) — no live dynamic-light call site exists today |
| `RenderTexture.h` | `RenderTexture` | editor RTT panel: camera, background, overlays/shadows toggle, resize-by-recreate, native texture id for ImGui, `writeContentsToFile` |

**Not** in the facade, by design: materials as a system (only the two audited material
*services* exist — sprite material generation inside `SpriteQuad`, unlit fixup on
`MeshInstance`; per CLAUDE.md, keep materials simple/generated), overlays/imgui
(editor-private glue), frame *events* (stay on the core event system around
`renderOneFrame`), log forwarding (engine service, not renderer), Gorilla/fastgui.

### Handle model: `optr` (shared_ptr), not ids — rationale

`PhysicsWorld` uses integer `BodyId`s; the render facade uses `optr<RenderNode>` etc.
Deliberately different because the workloads differ:

- **RAII is the point.** Component `onRemove` today hand-rolls detach/destroy chains
  (NodeUtil's recursive wipes exist only for that). A handle whose destruction
  detaches+destroys removes a whole bug class. Bodies never needed that — the sim owns
  them flatly.
- **Call-site ergonomics.** Nodes are chatty (position/orientation every frame,
  hierarchy walks). Id-based APIs would funnel every call through
  `RenderWorld::setNodePosition(id, ...)` lookups; PhysicsWorld gets away with it
  because its per-body surface is small.
- **House style.** `optr` is the engine-wide pointer vocabulary; sol2 binds
  `std::shared_ptr` usertypes natively, so the A1 Lua migration of the SceneNode
  bindings is direct; `woptr` gives observers (editor selection) safe dangling checks —
  the very problem SceneNodeGuard's "guard" half tried to solve.
- Ids remain right where identity must cross serialization/undo/network — that is the
  GameObject id layer, which already exists above components.

**No virtual dispatch.** Because the backend is fixed at build time, the facade classes
are concrete: each declares `struct Impl; Impl* mImpl;` and exactly one backend's TUs
define the ctors/dtors/methods (`PhysicsWorld`'s proven pimpl pattern, minus vtables).
Swapping backends = compiling a different impl directory, same headers, same ABI-shaped
API. This also keeps the door open for a header-only inline fast path later if a hot
setter ever shows up in profiles.

### Math types — decision

**Recommendation: Option A — alias now, own types before Filament.**
`RenderMath.h` defines the engine vocabulary (`Orkige::Vec3`, `Quat`, `Color`,
`Degree`, `Ray3`, `AABB`, ...) as typedefs of the Ogre math types, and is the single
documented swap point.

Why not own thin types immediately (Option B):

- **Both planned backends share Ogre math.** Ogre-Next kept `Ogre::Vector3/Quaternion/
  ColourValue/Degree` with identical names, layout and semantics. Until Filament
  actually starts, own types buy zero backend freedom while costing everything below.
- **Churn budget.** 1563 math refs sit above engine_graphic — in component public APIs,
  in the sol2 usertype registrations (`module.cpp` exposes Vector3/Vector2/Quaternion
  to every project script — `projects/jumper-lua` does `Vec3(...)` arithmetic), in
  serialization field writes, in ~15 test files. Option B forces that migration *now*
  and duplicates Ogre's math (slerp, quat-from-axes, intersects) with fresh bugs;
  Option A spends the same budget on the behavioral facade instead.
- **Serialization is layout-stable either way.** Archives store named scalar fields
  (x/y/z/w), not blobs — the later type swap does not touch save files.
- **Lua is name-stable either way.** Scripts see usertype names ("Vec3"), not C++
  types; re-registering the same script-facing API against own types later is invisible
  to script code.
- **The swap is mechanical when it comes.** The facade check TU already
  `static_assert`s packed layout (Vec3 = 3 Reals, ...); new code spells `Orkige::Vec3`;
  remaining `Ogre::Vector3` spellings reduce opportunistically per CLAUDE.md's
  containment rule and by A3 the facade-facing surface is alias-only, so swapping the
  header + a sed over stragglers closes it.

Accepted (and flagged, question #4) tradeoff: facade headers transitively include Ogre
*math* headers. They still name zero Ogre types — the boundary is enforced socially +
by review, with the compile check keeping the umbrella (`Ogre.h`/EnginePrerequisites)
out.

---

## Classic → Ogre-Next mapping — the hard areas

| Area | Classic today | Ogre-Next reality | Plan |
|---|---|---|---|
| **Programmatic materials** | `MaterialManager::create` + `Pass::setSceneBlending/ setDepthWriteEnabled/setLightingEnabled/setVertexColourTracking` + RTSS generates shaders (sprite material, VertexColour, Gorilla atlas) | Materials are **HLMS datablocks**: `HlmsUnlit`/`HlmsPbs` + HlmsBlendblock (alpha blend) + HlmsMacroblock (depth check/write, culling) + HlmsSamplerblock. No RTSS, no `Pass`. OGRE *material scripts* effectively dead (already banned by CLAUDE.md) | The facade never exposes materials — `SpriteQuad` and `setVertexColourUnlit` map to a tiny set of generated HlmsUnlit datablocks in the Next backend. Keep the generated-material discipline; anything fancier waits for a real need |
| **Meshes** | v1 `Ogre::Mesh` + `Entity`; assimp codec loads glTF/glb directly | v2 `Mesh`/`Item` (+`Ogre::v1::` legacy namespace). Assimp-loaded/v1 meshes must go through `Mesh::importV1` (or the v1 entity path with worse performance) | `MeshInstance` hides it: Next backend loads v1 via codec then `importV1` → `Item`. Skeletal: v2 `SkeletonInstance` differs — the facade's AnimationState-shaped API is implementable on both; root-motion bone digging stays classic-only (question #1) |
| **RTT** | `TextureManager::createManual(TU_RENDERTARGET)` → `getBuffer()->getRenderTarget()->addViewport(camera)`; per-viewport background/overlay toggles | No viewports-on-targets. `TextureGpuManager::createTexture(RenderToTexture)` + a **compositor workspace** whose pass targets the texture; clear colour and overlay inclusion are compositor-pass properties | `RenderTexture` maps 1:1 onto a one-pass workspace; `setOverlaysEnabled(false)` = omit the overlay pass. Resize-by-recreate matches both. ImGui consumption differs per RS — hence the opaque `getNativeTextureId` |
| **Ray queries** | `SceneManager::createRayQuery` → AABB hits; CollisionTools adds triangle tests via hw-buffer reads | Next still has `RaySceneQuery` (v2 objects) but it is de-emphasized; triangle-accurate picking against render meshes means VAO reads (painful) | `RenderWorld::queryRay` stays AABB-level (that is all the editor uses). Triangle accuracy = `PhysicsWorld::castRay` against collision shapes — physics is the cross-backend truth for precise picking. CollisionTools retires |
| **Frame loop / rendering** | `Root::renderOneFrame` renders viewports implicitly | Nothing renders without an explicit **CompositorManager2 workspace** per target/window | Next backend creates one window workspace in `RenderSystem` setup + one per `RenderTexture`. Facade signature unchanged |
| **HUD (Gorilla/fastgui)** | RenderQueueListener + `manualRender` + hand-built vertex buffers + texel offsets (see audit) — every removed-in-Next API at once | Would need: v2 Renderable + VaoManager buffers + HlmsUnlit + compositor hook. A rewrite, per backend, forever | **Recommend: Gorilla classic-only; future HUD = facade `SpriteQuad` layer** (screen-space ortho camera + zOrder painter sorting — SpriteComponent already proves the primitives). One HUD implementation for every backend incl. Filament, instead of N ports of a dead library. jumper/roller HUDs migrate in A3 |
| **Window/stats plumbing** | `RenderWindow::writeContentsToFile/getStatistics/ windowMovedOrResized`; `Viewport::getActualWidth` | `Ogre::Window` + `TextureGpu` readback; stats via RenderSystem metrics/workspace | All behind `RenderSystem` methods already |
| **Ambient light** | `SceneManager::setAmbientLight(colour)` | `setAmbientLight(upperHemi, lowerHemi, dir)` | Facade takes one colour; Next impl passes it to both hemispheres |
| **Resources/archives** | `ResourceGroupManager`, `Archive`, BigZip subclass | Same subsystem exists (minor drift); HLMS additionally needs its library folders registered | `addResourceLocation` unchanged; Next backend registers HLMS data in setup |

Filament notes are inline per method in the headers; the structural ones: no scene
graph (TransformManager parent links — RenderNode maps cleanly), no material system to
avoid (filamat/matc compiled materials — the generated-materials discipline maps to a
handful of prebuilt .filamat), no resource groups (impl VFS), picking via impl AABB
walk or `View::pick`, and `Renderer::readPixels` for both screenshot paths.

---

## Build flavors

- **`ORKIGE_RENDER_BACKEND=classic|next`** (root CMakeLists cache option, default
  `classic`; `next` currently stops with a pointed FATAL_ERROR until A2). It sets
  exactly one of `ORKIGE_RENDER_CLASSIC` / `ORKIGE_RENDER_NEXT` as a PUBLIC compile
  definition on `orkige_engine`; `RenderFacadeCheck.cpp` hard-errors when none/both are
  set. In A2 it additionally: swaps the vcpkg dependency (`ogre` ↔ `ogre-next` feature
  set via manifest features), swaps `find_package`, and swaps the backend impl
  directory in `ORKIGE_ENGINE_SOURCES`.
- **`ORKIGE_RENDERSYSTEM` (env) keeps its meaning**: the *runtime* graphics-API pick
  (GL3Plus default / Metal / Vulkan / GLES2) among the plugins the chosen backend
  linked. Both Ogre backends have such plugins; the two knobs are orthogonal and must
  never be merged. Documented in `RenderSystem.h`'s naming note.
- **Presets**: keep `macos-debug/-release` = classic (unchanged, no reconfigure churn).
  A2 adds `macos-debug-next` (inherits base, sets `ORKIGE_RENDER_BACKEND=next`,
  `binaryDir` `build/macos-debug-next`) + matching build preset and `unit-next` /
  `desktop-next` test presets. Mobile presets stay classic until Phase-3 evaluation.
- **Directory layout** (recommendation):
  - `engine_render/` — interfaces only (this phase). Never includes backend headers.
  - `engine_render_classic/` — NEW home for facade impls (`RenderNodeClassic.cpp`, ...)
    and a private `ClassicBackend.h` (the one place allowed to hand Ogre pointers to
    classic-only zones like Gorilla glue during migration).
  - `engine_graphic/` — **keeps its name** and becomes de-facto classic-private
    (Engine.cpp already is the classic bootstrapper; renaming it buys nothing and
    breaks every include). Engine's window/config/event plumbing stays; its
    scene-facing accessors (`getSceneManager/getCamera/getViewport`) get deprecated in
    favor of `RenderSystem::get()` and deleted once call sites are migrated.
  - A2 adds `engine_render_next/` mirroring classic.
- **Test matrix**:
  | Suite | classic | next (from A2) |
  |---|---|---|
  | `ctest --preset unit` (headless) | every commit | every commit (`unit-next`; facade-level unit tests are backend-parametrized by construction — they only see engine_render) |
  | `ctest --preset desktop` (integration) | every commit — the 137-green contract | `desktop-next` once demos render; until then a build-only `next-compile` check |
  | `ctest --preset all` (device) | deploy changes | classic-only until Phase 3 decides mobile backend |
  New in A1: a facade smoke integration test (`render_facade_selfcheck`: node
  hierarchy + mesh + sprite + camera + RTT + queryRay + screenshot in one headed run)
  — that test IS the backend conformance suite and must pass identically per backend.

---

## Phase plan A1–A3 (work packages sized for sequential agents)

### A1 — classic backend implements the facade; call sites migrate (behavior-neutral)

- **WP-A1.1 backend skeleton** *(DELIVERED 2026-07-08)*: `engine_render_classic/`
  implementing all 8 classes against OGRE 14 (Impl structs; `RenderSystem::get` wired
  from `Engine::setup`; query-flag defaults; user-pointer bookkeeping). Files: new
  `engine_render_classic/*.cpp` (~8), `orkige_engine/CMakeLists.txt`, small hooks in
  `engine_graphic/Engine.{h,cpp}`. Plus the `render_facade_selfcheck` app + ctest
  registration. Deliverable: facade fully usable next to the old paths.
  Implementation notes: the backend's private door is `struct RenderBackend`
  (`engine_render_classic/ClassicBackend.h`, befriended by the facade classes via
  `RenderPrerequisites.h`); the question-#6 dead files were deleted; the selfcheck
  lives in `tests/render_facade/` (backend-agnostic main + per-backend bootstrap TU)
  and IS the conformance suite every future backend must pass.
- **WP-A1.2 components** *(DELIVERED 2026-07-08)*: `TransformComponent` (drops the SceneNodeGuard base for an
  owned `optr<RenderNode>`; keeps its event surface), `ModelComponent`
  (`MeshInstance`), `SpriteComponent` (`SpriteQuad`; pure helpers stay),
  `AnimationComponent` (facade animation API; root-motion via classic backdoor per
  question #1), `CameraComponent` (`RenderCamera`), `CameraDefaultModes` (drop
  CollisionTools terrain stub). Files: `engine_gocomponent/*.{h,cpp}` (12),
  `engine_util/NodeUtil.h` shrink. Tests: TwoDSupport/component suites keep passing
  untouched where possible.
  Implementation notes:
  - `SceneNodeGuard` was RESHAPED (not yet deleted): it stays the components'
    common base but now owns an `optr<RenderNode>` and forwards only the ~15
    used methods; components keep their `initSceneNodeGuard`/`deinitSceneNodeGuard`
    shape until WP-A1.5 inlines the optr. The Ogre `Node::Listener` is gone;
    `NodeAttached/DetachedEvent` fire from `attachToNode`, `NodeUpdatedEvent`
    is declared but NOT emitted (no facade per-node update callback, zero
    consumers existed) - wire a facade listener if a consumer appears.
  - **Dual-tagging (transition)**: `TransformComponent::onAdd` sets the facade
    user pointer (`RenderNode::setUserPointer(this)`) AND, classic-only, the
    legacy `Ogre::Any` user binding on the backend node, because the editor
    still picks with its own Ogre `RaySceneQuery` until WP-A1.4. The classic
    `getComponentFromNode(Ogre::Node*)` overload and NodeUtil's raw-node
    overload exist solely for that path; WP-A1.4 deletes all three together.
    NodeUtil's `cleanSceneNode`/`wipeSceneNode` destroy chains are gone - the
    facade handles are RAII.
  - Facade API added: `RenderSystem::getWindowCamera` (CameraComponent takes
    over the window camera; on the classic Engine path it wraps Engine's
    default camera non-owning), `RenderCamera::getNearClip/getFarClip`
    (projection switchers preserve clips). Backend-private additions:
    `RenderBackend::wrapCamera` and `RenderBackend::ogreEntity` - the latter is
    the sanctioned door for the AnimationComponent root-motion backdoor, which
    is a documented `#if ORKIGE_RENDER_CLASSIC` block inside the component
    .cpp (the single sanctioned ClassicBackend.h include outside the backend);
    bone names are classic-only too, empty elsewhere.
  - Component API changes: `ModelComponent::getModel()` (Ogre::Entity*) became
    `getMeshInstance()`; `loadModel` lost the caller-less Ogre-specific
    `shareSkeletonInstance` flag; `AnimationComponent::getAnimationStates()`
    (Ogre type, caller-less) was dropped. The handful of app/editor call sites
    (unlit fixup, texture probes, `!= nullptr` checks) were mechanically moved
    onto `setVertexColourUnlit`/`subMeshHasTexture`/`getMeshInstance` -
    NOT their WP-A1.3/A1.4 migrations, just compile-compat.
    `SpriteComponent::createSpriteMaterial` moved into the backend
    (`RenderBackend::getOrCreateSpriteMaterial` was already the same recipe).
  - `CameraComponent` drives `getWindowCamera()` on a facade-node rig; the
    Ogre auto-tracking became an explicit per-update `lookAt(target)` (fixed
    yaw axis keeps it roll-free). `CameraDefaultModes` lost the CollisionTools
    camera-collision loop (physics-based camera collision is the successor
    when a game needs it). NOTE: the camera-rig attach path has no test
    coverage (no live caller); the projection state round-trip is covered.
  - hello_orkige still attaches raw ManualObjects; it reaches the component's
    backend node by its deterministic name ("<id>.TransformComponent") until
    WP-A1.3 migrates demo content onto facade types.
- **WP-A1.3 apps + services**: player, hello_orkige, jumper, jumper-native onto
  `RenderSystem` services (resource locations, ambient, background, screenshots,
  stats, window size) + `SoundManager::setListener` seam + PlayerRuntime/editor log
  forwarding into one engine service. Files: `tools/player/main.cpp`,
  `samples/*/main.cpp`, `projects/jumper-native/native/main.cpp`,
  `engine_sound/SoundManager.{h,cpp}`, `engine_runtime/PlayerRuntime.cpp`.
- **WP-A1.4 editor**: RTT panel → `RenderTexture`; picking → `queryRay` +
  `findUserPointerUpwards`; gizmo → `RenderCamera` matrices/project; stats panel;
  grid via backend service; ImGuiOverlay glue isolated behind an editor-local seam.
  Files: `tools/editor/main.cpp`, `EditorCore.{h,cpp}`. CollisionTools retired
  (removed from build with the question-#6 dead files).
- **WP-A1.5 Lua + cleanup**: `module.cpp` usertypes re-targeted at facade classes
  (script-facing names preserved; `projects/jumper-lua` must run unchanged);
  SceneNodeGuard deleted; Engine scene accessors deprecated; sweep that no file above
  the backend dirs adds new `Ogre::` scene types. Gate: `ctest --preset desktop` green.

### A2 — Ogre-Next backend

- **WP-A2.1 dependency + flavor**: vcpkg manifest feature for `ogre-next` (port
  exists, 3.0.0), `ORKIGE_RENDER_BACKEND=next` wiring (find_package/link/impl-dir
  swap), `macos-debug-next` preset, build-only CI check. Expect overlay-port work
  (the `ports/` + `Docs/ports.md` discipline applies).
- **WP-A2.2 core scene**: `engine_render_next/` RenderSystem/World/Node/Camera/Light
  (workspace bootstrap, v2 scene manager, hemisphere ambient). Gate: facade selfcheck
  renders a cube on Next (needs the WP-A2.3 mesh piece for the cube — coordinate).
- **WP-A2.3 content**: MeshInstance (`importV1` path for assimp meshes, HlmsUnlit
  fixup, v2 skeleton animation surface), SpriteQuad (HlmsUnlit datablock per texture,
  v2 quad geometry, queue-based zOrder), RenderTexture (workspace-per-target,
  readback screenshot), queryRay (v2 ray query or impl AABB walk).
- **WP-A2.4 parity run**: `render_facade_selfcheck` + demo scenes + player scene
  loading green on `desktop-next` minus fastgui-dependent tests (explicit skip list
  documented; HUD arrives in A3).

### A3 — cross-backend HUD + closure

- **WP-A3.1 facade HUD**: screen-space sprite layer on `SpriteQuad` + ortho camera
  (atlas support = UV rects, already in the facade; text via atlas glyph quads reusing
  `Util/make_fastgui_atlas.py` output). fastgui widgets get the draw-surface seam;
  Gorilla remains available classic-side until the seam covers jumper's HUD needs.
- **WP-A3.2 HUD migration**: jumper + roller + player HUD paths onto the facade HUD;
  fastgui atlas tests extended; `desktop-next` skip list emptied.
- **WP-A3.3 closure**: containment lint (grep gate: no `Ogre::` outside
  engine_graphic/engine_render_classic/engine_render_next except `RenderMath.h`),
  editor-backend decision (question #3) revisited with real Next numbers, math-swap
  readiness review (question #4), mobile backend evaluation input for Phase 3.

---

*A0 artifacts: `orkige_engine/engine_render/*.h` + `RenderFacadeCheck.cpp` (compile
check, PCH-exempt), `ORKIGE_RENDER_BACKEND` CMake option, this document. No runtime
behavior changed; `ctest --preset desktop` stays the gate.*
