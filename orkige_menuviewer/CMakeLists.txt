# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

set(TargetName orkige_menuviewer)

# ---------------------------------------------------------
# menuviewer Files
# ---------------------------------------------------------
FILE (GLOB menuviewer_SOURCE_files "*.cpp")
FILE (GLOB menuviewer_HEADER_files "*.h")

set(menuviewer_SOURCE
    # ----- Source -----
    "${menuviewer_SOURCE_files}"
)

set(menuviewer_HEADER
    # ----- Headers -----
    "${menuviewer_HEADER_files}"
)

source_group(menuviewer FILES ${menuviewer_HEADER} ${menuviewer_SOURCE})

# ---------------------------------------------------------
# Build setup
# ---------------------------------------------------------
ConfigureOrkigeDependencies()
    
add_executable(
		${TargetName}
		WIN32
		${menuviewer_SOURCE}			${menuviewer_HEADER}
	)

