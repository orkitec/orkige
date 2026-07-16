# Vector animation (`.oanim`)

Keyframed animation for vector shapes: a layer rig whose
transforms, opacities and path poses are keyframed over one frame timeline,
carved into named clips (idle/walk/run/...), evaluated by the pure
`core_util/VectorAnimEval` and blended clip-to-clip at the pose level. The
`.oanim` text asset is the animated sibling of `.oshape` — same fill/contour
vocabulary, same agent-authorable plain text.

## The `.oanim` format (v2)

One token stream, `#` starts a line comment, indentation is cosmetic.
Coordinates are shape-local units, +y up (the `.oshape` space); colours are
straight RGBA 0..1. The authoritative grammar lives in
`orkige_core/core_util/VectorAnimAsset.h`; summary:

```
version 2
fps 30                          # frames per second (> 0), required
duration 60                     # timeline length in frames (> 0), required
clip idle 0 30 loop             # name start end loop|once — header, unique names;
clip walk 30 60 once            #   no clip lines = one implicit `default` loop clip
layer root parent -1            # paint order = file order; parent = index of an
                                #   EARLIER layer or -1 (acyclic by construction);
                                #   no shape blocks = a pure parent (null layer)
  pos k 2                       # channels: pos/anchor (x y), scale (sx sy, 1 = 100%),
    kf 0  0 0  lin              #   rot (degrees CCW), opacity (0..1); each at most
    kf 30 10 0 ease 0.42 0 0.58 1   # once; absent = default (pos/anchor 0, scale 1,
  rot k 1                       #   rot 0, opacity 1)
    kf 0 0                      # kf FRAME VALUES [lin|hold|ease ox oy ix iy]
layer body parent 0             #   frames strictly increasing, within 0..duration;
  shape k 2                     #   easing applies TO THE NEXT key (default lin)
    kf 0 lin                    # shape keys: one full region pose per key in the
      fill 0.9 0.4 0.35 1       #   .oshape vocabulary (fill/contour/v/hole);
      contour 4                 #   EVERY key repeats the first key's topology —
      v -1 -1                   #   fixed vertex counts make path animation a pure
      v  1 -1                   #   point-for-point lerp (the morph discipline);
      v  1  1                   #   a mismatch is a parse error
      v -1  1
    kf 30
      ...
layer ink parent 0              # a STROKE region (v2): the contour is a CENTRELINE
  shape k 1                     #   the renderer sweeps, not a filled boundary
    kf 0
      fill 0.1 0.1 0.1 1        # stroke W CAP JOIN LIMIT ENDS
      stroke 0.08 round round 4 open   #   CAP butt|round|square, JOIN miter|round|bevel,
      contour 3                 #   LIMIT = miter ceiling in half widths,
      v -1 0                    #   ENDS open|closed (a closed centreline has no caps)
      v  0 1                    # W animates across keys; the style does not.
      v  1 0                    # A stroke takes no `hole`; 2 points is a legal ribbon.
      mask 4                    # optional CONVEX clip polygon (a cooked layer mask)
      v -2 -2
      v  2 -2
      v  2  2
      v -2  2
```

Easing: `lin` is linear, `hold` is constant until the next key, `ease` is a
cubic value bezier through (0,0), (ox,oy), (ix,iy), (1,1) — x is the time
fraction (clamped to 0..1 so time stays monotone), y the value fraction (free,
for overshoot). This is the whole runtime interpolator vocabulary by design:
anything richer in an imported source is densified into extra keys at cook.

v2 only ADDED `stroke`/`mask`, so every v1 file is a valid v2 file.

Malformation honesty: `VectorAnimAsset::parse` returns false and an EMPTY
document on any malformation (missing header, bad clip ranges, forward/self
parents, truncated key or vertex runs, topology mismatches) — never a
half-loaded rig. Unknown keywords between complete elements are reserved and
ignored. An optional `ParseError` out-param reads back the offending 1-based
line and the reason (filled only on failure — the success path carries no
reporting cost); the component load, the editor's preview and the
`preview_animation` verb all surface it, so a hand-authored rig's typo names
its line instead of a bare "malformed".

