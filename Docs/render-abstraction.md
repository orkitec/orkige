# Render abstraction (`engine_render`) — audit and design

Render-backend abstraction design. Directive: dual backend **classic OGRE 14**
(what ships today) + **Ogre-Next**, extensible to **Filament** as backend #3 later.
Backend selection is **build-time only**: classic OGRE and Ogre-Next define the same
`Ogre::` symbols, so linking both into one binary violates the ODR — there is
deliberately no runtime switch (`ORKIGE_RENDER_BACKEND`, see "Build flavors").

The facade **interface headers** in `orkige_engine/engine_render/` are backend-free
(compiling, no `Ogre::` names), and this document records the design behind them.

---

## Design decisions

1. **AnimationComponent root-motion extraction** digs into `Ogre::Bone` /
   `NodeAnimationTrack` / `TransformKeyFrame` (backing up and restoring keyframes of the
   driving bone). That is far below scene-graph level and has no 1:1 Ogre-Next/Filament
   shape. Proposal: keep it a **classic-only backdoor** inside the classic backend and
   add a facade `getBoneWorldTransform(name)`-style API only when a game actually needs
   root motion on another backend.
   **Decision: root motion stays a classic-only backdoor; no facade bone API until a real cross-backend need.**
2. **HUD strategy** (detail in the mapping table): recommendation is **Gorilla stays
   classic-only**; the future cross-backend HUD is built on facade `SpriteQuad` +
   screen-space camera. That means gui HUDs (jumper, roller) do not run on the
   Ogre-Next backend until the facade HUD exists.
   **Decision: Gorilla/gui HUDs are classic-only until the facade HUD lands.**
   *(Later superseded: gui itself became the cross-backend HUD on
   `DrawLayer2D`; Gorilla was deleted — see the cross-backend HUD section.)*
3. **Which backend runs the editor?** ImGuiOverlay, RTSS probes and the OverlaySystem
   wiring are classic glue. Cheapest path: the editor stays a classic-backend app
   indefinitely; games choose their backend; RenderTexture/picking still go through the
   facade so an eventual editor-on-Next is unblocked but unscheduled.
   **Decision: the editor stays a classic-backend app; games pick their backend.**
4. **Math alias tradeoff**: with "Orkige math = Ogre math" the facade headers
   transitively include Ogre *math* headers (only math — no scene/render types). Zero
   churn for classic+Next; must be swapped to engine-owned types before Filament.
   The deferred cost is accepted — see "Math types".
   **Decision: alias now (`RenderMath.h` is the swap point), engine-owned types before any Filament work starts.**
5. **`RenderCamera::setWireframe`** (Engine wireframe debug mode) has no Filament
   equivalent (Filament has no polygon-mode toggle).
   **Decision: documented no-op on Filament; classic/Next keep the polygon-mode toggle.**
6. **Dead legacy renderables**: `ColoredBoundingBox`, `LightMap`, `CameraUtil.h`,
   `OverlayUtil.h`, `SerializationUtil.*` have **zero callers** (MovableText's only
   caller is the unbuilt sceneoptimizer). Drop them from the build
   instead of dragging them through the abstraction (recoverable from git).
   **Decision: unbuilt AND deleted (incl. MovableText; recoverable from git).**
7. **Multi-window**: Engine carries an 8-window array; every call site uses window 0.
   The facade models exactly one main window. OK to freeze that until a real
   multi-window need appears?
   **Decision: single main window frozen; the facade models exactly one until a real need appears.**

---

## The native-fast-path rule

**A facade contract must be expressible as each backend's NATIVE fast path.
Anything a backend must emulate goes behind a capability flag, chosen
consciously and documented — neither backend may be silently pessimized to
serve the other.**

The motivating example: the facade originally created every node and Item
`SCENE_DYNAMIC`, so Ogre-Next's static memory managers — the machinery that
skips per-frame transform derivation and cull prep for immobile objects —
sat entirely unused until the mobility flag exposed the intent
(`RenderNode::setStatic`; classic maps the same declaration onto
StaticGeometry regions — `Docs/performance.md`). When designing a facade
surface, ask per backend: *what is the cheapest native way to render this
intent?* — and shape the contract so each backend can take it. The
per-scene structural budget gate (`benchmark_budget`, per flavor) is the
standing mechanical guard: a facade change that silently costs draw calls
on either backend fails it.

---

## Audit

Counted (`grep -rEo '\bOgre::'`, `.h/.cpp/.mm`): **2882 references in
99 files** above `engine_graphic` (engine modules w/o engine_graphic + tools + samples
+ tests + projects), plus 619 in `engine_graphic` itself (which *is* the classic
backend and is allowed to be Ogre).

