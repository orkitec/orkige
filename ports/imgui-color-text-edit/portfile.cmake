vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO goossens/ImGuiColorTextEdit
    REF a74fb090d2ea9276ae6c35c2f6ab39491c7d404f
    SHA512 eb8ef12565b43b79ac2f571a6933f64531578dfc001e66d50e89f2497bdfa0e8c18edd745266fe9919559ca04faa3d67d4cb36ff0e53b2a04aca49f6eb918d16
    HEAD_REF master
)

# our CMakeLists installed over the source (the upstream repo ships none for
# library use), same convention as ports/imgui. Only TextEditor.{h,cpp} build;
# TextDiff.* and its bundled dtl.h are omitted (keeps the port MIT-only).
file(COPY "${CMAKE_CURRENT_LIST_DIR}/imgui-color-text-edit-config.cmake.in" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS_DEBUG
        -DICTE_SKIP_HEADERS=ON
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
