# orkige_exporter - the project exporter as a LIBRARY.
#
# Packaging a project is a host operation the editor performs, so it is C++ the
# editor links rather than a script it spawns: no interpreter to preflight, no
# tool directory to resolve, nothing to stage inside a distributed app. The
# same library backs the `orkige_export` CLI the tests, CI and the device
# runbook drive.
#
# Dependencies stay host-tier and small: tinyxml2 (the manifest and the Apple
# property lists), the stb_image decoder in its ONE translation unit, and
# orkige_core for the shared String/Json/PngWriter vocabulary. No renderer, no
# window system, no physics - an export packages what a build already produced.

find_package(tinyxml2 CONFIG REQUIRED)
find_package(Stb REQUIRED)

add_library(orkige_exporter STATIC
    ExportProject.h
    ExportProject.cpp
    ExportSettings.h
    ExportSettings.cpp
    ExportPlist.h
    ExportPlist.cpp
    ExportImage.h
    ExportImage.cpp
    ExportImageDecode.cpp
    ExportFiles.h
    ExportFiles.cpp
    ExportProcess.h
    ExportProcess.cpp
    ExportTextureCook.h
    ExportTextureCook.cpp
    ExportIcons.h
    ExportIcons.cpp
    ExportSelfContain.h
    ExportSelfContain.cpp
)

target_include_directories(orkige_exporter PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(orkige_exporter PUBLIC Orkige::Core orkige_texcook)
target_link_libraries(orkige_exporter PRIVATE tinyxml2::tinyxml2)
target_compile_features(orkige_exporter PUBLIC cxx_std_20)

# the single-file stb_image decoder sees the stb include in exactly one TU
# (STB_IMAGE_STATIC keeps its symbols internal, so a binary carrying a second
# stb_image implementation still links)
set_source_files_properties(ExportImageDecode.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    INCLUDE_DIRECTORIES "${Stb_INCLUDE_DIR}")
