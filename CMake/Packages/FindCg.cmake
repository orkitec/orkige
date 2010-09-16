#
# Try to find NVIDIA's Cg compiler, runtime libraries, and include path.
# Once done this will define
#
# Cg_FOUND =system has NVIDIA Cg and it can be used. 
# Cg_INCLUDE_DIRS = directory where cg.h resides
# Cg_LIBRARIES = full path to libCg.so (Cg.DLL on win32)
# Cg_GL_LIBRARY = full path to libCgGL.so (CgGL.dll on win32)
# Cg_COMPILER = full path to cgc (cgc.exe on win32)
# 


# On OSX default to using the framework version of Cg.


IF (APPLE)
  INCLUDE(${CMAKE_ROOT}/Modules/CMakeFindFrameworks.cmake)
  SET(Cg_FRAMEWORK_INCLUDES)
  CMAKE_FIND_FRAMEWORKS(Cg)
  IF (Cg_FRAMEWORKS)
    FOREACH(dir ${Cg_FRAMEWORKS})
      SET(Cg_FRAMEWORK_INCLUDES ${Cg_FRAMEWORK_INCLUDES}
        ${dir}/Headers ${dir}/PrivateHeaders)
    ENDFOREACH(dir)


    # Find the include  dir
    FIND_PATH(Cg_INCLUDE_DIRS cg.h
      ${Cg_FRAMEWORK_INCLUDES}
      )


    # Since we are using Cg framework, we must link to it.
	# Note, we use weak linking, so that it works even when Cg is not available.
    SET(Cg_LIBRARIES "-weak_framework Cg" CACHE STRING "Cg library")
    SET(Cg_GL_LIBRARY "-weak_framework Cg" CACHE STRING "Cg GL library")
  ENDIF (Cg_FRAMEWORKS)
  FIND_PROGRAM(Cg_COMPILER cgc
    /usr/bin
    /usr/local/bin
    DOC "The Cg compiler"
    )
ELSE (APPLE)
  IF (WIN32)
    FIND_PROGRAM( Cg_COMPILER cgc
      $ENV{Cg_BIN_PATH}
      $ENV{PROGRAMFILES}/NVIDIA\ Corporation/Cg/bin
      $ENV{PROGRAMFILES}/Cg
      ${PROJECT_SOURCE_DIR}/../Cg
      DOC "The Cg Compiler"
      )
    IF (Cg_COMPILER)
      GET_FILENAME_COMPONENT(Cg_COMPILER_DIR ${Cg_COMPILER} PATH)
      GET_FILENAME_COMPONENT(Cg_COMPILER_SUPER_DIR ${Cg_COMPILER_DIR} PATH)
    ELSE (Cg_COMPILER)
      SET (Cg_COMPILER_DIR .)
      SET (Cg_COMPILER_SUPER_DIR ..)
    ENDIF (Cg_COMPILER)
    FIND_PATH( Cg_INCLUDE_DIRS Cg/cg.h
      $ENV{Cg_INC_PATH}
      $ENV{PROGRAMFILES}/NVIDIA\ Corporation/Cg/include
      $ENV{PROGRAMFILES}/Cg
      ${PROJECT_SOURCE_DIR}/../Cg
      ${Cg_COMPILER_SUPER_DIR}/include
      ${Cg_COMPILER_DIR}
      DOC "The directory where Cg/cg.h resides"
      )
    FIND_LIBRARY( Cg_LIBRARIES
      NAMES Cg
      PATHS
      $ENV{Cg_LIB_PATH}
      $ENV{PROGRAMFILES}/NVIDIA\ Corporation/Cg/lib
      $ENV{PROGRAMFILES}/Cg
      ${PROJECT_SOURCE_DIR}/../Cg
      ${Cg_COMPILER_SUPER_DIR}/lib
      ${Cg_COMPILER_DIR}
      DOC "The Cg runtime library"
      )
    FIND_LIBRARY( Cg_GL_LIBRARY
      NAMES CgGL
      PATHS
      $ENV{PROGRAMFILES}/NVIDIA\ Corporation/Cg/lib
      $ENV{PROGRAMFILES}/Cg
      ${PROJECT_SOURCE_DIR}/../Cg
      ${Cg_COMPILER_SUPER_DIR}/lib
      ${Cg_COMPILER_DIR}
      DOC "The Cg runtime library"
      )
  ELSE (WIN32)
    FIND_PROGRAM( Cg_COMPILER cgc
      /usr/bin
      /usr/local/bin
      DOC "The Cg Compiler"
      )
    GET_FILENAME_COMPONENT(Cg_COMPILER_DIR "${Cg_COMPILER}" PATH)
    GET_FILENAME_COMPONENT(Cg_COMPILER_SUPER_DIR "${Cg_COMPILER_DIR}" PATH)
    FIND_PATH( Cg_INCLUDE_DIRS Cg/cg.h
      /usr/include
      /usr/local/include
      ${Cg_COMPILER_SUPER_DIR}/include
      DOC "The directory where Cg/cg.h resides"
      )
    FIND_LIBRARY( Cg_LIBRARIES Cg
      PATHS
      /usr/lib64
      /usr/lib
      /usr/local/lib64
      /usr/local/lib
      ${Cg_COMPILER_SUPER_DIR}/lib64
      ${Cg_COMPILER_SUPER_DIR}/lib
      DOC "The Cg runtime library"
      )
	SET(Cg_LIBRARIES ${Cg_LIBRARIES} -lpthread)
    FIND_LIBRARY( Cg_GL_LIBRARY CgGL
      PATHS
      /usr/lib64
      /usr/lib
      /usr/local/lib64
      /usr/local/lib
      ${Cg_COMPILER_SUPER_DIR}/lib64
      ${Cg_COMPILER_SUPER_DIR}/lib
      DOC "The Cg runtime library"
      )
  ENDIF (WIN32)
ENDIF (APPLE)


IF (Cg_INCLUDE_DIRS)
  SET( Cg_FOUND 1 CACHE STRING "Set to 1 if Cg is found, 0 otherwise")
ELSE (Cg_INCLUDE_DIRS)
  SET( Cg_FOUND 0 CACHE STRING "Set to 1 if Cg is found, 0 otherwise")
ENDIF (Cg_INCLUDE_DIRS)


MARK_AS_ADVANCED( Cg_FOUND )