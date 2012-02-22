# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

set(TargetName orkige_sceneoptimizer)

# ---------------------------------------------------------
# menuviewer Files
# ---------------------------------------------------------
FILE (GLOB sceneoptimizer_SOURCE_files "*.cpp")
FILE (GLOB sceneoptimizer_HEADER_files "*.h")

set(menuviewer_SOURCE
    # ----- Source -----
    "${sceneoptimizer_SOURCE_files}"
)

set(menuviewer_HEADER
    # ----- Headers -----
    "${sceneoptimizer_HEADER_files}"
)

source_group(sceneoptimizer FILES ${sceneoptimizer_HEADER} ${sceneoptimizer_SOURCE})

# ---------------------------------------------------------
# Build setup
# ---------------------------------------------------------
ConfigureOrkigeDependencies()
    
add_executable(
		${TargetName}
		WIN32
		${menuviewer_SOURCE}			${menuviewer_HEADER}
	)

