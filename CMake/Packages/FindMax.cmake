    FIND_PATH( MAXSDK_INCLUDE_DIRS max.h
      $ENV{MAXSDK_INC_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/include
      $ENV{PROGRAMFILES}/maxsdk/include
      DOC "The directory where max.h resides"
      )
    FIND_LIBRARY( MAXSDK_LIBRARIES
      NAMES MAXSDK
      PATHS
      $ENV{MAXSDK_LIB_PATH}
      $ENV{PROGRAMFILES}/Autodesk/3ds\ Max\ 2009\ SDK/maxsdk/lib
      $ENV{PROGRAMFILES}/maxsdk/lib
      DOC "The 3dsmax sdk runtime library"
      )



IF (MAXSDK_INCLUDE_DIRS)
  SET( MAXSDK_FOUND 1 CACHE STRING "Set to 1 if MaxSdk is found, 0 otherwise")
ELSE (MAXSDK_INCLUDE_DIRS)
  SET( MAXSDK_FOUND 0 CACHE STRING "Set to 1 if MaxSdk is found, 0 otherwise")
ENDIF (MAXSDK_INCLUDE_DIRS)


MARK_AS_ADVANCED( MAXSDK_FOUND )
