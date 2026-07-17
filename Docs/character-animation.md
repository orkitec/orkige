# Character animation

Orkige animates characters two ways: **3D skeletal** meshes (skinned glTF with
bones and clips) and **2D vector/sprite** rigs (the flat-colour art path). This
guide states what each does today, on each render flavor, and where the honest
edges are. glTF is the 3D interchange standard; Lottie is the 2D vector-clip
standard (see `vector-animation.md`).

## 3D skeletal animation

A skinned mesh is imported through the ModelComponent mesh path; an
`AnimationComponent` on the same object plays the mesh's clips over the facade
`MeshInstance` animation surface. The component auto-plays the first clip
(looping) unless a script drives it. Playback advances every clip's time each
frame; a `crossFadeTo` blends two clips by weight.

### Capability by render flavor

Skinned glTF import runs on BOTH flavors (the next importer's skinned road
closed the former classic-only gap; see the matrix in
`render-abstraction.md`).

| Capability | Classic (compatibility flavor) | Next (default flavor) |
| --- | --- | --- |
| Skinned glTF import (bones + weights) | Yes - the Assimp codec builds an OGRE skeleton with bone assignments | Yes - the backend's assimp road forks on skinning: the neutral `SkinnedRig` extraction feeds a v1 skeleton + bone assignments, `importV1` carries it to the v2 mesh |
| Animation clips from glTF | Yes - one OGRE animation per glTF animation | Yes - one v1 animation per clip, converted to v2 `SkeletonAnimationDef`s (names exact in every build config via the backend's clip-name registry) |
| Clip playback (play/stop/loop/time/length) | Yes - `AnimationState` | Yes - v2 `SkeletonInstance`/`SkeletonAnimation` |
| Weighted crossfade / blend | Yes - `AnimationComponent::crossFadeTo` ramps outgoing/incoming weights | Yes - same component code path over the v2 per-animation weights |
| Skeleton-driven bounds (correct culling of a swinging limb) | Yes - `MeshInstance::setAnimatedBounds` (armed automatically when clips load) | Yes - derived in the facade: the armed instance rebuilds `getLocalBounds` from the live bone poses expanded by the import-time bone radius (Ogre-Next itself keeps the bind-pose Aabb) |
| Root-motion extraction | Yes - a classic-only backdoor (`handleMotion`/`motionBone`) reads the bone keyframe track directly | No - inert (no cross-backend facade bone API) |
| Bone/tag-point attachment (weapon-in-hand) | Not exposed through the facade | Not exposed |

### Two importer roads, one extraction (the drift alarm)

The engine deliberately keeps TWO importer paths: classic loads glTF through
the upstream render library's own assimp codec (reuse - never rewritten),
next owns its import end to end (`engine_render_next/MeshLoaderNext.cpp`).
To keep the skeleton/clip semantics from drifting between them, the NEW
logic is written once in a backend-neutral layer:
`engine_render/SkinnedRigExtract` turns the assimp scene into a pure
`SkinnedRig` (joints, keyframe tracks in seconds, per-vertex weights - no
renderer types), unit-tested against the generated mannequin's known
structure (`SkinnedRigExtractTests`), and the next backend only realises
that rig. If classic's codec ever diverges on animation semantics, classic
consumes the same extraction instead of growing a second interpretation.
The drift ALARM is `player_character_rig_selfcheck` on both flavors: the
same generated rig must produce comparable measured motion (walk bounds
spread, weighted crossfade, idle sway) on each importer road.

### The AnimationComponent surface

- `playAnimation(name, loop)` / `stopAnimation(name)` - enable/disable a clip.
- `crossFadeTo(name, durationSeconds)` - blend from the currently playing clip
  to `name`; both clips run together while the weight ramps (0.4 s reads well),
  then the outgoing clip is dropped. A non-positive duration switches instantly.
  `isCrossFading()` / `getCrossFadeProgress()` observe the blend.
- `setSpeed(speed)` - time scale for all playing clips.
- `getAvailableAnimations()` / `getDefaultAnimation()` / `setDefaultAnimation()`.
- Root motion: `setHandleMotion`/`setHandleRotation` + `setMotionBone` move the
  owning transform by the motion bone's track (classic-only, inert elsewhere).

All of the above are reflected, so they are reachable from Lua (`self.<obj>`)
and over MCP through the one property/function registry (see `lua-api.md`).

Runtime state (which clip, weights, time) does not serialize yet - a saved scene
restores the model and re-auto-plays the default clip.

### The generated test rig

`Util/make_character_rig.py` writes a deterministic, art-free **skinned
mannequin** `.glb` - the house pattern (the 3D counterpart of the terrain and
water generators). It carries a seven-joint skeleton (root/spine/head + two arms
+ two legs), per-vertex weights (summing to 1, with a real multi-joint blend on
the torso), and two clips: a 1 s looping `walk` (limbs swinging in opposition)
and a 2 s `idle` sway. `--selftest` validates the skinning structure headlessly
(`make_character_rig_selftest`).

The runtime proof is `player_character_rig_selfcheck` (both flavors,
`tests/projects/character/`): it plays `walk` and asserts the skeleton-driven
bounds spread (a swinging limb moves the skinned vertices), crossfades to `idle`
and asserts the blend transitioned, then asserts idle's sway keeps moving the
bounds. The thresholds prove MOTION, not magnitude - the two flavors' bounds
implementations report different absolute spreads for the same clip. On a
hypothetical flavor that imports glTF statically, the rig loads with no clips
and the check **skips honestly** with that finding as its message.

## 2D character animation

The 2D path is the flat-colour art direction (see `vector-animation.md`). Its
character-motion taxonomy - all renderer-neutral, identical on both flavors:

| Technique | What it animates | How | Status |
| --- | --- | --- | --- |
| Sprite flipbook | frame-by-frame texture swaps | `SpriteAnimationComponent` | Shipping |
| Vector cutout rig | a layer hierarchy of flat-colour shapes, keyframed transforms + opacity | `.oanim` (`VectorAnimationComponent`), Lottie-cooked | Shipping |
| Vector morph | same-topology path blends (whole-shape deformation) | `.oanim` shape keys / `.oshape` morph targets | Shipping |
| Soft body | smooth squash/stretch/wobble deformation | `VectorShapeComponent` softBody (control-point skinning) | Shipping |
| Textured cutout parts | TEXTURED sprite pieces driven by a cutout rig's transforms | - | **Gap** (see below) |

The vector cutout rig (`.oanim`) already gives a full parent-chain transform
hierarchy - the "bones" of a 2D cutout character - and blends between clips
(`crossFadeTo`). What it cannot do today is skin those transforms to **textured**
parts: a `.oanim` layer's shapes are flat-colour fills (solid or gradient), and
the facade `VectorMesh` it renders through carries position + colour per vertex,
no UVs and one shared untextured datablock. A textured-cutout character (photo/
painted limbs animated by the same rig) needs per-region UVs on the tessellated
vertices, a per-texture datablock split (the way `SpriteBatch` batches per
texture), and a `texture`/UV binding in the `.oanim` shape grammar. The transform
and blend machinery is already there - only the textured render path is missing.

### Weighted 2D mesh skinning - a deliberate non-goal

Weighted 2D mesh skinning (binding one continuous surface to a bone skeleton
with smooth per-vertex weight falloff, bones rotating the surface) is **not**
provided, and by the standing decisions it is largely unnecessary: the art
direction is flat-colour vector, where smooth deformation is carried by morphs
and soft-body, and jointed motion by the cutout transform hierarchy. Between
cutout rigs (rigid jointed parts), morph targets (arbitrary same-topology
blends) and soft-body (springy squash/stretch), the expressible motion covers
the intended style.

The one honest residual: smooth single-surface joint **bending** driven by a
single rotation parameter (a limb that bends at the elbow as one continuous
skin) is expressible today only by authoring morph targets for the bent poses,
not by a rotation-weighted skin. If a real content need for that appears, the
natural, minimal answer is rotation-weighted control points layered onto the
existing soft-body skinning - not a separate skeletal-mesh system.
