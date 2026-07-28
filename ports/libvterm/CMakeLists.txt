cmake_minimum_required(VERSION 3.16)
project(libvterm C)

# Upstream ships no library CMake build (a Makefile plus a Perl step that turns
# the encoding .tbl tables into .inc headers). This fork commits the GENERATED
# .inc files (src/encoding/*.inc, src/fullwidth.inc), so the whole library is a
# plain C99 compile with NO Perl at build time - this CMakeLists just names the
# translation units the Makefile would. Static only (the engine links everything
# statically during the revival).

set(VTERM_SOURCES
    src/encoding.c
    src/keyboard.c
    src/mouse.c
    src/parser.c
    src/pen.c
    src/screen.c
    src/state.c
    src/unicode.c
    src/vterm.c
)

add_library(vterm STATIC ${VTERM_SOURCES})
add_library(libvterm::libvterm ALIAS vterm)

target_include_directories(vterm
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # src/ carries vterm_internal.h and the encoding/*.inc + fullwidth.inc
        # that encoding.c / unicode.c include with quoted relative paths
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(vterm PRIVATE c_std_99)

if(MSVC)
    # the C sources call snprintf/strncpy and friends; MSVC's secure-CRT
    # deprecation noise is not actionable for a vendored third-party lib
    target_compile_definitions(vterm PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

install(
    TARGETS vterm
    EXPORT libvterm_target
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/include/vterm.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/vterm_keycodes.h"
    DESTINATION include)

include(CMakePackageConfigHelpers)
configure_package_config_file(libvterm-config.cmake.in
    libvterm-config.cmake
    INSTALL_DESTINATION share/libvterm)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libvterm-config.cmake"
    DESTINATION share/libvterm)

install(
    EXPORT libvterm_target
    NAMESPACE libvterm::
    FILE libvterm-targets.cmake
    DESTINATION share/libvterm
)
