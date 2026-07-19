# orkige_player - the standalone scene player.
# Boots the engine exactly like samples/hello_orkige (SDL3 window,
# externalWindowHandle path), loads the .oscene file given as argv[1] through
# SceneSerializer and runs the game loop. This is the runtime the editor's
# play mode builds on.

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iOS: the player is an app bundle (preset ios-simulator-debug). SDL3
    # provides the UIKit application bootstrap (main.cpp includes SDL_main.h),
    # OGRE renders through the GLES2/EAGL path into SDL's UIWindow. All media
    # is copied into the flat .app bundle below and found at runtime via
    # PlatformUtil::getResourceDirectory() (see main.cpp, ORKIGE_IPHONE paths).
    add_executable(orkige_player MACOSX_BUNDLE main.cpp
        PlayerContext.h PlayerSelfChecks.h PlayerSelfChecks.cpp)
    set_target_properties(orkige_player PROPERTIES
        OUTPUT_NAME "OrkigePlayer"
        MACOSX_BUNDLE_BUNDLE_NAME "Orkige Player"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.orkitec.orkige-player"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        # custom plist: adds UILaunchScreen so the app boots at native full
        # resolution (the CMake default omits it -> scaled compatibility mode).
        # The project exporter rewrites identity/name and adds icons on top.
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Android (preset android-debug): SDL3's application model - the APK's
    # OrkigeActivity (extends SDLActivity, see tools/player/android/) does
    # System.loadLibrary("main") and calls the SDL_main inside it, so the
    # player builds as the shared lib libmain.so with EVERYTHING (SDL3, OGRE,
    # engine, core) statically linked in. Media ships in the APK's assets/
    # and is extracted to the app files dir on first launch (see main.cpp);
    # tools/player/android/package_apk.sh assembles + signs the APK.
    add_library(orkige_player SHARED main.cpp
        PlayerContext.h PlayerSelfChecks.h PlayerSelfChecks.cpp)
    set_target_properties(orkige_player PROPERTIES OUTPUT_NAME "main")
    # SDL's Java<->native glue: libSDL3.a registers its natives from
    # JNI_OnLoad (RegisterNatives) - force the linker to keep that object
    # even though no C++ code references it
    target_link_options(orkige_player PRIVATE "-Wl,--undefined=JNI_OnLoad")
else()
    add_executable(orkige_player main.cpp
        PlayerContext.h PlayerSelfChecks.h PlayerSelfChecks.cpp)
endif()

target_link_libraries(orkige_player PRIVATE Orkige::Engine)

if(EMSCRIPTEN)
    # Browser player (preset web-release): the executable is a wasm module +
    # JS loader pair; the classic GLES2 render system draws through WebGL on
    # the page's canvas. Scoped to this target - the unit-test binary and any
    # future web tool pick their own flags.
    #   MAX_WEBGL_VERSION allow a WebGL2 context where the driver asks for
    #                    GLES3-level features (WebGL1 remains the floor)
    #   FORCE_FILESYSTEM the project payload + engine media arrive as a
    #                    file_packager .data image (export packaging), so the
    #                    FS layer must exist even before any C fopen is seen
    #   -lopenal         Emscripten's WebAudio-backed OpenAL implementation
    #                    (the same AL surface openal-soft provides natively)
    target_link_options(orkige_player PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=5242880
        -sMAX_WEBGL_VERSION=2
        -sFORCE_FILESYSTEM=1
        -sEXIT_RUNTIME=1
        -lopenal
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # the engine's backend media differs per flavor: classic ships the RTSS
    # shader library (Media/{Main,RTShaderLib}); next ships the Hlms shader
    # templates plus the Atmosphere sky material media (Media/Hlms,
    # Media/Atmosphere - the next backend's registerAtmosphereMedia looks up
    # Atmosphere/ as a sibling of Hlms/). main.cpp resolves both under
    # <bundle>/Media.
    if(ORKIGE_RENDER_BACKEND STREQUAL "next")
        set(ORKIGE_PLAYER_IOS_MEDIA_COPY
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre-next/Media/Hlms"
                "$<TARGET_BUNDLE_CONTENT_DIR:orkige_player>/Media/Hlms")
        # optional: an older vcpkg port pin may not ship the sky media yet -
        # the runtime degrades that honestly (no sky, flat fog colour)
        if(EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre-next/Media/Atmosphere")
            list(APPEND ORKIGE_PLAYER_IOS_MEDIA_COPY
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre-next/Media/Atmosphere"
                    "$<TARGET_BUNDLE_CONTENT_DIR:orkige_player>/Media/Atmosphere")
        endif()
    else()
        set(ORKIGE_PLAYER_IOS_MEDIA_COPY
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre/Media/Main"
                "$<TARGET_BUNDLE_CONTENT_DIR:orkige_player>/Media/Main"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre/Media/RTShaderLib"
                "$<TARGET_BUNDLE_CONTENT_DIR:orkige_player>/Media/RTShaderLib")
    endif()
    # bundle the backend media, the sample assets the scenes reference, and
    # the example scene itself
    add_custom_command(TARGET orkige_player POST_BUILD
        ${ORKIGE_PLAYER_IOS_MEDIA_COPY}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/samples/hello_orkige/media"
            "$<TARGET_BUNDLE_CONTENT_DIR:orkige_player>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/samples/jumper/media"
            "$<TARGET_BUNDLE_CONTENT_DIR:orkige_player>/jumper_media"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/samples/scenes/example.oscene"
            "$<TARGET_BUNDLE_CONTENT_DIR:orkige_player>/example.oscene"
        COMMENT "Bundling media + example scene into OrkigePlayer.app"
    )
endif()

# Player-only: point at the RTSS shader library shipping with the
# vcpkg-installed OGRE so the player runs straight out of the build tree
# (same approach as samples/hello_orkige and tools/editor). Real games ship
# their own media and use a resources.cfg instead.
target_compile_definitions(orkige_player PRIVATE
    ORKIGE_PLAYER_MEDIA_DIR="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ogre/Media"
    # sample assets (test_mesh.glb, generated by Util/make_test_mesh.py) -
    # scenes saved from the editor reference meshes from this directory
    ORKIGE_PLAYER_ASSET_DIR="${CMAKE_SOURCE_DIR}/samples/hello_orkige/media"
    # jumper sample assets (textured .glb meshes) so the editor's play mode
    # works on samples/jumper/level1.oscene too
    ORKIGE_PLAYER_JUMPER_ASSET_DIR="${CMAKE_SOURCE_DIR}/samples/jumper/media"
    # the engine-default font dir (Nunito) - the out-of-bundle fallback the
    # player registers when running straight out of the build tree (an exported
    # bundle carries the same fonts under its Media/ dir instead)
    ORKIGE_PLAYER_FONT_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/fonts"
    # the engine water media dir (the shared water plane mesh + tiling water
    # normal map WaterComponent references) - the out-of-bundle fallback the
    # player registers when running straight out of the build tree (an exported
    # bundle carries the same files under its Media/water dir instead)
    ORKIGE_PLAYER_WATER_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/water"
    # the engine decal media dir (the default mark + blob-shadow textures the
    # DecalComponent references) - the out-of-bundle build-tree fallback (an
    # exported bundle carries the same files under its Media/decals dir instead)
    ORKIGE_PLAYER_DECAL_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/decals"
)
# the engine bloom compositor media (bright/blur/combine material + shaders
# engine:setBloom needs) - PER FLAVOR: bloom/next (the quad-chain shaders) vs
# bloom/classic (the viewport-compositor shaders). The baked source-tree dir is
# the dev fallback; an exported bundle carries the same files under
# Media/<subdir> (the bundled-first pair in main.cpp).
if(ORKIGE_RENDER_BACKEND STREQUAL "next")
    set(ORKIGE_BLOOM_MEDIA_SUBDIR "bloom/next")
else()
    set(ORKIGE_BLOOM_MEDIA_SUBDIR "bloom/classic")
endif()
target_compile_definitions(orkige_player PRIVATE
    ORKIGE_PLAYER_BLOOM_DIR="${Orkige_SOURCE_DIR}/orkige_engine/media/${ORKIGE_BLOOM_MEDIA_SUBDIR}"
    ORKIGE_BLOOM_MEDIA_SUBDIR="${ORKIGE_BLOOM_MEDIA_SUBDIR}")
