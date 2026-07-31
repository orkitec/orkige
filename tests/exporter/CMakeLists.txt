# Exporter unit tests - the pure half of packaging a project.
#
# Everything asserted here is a DECISION: the manifest facts, the setting
# vocabularies (orientation, launch background, Android version/assets/package),
# the machine-local credential resolvers, the fixed Apple declarations
# (entitlements, App Transport Security, the privacy manifest) and the image
# arithmetic an icon set and a texture cook are built from. None of it needs a
# certificate, a device or a build tree, which is exactly why it can be a hard
# gate on every host.

find_package(Catch2 3 CONFIG REQUIRED)

add_executable(orkige_exporter_tests
    ExportProjectTests.cpp
    ExportSettingsTests.cpp
    ExportPlistTests.cpp
    ExportImageTests.cpp
    ExportFilesTests.cpp
    ExportTextureCookTests.cpp
    ExportIconsTests.cpp
    ExportSelfContainTests.cpp
    TextureEncodeTests.cpp
)

target_link_libraries(orkige_exporter_tests PRIVATE
    orkige_exporter
    orkige_texcook
    Catch2::Catch2WithMain
)
target_compile_features(orkige_exporter_tests PRIVATE cxx_std_20)

include(Catch)
catch_discover_tests(orkige_exporter_tests
    PROPERTIES LABELS unit TIMEOUT 30
)