## Strokes

A stroke is NOT expanded into a filled outline. The asset carries the
CENTRELINE plus a width/cap/join/limit (a `stroke` region), and
`VectorTessellator::appendStroke` sweeps it into geometry at build time:

- a **quad** per segment (the ribbon between the two offset edges),
- a **wedge** per interior corner on the OUTER side of the turn (a round fan, a
  miter limited to `LIMIT` half widths, or a bevel triangle) — the inner side
  needs nothing, the two segment quads already overlap there,
- a **cap** per open end (butt = nothing, square = a quad, round = a half disc).

Every piece is CONVEX and independently valid, so no triangulator is involved.
That is the whole point: an offset outline self-intersects wherever the path
curves tighter than the half width, doubles back, or has a near-degenerate
segment, and a triangulator (which requires a SIMPLE polygon) turns such an
outline into stray spikes, filaments and streaks. Overlapping convex pieces
cannot. Both render flavors share this one pure implementation, so they stay
pixel-identical by construction.

The soft edge (the engine forces FSAA 0, so edge AA is baked geometry) walks the
ribbon's two offset boundaries — joins and caps included — as an alpha ramp,
clamped to the stroke's own width so a hairline is not swallowed by a halo.
Where a rim quad crosses the ribbon's own overlap it paints the stroke's colour
over the stroke's colour, which is invisible.

HONEST LIMIT: a TRANSLUCENT stroke double-blends where pieces overlap (a sharp
corner's join wedge over the segment quads, and the rim over the ribbon), so its
corners read slightly darker. An opaque stroke — the common case, and what
flat-colour vector art uses — is exact. Removing that would need either a
stencil/depth pre-pass or a stroke-to-fill boolean union; neither is worth its
cost here.

Layer masks over strokes ride along: the cooked convex mask polygon travels with
the region (`mask`) and every piece (fill and rim) is clipped convex-against-
convex before it is emitted — again no triangulator.

## The evaluator (`core_util/VectorAnimEval`)

Pure, headless, allocation-free per tick (buffers sized once at `build`, the
`SoftBodyDeform` architecture). A clip maps seconds onto the frame timeline
(loop wraps, `once` clamps); at a frame every channel and shape block is
sampled into a **Pose** — per-layer LOCAL transforms + opacity, plus every
shape's region in its layer's own space.

- `evaluateAt(clip, seconds, pose)` — stateless scrub/preview/thumbnail entry.
- `blendPose(a, b, weight, out)` — pose-level lerp BEFORE parent composition:
  local transforms component-wise (2D rotation is a scalar — singularity-free),
  path vertices point-wise, fills/opacities per channel. Structure mismatch is
  refused (poses of different rigs never blend).
- `composeRegions(pose, out)` / `writeRegions(out)` — compose the parent chain
  (`local(v) = pos + R(rot) · (scale · (v − anchor))`, opacity multiplied down
  the chain into fill alpha) and emit a `VectorTessellator::Region` list in
  paint order with CONSTANT topology across frames — a consumer tessellates
  once and then only moves vertices. A stroke region's WIDTH rides the same
  chain (scaled by the world affine's area factor, the honest scalar under a
  non-uniform scale), as does its mask.
- `setClip` / `crossFadeTo(clip, seconds)` / `update(dt)` — playback with
  crossfades: BOTH clips keep advancing while a smoothstepped weight ramps
  0 → 1, then the outgoing clip is dropped (steady-state cost = one clip).
  Blending is same-rig only (clips of one `.oanim`).

Unit coverage: `tests/core/VectorAnimAssetTests.cpp` (grammar, malformation
honesty, fixed-topology enforcement, clip lookup) and
`tests/core/VectorAnimEvalTests.cpp` (interpolation incl. bezier easing,
parent/anchor/scale/opacity composition, clip playback, pose lerp, crossfade
ramp, statelessness, allocation stability).

## Importing Lottie documents (`Util/cook_vector_anim.py`)

Lottie JSON (the open, Linux-Foundation-standardized vector animation
interchange format) is the import on-ramp for `.oanim`:

```sh
python3 Util/cook_vector_anim.py character.json            # character.oanim beside it
python3 Util/cook_vector_anim.py character.json out.oanim --extent 2.0
python3 Util/cook_vector_anim.py plain.json --clips "idle:0:30,walk:30:60:once"
```

The cook is stdlib-only, deterministic and idempotent: the source `.json`
stays the artist's living document and re-running the cook after an edit
regenerates the same `.oanim` byte-for-byte. A document where NOTHING
animates cooks to a plain `.oshape` instead (the output suffix switches) —
so any tool that exports Lottie is also an `.oshape` authoring tool.

Unit conversions: coordinates y-flip (Lottie is y-down, `.oanim` +y up) and
scale so the composition's larger side spans `--extent` world units (default
2), centered on the composition midpoint (a synthetic `comp` root layer
carries the centering); rotations negate (to CCW, +y up); Lottie scale 100 →
1.0 and opacity 100 → 1.0; value beziers carry over 1:1.

