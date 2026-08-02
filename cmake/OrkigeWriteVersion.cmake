# OrkigeWriteVersion.cmake - CMake -P entry that (re)writes the package's
# OrkigeConfigVersion.cmake + OrkigeAbiStamp.txt for the CURRENT engine
# sources. Run at configure time (so the files exist before the first module
# build) and again as a POST_BUILD step of the engine libraries (so the stamp
# always reflects the sources the archive was last built from). See
# cmake/OrkigePackage.cmake.
#
# Required -D arguments:
#   ORKIGE_ROOT          the ABI surface root (an engine source tree, or an
#                        installed SDK pack - see cmake/OrkigeSdk.cmake)
#   ORKIGE_ABI_OUT_DIR   the dir to write the package version files into
#   ORKIGE_ABI_TAG       a per-writer tag (e.g. "configure"/"core"/"engine") so
#                        concurrent POST_BUILD writers stage to distinct temp
#                        files and rename atomically into place
#
# Optional -D arguments, for a surface laid out differently from the engine
# source tree (the SDK pack's merged include/ root): ORKIGE_ABI_SOURCE_DIRS and
# ORKIGE_ABI_EXTRA_FILES override the defaults OrkigeAbiStamp.cmake declares -
# same fingerprint function, different surface.

set(_orkige_abi_dirs_override "${ORKIGE_ABI_SOURCE_DIRS}")
set(_orkige_abi_extra_override "${ORKIGE_ABI_EXTRA_FILES}")
include("${CMAKE_CURRENT_LIST_DIR}/OrkigeAbiStamp.cmake")
include(CMakePackageConfigHelpers)
if(_orkige_abi_dirs_override)
    set(ORKIGE_ABI_SOURCE_DIRS ${_orkige_abi_dirs_override})
endif()
if(_orkige_abi_extra_override)
    set(ORKIGE_ABI_EXTRA_FILES ${_orkige_abi_extra_override})
endif()

orkige_compute_abi_stamp("${ORKIGE_ROOT}" _version _stamp)

set(_tmp_version "${ORKIGE_ABI_OUT_DIR}/OrkigeConfigVersion.${ORKIGE_ABI_TAG}.cmake")
write_basic_package_version_file("${_tmp_version}"
    VERSION "${_version}" COMPATIBILITY ExactVersion)
file(RENAME "${_tmp_version}" "${ORKIGE_ABI_OUT_DIR}/OrkigeConfigVersion.cmake")

set(_tmp_stamp "${ORKIGE_ABI_OUT_DIR}/OrkigeAbiStamp.${ORKIGE_ABI_TAG}.txt")
file(WRITE "${_tmp_stamp}" "${_stamp}")
file(RENAME "${_tmp_stamp}" "${ORKIGE_ABI_OUT_DIR}/OrkigeAbiStamp.txt")
