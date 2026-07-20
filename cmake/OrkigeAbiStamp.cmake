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
# It is a CONTENT FINGERPRINT of the engine source surface, git-INDEPENDENT: it
# hashes the actual on-disk bytes of every engine source file plus their paths.
# That covers EVERY case uniformly - committed, uncommitted, a brand-new
# untracked header the module includes, or a plain tarball drop with no git at
# all - because it reads the files, not the VCS state.
#
# SCOPED to the engine's ABI surface ONLY: the compiled source of the two engine
# layers (orkige_core/, orkige_engine/) plus the cmake files that define how a
# module compiles and links against them (their CMakeLists + the package/link
# helpers). A change ANYWHERE ELSE - a game script, an asset, a doc, a test,
# anything under projects/, samples/, tests/, Docs/, Util/, .github/ - does NOT
# move it, so the guard never fires on the ordinary edit-your-game-and-replay
# loop. Runtime media (orkige_engine/media/*: shaders, fonts, meshes, textures)
# is deliberately EXCLUDED - it is bundled assets, not compiled object layout,
# so its extensions are not in the source set below.

# the engine source dirs globbed recursively, and the compiled-source
# extensions to match within them (verified against the tree: .h .cpp .mm .inc
# are what the engine uses; the rest are future-proofing and match nothing today)
set(ORKIGE_ABI_SOURCE_DIRS orkige_core orkige_engine)
set(ORKIGE_ABI_SOURCE_EXTENSIONS h hpp hh hxx inc ipp cpp cc cxx c mm)

# the build/link surface files outside the source dirs (or that a source glob
# skips): the engine layers' CMakeLists define the compiled sources + ABI
# compile definitions, and the cmake package/link helpers define the module's
# compile/link surface. Few and explicit; a missing one is simply skipped.
set(ORKIGE_ABI_EXTRA_FILES
    orkige_core/CMakeLists.txt
    orkige_engine/CMakeLists.txt
    cmake/OrkigeGameModule.cmake
    cmake/OrkigeConfig.cmake.in
    cmake/OrkigePackage.cmake
    cmake/OrkigeAbiStamp.cmake
    cmake/OrkigeWriteVersion.cmake)

# orkige_compute_abi_stamp(<engine-source-root> <out_version_var> <out_stamp_var>)
#   out_version_var  <- the numeric package version ("2.0.<hi>.<lo>") the
#                       ConfigVersion file records and find_package(... EXACT)
#                       compares
#   out_stamp_var    <- a short human-readable stamp for diagnostics
function(orkige_compute_abi_stamp root out_version out_stamp)
    # every compiled engine source file, recursively (build outputs live under
    # build/, never inside the source dirs, so nothing generated is picked up)
    set(_globs "")
    foreach(_dir IN LISTS ORKIGE_ABI_SOURCE_DIRS)
        foreach(_ext IN LISTS ORKIGE_ABI_SOURCE_EXTENSIONS)
            list(APPEND _globs "${root}/${_dir}/*.${_ext}")
        endforeach()
    endforeach()
    file(GLOB_RECURSE _files LIST_DIRECTORIES false ${_globs})
    foreach(_extra IN LISTS ORKIGE_ABI_EXTRA_FILES)
        if(EXISTS "${root}/${_extra}")
            list(APPEND _files "${root}/${_extra}")
        endif()
    endforeach()
    # deterministic order, then hash each file's bytes keyed by its relative path
    # (so a rename - same content, new path - is a change too)
    list(SORT _files)
    set(_digest "")
    foreach(_file IN LISTS _files)
        file(MD5 "${_file}" _hash)
        file(RELATIVE_PATH _rel "${root}" "${_file}")
        string(APPEND _digest "${_rel}\t${_hash}\n")
    endforeach()
    if(_digest STREQUAL "")
        # no engine sources under root (a wrong root) - a constant, but
        # consistent, stamp so both sides still agree instead of crashing
        set(_digest "orkige-empty-abi-surface")
    endif()
    string(MD5 _md5 "${_digest}")
    string(SUBSTRING "${_md5}" 0 7 _hi7)
    string(SUBSTRING "${_md5}" 7 7 _lo7)
    math(EXPR _hi "0x${_hi7}")
    math(EXPR _lo "0x${_lo7}")
    string(SUBSTRING "${_md5}" 0 12 _md5short)
    set(${out_version} "2.0.${_hi}.${_lo}" PARENT_SCOPE)
    set(${out_stamp} "content.${_md5short}" PARENT_SCOPE)
endfunction()
