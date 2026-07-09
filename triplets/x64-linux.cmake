# Overlay triplet: the stock community x64-linux (static libs, dynamic CRT),
# plus one Linux-CI fix. It REPLACES vcpkg's built-in x64-linux, so the four
# stock settings below are replicated verbatim - only VCPKG_CMAKE_CONFIGURE_
# OPTIONS is added.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# SDL3 otherwise emits a standalone -liconv on its link line, which does not
# exist on a glibc system (iconv lives inside glibc itself) - the cold CI build
# dies with "/usr/bin/ld: cannot find -liconv". SDL's own CMakeCache confirms
# the trap: ICONV_IN_LIBC=Success and SDL_LIBICONV=OFF (so it is NOT preferring
# a standalone libiconv), yet SDL_SYSTEM_ICONV=ON still links -liconv for the
# "system iconv" path. SDL_SYSTEM_ICONV=OFF selects SDL's fully self-contained
# builtin SDL_iconv (no external link at all) - the definitive fix. Both flags
# are SDL-specific but harmless to the other ports (CMake ignores an unused -D).
# macOS/iOS/Android are unaffected - they use their own triplets and
# Homebrew/NDK provide iconv.
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DSDL_SYSTEM_ICONV=OFF -DSDL_LIBICONV=OFF)
