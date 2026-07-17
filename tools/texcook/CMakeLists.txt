# texcook - the export-time GPU texture encoder (a host-only CLI).
#
# The export cook (Util/cook_textures.py, stdlib-only by policy) shells out to
# this tool to block-compress shipped textures: libktx carries both the ASTC
# encoder (every block size) and the universal encoder/transcoder that yields
# ETC2 and BC1/BC3/BC7 blocks - one vcpkg dependency covers every format the
# import-settings model offers. texcook consumes raw RGBA8 level data (the
# Python side owns PNG decode/downscale/premultiply/mip chains) and writes the
# container the target runtime already reads: .dds (BCn, both flavors),
# .ktx (KTX1 - the classic flavor's ETC2/ASTC loader) or .oitd (the Ogre-Next
# native container). See Docs/textures.md.
find_package(Ktx CONFIG REQUIRED)

add_executable(texcook main.cpp)
target_link_libraries(texcook PRIVATE KTX::ktx)
target_compile_features(texcook PRIVATE cxx_std_20)
