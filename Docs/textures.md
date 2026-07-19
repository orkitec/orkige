# Texture pipeline

How a texture travels from a project's `assets/` folder to a shipped export:
the import-settings model, the export-time cook, and GPU block compression.

The one-line contract: **the dev loop always renders the raw source PNG;
only the exported payload is conditioned** (resized, premultiplied,
block-compressed). Iteration never waits on a cook.

## Import settings (the `.orkmeta` sidecar)

Every id-tracked texture carries its import settings in its sidecar's
`<texture>` block (`core_project/AssetDatabase.h`, sidecar v3). The block
holds the DEFAULT settings plus optional per-platform override sub-blocks —
`<android>`, `<ios>` and `<web>` — each stored as a delta over the default:

```xml
<orkmeta id="5f2c...">
    <texture filter="bilinear" wrap="clamp" maxSize="0" premultiply="false"
             generateMips="false" format="auto" quality="normal">
        <android maxSize="1024"/>
        <ios format="astc-4x4"/>
        <web maxSize="512"/>
    </texture>
</orkmeta>
```

| Field | Consumed by | Meaning |
| --- | --- | --- |
| `filter`, `wrap` | the runtime, LIVE | sampler settings, honored at sprite material creation |
| `maxSize` | the export cook | longest-side texel cap (0 = uncapped) |
| `premultiply` | the export cook | fold alpha into RGB |
| `generateMips` | the export cook | bake an offline mip chain into the compressed container |
| `format` | the export cook | GPU block compression: `auto`, `none`, or one explicit format |
| `quality` | the export cook | encoder effort; under `auto` also the ASTC block size |

A texture whose sidecar is **id-only** cooks with the defaults (`format`
`auto`) — every id-tracked texture ships compressed unless it opts out. A
texture with **no sidecar at all** carries no import intent and ships
untouched.

Edit the settings in the editor (Inspector › Texture Import Settings, with
Base/Android/iOS/Web override sections and a resolved preview), or write the
sidecar directly — agents use the MCP `write_project_file` verb on the
`.orkmeta`; the Inspector re-reads a sidecar the moment the file changes, so
an external write is picked up live. No dedicated MCP verb exists or is
needed.

## The format model

One shared vocabulary on every platform slot:

`auto` | `none` | `astc-4x4` | `astc-6x6` | `astc-8x8` | `etc2` | `bc1` |
`bc3` | `bc7`

* `etc2` is a **family**, not one codec: the cook picks ETC2 RGB8 for an
  opaque texture and ETC2 RGBA8 (EAC alpha) when the source carries any
  alpha below 255. The BCn `auto` pick is alpha-aware the same way.
* Every explicit format is selectable on every slot; the cook **validates**
  the (format, platform, flavor) pair at export and refuses impossible
  combinations honestly (e.g. `bc7` on a mobile platform) — never a
  half-cooked export.
* `quality` (`low`/`normal`/`high`) maps to encoder effort everywhere, and
  under `format="auto"` on iOS it also picks the ASTC block size (`high` →
  4x4, `normal` → 6x6, `low` → 8x8). **An explicit block-size format wins
  over the quality-implied one** — quality then only tunes encoder effort.

### The `auto` table

`auto` resolves per export platform and packaged render flavor:

| Export | next flavor | classic flavor |
| --- | --- | --- |
| macOS (desktop) | opaque: `bc1` (`bc7` at quality `high`); alpha: `bc7` | opaque: `bc1`; alpha: `bc3` |
| iOS | ASTC (block size by quality), all Metal-capable iOS hardware decodes ASTC | `none` (PNG) |
| Android | ASTC (block size by quality), Vulkan-capable arm64 at the API-28 floor decodes ASTC LDR | `none` (PNG) |
| Web | `none` (PNG) | `none` (PNG) |

Rationale for the corners:

* **Desktop ships the BC family only**: ASTC/ETC2 refuse on the desktop
  slot for BOTH flavors — classic desktop GL exposes neither, and the next
  flavor's desktop renderer compiles its ASTC/ETC2 pixel-format mappings
  only into its mobile builds (the hardware may decode them; the loader
  does not). Classic desktop additionally never picks `bc7`: its default GL
  renderer runs on desktops whose GL has no BC7 support, so `auto` stays at
  the universally supported `bc1`/`bc3` and an explicit `bc7` on the
  classic desktop slot is refused.
