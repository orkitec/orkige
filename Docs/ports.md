# Overlay ports — rationale

Full documentation for the vcpkg overlay ports in `ports/` (wired via
`VCPKG_OVERLAY_PORTS` in CMakePresets.json). The prose lives HERE, not in the
port directories: vcpkg hashes every byte of a port dir into the port's ABI
hash, so editing a README inside `ports/<name>/` forces a full rebuild of that
port on every triplet (macOS + iOS + Android). Keep the in-port READMEs to a
single pointer line and batch any real port edits (see "Build speed" in
CLAUDE.md).

## ports/ogre

Overlay of the upstream vcpkg `ogre` port, repinned from the v14.5.2 release
tag to master commit `7d25ffc3d7e1119160e5f0f23037c9577c916e96` (2026-07-12,
declares itself 14.6.0; `version-date` in the port's vcpkg.json) - the first
reviewed pin that CONTAINS our three merged upstream PRs (OGRECave/ogre
#3667/#3668/#3669, merged 2026-07-07/08). No release tag carries them yet;
move the REF back to a release tag when one does. The pin moves like
ogre-next's: a reviewed bump, full-suite verified, never implicit. Enabled
from the root `vcpkg.json`. Delete this overlay if upstream ever grows
equivalent features. Local additions:

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
- `zip-entry-open-nonstrict.patch` - upstream candidate (submitted as
  OGRECave/ogre #3673): master's
  `OGRE_RESOURCEMANAGER_STRICT=0` fallback lookup in `ZipArchive::open` calls
  a three-argument `zip_entry_open` the bundled zip library does not declare
  (its case-sensitive variant is the separate `zip_entry_opencasesensitive`).
  Upstream CI builds strict, which preprocesses the branch out; the port
  builds non-strict (no `strict` feature requested), so the branch must
  compile. Inside that `#else` branch the strict flag is 0 by definition -
  the two-argument case-insensitive call is behavior-identical.
- `manual-render-null-renderable.patch` - upstream candidate (submitted as
  OGRECave/ogre #3674):
  `SceneManager::manualRender(RenderOperation*, ...)` resets the auto-param
  state with `setCurrentRenderable(0)` (the documented "matrices supplied
  explicitly" contract the null-tolerant
  `AutoParamDataSource::getViewMatrix`/`getProjectionMatrix` accessors
  honor), but the GpuParamsDirty refactor on master dereferences the
  renderable unconditionally there - every `manualRender(RenderOperation*)`
  call asserts in debug and would null-deref in release. The classic
  `DrawLayer2D` backend draws each 2D element through exactly that
  `manualRender` overload, so the whole gui/editor/UI surface died on it.
  The patch treats a null renderable as "no identity view/projection".
- `cpufeatures-build-interface.patch` - upstream candidate (submitted as
  OGRECave/ogre #3675): master's
  `target_link_libraries(OgreMain PRIVATE cpufeatures log z)` (new since
  14.5.2) leaks the NDK's source-only `cpufeatures` module - an internal,
  never-exported CMake target - onto static OgreMain's installed link
  interface as a bare `-lcpufeatures` no consumer can resolve (the NDK
  ships it as a source module, not a library); every Android link against
  the vcpkg-installed OgreTargets failed. The patch wraps the dependency in
  `$<BUILD_INTERFACE:...>`: OGRE's own tree builds unchanged, and installed
  consumers keep the long-standing contract of compiling
  `cpu-features.c` themselves (our `orkige_ndk_cpufeatures` in
  `orkige_engine/CMakeLists.txt`).
- Future upstream-candidate fixes follow the same lifecycle those three had:
  vendor the patch in the port the same day the PR goes upstream, then drop
  the patch file at the next reviewed pin bump once it is merged.
- `fix-dependencies.patch` (upstream vcpkg patch, locally amended): the
  OGREConfig.cmake template's `find_dependency(SDL2 CONFIG)` is now guarded
  by `if(NOT "@ANDROID@" AND NOT "@EMSCRIPTEN@")` - the same condition OGRE's
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

## ports/ogre-next

Locally authored port (pinned master commit `ef2e8f35c3ac929b06f67c76cbc80c5577016b30`,
2026-07-16, declared as `version-date` in the port's vcpkg.json; no upstream
vcpkg port exists). Master over the v3.0.0 tag by decision: upstream maintains
only master (no patch releases since the 2024 tag) and it carries Vulkan
hardening the engine wants - device-loss recovery in `VulkanRootLayout`, an
Adreno 6xx workaround - plus it absorbed part of this port's Apple patches
(below). The current pin contains our merged OGRECave/ogre-next #582 (the
NEON Math/Array include-order fix, merged 2026-07-15), which un-breaks the
arm64 Linux build - the `linux-debug-sanitize` preset in the Linux rig
container cold-builds this port natively now. The pin moves deliberately (a
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
`macos-debug`/`macos-release` presets since the 2026-07-08 default flip)
installs both into one `vcpkg_installed` tree.

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

All four also build the NULL render system (headless option), Hlms PBS + Unlit
components and rapidjson (a hard OgreMain 3.0 dependency: OgreRootLayout.cpp
includes it unconditionally).

**Image codec** is FreeImage on desktop (decode + encode - screenshots need an
encoder). Mobile drops the FreeImage dependency (`platform: "!ios & !android"`
in the manifest, matching the classic mobile flavor) and builds the in-tree
STBI codec (`OGRE_CONFIG_ENABLE_STBI=ON`, decode-only - all device asset
loading needs). A screenshot-based device check would have to account for the
missing encoder, same as classic mobile.

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
Foundation/UIKit/QuartzCore/CoreGraphics, no Cocoa/Carbon/IOKit), Linux xcb
libs vs Android's xcb-free Vulkan interface, and it drops
`freeimage::FreeImage` (and its `find_dependency`) on mobile.

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
- `pbs-honour-non-srgb-target.patch` - upstream candidate. HlmsPbs
  hardcodes `hw_gamma_write` to 1 in `preparePassHash`, assuming an sRGB
  colour target - on a UNORM swapchain (this engine's deliberate classic
  colour-parity convention) the LINEAR lighting result lands raw and every
  lit surface displays gamma-crushed. The patch derives the property from
  the live pass descriptor's colour format
  (`PixelFormatGpuUtils::isSRgb`), which engages the stock template's
  in-shader `sqrt` encode (`!hw_gamma_write`) on non-sRGB targets. HlmsUnlit
  is deliberately untouched (its raw passthrough IS the 2D parity
  convention).

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

## nanosvg (stock port — no overlay)

`nanosvg` is a plain vcpkg-registry dependency (`vcpkg.json`), NOT an overlay
port — it needs no patch. Recorded here only so the choice has a rationale:
`engine_gui/FontAtlas` rasterises `.svg` UI sprites into the runtime font
atlas at boot, and nanosvg is a tiny, permissively-licensed (Zlib) single-file
SVG parser + rasteriser. The vcpkg port precompiles the implementation into
static libs (`NanoSVG::nanosvg` / `NanoSVG::nanosvgrast`), so — unlike the
header-only `stb` libs — nothing defines `NANOSVG_IMPLEMENTATION`; the engine
just links the targets. Its headers are still confined to a single TU
(`engine_gui/SvgRasterImpl.cpp`, the `StbVorbisImpl.cpp` precedent) so the
library stays out of every header and the precompiled header. The matching
glyph rasteriser is `stb_truetype.h` from the already-vendored `stb` port,
confined to `engine_gui/FontBakeImpl.cpp`.
