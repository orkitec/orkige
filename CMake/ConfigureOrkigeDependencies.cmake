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
	
	if(ORKIGE_ENABLE_APPUP)
		include_directories(
			"C:/Program Files (x86)/Intel/IntelAppUpSDK/Cpp/lib"
			"C:/Program Files (x86)/Intel/IntelAppUpSDK/Cpp/include"
		)

		link_libraries(
			"C:/Program Files (x86)/Intel/IntelAppUpSDK/Cpp/lib/adpcored.lib"
			"C:/Program Files (x86)/Intel/IntelAppUpSDK/Cpp/lib/adpcppfd.lib"
			Psapi
			Shlwapi
		)
	endif(ORKIGE_ENABLE_APPUP)
endmacro(ConfigureOrkigeDependencies)

