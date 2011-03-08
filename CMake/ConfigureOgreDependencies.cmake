macro(ConfigureOgreDependencies)

	include_directories(
		${CMAKE_SOURCE_DIR}/Dependencies/Source/boost
		${ORKIGE_OGRE_INCLUDE}
		${ORKIGE_DEP_DIR}
		${ORKIGE_FREEIMAGE_INCLUDE}
		${ORKIGE_FREETYPE_INCLUDE}
		${ORKIGE_ZLIB_INCLUDE}
		${ORKIGE_ZZIP_INCLUDE}
		${ORKIGE_OIS_INCLUDE}
		./
	)
	
	link_libraries(
		OgreMain 
		${ORKIGE_FREEIMAGE_TARGET} 
		${ORKIGE_FREETYPE_TARGET} 
		${ORKIGE_ZLIB_TARGET} 
		${ORKIGE_ZZIP_TARGET}
		OIS
	)
	
	if(ORKIGE_BUILD_BOOST_REGEX)
		link_libraries(
			boost_regex
		)
	endif(ORKIGE_BUILD_BOOST_REGEX)

	link_libraries(
		OgreMain 
		${ORKIGE_FREEIMAGE_TARGET} 
		${ORKIGE_FREETYPE_TARGET} 
		${ORKIGE_ZLIB_TARGET} 
		${ORKIGE_ZZIP_TARGET}
		OIS
	)
	
	if (OGRE_BUILD_RENDERSYSTEM_GL)
		add_definitions(-DORKIGE_GLRS)

		include_directories(
			${OGRE_INCLUDE_DIR}/RenderSystems/GL/include
			${OGRE_INCLUDE_DIR}/RenderSystems/GL/src/GLSL/include
			${OGRE_INCLUDE_DIR}/RenderSystems/GL/src/atifs/include
			${OGRE_INCLUDE_DIR}/RenderSystems/GL/src/nvparse
		)
	
		link_libraries(
			RenderSystem_GL
			${OPENGL_gl_LIBRARY}
			${OPENGL_glu_LIBRARY}
		)
	endif()

	if (OGRE_BUILD_RENDERSYSTEM_GLES)
		
		add_definitions(-DORKIGE_GLESRS)

		include_directories(
			${OGRE_INCLUDE_DIR}/RenderSystems/GLES/include
			${OGRE_INCLUDE_DIR}/RenderSystems/GLES/include/EAGL			
		)
	
		link_libraries(
			RenderSystem_GLES
		)
		
		if(WIN32)
			include_directories(
				${OPENGLES_INCLUDE_DIR}
			)
	
			link_libraries(
				${OPENGLES_gl_LIBRARY}
			)
		endif(WIN32)
	endif()

	if (OGRE_BUILD_RENDERSYSTEM_D3D9)

		add_definitions(-DORKIGE_D3D9RS)
		
		include_directories(
			${OGRE_INCLUDE_DIR}/RenderSystems/Direct3D9/include
		)
		
		link_libraries(
			RenderSystem_Direct3D9
			${DirectX_D3D9_LIBRARY}
		)
	endif()
	
	if (OGRE_BUILD_RENDERSYSTEM_D3D10 AND DirectX_D3D10_FOUND)
		add_definitions(-DORKIGE_D3D10RS)
		
		include_directories(
			${OGRE_INCLUDE_DIR}/RenderSystems/Direct3D10/include
		)
		
		link_libraries(
			RenderSystem_Direct3D10
			${DirectX_D3D10_LIBRARIES}
		)
			
	endif()

	if (OGRE_BUILD_RENDERSYSTEM_D3D11 AND DirectX_D3D11_FOUND)
		add_definitions(-DORKIGE_D3D11RS)
		
		include_directories(
			${OGRE_INCLUDE_DIR}/RenderSystems/Direct3D11/include
		)
		
		link_libraries(
			$RenderSystem_Direct3D11
			${DirectX_D3D10_LIBRARIES}
		)
	endif()
	
	if (OGRE_BUILD_PLUGIN_CG)
		add_definitions(-DORKIGE_CG)
		
		include_directories(
			${OGRE_INCLUDE_DIR}/PlugIns/CgProgramManager/include
		)

		link_libraries(
			Plugin_CgProgramManager 
			${Cg_LIBRARY_REL}
		)
	endif()
	
	if (OGRE_BUILD_PLUGIN_OCTREE)
		add_definitions(-DORKIGE_OCTREE)
		
		include_directories(
			${OGRE_INCLUDE_DIR}/PlugIns/OctreeSceneManager/include
		)

		link_libraries(
			Plugin_OctreeSceneManager
		)
	endif()
	
	if (OGRE_BUILD_PLUGIN_PFX)
		add_definitions(-DORKIGE_PFX)
		
		include_directories(
			${OGRE_INCLUDE_DIR}/PlugIns/ParticleFX/include
		)

		link_libraries(
			Plugin_ParticleFX
		)
	endif()
	
	if (OGRE_BUILD_COMPONENT_PAGING)
		add_definitions(-DORKIGE_PAGING)
		
		ogre_add_component_include_dir(Paging)

		link_libraries(
			OgrePaging
		)
	endif()
	
	if (OGRE_BUILD_COMPONENT_TERRAIN)
		add_definitions(-DORKIGE_TERRAIN)
		
		ogre_add_component_include_dir(Terrain)
		
		link_libraries(
			OgreTerrain
		)
	endif()
	
endmacro(ConfigureOgreDependencies)

