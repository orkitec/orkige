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
