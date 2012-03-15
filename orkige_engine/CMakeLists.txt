# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)
include(MacroListFilter)
# ---------------------------------------------------------
FILE (GLOB engine_base_FILES "engine_base/[A-z]*.*")
source_group(engine_base FILES ${engine_base_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_gocomponent_FILES "engine_gocomponent/[A-z]*.*")
source_group(engine_gocomponent FILES ${engine_gocomponent_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_graphic_FILES "engine_graphic/[A-z]*.*")
source_group(engine_graphic FILES ${engine_graphic_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_fastgui_FILES "engine_fastgui/[A-z]*.*")
source_group(engine_fastgui FILES ${engine_fastgui_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_input_FILES "engine_input/[A-z]*.*")
source_group(engine_input FILES ${engine_input_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_module_FILES "engine_module/[A-z]*.*")
source_group(engine_module FILES ${engine_module_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_physic_FILES "engine_physic/[A-z]*.*")
source_group(engine_physic FILES ${engine_physic_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_sound_FILES "engine_sound/[A-z]*.*")
if (NOT APPLE)
	SET(re_objc ".+\\.h$" ".+\\.hh$" ".+\\.hxx$" ".+\\.hpp$" ".+\\.c+" ".+\\.hcc$")
	#remove *.mm files
	LIST_FILTER(engine_sound_FILES re_objc)
endif()
source_group(engine_sound FILES ${engine_sound_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_video_FILES "engine_video/[A-z]*.*")
source_group(engine_video FILES ${engine_video_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_filesystem_FILES "engine_filesystem/[A-z]*.*")
source_group(engine_filesystem FILES ${engine_filesystem_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_util_FILES "engine_util/[A-z]*.*")
source_group(engine_util FILES ${engine_util_FILES})
# ---------------------------------------------------------
if (ORKIGE_ENABLE_GAMESWF)
	FILE (GLOB engine_swf_FILES "engine_swf/[A-z]*.*")
	source_group(engine_swf FILES ${engine_swf_FILES})
endif(ORKIGE_ENABLE_GAMESWF)
# ---------------------------------------------------------
if (ORKIGE_ENABLE_MYGUI)
	FILE (GLOB engine_mygui_FILES "engine_mygui/[A-z]*.*")
	source_group(engine_mygui FILES ${engine_mygui_FILES})
endif(ORKIGE_ENABLE_MYGUI)
# ---------------------------------------------------------
# ---------------------------------------------------------
if (ORKIGE_ENABLE_TUIO)
	FILE (GLOB engine_TUIO_FILES "engine_TUIO/[A-z]*.*")
	source_group(engine_TUIO FILES ${engine_TUIO_FILES})
endif(ORKIGE_ENABLE_TUIO)
# ---------------------------------------------------------
# ---------------------------------------------------------
if (ORKIGE_ENABLE_TUIO)
	FILE (GLOB engine_OSC_FILES "engine_OSC/[A-z]*.*")
	source_group(engine_OSC FILES ${engine_OSC_FILES})
endif(ORKIGE_ENABLE_TUIO)
# ---------------------------------------------------------

# ---------------------------------------------------------
# Build setup
# ---------------------------------------------------------
if (APPLE)
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/engine_input/InputManager.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/engine_sound/SoundManager.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/engine_graphic/Engine.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/engine_base/Localisation.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/engine_video/VideoManager.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()

ConfigureOrkigeDependencies()

ogre_add_library(orkige_engine ${ORKIGE_LIB_TYPE}
            ${engine_base_FILES}
            ${engine_gocomponent_FILES}
            ${engine_graphic_FILES}
            ${engine_fastgui_FILES}
            ${engine_input_FILES}
            ${engine_module_FILES}
            ${engine_physic_FILES}
            ${engine_sound_FILES}
						${engine_filesystem_FILES}
	    ${engine_TUIO_FILES}
	    ${engine_OSC_FILES}
            ${engine_video_FILES}
            ${engine_util_FILES}
            ${engine_swf_FILES}
            ${engine_mygui_FILES}
            SEPARATE 
            "${CMAKE_CURRENT_SOURCE_DIR}/engine_input/InputManager.cpp" 
            "${CMAKE_CURRENT_SOURCE_DIR}/engine_sound/SoundManager.cpp" 
            "${CMAKE_CURRENT_SOURCE_DIR}/engine_graphic/Engine.cpp" 
            "${CMAKE_CURRENT_SOURCE_DIR}/engine_base/Localisation.cpp" 
            "${CMAKE_CURRENT_SOURCE_DIR}/engine_video/VideoManager.cpp"
    )
