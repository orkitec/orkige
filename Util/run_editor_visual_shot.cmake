# run_editor_visual_shot.cmake - boot the editor headlessly in one theme and
# dump a whole-window PNG as a reviewable ctest artifact (chrome evidence, not a
# golden-image gate). Verifies only that the capture was produced, so a boot or
# capture regression fails loudly while the taste calls stay a human review.
#
# Invoked from tests/CMakeLists.txt:
#   cmake -DEDITOR_EXE=<exe> -DOUT_PNG=<path> -DTHEME=dark|light
#         -P Util/run_editor_visual_shot.cmake
#
# Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec

if(NOT EDITOR_EXE OR NOT OUT_PNG OR NOT THEME)
	message(FATAL_ERROR "run_editor_visual_shot: EDITOR_EXE, OUT_PNG and THEME "
		"are all required")
endif()

get_filename_component(_outDir "${OUT_PNG}" DIRECTORY)
file(MAKE_DIRECTORY "${_outDir}")
file(REMOVE "${OUT_PNG}")

# ORKIGE_EDITOR_THEME forces the variant (capture-only, never persisted);
# ORKIGE_DEMO_SCREENSHOT dumps the whole window at frame 60; ORKIGE_DEMO_FRAMES
# gives the boot + capture margin before the editor exits on its own.
execute_process(
	COMMAND ${CMAKE_COMMAND} -E env
		"ORKIGE_EDITOR_THEME=${THEME}"
		"ORKIGE_DEMO_SCREENSHOT=${OUT_PNG}"
		"ORKIGE_DEMO_FRAMES=90"
		"${EDITOR_EXE}"
	RESULT_VARIABLE _exit)

if(NOT _exit EQUAL 0)
	message(FATAL_ERROR "editor exited ${_exit} while capturing the ${THEME} "
		"theme shot")
endif()
if(NOT EXISTS "${OUT_PNG}")
	message(FATAL_ERROR "no ${THEME} theme shot was written to ${OUT_PNG}")
endif()
file(SIZE "${OUT_PNG}" _pngBytes)
if(_pngBytes LESS 1000)
	message(FATAL_ERROR "the ${THEME} theme shot at ${OUT_PNG} is implausibly "
		"small (${_pngBytes} bytes) - the capture likely failed")
endif()
message(STATUS "editor ${THEME} theme window shot: ${OUT_PNG} (${_pngBytes} bytes)")
