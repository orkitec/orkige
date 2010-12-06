FIND_PROGRAM( 7ZIP 7z
      $ENV{7ZIP_PATH}
      $ENV{PROGRAMFILES}/7-Zip
      DOC "7 Zip"
      )

IF (7ZIP)
  SET( 7ZIP_FOUND 1 CACHE STRING "Set to 1 if 7ZIP is found, 0 otherwise")
ELSE (7ZIP)
  SET( 7ZIP_FOUND 0 CACHE STRING "Set to 1 if 7ZIP is found, 0 otherwise")
ENDIF (7ZIP)


MARK_AS_ADVANCED( 7ZIP_FOUND )