### Subset

| Source feature | Cooks to |
|---|---|
| shape layer (`ty` 4) | a `layer` with channels + `shape` blocks |
| null layer (`ty` 3) | a channel-only layer (pure parent) |
| solid layer (`ty` 1) | a rect `shape` block in the solid colour |
| untimed precomp (`ty` 0, stretch 1, no remap) | inlined at cook (nested works; the crop rectangle is not applied); the precomp layer becomes a transform carrier whose opacity multiplies down to the inlined children, named `precomp/child` |
| parenting | `parent` links; a parent that also paints is split into a transform carrier + a `_paint` layer, so the grammar's parents-precede-children and paint-order-is-file-order rules both hold (layer opacity stays per-layer, matching the source's non-inheriting opacity) |
| layer in/out window (`ip`/`op`) | baked into the opacity channel (hold keys at 0 outside the window) |
| hidden layers/items (`hd`) | skipped (they never render) |
| position/anchor/scale/rotation/opacity keyframes | the five transform channels, easing preserved |
| bezier path (`sh`), ellipse (`el`), rect (`rc`, incl. rounded), polystar (`sr`) | flattened contours; animated primitive parameters preserve one fixed topology |
| flat fill (`fl`), linear/radial gradient fill (`gf`) | per-key paint; animated colours, stops, endpoints, radial focal highlight/angle and opacity; fill rule (nonzero/evenodd) drives hole assignment |
| stroke / gradient stroke (`st` / `gs`) | a `stroke` REGION — the flattened centreline plus width/cap/join/limit, swept by the renderer (see [Strokes](#strokes)) — with butt/round/square caps, miter/round/bevel joins, animated width/paint and dash/gap/offset patterns (one region per dash) |
| parallel trim path (`tm`, `m` 1) | the flattened centreline is length-trimmed before it becomes the stroke region; animated start/end/offset stays fixed-topology |
| rounded corners (`rd`), pucker/bloat (`pb`) | modifiers bake into path poses, including animated amounts |
| additive merge (`mm` 1) | input contours remain separate and are tessellated together |
| group (`gr`) | recursed; static and animated position/anchor/scale/rotation bake into shape poses; animated group opacity folds into paint alpha |
| one additive, non-inverted convex layer mask | clipped vector contours; animated mask paths are topology-normalised |
| direct layer-transform link expression | copied from the named source layer before normal channel conversion |
| markers | clips (`#once` comment suffix = one-shot; a zero-duration marker extends to the next marker); `--clips name:start:end[:loop\|once],...` overrides for marker-less tools |

Path flattening obeys the fixed-segment-count discipline: each path edge gets
ONE segment count chosen from the worst-case curvature across ALL of the
path's keyframes, applied at every key — identical vertex counts by
construction (the `.oanim` topology law), and because beziers are linear in
their control points, lerping the flattened vertices equals flattening the
lerped curve exactly. The per-edge count is the larger of two bounds: an
ABSOLUTE one (the chord-deviation must stay under a fraction of the whole
composition, keeping a heavy rig cheap) and an ANGULAR floor
(`EDGE_MAX_SEGMENT_ANGLE`: at least one segment per fixed turn of the control
polygon). The angular floor is size-independent, so a small tightly-curved
feature (an eye, a claw) stays as round as a large curve of the same shape
instead of collapsing to a facet — the extra detail lands only where it is
cheap (short, few edges); near-straight edges turn through no angle and are
untouched. `MAX_EDGE_SEGMENTS` still caps every edge.

DENSIFICATION: whatever the runtime grammar cannot express directly is baked
into per-frame linear keys at cook — spatial position tangents (`ti`/`to`),
split x/y position tracks, per-dimension easing, keys outside the timeline,
animated ellipse/rect parameters, shape blocks where several properties
animate on misaligned keys, and windowed animated opacity. The runtime
interpolator stays cubic value-bezier + hold.

### Out of subset — named, per-layer errors

The cook collects EVERY violation and refuses (never a silent skip): track
mattes, layer effects, arbitrary expressions, image layers, text layers,
repeaters, boolean merge modes, sequential trim, non-convex/multiple/subtract
masks, unsupported modifiers (zig-zag/offset/twist), skew, auto-orient, 3D
layers, time stretch, time remap, timed precomps and precomp cycles. Example:

```
cook_vector_anim: cannot cook bad.json:
  sequential trim paths on layer 'hero' are not supported; use parallel trim or bake the result
  unsupported text layer 'caption' - only shape, null, solid and untimed precomp layers cook
```

### Character authoring workflow

Author ONE composition per character (one file = one rig — clips that must
blend live together): layers as the bones (a null as the root, body parts
parented to it), all clips back-to-back on one timeline, markers naming the
frame ranges (`idle` 0–30, `walk` 30–60, a `#once` suffix on one-shots like
`die`). [Glaxnimate](https://glaxnimate.mattbas.org/) is a free tool that
exports this subset; if a tool cannot edit markers, pass the ranges at cook
time via `--clips`. Agents can also skip the cook entirely and write `.oanim`
text directly (see the grammar above) — the cook is an on-ramp, not a gate.

Verification: `cook_vector_anim_selftest` (a unit ctest) cooks embedded
fixtures for every mapped feature and every error path, and re-cooks
`tests/assets/vectoranim/roundtrip.json` and `stroke.json` asserting byte
identity with the committed `.oanim` beside each; the
`cook_vector_anim_roundtrip` unit test feeds that same `.oanim` through the real
parser + evaluator and pins evaluated poses (rotation easing, parent
composition, colour animation), and `cook_vector_anim_stroke_regions_sweep_clean`
pins the stroke fixture — a hairpin whose curvature is far tighter than its half
width plus a closed ring with an animated width — asserting the swept geometry
never escapes the ribbon. The sweep itself (segment quads, every join and cap,
the miter-limit fallback, tight curvature, degenerate segments, the mask clip)
is pinned in `tests/core/VectorTessellatorTests.cpp`.

## In the editor: import, thumbnails and preview

**Import** — dropping/importing a Lottie `.json` (Finder drop, the asset
browser, or MCP `import_asset`) cooks it to `.oanim` in place via
`cook_vector_anim.py` (a subprocess, the same wiring `.svg`→`.oshape` uses,
with the `python3` toolchain preflight). UNLIKE the one-way `.svg` on-ramp the
SOURCE `.json` is KEPT beside the cooked asset — both get an `.orkmeta` id, and
re-importing an edited `.json` re-cooks the `.oanim` in place (the living-source,
recook-on-reimport discipline; a document where nothing animates lands a
`.oshape` instead). `EditorDocument.cpp` `cookLottieFileToDir`. The cook's
post-cook summary — the clip table (name, frame range, loop/once), fps,
duration, layer count, and an explicit note when a marker-less document gets
the single implicit runtime clip `default` — prints on the CLI, mirrors into
the editor Console as `[import]` lines, and rides an `import_asset` reply as
`clips`, so clip discovery lands WITH the import instead of a file-open later.

**Thumbnails** — an `.oanim` asset shows a real tile: its default clip is
evaluated at frame 0 (`VectorAnimEval::evaluateAt` → `composeRegions`),
tessellated once and CPU-rasterized with `core_util/VectorShapeRaster`, then
uploaded via `RenderSystem::createTexture2D` — the exact `.oshape` thumbnail
path with the animation evaluator in front (`EditorAssetBrowserPanel.cpp`
`buildAnimThumbnail`).

**Preview in the Inspector** — selecting a `.oanim` (or a kept `.json` whose
cooked sibling exists) in the asset browser shows the animation preview in the
Inspector: choose a clip from the dropdown, scrub / play-pause it, and try a
same-rig blend (a second clip + a weight). The widget
(`AnimationPreviewPanel.cpp`) drives the editor-owned `AnimationPreviewStage`
on its OWN clock (the editor never ticks GameObjects): per frame it evaluates
the pose STATELESSLY, composes the region list, tessellates it and rasterizes
it (`VectorShapeRaster`) into a texture shown inline — a pure CPU raster, no
offscreen scene target, both render flavors, faithful to the tessellated
in-game look (same feather AA). No `Ogre::` and no RTT: it is
headless-capable.

**MCP readback** — the `preview_animation` verb (the `preview_ui` twin) shares
that one stage. Input `{ asset, clip?, time?, blendClip?, blendWeight?, size?,
path? }` renders the evaluated (optionally blended) pose to a PNG (encoded by
the dependency-free `core_util/PngWriter`) and returns
`{ clip, frame, time, duration, fps, layer_count, shape_count, vertex_count,
visible_pixel_count, coloured_pixel_count, at_end, clips }`, so an agent
verifies a cycle (t=0 / mid / end screenshots + numbers), rejects blank or
all-white output and checks a blend without a play session. It does not disturb the human's
Inspector preview (snapshot/restore). Covered by an `editor_control`
self-test leg (import a `.json` → `.oanim` with the source kept and the reply
carrying the rig's `clips` → preview at two times → the PNGs differ and the
readback carries the clips/layers) plus the `PngWriterTests` unit test.

## Playing a rig in a game

`engine_gocomponent/VectorAnimationComponent` is the runtime face (both
flavors): it parses the `.oanim`, builds the pure evaluator and, per gameplay
tick, advances the playing clip and uploads the moved vertices through the
dynamic `VectorMesh` path — dormant in the editor like every ticked component.
The reflected properties (`animation` AssetRef, `clip`, `speed`, `playing`,
`transitionTime`, `tint`, `scale`, `edgeSoftness`, `zOrder`, `visible`) ride
the ONE property registry: Inspector, scenes, MCP `set_component` and Lua all
see the same schema. `clip` names the clip playback starts on; a name the rig
no longer carries (a rename after a re-cook) falls back to the first clip AND
warns in the log.

Scripts drive the sibling rig through `self.anim` (a weak component handle):

- `play(clip)` / `stop()` — start a named clip (empty name = resume);
- `setClip(clip [, seconds])` / `crossFade(clip, seconds)` — switch with a
  crossfade (`setClip` without a time uses the reflected `transitionTime`;
  0 is a hard cut). Blending is same-rig only;
- `scrub(seconds)` — seat the current clip at a time (a paused pose scrub);
- `isPlaying()`, `currentClip()`, `currentFrame()`, `isAtEnd()`,
  `getClipCount()`, `getClipNames()` (comma-joined, the runtime clip
  discovery), `setSpeed`/`getSpeed`.

A name the rig does not carry REFUSES (returns false) and warns ONCE per name
per loaded rig in the log — naming the available clips — so a typo'd clip is a
diagnosis, not a frozen animation. The warning lives on the setter path only;
the per-tick evaluator never resolves names (the allocation-free tick is
untouched).

When a `once` clip reaches its end the component raises the owned
`VectorAnimationEndedEvent` (C++ listeners bound to the owner) and mirrors
`animation.ended {clip, object}` onto the script event bus — `object` is the
owner's id (the contact-event id convention), so several rigs playing the same
clip name stay distinguishable. `projects/vectorshapes/scripts/hero_anim.lua`
is the reference script (idle → one-shot hop crossfade → ended event back into
Lua), exercised by the `player_vectoranim_selfcheck` ctest on both flavors;
`Docs/lua-api.md` carries the drive snippet and the full method index.
