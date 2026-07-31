# animcook - the Lottie JSON -> .oanim cook as a host CLI.
#
# The editor reaches the same conversion in-process (core_util/VectorAnimCook,
# linked into orkige_core), so this tool exists for the paths that have no
# editor: an asset generator, a batch re-cook of a committed corpus, and the
# differential fixtures the unit suite pins. The shapecook precedent - a small
# host binary the tooling calls, never shipped to a device.
#
# The cook depends on orkige_core alone (its JSON front end and its geometry
# are both core), so the CLI needs no render backend, no window system and no
# physics.
add_executable(animcook main.cpp)
target_link_libraries(animcook PRIVATE Orkige::Core)
target_compile_features(animcook PRIVATE cxx_std_20)
