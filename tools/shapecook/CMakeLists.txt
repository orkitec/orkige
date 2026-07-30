# shapecook - the SVG -> .oshape cook as a host CLI.
#
# The editor reaches the same conversion in-process (SvgShapeCook, linked into
# orkige_engine), so this tool exists for the paths that have no editor: an
# asset generator, a batch re-cook, and the MORPH SET mode, whose several pose
# files have no single-file import gesture. The texcook precedent - a small host
# binary the tooling calls, never shipped to a device.
#
# It compiles the ONE nanosvg translation unit directly rather than linking the
# engine: the cook seam depends on orkige_core alone, so the CLI needs no
# render backend, no window system and no physics. See
# orkige_engine/engine_gui/SvgShapeCook.h for what the cook maps.
find_package(NanoSVG CONFIG REQUIRED)

add_executable(shapecook
    main.cpp
    "${Orkige_SOURCE_DIR}/orkige_engine/engine_gui/SvgShapeCookImpl.cpp")
target_include_directories(shapecook PRIVATE
    "${Orkige_SOURCE_DIR}/orkige_engine")
target_link_libraries(shapecook PRIVATE
    Orkige::Core
    NanoSVG::nanosvg)
target_compile_features(shapecook PRIVATE cxx_std_20)
