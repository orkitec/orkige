
# ---------------------------------------------------------
include(FindDoxygen)

if (DOXYGEN_FOUND)
	add_custom_target(OrkigeApiManual
	    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} 
        COMMENT "Building Orkige Documentation"}
    )
endif()
