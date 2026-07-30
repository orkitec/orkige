# Procedural meshes: the `.omesh` asset

3D geometry authored as TEXT. A `.omesh` is a list of placed parametric shapes
in one small, diffable file; the engine builds it into a real mesh resource on
first use, so `ModelComponent.mesh` accepts it exactly like a `.glb` — same PBS
`.omat` materials, same shadows, same `static` mobility flag, same visibility
flags, same native instancing. No binary asset, no offline tool, no Python.

It is the 3D sibling of the flat-colour text tier: `.oshape` describes a 2D
outline, `.omat` a surface look (see [materials.md](materials.md)), `.omesh` a
solid.

## The grammar

One directive per line, `#` starts a line comment, keywords are
case-insensitive.

```
# a small blockout
version 1
material stone
box 3 0.4 3                                      at 0 -0.8 0
stairs 1.6 1 1.2 steps 5                         at -0.7 -0.1 0
arch span 1.0 legs 0.8 thickness 0.25 depth 0.4  at 0 0.9 -1.2
cylinder radius 0.25 height 1.4 segments 20      at -1.1 0.1 1.0  material metal
revolve shape vase.oshape segments 28            at 3 0 0         material metal
```

`version 1` is optional but must come first when present. A `material NAME` line
on its own sets the material every later shape uses.

### Shapes

A shape's SIZE is positional where it is a box-like extent triple and NAMED
where it is a radius/height.

| Directive | Parameters |
| --- | --- |
| `box` | `SX SY SZ` |
| `roundedbox` | `SX SY SZ radius R [segments N]` |
| `plane` | `SX SZ [segments NX NZ]` |
| `wedge` | `SX SY SZ` (a ramp rising along +X) |
| `stairs` | `SX SY SZ [steps N]` |
| `sphere` | `radius R [segments N] [rings N]` |
| `icosphere` | `radius R [subdivisions N]` |
| `cylinder` | `radius R height H [segments N] [caps 0\|1]` |
| `cone` | `radius R height H [segments N] [caps 0\|1]` |
| `capsule` | `radius R height H [segments N] [rings N]` |
| `torus` | `radius R tube T [segments N] [tubesegments N]` |
| `tube` | `radius R inner I height H [segments N] [caps 0\|1]` |
| `disc` | `radius R [inner I] [segments N]` |
| `arch` | `span W legs H thickness T depth D [segments N]` |
| `extrude` | `shape FILE.oshape depth D [smoothsides]` |
| `revolve` | `shape FILE.oshape [segments N] [sweep DEGREES]` |

### Modifiers

Every shape line may carry these, in any order, at most once each:

| Modifier | Meaning |
| --- | --- |
| `at X Y Z` | translation (default `0 0 0`) |
| `rotate X Y Z` | Euler DEGREES, composed `Ry * Rx * Rz` |
| `scale S` / `scale SX SY SZ` | scale about the shape's own origin |
| `material NAME` | this shape's material, overriding the current default |
| `uv MODE [SU SV]` | re-project the UVs: `xz`, `xy`, `zy`, `box`, `cylindrical` or `spherical`, with an optional tiling scale |
| `smooth [ANGLE]` | re-average the normals, welding faces meeting below ANGLE degrees (default 60) |
| `flat` | replace the normals with per-face normals |

## Conventions

- right-handed, +Y up, counter-clockwise front faces seen from outside;
- every shape is centred on its own bounding-box centre, so `at` places its
  middle and two equal-sized shapes sit flush one size apart. `extrude` and
  `revolve` keep their authored placement instead — the outline decides;
- UVs run 0..1 across each shape's natural parametrisation (per face for the box
  family, angle/height for the round family), V down;
- counts (`segments`, `rings`, `steps`, `subdivisions`) are CLAMPED into a sane
  range: a value below the shape's structural minimum reads as the minimum, and
  a ceiling keeps a mistyped `segments 100000000` from trying to allocate the
  machine;
