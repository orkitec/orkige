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
# The fingerprint is SCOPED to the engine's ABI surface ONLY: the committed
# content of the two engine layers (orkige_core/, orkige_engine/) plus the cmake
# files that shape how a module compiles and links against them, AND any
# uncommitted edits to that same set (git diff HEAD over the same pathspec). A
# change here - committed or not - moves the stamp and fires the guard; a change
# ANYWHERE ELSE (a game script, an asset, a doc, a test) does NOT, so the guard
# never fires on the ordinary edit-your-game-and-replay loop. Untracked new
# files do not shift it (both sides ignore them the same way), and a non-git
# tree (an unpacked source drop) collapses to one constant stamp - honest
# limits, documented in Docs/native-modules.md.

# the engine ABI surface: the two engine layers + the cmake files that define
# the module's compile/link surface (defines, imported-target interface, the
# package/stamp machinery). A pathspec deliberately EXCLUDING projects/,
# samples/, tests/, Docs/, Util/, .github/ - editing those must not bump the ABI.
set(ORKIGE_ABI_PATHSPEC
    orkige_core orkige_engine
    cmake/OrkigeGameModule.cmake cmake/OrkigeConfig.cmake.in
    cmake/OrkigePackage.cmake cmake/OrkigeAbiStamp.cmake
    cmake/OrkigeWriteVersion.cmake)

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
        # committed content of the engine surface (recursive blob listing:
        # <mode> blob <hash>\t<path> per file - changes when that content does)
        execute_process(COMMAND "${_orkige_abi_git}" -C "${root}"
                ls-tree -r HEAD -- ${ORKIGE_ABI_PATHSPEC}
            OUTPUT_VARIABLE _tree RESULT_VARIABLE _rc
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_rc EQUAL 0 AND _tree)
            # uncommitted edits to that SAME surface (a header change with no
            # commit still shifts the diff, hence the stamp)
            execute_process(COMMAND "${_orkige_abi_git}" -C "${root}"
                    diff HEAD -- ${ORKIGE_ABI_PATHSPEC}
                OUTPUT_VARIABLE _diff ERROR_QUIET)
            # the commit short id is carried into the human stamp only (context
            # for diagnostics); it is NOT part of the fingerprint, so an
            # unrelated commit does not change the ABI version
            execute_process(COMMAND "${_orkige_abi_git}" -C "${root}"
                    rev-parse --short HEAD
                OUTPUT_VARIABLE _short OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
            set(_fingerprint "${_tree}${_diff}")
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
