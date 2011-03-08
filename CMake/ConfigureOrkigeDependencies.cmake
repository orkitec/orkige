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
endmacro(ConfigureOrkigeDependencies)