- a non-positive or non-finite EXTENT is an error, reported with its line;
- the same parameters always produce byte-identical geometry.

## Materials and sections

`material NAME` refers to `NAME.omat`. Shapes sharing a material MERGE into one
draw section (one sub-mesh, one draw call), so the example above renders in two
draws — stone and metal — no matter how many shapes each carries. Section order
is the order materials first appear.

The live material is the same one a `ModelComponent.material` slot naming the
same file produces, so a mesh section and a component reference share it.

## Refusals

An unknown directive, an unknown modifier key, a missing or non-numeric value, a
duplicated key, a trailing token, an unsupported version, a refused shape and an
unresolvable `extrude`/`revolve` reference are all ERRORS reported as
`line N: ...`. Nothing is silently ignored — a typo that misrenders without a
trace is worse than a refusal. In the editor's script editor the reported line
becomes a clickable marker as you type (the `.omat` live-diagnostics road).

The only thing the editor's live check cannot resolve is a `shape` REFERENCE: it
validates the grammar with a placeholder outline, so a missing `.oshape` file
surfaces at load time the way a missing `.omat` does.

## 2D to 3D

`extrude` and `revolve` consume an `.oshape` — the same outline a
`VectorShapeComponent` paints and a shape collider collides against.

- `extrude` closes the outline into a solid of the given depth: a front cap, a
  back cap and side walls swept from every boundary loop, so a shape with a hole
  extrudes as a real tunnel. `smoothsides` averages the wall normals at each
  contour vertex, which makes a flattened curve shade as a curve.
- `revolve` reads the outline's first solid region as a PROFILE: each point's x
  is a radius and its y a height, so the outline must stay on the x >= 0
  half-plane. `sweep` below 360 leaves the two profile-shaped ends open.

## Using one

Write it into a project's `assets/` and name it from a `ModelComponent`:

- in the editor, the mesh field's picker offers `.omesh` files beside `.glb`
  ones, and the asset browser shows a rendered thumbnail;
- over MCP, `write_project_file` authors the text and `set_component` points a
  `ModelComponent.mesh` at it — no new verb;
- during Play, saving an edited `.omesh` HOT-RELOADS it: the editor watches the
  project tree and the running player parses the fresh text first, then rebuilds
  every component naming it. A broken edit keeps the old geometry on screen and
  reports the line.

## Where the code lives

| Piece | File |
| --- | --- |
| the indexed mesh type + the operators (transform, merge, normals, UVs, tangents, validate) | `orkige_core/core_util/MeshBuilder.h` |
| the parametric shape generators | `orkige_core/core_util/MeshShapes.h` |
| `extrude` / `revolve` over `.oshape` outlines | `orkige_core/core_util/MeshExtrude.h` |
| the `.omesh` parser | `orkige_core/core_util/MeshAsset.h` |
| the facade entry (a lit mesh resource from data) | `orkige_engine/engine_render/RenderWorld.h` — `createMeshFromData` |
| the asset road (text to resource, flavor-neutral) | `orkige_engine/engine_render/MeshAssetLoad.cpp` — `ensureMeshAsset` |

Everything above the facade is pure, headless and unit-tested without a
renderer; the per-flavor mechanics are in
[render-abstraction.md](render-abstraction.md).

## Honest boundaries

Not in this version, each a real feature with a real cost rather than an
oversight:

- no booleans (union/subtract/intersect);
- no arrays, loops, variables or expressions in the grammar — it stays a flat,
  diffable placed-shape list;
- no per-vertex colour: a `.omesh` surface takes its look from a `.omat`;
- `revolve` sweeps the FIRST solid region of its `.oshape`, and a partial sweep
  leaves its ends open (capping one needs the profile triangulated);
- an `icosphere`'s UVs are a spherical projection and carry the usual longitude
  seam — author it for flat or triplanar looks rather than a wrapped atlas;
- no collider derived from a `.omesh` yet: a physics body still names its own
  primitive or `.oshape`.
