# texcook - the export-time GPU texture encoder.
#
# The encoder is the LIBRARY orkige_texcook: libktx carries both the ASTC
# encoder (every block size) and the universal encoder/transcoder that yields
# ETC2 and BC1/BC3/BC7 blocks - one vcpkg dependency covers every format the
# import-settings model offers. It consumes raw RGBA8 level data (the caller
# owns PNG decode/downscale/premultiply/mip chains) and produces the container
# the target runtime already reads: .dds (BCn, both flavors), .ktx (KTX1 - the
# classic flavor's ETC2/ASTC loader) or .oitd (the Ogre-Next native container).
# The project exporter links it directly, so an export resolves no tool path
# and stages no extra binary. See Docs/textures.md.
#
# The `texcook` executable beside it is the argv face for the paths that drive
# the cook as a separate process. The encoder's structural cases live in the
# Catch2 suite tests/exporter, over the library.
find_package(Ktx CONFIG REQUIRED)

add_library(orkige_texcook STATIC
    TextureEncode.h
    TextureEncode.cpp)
target_include_directories(orkige_texcook PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(orkige_texcook PRIVATE KTX::ktx)
target_compile_features(orkige_texcook PUBLIC cxx_std_20)

add_executable(texcook main.cpp)
target_link_libraries(texcook PRIVATE orkige_texcook)
target_compile_features(texcook PRIVATE cxx_std_20)
