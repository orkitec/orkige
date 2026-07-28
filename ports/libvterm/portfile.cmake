vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO neovim/libvterm
    REF 934bc2fbf21800ac3458a499df8820ca5fb45fd3
    SHA512 f1cc6dfba8ddd230792428384215c72361b1024f7029eaf38592277b363458e6f6d392585fd1810e9385fa7eb8962f8b739b18d9951fde3899ee95423e541b7b
    HEAD_REF master
)

# our CMakeLists builds the library the upstream Makefile would (this fork ships
# the pre-generated encoding .inc headers, so no Perl runs at build time), same
# convention as ports/imgui / ports/imgui-color-text-edit.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/libvterm-config.cmake.in"
    DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup()

# headers ship once (release tree); the debug build installs an identical copy
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
