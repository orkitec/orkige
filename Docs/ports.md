# Overlay ports — rationale

Full documentation for the vcpkg overlay ports in `ports/` (wired via
`VCPKG_OVERLAY_PORTS` in CMakePresets.json). The prose lives HERE, not in the
port directories: vcpkg hashes every byte of a port dir into the port's ABI
hash, so editing a README inside `ports/<name>/` forces a full rebuild of that
port on every triplet (macOS + iOS + Android). Keep the in-port READMEs to a
single pointer line and batch any real port edits (see "Build speed" in
CLAUDE.md).

For the supply-chain view — how the whole dependency closure is pinned (one
vcpkg commit), which single-file asset parsers are vcpkg-managed vs. vendored
in-tree, and the SHA-pinning of the GitHub Actions (and how to re-pin on a
version bump) — see [vendored-libs.md](vendored-libs.md).

## ports/ogre

Overlay of the upstream vcpkg `ogre` port, repinned from the v14.5.2 release
tag to master commit `027c77662a8d161a0c9eebf16018ffe4558e1e57` (2026-08-04,
declares itself 14.6.0; `version-date` in the port's vcpkg.json). The pin
CONTAINS all EIGHT of our merged upstream PRs: the
Metal/Vulkan trio (OGRECave/ogre #3667/#3668/#3669, merged 2026-07-07/08),
the classic-master build/runtime fixes (#3673 zip fallback, #3674 null
manualRender, #3675 Android cpufeatures, merged 2026-07-17) and the two
Assimp animation-import fixes (#3680 keyframe predecessor + interpolation
units, #3681 stray-scale-key times + keyless-track bind-pose fallback, merged
2026-07-22). Each was vendored as an overlay patch the day it went upstream
and DROPPED here at this pin bump once it merged - the standing lifecycle.
No release tag carries them yet; move the REF back to a release tag when one
does. The pin moves like ogre-next's: a reviewed bump, full-suite verified,
never implicit. Enabled from the root `vcpkg.json`. Delete this overlay if
upstream ever grows equivalent features.

**Moving this pin is a pixel decision, not just a build one.** Two paths
through the RTSS reach the screen, and an upstream lighting change lands on
only one of them.

A material the engine generates gets a render state composed by hand in
`RenderSystemClassic.cpp`: the stock `SRS_TRANSFORM`, `SRS_VERTEX_COLOUR`,
`SRS_TEXTURING`, `SRS_ALPHA_TEST` and `SRS_NORMALMAP` stages, plus four
ENGINE-OWNED sub-render-states - metal-rough lighting, hemisphere ambient,
image lighting and atmospheric fog - which stand in for the stock lighting and
fog stages. Lit surface response therefore comes from
`orkige_engine/media/rtss/OrkigeLib_MetalRough.glsl`, never from
`SGXLib_PerPixelLighting.glsl` or `SGXLib_CookTorrance.glsl`. That library
includes `RTSLib_Lighting.glsl` for `getAngleAttenuation` alone and computes
its own distance falloff inline, so upstream attenuation and directional-light
work does not reach it.

A material that never gets an engine-built render state falls through
`handleSchemeNotFound` (`engine_graphic/Engine.h`), which registers a shader
technique against the scheme's DEFAULT render state - `SRS_TRANSFORM`,
`SRS_VERTEX_COLOUR`, `SRS_PER_PIXEL_LIGHTING`, `SRS_TEXTURING`, `SRS_FOG`,
`SRS_ALPHA_TEST` (`OgreShaderRenderState.cpp`). That is the surface an
upstream RTSS lighting change moves, and it moves classic alone while the next
flavor stays exactly where it was - a one-sided shift, which is precisely what
`render_backend_parity` fails on. Read the RTSS commits in a candidate range
before bumping, classify them by which of the two paths they touch, and budget
for re-baselining the parity gate. Local additions:

- `metal` feature (`OGRE_BUILD_RENDERSYSTEM_METAL=ON`, Apple platforms) so
  RenderSystem_Metal is available next to GL3Plus - the upstream port has no
  way to enable it. The exported target carries its include dirs since our
  merged #3667 - the former `metal-export-include-dirs.patch` is consumed
  from upstream via the pin.
- `vulkan` feature (`OGRE_BUILD_RENDERSYSTEM_VULKAN=ON` +
  `OGRE_BUILD_PLUGIN_GLSLANG=ON`, deps: vulkan-headers, vulkan-loader,
  glslang). The VK_EXT_metal_surface window branch (Vulkan RS on macOS/iOS
  through MoltenVK, incl. VK_KHR_portability_enumeration/subset handling) and
  the base `RenderSystem::shutdown()` call (debug builds aborted on exit with
  a VMA leak assertion without it) are upstream now - our merged #3669 and
  #3668, formerly the `vulkan-metal-surface.patch` /
  `vulkan-shutdown-call-base.patch` this port vendored. MoltenVK itself stays
  driver-tier from Homebrew (see CLAUDE.md); static MoltenVK packaging into
  the app bundle is handled separately (see the feature description in
  vcpkg.json). One patch remains:
  - `vulkan-vcpkg-deps.patch` - vcpkg-only: resolve vulkan-headers/glslang
    through their CMake configs and re-export them from OGREConfig.cmake.
- Five former upstream-candidate patches were CONSUMED at the 2026-07-22 pin
  bump, their PRs having merged into the range this REF spans - they now come
  from upstream through the pin, exactly like the Metal/Vulkan trio above:
  `zip-entry-open-nonstrict.patch` (#3673: master's non-strict
  `ZipArchive::open` fallback called a three-argument `zip_entry_open` the
  bundled zip library does not declare; upstream CI builds strict and
  preprocessed the branch out, this port builds non-strict), the twin
  `manual-render-null-renderable.patch` (#3674: the GpuParamsDirty refactor
  dereferenced the null renderable that `manualRender(RenderOperation*)`
  documents, killing the classic `DrawLayer2D` gui/editor surface),
  `cpufeatures-build-interface.patch` (#3675: static OgreMain's Android export
  leaked the internal `cpufeatures` target as a bare `-lcpufeatures` no
  consumer can resolve), `assimp-single-key-interpolation.patch` (#3680: the
  Assimp keyframe-neighbour search skipped the immediate predecessor and mixed
  seconds with ticks in the interpolation factor - single-key channels
  collapsed to the inverse bind) and `assimp-scale-track-fallback.patch`
  (#3681: the Assimp loader keyed stray scale keyframes by the rotation time -
  garbage times + an OOB read - and fell keyless tracks back to a neutral
  placeholder instead of the bone's bind pose). The
  `player_character_rig_selfcheck` head-scale/pose legs are the standing proof
  the two Assimp fixes behave identically arriving from upstream.
- Future upstream-candidate fixes follow the same lifecycle these did: vendor
  the patch in the port the same day the PR goes upstream, then drop the patch
  file at the next reviewed pin bump once it is merged.
- `fix-dependencies.patch` (upstream vcpkg patch, locally amended): resolves
  OGRE's dependencies through the vcpkg CMake configs instead of its bundled
  find modules, and redirects the overlay component's configure-time imgui
  DOWNLOAD to `find_package(imgui CONFIG REQUIRED)` - a port build performs no
  network fetch, and the overlay links the same `ports/imgui` this repository
  pins everywhere else. That hunk's context carries upstream's own imgui
  version string, so it needs one line rebased whenever upstream bumps imgui.
  The local amendment is the OGREConfig.cmake template's
  `find_dependency(SDL2 CONFIG)`, now guarded by
  `if(NOT "@ANDROID@" AND NOT "@EMSCRIPTEN@")` - the same condition OGRE's
  own CMake uses to skip SDL2 there, and the same platforms the port's
  vcpkg.json excludes the sdl2 dependency on. Without the guard the installed
  config hard-fails on arm64-android (SDL2 is never installed for it).
  Upstream-candidate for vcpkg's ogre port.
- `ios-ninja-and-install-paths.patch` - iOS builds with the Ninja generator
  (the arm64-ios-simulator triplet): OGRE's iOS branch injects the Xcode
  generator variable `$(PLATFORM_NAME)` into the library output dirs (a
  literal `$(...)` in build.ninja, which Ninja rejects) and installs release
  static libs into `lib/Release`, breaking the vcpkg layout. Note the
  companion quirk handled in `triplets/arm64-ios-simulator.cmake`: OGRE only
  recognizes iOS through `APPLE_IOS`, which upstream sets exclusively in its
  own Xcode toolchain file - the triplet passes `-DAPPLE_IOS=ON` for this
  port (plus `VCPKG_OSX_ARCHITECTURES arm64`, because OGRE pre-seeds
  `CMAKE_OSX_ARCHITECTURES=x86_64` before `project()`).

### The patch set

Applied in this order (`portfile.cmake`), each verified to apply clean against
the pinned REF:

| Patch | Why it exists |
|-------|---------------|
| `fix-dependencies.patch` | resolve dependencies through the vcpkg CMake configs; redirect the imgui download to `ports/imgui` (detailed above) |
| `cfg-rel-paths.patch` | resolve the installed `resources.cfg`'s plugin and media paths relative to the config file's own directory, and let the port's `OGRE_CFG_INSTALL_PATH` stand on Apple platforms, so the package relocates |
| `swig-python-polyfill.patch` | define `_PyObject_GC_UNTRACK` for the SWIG-generated Python module where SWIG is older than 4.0.1 and Python 3.8 or newer removed it |
| `pkgconfig.patch` | the static `.pc` template's `Requires:` line is configured from what the build actually enables, instead of a fixed list naming packages that may be absent |
| `same-install-rules-all-platforms.patch` | drop the macOS-only `/${PLATFORM_NAME}` release subdirectory from the library install path, so one layout holds everywhere |
| `ios-ninja-and-install-paths.patch` | the iOS + Ninja fixes (detailed above); applies on top of the previous patch |
| `cmake4.patch` | move `enable_language(OBJC/OBJCXX)` below `project()` - CMake 4 refuses a language enabled before the project call |
| `vulkan-vcpkg-deps.patch` | resolve vulkan-headers/glslang through their CMake configs; applies on top of `fix-dependencies.patch` |

### Pinned range: 2026-07-22 to 2026-08-04

Twenty-five upstream commits, `63c5e68c8` to `027c77662`. No dependency was
added or removed and no CMake option was renamed, so the port's feature set
(`assimp`, `overlay`, `zip`, `metal`, `vulkan`, `freeimage`, `bullet`,
`openexr`, `d3d9`, `java`, `python`, `csharp`, `strict`, `tools`) still maps
onto upstream's option names one for one. Two commits bump the versions of
zlib (1.3.2) and freetype (2.14.3) that `CMake/Dependencies.cmake` would
build; this port sets `OGRE_BUILD_DEPENDENCIES=OFF` and takes both from
vcpkg, so neither reaches the build.

**Reaches the fallback lighting path** - `SRS_PER_PIXEL_LIGHTING` and the
`FFPLighting` base it derives from, so these are the parity-relevant ones:

- `36dbf36fa` RTSS: Lighting - use smoothed distance attenuation. Adds a
  `vec4` overload of `getDistanceAttenuation` in `RTSLib_Lighting.glsl` -
  the classical falloff multiplied by a quartic smoothstep that reaches zero
  at the light's range - and switches the per-pixel stage onto it. Point and
  spot lights fade out where they previously cut off at max distance. The
  `vec3` overload is untouched.
- `d4b85c3f5` RTSS: do not attenuate directional lights. The per-pixel stage
  computes attenuation only inside the `vLightPos.w != 0.0` branch, so a
  directional light is no longer divided by its constant attenuation term.
  Directional-lit surfaces brighten wherever that term was not 1.
- `61e892043` RTSS: FFPLighting - do light iteration in shader. The per-light
  loop moves out of the generated C++ (one unrolled call per light) into
  `SGXLib_PerPixelLighting.glsl`, matching the Cook-Torrance stage. The maths
  is the same; the accumulation order and the generated shader are not.
- `5ee1d7d3f` RTSS: fix attenuation when using relaxed precision. Types the
  classical overload's distance parameter `float32_t`.
- `3f333feef` RTSS: PerPixelLighting - fix precision for D3D11 compat.

**Cook-Torrance only** - `SRS_COOK_TORRANCE_LIGHTING` and
`SGXLib_CookTorrance.glsl`, which neither path installs, so these change
nothing here: `5206bd579` (ambient mixes with `diffuseColour`, so it no longer
lights a metal that cannot diffuse it), `a05cce808` (energy compensation made
visible, Schlick takes LoH), `930240674` (light-count logic shared with
FFPLighting), `4002b30c9` (relaxed-precision fixes), `aee0dee98`
(uninitialized warning), `c978979e9` (in/out typo).

**Other rendering**: `797b5d7a6` (RTSS static initialization order),
`b7c941342` (SceneManager keeps its light caches across a program change),
`92b71cc88` (DistanceSort tie-break), `9e5a7f185` (a `RealRect` overload of
`Frustum::projectSphere`, with `Camera` re-exporting the base via `using`),
`2ebfcfd1d` (`Node::convert*` become const; `Mesh::getPoseIndex`),
`df1276dca` (`uvecN` types in `OgreUnifiedShader.h`), `c99d17878`,
`74b56aad2` and `027c77662` (Direct3D11).

**Build and samples**: `82b9f6b0a` (imgui 1.92.9 - the overlay source now
iterates `ImDrawData::CmdLists` as a range, which the 1.92.8 `ports/imgui`
already declares an `ImVector`, so the two ports stay independently pinned),
`472d68033` and `3d7e8d81c` (the zlib/freetype versions this port does not
build), `30ec2f30a` and `5746dd144` (samples).

## ports/ogre-next

Locally authored port (pinned master commit `2a82de656f8abe7627e373621044b53df080f9de`,
2026-08-01, declared as `version-date` in the port's vcpkg.json; no upstream
vcpkg port exists). Master over the v3.0.0 tag by decision: upstream maintains
only master (no patch releases since the 2024 tag) and it carries Vulkan
hardening the engine wants - device-loss recovery in `VulkanRootLayout`, an
Adreno 6xx workaround, a run of render-pass/synchronization-hazard corrections
in the Vulkan RS - plus it absorbed part of this port's Apple patches
(below). The pin is the merge commit of our own OGRECave/ogre-next #586, so the
tip of the pinned range is a reviewed change rather than an arbitrary HEAD. It
also contains our merged #582 (the
NEON Math/Array include-order fix, merged 2026-07-15), which un-breaks the
arm64 Linux build - the `linux-debug-sanitize` preset in the Linux rig
container cold-builds this port natively. The pin moves deliberately (a
reviewed bump, full-suite verified, roughly monthly), never implicitly. The Ogre-Next backend of
the `engine_render` facade (Docs/render-abstraction.md); pulled in ONLY by the
`render-next` manifest feature (root vcpkg.json), so classic-only development
never builds it. Static, `supports: (osx & arm64) | (linux & x64) | (windows & x64 & !uwp) |
(ios & arm64) | (android & arm64)` - Linux and Windows are the desktop Vulkan
flavors (the CI `linux-next`/`windows-next` jobs); the iOS (Metal) and
Android (Vulkan) halves carry the mobile next backend.

**Coexistence with classic `ogre` in one installed tree** is a hard
requirement and holds by construction: `OGRE_USE_NEW_PROJECT_NAME=ON` gives
`include/OGRE-Next/` headers and `libOgreNext*Static.a` /
`libRenderSystem_MetalStatic.a` lib names (classic: `include/OGRE/`,
`libOgre*.a`, `libRenderSystem_Metal.a` - file-disjoint even for the Metal
RS), CMake config + HLMS media live under `share/ogre-next/`
(classic: `share/ogre/`). Verified live: a `render-next` tree (the default
`macos-debug`/`macos-release` presets) installs both into one
`vcpkg_installed` tree.

Configuration: ONE render system per platform (four-way in the portfile) -
Metal on macOS and iOS (first-class on Ogre-Next; the legacy GL3+ 4.1 path
buys nothing there), Vulkan on Linux and Android:

- **Linux** - XCB windowing; headers/loader from the vcpkg
  `vulkan-headers`/`vulkan-loader` ports, glslang from the `glslang` port for
  the RS's runtime GLSL->SPIR-V compile. The upstream static archive has no
  link interface, so the shipped config carries
  `Vulkan::Vulkan;glslang::glslang;glslang::SPIRV;xcb;X11-xcb;xcb-randr` on
  `OgreNext::RenderSystem_Vulkan`, and `OgreNext::Main`'s Linux platform libs
  include `Xt;Xaw;Xrandr` for the GLX config dialog compiled into OgreMain;
  the xcb/Xt/Xaw dev packages come from the system package manager, same rule
  as classic ogre on Linux.
- **Windows** - Vulkan with the Win32 window surface (Direct3D stays off -
  the render facade drives Vulkan on every non-Apple platform); loader and
  headers from the vcpkg `vulkan-*` ports, glslang from vcpkg; static libs
  follow the MSVC `<name>.lib` layout (the shipped config resolves both
  naming schemes).
- **Android** - Vulkan with the ANativeWindow surface (no X11/xcb). The Vulkan
  loader and headers come from the **NDK sysroot** (API 28 >= Vulkan 1.1),
  NOT from vcpkg: Vulkan on a device is driver-tier, the same doctrine as
  MoltenVK on macOS (a platform-provided driver, not a vendored library).
  `vulkan-headers`/`vulkan-loader` stay `platform: linux` in the manifest
  (vcpkg's `vulkan-loader` is `supports: !android` anyway); glslang IS a vcpkg
  dependency here (`platform: "linux | android"`) for the runtime compile. The
  shipped config carries `Vulkan::Vulkan;glslang::glslang;glslang::SPIRV`
  (CMake's built-in `FindVulkan` resolves the NDK loader into `Vulkan::Vulkan`).
- **macOS / iOS** - Metal only, `CMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON` for
  hermeticity. iOS additionally sets `OGRE_BUILD_PLATFORM_APPLE_IOS=ON` in the
  portfile (Ogre-Next's own iOS switch, a plain option upstream never sets from
  the toolchain - selects the UIKit platform sources and codec set), which
  keeps the `arm64-ios-simulator` triplet untouched. The Metal RS config
  interface is `-framework Metal;-framework AppKit;-framework QuartzCore` on
  macOS, `-framework Metal;-framework QuartzCore` on iOS (no AppKit).

All four also build the NULL (headless) render system, Hlms PBS + Unlit
components and rapidjson (a hard OgreMain 3.0 dependency: OgreRootLayout.cpp
includes it unconditionally). The NULL RS needs no portfile switch and has no
`OGRE_BUILD_RENDERSYSTEM_NULL` to set: upstream's `RenderSystems/CMakeLists.txt`
gates every other render system behind an option but adds this one
unconditionally, so it is present on every triplet as a property of the
upstream build rather than a choice this port makes.

**Image codec** is the in-tree STBI codec on every platform
(`OGRE_CONFIG_ENABLE_STBI=ON`), which reads the formats engine assets arrive in
(png, jpg, tga, bmp and friends) and is decode-only. Nothing here needs to
encode: writing an image is the engine's own job, and the render backend turns
a texture readback into a PNG through `core_util/PngWriter`
(`RenderBackend::saveImageAsPng`). That keeps the FreeImage closure - LibRaw,
OpenEXR, LibTIFF, JasPer and the rest - out of every triplet.

**Silent-disable guard**: `OGRE_BUILD_RENDERSYSTEM_VULKAN` is a
`cmake_dependent_option` gated on `Vulkan_FOUND`, so a failing Vulkan find-probe
would drop the whole RS and STILL let the build complete "successfully" (only a
feature-summary log line), surfacing later only as a missing interface include
dir in the consumer's generate step. The portfile asserts (FATAL_ERROR) after
install, on the Vulkan platforms, that `libRenderSystem_VulkanStatic.a` and the
`RenderSystems/Vulkan/include` header dir actually landed - failing in the port
build instead. (The Linux/Vulkan build is proven end-to-end by the
`linux-next` CI job - it renders the full windowed desktop suite under Mesa
lavapipe.)

The **Atmosphere** component is ON (`OGRE_BUILD_COMPONENT_ATMOSPHERE`): its
`AtmosphereNpr` is the sky dome + HlmsPbs-integrated object fog + sun-linkage
solution the engine_render environment surface wires
(`RenderWorld::setAtmosphere`, `Docs/render-abstraction.md`). It exports the
`OgreNext::Atmosphere` target (lib `OgreNextAtmosphereStatic`, headers under
`include/OGRE-Next/Atmosphere`). Its **sky material media** is installed beside
the Hlms templates under `share/ogre-next/Media/Atmosphere` - only the sky's own
files (`Atmosphere.material`, a trimmed `AtmosphereQuad.program` in place of the
samples' full `Quad.program`, the `AtmosphereNprSky_ps` fragment shader +
`QuadCameraDirNoUV_vs` vertex shader per shading language, and the shared
`Any/AtmosphereNprSky_ps.any` include), NOT the whole samples Common material set
(which carries unrelated effects + heavyweight LUT `.dds` files). Two of those
files are PORT-DIR copies rather than source-tree copies:
`AtmosphereQuad.program` carries the `default_params` block the samples'
`Quad.program` binds (`worldViewProj` + `rsDepthRange` auto params - without
them the sky quad transforms by a zero matrix and silently never renders), and
`Any/AtmosphereNprSky_ps.any` appends an in-shader gamma encode (`sqrt`) to the
linear sky colour, because this engine renders into a non-sRGB swapchain with
no hardware gamma-on-write (the same encode the patched HlmsPbs applies to lit
content - see `pbs-honour-non-srgb-target.patch` below). The runtime
registers `Media/Atmosphere` (the script dir plus each per-language shader subdir
as its own location, so a script's bare `source X.metal` and shader includes
resolve) alongside the Hlms media at boot; the HlmsPbs object-fog integration
pieces (`Pbs/Any/Atmosphere/*.any`) already ride in the shipped Hlms Pbs
templates.

The same media set also carries the **cubemap sky material**
`SkyCubemap.material` (a port-dir trim of the samples' `Sky.material` to the
cubemap method only - the equirectangular method and its sources stay out)
plus the `SkyCubemap_ps` fragment sources per shading language. It defines the
`Ogre/Sky/Cubemap` material `SceneManager::setSky(SkyCubemap)` loads by name -
the atmosphere's `skybox` sky type (`AtmosphereDesc::skyType`,
`Docs/render-abstraction.md`); the vertex program is the shipped
`QuadCameraDirNoUV_vs`. The cubemap texel is emitted as-is (no gamma encode):
skybox content is authored gamma-space artwork, the raw sample IS the colour -
the classic flavor's sky box multiplies nothing either, which keeps the one
`.dds` pixel-comparable across flavors.

Overlay/samples/tools and all other components OFF until a consumer needs them;
zip archives OFF (would add zziplib - revisit when content work needs
`addResourceLocation(LT_ZIP)`).

Upstream installs **no CMake package config** (only pkg-config templates
whose static .pc unconditionally `Requires: gl` - removed); the port ships
its own `OGRE-NextConfig.cmake` with namespaced imported targets
(`OgreNext::Main`, `OgreNext::HlmsPbs`, `OgreNext::HlmsUnlit`,
`OgreNext::Atmosphere`,
`OgreNext::RenderSystem_Metal` on Apple / `OgreNext::RenderSystem_Vulkan`
on Linux and Android, `OgreNext::RenderSystem_NULL`) plus
`OGRE_NEXT_MEDIA_DIR` (the shipped `Media/Hlms` shader templates every
Ogre-Next app must register). The config detects the consumer's platform
(`CMAKE_SYSTEM_NAME` = `iOS`/`Android`) to pick the per-platform link
interfaces: macOS vs iOS platform frameworks on `OgreNext::Main` (iOS uses
Foundation/UIKit/QuartzCore/CoreGraphics, no Cocoa/Carbon/IOKit) and Linux xcb
libs vs Android's xcb-free Vulkan interface.

Patches (the same Xcode-oriented-CMake class as classic's ios/metal patches):

- `apple-ninja-objcxx-sysroot.patch` - upstream assumes Xcode on Apple:
  (a) enable OBJC/OBJCXX so the `.mm` sources (OgreMain/src/OSX,
  RenderSystem_Metal) compile under single-config generators; (b) do not
  clobber the iOS `CMAKE_OSX_SYSROOT` with the symbolic "iphoneos" SDK name
  after `project()` (Ninja passes `-isysroot` verbatim; the hunk stops
  upstream from overwriting the simulator triplet's `iphonesimulator`
  sysroot). The equivalent macOS "macosx" hunk this patch used to carry is
  upstream now - master guards that block with `if(NOT CMAKE_OSX_SYSROOT)`,
  and CMake resolves the sysroot to a real path before `project()`, so the
  guard skips it; the iOS block is still unguarded upstream; (c) mirror
  upstream's `-DDEBUG=1` debug flag into the OBJCXX/OBJC debug flags -
  `OGRE_DEBUG_MODE` is ABI-relevant (`generateAbiCookie`) and the Metal
  plugin's ObjC++ TUs must agree with OgreMain's C++ TUs.
  (A second patch this port used to carry, guarding OgreMain's
  framework-header POST_BUILD behind `OGRE_BUILD_LIBS_AS_FRAMEWORKS`, was
  dropped when the pin moved to master: upstream added the identical guard.)
- `vulkan-no-shaderc-probe.patch` - ogre-next's bundled
  `CMake/Packages/FindVulkan.cmake` requires `libshaderc_combined` (via
  `Vulkan_SHADERC_LIB_REL`/`_DBG`) in its `find_package_handle_standard_args`,
  a Windows-Vulkan-SDK-ism. The Vulkan RS compiles GLSL to SPIR-V
  through **glslang only** (`OgreVulkanProgram.cpp` uses `glslang/Public/
  ShaderLang.h` and self-declares the `GlslangToSpv` prototypes - zero shaderc
  usage under `RenderSystems/Vulkan/`), and neither vcpkg nor the NDK ships
  shaderc_combined, so the probe failed, `Vulkan_FOUND` went false, and the
  `cmake_dependent_option` silently dropped the RS. The patch drops the two
  shaderc vars from the probe and only appends the `optimized`/`debug` shaderc
  entries to `Vulkan_LIBRARIES` when found (an absent shaderc otherwise leaves
  a dangling `optimized` keyword that reaches `target_link_libraries` and
  hard-errors). Needed on both Linux and Android.
- `lib-install-path.patch` - `CMake/Utils/OgreConfigTargets.cmake`
  installs iOS release static libs into `lib/Release` (an Xcode-layout
  leftover); drop that so iOS keeps the standard vcpkg `lib/` layout the
  shipped config's `lib/lib*.a` paths expect. (The `$(PLATFORM_NAME)`-under-
  Ninja output-path problem classic patched is already fixed upstream in
  ogre-next - `OgreConfigTargets.cmake` excludes Ninja.)
- `pbs-honour-non-srgb-target.patch` - upstream candidate (submitted as
  OGRECave/ogre-next #584). HlmsPbs
  hardcodes `hw_gamma_write` to 1 in `preparePassHash`, assuming an sRGB
  colour target - on a UNORM swapchain (this engine's deliberate classic
  colour-parity convention) the LINEAR lighting result lands raw and every
  lit surface displays gamma-crushed. The patch derives the property from
  the live pass descriptor's colour format
  (`PixelFormatGpuUtils::isSRgb`), which engages the stock template's
  in-shader `sqrt` encode (`!hw_gamma_write`) on non-sRGB targets. HlmsUnlit
  is deliberately untouched (its raw passthrough IS the 2D parity
  convention).
- One former upstream-candidate patch was CONSUMED at the 2026-08-01 pin bump,
  its PR having merged into the range this REF spans - it now comes from
  upstream through the pin, the same lifecycle the classic port's patches
  follow: `hlms-tls-init-symbol-visibility.patch` (OGRECave/ogre-next #586, the
  merge commit this REF names). `Hlms::msThreadId` is a `thread_local` static
  member whose definition in `OgreHlms.cpp` is constant-initialized; a
  translation unit seeing only the declaration cannot know that, so it emits the
  Itanium ABI access wrapper `_ZTW...` weakly referencing the thread-local init
  function `_ZTHN4Ogre4Hlms10msThreadIdE` - a symbol a constant-initialized
  variable never defines. Resolving that dangling weak reference to zero needs a
  GOT entry, which clang only emits for a symbol that may bind externally;
  `OGRE_SHADER_THREADING_USE_TLS` is a static-build-only setting and
  `_OgreExport` is `visibility("hidden")` there, so clang addressed the symbol
  directly and the object carried `R_X86_64_PC32`, which GNU ld refuses to link
  into a PIE - clang-on-x86-64 only, which is exactly the Release Linux
  configuration. Upstream now declares the member with explicit default
  visibility under `OGRE_GCC_VISIBILITY`, restoring the GOT indirection every
  other configuration already uses.
- Future upstream-candidate fixes follow the same lifecycle this one did: vendor
  the patch in the port the same day the PR goes upstream, then drop the patch
  file at the next reviewed pin bump once it is merged.

Debug/release note: vcpkg ships ONE header tree for both configs, but
ogre-next's generated `OgreBuildSettings.h` bakes `OGRE_DEBUG_MODE` per build
type under single-config generators - a debug consumer compiling against the
release header while linking the debug lib is a REAL ABI break (v2 debug
bookkeeping changes struct layouts; observed as a scene-node crash). The port
builds with `OGRE_EMBED_DEBUG_MODE=never` (level derived from
`_DEBUG`/`DEBUG`/`NDEBUG` at compile time) and the shipped config propagates
`$<$<CONFIG:Debug>:DEBUG=1;_DEBUG=1>` on `OgreNext::Main` so consumers always
match the libs.

## ports/sol2

Overlay of the upstream vcpkg `sol2` port (3.5.0#1). Delete this overlay once
upstream carries an equivalent fix. Local addition:

- `clang18-noexcept-member-variable.patch`: clang >= 18 (the NDK r27
  toolchain used by the android-debug preset) rejects
  `lua_CFunction freefunc = &upvalue_this_member_variable<...>::call<...>`
  because those `call`/`real_call`/`operator()` templates carry a
  `noexcept(std::is_nothrow_copy_assignable_v<T>)` specifier - the address
  of a noexcept function no longer matches the plain `int(lua_State*)`
  target type under clang's stricter overload resolution
  (upstream: sol2 issues #1581, #1678; hits every usertype MEMBER-VARIABLE
  binding, i.e. every OVAR in core_base/Meta_Lua.h). The noexcept there is
  cosmetic (the functions call luaL_error/trampolines anyway); the patch
  drops it. Upstream-candidate.

## ports/imgui

The stock vcpkg imgui port (docking branch, the editor's UI library) plus one
behavioral patch:

- `selected-tab-ignores-hover.patch` — a SELECTED tab keeps its own colour
  while the cursor is over it; the hover colour applies only to UNSELECTED
  tabs (and the held/drag state keeps its feedback everywhere). The editor
  theme paints the selected tab in the exact panel-body colour so tab and
  content read as one connected surface — a hover flash on the active tab
  would break that surface apart. Upstream has no per-state colour slot for
  a hovered-selected tab (one `ImGuiCol_TabHovered` covers all tabs), so
  the split lives in the tab render.

## ports/imgui-color-text-edit

The Script panel's code-editor widget (`goossens/ImGuiColorTextEdit` — the
actively maintained rework of the classic syntax-highlighting editor widget:
language tokenizers, markers, line decorators, find/replace, autocomplete
hooks), pinned to a known-good commit because the upstream repo tags no
releases and ships no library CMake. Not in the vcpkg registry, hence the
overlay port — same conventions as `ports/imgui`:

- our `CMakeLists.txt` + config template are installed OVER the source; the
  static lib builds from `TextEditor.{h,cpp}` ONLY. `TextDiff.*` and its
  bundled `dtl.h` diff library are deliberately omitted (unused by the
  editor, and leaving them out keeps the port's license inventory MIT-only).
- PUBLIC dependency on `imgui::imgui`, so it always compiles against the
  same docking imgui the editor uses (the widget speaks the modern key API;
  it needs no imgui internals and no texture API). `IMGUI_USE_WCHAR32`
  intentionally matches the imgui port's configuration (unset — the widget
  works on `ImWchar` either way, but the two must agree).
- `cxx_std_17` (the widget's own requirement; the engine builds C++20).

Consumed only by `orkige_editor` (desktop platforms — the same
`!ios & !android & !emscripten` gate as imgui itself).

## ports/libvterm

The embedded Terminal panel's VT screen model
(`tools/editor/EditorTerminalScreen.cpp`). `libvterm` is the LeoNerd/neovim
abstract terminal library — the callback-driven, allocation-free-in-steady-state
VT220/xterm/ECMA-48 screen model that neovim's `:terminal` uses (its existence
proof is a shell — and Claude Code — running inside neovim every day). Pinned to
a neovim-fork commit; not in the vcpkg registry, hence the overlay port.

NAMING TRAP recorded so nobody swaps it: there is an unrelated,
ncurses/ROTE-based project that also calls itself `libvterm`
(TragicWarrior/libvterm). This is NOT that one — it is the C99 abstract
screen-model library.

- upstream ships no library CMake (a Makefile plus a Perl step that turns the
  encoding `.tbl` tables into `.inc` headers). The neovim fork COMMITS those
  generated `.inc` files (`src/encoding/*.inc`, `src/fullwidth.inc`), so our
  `CMakeLists.txt` (installed over the source, the `ports/imgui` convention) is
  a plain C99 compile of `src/*.c` with NO Perl at build time — it builds on
  arm64-osx, x64-linux and x64-windows-static-md (MSVC-clean: the fork is
  maintained against MSVC; a `_CRT_SECURE_NO_WARNINGS` define quiets the
  secure-CRT noise). Static only.
- the library exports `libvterm::vterm`. It links PRIVATE into
  `orkige_editor_core`: the header `EditorTerminalScreen.h` exposes only the
  editor's own cell/grid/cursor vocabulary, and every libvterm type and call is
  confined to the one implementation TU (the `FontBakeImpl.cpp` single-file-lib
  precedent). That confinement is the swap seam — if a maturing pure-VT library
  grows a stable API and a Windows build it can replace libvterm without a
  caller changing.

Consumed only by the editor (desktop platforms — the same
`!ios & !android & !emscripten` gate as imgui).

## nanosvg (stock port — no overlay)

`nanosvg` is a plain vcpkg-registry dependency (`vcpkg.json`), NOT an overlay
port — it needs no patch. Recorded here only so the choice has a rationale:
it is the ONE SVG reader in the tree, with two consumers:
`engine_gui/FontAtlas` rasterises `.svg` UI sprites into the runtime font
atlas at boot, and `engine_gui/SvgShapeCook` flattens an imported drawing into
the native `.oshape` — so the editor imports a vector shape with no interpreter
and no subprocess. nanosvg is a tiny, permissively-licensed (Zlib) single-file
SVG parser + rasteriser. The vcpkg port precompiles the implementation into
static libs (`NanoSVG::nanosvg` / `NanoSVG::nanosvgrast`), so — unlike the
header-only `stb` libs — nothing defines `NANOSVG_IMPLEMENTATION`; the engine
just links the targets. Its headers are confined to exactly two TUs
(`engine_gui/SvgRasterImpl.cpp` and `engine_gui/SvgShapeCookImpl.cpp`, the
`StbVorbisImpl.cpp` precedent) so the library stays out of every header and the
precompiled header. `SvgShapeCook.h` depends on `orkige_core` alone, so the
host-side `tools/shapecook` CLI compiles that one TU without the engine closure. The matching
glyph rasteriser is `stb_truetype.h` from the already-vendored `stb` port,
confined to `engine_gui/FontBakeImpl.cpp`.
