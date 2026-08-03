# orkige_core - platform- and renderer-independent engine core.

set(ORKIGE_CORE_MODULES
    core_base
    core_debug
    core_debugnet
    core_event
    core_filesystem
    core_game
    core_http
    core_module
    core_project
    core_script
    core_serialization
    core_tween
    core_util
)

set(ORKIGE_CORE_SOURCES "")
foreach(module IN LISTS ORKIGE_CORE_MODULES)
    file(GLOB module_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.h"
    )
    list(APPEND ORKIGE_CORE_SOURCES ${module_sources})
endforeach()

if(NOT ORKIGE_SCRIPTING STREQUAL "LUA")
    # the neutral core_script/ScriptRuntime.h seam is ALWAYS compiled (it is
    # what keeps call sites free of ORKIGE_LUA guards); only the sol2-backed
    # Lua backend behind it drops out of no-scripting builds
    list(REMOVE_ITEM ORKIGE_CORE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/core_script/ScriptManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/core_script/ScriptManager.h"
    )
endif()

if(APPLE)
    file(GLOB core_objcxx_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/core_util/*.mm"
    )
    list(APPEND ORKIGE_CORE_SOURCES ${core_objcxx_sources})
endif()

# --- the HTTP client's platform transport ------------------------------------
# Exactly ONE backend translation unit is compiled, chosen here so no call site
# above the HttpBackend seam carries a platform #ifdef (the
# engine_input/HapticManager + HapticBridgeApple.mm split). Each backend also
# carries its own ORKIGE_HTTP_* guard, so a stray addition to the glob above
# still compiles to nothing.
#
#   Apple (macOS/iOS)         NSURLSession  - the system trust store, and no
#                                             third-party library at all
#   Android                   the platform's own HTTP stack over JNI - the
#                                             device's trust anchors INCLUDING
#                                             user and enterprise ones, the
#                                             app's network security config and
#                                             its proxy settings, and no
#                                             library in the APK at all
#   Windows                   WinHTTP       - Schannel over the machine's own
#                                             certificate stores (enterprise
#                                             and hand-installed anchors
#                                             included) plus the system AND
#                                             per-user proxy configuration,
#                                             from a DLL that ships with the OS
#   Linux                     libcurl       - OpenSSL over the system CA store,
#                                             http+https only
#   wasm                      fetch         - a wasm module has no sockets
#
# ORKIGE_HTTP=OFF drops every transport (the seam still compiles, and
# HttpClient::available() answers false with an honest reason) - the lever for
# a size-constrained target that ships no networking. See Docs/http.md.
list(REMOVE_ITEM ORKIGE_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendAndroid.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendCurl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendFetch.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendNone.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendWin.cpp"
)
set(ORKIGE_HTTP_BACKEND "none")
if(ORKIGE_HTTP)
    if(APPLE)
        set(ORKIGE_HTTP_BACKEND "apple")
        list(APPEND ORKIGE_CORE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendApple.mm")
    elseif(ANDROID)
        set(ORKIGE_HTTP_BACKEND "android")
        list(APPEND ORKIGE_CORE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendAndroid.cpp")
    elseif(EMSCRIPTEN)
        set(ORKIGE_HTTP_BACKEND "fetch")
        list(APPEND ORKIGE_CORE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendFetch.cpp")
    elseif(WIN32)
        set(ORKIGE_HTTP_BACKEND "win")
        list(APPEND ORKIGE_CORE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendWin.cpp")
    else()
        set(ORKIGE_HTTP_BACKEND "curl")
        list(APPEND ORKIGE_CORE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendCurl.cpp")
    endif()
else()
    list(APPEND ORKIGE_CORE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendNone.cpp")
endif()

# the resolved transport, hoisted out of this directory scope so the build-tree
# package can DECLARE it: a native game module links the core archive by path
# and therefore has to satisfy the transport's own dependency itself (libcurl on
# Linux, the WinHTTP import library on Windows), and it must learn WHICH one
# from here rather than re-deriving the platform decision a second time
set(ORKIGE_HTTP_BACKEND "${ORKIGE_HTTP_BACKEND}" CACHE INTERNAL
    "the resolved core_http transport (apple/android/win/curl/fetch/none)")

add_library(orkige_core STATIC ${ORKIGE_CORE_SOURCES})
add_library(Orkige::Core ALIAS orkige_core)

target_include_directories(orkige_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

find_package(tinyxml2 CONFIG REQUIRED)
target_link_libraries(orkige_core PRIVATE tinyxml2::tinyxml2)

# earcut-hpp: single-header polygon triangulation (concave + holes), the one
# library the vector-shape tessellator takes. Header-only, no CMake config -
# the include path is scoped to core_util/VectorTessellator.cpp ALONE (nothing
# else includes it), so the triangulator stays out of every other TU and the
# precompiled header, exactly like engine_sound/StbVorbisImpl.cpp confines
# stb_vorbis.
find_path(EARCUT_INCLUDE_DIR "mapbox/earcut.hpp" REQUIRED)
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/core_util/VectorTessellator.cpp"
    PROPERTIES INCLUDE_DIRECTORIES "${EARCUT_INCLUDE_DIR}")

# DO NOT REMOVE: the asset pipeline's reproducibility depends on this.
#
# The vector-animation cook emits text that is a REPRODUCIBILITY CONTRACT - the
# same source document must cook to the same bytes on every machine, compiler
# and architecture, because a cooked .oanim is a committed asset that gets
# re-cooked on import and diffed. A fused multiply-add rounds ONCE where the
# written arithmetic rounds twice, so with the compiler's default contraction a
# vertex lands on the other side of a printed decimal on one target and not
# another: the same artwork would cook differently for two people. Worse, the
# cook's lengths drive segment counts through a ceil(), so a single contracted
# expression can change a contour's vertex count outright.
#
# Contraction is therefore off for the cook's two translation units alone;
# everything else in the core keeps the default (this is not a numerics
# preference, it is a determinism requirement, and it costs nothing outside
# these files). Removing it fails the byte-identity gate in
# tests/core/VectorAnimCookTests.cpp - which is exactly what that gate is for.
set(ORKIGE_NO_FP_CONTRACT "-ffp-contract=off")
if(MSVC)
    set(ORKIGE_NO_FP_CONTRACT "/fp:precise")
endif()
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/core_util/VectorAnimCook.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core_util/VectorAnimCookGeometry.cpp"
    PROPERTIES COMPILE_OPTIONS "${ORKIGE_NO_FP_CONTRACT}")

if(WIN32)
    # the debug-net socket layer's winsock backend (WSAStartup, closesocket)
    target_link_libraries(orkige_core PUBLIC ws2_32)
endif()

# --- HTTP client: the chosen transport's define + link -----------------------
# ORKIGE_HTTP is PUBLIC (HttpClient::compiled() is inline-visible to consumers
# and the tests assert on it); the per-backend macro is PRIVATE - only the one
# backend TU reads it.
if(ORKIGE_HTTP)
    target_compile_definitions(orkige_core PUBLIC ORKIGE_HTTP)
    if(ORKIGE_HTTP_BACKEND STREQUAL "apple")
        target_compile_definitions(orkige_core PRIVATE ORKIGE_HTTP_APPLE)
    elseif(ORKIGE_HTTP_BACKEND STREQUAL "android")
        target_compile_definitions(orkige_core PRIVATE ORKIGE_HTTP_ANDROID)
        # the JNI entry points come from the NDK's own libraries, which every
        # Android link already carries; the Java half (core_http/OrkigeHttp.java)
        # is compiled into the APK's dex by the exporter's Android assembly
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendAndroid.cpp"
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    elseif(ORKIGE_HTTP_BACKEND STREQUAL "fetch")
        target_compile_definitions(orkige_core PRIVATE ORKIGE_HTTP_FETCH)
        # Emscripten's fetch implementation is opt-in at LINK time; every
        # executable that links this library needs the flag, hence INTERFACE
        target_link_options(orkige_core INTERFACE -sFETCH=1)
    elseif(ORKIGE_HTTP_BACKEND STREQUAL "win")
        target_compile_definitions(orkige_core PRIVATE ORKIGE_HTTP_WIN)
        # winhttp.dll ships with Windows, so this is an import library from the
        # SDK rather than a dependency - and its symbols never leave
        # HttpBackendWin.cpp, so the link stays PRIVATE and windows.h/winhttp.h
        # stay off the precompiled header
        target_link_libraries(orkige_core PRIVATE winhttp)
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendWin.cpp"
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    else()
        target_compile_definitions(orkige_core PRIVATE ORKIGE_HTTP_CURL)
        # curl symbols never leave HttpBackendCurl.cpp, so the dependency is
        # PRIVATE - and the header stays off the precompiled header
        find_package(CURL CONFIG REQUIRED)
        target_link_libraries(orkige_core PRIVATE CURL::libcurl)
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/core_http/HttpBackendCurl.cpp"
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endif()
else()
    target_compile_definitions(orkige_core PRIVATE ORKIGE_HTTP_NONE)
endif()

if(APPLE)
    # core_util/PlatformUtil.mm uses Foundation (NSFileManager & co) and
    # CoreServices (FSFindFolder); PUBLIC because static-lib consumers must
    # link the frameworks themselves
    target_link_libraries(orkige_core PUBLIC
        "-framework Foundation"
        "-framework CoreServices"
    )
endif()

if(ORKIGE_SCRIPTING STREQUAL "LUA")
    # PUBLIC: the Meta_Lua.h macros (and thus sol2/lua headers) surface in
    # the headers of every dependent target
    find_package(Lua REQUIRED)
    find_package(sol2 CONFIG REQUIRED)
    target_include_directories(orkige_core PUBLIC ${LUA_INCLUDE_DIR})
    target_link_libraries(orkige_core PUBLIC sol2::sol2 ${LUA_LIBRARIES})

    # sol2 reaches nearly every TU through the Meta_Lua.h backend and is by
    # far the heaviest include in the layer - precompile it. CXX-only guard:
    # the target has ObjC++ sources on Apple platforms. Adding the heavy std
    # headers next to it was measured as a no-op (sol.hpp already includes
    # them, so they ride along in this PCH).
    target_precompile_headers(orkige_core PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:<sol/sol.hpp$<ANGLE-R>>"
    )
    orkige_pch_ccache_compat(orkige_core)
endif()
