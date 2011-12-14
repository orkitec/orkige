# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

#SET(CMAKE_SUPPRESS_REGENERATION  1)
#SET(CMAKE_USE_RELATIVE_PATHS 1)
#SET(INTERNAL_CREATE_DISTRIBUTABLE_MSVC_PROJECTFILES 1)

set(CMAKE_USER_MAKE_RULES_OVERRIDE
   ${CMAKE_CURRENT_SOURCE_DIR}/CMake/c_flag_overrides.cmake)
set(CMAKE_USER_MAKE_RULES_OVERRIDE_CXX
   ${CMAKE_CURRENT_SOURCE_DIR}/CMake/cxx_flag_overrides.cmake)
   
SET(ORKIGE_PROJECT_NAME  "ORKIGE" CACHE STRING "Project File Name" )

project(${ORKIGE_PROJECT_NAME})

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
include(ConfigureOrkige)

ConfigureOrkige()

if (APPLE)
    if (${XCODE_VERSION} STRGREATER "4.0.0")
        set(CMAKE_XCODE_ATTRIBUTE_GCC_VERSION "4.2")
    endif()
endif()

# determine Ogre version numbers
include(OgreGetVersion)
ogre_get_version(${OGREPATH}/OgreMain/include/OgrePrerequisites.h)
message(STATUS "Configuring OGRE ${OGRE_VERSION}")
# Configure version file for use by scripts
configure_file("${OGRE_TEMPLATES_DIR}/version.txt.in" "${OGRE_BINARY_DIR}/version.txt" @ONLY)

subdirs(Dependencies)
subdirs(${OGRELITEDIRECTORY})
subdirs(orkige_core)
subdirs(orkige_engine)

if (ORKIGE_BUILD_GAME)
	subdirs(../game)
endif()

if (ORKIGE_BUILD_MAXEXPORTER)
	subdirs(orkige_maxexport)
endif()

if (ORKIGE_BUILD_FONTCONVERTER)
	subdirs(orkige_fontconverter)
endif()

if (ORKIGE_BUILD_MENUVIEWER)
	subdirs(orkige_menuviewer)
endif()

if (ORKIGE_UPDATE_DOCS)
	subdirs(Docs/OrkigeAPI)
endif()

if (ORKIGE_BUILD_OGITOR)
	subdirs(Ogitor)
endif()
