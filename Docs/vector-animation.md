# Vector animation (`.oanim`)

Keyframed animation for flat-colour vector shapes: a layer rig whose
transforms, opacities and path poses are keyframed over one frame timeline,
carved into named clips (idle/walk/run/...), evaluated by the pure
`core_util/VectorAnimEval` and blended clip-to-clip at the pose level. The
`.oanim` text asset is the animated sibling of `.oshape` — same fill/contour
vocabulary, same agent-authorable plain text.

## The `.oanim` format (v1)

One token stream, `#` starts a line comment, indentation is cosmetic.
Coordinates are shape-local units, +y up (the `.oshape` space); colours are
straight RGBA 0..1. The authoritative grammar lives in
`orkige_core/core_util/VectorAnimAsset.h`; summary:

```
version 1
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
```

Easing: `lin` is linear, `hold` is constant until the next key, `ease` is a
cubic value bezier through (0,0), (ox,oy), (ix,iy), (1,1) — x is the time
fraction (clamped to 0..1 so time stays monotone), y the value fraction (free,
for overshoot). This is the whole runtime interpolator vocabulary by design:
anything richer in an imported source is densified into extra keys at cook.

Malformation honesty: `VectorAnimAsset::parse` returns false and an EMPTY
document on any malformation (missing header, bad clip ranges, forward/self
parents, truncated key or vertex runs, topology mismatches) — never a
half-loaded rig. Unknown keywords between complete elements are reserved and
ignored.

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
  once and then only moves vertices.
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

### Subset (v1)

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
| bezier path (`sh`), ellipse (`el`), rect (`rc`, incl. rounded) | flattened contours; an animated corner radius keeps the 8-vertex rounded topology so counts stay fixed |
| flat fill (`fl`), animated colour/opacity | per-key `fill`; the fill rule (nonzero/evenodd) drives hole assignment for multi-path fills |
| group (`gr`) | recursed; its STATIC transform is baked into vertices; animated group opacity folds into the fill alpha |
| markers | clips (`#once` comment suffix = one-shot; a zero-duration marker extends to the next marker); `--clips name:start:end[:loop\|once],...` overrides for marker-less tools |

Path flattening obeys the fixed-segment-count discipline: each path edge gets
ONE segment count chosen from the worst-case curvature across ALL of the
path's keyframes, applied at every key — identical vertex counts by
construction (the `.oanim` topology law), and because beziers are linear in
their control points, lerping the flattened vertices equals flattening the
lerped curve exactly.

DENSIFICATION: whatever the runtime grammar cannot express directly is baked
into per-frame linear keys at cook — spatial position tangents (`ti`/`to`),
split x/y position tracks, per-dimension easing, keys outside the timeline,
animated ellipse/rect parameters, shape blocks where several properties
animate on misaligned keys, and windowed animated opacity. The runtime
interpolator stays cubic value-bezier + hold.

### Out of subset — named, per-layer errors

The cook collects EVERY violation and refuses (never a silent skip):
gradients (fill/stroke), strokes, masks, track mattes, layer effects,
expressions, image layers, text layers, repeaters, merge paths, trim paths,
rounded-corner modifiers, other path modifiers (pucker/zig-zag/offset/twist),
skew, auto-orient, 3D layers, time stretch, time remap, TIMED precomps and
precomp cycles. Example:

```
cook_vector_anim: cannot cook bad.json:
  gradient fill (flat fills only) on layer 'hero' (item 'shine') - not supported
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
`tests/assets/vectoranim/roundtrip.json` asserting byte identity with the
committed `roundtrip.oanim`; the `cook_vector_anim_roundtrip` unit test feeds
that same `.oanim` through the real parser + evaluator and pins evaluated
poses (rotation easing, parent composition, colour animation).
