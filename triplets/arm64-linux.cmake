# Overlay triplet: the stock community arm64-linux (static libs, dynamic
# CRT), plus the same Linux fix as the x64-linux overlay. It REPLACES vcpkg's
# built-in arm64-linux, so the four stock settings below are replicated
# verbatim - only VCPKG_CMAKE_CONFIGURE_OPTIONS is added.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# Same glibc iconv trap as x64-linux (see that triplet for the full story):
# SDL3's "system iconv" path links a standalone -liconv that does not exist
# on glibc. SDL_SYSTEM_ICONV=OFF selects SDL's self-contained builtin
# SDL_iconv; both flags are harmless to the other ports.
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DSDL_SYSTEM_ICONV=OFF -DSDL_LIBICONV=OFF)
