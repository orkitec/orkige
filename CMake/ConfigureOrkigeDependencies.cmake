include(ConfigureOgreDependencies)

macro(ConfigureOrkigeDependencies)

	ConfigureOgreDependencies()

	include_directories(
		${ORKIGE_CORE_INCLUDE}
		${ORKIGE_ENGINE_INCLUDE}
	)
	
	link_libraries(
		orkige_core
		orkige_engine
	)
	
	if(ORKIGE_BUILD_ANDROID)
		link_libraries(
			log
		)
	endif()
	
	if(ORKIGE_OPENAL_SOUND)
		add_definitions(-DORKIGE_OPENAL_SOUND)
		include_directories(
			${ORKIGE_OPENAL_INCLUDE}
		)
	
		link_libraries(
			${ORKIGE_OPENAL_LIBRARY}
		)
	endif()
	
	if (ORKIGE_BUILD_OGGSOUNDMANAGER)		
		include_directories(
			${ORKIGE_OGGVORBIS_INCLUDE}
			${ORKIGE_DEP_DIR}/OgreOggSound/include
		)

		link_libraries(
			OggVorbis
			OgreOggSound
		)
	endif()

	if (ORKIGE_BUILD_THEORAVIDEOMANAGER)		
		include_directories(
			${ORKIGE_DEP_DIR}/Theora/include
			${ORKIGE_DEP_DIR}/libtheoraplayer/include
			${ORKIGE_DEP_DIR}/libtheoraplayer/include/theoraplayer
			${ORKIGE_DEP_DIR}/OgreVideo/include
		)

		link_libraries(
			Theora
			libtheoraplayer
			OgreVideo
		)
	endif()
	
	if (ORKIGE_BUILD_POCO)		
		include_directories(
			${ORKIGE_DEP_DIR}/poco/Foundation/include
			${ORKIGE_DEP_DIR}/poco/Net/include
		)

		link_libraries(
			optimized ${ORKIGE_DEP_DIR}/poco/lib/PocoFoundation.lib
			optimized ${ORKIGE_DEP_DIR}/poco/lib/PocoNet.lib
			debug ${ORKIGE_DEP_DIR}/poco/lib/PocoFoundationd.lib
			debug ${ORKIGE_DEP_DIR}/poco/lib/PocoNetd.lib
		)
	endif()

	if (ORKIGE_ENABLE_TUIO)		
		include_directories(
			${ORKIGE_DEP_DIR}/TUIO/TUIO
			${ORKIGE_DEP_DIR}/TUIO/oscpack
		)
		link_libraries(
			libTUIO
			ws2_32.lib 
			winmm.lib 
		)

	endif()
	
	
	if(ORKIGE_ENABLE_APPUP)
		include_directories(
			"C:/Program Files (x86)/Intel/IntelAppUpSDK/Cpp/lib"
			"C:/Program Files (x86)/Intel/IntelAppUpSDK/Cpp/include"
		)
		
		link_libraries(
				optimized "C:/Program Files (x86)/Intel/IntelAppUpSDK/Cpp/lib/adpcore.lib"
				optimized "C:/Program Files (x86)/Intel/IntelAppUpSDK/Cpp/lib/adpcppf.lib"
				debug "C:/Program Files (x86)/Intel/IntelAppUpSDK/Cpp/lib/adpcored.lib"
				debug "C:/Program Files (x86)/Intel/IntelAppUpSDK/Cpp/lib/adpcppfd.lib"
				Psapi
				Shlwapi
			)
	endif(ORKIGE_ENABLE_APPUP)

	if(ORKIGE_ENABLE_WINSDK)
		GET_FILENAME_COMPONENT(WINSDK_ROOT_PATH  "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Microsoft SDKs\\Windows\\v7.1;InstallationFolder]" ABSOLUTE CACHE)
		include_directories(
			${WINSDK_ROOT_PATH}/Include
			${WINSDK_ROOT_PATH}/Lib
		)
		
		link_libraries(
				${WINSDK_ROOT_PATH}/Lib/Vfw32.Lib
				
			)
	endif(ORKIGE_ENABLE_WINSDK)
	
	if(ORKIGE_ENABLE_JADEDS)
		include_directories(
			${ORKIGE_DEP_DIR}/JadeDS
		)
		
		link_libraries(
			JadeDS_OgrePlugin
		)
	endif(ORKIGE_ENABLE_JADEDS)
	
	if (ORKIGE_ENABLE_GAMESWF)		
		include_directories(
			${ORKIGE_DEP_DIR}/gameswf
			${ORKIGE_DEP_DIR}/gameswf/base
			${ORKIGE_DEP_DIR}/gameswf/gameswf
			${ORKIGE_DEP_DIR}/gameswf/gameswf/gameswf_as_classes
			${ORKIGE_DEP_DIR}/gameswf/gameswf/platforms
			${ORKIGE_DEP_DIR}/gameswf/gameswf/plugins/file

		)

		link_libraries(
			gameswf
		)
		add_definitions(-DTU_CONFIG_LINK_TO_LIBPNG=1)
		add_definitions(-DTU_CONFIG_LINK_TO_FFMPEG=0)
		add_definitions(-DTU_CONFIG_LINK_TO_LIB3DS=0)
		add_definitions(-DTU_CONFIG_LINK_TO_MYSQL=0)
		add_definitions(-DTU_CONFIG_LINK_TO_FREETYPE=1)
		add_definitions(-DTU_CONFIG_LINK_TO_THREAD=0)
		add_definitions(-DTU_ENABLE_NETWORK=0)
		add_definitions(-DTU_USE_FLASH_COMPATIBLE_HITTEST=0)
		add_definitions(-DTU_CONFIG_LINK_TO_JPEGLIB=1)
		add_definitions(-DTU_USE_SDL=0)
	endif()


	if (ORKIGE_ENABLE_MYGUI)		
		include_directories(
			${ORKIGE_DEP_DIR}/MyGui
			${ORKIGE_DEP_DIR}/MyGui/MyGUIEngine
			${ORKIGE_DEP_DIR}/MyGui/MyGUIEngine/include
			${ORKIGE_DEP_DIR}/MyGui/Platforms
			${ORKIGE_DEP_DIR}/MyGui/Platforms/Ogre
			${ORKIGE_DEP_DIR}/MyGui/Platforms/Ogre/OgrePlatform
			${ORKIGE_DEP_DIR}/MyGui/Platforms/Ogre/OgrePlatform/include
			${ORKIGE_DEP_DIR}/MyGui/Common
		)

		link_libraries(
			MyGUIEngine
			MyGUI_OgrePlatform
		)
		add_definitions(-DMYGUI_USE_FREETYPE)
		add_definitions(-DMYGUI_BUILD)
		add_definitions(-DMYGUI_STATIC)
	endif()
	
	if (ORKIGE_ENABLE_PARTICLE_UNIVERSE)		
		include_directories(
			${ORKIGE_DEP_DIR}/ParticleUniverse/include
			${ORKIGE_DEP_DIR}/ParticleUniverse/include/Externs
			${ORKIGE_DEP_DIR}/ParticleUniverse/include/ParticleAffectors
			${ORKIGE_DEP_DIR}/ParticleUniverse/include/ParticleBehaviours
			${ORKIGE_DEP_DIR}/ParticleUniverse/include/ParticleEmitters
			${ORKIGE_DEP_DIR}/ParticleUniverse/include/ParticleEventHandlers
			${ORKIGE_DEP_DIR}/ParticleUniverse/include/ParticleObservers
			${ORKIGE_DEP_DIR}/ParticleUniverse/include/ParticleRenderers

		)

		link_libraries(
			particleUniverse_OgrePlugin
		)
	endif()
endmacro(ConfigureOrkigeDependencies)