* **Classic GLES2 mobile ships PNG**: ETC2 is GLES3-core — it is NOT
  guaranteed in a GLES2 context; ETC1 has no alpha, and the legacy
  square-power-of-two format family is not worth adding for a compatibility
  flavor. Until the classic GLES2 loader path is proven on the device floor,
  `auto` resolves to PNG there. An explicit `etc2`/ASTC override on the
  classic mobile slots is permitted (it ships as KTX1, which the classic
  loader parses) but the cook warns loudly that a GLES2 context may not
  accept it — treat it as a per-device experiment, not a shipping default.
* **Web ships PNG in v1**: compressed-texture support in a browser is a
  property of the *visitor's* GPU — desktop visitors expose the BC family,
  phones expose ETC2/ASTC, all as optional extensions, none guaranteed. An
  explicit format on the `<web>` slot is permitted but warns loudly: it only
  loads on clients exposing the matching extension. The universal answer —
  KTX2 with a supercompressed intermediate transcoded at load to whatever
  the visitor supports — is the planned **v2** of this pipeline and is
  deliberately not started here.
* **Android auto is ASTC** (matching iOS): the next flavor on Android boots
  Ogre-Next's Vulkan renderer, and every Vulkan-capable device at the shipping
  floor — arm64, API 28+ — decodes ASTC LDR (it is near-universal on that
  class of hardware, and the same encoder road iOS already ships). So `auto`
  resolves to ASTC there, block size by `quality` exactly like iOS (`high`
  4x4 / `normal` 6x6 / `low` 8x8), in the native `.oitd`. **ETC2 stays a
  first-class explicit override** (`format="etc2"` → ETC2 RGB8/RGBA8 `.oitd`)
  for a project targeting older or ASTC-less Android GPUs; the cook validates
  and encodes it unchanged. The on-device load proof rides the Android
  Play/export device tests; the host asserts the cook output + the `.oitd`
  `PixelFormatGpu` (an ASTC value for `auto`, an ETC2 value for the explicit
  override).
