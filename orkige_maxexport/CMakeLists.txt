# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

# ---------------------------------------------------------
# core_base Files
# ---------------------------------------------------------
FILE (GLOB maxexporter_SOURCE_files "*.cpp")
FILE (GLOB maxexporter_HEADER_files "*.h")

set(maxexporter_SOURCE
    # ----- Source -----
    "${maxexporter_SOURCE_files}"
)

set(maxexporter_HEADER
    # ----- Headers -----
    OrkigeMaxGUI.ms
    "${maxexporter_HEADER_files}"
)

source_group(core_base FILES ${core_base_HEADER} ${core_base_SOURCE})

# ---------------------------------------------------------


# ---------------------------------------------------------
# Build setup
# ---------------------------------------------------------

# for swig no __declspec(dllexport)
add_definitions(-DSTATIC_LINKED)

include_directories(
	${CMAKE_SOURCE_DIR}/Dependencies/Source/boost
  ${ORKIGE_OGRE_INCLUDE}
	./
	)

link_libraries(
    ${OGRE_LIBDIR}
    ${ORKIGE_OGRE_LIBS}
    )
add_library(orkige_maxexport MODULE
	${maxexporter_SOURCE}			${maxexporter_HEADER}
)
