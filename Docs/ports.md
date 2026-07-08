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
never builds it. Static, `supports: osx & arm64` for now (widened when the
Phase-3 mobile evaluation decides backends).

**Coexistence with classic `ogre` in one installed tree** is a hard
requirement and holds by construction: `OGRE_USE_NEW_PROJECT_NAME=ON` gives
`include/OGRE-Next/` headers and `libOgreNext*Static.a` /
`libRenderSystem_MetalStatic.a` lib names (classic: `include/OGRE/`,
`libOgre*.a`, `libRenderSystem_Metal.a` - file-disjoint even for the Metal
RS), CMake config + HLMS media live under `share/ogre-next/`
(classic: `share/ogre/`). Verified live: a `render-next` tree (the default
`macos-debug`/`macos-release` presets since the 2026-07-08 default flip)
installs both into one `vcpkg_installed` tree.

Configuration: Metal render system only (first-class on Ogre-Next; the legacy
GL3+ 4.1 path buys nothing on macOS) plus the NULL render system (headless
option), Hlms PBS + Unlit components, FreeImage codec (the ogre-next STBI
codec is decode-only - screenshots need an encoder), rapidjson (a hard
OgreMain 3.0 dependency: OgreRootLayout.cpp includes it unconditionally).
Overlay/samples/tools and all other components OFF until a phase needs them;
zip archives OFF (would add zziplib - revisit in B2 when content work needs
`addResourceLocation(LT_ZIP)`).

Upstream installs **no CMake package config** (only pkg-config templates
whose static .pc unconditionally `Requires: gl` - removed); the port ships
its own `OGRE-NextConfig.cmake` with namespaced imported targets
(`OgreNext::Main`, `OgreNext::HlmsPbs`, `OgreNext::HlmsUnlit`,
`OgreNext::RenderSystem_Metal`, `OgreNext::RenderSystem_NULL`) plus
`OGRE_NEXT_MEDIA_DIR` (the shipped `Media/Hlms` shader templates every
Ogre-Next app must register).

Patches (the same Xcode-oriented-CMake class as classic's ios/metal patches):

- `apple-ninja-objcxx-sysroot.patch` - upstream assumes Xcode on Apple:
  (a) enable OBJC/OBJCXX so the `.mm` sources (OgreMain/src/OSX,
  RenderSystem_Metal) compile under single-config generators; (b) do not
  clobber `CMAKE_OSX_SYSROOT` with the symbolic "macosx" after `project()`
  (Ninja passes `-isysroot` verbatim; resolve via xcodebuild only when
  unset); (c) mirror upstream's `-DDEBUG=1` debug flag into the
  OBJCXX/OBJC debug flags - `OGRE_DEBUG_MODE` is ABI-relevant
  (`generateAbiCookie`) and the Metal plugin's ObjC++ TUs must agree with
  OgreMain's C++ TUs.
- `apple-ninja-no-framework-postbuild.patch` - macOS: OgreMain's
  framework-header POST_BUILD step uses Xcode `$(PLATFORM_NAME)` variables
  (literal `$(...)` under Ninja); guard it behind
  `OGRE_BUILD_LIBS_AS_FRAMEWORKS`.

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