* **Cubemaps cook through the same matrix**: a skybox cubemap
  (`AtmosphereDesc::skyboxTexture`) is a six-face `.dds` — one uncompressed
  BGRA8 container with a baked mip chain, baked by `Util/make_sky_assets.py`.
  A sidecar-carrying cubemap resolves its `format` through the SAME `auto`
  table and encoder as a 2D texture, so `auto` block-compresses it per
  platform (desktop BCn `.dds`; iOS/Android ASTC `.oitd` on next), and
  `none` (the sky baker's default stamp) ships it verbatim. Two properties of
  a cubemap are preserved exactly: the **face order** (`+X,-X,+Y,-Y,+Z,-Z`)
  and the **baked mip chain** — a sky cubemap's chain IS the prefiltered
  roughness chain the IBL samplers index (`make_sky_assets.py`), so the cook
  re-encodes each level as-is rather than regenerating it (`maxSize`/
  `premultiply`/`generateMips` therefore do not apply to a cubemap). The BC
  container reuses the source `.dds` name in place; the mobile ASTC/ETC2
  containers rename `.dds` → `.oitd`/`.ktx`, and the skybox loaders on both
  flavors resolve a missing `.dds` to its cooked sibling (the same
  cooked-extension fallback the 2D paths use). The stock skies still stamp
  `format="none"` (a 128px-face sky is ~0.5 MB and its prefiltered chain is
  quality-sensitive), so shipping bytes are unchanged until a cubemap opts in
  — the capability is there for any project that wants it.

### Containers and what the runtime loads

The cook replaces `foo.png` in the payload with the compressed container and
renames the sidecar along with it (`foo.dds.orkmeta`), so the asset-id
machinery resolves id-carrying scene references to the shipped name. For
bare, id-less references (`.omat` texture names, gui atlas files,
script-assigned sprite names) both render backends fall back from a missing
`.png` to its cooked siblings.

| Formats | Container | Loaded by |
| --- | --- | --- |
| `bc1`/`bc3`/`bc7` | `.dds` | both flavors (each registers a DDS codec at boot) |
| ASTC/ETC2, next flavor | `.oitd` | the next runtime's native image container |
| ASTC/ETC2, classic flavor | `.ktx` (KTX1) | the classic runtime's compressed-texture codec |

The same three containers carry a **cubemap** (six faces, the full mip chain):
the `.dds` sets the cubemap caps + a face-major payload, the `.oitd` a `TypeCube`
header with the faces mip-major (the Ogre-Next `Image2` slice layout), the KTX1
`numberOfFaces` = 6. `texcook --faces 6` encodes all six faces in one pass.

All shipped formats are the **non-sRGB (UNORM) variants**: the engine's
render pipeline is deliberately gamma-space passthrough on both flavors
(textures sampled raw, non-sRGB swapchain — the cross-flavor colour-parity
convention), so no colorspace field exists in the settings; nothing would
consume it.

### Mip chains

`generateMips` bakes an offline mip chain: the cook downsamples the
(resized) source area-averaged per level and the encoder compresses every
level into the container — `.dds`, `.ktx` and `.oitd` all carry mip chains
natively. The runtime never generates mips for compressed textures (there is
nothing to generate them with on the GPU), so a compressed texture without a
baked chain simply samples its base level. Default off; flip it for 3D
content viewed at oblique angles.

### Normal maps

A normal map encodes directions, not colours — BC1's 5:6:5 endpoint
quantisation visibly distorts lighting. The generators that bake normal maps
(`make_terrain_mesh.py`, `make_material_demo.py`) stamp `format="none"` so
they ship as exact texels. Manually choosing ASTC (fine quality for normals)
or `bc7` on the next desktop slot is reasonable; this pipeline deliberately
has no texture-type system — the per-texture format field IS the knob.

### Atlas defaults

Glyph/UI atlases (`make_gui_atlas.py`) and sprite region atlases
(`make_sprite_atlas.py`) are pixel-exact, point-sampled artwork — block
compression smears glyph and frame edges. The generators stamp
`format="none"` into the generated sidecar (preserving an existing asset id,
never overriding a format a user already chose — `Util/orkige_sidecar.py`),
so atlases ship as PNG while any decorative atlas can still opt in per
asset.

## The export cook

`Util/orkige_export.py` stages the project payload and runs
`Util/cook_textures.py` over it, resolved for the target platform token
(`""`/`ios`/`android`/`web`) and the packaged render flavor. The cook stays
stdlib-only by policy: the encoding itself runs in **`texcook`**
(`tools/texcook`), a small host CLI built in every desktop engine tree. Its
one dependency, libktx (vcpkg `ktx`), carries the ASTC encoder for every
block size plus the universal encoder/transcoder pair that yields ETC2 and
BC1/BC3/BC7 blocks; the `.dds`/`.ktx`/`.oitd` containers are written by the
tool itself.

**Encoder requirement**: a payload that resolves to any compressed format
needs a `texcook` binary — the exporter finds the engine tree's own, falls
back to the repo's desktop trees, and honors `ORKIGE_TEXCOOK`. Without one
the export **refuses** with a clear message rather than shipping a
half-cooked payload. Web/mobile-classic exports whose textures all resolve
to PNG need no encoder.

Out of scope by construction: textures baked at runtime (TrueType font atlas
pages, rasterised SVG sprites, anything through `createTexture2D`) are
uploaded raw on device and never touch this pipeline; textures embedded
inside `.glb` meshes ship as authored.

## Verification

* `texcook --selftest` — every format encodes, container layouts verify,
  plus the cubemap round-trip (six faces to DDS/OITD/KTX, cube caps + a
  face-complete payload).
* `cook_textures.py --selftest` — the auto table, override precedence
  (incl. the web slot), impossible-pair refusals, no-encoder refusal, real
  encode legs (rename + sidecar + mip chains) when given an encoder, and the
  cubemap legs (decode an uncompressed six-face `.dds`, cook it in place to a
  BC1 cube DDS with the mip chain + cube caps preserved, rename to a `.oitd`
  cube on Android, ship a non-cubemap `.dds` verbatim, refuse without an
  encoder).
* `render_cooked_cubemap` (ctest, both flavors) — block-compresses the stock
  debug cubemap through the real cook and boots the render-facade selfcheck
  against it: the skybox leg proves the compressed cube LOADS with its +X face
  and baked mip chain intact (desktop BC; the mobile ASTC/ETC2 cube containers
  are structural + device-tested, like the 2D `.oitd`).
* `player_cooked_textures` (ctest, both flavors) — the real player renders a
  cooked payload from `.dds`, through both the asset-id rename and the
  bare-name fallback; the iOS and Android `.oitd` cooks (both ASTC by
  default) are asserted structurally (their on-device load proof rides the
  iOS-simulator/Android Play and export device tests).
* `export_*` ctests — every exported payload's compressed-texture set
  matches what its source project's settings resolve to per platform.
