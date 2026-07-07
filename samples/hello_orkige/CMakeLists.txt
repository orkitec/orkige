add_executable(hello_orkige main.cpp)

target_link_libraries(hello_orkige PRIVATE Orkige::Engine)

# Demo-only: point the sample at the RTSS shader library that ships with the
# vcpkg-installed OGRE so it runs straight out of the build tree. Real games
# ship their own media and use a resources.cfg instead.
target_compile_definitions(hello_orkige PRIVATE
    ORKIGE_DEMO_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre/Media"
)

if(APPLE)
    target_sources(hello_orkige PRIVATE native_window.mm)
endif()
