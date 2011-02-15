# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

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
FILE (GLOB engine_gui_FILES "engine_gui/[A-z]*.*")
source_group(engine_gui FILES ${engine_gui_FILES})
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
source_group(engine_sound FILES ${engine_sound_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_video_FILES "engine_video/[A-z]*.*")
source_group(engine_video FILES ${engine_video_FILES})
# ---------------------------------------------------------
FILE (GLOB engine_util_FILES "engine_util/[A-z]*.*")
source_group(engine_util FILES ${engine_util_FILES})
# ---------------------------------------------------------

# ---------------------------------------------------------
# Build setup
# ---------------------------------------------------------
if (APPLE)
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/engine_input/InputManager.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/engine_sound/SoundManager.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/engine_graphic/Engine.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/engine_base/Localisation.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()

ConfigureOrkigeDependencies()

add_library(orkige_engine STATIC
            ${engine_base_FILES}
            ${engine_gocomponent_FILES}
            ${engine_graphic_FILES}
            ${engine_gui_FILES}
            ${engine_fastgui_FILES}
            ${engine_input_FILES}
            ${engine_module_FILES}
            ${engine_physic_FILES}
            ${engine_sound_FILES}
            ${engine_video_FILES}
            ${engine_util_FILES}
    )
