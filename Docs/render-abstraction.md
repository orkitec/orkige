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
| `engine_util/SceneNodeGuard` | **superseded by `RenderNode`; kept as node-owner base** | The facade carries only the used ~40% of its mirror. WP-A1.2 reshaped the guard into the components' facade-node-owner base (holds `optr<RenderNode>`, forwards ~15 used methods). WP-A1.5 kept it (recorded deviation): it is Ogre-free and shared by three components — deleting it would just triple the forwarding surface. |
| `engine_util/MeshUtil` | **backend-private** | Raw vertex/index buffer extraction; only caller is CollisionTools' triangle raycast (being superseded by PhysicsWorld) + unbuilt sceneoptimizer. Moves behind the classic seam; Ogre-Next equivalent only if triangle-accurate *render-mesh* picking is ever needed there. |
| `engine_util/PrimitiveUtil` | **split** | "EditorCube" mesh + vertex-colour-unlit fixup are wanted on every backend → facade (`MeshInstance::setVertexColourUnlit`; cube-mesh factory becomes a backend service in A1 — DONE in WP-A1.3: `RenderWorld::createVertexColourCubeMesh`, classic impl calls PrimitiveUtil). ManualObject guts stay backend-private; direct PrimitiveUtil callers left are the classic backend + the classic-only editor (WP-A1.4 decides its final home). |
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
| `RenderWorld.h` | `RenderWorld` | root node, node/content factories, ambient light, `queryRay` AABB picking (`RayQueryHit`: distance/node/userPointer), `createVertexColourCubeMesh` (the WP-A1.3 backend cube-mesh service - the editor's "Create Cube" resource every scene-loading app needs; classic impl reuses PrimitiveUtil) |
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
- **WP-A1.3 apps + services** *(DELIVERED 2026-07-08)*: player, hello_orkige, jumper, jumper-native onto
  `RenderSystem` services (resource locations, ambient, background, screenshots,
  stats, window size) + `SoundManager::setListener` seam + PlayerRuntime/editor log
  forwarding into one engine service. Files: `tools/player/main.cpp`,
  `samples/*/main.cpp`, `projects/jumper-native/native/main.cpp`,
  `engine_sound/SoundManager.{h,cpp}`, `engine_runtime/PlayerRuntime.cpp`.
  Implementation notes:
  - **The sanctioned classic boot block**: every app keeps a marked raw-Ogre
    corner covering exactly (a) the `Engine` constructor/window params
    (`Ogre::SMT_DEFAULT`), (b) the RTSS-internal media registration into
    `RGN_INTERNAL` (must precede `Engine::setup`; same rule as
    `tests/render_facade/bootstrap_classic.cpp`), (c) `ORKIGE_RENDERSYSTEM`
    handling via `Engine::setPreferredRenderSystem` (classic-internal runtime
    graphics-API pick, see "Build flavors"). Everything after `Engine::setup`
    is facade-only.
  - **Window camera**: hello_orkige, jumper and jumper-native replaced
    `createDefaultCameraAndViewport` with a facade rig (`createCamera` +
    `createNode` + `setFixedYawAxis` + `showCameraOnWindow`). tools/player
    KEEPS the Engine-path camera (documented residue): the project scripts
    drive it through the Lua bindings (`engine:getCamera(0)`,
    `setCameraOrthographic`, `getViewport`) which WP-A1.5 re-targets - only
    then can the player switch rigs. `Engine::getViewport` grew a classic
    migration bridge: it falls back to the window's viewport 0 when apps boot
    the camera through the facade, so fastgui (classic-only, reads
    `Engine::getViewport`) works on both boot paths until WP-A1.5.
  - **hello_orkige content**: the raw ManualObject cubes became instances of
    the facade cube-mesh service (`createVertexColourCubeMesh("HelloCube.mesh",
    1.0)` + `createMeshInstance`), physics-box visuals are per-axis-scaled
    child nodes; the "reach the component's backend node by name" transition
    hack from WP-A1.2 is gone (visuals attach via
    `TransformComponent::createChildNode`). Cosmetic delta: the orbiter cube
    reuses the shared vertex-colour order (the reversed palette is gone).
    Triangle self-checks read `RenderSystem::getFrameStats`.
  - **Log service**: `engine_base/EngineLog.h` (`EngineLogCapture`) is the
    engine-service home for log capture/forwarding - pimpl over the logging
    backend (OGRE LogManager, also present in Next), bounded backlog,
    `drain()` per frame, static `logMessage`. `PlayerDebugLink` uses it; the
    editor's duplicated console listener migrates in WP-A1.4.
  - **SoundManager seam**: the listener is now an `optr<RenderNode>` (ctor +
    `setListener`); forward/up derive from the node's world orientation
    (-Z/+Y). No live caller existed - the seam is for the camera-node rigs.
  - `engine_util/FrameStatsUtil` needed NO change: it is a frame-TIME
    (wall-clock dt) collector with zero renderer coupling; the renderer
    stats consumers (triangle counts) moved to `RenderSystem::getFrameStats`.
  - JumperHud (fastgui, classic-only per decision #2) kept exactly one Ogre
    spelling: the resource-group default parameter it forwards to
    FastGuiManager; its math went to the alias vocabulary.
- **WP-A1.4 editor** *(DELIVERED 2026-07-08)*: RTT panel → `RenderTexture`; picking → `queryRay` +
  `findUserPointerUpwards`; gizmo → `RenderCamera` matrices/project; stats panel;
  grid via backend service; ImGuiOverlay glue isolated behind an editor-local seam.
  Files: `tools/editor/main.cpp`, `EditorCore.{h,cpp}`. CollisionTools retired
  (removed from build with the question-#6 dead files).
  Implementation notes:
  - **Dual-tagging DELETED** as planned: `TransformComponent::onAdd` no longer
    sets the legacy `Ogre::Any` user binding (facade `setUserPointer` only),
    and the classic-only `Ogre::Node*` overloads of
    `TransformComponent::getComponentFromNode` / `NodeUtil::getGameObjectFromNode`
    (+ `USEROBJECT_BINDING_KEY`) are gone. Editor picking resolves
    `RayQueryHit::userPointer` straight to the tagging TransformComponent.
  - **CollisionTools deleted** (files + build entry; recoverable from git).
    `engine_util/MeshUtil.h` stays: its remaining reference is the unbuilt
    sceneoptimizer.
  - **Facade API added**: `RenderSystem::removeResourceLocation` (idempotent),
    `resourceGroupExists`, `destroyResourceGroup`, `resourceExists` - the
    editor's project switch/import needed the teardown half of the resource
    surface (the player only ever registers). `RenderMath.h` grew the
    `Orkige::Affine3` alias (gizmo TRS decomposition).
  - **Camera rig**: the scene camera is a facade `createCamera` +
    `createNode` rig; orbit/fly/pan write facade node transforms. ImGuizmo
    still receives the row-major->column-major transpose (`matrixToImGuizmo`)
    - facade matrices are byte-identical to the raw camera's, verified by the
    selfcheck pick/gizmo frames. `Ogre::Math::ASin/ATan2/Cos/Sin` spellings
    became `std::` on alias radians. Editor math is alias-vocabulary
    throughout (EditorCore/EditorCamera headers include `RenderMath.h`;
    `Ogre::Exception` catches became `std::exception`).
  - **Console**: the editor's own `Ogre::LogListener` twin is deleted;
    `EngineLogCapture` (sized to the Console's 5000-line cap - the OGRE boot
    overflows the 200-line default) is drained into the Console once per
    frame.
  - **Selfcheck deltas** (behavior-neutral probes): the frame-30
    "test_mesh.glb became a resource" check now asserts the loaded facade
    `MeshInstance` has sub-meshes (was `MeshManager::getByName`); the
    frame-90 round-trip counts facade-graph root children
    (`RenderWorld::getRootNode()->numChildren()`) instead of raw
    `SceneManager` root children.
  - **Sanctioned classic blocks left in the editor** (each marked in
    main.cpp): (a) the app-standard classic boot block (Engine ctor,
    ORKIGE_RENDERSYSTEM, RTSS internal media); (b) the
    OverlaySystem/ImGuiOverlay wiring incl. the font-texture SetTexID
    bridge; (c) the UI-only window viewport (visibility mask 0 + RTSS
    scheme - per-viewport masks are deliberately not facade API);
    (d) `createEditorGrid`'s ManualObject line list. Residual `Ogre::` in
    tools/editor: 33 spellings in main.cpp (all in those blocks or
    comments), plus 5 comment-only mentions across
    ImGuiSDL3Input/EditorTheme; down from the audit's 211.
- **WP-A1.5 Lua + containment lock** *(DELIVERED 2026-07-08 — closes A1)*:
  `module.cpp` usertypes re-targeted at facade classes; player onto the facade
  camera rig; the containment rule became a mechanical gate (pulled forward
  from WP-A3.3). Gate: `ctest --preset desktop` green (139 tests incl. the lint).
  Implementation notes:
  - **Lua surface**: the classic usertypes (`SceneNode`/`SceneManager`/
    `Viewport`/`Camera`) are GONE. Facade usertypes: `RenderNode` (position/
    orientation/scale + world getters, translate/lookAt/setDirection/
    setFixedYawAxis/setVisible, createChild/getParent/numChildren,
    `TransformSpace` enum), `RenderCamera` (`getNode` — NEW facade API,
    the rig-node accessor scripts place the camera with — setOrthographic,
    projection type + clips, setAspectRatio, setWireframe), `RenderSystem`
    (getWindowCamera/getWorld/saveWindowContents), `RenderWorld`
    (getRootNode/createNode). `Radian`/`Degree`-taking methods (yaw/pitch/
    roll, setPerspective/setFOVy) stay unregistered until an angle usertype
    exists — scripts rotate via setOrientation/lookAt.
  - **Engine stays the app/Lua singleton**, its scene surface re-pointed:
    Lua `getCamera()` = the facade window camera (C++
    `Engine::getWindowCamera`), `getWindowWidth/Height` replace
    `getViewport(0):getActualWidth/Height`, `setCameraOrthographic(size)` /
    `setCameraPerspective()` / `setWindowBackgroundColour(r,g,b)` route
    through the facade preserving clips/FOV. `getSceneManager`/`getCamera(n)`/
    `getViewport`/`createDefaultCameraAndViewport` left the Lua surface and
    are deprecated in C++ (doc comments name the remaining classic-only
    consumers). Script spellings updated in projects/jumper-lua +
    projects/roller (`RenderNode.TransformSpace`,
    `engine:getCamera():getNode()`, window-size calls) — behavior identical,
    proven by the untouched selfcheck expectations; hello_orkige's inline
    Lua smoke test now walks the facade types.
  - **Player camera**: tools/player builds the standard facade rig
    (createCamera + createNode + setFixedYawAxis + showCameraOnWindow); the
    WP-A1.3 residue (Engine-path camera + roll probe) is gone.
    `Engine::getViewport`'s bridge STAYS with one consumer: fastgui
    (classic-only, decision #2) — it goes with the A3 draw-surface seam.
  - **Late-handle guard**: script-held facade handles legally outlive the
    render system now (Lua userdata lives until the Lua state closes, after
    ~Engine) — the classic RenderNode/RenderCamera dtors detect the dead
    backend (`RenderBackend::system() == NULL`) and free facade memory only.
  - **SceneNodeGuard NOT deleted — recorded plan deviation**: the WP-A1.2
    reshape left it Ogre-free and it is the shared facade-node-owner base of
    exactly three components (Transform/Model/Sprite); inlining the optr
    would triple ~15 forwarding methods for zero containment gain. It stays
    (in engine_util) until a component needs a different node model.
  - **Containment lint** (pulled forward from WP-A3.3):
    `Util/check_ogre_containment.py` + `Util/ogre_containment.json`, wired
    as ctest `render_containment_lint` (LABELS unit → unit AND desktop
    presets). Comment-stripped scan; allowed = engine_graphic/,
    engine_render_classic/ (+ engine_render_next/ in A2), RenderMath.h;
    everything else needs a config entry (whole dir/file with reason — the
    audit's residual lists are the baseline) or an explicitly marked
    `ORKIGE_SANCTIONED_OGRE_BEGIN(<tag>)`/`_END` block (the app classic boot
    blocks, the editor glue, the AnimationComponent root-motion backdoor).
    Stale sanctions fail the lint too, so the list only shrinks. Current
    violation count: **0**. Bonus de-leak: ScriptComponent's error logging
    moved onto `EngineLogCapture::logError` (the engine log service grew the
    stderr-fallback error path).

### A phase status: A1 COMPLETE (2026-07-08)

Everything above `engine_graphic`/`engine_render_classic` talks to the
renderer exclusively through `engine_render` — components, apps, tools,
editor AND the Lua script surface — with the residue pinned down in
`Util/ogre_containment.json` and enforced by `render_containment_lint`.

**What a new backend must implement** (the A2 `engine_render_next/` work
order): the 8 facade classes (`RenderSystem`, `RenderWorld`, `RenderNode`,
`MeshInstance`, `SpriteQuad`, `RenderCamera`, `RenderLight`,
`RenderTexture`) against the per-method mapping comments in the headers,
plus a bootstrap TU for `tests/render_facade`. The conformance contract IS
the test suite, unchanged:

- `render_facade_selfcheck` — window, node hierarchy, mesh + unlit fixup,
  sprite, perspective/ortho cameras + matrices, light, RTT, queryRay,
  stats, screenshots;
- `player_jumper_lua_selfcheck` / `player_roller_selfcheck` — the full
  script-visible surface (facade usertypes, window camera rig, 2D tier);
- the demo/editor/player integration runs minus the documented
  fastgui-dependent skips (HUD arrives in A3).

### A2 — Ogre-Next backend

- **WP-A2.1 dependency + flavor** *(DELIVERED 2026-07-08 — aka phase B0/B1)*:
  vcpkg manifest feature for `ogre-next` (locally authored overlay port,
  3.0.0 - see Docs/ports.md), `ORKIGE_RENDER_BACKEND=next` wiring
  (find_package/link/impl-dir swap), `macos-debug-next` preset,
  `desktop-next` test preset. DELIVERED BEYOND build-only: the flavor
  configures + builds + `engine_render_next/` is a SKELETON with a REAL boot
  (`RenderBackend::createRenderSystem(NextBootOptions)` - on Next the
  RenderSystem facade IS the boot, there is no Engine; Root + Metal RS +
  SDL-hosted window via externalWindowHandle + CompositorManager2 clear
  workspace + window screenshots via the manual-swap-release recipe +
  resource locations + full RenderNode/RenderCamera). Content classes
  (MeshInstance/SpriteQuad/RenderLight/RenderTexture/queryRay/cube-mesh)
  are honest stubs: safe defaults + one "not implemented on the next
  backend yet" log per feature. Tests: `render_next_smoke` (boot, clear to
  a colour, non-black facade screenshot, exit 0) passes on `desktop-next`;
  `render_facade_selfcheck` builds on the flavor (bootstrap_next.cpp) but
  is registered DISABLED until WP-A2.2/A2.3 fill the stubs. Flavor gates:
  hello_orkige/jumper/editor/player stay classic-only (they boot through
  the classic Engine; B2 ports the B-phase apps). Deviations recorded:
  `RenderMath.h` grew a per-backend `Affine3` alias (Ogre-Next has no
  Affine3; Matrix4 carries the affine helpers - only the classic-only
  editor gizmo consumes it) and `RenderCamera::setWireframe` is a stub on
  Next (the v2 Camera lost the polygon-mode toggle; B2 revisits).
- **WP-A2.2 core scene + WP-A2.3 content** *(DELIVERED together 2026-07-08 — aka
  phase B2)*: `engine_render_next/` implements the WHOLE facade;
  **`render_facade_selfcheck` passes UNCHANGED on `desktop-next` (enabled in ctest,
  zero carve-outs)**; classic `desktop` stays 139-green. Per-class notes (the
  reference for future material/mesh work on Next):
  - **Mesh path (the B2 decision)**: Ogre-Next has no assimp codec, so the backend
    links assimp PRIVATE (`MeshLoaderNext.cpp` is the only TU seeing it; the lib was
    already in the tree via classic ogre's `assimp` feature) and owns the import:
    `*.mesh` → `v1::MeshManager` (serializer); glb/gltf/obj/… → assimp
    `ReadFileFromMemory` from the resource stream (`Triangulate | GenSmoothNormals |
    PreTransformVertices | SortByPType`) → throwaway `v1::ManualObject`
    (`setReadable`, placeholder "BaseWhite" material — v1 MO refuses unknown
    MATERIAL names) → `convertToMesh` → both roads end in
    `MeshManager::createByImportingV1`. TWO Next gotchas learned hard:
    `createByImportingV1` is DEFERRED (records name/group, imports on `load()` —
    call it, and KEEP the v1 intermediate alive: it is the reload source after
    device-lost), and real datablock names are written onto the v2 sub-meshes AFTER
    import (`SubMesh::setMaterialName`). `PreTransformVertices` bakes node
    transforms: **static meshes only** — skeletal glb import is an honest
    `notImplementedOnce` gap (the facade animation surface itself is implemented
    over v2 `SkeletonInstance`/`SkeletonAnimation`, exercised only for
    unknown-name safety until animated content exists).
  - **HLMS mapping (the backend's whole material surface, all generated +
    registered for the wireframe toggle)**: mesh sub-mesh → PBS datablock
    `"<mesh>/mat<i>"` (diffuse colour via `setDiffuse`, diffuse texture via
    `setTexture(PBSM_DIFFUSE)`; glb-embedded textures decode from the blob through
    `Image2` + manual `TextureGpu` upload, external names resolve through the
    resource groups); sprite → shared per-texture Unlit datablock `"Sprite/<tex>"`
    (blendblock `SBT_TRANSPARENT_ALPHA`, macroblock depth-write off + `CULL_NONE`;
    tint/flips stay vertex data, so sprites of one texture share it — same rules as
    classic); `setVertexColourUnlit` → swaps each sub-item onto Unlit
    `"<mesh>/VCUnlit<i>"` keeping a diffuse texture — vertex colours need NO
    datablock knob on Next: `hlms_colour` activates from `VES_DIFFUSE` in the
    vertex format; cube-mesh service → the same recipe with the shared
    `"VertexColour"` Unlit datablock (classic palette/winding kept). Visual
    parity vs classic: same content/hues at every probe point, but Next renders
    into an sRGB swapchain, so output is brighter than classic's non-sRGB path —
    colour management is a WP-A2.4/content-phase refinement, not a B2 blocker.
  - **SpriteQuad**: v2 `ManualObject` quad (has `colour()` — tint/UV/flip rebuilds
    identical to classic); zOrder → render queue `50+z`, inside Next's default-FAST
    v2 queues 0..99, so no queue-mode surgery needed.
  - **RenderTexture**: `TextureGpu(RenderToTexture)` + one basic workspace per
    target incarnation (background bakes into the definition; setCamera/resize/
    background = recreate); `writeContentsToFile` is a plain
    `Image2::convertFromTexture` readback (only the WINDOW needs the Metal
    manual-swap dance); `getNativeTextureId` = the `TextureGpu*` (documented
    opaque id; editor is classic-only per decision #3). Overlays/shadows toggles
    are facade caches — no overlay component compiles on this flavor and the basic
    workspace has no shadow node, so "off" holds structurally.
  - **queryRay**: v2 still ships `DefaultRaySceneQuery` (SIMD AABB over the entity
    memory managers; lights/cameras live elsewhere) — same
    createRayQuery/sort/execute shape as classic, mask semantics intact.
  - **RenderLight**: v2 `Ogre::Light` on a facade node, same range→attenuation
    approximation as classic.
  - **Node/world-bounds**: `getWorldBounds` merges `getWorldAabbUpdated` of every
    attached object in the backend subtree (v2 has no per-node world AABB).
    v2 relative node ops (translate/yaw/pitch/roll/lookAt/setDirection) read
    derived transforms IMMEDIATELY and hard-assert on dirty caches in debug —
    the facade forces `_getDerivedPositionUpdated()` first (facade contract:
    node ops valid at any time).
  - **Stats**: `RenderingMetrics` with recording enabled at boot; the Metal RS
    never passes `_beginFrameOnce`'s reset, so the backend calls `_resetMetrics()`
    per `renderOneFrame` (getFrameStats = last frame, classic semantics); facade
    batches = `mBatchCount + mDrawCount` (v2 draws count into the latter).
  - **Recorded deviation — `setWireframe` is GLOBAL on Next** (question #5
    refined): the v2 camera lost the polygon-mode toggle; the backend flips the
    macroblock polygon mode of every generated datablock instead. Fine for the
    debug-view call sites; noted in RenderCamera.h.
  - Honest gaps (each `notImplementedOnce`): `LT_ZIP`/`LT_BIGZIP` resource
    locations (zziplib port feature + engine_filesystem port pending), skeletal
    glb import (above).
- **WP-A2.4 parity run** *(DELIVERED 2026-07-08 — aka phase B3, closes phase B)*:
  real games run on the Next flavor. What landed:
  - **EnginePrerequisites de-classicified**: `engine_module/EnginePrerequisites.h`
    is backend-NEUTRAL (core prerequisites + Meta + the `RenderMath.h` alias
    vocabulary — no `<Ogre.h>`). Classic-only TUs include the new
    `engine_module/EnginePrerequisitesClassic.h` (neutral umbrella + the classic
    OGRE/Overlay headers; hard `#error` on the next flavor): engine_graphic's
    classic files, fastgui, BigZip, Localisation, PrimitiveUtil/MeshUtil,
    ClassicBackend.h, the unbuilt legacy tools. Neutral-side fallout fixed with
    explicit includes (StringConverter → Ogre string headers, LoadWavData →
    DataStream/ResourceGroupManager, StringUtil's `convertToUTF` classic-gated —
    Next has no DisplayString; `SpriteComponent::renderQueueForZOrder` became
    backend-free (literal 50); `InputManager::initialise` reads
    `RenderSystem::getWindowSize` instead of the classic RenderWindow metrics).
  - **Engine exists on BOTH flavors**: `engine_graphic/Engine.h` dispatches per
    flavor. Classic keeps the bootstrapper unchanged; the next flavor compiles
    the facade-only sibling `engine_graphic/EngineNext.{h,cpp}` — same
    OOBJECT name, same Lua/app surface (getCamera/getRenderSystem/window size/
    projection switches/background), setup() wraps
    `RenderBackend::createRenderSystem` (Hlms media dir = compile-time dev
    default `ORKIGE_NEXT_HLMS_MEDIA_DIR`, `setHlmsMediaDir` for bundles later),
    and the frame events (FrameStarted/RenderingQueued/Ended) fire from an
    Ogre-Next FrameListener bridge so InputManager's tilt simulation and game
    code stay flavor-blind. EngineNext.cpp is the flavor's ONE sanctioned
    NextBackend.h consumer above the backend.
  - **UI capability probe (the HUD decision, implemented)**:
    `Engine::hasUISystem()` — true on classic, false on next — registered to
    Lua on both flavors. `projects/jumper-lua` + `projects/roller` `game.lua`
    probe it and skip their fastgui HUD honestly (state machines, tile slides,
    win flow run identically; jumper's title/win advance on ENTER). module.cpp's
    fastgui/IngameConsole exports are `#ifdef ORKIGE_RENDER_CLASSIC` — an
    unguarded FastGui call on next fails with an honest Lua nil error. The
    player selfchecks compile their UI assertions per flavor
    (`uiChecksEnabled`); gameplay assertions are identical. Drive-by fix:
    game.lua's ENTER edge detection samples every frame now (a held ENTER
    entering "playing" used to read as still-down at the win screen).
  - **Flavor gates opened**: orkige_engine builds ONE shared backend-neutral
    module list (components incl. ScriptComponent, input, sound incl. .caf via
    ResourceUtil::findPath, physics, runtime, EngineLog, module.cpp) plus
    per-flavor additions; `tools/player` + `samples/hello_orkige` build on next
    (per-flavor `#if` inside the sanctioned boot blocks — classic keeps
    Engine-ctor/RTSS-media/ORKIGE_RENDERSYSTEM, next constructs
    `Engine(logFile)`); the `macos-debug-next` preset builds tools; editor +
    samples/jumper stay classic-gated.
  - **Tests**: `desktop-next` runs the core unit suite + lint +
    `render_facade_selfcheck` + `render_next_smoke` + all five hello demo runs
    (mesh/physics/sound/synth-esc incl. the Lua smoke test) + player runs
    (example scene/project, debug protocol) + **`player_roller_selfcheck_next`
    and `player_jumper_lua_selfcheck_next`** (the flavor-suffixed game
    selfchecks). Classic `ctest --preset desktop` stays fully green (139).
    Export tests + Vulkan runs are classic-gated (the runtime graphics-API pick
    and the exporter's media bundling are classic-backend concerns).

### B phase status: COMPLETE (2026-07-08)

B0 (dependency/flavor) + B1 (boot skeleton) + B2 (full facade conformance)
+ B3 (games run) are delivered. **Flavor capability matrix**:

| Capability | classic (`macos-debug`) | next (`macos-debug-next`) |
|---|---|---|
| engine_render facade (conformance suite) | yes | yes (zero carve-outs) |
| components/game objects/serialization | yes | yes |
| Lua scripting (sol2 module surface) | yes | yes (minus fastgui usertypes) |
| input (SDL3, tilt sim), sound (OpenAL, .caf/.wav), physics (Jolt) | yes | yes |
| player + hello_orkige + games (jumper-lua, roller) | yes | yes (HUD-less: `engine:hasUISystem()` = false) |
| fastgui/Gorilla HUD, IngameConsole | yes | no — classic-only until the A3 facade HUD (decision #2) |
| editor | yes | no — classic by decision #3 |
| jumper sample (C++ fastgui HUD) | yes | no (follows the A3 HUD migration) |
| BigZip / LT_ZIP resource locations | yes | honest `notImplementedOnce` stub |
| export pipeline, Vulkan/GL runtime RS pick | yes | no (classic-backend concerns; next boots Metal) |
| root-motion animation backdoor | yes | no (decision #1) |

Remaining known gaps on next (unchanged from B2, all logged once at runtime):
LT_ZIP/LT_BIGZIP locations, skeletal glb import, sRGB-swapchain colour
difference vs classic (content-phase refinement).

### A3 — cross-backend HUD + closure

- **WP-A3.1 facade HUD**: screen-space sprite layer on `SpriteQuad` + ortho camera
  (atlas support = UV rects, already in the facade; text via atlas glyph quads reusing
  `Util/make_fastgui_atlas.py` output). fastgui widgets get the draw-surface seam;
  Gorilla remains available classic-side until the seam covers jumper's HUD needs.
- **WP-A3.2 HUD migration**: jumper + roller + player HUD paths onto the facade HUD;
  fastgui atlas tests extended; `desktop-next` skip list emptied.
- **WP-A3.3 closure**: containment lint DELIVERED EARLY (WP-A1.5:
  `render_containment_lint` + `Util/ogre_containment.json`) — the A3 job
  shrinks its sanction list as fastgui/HUD migrate; editor-backend decision
  (question #3) revisited with real Next numbers, math-swap readiness review
  (question #4), mobile backend evaluation input for Phase 3.

---

*A0 artifacts: `orkige_engine/engine_render/*.h` + `RenderFacadeCheck.cpp` (compile
check, PCH-exempt), `ORKIGE_RENDER_BACKEND` CMake option, this document. No runtime
behavior changed; `ctest --preset desktop` stays the gate.*
