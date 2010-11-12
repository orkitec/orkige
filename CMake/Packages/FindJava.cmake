FIND_PATH( JAVA_INCLUDE_DIRS jni.h
      $ENV{JAVA_INC_PATH}
      $ENV{PROGRAMFILES}/Java/jdk1.6.0_22/include
      $ENV{PROGRAMFILES}/Java/include
      DOC "The directory where jni.h resides"
)
FIND_LIBRARY( JAVA_LIBRARIES
      NAMES jvm
      PATHS
      $ENV{JAVA_LIB_PATH}
      $ENV{PROGRAMFILES}/Java/jdk1.6.0_22/lib
      $ENV{PROGRAMFILES}/Java/lib
      DOC "The Java jvm library"
)

IF (JAVA_INCLUDE_DIRS)
  SET( JAVA_FOUND 1 CACHE STRING "Set to 1 if Java is found, 0 otherwise")
ELSE (JAVA_INCLUDE_DIRS)
  SET( JAVA_FOUND 0 CACHE STRING "Set to 1 if Java is found, 0 otherwise")
ENDIF (JAVA_INCLUDE_DIRS)


MARK_AS_ADVANCED( JAVA_FOUND )
