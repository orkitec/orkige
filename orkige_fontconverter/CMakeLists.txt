# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

set(TargetName orkige_fontconverter)

# ---------------------------------------------------------
# fontconverter Files
# ---------------------------------------------------------
FILE (GLOB fontconverter_SOURCE_files "*.cpp")
FILE (GLOB fontconverter_HEADER_files "*.h")

set(fontconverter_SOURCE
    # ----- Source -----
    "${fontconverter_SOURCE_files}"
)

set(fontconverter_HEADER
    # ----- Headers -----
    "${fontconverter_HEADER_files}"
)

source_group(fontconverter FILES ${fontconverter_HEADER} ${fontconverter_SOURCE})

# ---------------------------------------------------------
# Build setup
# ---------------------------------------------------------
ConfigureOrkigeDependencies()
    
add_executable(
		${TargetName}
		${fontconverter_SOURCE}			${fontconverter_HEADER}
	)
add_custom_command(
    TARGET ${TargetName}
    POST_BUILD
		COMMAND echo xcopy /y /h \"${CMAKE_CURRENT_SOURCE_DIR}/../Util/bmfontgen.exe\" \"${CMAKE_CURRENT_BINARY_DIR}\"
    COMMAND xcopy /y /h \"${CMAKE_CURRENT_SOURCE_DIR}/../Util\\bmfontgen.exe\" \"${CMAKE_CURRENT_BINARY_DIR}\"
)
add_custom_command(
    TARGET ${TargetName}
    POST_BUILD
		COMMAND echo xcopy /y /h \"${CMAKE_CURRENT_SOURCE_DIR}/../Util/sspack.exe\" \"${CMAKE_CURRENT_BINARY_DIR}\"
    COMMAND xcopy /y /h \"${CMAKE_CURRENT_SOURCE_DIR}/../Util\\sspack.exe\" \"${CMAKE_CURRENT_BINARY_DIR}\"
)
