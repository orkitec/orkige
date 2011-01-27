# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

set(TargetName orkige_fontconverter)

# ---------------------------------------------------------
# fontconverter Files
# ---------------------------------------------------------
FILE (GLOB fontconverter_SOURCE_files "*.cpp")
FILE (GLOB fontconverter_HEADER_files "*.h")

set(fontconverter_SOURCE
    # ----- Source -----
    "${fontconverter_SOURCE_files}"
)

set(fontconverter_HEADER
    # ----- Headers -----
    "${fontconverter_HEADER_files}"
)

source_group(fontconverter FILES ${fontconverter_HEADER} ${fontconverter_SOURCE})

# ---------------------------------------------------------
# Build setup
# ---------------------------------------------------------

# for swig no __declspec(dllexport)
add_definitions(-DPRE_MAX_2010 -D_SECURE_SCL=0 -D_SCL_SECURE_NO_DEPRECATE -DISOLATION_AWARE_ENABLED=1)

include_directories(
	${ORKIGE_INCLUDE}
	${ORKIGE_OGRE_INCLUDE}
	${OGRE_INCLUDE_DIR}/PlugIns/OctreeSceneManager/include
	${OGRE_INCLUDE_DIR}/PlugIns/CgProgramManager/include
	${OGRE_INCLUDE_DIR}/PlugIns/ParticleFX/include
	${ORKIGE_WATERMAZE_RENDERSYSTEM_INCLUDES}
	${ORKIGE_OIS_INCLUDE}
	${ORKIGE_ZLIB_INCLUDE}
	${ORKIGE_LUA_INCLUDE}
	${ORKIGE_OPENAL_INCLUDE}
	./
	)

link_libraries(
		${ORKIGE_LIB}
		${OGRE_LIBDIR}
		${ORKIGE_OGRE_LIBS}
		Box2D
		OgrePaging
		OgreTerrain
		Plugin_ParticleFX
		Plugin_OctreeSceneManager
		OIS
    )
    
add_executable(
		${TargetName}
		${fontconverter_SOURCE}			${fontconverter_HEADER}
	)
