# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

#SET(CMAKE_SUPPRESS_REGENERATION  1)
#SET(CMAKE_USE_RELATIVE_PATHS 1)
#SET(INTERNAL_CREATE_DISTRIBUTABLE_MSVC_PROJECTFILES 1)

project(ORKIGE)
SET(ORKIGE 1.0)

if(COMMAND cmake_policy)
   cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

if (MSVC)
	add_definitions( -D_CRT_SECURE_NO_WARNINGS )
	add_definitions( -D_CRT_SECURE_NO_DEPRECATE )
endif()

# ---------------------------------------------------------
set(CMAKE_MODULE_PATH
    ${CMAKE_SOURCE_DIR}/CMake
    ${CMAKE_SOURCE_DIR}/CMake/Utils
    ${CMAKE_SOURCE_DIR}/CMake/Packages
    ${CMAKE_SOURCE_DIR}/CMake/Templates
    )
include(CMakeDependentOption)
include(CheckCXXCompilerFlag)
include(ConfigOrkige)


configure_orkige(${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/OgreLite)


# ---------------------------------------------------------
# Common base paths
set(ORKIGE_SOURCE_DIR ${CMAKE_SOURCE_DIR})
set(ORKIGE_INCLUDE ${ORKIGE_OGRE_INCLUDE}
	${ORKIGE_SOURCE_DIR}/orkige_core
	${ORKIGE_SOURCE_DIR}/orkige_engine
	${CMAKE_SOURCE_DIR}/Dependencies/Source/boost
	${ORKIGE_DEP_INCLUDE}
)

# determine Ogre version numbers
include(OgreGetVersion)
ogre_get_version(${ORKIGE_SOURCE_DIR}/OgreLite/OgreMain/include/OgrePrerequisites.h)
message(STATUS "Configuring OGRE ${OGRE_VERSION}")
# Configure version file for use by scripts
configure_file("${OGRE_TEMPLATES_DIR}/version.txt.in" "${OGRE_BINARY_DIR}/version.txt" @ONLY)


if (ORKIGE_USE_LUA)
	set(ORKIGE_CORE_LIB             orkige_core ${ORKIGE_LUA_TARGET})
	set(ORKIGE_ENGINE_LIB           orkige_engine ${ORKIGE_LUA_TARGET})
	set(ORKIGE_STD_LUA_LIB          orkige_core ${ORKIGE_LUA_TARGET})
else()
	set(ORKIGE_CORE_LIB             orkige_core)
	set(ORKIGE_ENGINE_LIB           orkige_engine)
endif()

set(ORKIGE_LIB orkige_core orkige_engine)

subdirs(Dependencies)
subdirs(OgreLite)
subdirs(orkige_core)
subdirs(orkige_engine)
subdirs(../game)

if (ORKIGE_UPDATE_DOCS)
	subdirs(Docs/LuaAPI)
endif()
