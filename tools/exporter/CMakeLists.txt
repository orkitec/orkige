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
# zlib backs the zip WRITER (ExportZip.cpp) an .ipa and a browser pak are
# packaged with - the write side of the reader the engine already runs on it
find_package(ZLIB REQUIRED)

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
    ExportBuildTree.h
    ExportBuildTree.cpp
    ExportPayload.h
    ExportPayload.cpp
    ExportMacos.h
    ExportMacos.cpp
    ExportZip.h
    ExportZip.cpp
    ExportIos.h
    ExportIos.cpp
    ExportAndroid.h
    ExportAndroid.cpp
    ExportAndroidAssemble.h
    ExportAndroidAssemble.cpp
    ExportWeb.h
    ExportWeb.cpp
    ExportRun.h
    ExportRun.cpp
)

target_include_directories(orkige_exporter PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(orkige_exporter PUBLIC Orkige::Core orkige_texcook)
target_link_libraries(orkige_exporter PRIVATE tinyxml2::tinyxml2 ZLIB::ZLIB)
target_compile_features(orkige_exporter PUBLIC cxx_std_20)

# the single-file stb_image decoder sees the stb include in exactly one TU
# (STB_IMAGE_STATIC keeps its symbols internal, so a binary carrying a second
# stb_image implementation still links)
set_source_files_properties(ExportImageDecode.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    INCLUDE_DIRECTORIES "${Stb_INCLUDE_DIR}")

# the argv face. Two source-tree facts are baked in so a developer run needs no
# extra arguments: the repository the engine media and the module build scripts
# come from, and the neutral default app icon. A distributed copy passes
# --engine-bundle instead and needs neither.
add_executable(orkige_export main.cpp)
target_link_libraries(orkige_export PRIVATE orkige_exporter)
target_compile_features(orkige_export PRIVATE cxx_std_20)
target_compile_definitions(orkige_export PRIVATE
    "ORKIGE_EXPORT_REPO_ROOT=\"${Orkige_SOURCE_DIR}\""
    "ORKIGE_EXPORT_DEFAULT_ICON=\"${Orkige_SOURCE_DIR}/Util/media/orkige_default_icon.png\"")
