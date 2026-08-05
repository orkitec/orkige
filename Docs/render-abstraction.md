# Render backend (`engine_render`)

`engine_render` is the backend-neutral rendering interface. Engine code,
components, tools, the editor and the Lua surface talk to the renderer only
through it — never to `Ogre::` directly.

Two backends (**flavors**) implement it:

- **Ogre-Next** — the default. Boots Metal on macOS/iOS, Vulkan on Android.
- **classic OGRE 14** — the compatibility flavor. Boots GL3Plus or Vulkan on
  desktop, GLES2 on mobile and WebGL on web, chosen at runtime by the
  `ORKIGE_RENDERSYSTEM` env var. It owns two things Ogre-Next does not: the
  runtime GL/Vulkan render-system pick, and the `jumper` C++ sample.

Both flavors render the **same image** (WYSIWYG), gated by the
`render_backend_parity` pixel test.

A comparison needs both flavors, and a build tree carries one, so each parity
driver has two roads to the same verdict:

- **Both binaries in one run** — a machine holding both trees. This is the
  ctest (`render_backend_parity`, `grade_look_parity`,
  `benchmark_crossflavor_parity{,_mirror}`), registered on the next preset and
  skipping honestly (exit 77) when the sibling tree is unbuilt.
- **Captures compared later** (`--classic-shots`/`--next-shots`,
  `--capture`/`--compare-shots`) — each flavor writes its frames where it was
  built and the comparison happens elsewhere, off the images alone. This is how
  a per-flavor build matrix reaches the same verdict; the `render-parity` CI job
  compares the two Linux flavor jobs' captures. A capture that is missing,
  empty or unreadable FAILS: a parity gate that compared nothing must never
  report parity.

The measurements the second road needs travel with the screenshots — the
selfcheck writes `dimensions.txt` (the window density both flavors must agree
on) and `grade_metrics.txt` (the same metrics line it prints) beside them.

The **browser** is one seam further out and gets its own driver on the same two
roads: the wasm player renders the classic flavor, so `run_web_parity_test.py`
compares its frames against the DESKTOP CLASSIC player's frames of the same
scenes (the `web-parity` CI job). Same flavor on both sides, so a divergence
names the WebGL/GLES3 tier alone; desktop-next versus the browser is that gate
composed with the cross-flavor one — `Docs/web-export.md`.

**Reading a parity failure.** A mean and an outlier fraction say that two
frames differ, never where, so both pixel drivers add two things from
`tests/integration_driver/parity_diff.py`. Every run — green included — prints
the largest **8-connected region** of pixels differing by more than 48, with
its bounding box: ten thousand scattered one-level pixels and one badly
rendered object score the same mean, and only the second is usually a bug. A
run that FAILS also writes a **diff image** beside the compared frame
(`<shot>.diff.png`, or `--diff-dir` elsewhere) — the per-pixel delta on an
absolute heat ramp (blue a shade off, cyan at the outlier threshold, red
inverted or missing) over a dimmed grayscale of the classic frame, so the
picture shows which object moved. The captures directory is what the CI job
uploads, so the diff rides out with the failure. The region size is
**reported, not gated**: its healthy value on the CI rasterizer pair has never
been measured, and a threshold invented without a measurement blocks merges
instead of catching bugs — the green logs are what a corridor would be
measured from.

The flavor is fixed at **build time** — classic OGRE and Ogre-Next export the
same `Ogre::` symbols, so one binary links exactly one backend; there is no
runtime switch. Build trees are flavor-bound: reconfiguring a tree with the
other backend is a hard error (delete the tree, or use the matching preset).
Presets carry the flavor — see the preset list in the top-level `CLAUDE.md`
(`macos-debug`/`-classic`, `android-debug`/`-classic`, and so on).

## The deviceless run

