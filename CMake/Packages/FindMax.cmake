FIND_PATH( MAXSDK_INCLUDE_DIRS max.h
      $ENV{MAXSDK_INC_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/include
      $ENV{PROGRAMFILES}/maxsdk/include
      DOC "The directory where max.h resides"
)
FIND_LIBRARY( MAXSDK_core_LIBRARIES
      NAMES core
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/lib
      $ENV{PROGRAMFILES}/maxsdk/lib
      DOC "The 3dsmax sdk core library"
)
FIND_LIBRARY( MAXSDK_geom_LIBRARIES
      NAMES geom
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/lib
      $ENV{PROGRAMFILES}/maxsdk/lib
      DOC "The 3dsmax sdk geom library"
)
FIND_LIBRARY( MAXSDK_gfx_LIBRARIES
      NAMES gfx
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/lib
      $ENV{PROGRAMFILES}/maxsdk/lib
      DOC "The 3dsmax sdk gfx library"
)
FIND_LIBRARY( MAXSDK_bmm_LIBRARIES
      NAMES bmm
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/lib
      $ENV{PROGRAMFILES}/maxsdk/lib
      DOC "The 3dsmax sdk bmm library"
)
FIND_LIBRARY( MAXSDK_mesh_LIBRARIES
      NAMES mesh
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/lib
      $ENV{PROGRAMFILES}/maxsdk/lib
      DOC "The 3dsmax sdk mesh library"
)
FIND_LIBRARY( MAXSDK_maxutil_LIBRARIES
      NAMES maxutil
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/lib
      $ENV{PROGRAMFILES}/maxsdk/lib
      DOC "The 3dsmax sdk maxutil library"
)
FIND_LIBRARY( MAXSDK_maxscrpt_LIBRARIES
      NAMES maxscrpt
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/lib
      $ENV{PROGRAMFILES}/maxsdk/lib
      DOC "The 3dsmax sdk maxscrpt library"
)
FIND_LIBRARY( MAXSDK_paramblk2_LIBRARIES
      NAMES paramblk2
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/lib
      $ENV{PROGRAMFILES}/maxsdk/lib
      DOC "The 3dsmax sdk paramblk2 library"
)
FIND_LIBRARY( MAXSDK_igame_LIBRARIES
      NAMES igame
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/lib
      $ENV{PROGRAMFILES}/maxsdk/lib
      DOC "The 3dsmax sdk igame library"
)

FIND_PATH( MAXSDK_Morpher_INCLUDE_DIRS wm3.h
      $ENV{MAXSDK_INC_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/samples/modifiers/morpher
      $ENV{PROGRAMFILES}/maxsdk/samples/modifiers/morpher
      DOC "The directory where wm3.h resides"
)
FIND_LIBRARY( MAXSDK_Morpher_LIBRARIES
      NAMES Morpher
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/samples/modifiers/morpher/Release
      $ENV{PROGRAMFILES}/maxsdk/samples/modifiers/morpher/Release
      DOC "The 3dsmax sdk Morpher library"
)

IF (MAXSDK_INCLUDE_DIRS)
  SET( MAXSDK_FOUND 1 CACHE STRING "Set to 1 if MaxSdk is found, 0 otherwise")
ELSE (MAXSDK_INCLUDE_DIRS)
  SET( MAXSDK_FOUND 0 CACHE STRING "Set to 1 if MaxSdk is found, 0 otherwise")
ENDIF (MAXSDK_INCLUDE_DIRS)


MARK_AS_ADVANCED( MAXSDK_FOUND )
