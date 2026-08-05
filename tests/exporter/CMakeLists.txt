# Exporter unit tests - the pure half of packaging a project.
#
# Everything asserted here is a DECISION: the manifest facts, the setting
# vocabularies (orientation, launch background, Android version/assets/package),
# the machine-local credential resolvers, the fixed Apple declarations
# (entitlements, App Transport Security, the privacy manifest), the image
# arithmetic an icon set and a texture cook are built from, and the whole
# Android package assembly - every program it spawns, decided up front by a
# pure planner. None of it needs a certificate, an SDK, a device or a build
# tree, which is exactly why it can be a hard gate on every host.

find_package(Catch2 3 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(orkige_exporter_tests
    ExportProjectTests.cpp
    ExportSettingsTests.cpp
    ExportPlistTests.cpp
    ExportImageTests.cpp
    ExportFilesTests.cpp
    ExportTextureCookTests.cpp
    ExportIconsTests.cpp
    ExportSelfContainTests.cpp
    ExportBuildTreeTests.cpp
    ExportLinuxTests.cpp
    ExportWindowsTests.cpp
    ExportPayloadTests.cpp
    ExportZipTests.cpp
    ExportMobileTests.cpp
    ExportMacosSignTests.cpp
    ExportAndroidAssembleTests.cpp
    ExportAndroidLibraryTests.cpp
    ExportWebTests.cpp
    TextureEncodeTests.cpp
    # the engine's zip READER, compiled straight in: the writer's round trip is
    # only a proof when the consumer parses it. MiniZip is renderer-free (the
    # fuzz harness consumes it the same way), so this costs one TU and no
    # dependency on the engine library.
    "${CMAKE_SOURCE_DIR}/orkige_engine/engine_filesystem/MiniZip.cpp"
)

target_link_libraries(orkige_exporter_tests PRIVATE
    orkige_exporter
    orkige_texcook
    ZLIB::ZLIB
    Catch2::Catch2WithMain
)
target_include_directories(orkige_exporter_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/orkige_engine")
target_compile_features(orkige_exporter_tests PRIVATE cxx_std_20)
# the neutral engine icon: the web export's favicon leg needs a real image to
# decode, and this is the same file the CLI falls back to
target_compile_definitions(orkige_exporter_tests PRIVATE
    "ORKIGE_EXPORT_TEST_DEFAULT_ICON=\"${Orkige_SOURCE_DIR}/Util/media/orkige_default_icon.png\"")

include(Catch)
catch_discover_tests(orkige_exporter_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