| Area | refs | files | character |
|---|---|---|---|
| engine_gui (incl. Gorilla) | 1177 | 33 | Gorilla = deep render coupling; widgets = math + Gorilla calls |
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
| `engine_gui/Gorilla.{h,cpp}` | **backend-private (classic-only)** | Bundled fork; manual `RenderOperation` + `HardwareBuffer::lock` + `RenderQueueListener::renderQueueEnded` → `manualRender`, programmatic `Pass` materials, `SimpleRenderable`, texel offsets — every hard Ogre-Next break at once. Porting it = rewriting it; the cross-backend HUD is the facade-sprite HUD instead. |
| `engine_gui/Gui*` widgets/view | **keep, de-Ogre opportunistically** | Math + Gorilla calls only; survive as-is wherever Gorilla runs. When the facade HUD lands, widgets get a draw-surface seam instead of `Gorilla::Layer*`. |
| `engine_gui/GuiManager` | **classic-only edges isolated** | `RenderTargetListener` + Material/TextureManager cleanup + `getStatistics` are thin; they move behind the backend seam with Gorilla. |
| `engine_gui/GuiFactory` | **de-Ogre in passing** | Subclass `Ogre::ConfigFile` for INI parsing only — replace with a small engine parser (or keep classic-only; nothing renders here). |
| `tools/editor` ImGuiOverlay/OverlaySystem glue | **backend-private editor glue** | `Ogre::ImGuiOverlay` is a classic-OGRE component; Ogre-Next has its own imgui integration pattern, Filament its own renderer backend for imgui. Isolate behind a small `EditorImGuiBackend` seam *inside the editor*; do not put imgui into engine_render. |
| `engine_graphic/IngameConsole` | **classic-only, candidate to unbuild** | `Rectangle2D` + Overlay; live users are only `module.cpp` (Lua export) and an InputManager toggle. Keep classic-only; revisit when a cross-backend console is wanted (could be rebuilt on gui/facade sprites). |
| `engine_graphic/MovableText`, `DynamicLines`, `DynamicRenderable` | **classic-only; MovableText candidate to unbuild** | Ogre `SimpleRenderable`/`MovableObject` subclasses = per-backend by nature. DynamicLines' only user is the unused ColoredBoundingBox; MovableText's only user is the unbuilt sceneoptimizer. |
| `engine_graphic/ColoredBoundingBox`, `LightMap` | **unbuild (zero callers)** | Zero callers. |
| `engine_util/SceneNodeGuard` | **superseded by `RenderNode`; kept as node-owner base** | The facade carries only the used ~40% of its mirror. The guard was reshaped into the components' facade-node-owner base (holds `optr<RenderNode>`, forwards ~15 used methods). It was kept (recorded deviation): it is Ogre-free and shared by three components — deleting it would just triple the forwarding surface. |
| `engine_util/MeshUtil` | **backend-private** | Raw vertex/index buffer extraction; only caller is CollisionTools' triangle raycast (being superseded by PhysicsWorld) + unbuilt sceneoptimizer. Moves behind the classic seam; Ogre-Next equivalent only if triangle-accurate *render-mesh* picking is ever needed there. |
| `engine_util/PrimitiveUtil` | **split** | "EditorCube" mesh + vertex-colour-unlit fixup are wanted on every backend → facade (`MeshInstance::setVertexColourUnlit`; the cube-mesh factory became a backend service: `RenderWorld::createVertexColourCubeMesh`, classic impl calls PrimitiveUtil). ManualObject guts stay backend-private; direct PrimitiveUtil callers left are the classic backend + the classic-only editor. |
| `engine_util/StringConverter` | **keep (math-adjacent)** | Converts math types + scalars; follows whatever the math decision says (aliases keep it working on both Ogre backends; own-types rewrite it). |
| `engine_util/CameraUtil.h`, `OverlayUtil.h`, `SerializationUtil.*` | **unbuild (zero callers)** | Zero callers; SerializationUtil's Light/Entity round-trip is superseded by component save/load. |
| `engine_util/NodeUtil` | **absorbed by facade RAII** | Its recursive destroy dance exists because raw SceneNodes have no ownership; `RenderNode`/`MeshInstance` handles are RAII. `getGameObjectFromNode` → `RenderNode::setUserPointer`/`findUserPointerUpwards` (used by picking). |
| `engine_physic/CollisionTools` | **retire** | Legacy `RaySceneQuery` + triangle tests; already superseded by `PhysicsWorld::castRay` (physics) and `RenderWorld::queryRay` (editor AABB picking). Live callers: editor main.cpp (migrates), CameraDefaultModes (terrain follow — stub anyway), unbuilt tools. |
| `engine_filesystem/BigZip*` | **backend-facing but portable to Next** | `Ogre::Archive`/`ArchiveFactory` exist in Ogre-Next too (minor API drift). Stays as-is for both Ogre backends behind `RenderSystem::addResourceLocation(LT_BIGZIP)`; Filament gets an impl-side VFS. |
| `engine_sound`, `engine_input` | **math-only leak** | `Ogre::Vector3`/`Ogre::Camera*` listener. Math: alias handles it. `SoundManager::setListener(Ogre::Camera*)` → takes `optr<RenderCamera>` or a node (one-line seam). |
| `engine_runtime/PlayerRuntime` | **math-only leak + LogListener** | Wire-format vec/quat formatting (math alias) and an `Ogre::LogListener` (duplicated in the editor) — fold log forwarding into a small engine service (not part of the render facade; OGRE's LogManager is incidentally also present in Next). |
| `engine_module/module.cpp` Lua exports | **migrate onto facade** | Currently registers `Ogre::SceneNode/SceneManager/Viewport/Camera` usertypes. Re-target the same Lua-facing names at `RenderNode`/`RenderWorld`/`RenderCamera` (optr binds natively in sol2). Math usertypes follow the math decision (aliases = unchanged today). |
| tests | **math + headless parsing only** | No test touches a render backend; GuiAtlasTests' `Ogre::ConfigFile` follows GuiFactory's fate. |

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
| `RenderSystem.h` | `RenderSystem` | frame loop, main-window camera/background/resize/size (`showCameraOnWindow` + `getWindowCamera` - the latter added so CameraComponent can take over the window camera while apps still set it up through Engine), screenshots, `FrameStats` (fps/triangles/batches), resource locations (FileSystem/Zip/BigZip), `createRenderTexture`, `getWorld` |
| `RenderWorld.h` | `RenderWorld` | root node, node/content factories, ambient light, dynamic-shadow control (the `RenderCaps::DynamicShadows` capability + the `setShadowQuality` off/low/medium/high knob over `core_util/ShadowPreset.h`), sky/fog atmosphere (the `RenderCaps::SkyDome` capability + `setAtmosphere(AtmosphereDesc)` — sun-linked atmospheric sky dome + object fog on next, a vertex-colour gradient sky dome + fog subset on classic), `queryRay` AABB picking (`RayQueryHit`: distance/node/userPointer), `createVertexColourCubeMesh` (the backend cube-mesh service - the editor's "Create Cube" resource every scene-loading app needs; classic impl reuses PrimitiveUtil) |
| `RenderNode.h` | `RenderNode` | transform get/set (local + world), translate/yaw/pitch/roll/lookAt/setDirection/fixedYawAxis, child creation/re-parenting/navigation, visibility, world bounds, user-pointer back-mapping |
| `MeshInstance.h` | `MeshInstance` | ModelComponent needs (load/attach/visible/shadows/bounds/query flags), vertex-colour-unlit fixup + sub-mesh introspection (self-checks), `setMaterial` (assign a `createMaterial` material to all sub-meshes), AnimationComponent's AnimationState control surface (names/enable/loop/time/length/ended) |
| `RenderMaterial.h` | `RenderMaterialDesc` | the material authoring surface (metal-rough PBS description: albedo colour/map, metalness, roughness, normal map, emissive) consumed by `RenderSystem::createMaterial` — usually parsed from a `.omat` asset (see `Docs/materials.md`) |
| `SpriteQuad.h` | `SpriteQuad` | SpriteComponent needs: texture + texel size, size, UV rect, tint, flips, zOrder (painter's sorting), visibility |
<!-- 2D painter order is a per-backend implementation detail behind the SAME
zOrder contract. Next maps zOrder → render-queue id `50+z` (the queue id IS the
paint order). Classic can NOT: OGRE sorts alpha-blended, depth-write-disabled
renderables by CAMERA DISTANCE and does not honour the render-queue GROUP id
across groups for them (a full-screen backdrop at a lower group paints OVER a
higher-group sprite because, being centred, it sorts nearest). So the classic
backend puts ALL 2D content (SpriteQuad/VectorMesh/SpriteBatch) in ONE group
(`RENDER_QUEUE_MAIN`) and maps zOrder → render PRIORITY, which OGRE DOES honour
within a group — `RenderBackend::applyZOrder`. Both flavors therefore paint 2D by
zOrder (WYSIWYG, `render_backend_parity` + the `flatland` benchmark probe). -->

| `VectorMesh.h` | `VectorMesh` | flat-colour vector-shape content (VectorShapeComponent): a world-space untextured vertex-coloured indexed triangle list refilled from a CPU array (`setMesh`, the SpriteBatch contract), one shared "VectorFill" unlit alpha datablock, zOrder on the SAME painter window as SpriteQuad/SpriteBatch. classic = `ManualObject` OT_TRIANGLE_LIST + "VectorFill" material; next = v2 `ManualObject` SCENE_DYNAMIC + HlmsUnlit datablock. Triangles come from the renderer-free `core_util/VectorTessellator` |
| `RenderCamera.h` | `RenderCamera` | perspective/ortho (vertical half-extent), FOVy, aspect, near/far clip getters (added so projection switchers preserve the clips), viewport ray + project point, view/projection matrices (gizmo), wireframe toggle |
| `RenderLight.h` | `RenderLight` | deliberate minimum + room (type/colour/range/spot/shadows) — consumed by `engine_gocomponent/LightComponent` (dir/point/spot, reflected/serialized/Lua/MCP; intensity folds into the colour handed to the facade) |
| `RenderTexture.h` | `RenderTexture` | editor RTT panel: camera, background, overlays/shadows toggle, resize-by-recreate, native texture id for ImGui, `writeContentsToFile` |

**Not** in the facade, by design: materials as a system (only the two audited material
*services* exist — sprite material generation inside `SpriteQuad`, unlit fixup on
`MeshInstance`; per CLAUDE.md, keep materials simple/generated), overlays/imgui
(editor-private glue), frame *events* (stay on the core event system around
`renderOneFrame`), log forwarding (engine service, not renderer), Gorilla/gui.

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
  `std::shared_ptr` usertypes natively, so the Lua migration of the SceneNode
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
  containment rule and the facade-facing surface is alias-only, so swapping the
  header + a sed over stragglers closes it.

Accepted (and flagged above) tradeoff: facade headers transitively include Ogre
*math* headers. They still name zero Ogre types — the boundary is enforced socially +
by review, with the compile check keeping the umbrella (`Ogre.h`/EnginePrerequisites)
out.

---

## Classic → Ogre-Next mapping — the hard areas

| Area | Classic today | Ogre-Next reality | Plan |
|---|---|---|---|
| **Programmatic materials** | `MaterialManager::create` + `Pass::setSceneBlending/ setDepthWriteEnabled/setLightingEnabled/setVertexColourTracking` + RTSS generates shaders (sprite material, VertexColour, Gorilla atlas) | Materials are **HLMS datablocks**: `HlmsUnlit`/`HlmsPbs` + HlmsBlendblock (alpha blend) + HlmsMacroblock (depth check/write, culling) + HlmsSamplerblock. No RTSS, no `Pass`. OGRE *material scripts* effectively dead (already banned by CLAUDE.md) | The facade exposes ONE generated-material surface: `RenderSystem::createMaterial` (a metal-rough `RenderMaterialDesc`, usually parsed from a `.omat` — `Docs/materials.md`) maps to an HlmsPbs datablock on Next and an RTSS Cook-Torrance material (normal-map + additive emissive stages) on classic; `SpriteQuad` and `setVertexColourUnlit` stay a tiny set of generated HlmsUnlit datablocks. The generated-material discipline holds — no graphs, no script formats |
| **Meshes** | v1 `Ogre::Mesh` + `Entity`; assimp codec loads glTF/glb directly | v2 `Mesh`/`Item` (+`Ogre::v1::` legacy namespace). Assimp-loaded/v1 meshes must go through `Mesh::importV1` (or the v1 entity path with worse performance) | `MeshInstance` hides it: Next backend loads v1 via codec then `importV1` → `Item`. Skeletal: v2 `SkeletonInstance` differs — the facade's AnimationState-shaped API is implementable on both; root-motion bone digging stays a classic-only backdoor |
| **RTT** | `TextureManager::createManual(TU_RENDERTARGET)` → `getBuffer()->getRenderTarget()->addViewport(camera)`; per-viewport background/overlay toggles | No viewports-on-targets. `TextureGpuManager::createTexture(RenderToTexture)` + a **compositor workspace** whose pass targets the texture; clear colour and overlay inclusion are compositor-pass properties | `RenderTexture` maps 1:1 onto a one-pass workspace; `setOverlaysEnabled(false)` = omit the overlay pass. Resize-by-recreate matches both. ImGui consumption differs per RS — hence the opaque `getNativeTextureId` |
| **Ray queries** | `SceneManager::createRayQuery` → AABB hits; CollisionTools adds triangle tests via hw-buffer reads | Next still has `RaySceneQuery` (v2 objects) but it is de-emphasized; triangle-accurate picking against render meshes means VAO reads (painful) | `RenderWorld::queryRay` stays AABB-level (that is all the editor uses). Triangle accuracy = `PhysicsWorld::castRay` against collision shapes — physics is the cross-backend truth for precise picking. CollisionTools retires |
| **Frame loop / rendering** | `Root::renderOneFrame` renders viewports implicitly | Nothing renders without an explicit **CompositorManager2 workspace** per target/window | Next backend creates one window workspace in `RenderSystem` setup + one per `RenderTexture`. Facade signature unchanged |
| **HUD (Gorilla/gui)** | RenderQueueListener + `manualRender` + hand-built vertex buffers + texel offsets (see audit) — every removed-in-Next API at once | Would need: v2 Renderable + VaoManager buffers + HlmsUnlit + compositor hook. A rewrite, per backend, forever | **Recommend: Gorilla classic-only; future HUD = facade `SpriteQuad` layer** (screen-space ortho camera + zOrder painter sorting — SpriteComponent already proves the primitives). One HUD implementation for every backend incl. Filament, instead of N ports of a dead library. jumper/roller HUDs migrate onto the facade HUD |
| **Window/stats plumbing** | `RenderWindow::writeContentsToFile/getStatistics/ windowMovedOrResized`; `Viewport::getActualWidth` | `Ogre::Window` + `TextureGpu` readback; stats via RenderSystem metrics/workspace | All behind `RenderSystem` methods already |
| **Ambient light** | `SceneManager::setAmbientLight(colour)` | `setAmbientLight(upperHemi, lowerHemi, dir)` | Facade `setAmbientLight` takes one colour (flat); `setAmbientHemisphere(upper, lower)` is native on Next and averaged-to-flat on classic (an honest subset) |
| **Resources/archives** | `ResourceGroupManager`, `Archive`, BigZip subclass | Same subsystem exists (minor drift); HLMS additionally needs its library folders registered | `addResourceLocation` unchanged; Next backend registers HLMS data in setup |

Filament notes are inline per method in the headers; the structural ones: no scene
graph (TransformManager parent links — RenderNode maps cleanly), no material system to
avoid (filamat/matc compiled materials — the generated-materials discipline maps to a
handful of prebuilt .filamat), no resource groups (impl VFS), picking via impl AABB
walk or `View::pick`, and `Renderer::readPixels` for both screenshot paths.

---

## Build flavors

- **`ORKIGE_RENDER_BACKEND=classic|next`** (root CMakeLists cache option, originally
  default `classic` — *later superseded: the default is `next`, see "Default backend" at the
  end of this document*; `next` initially stopped with a pointed FATAL_ERROR until the
  backend landed). It sets exactly one of `ORKIGE_RENDER_CLASSIC` / `ORKIGE_RENDER_NEXT`
  as a PUBLIC compile definition on `orkige_engine`; `RenderFacadeCheck.cpp` hard-errors
  when none/both are set. Selecting `next` additionally: swaps the vcpkg dependency
  (`ogre` ↔ `ogre-next` feature set via manifest features), swaps `find_package`, and
  swaps the backend impl directory in `ORKIGE_ENGINE_SOURCES`.
- **`ORKIGE_RENDERSYSTEM` (env) keeps its meaning**: the *runtime* graphics-API pick
  (GL3Plus default / Metal / Vulkan / GLES2) among the plugins the chosen backend
  linked. Both Ogre backends have such plugins; the two knobs are orthogonal and must
  never be merged. Documented in `RenderSystem.h`'s naming note.
- **Presets**: `macos-debug/-release` started as classic (no reconfigure churn).
  The next flavor adds `macos-debug-next` (inherits base, sets `ORKIGE_RENDER_BACKEND=next`,
  `binaryDir` `build/macos-debug-next`) + matching build preset and `unit-next` /
  `desktop-next` test presets. Mobile presets started classic pending mobile-backend evaluation.
  *(Later superseded by the default flip: `macos-debug/-release` = next,
  classic moved to `macos-*-classic`; the mobile presets flipped the same way
  — `ios-simulator-debug`/`android-debug` = next, `-classic` variants added —
  once next rendered on both devices. See "Default backend" at the end.)*
- **Directory layout** (recommendation):
  - `engine_render/` — interfaces only (no implementation). Never includes backend headers.
  - `engine_render_classic/` — NEW home for facade impls (`RenderNodeClassic.cpp`, ...)
    and a private `ClassicBackend.h` (the one place allowed to hand Ogre pointers to
    classic-only zones like Gorilla glue during migration).
  - `engine_graphic/` — **keeps its name** and becomes de-facto classic-private
    (Engine.cpp already is the classic bootstrapper; renaming it buys nothing and
    breaks every include). Engine's window/config/event plumbing stays; its
    scene-facing accessors (`getSceneManager/getCamera/getViewport`) get deprecated in
    favor of `RenderSystem::get()` and deleted once call sites are migrated.
  - The next flavor adds `engine_render_next/` mirroring classic.
- **Test matrix**:
  | Suite | classic | next |
  |---|---|---|
  | `ctest --preset unit` (headless) | every commit | every commit (`unit-next`; facade-level unit tests are backend-parametrized by construction — they only see engine_render) |
  | `ctest --preset desktop` (integration) | every commit — the 137-green contract | `desktop-next` once demos render; until then a build-only `next-compile` check |
  | `ctest --preset all` (device) | deploy changes | classic-only until the mobile backend is decided |
  A facade smoke integration test (`render_facade_selfcheck`: node
  hierarchy + mesh + sprite + camera + RTT + queryRay + screenshot in one headed run)
  — that test IS the backend conformance suite and must pass identically per backend.

---

## How the abstraction was built

### Classic backend implements the facade; call sites migrate (behavior-neutral)

- **Backend skeleton**: `engine_render_classic/`
  implementing all 8 classes against OGRE 14 (Impl structs; `RenderSystem::get` wired
  from `Engine::setup`; query-flag defaults; user-pointer bookkeeping). Files: new
  `engine_render_classic/*.cpp` (~8), `orkige_engine/CMakeLists.txt`, small hooks in
  `engine_graphic/Engine.{h,cpp}`. Plus the `render_facade_selfcheck` app + ctest
  registration. Result: facade fully usable next to the old paths.
  Implementation notes: the backend's private door is `struct RenderBackend`
  (`engine_render_classic/ClassicBackend.h`, befriended by the facade classes via
  `RenderPrerequisites.h`); the zero-caller dead files were deleted; the selfcheck
  lives in `tests/render_facade/` (backend-agnostic main + per-backend bootstrap TU)
  and IS the conformance suite every future backend must pass.
- **Components**: `TransformComponent` (drops the SceneNodeGuard base for an
  owned `optr<RenderNode>`; keeps its event surface), `ModelComponent`
  (`MeshInstance`), `SpriteComponent` (`SpriteQuad`; pure helpers stay),
  `AnimationComponent` (facade animation API; root-motion via classic backdoor),
  `CameraComponent` (`RenderCamera`), `CameraDefaultModes` (drop
  CollisionTools terrain stub). Files: `engine_gocomponent/*.{h,cpp}` (12),
  `engine_util/NodeUtil.h` shrink. Tests: TwoDSupport/component suites keep passing
  untouched where possible.
  Implementation notes:
  - `SceneNodeGuard` was RESHAPED (not yet deleted): it stays the components'
    common base but now owns an `optr<RenderNode>` and forwards only the ~15
    used methods; components keep their `initSceneNodeGuard`/`deinitSceneNodeGuard`
    shape (the reshape stops short of inlining the optr — see the recorded
    deviation below). The Ogre `Node::Listener` is gone;
    `NodeAttached/DetachedEvent` fire from `attachToNode`, `NodeUpdatedEvent`
    is declared but NOT emitted (no facade per-node update callback, zero
    consumers existed) - wire a facade listener if a consumer appears.
  - **Dual-tagging (transition)**: `TransformComponent::onAdd` sets the facade
    user pointer (`RenderNode::setUserPointer(this)`) AND, classic-only, the
    legacy `Ogre::Any` user binding on the backend node, because the editor
    still picks with its own Ogre `RaySceneQuery` until the editor migrates. The classic
    `getComponentFromNode(Ogre::Node*)` overload and NodeUtil's raw-node
    overload exist solely for that path; the editor migration deletes all three together.
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
    NOT their later migrations, just compile-compat.
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
    demo content migrates onto facade types.
- **Apps + services**: player, hello_orkige, jumper, jumper-native onto
  `RenderSystem` services (resource locations, ambient, background, screenshots,
  stats, window size) + `SoundManager::setListener` seam + PlayerRuntime/editor log
  forwarding into one engine service. Files: `tools/player/main.cpp`,
  `samples/*/main.cpp`, `projects/jumper-native/native/main.cpp`,
  `engine_sound/SoundManager.{h,cpp}`, `engine_runtime/PlayerRuntime.cpp`.
  Implementation notes:
  - **The sanctioned classic boot block**: a marked raw-Ogre corner covering
    exactly (a) the `Engine` constructor/window params (`Ogre::SMT_DEFAULT`),
    (b) the RTSS-internal media registration into `RGN_INTERNAL` (must
    precede `Engine::setup`; same rule as
    `tests/render_facade/bootstrap_classic.cpp`), (c) `ORKIGE_RENDERSYSTEM`
    handling via `Engine::setPreferredRenderSystem` (classic-internal runtime
    graphics-API pick, see "Build flavors"). Everything after `Engine::setup`
    is facade-only. Originally every app carried its own copy; the five
    per-host blocks have since been consolidated into the ONE shared boot
    scaffold `engine_runtime/AppHost.cpp` - the single sanctioned
    classic-boot zone in `Util/ogre_containment.json`.
  - **Window camera**: hello_orkige, jumper and jumper-native replaced
    `createDefaultCameraAndViewport` with a facade rig (`createCamera` +
    `createNode` + `setFixedYawAxis` + `showCameraOnWindow`). tools/player
    KEEPS the Engine-path camera (documented residue): the project scripts
    drive it through the Lua bindings (`engine:getCamera(0)`,
    `setCameraOrthographic`, `getViewport`) which the Lua migration re-targets - only
    then can the player switch rigs. `Engine::getViewport` grew a classic
    migration bridge: it falls back to the window's viewport 0 when apps boot
    the camera through the facade, so gui (classic-only, reads
    `Engine::getViewport`) works on both boot paths until the Lua migration.
  - **hello_orkige content**: the raw ManualObject cubes became instances of
    the facade cube-mesh service (`createVertexColourCubeMesh("HelloCube.mesh",
    1.0)` + `createMeshInstance`), physics-box visuals are per-axis-scaled
    child nodes; the "reach the component's backend node by name" transition
    hack is gone (visuals attach via
    `TransformComponent::createChildNode`). Cosmetic delta: the orbiter cube
    reuses the shared vertex-colour order (the reversed palette is gone).
    Triangle self-checks read `RenderSystem::getFrameStats`.
  - **Log service**: `engine_base/EngineLog.h` (`EngineLogCapture`) is the
    engine-service home for log capture/forwarding - pimpl over the logging
    backend (OGRE LogManager, also present in Next), bounded backlog,
    `drain()` per frame, static `logMessage`. `PlayerDebugLink` uses it; the
    editor's duplicated console listener migrates with the editor.
  - **SoundManager seam**: the listener is now an `optr<RenderNode>` (ctor +
    `setListener`); forward/up derive from the node's world orientation
    (-Z/+Y). No live caller existed - the seam is for the camera-node rigs.
  - `engine_util/FrameStatsUtil` needed NO change: it is a frame-TIME
    (wall-clock dt) collector with zero renderer coupling; the renderer
    stats consumers (triangle counts) moved to `RenderSystem::getFrameStats`.
  - JumperHud (gui, classic-only per the HUD decision) kept exactly one Ogre
    spelling: the resource-group default parameter it forwards to
    GuiManager; its math went to the alias vocabulary.
- **Editor**: RTT panel → `RenderTexture`; picking → `queryRay` +
  `findUserPointerUpwards`; gizmo → `RenderCamera` matrices/project; stats panel;
  grid via backend service; ImGuiOverlay glue isolated behind an editor-local seam.
  Files: `tools/editor/main.cpp`, `EditorCore.{h,cpp}`. CollisionTools retired
  (removed from build with the zero-caller dead files).
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
    ORKIGE_RENDERSYSTEM, RTSS internal media - since moved into the shared
    `engine_runtime/AppHost.cpp` scaffold); (b) the
    OverlaySystem/ImGuiOverlay wiring incl. the font-texture SetTexID
    bridge; (c) the UI-only window viewport (visibility mask 0 + RTSS
    scheme - per-viewport masks are deliberately not facade API);
    (d) `createEditorGrid`'s ManualObject line list. Residual `Ogre::` in
    tools/editor: 33 spellings in main.cpp (all in those blocks or
    comments), plus 5 comment-only mentions across
    ImGuiSDL3Input/EditorTheme; down from the audit's 211.
- **Lua + containment lock**:
  `module.cpp` usertypes re-targeted at facade classes; player onto the facade
  camera rig; the containment rule became a mechanical gate. Gate:
  `ctest --preset desktop` green (139 tests incl. the lint).
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
    earlier residue (Engine-path camera + roll probe) is gone.
    `Engine::getViewport`'s bridge STAYS with one consumer: gui
    (classic-only) — it goes with the draw-surface seam.
  - **Late-handle guard**: script-held facade handles legally outlive the
    render system now (Lua userdata lives until the Lua state closes, after
    ~Engine) — the classic RenderNode/RenderCamera dtors detect the dead
    backend (`RenderBackend::system() == NULL`) and free facade memory only.
  - **SceneNodeGuard NOT deleted — recorded plan deviation**: the
    reshape left it Ogre-free and it is the shared facade-node-owner base of
    exactly three components (Transform/Model/Sprite); inlining the optr
    would triple ~15 forwarding methods for zero containment gain. It stays
    (in engine_util) until a component needs a different node model.
  - **Containment lint**:
    `Util/check_ogre_containment.py` + `Util/ogre_containment.json`, wired
    as ctest `render_containment_lint` (LABELS unit → unit AND desktop
    presets). Comment-stripped scan; allowed = engine_graphic/,
    engine_render_classic/ (+ engine_render_next/ for the next flavor), RenderMath.h;
    everything else needs a config entry (whole dir/file with reason — the
    audit's residual lists are the baseline) or an explicitly marked
    `ORKIGE_SANCTIONED_OGRE_BEGIN(<tag>)`/`_END` block (the app classic boot
    blocks, the editor glue, the AnimationComponent root-motion backdoor).
    Stale sanctions fail the lint too, so the list only shrinks. Current
    violation count: **0**. Bonus de-leak: ScriptComponent's error logging
    moved onto `EngineLogCapture::logError` (the engine log service grew the
    stderr-fallback error path).

### Classic backend status

Everything above `engine_graphic`/`engine_render_classic` talks to the
renderer exclusively through `engine_render` — components, apps, tools,
editor AND the Lua script surface — with the residue pinned down in
`Util/ogre_containment.json` and enforced by `render_containment_lint`.

**What a new backend must implement** (the `engine_render_next/` work
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
  gui-dependent skips (HUD arrives with the facade HUD).

### Ogre-Next backend

- **Dependency + flavor**:
  vcpkg manifest feature for `ogre-next` (locally authored overlay port,
  3.0.0 - see Docs/ports.md), `ORKIGE_RENDER_BACKEND=next` wiring
  (find_package/link/impl-dir swap), `macos-debug-next` preset,
  `desktop-next` test preset. Beyond build-only: the flavor
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
  is registered DISABLED until the stubs are filled. Flavor gates:
  hello_orkige/jumper/editor/player stay classic-only (they boot through
  the classic Engine; the apps are ported later). Deviations recorded:
  `RenderMath.h` grew a per-backend `Affine3` alias (Ogre-Next has no
  Affine3; Matrix4 carries the affine helpers - only the classic-only
  editor gizmo consumes it) and `RenderCamera::setWireframe` is a stub on
  Next (the v2 Camera lost the polygon-mode toggle; revisited below).
- **Core scene + content**: `engine_render_next/` implements the WHOLE facade;
  **`render_facade_selfcheck` passes UNCHANGED on `desktop-next` (enabled in ctest,
  zero carve-outs)**; classic `desktop` stays 139-green. Per-class notes (the
  reference for future material/mesh work on Next):
  - **Mesh path**: Ogre-Next has no assimp codec, so the backend
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
    import (`SubMesh::setMaterialName`). The road FORKS on skinning: a STATIC
    source gets `PreTransformVertices` (node transforms baked, the historical
    path byte-identical — the probe parses first with zero post-processing and
    defers the post steps via `ApplyPostProcessing`, which is exactly what a
    one-call read does internally); a SKINNED/ANIMATED source keeps the node
    hierarchy — the backend-neutral `engine_render/SkinnedRigExtract` fills a
    `SkinnedRig` (joints/clips/skins), realised as a `v1::Skeleton`
    (`OldSkeletonManager`, manual) + v1 animation tracks + per-sub-mesh bone
    assignments, node transforms applied explicitly per section, and
    `importV1` carries the whole rig to the v2 mesh (`SkeletonDef` + compiled
    blend buffers) where the facade animation surface plays it over v2
    `SkeletonInstance`/`SkeletonAnimation`. Animated BOUNDS are a facade
    concern here: Ogre-Next keeps an item's local Aabb at the bind pose, so an
    armed `setAnimatedBounds` instance derives `getLocalBounds` from the live
    bone poses expanded by the import-time bone radius (classic's
    bounds-from-skeleton semantics); clip NAMES read back through the
    backend's skinned-mesh registry because the v2 `IdString` only keeps
    readable strings in debug builds.
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
    colour management is a later refinement, not a blocker here.
  - **SpriteQuad**: v2 `ManualObject` quad (has `colour()` — tint/UV/flip rebuilds
    identical to classic); zOrder → render queue `50+z`, inside Next's default-FAST
    v2 queues 0..99, so no queue-mode surgery needed.
  - **RenderTexture**: `TextureGpu(RenderToTexture)` + one basic workspace per
    target incarnation (background bakes into the definition; setCamera/resize/
    background = recreate); `writeContentsToFile` is a plain
    `Image2::convertFromTexture` readback (only the WINDOW needs the Metal
    manual-swap dance); `getNativeTextureId` = the `TextureGpu*` (documented
    opaque id; editor is classic-only). Overlays/shadows toggles
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
  - **Recorded deviation — `setWireframe` is GLOBAL on Next** (refining the
    polygon-mode decision): the v2 camera lost the polygon-mode toggle; the backend flips the
    macroblock polygon mode of every generated datablock instead. Fine for the
    debug-view call sites; noted in RenderCamera.h.
  - Honest gap (`notImplementedOnce`): `LT_ZIP`/`LT_BIGZIP` resource
    locations (zziplib port feature + engine_filesystem port pending).
    (Skeletal glb import was the second gap here — closed by the skinned
    fork of the assimp road above.)
- **Parity run**: real games run on the Next flavor. What landed:
  - **EnginePrerequisites de-classicified**: `engine_module/EnginePrerequisites.h`
    is backend-NEUTRAL (core prerequisites + Meta + the `RenderMath.h` alias
    vocabulary — no `<Ogre.h>`). Classic-only TUs include the new
    `engine_module/EnginePrerequisitesClassic.h` (neutral umbrella + the classic
    OGRE/Overlay headers; hard `#error` on the next flavor): engine_graphic's
    classic files, gui, BigZip, Localisation, PrimitiveUtil/MeshUtil,
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
    probe it and skip their gui HUD honestly (state machines, tile slides,
    win flow run identically; jumper's title/win advance on ENTER). module.cpp's
    gui/IngameConsole exports are `#ifdef ORKIGE_RENDER_CLASSIC` — an
    unguarded Gui call on next fails with an honest Lua nil error. The
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

### Backend conformance status

Dependency/flavor, boot skeleton, full facade conformance and games-run are
all in place. **Flavor capability matrix**:

| Capability | classic (`macos-debug-classic`) | next (`macos-debug`) |
|---|---|---|
| engine_render facade (conformance suite) | yes | yes (zero carve-outs) |
| components/game objects/serialization | yes | yes |
| `LightComponent` (dir/point/spot over `RenderLight`; reflected/serialized/Lua/MCP) | yes | yes (per-flavor lit-vs-unlit selfcheck; the demo meshes carry no PBS normals, so the render-difference probe drives the ambient term) |
| hemisphere ambient (`RenderWorld::setAmbientHemisphere`) | REGISTERED subset: the two hemisphere colours are AVERAGED to one flat ambient. classic OGRE's `SceneManager::setAmbientLight` takes a single colour, and the RTSS lighting stages consume the flat `ACT_DERIVED_AMBIENT_LIGHT_COLOUR` auto-param — a true two-colour sky/ground blend would need a custom RTSS sub-render-state (the `RenderCaps::HemisphereAmbient` capability, next-only) | yes (native two-colour sky/ground term, `setAmbientLight(upper, lower, dir)`) |
| PBS materials (`.omat` → `RenderSystem::createMaterial` + `MeshInstance::setMaterial`; `ModelComponent.material`) | RTSS metal-rough: `SRS_COOK_TORRANCE_LIGHTING` (metalness/roughness from `specular.xy`, albedo colour+map, emissive colour) + `SRS_NORMALMAP` (normal map, tangents built on demand in `setMaterial`) + an additive pass for the emissive map (opaque only) — maps render, but the shading model/ambient differ so still no pixel parity for lit content. Cutout (`alphaTest`) = pass alpha rejection + a generated `<name>/Caster` shadow-caster override so cutouts shadow as cutouts; `twoSided` = `CULL_NONE`, back faces lit with the front normal (registered subset) | yes — native HlmsPbs metallic workflow incl. normal/emissive maps (tangents generated at import for UV meshes), native alpha-test incl. the caster path, native two-sided lighting; per-flavor `demo_material` selfcheck + the `material_looks_right`/`material_cutout_right` pixel probes |
| animated water (`RenderWaterDesc` → `RenderSystem::createWaterMaterial` + `setWaterTime`; `WaterComponent`) | RTSS metal-rough transparent plane: Cook-Torrance on the deep/shallow tint (opacity=alpha) with an intrinsic Fresnel edge, plus a COMPOSITE of two cues from the one normal map — the RTSS normal-map stage LIGHTS the ripples (a static sun-catching relief) while the same map bound a second time gives a scrolling colour-shimmer MOTION. REGISTERED subset: the lit ripple detail is STATIC (RTSS classic samples the raw texcoord, so a normal map lights OR scrolls on one unit, not both) — fully animated normal-mapped water + the two detail-normal ripple are next-only | yes — HlmsPbs specular-as-fresnel dielectric: TWO detail normal maps scrolling in different directions/speeds (the ripple), realistic fresnel-preserving transparency, deep-colour water body + subtle shallow-colour scatter; per-flavor `demo_water` selfcheck. Common v1 boundary BOTH flavors: NO screen-space refraction distortion and NO true depth-graded deep→shallow transmission — both need a compositor refraction/depth pass (a future desktop quality knob, see below) |
| dynamic-light budget (`RenderSystem::lightBudget` / `engine:getLightBudget`; a NUMERIC capability beside the boolean register) | ~30 concurrent dynamic point/spot lights — the RTSS forward renderer's per-pass headroom (`RenderBackend::FORWARD_LIGHT_BUDGET`) | 96 — the clustered-forward per-cluster light-list bound set at boot (`setForwardClustered` lightsPerCell); the worst-case (all lights in one cell) concurrent count, so next's signature many-lights headroom is far above classic's floor |
| dynamic shadows (`RenderWorld::setShadowQuality` knob + `r.shadowQuality` cvar; `LightComponent.castsShadows`; per-object `ModelComponent.castShadows`/`receiveShadows`, `WaterComponent.receiveShadows`) | yes — RTSS integrated PSSM (`SHADOWTYPE_TEXTURE_ADDITIVE_INTEGRATED`): real 16-bit depth shadow maps + a `PSSMShadowCameraSetup` split scheme, with the shadow-mapping receiver sub-render-state injected ONCE into the generated-material scheme (the shadow factor folds into the SAME Cook-Torrance/FFP lighting stage every lit material uses; 2D/unlit materials opt out via `setReceiveShadows(false)`, so sprites/shapes/gui never join the pass). Armed only while the knob is on AND a directional light casts AND the atmosphere-driven sun is not night-dark; disarmed restore-exactly (technique NONE, maps freed, receiver removed). Same `ShadowPreset` budgets as next; the 1-split low tier renders as a single focused map. RUNTIME-gated on depth-texture render targets — a bare GLES2/WebGL1 context answers `supports(RenderCaps::DynamicShadows)` = false with ONE log line. Cascade-blend/filter quality differs from next (tolerance parity, not pixel parity) | yes (PSSM/PCF compositor shadow node in the window and RTT workspaces, attached lazily while quality ≠ off AND a light casts; v1 = DIRECTIONAL casters; budgets in `core_util/ShadowPreset.h` — medium default is the phone budget: 2 cascades, 1024 base atlas ≈ 6 MB, 3×3 PCF; the 1-split low tier is a single focused map) |
| sky / fog / day-night atmosphere (`RenderWorld::setAtmosphere(AtmosphereDesc)` + the `RenderCaps::SkyDome` capability; `Engine::setAtmosphere` Lua; `core_util/AtmosphereDesc.h`) | vertex-colour gradient sky dome SUBSET (`supports(RenderCaps::SkyDome)` = true): a camera-following inward sphere in the sky render queue whose vertex colours are a zenith->horizon->ground gradient (horizon hazed by `density`) plus a soft dot-product sun glow toward the FIRST directional `RenderLight`; `fogDensity`/fog colour drive FOG_EXP2 scene fog. NO true atmospheric scattering; the sun glow recomputes on `setAtmosphere` (games pair sun-orient with setAtmosphere), so a game that re-orients the sun WITHOUT calling setAtmosphere shows a STALE glow (next's native link tracks it live) - the registered subset gap. SKY TYPES (`AtmosphereDesc::skyType`, `engine:setAtmosphereSky`): `skybox` raises the native camera-bound `SceneManager::setSkyBox` over a generated cubemap material (first texture unit `TEX_TYPE_CUBE_MAP`, one cubemap `.dds` — `Util/make_sky_assets.py` bakes the stock set), `colour` clears flat in the sky tint; fog + the sun drive stay active on every type | yes — native `AtmosphereNpr` (atmospheric sky dome + HlmsPbs-integrated object fog + sun-linked day/night); the sun is the FIRST directional `RenderLight` (its direction drives the sky, the atmosphere drives its colour/power); sky material media ships from the ogre-next port (`Media/Atmosphere`); a media-less/headless boot degrades to the flat sky colour. SKY TYPES: `skybox` shows the native `SceneManager::setSky(SkyCubemap)` quad (the `Ogre/Sky/Cubemap` material from the same port media set, moved to the skies-early queue like the NprSky quad; the cubemap loads non-sRGB, no batching — colour parity), `colour` clears flat; `AtmosphereNpr` stays alive on every type for fog + sun linkage. Both flavors sample the cubemap with the same `(x, y, -z)` convention, so one `.dds` renders the SAME picture per flavor |
| Lua scripting (sol2 module surface) | yes | yes (minus gui usertypes) |
| input (SDL3, tilt sim), sound (OpenAL, .caf/.wav), physics (Jolt) | yes | yes |
| player + hello_orkige + games (jumper-lua, roller) | yes | yes (full HUD: `engine:hasUISystem()` = true) |
| gui HUD (widgets + UiAtlas/UiRenderer on DrawLayer2D; Gorilla DELETED) | yes | yes (one draw batch per screen, selfchecked) |
| offscreen 2D composition (DrawLayer2D into an RTT; the editor GUI Preview tab + `preview_ui`) | REGISTERED next-only: `supports(RenderCaps::OffscreenOwnedLayers)` = false. classic's 2D composite is one main-window-gated RenderQueueListener over shader-only materials the RTSS transiently rebuilds; per-target offscreen surfaces are a distinct next-only render path — the tab disables with a note | yes (per-target UI pass + visibility band; `render_facade_selfcheck` pixel case) |
| IngameConsole | yes | no — classic Overlay zone (rebuild on gui/DrawLayer2D when wanted) |
| editor (ImGui on DrawLayer2D since the editor-on-Next port) | yes | **yes** (the editor-stays-classic decision was superseded, see the editor-on-both-flavors section) |
| pixel-level colour parity with classic (WYSIWYG) | — (the reference) | yes (`render_backend_parity`; gamma-space passthrough) |
| jumper sample (C++ gui HUD) | yes | no (classic boot block only; the HUD itself is flavor-neutral now) |
| BigZip / LT_ZIP resource locations | yes | honest `notImplementedOnce` stub |
| project export (macOS/iOS/Android) | yes (RTSS media) | yes (bundles Hlms shader media) |
| Vulkan/GL runtime RS pick | yes | no (classic-backend concern; next boots Metal) |
| mobile: iOS + Android runtime | yes (GLES2) | yes (iOS Metal, Android Vulkan — the default) |
| skinned glTF import + skeletal clip playback (`AnimationComponent` over `MeshInstance`) | yes — the upstream assimp codec builds the skeleton + `AnimationState` clips; bounds via `setUpdateBoundingBoxFromSkeleton` | yes — the backend's own assimp road forks on skinning: the neutral `SkinnedRig` extraction → v1 skeleton/tracks/weights → `importV1` → v2 `SkeletonInstance` playback incl. weighted crossfade; animated bounds derived from live bone poses (`player_character_rig_selfcheck` both flavors) |
| root-motion animation backdoor | yes | no (classic-only backdoor) |
| static mobility flag (`TransformComponent.static` → `RenderNode::setStatic`; `r.staticScene` gate) | yes — entities on static nodes bake into shared `StaticGeometry` regions (fewer draws; demote-on-move repair) | yes — node + content migrate into the `SCENE_STATIC` memory managers (no per-frame transform/cull prep; `notifyStaticDirty` repair). `Docs/performance.md` |
| sprite-run batching (`SpriteBatcher` + pure `SpriteRunPlanner`; `r.spriteBatching` gate) | yes — one facade `SpriteBatch` (ManualObject) per contiguous same-material run | yes — same facade path over the v2 ManualObject + shared HlmsUnlit datablock |
| same-mesh 3D instancing | no — gated out by verdict (RTSS generates no instanced vertex path; measured content never needs it — `Docs/performance.md`) | yes — native Hlms auto-instancing of identical Items (nothing to declare) |
| per-scene structural budget gate (`benchmark_budget` ctest) | yes | yes |

Remaining known gap on next (logged once at runtime):
LT_ZIP/LT_BIGZIP locations. (The earlier "sRGB-swapchain colour difference"
is GONE — see the colour-parity work below; the skeletal-glb-import gap
closed with the skinned fork of the assimp road.)

### Render capability register

The machine-checked subset of the matrix above: the render deltas an app can
PROBE at runtime, each a `RenderCaps` enum identity behind one call —
`RenderSystem::get()->supports(RenderCaps::X)` from engine code,
`engine:supports("name")` from Lua, and MCP `get_state`'s `capabilities` object
for an agent. A capability's identity, name, kind and description live in ONE
place — the `ORKIGE_RENDER_CAPS` X-macro table in
`orkige_engine/engine_render/RenderCaps.h` — from which the enum, the name lookup
and the parse all expand, so they cannot drift (no sidecar, no generated header).
The per-flavor VALUES are filled by each backend at boot and mirrored by a
committed snapshot table (`engine_render_classic/RenderCapsExpectedClassic.inc`,
`engine_render_next/RenderCapsExpectedNext.inc`); the `render_facade_selfcheck`
register leg asserts each backend's live `supports()` matches its snapshot and
that every enum identity is covered. The matrix below is generated from the
X-macro (names/kind/description) joined to the two snapshots (the classic/next
columns); `update_docs.py --check` fails on an unregenerated edit to EITHER
source, so a cap that drifts between the enum, a snapshot and the backend that
renders it fails CI.

<!-- GENERATED:render-caps-matrix - edit Util/update_docs.py / lua_api_annotations.json; do not hand-edit -->
| Capability (`RenderCaps` name) | classic | next | What it is |
| --- | :---: | :---: | --- |
| `skyDome` | yes | yes | a horizon-to-zenith sky dome behind the scene (sun-linked atmospheric on next, a vertex-colour gradient on classic) vs a flat clear colour; the dome is the `procedural` sky type - `AtmosphereDesc::skyType` also selects a cubemap `skybox` or a flat `colour` sky on both flavors |
| `dynamicShadows` | yes | yes | dynamic shadow maps cast by shadow-casting directional lights (next = compositor PSSM + PCF; classic = RTSS integrated PSSM folded into the one generated-material scheme - on GLES2/WebGL the bit is runtime-gated on depth-texture render targets) |
| `hemisphereAmbient` | no | yes | a two-colour sky/ground ambient term; classic averages the two colours to one flat ambient |
| `sunExposureLinkage` | yes | yes | the atmosphere drives the linked sun's colour/power (an exposure the un-tonemapped pipeline can clip) - native on next, the same day/night curve evaluated on the CPU on classic (colour + averaged-flat ambient fill, tolerance parity) |
| `animatedNormalMappedWater` | no | yes | fully animated normal-mapped water ripples; classic lights OR scrolls one normal map on a unit, not both, so its lit relief is static |
| `offscreenOwnedLayers` | no | yes | 2D layers composited into an offscreen RenderTexture (the editor GUI Preview + preview_ui), not just the main window |
| `screenSpaceRefraction` | no | no | screen-space refraction distortion through transparent surfaces (a compositor refraction pass) - absent on both flavors |
| `iblReflections` | no | no | image-based lighting: environment/reflection cubemaps on PBS materials - absent on both flavors |

_A capability marked `no`/`no` is a `PlannedAbsent` v1 boundary (absent on both flavors, next-first when it lands); the rest are real classic/next deltas. Probe from code with `RenderSystem::get()->supports(RenderCaps::X)`, from Lua with `engine:supports("name")`, and over MCP from `get_state`'s `capabilities` object._
<!-- /GENERATED:render-caps-matrix -->

Not every capability is a boolean. The **dynamic-light budget** is the NUMERIC
sibling of this register: `RenderSystem::lightBudget()` (Lua
`engine:getLightBudget()`) answers the flavor's sane ceiling on concurrent
dynamic point/spot lights — classic reports its RTSS forward per-pass headroom
(`FORWARD_LIGHT_BUDGET` = 30), next derives it from the clustered-forward
light-list bound configured at boot (`setForwardClustered` lightsPerCell = 96,
the per-cluster worst case). It is filled once at boot from the pure per-flavor
`RenderSystem::defaultLightBudget()` (unit-tested headless per flavor, the
`RenderCapsExpected*.inc` discipline), and the `render_facade_selfcheck` asserts
the live value equals it. A consumer that ramps live lights — the benchmark's
many-lights showcase — caps at this instead of an authored constant, so next's
signature many-lights headroom is not pinned to classic's floor.

**Future desktop quality knob — water refraction/depth pass.** The animated
water v1 is contained deliberately: it renders through the EXISTING single scene
pass, so it does NOT do screen-space refraction distortion or a true
depth-graded deep→shallow transmission. HlmsPbs CAN do refraction
(`HlmsPbsDatablock::Refractive` + `setRefractionStrength`), but the datablock
doc is explicit that "the compositor scene pass must be set to render refractive
objects in its own pass" — i.e. splitting the `engine_render_next` workspace
into a refractions render pass reading a depth/refraction texture. That is a
workspace RESTRUCTURE, not a contained material change, and it was consciously
deferred (no `ports/` edit was permitted for the water package either). When a
desktop-quality vista wants it, the knob is: add the refraction pass to the next
workspace, flip the water datablock to `Refractive`, and feed `fresnelPower`/a
new `refractionStrength` through `RenderWaterDesc`. The reflected properties and
`WaterComponent` need no shape change to pick it up.

### Cross-backend HUD + closure

- **Facade HUD & migration** (revised design): instead of the sprite-quad HUD
  sketched above, the owner decided
  (a) gui runs on BOTH backends and (b) **Gorilla is DROPPED**. What
  landed:
  - `engine_render/DrawLayer2D` — the facade's screen-space 2D layer:
    retained pixel-space triangle batches, per-batch texture binding by
    resource name, analytic (Sutherland-Hodgman) scissor clipping shared by
    both backends (`DrawLayer2DClip.h`), zOrder compositing over the main
    window only. Classic impl: one RenderQueueListener after
    RENDER_QUEUE_OVERLAY + `manualRender` of a per-layer dynamic vertex
    buffer (one draw per batch). Next impl: one v2 ManualObject + generated
    `DrawLayer2D/<tex>` HlmsUnlit datablock per batch in a dedicated UI
    queue, drawn by the window workspace's late pass through a pixel-space
    ortho camera whose `mSortMode = SortModeDepthRadiusIgnoring` makes the
    per-batch node depths the painter order. Conformance: the
    `render_facade_selfcheck` 2D pattern pixel-verifies z-order, in-layer
    order, alpha blending, scissor, texture binding, show/hide, RTT
    isolation and RAII teardown identically on both flavors.
  - **Gorilla.{h,cpp} DELETED** (recoverable from git). Its .ogui parser
    lives on as the backend-neutral `engine_gui/UiAtlas.{h,cpp}`
    (sprites, fonts/glyphs/kerning, whitepixel, markup colours; headless
    constructor for the unit tests) and its glyph layout math as
    `engine_gui/UiRenderer.{h,cpp}` (`UiScreen`/`UiLayer`/`UiRect`/
    `UiCaption`/`UiMarkupText`). The unused primitives (Polygon, LineList,
    QuadList, borders, gradients, per-corner colours) are gone.
  - **Perf contract (mobile ethos)**: one UiScreen = ONE DrawLayer2D batch
    (all layers/widgets of an atlas concatenate into one retained vertex
    vector, capacity kept across frames); elements relayout only when
    dirty; clean frames rebuild and upload NOTHING. The jumper-lua player
    selfcheck asserts the property: hiding all views drops the frame batch
    count by exactly the screen count (1), never the widget count (8+).
  - Widget API + Lua surface byte-compatible: Gui* classes unchanged
    (`getLayer()` now returns `UiLayer*`, the Lua `GuiLayer` usertype
    re-points to it with the same methods), `engine:hasUISystem()` is true
    on both flavors, module.cpp's gui exports register unconditionally,
    the .ogui format + `Util/make_gui_atlas.py` are unchanged.
- **Closure**: the containment lint landed earlier
  (`render_containment_lint` + `Util/ogre_containment.json`) — engine_gui
  is now a flavor-neutral sanctioned zone (math aliases + ConfigFile-family
  utilities present in both backends); the editor-backend decision
  revisited with real Next numbers, the math-swap readiness review, and
  mobile-backend evaluation input.

### Editor on both flavors + WYSIWYG colour parity

Owner decisions: the editor must build/run on BOTH backends (compile
option), backends must render THE SAME image (WYSIWYG), Ogre-Next becomes
the default-backend candidate. **The earlier decision that the editor stays a
classic-backend app is SUPERSEDED.** What landed:

- **ImGui on the facade** (`tools/editor/ImGuiFacadeRenderer.{h,cpp}`): the
  classic-only `Ogre::ImGuiOverlay`/OverlaySystem integration is GONE. ImGui
  draw data = textured triangles + scissor rects = exactly the DrawLayer2D
  contract, so the editor UI is now ONE facade 2D layer resubmitted per
  frame (per `ImDrawCmd`: registered texture + pixel scissor into
  `addTriangles`). The editor owns the ImGui context; `ImGuiSDL3Input`
  gained the `io.DeltaTime` bookkeeping `ImGuiOverlay::NewFrame` used to do.
  Facade API grown for it (design in the headers):
  - `RenderSystem::createTexture2D(name, rgbaPixels, w, h)` +
    `destroyTexture2D(name)` — raw-RGBA-under-a-resource-name uploads (the
    font atlas service DrawLayer2D.h anticipated); both backends resolve
    such backend-object textures BEFORE the resource system when binding 2D
    batches. Explicit destroy exists because manual textures are not
    resource-group content — Vulkan asserts on GPU memory outliving
    teardown (found by `editor_selfcheck_vulkan`).
  - `DrawLayer2D::addTriangles(optr<RenderTexture>, ...)` — the Scene
    panel's RTT binds by facade HANDLE, not name/id: the backend re-resolves
    the target's CURRENT texture per draw (classic: one binder material
    re-pointed per batch; next: per-target `DrawLayer2D/RTT/<name>` Unlit
    datablock re-pointed on build + detached by the dying incarnation), so
    resize-by-recreate can never dangle and the ImTextureID is STABLE.
    RenderTexture batches composite OPAQUE by contract (a target's alpha is
    a rendering byproduct - classic RTTs don't even have an alpha channel);
    on next the opaque blendblock carries
    `setForceTransparentRenderOrder(true)` so the batch stays in the
    back-to-front path the whole 2D painter order rides on.
  - `RenderSystem::showUIOnlyWindow()` — the editor-shell window mode: the
    main window composites ONLY 2D layers over the background colour, and
    `getWindowCamera()` answers NULL (CameraComponent now skips gracefully
    instead of asserting - editors legally load scenes carrying one).
    Classic = mask-0 viewport fed by an internal camera; next = a one-pass
    (clear + UI-queue) window workspace. Replaces the sanctioned
    "editor-ui-viewport" block.
  - `RenderWorld::createLineListMesh(name, points, colours, count)` — the
    editor grid is a facade line-list mesh now (classic: ManualObject
    OT_LINE_LIST -> convertToMesh; next: the cube-service v1->importV1
    recipe with OT_LINE_LIST — the operation type survives both
    conversions), instantiated like any mesh, query flags 0. Replaces the
    sanctioned "editor-grid" block. `RenderWorld::CUBE_MESH_NAME` is the
    flavor-neutral home of PrimitiveUtil's constant.
  - Fixed in passing (classic): per-texture 2D layer materials were CLONES
    of the master — cloning copies an already-RTSS-generated technique
    built for the SOURCE's (empty) texture-unit layout, so textured batches
    rendered flat white whenever an untextured batch had drawn first (the
    selfcheck's "textured batch" probe was too weak to catch white).
    Materials are now built from scratch and get their own RTSS technique.
- **Editor builds/runs on next**: root CMake gate opened; the boot block is
  per-flavor like tools/player (classic: Engine ctor + RTSS media +
  ORKIGE_RENDERSYSTEM env — the Vulkan/GL pick stays `#ifdef
  ORKIGE_RENDER_CLASSIC`, next boots Metal unconditionally); EngineNext
  gained classic's topLevelHandle-falls-back-to-externalHandle rule.
  Tests: `editor_selfcheck_next`, `editor_resize_next`,
  `editor_edittest_next`, `editor_play_stop/crash_next`,
  `editor_project_play_next`, `editor_play_script_error_next` all green in
  `desktop-next` (`ORKIGE_FLAVOR_TEST_SUFFIX` now covers editor + game
  registrations). Classic-gated: the native compile-on-Play tests (the
  module's persistent build tree configures against ONE engine tree),
  simulator/Android playtests (single device suite), Vulkan runs, exports.
- **Colour parity (the sRGB fix)**: root cause was colour management, not
  content: classic runs a fully gamma-space pipeline (non-sRGB swapchain,
  textures sampled raw), while Next defaulted to an sRGB swapchain
  (re-encoding on write = brighter) with sRGB-preferring texture loads.
  Fix at the source instead of per-path compensation: the Next backend is
  now gamma-space passthrough end to end — window created with
  `gamma=false`, `loadTexture2D` loads UNORM (no
  `PrefersLoadingFromFileAsSRGB`), RTTs are `PFG_RGBA8_UNORM`, and the
  DrawLayer2D vertex-colour pre-decode hack is DELETED (colours
  upload raw; blending now also matches classic exactly). Next screenshots
  additionally force alpha opaque (readback alpha is a byproduct; classic
  screenshots are opaque). Residual difference: PBS-vs-RTSS-Phong shading
  on LIT content only (mean diff 0.65/255, 0.73% outlier pixels on the
  selfcheck window shot; unlit/vertex-colour/textured/2D content is
  byte-identical, the RTT capture matches with mean 0.00).
- **Lit-content gamma on next (the crushed-PBS fix)**: the passthrough
  pipeline above left one hole — Ogre-Next's `HlmsPbs` unconditionally
  assumed an sRGB colour target (`hw_gamma_write` hardcoded to 1), so its
  LINEAR lighting result landed raw in the UNORM swapchain and every lit
  3D surface displayed gamma-crushed (a mid-grey slab under the noon sun
  read near-black; only emissive and the unlit 2D path looked right). The
  overlay port now derives `hw_gamma_write` from the live pass descriptor
  (`ports/ogre-next/pbs-honour-non-srgb-target.patch`, an upstream
  candidate), which engages the stock template's in-shader `sqrt` encode on
  UNORM targets; the port-shipped atmosphere sky shader carries the same
  encode. Unlit/2D stays byte-identical (HlmsUnlit is untouched — its raw
  passthrough IS the parity convention). Lit 3D content remains a
  TOLERANCE parity between the flavors (PBS + gamma-2 encode vs classic's
  gamma-space Cook-Torrance), guarded by the per-vignette benchmark probes
  (`run_benchmark_scene_probe.py`) and the facade exposure leg.
- **Point/spot lights on next (clustered forward)**: without a Forward+
  system Ogre-Next's HlmsPbs only shades point/spot lights that CAST
  SHADOWS — a plain dynamic lamp never lit anything. The backend now boots
  the scene manager with clustered forward light lists
  (`setForwardClustered`, 16x8x24 grid, 96 lights/cell, 2..100 units), so
  dynamic lamps render on both flavors (the lumens-vignette contract).
  Directional lights were unaffected (they ride the pass buffer).
- **The sun-exposure linkage on classic**: `AtmosphereDesc::sunPower` /
  `ambientPower` now act on BOTH flavors. The classic backend evaluates the
  SAME day/night colour model on the CPU (`core_util/AtmosphereSunDrive.h`,
  headless-unit-tested) and drives the linked sun's diffuse/specular plus an
  averaged-flat scene ambient, exposure-calibrated at the mid-grey reference
  (the sqrt linearisation — monotone agreement, not per-pixel parity).
  RESTORE-EXACTLY on both flavors: the atmosphere snapshots the linked
  light's authored colours when it takes it and writes them back when it
  lets go (disable, sun-set change, teardown); authored colour writes while
  driven land in the snapshot (the atmosphere owns the visible value). The
  facade selfcheck's restore leg proves the round-trip in pixels per flavor.
- **`render_backend_parity`** (next preset): runs the facade selfcheck on
  BOTH backends and pixel-compares `selfcheck_window/drawlayer2d/rtt.png`
  (stdlib-only PNG decode, `tests/integration_driver/
  compare_backend_screenshots.py`; mean ≤ 6/255, ≤ 2% pixels off by > 48).
  Cross-preset honestly: it runs the classic tree's selfcheck binary when
  present and SKIPs (exit 77) when that preset was never built. It also gates
  window PIXEL DENSITY: each selfcheck writes a `dimensions.txt` sidecar
  (logical points + drawable pixels) and the driver asserts both flavors report
  the SAME logical AND the SAME pixel size — a flavor that ignores the backing
  scale is caught directly, not resized away.
- **Window pixel-density policy**: engine-hosted windows are created with
  `SDL_WINDOW_HIGH_PIXEL_DENSITY`, so the render surface tracks the OS backing
  scale on every flavor. This is not a free choice: the Ogre-Next Metal window
  renders at the view's `screen.backingScaleFactor` with no override hook, so
  the only way both flavors can agree BY CONSTRUCTION (rather than by luck of
  the display config) is for classic to follow the same backing scale — which
  it does automatically once the window is high-density (960 points → 1920 px
  drawable on a 2× display, matching Metal). Everything downstream consumes
  window PIXELS consistently (`getWindowSize`, input point→pixel mapping,
  `DrawLayer2D` pixel space, gui `getContentScale`-driven UI scale,
  safe-area insets), and the editor derives its ImGui content scale from the
  drawable/points ratio, so all of it stays coherent at native density. Mobile
  is unaffected: the player's fullscreen window renders at native scale through
  the Metal/EAGL2 view's own `contentScaleFactor` and keeps SDL in points.
- **Cross-flavor Play targets**: the editor toolbar picker now offers
  "Desktop (classic OGRE)" and "Desktop (Ogre-Next)" — its own flavor's
  player is the TARGET_FILE, the other flavor's binary path is baked in
  from the conventional preset tree and the entry greys out (tooltip) while
  it is not built. The debug protocol is flavor-agnostic; native-module
  projects refuse the cross-flavor pick honestly (the module links against
  THIS editor's tree).

---

*Initial facade artifacts: `orkige_engine/engine_render/*.h` + `RenderFacadeCheck.cpp`
(compile check, PCH-exempt), `ORKIGE_RENDER_BACKEND` CMake option, this document.
`ctest --preset desktop` stays the gate.*

---

### Default backend: Ogre-Next becomes the DEFAULT backend

Owner decision: with the editor, games, UI and pixel parity proven on both
flavors, **Ogre-Next is the engine's default render backend** on desktop AND
mobile (iOS boots Metal, Android boots Vulkan — the unsuffixed
`ios-simulator-debug`/`android-debug` presets are next); classic stays the
fully supported **compatibility flavor** — kept because it OWNS what next
doesn't do yet: native game modules, BigZip, the Vulkan/GL runtime RS pick and
the jumper C++ sample. (The mobile GLES2 path is now the classic flavor's
`-classic` mobile presets; project export ships on both flavors.)

- **`ORKIGE_RENDER_BACKEND` defaults to `next`** in the root CMakeLists. Every
  preset still forces its backend EXPLICITLY (nothing relies on the default;
  the `-classic` presets set `classic` and omit the `render-next` manifest
  feature, so ogre-next never builds in a classic tree).
- **Preset/directory mapping** (muscle-memory commands get the default = next):

  | Old preset (flavor) | New preset (flavor) | Build dir |
  |---|---|---|
  | `macos-debug` (classic) | `macos-debug` (**next**) | `build/macos-debug` — old classic tree must be deleted |
  | `macos-debug-next` (next) | `macos-debug` (**next**) | `build/macos-debug` (fresh; `build/macos-debug-next` is orphaned) |
  | — | `macos-debug-classic` (classic) | `build/macos-debug-classic` |
  | `macos-release` (classic) | `macos-release` (**next**) | `build/macos-release` — old classic tree must be deleted |
  | — | `macos-release-classic` (classic) | `build/macos-release-classic` — **exports package from here** |
  | test `desktop-next` | test `desktop` (next, the full/default suite) | runs in `build/macos-debug` |
  | test `desktop` (classic) | test `desktop-classic` | runs in `build/macos-debug-classic` |
  | test `unit` (classic) | test `unit` (next) | runs in `build/macos-debug` |
  | test `all` (classic incl. device) | test `all` (classic incl. device) | runs in `build/macos-debug-classic` |
  | `ios-simulator-debug` (classic) | `ios-simulator-debug` (**next**, Metal) | `build/ios-simulator-debug` — old classic tree must be deleted |
  | `ios-simulator-debug-next` (next) | `ios-simulator-debug` (**next**) | `build/ios-simulator-debug` (fresh) |
  | — | `ios-simulator-debug-classic` (classic) | `build/ios-simulator-debug-classic` |
  | `android-debug` (classic) | `android-debug` (**next**, Vulkan) | `build/android-debug` — old classic tree must be deleted |
  | `android-debug-next` (next) | `android-debug` (**next**) | `build/android-debug` (fresh) |
  | — | `android-debug-classic` (classic) | `build/android-debug-classic` |

- **Cache guard**: build trees are flavor-bound (CMake cache, vcpkg manifest
  installs and objects encode one backend). The root CMakeLists refuses an
  in-place flip with a FATAL_ERROR ("delete the build dir or use the matching
  preset") via the internal `ORKIGE_RENDER_BACKEND_CONFIGURED` cache variable;
  legacy trees from before the guard are fingerprinted by their
  `OGRE_DIR`/`OGRE-Next_DIR` find_package cache entries.
- **Exports ship on both flavors**: `Util/orkige_export.py` reads the
  `--engine-build` tree's `ORKIGE_RENDER_BACKEND` and bundles the matching
  engine media — the classic RTSS library (Media/Main + RTShaderLib) or the
  Ogre-Next Hlms shader templates (Media/Hlms, registered via
  `Engine::setHlmsMediaDir`, wired through `PlayerBundle::resolveMediaDirectory`).
  Each flavor prefers its own Release sibling for the shippable player
  (`build/macos-release-classic` vs `build/macos-release`). Native game modules
  stay classic-pinned (`cmake/OrkigeGameModule.cmake`).
- **Editor Play targets**: the default Play target is the editor's OWN flavor
  (unchanged mechanics); the baked cross-flavor player paths follow the new
  conventional trees (`build/macos-debug` = next, `build/macos-debug-classic`
  = classic player).
- **.clangd inverted**: the default compilation database is
  `build/macos-debug` (next); the classic-only source set (backend dir,
  classic Engine/console/debug renderables, BigZip, Localisation,
  PrimitiveUtil/MeshUtil, jumper sample, classic-gated tests) is served from
  `build/macos-debug-classic`.

---

### Offscreen 2D composition (`DrawLayer2D` into RenderTextures)

The DrawLayer2D contract was originally main-window-only (composite the 2D
layers over the finished window frame). The editor GUI Preview tab and the
`preview_ui` MCP verb need a whole gui composited into an offscreen target at a
simulated device size — so the facade grew per-target 2D layer ownership:

- **Facade**: `RenderTexture::createLayer(zOrder)` makes a `DrawLayer2D` that
  composites into THAT target (instead of the window) at the target's own pixel
  size; the `RenderCaps::OffscreenOwnedLayers` capability
  (`RenderSystem::supports`) reports whether a backend can. The window path
  (`RenderSystem::createDrawLayer2D`) is unchanged and byte-identical for games —
  every existing selfcheck/parity output holds. The two surfaces are isolated
  (a window layer never leaks into a target and vice versa).
- **Ogre-Next** (`OffscreenOwnedLayers` = true): all 2D batches — window and every
  target — live in the ONE UI render queue; per-surface separation is by
  **visibility flag** (bit 0 = the window, bits 1..N handed out per target from
  `allocateUiVisibilityFlag`). A target that owns layers grows a UI pass in its
  compositor workspace (masked to its bit, drawn through a per-target pixel-space
  UI camera sized to the target); a UI-only target (no 3D camera - the preview
  case) is one clear + UI pass. `render_facade_selfcheck` pixel-verifies a
  gui-like pattern composited into an RTT plus the bidirectional isolation.
- **classic OGRE** (`OffscreenOwnedLayers` = false): REGISTERED next-only capability.
  The 2D compositor hook is a single `RenderQueueListener` gated on the
  main-window viewport, and its 2D materials are shader-only on GL3Plus — the
  RTSS transiently drops their generated technique whenever the dynamic light
  count changes or a scene teardown churns materials, so the composite carries
  a recompile-on-demand guard tied to that one viewport. Generalizing it to
  per-target offscreen surfaces (a new UI-only-RTT render path) is a next-only
  capability; classic reports honest no-support (`createLayer` returns NULL,
  logged once) and the editor disables the GUI Preview tab with a note. The
  facade surface stays backend-neutral; the render capability register records
  it as `RenderCaps::OffscreenOwnedLayers`.
- **filament** (future): a dedicated UI View on the target.

The gui stack renders into a preview target through
`GuiManager`'s optional **preview surface** (`GuiManager::PreviewSurface`): the
views' layers are created via `RenderTexture::createLayer`, and layout reads the
surface's simulated size + safe-area insets + content scale instead of the live
window (`UiScreen::setSurfaceSize` pins the layout size). The editor's shared
`GuiPreviewStage` owns the target + gui and is driven by both the tab and the
verb.
