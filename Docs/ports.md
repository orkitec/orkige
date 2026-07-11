# Overlay ports — rationale

Full documentation for the vcpkg overlay ports in `ports/` (wired via
`VCPKG_OVERLAY_PORTS` in CMakePresets.json). The prose lives HERE, not in the
port directories: vcpkg hashes every byte of a port dir into the port's ABI
hash, so editing a README inside `ports/<name>/` forces a full rebuild of that
port on every triplet (macOS + iOS + Android). Keep the in-port READMEs to a
single pointer line and batch any real port edits (see "Build speed" in
CLAUDE.md).

## ports/ogre

Overlay of the upstream vcpkg `ogre` port (14.5.2). Enabled from the root
`vcpkg.json`. Delete this overlay if upstream ever grows equivalent features.
Local additions:

- `metal` feature (`OGRE_BUILD_RENDERSYSTEM_METAL=ON`, Apple platforms) so
  RenderSystem_Metal is available next to GL3Plus - the upstream port has no
  way to enable it. Plus `metal-export-include-dirs.patch` (upstream candidate,
  see Docs/upstream) so the exported target carries its include dirs.
- `vulkan` feature (`OGRE_BUILD_RENDERSYSTEM_VULKAN=ON` +
  `OGRE_BUILD_PLUGIN_GLSLANG=ON`, deps: vulkan-headers, vulkan-loader,
  glslang). Three patches:
  - `vulkan-metal-surface.patch` - upstream candidate (Docs/upstream): adds the
    missing VK_EXT_metal_surface window branch so the Vulkan RS runs on
    macOS/iOS through MoltenVK, incl. VK_KHR_portability_enumeration/subset
    handling. MoltenVK itself is driver-tier and comes from Homebrew (see
    CLAUDE.md); static MoltenVK packaging into the app bundle is an iOS-phase
    item (see the feature description in vcpkg.json).
  - `vulkan-shutdown-call-base.patch` - upstream candidate (Docs/upstream):
    Vulkan RS must call RenderSystem::shutdown() like every other RS, else
    debug builds abort on exit (VMA leak assertion).
  - `vulkan-vcpkg-deps.patch` - vcpkg-only: resolve vulkan-headers/glslang
    through their CMake configs and re-export them from OGREConfig.cmake.
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

Locally authored port (3.0.0, pinned tag `v3.0.0` - the latest stable of
OGRECave/ogre-next; no upstream vcpkg port exists). The Ogre-Next backend of
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
build instead. TODO(linux): the Linux/Vulkan build is authored against the
3.0.0 sources but first proven by the `linux-next` CI job - glslang API drift
between ogre-next 3.0 and the current vcpkg glslang is the known risk.

Overlay/samples/tools and all other components OFF until a phase needs them;
zip archives OFF (would add zziplib - revisit when content work needs
`addResourceLocation(LT_ZIP)`).

Upstream installs **no CMake package config** (only pkg-config templates
whose static .pc unconditionally `Requires: gl` - removed); the port ships
its own `OGRE-NextConfig.cmake` with namespaced imported targets
(`OgreNext::Main`, `OgreNext::HlmsPbs`, `OgreNext::HlmsUnlit`,
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
  clobber `CMAKE_OSX_SYSROOT` with a symbolic SDK name after `project()`
  (Ninja passes `-isysroot` verbatim; resolve via xcodebuild only when unset)
  - carried for BOTH the macOS "macosx" block and the iOS "iphoneos" block
  (the iOS hunk stops upstream from overwriting the simulator triplet's
  `iphonesimulator` sysroot with `iphoneos` under Ninja); (c) mirror
  upstream's `-DDEBUG=1` debug flag into the OBJCXX/OBJC debug flags -
  `OGRE_DEBUG_MODE` is ABI-relevant (`generateAbiCookie`) and the Metal
  plugin's ObjC++ TUs must agree with OgreMain's C++ TUs.
- `apple-ninja-no-framework-postbuild.patch` - macOS: OgreMain's
  framework-header POST_BUILD step uses Xcode `$(PLATFORM_NAME)` variables
  (literal `$(...)` under Ninja); guard it behind
  `OGRE_BUILD_LIBS_AS_FRAMEWORKS`.
- `vulkan-no-shaderc-probe.patch` - ogre-next's bundled
  `CMake/Packages/FindVulkan.cmake` requires `libshaderc_combined` (via
  `Vulkan_SHADERC_LIB_REL`/`_DBG`) in its `find_package_handle_standard_args`,
  a Windows-Vulkan-SDK-ism. The v3.0.0 Vulkan RS compiles GLSL to SPIR-V
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
`engine_fastgui/FontAtlas` rasterises `.svg` UI sprites into the runtime font
atlas at boot, and nanosvg is a tiny, permissively-licensed (Zlib) single-file
SVG parser + rasteriser. The vcpkg port precompiles the implementation into
static libs (`NanoSVG::nanosvg` / `NanoSVG::nanosvgrast`), so — unlike the
header-only `stb` libs — nothing defines `NANOSVG_IMPLEMENTATION`; the engine
just links the targets. Its headers are still confined to a single TU
(`engine_fastgui/SvgRasterImpl.cpp`, the `StbVorbisImpl.cpp` precedent) so the
library stays out of every header and the precompiled header. The matching
glyph rasteriser is `stb_truetype.h` from the already-vendored `stb` port,
confined to `engine_fastgui/FontBakeImpl.cpp`.