`ORKIGE_RENDERSYSTEM=NULL` (or `headless`) boots the Ogre-Next flavor with **no
window and no GPU**: SDL's video subsystem is never initialised, no OS window is
created, and the render system is Ogre-Next's deviceless one. Everything above
the facade behaves normally — scenes load, transforms compose through the render
node graph, scripts tick, physics runs, frames advance and the process exits
cleanly — so a runtime can hold a live world on a machine with no display
server. `engine_render/RenderSystemSelection.h` is the one place the word is
parsed; `player_deviceless_next` is the gate, and it is the one player
registration in the `unit` label because a display is not among its
prerequisites.

Nothing is drawn, and the run is honest about it: the low-level shader tier
(`.material`/`.program` media — sky, bloom, output grade) is not registered,
because a deviceless render system has no GPU program manager to parse it into.
Anything that reads pixels back is meaningless here, so pixel and parity gates
stay on a real render system. The classic flavor carries no deviceless render
system and refuses the request rather than opening a window nobody asked for.

This is a **runtime** capability, and the editor is not one of its consumers:
its scene view, its preview and its whole interface are render targets, so a
windowed editor launch under that variable refuses by name instead of booting
([editor-cli.md](editor-cli.md#platform-notes)). Editor subcommands install no
render system at all and are unaffected.

## The containment rule

Code above the backend never names `Ogre::`. The only zones allowed to are
`engine_graphic/`, `engine_render_classic/`, `engine_render_next/`, and the math
aliases in `engine_render/RenderMath.h`. `render_containment_lint` enforces this;
sanctioned exceptions live in `Util/ogre_containment.json`.

Do not rely on features Ogre-Next dropped — OGRE material scripts especially.
Materials are simple and generated (see [Materials](#materials)).

## What runs on each flavor

Everything runs on both flavors: the `engine_render` facade, game
objects/components/serialization, Lua scripting, input, sound, physics, the gui
(widgets on `DrawLayer2D`), the editor (ImGui on `DrawLayer2D`), project export,
and mobile (iOS/Android). Skinned glTF characters, the static-mobility fast path
and sprite-run batching work on both.

The deltas are the render [capabilities](#capabilities) below, plus:

- **classic-only**: the runtime GL/Vulkan render-system pick; the `jumper` C++
  sample.
- **next-only**: same-mesh auto-instancing; offscreen 2D layers (see
  [Offscreen 2D](#offscreen-2d)).

## Facade classes

The interface headers live in `orkige_engine/engine_render/`. Each is
`Ogre::`-free and compiles stand-alone (checked by `RenderFacadeCheck.cpp`).

| Class (header) | Purpose |
|---|---|
| `RenderSystem` | frame loop; main window (camera/background/resize/size); screenshots; `FrameStats`; resource locations + pak mounting (`mountPak`); `createRenderTexture`; `createMaterial`/`createWaterMaterial`; `createTexture2D`; `getWorld` |
| `RenderWorld` | root node; node + content factories; ambient (`setAmbientHemisphere`); shadows (`setShadowQuality`); atmosphere (`setAtmosphere`); IBL / bloom / grade / planar-reflection toggles; `queryRay` AABB picking; the generated-mesh services (line-list, cube, `createMeshFromData`, `ensureMeshAsset`, `destroyGeneratedMesh`) |
| `RenderNode` | local + world transform; translate/rotate/lookAt; children + re-parenting; visibility; static mobility (`setStatic`); world bounds; user-pointer back-mapping |
| `MeshInstance` | load/attach/visible/shadows/bounds/query flags; `setMaterial`; vertex-colour-unlit fixup; `AnimationState` control (names/enable/loop/time/crossfade); per-instance tint/emissive accents |
| `SpriteQuad` | a textured quad: texture, size, UV rect, tint, flips, `zOrder`, visibility |
| `VectorMesh` | vector-shape triangle content — flat fill + textured cutout sections, on the sprite painter window |
| `LineMesh` | 3D line content — strip or segment list, depth-tested or on-top, with a same-count dynamic update fast path |
| `RenderCamera` | perspective/ortho, FOV, aspect, near/far clips, viewport ray, project point, view/projection matrices, wireframe toggle |
| `RenderLight` | directional/point/spot: type, colour, range, spot angles, casts-shadows |
| `RenderTexture` | render-to-texture: camera, background, overlays/shadows toggle, resize-by-recreate, native texture id (for ImGui), `writeContentsToFile`, `createLayer` |
| `RenderMaterialDesc` (`RenderMaterial.h`) | the metal-rough PBS authoring surface consumed by `createMaterial` (usually parsed from a `.omat`) |
| `RenderMath.h` | the math vocabulary and the [swap point](#math) |

## Handles

Facade objects are `optr` (`shared_ptr`) handles — `optr<RenderNode>`,
`optr<MeshInstance>`, and so on. Destroying the handle detaches and destroys the
underlying object, so component teardown is automatic. `woptr` (`weak_ptr`) is
for non-owning observers such as editor selection. Object *ids* stay at the
GameObject layer, above components.

The classes are concrete (no virtual dispatch): each is a pimpl
(`struct Impl; Impl* mImpl;`) whose methods one backend's translation units
define. Swapping backends compiles a different impl directory behind the same
headers.

## Math

`RenderMath.h` is the engine math vocabulary — `Orkige::Vec3`, `Quat`, `Color`,
`Degree`, `Ray3`, `AABB`, and friends — currently typedefs of the Ogre math
types, which both Ogre flavors share by name, layout and semantics. It is the
single swap point for engine-owned math if a third backend ever needs it. The
facade headers name zero Ogre types but do transitively include Ogre's *math*
headers (math only — no scene or render types).

## Materials

The facade carries **generated** materials only — no material-script system, no
node graphs:

- `RenderSystem::createMaterial(RenderMaterialDesc)` — a metal-rough PBS
  material, usually parsed from a `.omat` asset. HlmsPbs datablock on next, RTSS
  Cook-Torrance material on classic.
- `RenderSystem::createWaterMaterial(RenderWaterDesc)` — the animated water
  plane.
- Sprites and the vertex-colour-unlit fixup generate a handful of Unlit
  datablocks.

Full material, water, shadow, atmosphere, IBL, bloom and grade reference:
`Docs/materials.md`.

## Generated meshes

A mesh RESOURCE can come from CPU vertex data, not only from a loaded file.
`RenderWorld::createMeshFromData(name, MeshBuilder::Mesh)` registers positions,
normals, one UV set, tangents and per-material SECTIONS under a name; after that
the mesh is an ordinary mesh, so `createMeshInstance`, PBS `.omat` materials,
shadows, the `static` mobility flag, visibility flags and native instancing all
work with no procedural special case above the facade. Each section becomes one
sub-mesh, exactly like a multi-material import.

- **next**: one `v1::ManualObject` section per `MeshBuilder` section ->
  `convertToMesh` -> `buildTangentVectors` -> `Mesh::importV1`, with the sub-mesh
  material names written AFTER the import (a v1 `ManualObject` refuses an HLMS
  datablock name, so sections begin on a placeholder low-level material - the
  same dance the assimp and cube roads use).
- **classic**: one `Ogre::ManualObject` section per section -> `convertToMesh` ->
  `buildTangentVectors`.

Tangents are generated the way the loaded-mesh road generates them, deliberately:
the Hlms refuses a normal-mapping material on a tangent-less mesh, so a generated
mesh has to be able to take the same `.omat` an imported one takes.

The services are idempotent per name and are retired through
`destroyGeneratedMesh` (which also drops next's `"<name>/v1import"` intermediate)
— a name that regenerates must be dropped first, with no live instance on it.

`RenderWorld::ensureMeshAsset` sits on top: it turns a `.omesh` text asset into
such a resource on first use and both backends call it from
`createMeshInstance`. Its body is flavor-NEUTRAL and therefore lives with the
facade in `engine_render/MeshAssetLoad.cpp` rather than twice in the backend
directories — two copies would be two chances for the flavors to disagree about
what a `.omesh` means. Asset reference: `Docs/meshes.md`.

The cross-flavor gate is the `selfcheck_omesh.png` capture in
`render_backend_parity`: the generated surface renders EMISSIVE-only with every
light suppressed, so the compared image is a silhouette/coverage picture of the
geometry and neither shading model can contribute a delta.

## 2D painter order

Sprites, vector meshes and sprite batches paint by `zOrder`, identically on both
flavors. next maps `zOrder` → render-queue id `50+z` (the queue id is the paint
order). classic puts all 2D content in one render queue and maps `zOrder` →
render priority, because OGRE sorts alpha-blended, depth-write-off renderables by
camera distance and does not honour queue-group order across them — a single
queue plus priority is the only ordering it respects. `render_backend_parity`
and the `flatland` benchmark probe guard it.

## Capabilities

Rendering features that differ by flavor are `RenderCaps` flags. Probe them:

- **C++**: `RenderSystem::get()->supports(RenderCaps::X)`
- **Lua**: `engine:supports("name")`
- **MCP**: the `capabilities` object in `get_state`

A facade contract maps to each backend's **native fast path**. A feature a
backend would have to emulate goes behind a `RenderCaps` flag instead — neither
flavor is silently pessimized to serve the other. The per-flavor
`benchmark_budget` gate catches a facade change that costs draw calls on either
backend.

The register is defined once in `orkige_engine/engine_render/RenderCaps.h`; the
matrix below is generated from it and CI-gated (it cannot drift from the code).

<!-- GENERATED:render-caps-matrix - edit Util/update_docs.py / lua_api_annotations.json; do not hand-edit -->
| Capability (`RenderCaps` name) | classic | next | What it is |
| --- | :---: | :---: | --- |
| `skyDome` | yes | yes | a horizon-to-zenith sky dome behind the scene (sun-linked atmospheric on next; classic evaluates the same shared sky model per-pixel on GL3Plus/GLSL-ES3 targets with a vertex-colour gradient fallback on the GLES2/WebGL1/Vulkan floor) vs a flat clear colour; the dome is the `procedural` sky type - `AtmosphereDesc::skyType` also selects a cubemap `skybox` or a flat `colour` sky on both flavors |
| `dynamicShadows` | yes | yes | dynamic shadow maps cast by shadow-casting directional lights (next = compositor PSSM + PCF; classic = RTSS integrated PSSM folded into the one generated-material scheme - on GLES2/WebGL the bit is runtime-gated on depth-texture render targets) |
| `hemisphereAmbient` | yes | yes | a two-colour sky/ground ambient term evaluated PER PIXEL against the surface normal (mix(lower, upper, dot(hemisphereDir, N) * 0.5 + 0.5)); both flavors fill a surface's ambient from the same sky/ground split - next natively (HlmsPbs ambient hemisphere), classic through a generated-shader hemisphere-ambient sub-render-state on the lit surface materials (tolerance parity: the reflectance is recovered to display space to match next's gamma-space ambient consumption, and classic's fixed-function fallback + imported-mesh materials still see one flat average) |
| `sunExposureLinkage` | yes | yes | the atmosphere drives the linked sun's colour/power (an exposure the un-tonemapped pipeline can clip) - native on next, the same day/night curve evaluated on the CPU on classic (colour + per-pixel hemisphere ambient fill on the generated surface materials, tolerance parity) |
| `animatedNormalMappedWater` | no | yes | fully animated normal-mapped water ripples; classic lights OR scrolls one normal map on a unit, not both, so its lit relief is static |
| `offscreenOwnedLayers` | no | yes | 2D layers composited into an offscreen RenderTexture (the editor Preview panel .oui overlay + preview_ui), not just the main window |
| `projectedDecals` | no | yes | surface marks (impact/splat/footprint + blob-shadow fallback) as TRUE projected decals wrapping over geometry (next = HlmsPbs forward-clustered Decal) vs a surface-aligned textured quad floating above the surface (classic - flat, does not wrap uneven geometry) |
| `bloom` | yes | yes | an LDR highlight-glow post-process on the 3D scene only (bright-pass -> separable blur -> additive combine, per-scene opt-in via engine:setBloom, the r.bloomQuality tier) - the 2D tier (sprites/vector shapes/gui) is excluded so UI stays crisp. next = CompositorManager2 quad passes inserted between the 3D scene pass and the 2D/UI pass; classic = the same chain as a viewport compositor over the generated-material scheme, the `OgreUnifiedShader.h` bright/blur/combine quad passes authored once and run in the GLSL ES 3.0 profile on a GLES/WebGL context - so it reaches the WebGL2/GLES3 web+device path too, gated on the glsl300es probe like the IBL stage; the GLES2/WebGL1 floor answers false and an enabled bloom degrades to no pass with one log line |
| `screenSpaceRefraction` | yes | yes | opt-in screen-space refraction distortion through the water surface: the opaque scene colour is captured before the water draws and sampled at a normal-perturbed screen UV so what is under the water bends/wobbles (basic distortion, NOT depth-graded transmission). next = the HlmsPbs Refractive transparency mode fed by a compositor scene-colour+depth pass; classic = a grab-pass RenderTexture of the scene (water hidden) sampled at the perturbed screen UV, authored in two GLSL variants (desktop GL core + GLSL ES 3.0), so it reaches the WebGL2/GLES3 web+device path too - gated on the glsl300es probe like the IBL stage; the GLES2/WebGL1 floor keeps the byte-stable Stage-1 look, a Vulkan/Metal context answers false pending its own variant |
| `iblReflections` | yes | yes | opt-in image-based lighting sourced from the scene's SKY: specular reflections + a diffuse fill ADDED to the analytic lights on PBS-lit facade materials (next = the HlmsPbs reflection map + diffuse-GI env feature; classic = the engine-owned image-lighting sub-render-state over the same cubemap, evaluating next's live env term verbatim at the ONE shared intensity scale `IblPreset::fillScale` - on a GLES context the bit is runtime-gated on GLSL ES 3.0), tiered by the `r.iblQuality` cvar (`core_util/IblPreset.h`). ONE path, TWO sources selected automatically: a skybox atmosphere feeds the offline-baked prefiltered chain (`Util/make_sky_assets.py`); a procedural atmosphere feeds a runtime CPU capture of the sky (`core_util/SkyEnvMap` - a small cubemap synthesized from the atmosphere + sun with a box-downsampled roughness chain, recaptured on-demand only when the sun swings past ~6 degrees or the sky colours change, never per frame; a tolerance-parity approximation of the AtmosphereNpr dome on next, exact for the classic gradient sky). Colour / disabled skies have no environment and refuse honestly (`Docs/materials.md`) |
| `outputGrade` | yes | yes | the shared output LOOK/GRADE stage: an authored contrast (S-curve) + saturation transform applied IDENTICALLY on both flavors (the shared curve is core_util/GradeMath) to the 3D scene only - the 2D tier (sprites/vector shapes/gui) is excluded so UI stays crisp, and the pass sequences LAST (after bloom when both are on). next = a CompositorManager2 grade quad after the scene pass; classic = the same curve as a viewport compositor over the generated-material scheme, the `OgreUnifiedShader.h` grade quad authored once and run in the GLSL ES 3.0 profile on a GLES/WebGL context (so it reaches the WebGL2/GLES3 web+device path too, gated on the glsl300es probe like bloom/IBL); the GLES2/WebGL1 floor answers false and an enabled grade degrades to no pass with one honest log line. DEFAULT OFF and byte-stable - content that never opts in renders byte-identically |
| `planarReflection` | yes | yes | opt-in MIRROR reflection of the actual scene (sky + terrain + objects) in the water surface, rather than just the sky IBL cubemap it already samples: a camera reflected across the surface plane (normal +Y at `planeHeightY`) renders the scene into a reflection RenderTexture with the water surface hidden, which the water material samples at a FRESNEL-modulated blend (`reflectionStrength` boosts the base reflectivity and dims the body colour moderately - the surface stays water, never chrome). classic = the working path: a hand-authored GLSL program (desktop GL core + GLSL ES 3.0 variants, byte-identical bodies so web and desktop classic ripple alike) samples the mirror-camera RenderTexture at the fragment's ripple-perturbed screen UV (`screenUv + swellNormal.xz + disp`, so the mirror WOBBLES with the waves) with a Schlick fresnel + the sun's specular streak - reaching the WebGL2/GLES3 web+device path on the glsl300es probe; the GLES2/WebGL1 floor and a Vulkan/Metal context answer false pending their variant. next = the native planar-reflections subsystem: the hand-built reflection workspace renders the mirror RTT WITH its mip chain, and the mirror-on water material compensates HlmsPbs's linear-light env composition (a probe-calibrated mirror specular, near-mip-0 roughness, the squared classic body-dim law on albedo + scatter) so the mirrored scene measures at the classic paint's strength. Ogre's native planar reflection is a FLAT screen-projected mirror (the ripple normal only gates the plane distance/facing), so `createOrUpdateWaterDatablock` overrides the upstream `DoPlanarReflectionsPS` Hlms piece for water datablocks (a custom PS piece, parsed after the library so it wins - the swell VS piece's precedent) to ride the wave normal's slope and widen the harsh 20-degree tilt gate that would blank the mirror across crests - without it next's mirror reads glassy while classic ripples. the perturbation amplitude is calibrated so the reflected-OBJECT wobble measures at the classic mirror's strength (probe-equal on Mirror Lake's cube reflections). the mirror's sample sharpness is a LOD BAKED into that piece rather than read from the datablock's roughness, so the shared water roughness keeps shaping the sun glint's GGX lobe on both vignettes - the streak renders with and without the mirror, and the mirrorlake parity run gates its existence on both flavors. The residual look gap - classic's water body is an unlit painted tint while next's is a lit Lambert surface plus the hemisphere sky fill, so next reads brighter and more uniform - is a shading-model difference and a named open parity item. Probe-verified on both flavors by water_reflection_looks_right (strength) + water_mirror_wobble (the wave-perturbs-the-mirror EXISTENCE gate: it captures the same frame with and without the ORKIGE_WATER_FLAT_MIRROR diagnostic seam - which zeroes only the ripple UV perturbation - and asserts the mirror MOVES, a wall-clock-pacing-independent check; also the mirror diagnose seam ORKIGE_DUMP_MIRROR=<png>). Composes with screen-space refraction; off/unsupported keeps the byte-stable sky-reflection look |
| `sceneWireframeView` | yes | yes | the editor Scene view's WIREFRAME polygon mode: the Scene view renders line-fill while every other target (Preview, camera inset, Play) stays solid, and the 2D/UI tier (the editor's own ImGui chrome, gui, sprites, vector shapes) stays solid too. Available on BOTH flavors, by DIFFERENT roads. classic = PER TARGET: the Scene RTT's OWN viewport camera flips to wireframe polygon mode (Camera::setPolygonMode, per-camera + leak-free, @see RenderTexture::setViewMode). next = a GLOBAL state under the one-game-view render invariant (@see RenderWorld::setSceneWireframe), because Ogre-Next bakes polygon mode into the pipeline state object with no per-pass/per-target override: the backend keeps the generated datablocks split into a 3D-SCENE set (PBS mesh/material/water) and a 2D/UI set (sprites/vector/lines/DrawLayer2D), and wireframe flips ONLY the scene set's macroblock polygon mode (byte-exact restore, PSO variant caches), armed by the editor only on a frame where the Scene view is the ONE game view rendering - the same dock-tab discipline SceneUnlitView uses, so it never leaks into the Preview / Play and composes with lighting-off (flat unlit wireframe). The solid+wireframe overlay (ShadedWireframe) is a SEPARATE capability that adds overlay items on top of the shaded pass rather than flipping polygon mode - @see SceneWireframeOverlayView |
| `sceneWireframeOverlayView` | yes | yes | the editor Scene view's SHADED+WIREFRAME inspection look: the solid shaded scene with a thin wireframe drawn ON TOP of it (line edges over the lit surfaces). Available on BOTH flavors by the SAME road - OVERLAY ITEMS, never a mid-frame polygon-mode flip (impossible to bracket per-target on next's baked PSO, unbuilt as a second pass on classic). While armed (@see RenderWorld::setSceneWireframeOverlay), every scene-tier mesh instance gets a SECOND renderable sharing its mesh + node, drawn with ONE shared unlit near-black wireframe datablock/material whose macroblock carries polygon-mode wireframe + a small depth bias so the lines sit on the shaded surface without z-fighting; the shaded pass renders untouched, the overlay items add the lines. next = a second render Item per source item with the shared unlit wireframe datablock; classic = a second render Entity per source with a shared wireframe material. The overlays inherit the source node/transform, carry the editor-only visibility bit (masked OFF the Preview target, so they never leak into preview/Play), cast no shadows and are never pickable (query flags 0); the set rebuilds when the scene item set changes (live create/delete/mesh-swap) and disarm destroys it. ANIMATED/SKINNED meshes are excluded in this version (static scene geometry is the mode's habitat) - a skinned overlay would need its own animation state; soft-body/vector 2D content is scene-tier-excluded by construction. Radio-exclusive with the wireframe FLIP (SceneWireframeView) - only one is armed at a time, and both restore byte-exact |
| `sceneUnlitView` | yes | yes | the editor Scene view's flat LIGHTING-OFF inspection look (albedo + a bright flat ambient, every analytic light's contribution removed). Available on BOTH backends as a GLOBAL per-frame state (@see RenderWorld::setLightingSuppressed), NOT per-target: a per-target route is impossible - Ogre-Next gathers directional lights from the scene's global light list, unfiltered by the per-pass light-visibility mask (OgreHlms.cpp 'Always gather directional & area lights'), and classic has no per-viewport lighting override at all. The editor sidesteps that by exploiting the DOCK-TAB layout: the Scene view and Preview are rarely visible together, so lighting-off is armed ONLY on a frame where the Scene view is the visible tab AND the Preview is not - a Scene-only frame, so a global flat look is correct (when both are visible the real game look wins and the Scene view stays lit). next = snapshot + hide every scene light + a flat-white hemisphere ambient; classic = the same over the scene lights + the per-pixel hemisphere-ambient sub-render-state and the flat term. RECOVER-THEN-REAPPLY (the ScreenShake precedent): releasing restores the ambient + every light's visibility EXACTLY, and the state flips on tab switches (not per frame) so the zero-light shader variant caches |

_A capability marked `no`/`no` is a `PlannedAbsent` v1 boundary (absent on both flavors, next-first when it lands); the rest are real classic/next deltas. Probe from code with `RenderSystem::get()->supports(RenderCaps::X)`, from Lua with `engine:supports("name")`, and over MCP from `get_state`'s `capabilities` object._
<!-- /GENERATED:render-caps-matrix -->

Not every capability is boolean. The **light budget**
(`RenderSystem::lightBudget()` / `engine:getLightBudget()`) is the flavor's
ceiling on concurrent dynamic point/spot lights — classic 30 (RTSS forward
headroom), next 96 (the clustered-forward per-cluster bound). Consumers that ramp
live lights cap at this instead of an authored constant.

## Offscreen 2D

The editor renders through the facade on both flavors: ImGui is one
`DrawLayer2D` layer resubmitted per frame, and the Scene panel binds its
render-to-texture by facade handle (so resize-by-recreate never dangles).

`DrawLayer2D` composites screen-space 2D — pixel-space triangle batches with
per-batch texture binding and analytic scissor clipping. On the main window it
works on both flavors. Compositing 2D into an offscreen `RenderTexture`
(`RenderTexture::createLayer`) is next-only (`RenderCaps::OffscreenOwnedLayers`):
the editor's Preview-panel `.oui` overlay and the `preview_ui` MCP verb need it;
on classic `createLayer` returns null and that leg is refused with a note (the
scene render itself is unaffected).
