# -----------------------------------------------------------------------------
#
#  Application Utils
#
# -----------------------------------------------------------------------------
macro(ADD_ORKIGE_EXECUTABLE TARGET SRC CONTENT)

    # Engine application deps
    include_directories(
        ${ORKIGE_INCLUDE}
    )

    link_libraries(${ORKIGE_LIB})
    
 

    if (APPLE)
        set(EXETYPE MACOSX_BUNDLE)
        set_source_files_properties( MainMenu.nib  PROPERTIES MACOSX_PACKAGE_LOCATION Resources )
        set_source_files_properties(${EX} PROPERTIES MACOSX_PACKAGE_LOCATION Resources )
        set (NibFile MainMenu.nib)
    endif()
    source_group(Resources FILES ${${CONTENT}})

    set(NEW_SOURCE
        ${SRC}
        ${${CONTENT}}
    )

    if (APPLE)
	    add_executable(
	        ${TARGET} 
	        MACOSX_BUNDLE
	        MainMenu.nib
	        ${NEW_SOURCE} 
        )
    else ()
	    add_executable(${TARGET} ${NEW_SOURCE})
    endif()

endmacro(ADD_ORKIGE_EXECUTABLE)

#----------------------------------------------------------
macro(ADD_ORKIGE_MODULE TARGET)

    # Engine application deps
    include_directories(
        ${ORKIGE_INCLUDE}
    )
    link_libraries(${ORKIGE_STD_LUA_LIB})
    add_library(${TARGET} SHARED ${ARGN})

endmacro(ADD_ORKIGE_MODULE)


#----------------------------------------------------------
macro(ORKIGE_COPY_TARGET TGT DST)

    get_target_property(TGT_PTH ${TGT} LOCATION)
    add_custom_command(
	    TARGET ${TGT}
	    POST_BUILD
	    COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${TGT_PTH} ${DST}
	    COMMENT "Copy ${TGT}:${DST}"
    )
endmacro(ORKIGE_COPY_TARGET)


#----------------------------------------------------------
macro(ORKIGE_SORT_ARGS OKT_OUTFILES)
    foreach (it ${ARGN})
        set(${OKT_OUTFILES} ${${OKT_OUTFILES}} ${it})
    endforeach(it)

    if (${OKT_OUTFILES})
        list(SORT ${OKT_OUTFILES})
    endif()

endmacro(ORKIGE_SORT_ARGS)

#----------------------------------------------------------
macro(ORKIGE_GROUP_FILES OUT)
    # Append Root Path to src
    file(GLOB_RECURSE GLOBS "${ARGN}")

    orkige_sort_args(GROUPS ${GLOBS})

    foreach (it ${GROUPS})

        get_filename_component(PATH_COMPS ${it} PATH)

        set(${OUT} ${${OUT}} ${it})

        string(REGEX REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" ""   TRIM    ${PATH_COMPS})
        string(REGEX REPLACE "/" "\\\\" FORMAT  ${TRIM})

        source_group("${FORMAT}" FILES ${it})

    endforeach(it)

endmacro(ORKIGE_GROUP_FILES)
