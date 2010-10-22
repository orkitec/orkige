# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

# ---------------------------------------------------------
# maxexporter Files
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

source_group(maxexporter FILES ${maxexporter_HEADER} ${maxexporter_SOURCE})

# ---------------------------------------------------------
# maxexporter Files
# ---------------------------------------------------------
FILE (GLOB maxexporter_tinyxml_SOURCE_files "tinyxml/*.cpp")
FILE (GLOB maxexporter_tinyxml_HEADER_files "tinyxml/*.h")

set(maxexporter_tinyxml_SOURCE
    # ----- Source -----
    "${maxexporter_tinyxml_SOURCE_files}"
)

set(maxexporter_tinyxml_HEADER
    # ----- Headers -----
    "${maxexporter_tinyxml_HEADER_files}"
)

source_group(maxexporter_tinyxml FILES ${maxexporter_tinyxml_HEADER} ${maxexporter_tinyxml_SOURCE})

# ---------------------------------------------------------
# maxexporter Files
# ---------------------------------------------------------
FILE (GLOB maxexporter_MAXSCRIPT_files "*.ms")

set(maxexporter_MAXSCRIPT
    # ----- Headers -----
    OrkigeMaxGUI.ms
    "${maxexporter_MAXSCRIPT_files}"
)

source_group(scripts FILES ${maxexporter_MAXSCRIPT})
# ---------------------------------------------------------


# ---------------------------------------------------------
# Build setup
# ---------------------------------------------------------

# for swig no __declspec(dllexport)
add_definitions(-DPRE_MAX_2010)

include_directories(
	${CMAKE_SOURCE_DIR}/Dependencies/Source/boost
	${ORKIGE_OGRE_INCLUDE}
	${MAXSDK_INCLUDE_DIRS}
	${MAXSDK_INCLUDE_DIRS}/IGame
	${MAXSDK_Morpher_INCLUDE_DIRS}
	./
	)

link_libraries(
    ${OGRE_LIBDIR}
    ${ORKIGE_OGRE_LIBS}
    )
add_library(orkige_maxexport MODULE
	${maxexporter_MAXSCRIPT}
	${maxexporter_SOURCE}			${maxexporter_HEADER}
	${maxexporter_tinyxml_SOURCE}		${maxexporter_tinyxml_HEADER}
)

set_target_properties(orkige_maxexport PROPERTIES LINK_FLAGS ${CMAKE_LINK_DEF_FILE_FLAG}${PROJECT_SOURCE_DIR}/orkige_maxexport/orkige_maxexport.def)

target_link_libraries(orkige_maxexport 
	OgreMain  
	${MAXSDK_core_LIBRARIES}  
	${MAXSDK_geom_LIBRARIES}  
	${MAXSDK_gfx_LIBRARIES}  
	${MAXSDK_bmm_LIBRARIES} 
	${MAXSDK_mesh_LIBRARIES}
	${MAXSDK_maxutil_LIBRARIES}
	${MAXSDK_maxscrpt_LIBRARIES}
	${MAXSDK_paramblk2_LIBRARIES}
	msxml2.lib
	${MAXSDK_igame_LIBRARIES}
	${MAXSDK_Morpher_LIBRARIES}
)
