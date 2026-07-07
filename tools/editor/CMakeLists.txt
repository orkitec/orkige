# orkige_editor - the in-engine editor shell (Phase 4 pulled forward).
# An Orkige app: SDL3 owns the window/event loop, Orkige::Engine renders into
# it (externalWindowHandle), the UI is Dear ImGui through OGRE's own
# ImGuiOverlay integration (no imgui SDL backend - see ImGuiSDL3Input.cpp).

# imgui comes into the build through OGRE's Overlay component anyway, but that
# link is $<LINK_ONLY:...>, so the editor needs its own find_package to get
# the imgui include directories for direct ImGui:: calls.
find_package(imgui CONFIG REQUIRED)

add_executable(orkige_editor
    main.cpp
    ImGuiSDL3Input.h
    ImGuiSDL3Input.cpp
)

target_link_libraries(orkige_editor PRIVATE
    Orkige::Engine
    imgui::imgui
)

# Editor-only: point at the RTSS shader library shipping with the
# vcpkg-installed OGRE so the editor runs straight out of the build tree
# (same approach as samples/hello_orkige).
target_compile_definitions(orkige_editor PRIVATE
    ORKIGE_EDITOR_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre/Media"
    ORKIGE_EDITOR_VERSION="${PROJECT_VERSION}"
)

if(APPLE)
    target_sources(orkige_editor PRIVATE native_window.mm)
endif()
