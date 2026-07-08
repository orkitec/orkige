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
