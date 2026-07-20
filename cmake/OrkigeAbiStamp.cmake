# OrkigeAbiStamp.cmake - derive the engine's ABI version stamp from its sources.
#
# The stamp is a number that CHANGES whenever the engine source changes, so an
# EXACT find_package(Orkige <stamp>) match catches a native module compiled
# against newer headers than the static library it links (the stale-lib crash
# this guards - a header struct grows a member, the object layout the module
# sees no longer matches the code baked into the archive). See
# cmake/OrkigePackage.cmake (the engine records the stamp of the sources its
# libraries were built from) and cmake/OrkigeGameModule.cmake (the module
# computes the CURRENT source stamp and requires the package match it).
#
# The fingerprint is the committed HEAD plus the tracked working-tree diff
# (git diff HEAD): editing a committed header - even without committing - shifts
# the diff and therefore the stamp, so the guard fires on an uncommitted change
# too. Untracked new files do not shift it (both sides ignore them the same
# way), and a non-git tree (an unpacked source drop) collapses to one constant
# stamp - honest limits, documented in Docs/native-modules.md.

# orkige_compute_abi_stamp(<engine-source-root> <out_version_var> <out_stamp_var>)
#   out_version_var  <- the numeric package version ("2.0.<hi>.<lo>") the
#                       ConfigVersion file records and find_package(... EXACT)
#                       compares
#   out_stamp_var    <- a short human-readable stamp for diagnostics
function(orkige_compute_abi_stamp root out_version out_stamp)
    set(_fingerprint "")
    set(_short "nogit")
    find_program(_orkige_abi_git git)
    if(_orkige_abi_git)
        execute_process(COMMAND "${_orkige_abi_git}" -C "${root}" rev-parse HEAD
            OUTPUT_VARIABLE _head RESULT_VARIABLE _rc
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_rc EQUAL 0 AND _head)
            execute_process(COMMAND "${_orkige_abi_git}" -C "${root}" diff HEAD
                OUTPUT_VARIABLE _diff ERROR_QUIET)
            set(_fingerprint "${_head}${_diff}")
            string(SUBSTRING "${_head}" 0 10 _short)
        endif()
    endif()
    if(_fingerprint STREQUAL "")
        # non-git source drop: a single constant stamp (no source-change
        # tracking, but a consistent one both engine and module agree on)
        set(_fingerprint "orkige-no-git-abi")
    endif()
    string(MD5 _md5 "${_fingerprint}")
    string(SUBSTRING "${_md5}" 0 7 _hi7)
    string(SUBSTRING "${_md5}" 7 7 _lo7)
    math(EXPR _hi "0x${_hi7}")
    math(EXPR _lo "0x${_lo7}")
    string(SUBSTRING "${_md5}" 0 8 _md5short)
    set(${out_version} "2.0.${_hi}.${_lo}" PARENT_SCOPE)
    set(${out_stamp} "${_short}.${_md5short}" PARENT_SCOPE)
endfunction()
