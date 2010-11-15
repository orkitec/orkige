
macro (configure_orkige ROOT OGREPATH)

	set(GNUSTEP_SYSTEM_ROOT $ENV{GNUSTEP_SYSTEM_ROOT})
	
	if(APPLE OR GNUSTEP_SYSTEM_ROOT)
		
		if (WIN32 AND NOT CMAKE_COMPILER_IS_GNUCXX)
			set(ORKIGE_USE_COCOA FALSE CACHE BOOL "Forcing remove Use Cocoa" FORCE)
		else()
			option(ORKIGE_USE_COCOA	"Use Cocoa"	ON)
		endif()
		
		
	endif()
	
	if(ORKIGE_USE_COCOA)
		add_definitions(-DORKIGE_USE_COCOA)
		if(GNUSTEP_SYSTEM_ROOT)
			include_directories(${GNUSTEP_SYSTEM_ROOT}/Library/Headers)
			link_directories(${GNUSTEP_SYSTEM_ROOT}/Library/Libraries)
			link_libraries("gnustep-base")
			link_libraries("objc")
		endif()
	endif()

	set(ORKIGE_INSTALL_PREFIX ${ROOT}/Bin)
	#ogre options
	option(OGRE_BUILD_PLUGIN_OCTREE "Build Octree SceneManager plugin" TRUE)
	option(OGRE_BUILD_PLUGIN_PFX "Build ParticleFX plugin" FALSE)
	option(OGRE_BUILD_COMPONENT_PAGING "Build Paging component" TRUE)
	option(OGRE_BUILD_COMPONENT_TERRAIN "Build Terrain component" TRUE)
	#end of ogre options
	option(ORKIGE_BROWSERPLUGIN      "Build for Browser" OFF)
	option(ORKIGE_NOSCRIPT              "Use Scripting Language" ON)
	option(ORKIGE_ENABLE_MEMORYMANAGER  "Enable meory leak check (in debug builds)" ON)
	option(ORKIGE_DEBUG                 "Enable debugging information in release builds" ON)
	option(ORKIGE_EXTERN_LOG            "Enable specifiying an external logfile" OFF)
	option(ORKIGE_USE_LUA               "Use Lua script bindings" OFF)

	if (ORKIGE_USE_LUA)
		add_definitions(-DORKIGE_USE_LUA)
		add_definitions(-DORKIGE_LUA)	
	endif()
	
	if (ORKIGE_NOSCRIPT)
		add_definitions(-DORKIGE_NOSCRIPT)	
	endif()
	
	if (ORKIGE_ENABLE_MEMORYMANAGER)
		add_definitions(-DORKIGE_ENABLE_MEMORYMANAGER)	
	endif()
	
	if (ORKIGE_DEBUG)
		add_definitions(-DORKIGE_DEBUG)	
	endif()
	
	if (ORKIGE_EXTERN_LOG)
		add_definitions(-DORKIGE_EXTERN_LOG)	
	endif()

	set(ORKIGE_ZLIB_TARGET	ZLib)
	set(ORKIGE_ZZIP_TARGET ZZipLib)
	set(ORKIGE_FREEIMAGE_TARGET FreeImage)
	set(ORKIGE_FREETYPE_TARGET freetype)
	set(ORKIGE_V8_TARGET V8)
	set(ORKIGE_LUA_TARGET Lua)
	

	set(OGRE_BINARY_DIR ${OGREPATH}/Bin)
	set(OGRE_TEMPLATES_DIR ${ROOT}/CMake/Templates)
	set(OGRELITE_SOURCE_DIR ${OGREPATH})

	include(OgreConfigTargets)
	include(DependenciesOrkige)
	include(MacroLogFeature)

	if (APPLE)
		option(ORKIGE_BUILD_IPHONE	"Build Orkige on IPhone SDK"	OFF)
		if (ORKIGE_BUILD_IPHONE)
			option(ORKIGE_USE_COCOA "Use Cocoa" ON)
			option(ORKIGE_COMPILE_FOR_THUMB "Thumb Mode" OFF)
			if(ORKIGE_COMPILE_FOR_THUMB)
				add_definitions(-mthumb)
			else()
				add_definitions(-mno-thumb)	
			endif()
			option(ORKIGE_MULTITOUCH_TO_MOUSE	"Convert Multitouch events to mouse events"	ON)
			if(ORKIGE_MULTITOUCH_TO_MOUSE)
				add_definitions(-DORKIGE_MULTITOUCH_TO_MOUSE)
			endif()
			set(OGRE_BUILD_PLATFORM_IPHONE TRUE)
			add_definitions(-DORKIGE_IPHONE)
			set(ORKIGE_PLATFORM ${OGREPATH}/OgreMain/include/IPhone )
		else()
			set(ORKIGE_PLATFORM ${OGREPATH}/OgreMain/include/OSX )
		endif()
	  else (APPLE)
		if (UNIX)
		set(ORKIGE_PLATFORM ${OGREPATH}/OgreMain/include/GLX )
		else (UNIX)
		  if (WIN32)
		set(ORKIGE_PLATFORM ${OGREPATH}/OgreMain/include/WIN32 )
		  endif (WIN32)
		endif (UNIX)
	endif (APPLE)
	
	option(BUILD_GAME        "Build Game"     ON)

	option(BUILD_MAXEXPORTER        "Build 3dsmax Exporter"     OFF)
	
	#copy from ogre3d build
	# Set up iPhone overrides.
	if (OGRE_BUILD_PLATFORM_IPHONE)
	  include_directories("${OGREPATH}/OgreMain/include/iPhone")
	
	  # Set build variables
	  set(CMAKE_OSX_SYSROOT iphoneos4.1)
	  set(CMAKE_OSX_DEPLOYMENT_TARGET "")
	  set(CMAKE_EXE_LINKER_FLAGS "-framework Foundation -framework CoreGraphics -framework QuartzCore -framework UIKit -framework AudioToolbox")
		set(XCODE_ATTRIBUTE_GCC_THUMB_SUPPORT "NO")
		set(XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer: Steffen Roemer")
	  set(XCODE_ATTRIBUTE_SDKROOT iphoneos4.1)
	  set(OGRE_BUILD_RENDERSYSTEM_GLES TRUE CACHE BOOL "Forcing OpenGL ES RenderSystem for iPhone" FORCE)
	  set(OGRE_STATIC TRUE CACHE BOOL "Forcing static build for iPhone" FORCE)
	  set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.orkitec.\${PRODUCT_NAME:rfc1034identifier}")
	  set(OGRE_CONFIG_ENABLE_VIEWPORT_ORIENTATIONMODE TRUE CACHE BOOL "Forcing viewport orientation support for iPhone" FORCE)
	
	  # CMake 2.8.1 added the ability to specify per-target architectures.
	  # As a side effect, it creates corrupt Xcode projects if you try do it for the whole project.
	  if(VERSION STRLESS "2.8.1")
			set(CMAKE_OSX_ARCHITECTURES $(ARCHS_STANDARD_32_BIT))
	  else()
			set(CMAKE_OSX_ARCHITECTURES "armv6;armv7;")
	  endif()
	
	  add_definitions(-fno-regmove)
	  remove_definitions(-msse)
	  
	  if(VERSION STRLESS "2.8.1")
		message(STATUS "Copy iphone sdk files to " ${CMAKE_BINARY_DIR})
		file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/SDK/iPhone/edit_linker_paths.sed DESTINATION ${CMAKE_BINARY_DIR})
		file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/SDK/iPhone/fix_linker_paths.sh DESTINATION ${CMAKE_BINARY_DIR})
		
	  endif()
	  
	if (NOT OGRE_CONFIG_ENABLE_VIEWPORT_ORIENTATIONMODE)
		set(OGRE_SET_DISABLE_VIEWPORT_ORIENTATIONMODE 1)
	endif()
	  
	elseif (APPLE)
	
	  # Set 10.4 as the base SDK by default
	  set(XCODE_ATTRIBUTE_SDKROOT macosx10.4)
	
	  if (NOT CMAKE_OSX_ARCHITECTURES)
		set(CMAKE_OSX_ARCHITECTURES "i386")
	  endif()
	  
	  # 10.6 sets x86_64 as the default architecture.
	  # Because Carbon isn't supported on 64-bit and we still need it, force the architectures to ppc and i386
	  if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" OR CMAKE_OSX_ARCHITECTURES MATCHES "ppc64")
		string(REPLACE "x86_64" "" CMAKE_OSX_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES})
		string(REPLACE "ppc64" "" CMAKE_OSX_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES})
	  endif()
	
	  # Make sure that the OpenGL render system is selected for non-iPhone Apple builds
	  set(OGRE_BUILD_RENDERSYSTEM_GL TRUE)
	  set(OGRE_BUILD_RENDERSYSTEM_GLES FALSE)
	  
	endif ()

	option(ORKIGE_UPDATE_DOCS "Update Orkige API documentation(Requires doxygen)." OFF)



	set(ORKIGE_DEP_DIR ${ROOT}/Dependencies/Source)
	set(ORKIGE_FREEIMAGE_INCLUDE ${ORKIGE_DEP_DIR}/FreeImage)
	set(ORKIGE_FREETYPE_INCLUDE ${ORKIGE_DEP_DIR}/FreeType/include)
	set(ORKIGE_ZLIB_INCLUDE ${ORKIGE_DEP_DIR}/FreeImage/ZLib)
	set(ORKIGE_ZZIP_INCLUDE ${ORKIGE_DEP_DIR}/ZZipLib)
	set(ORKIGE_OIS_INCLUDE ${ORKIGE_DEP_DIR}/OIS/include)
	set(OGRE_INCLUDE_DIR ${OGREPATH})
	set(ORKIGE_OGRE_INCLUDE ${OGREPATH}/OgreMain/include ${OGREPATH}/Settings ${ORKIGE_PLATFORM})
	set(ORKIGE_LUA_INCLUDE ${ORKIGE_DEP_DIR}/Lua/lua)
	set(ORKIGE_OGGVORBIS_INCLUDE ${ORKIGE_DEP_DIR}/Codecs/include)
	
	set(ORKIGE_RECAST_INCLUDE ${ORKIGE_DEP_DIR}/Recast/Include)
	set(ORKIGE_DETOUR_INCLUDE ${ORKIGE_DEP_DIR}/Detour/Include)
	set(ORKIGE_OPENSTEER_INCLUDE ${ORKIGE_DEP_DIR}/OpenSteer/include)
	

	set(ORKIGE_DEP_INCLUDE
		${ORKIGE_FREEIMAGE_INCLUDE}
		${ORKIGE_FREETYPE_INCLUDE}
		${ORKIGE_ZLIB_INCLUDE}
		${ORKIGE_ZZIP_INCLUDE}
		${ORKIGE_OIS_INCLUDE}
		${ORKIGE_LUA_INCLUDE}
		${ORKIGE_OGGVORBIS_INCLUDE}
		${ORKIGE_RECAST_INCLUDE}
		${ORKIGE_DETOUR_INCLUDE}
		${ORKIGE_OPENSTEER_INCLUDE}
	)




	if (WIN32)
		# Use static library. No SDK needed at build time.
		# Must have OpenAL32.dll installed on the system 
		# In order to use OpenAL sound.
		set(OPENAL_FOUND TRUE)
	endif()


	if (OPENAL_FOUND)
		option(ORKIGE_OPENAL_SOUND "Enable building of the OpenAL subsystem" ON)
		
		if (WIN32)
			add_definitions(-DAL_STATIC_LIB -DALC_STATIC_LIB)
			set(ORKIGE_OPENAL_INCLUDE ${ORKIGE_DEP_DIR}/OpenAL/)
			set(ORKIGE_OPENAL_LIBRARY OpenAL)
		else()
			set(ORKIGE_OPENAL_INCLUDE ${OPENAL_INCLUDE_DIR})
			set(ORKIGE_OPENAL_LIBRARY ${OPENAL_LIBRARY})
		endif()
	else()
		option(ORKIGE_OPENAL_SOUND "Enable building of the OpenAL subsystem" OFF)
	endif()


	set(ORKIGE_MINGW_DIRECT3D TRUE)
	if (CMAKE_COMPILER_IS_GNUCXX)
		# Some Issues with unresolved symbols
		set(ORKIGE_MINGW_DIRECT3D FALSE)
	endif()



	if (WIN32)
		if (NOT DirectX_FOUND OR NOT ORKIGE_MINGW_DIRECT3D)
			# Default use OIS without dinput 

			option(ORKIGE_OIS_WIN32_NATIVE "Enable building of the OIS Win32 backend" ON)
		else ()
			# Use standard OIS build.

			option(ORKIGE_OIS_WIN32_NATIVE "Enable building of the OIS Win32 backend" OFF)
		endif()
	endif()




	if (OPENGL_FOUND)
		option(ORKIGE_BUILD_GLRS "Enable the OpenGL render system" ON)
	endif()
	
	if (OPENGLES_FOUND)
		option(ORKIGE_BUILD_GLESRS "Enable the OpenGLES system" ON)
	endif()

	if (OPENGL_FOUND AND ORKIGE_BUILD_GLRS)


		set(OGRE_BUILD_RENDERSYSTEM_GL  TRUE)
		set(ORKIGE_GLRS_LIBS           RenderSystem_GL)
		set(ORKIGE_GLRS_ROOT           ${OGREPATH}/RenderSystems/GL)
		set(ORKIGE_GLESRS_INCLUDE      ${OGREPATH}/RenderSystems/GLES/include)
		set(ORKIGE_GLRS_INCLUDE        ${OGREPATH}/RenderSystems/GL/include)
	endif()

	if (OPENGLES_FOUND AND ORKIGE_BUILD_GLESRS)
		
		set(OGRE_BUILD_RENDERSYSTEM_GLES TRUE)
		set(ORKIGE_GLESRS_LIBS          RenderSystem_GLES)
		set(ORKIGE_GLESRS_ROOT          ${OGREPATH}/RenderSystems/GLES)
		set(ORKIGE_GLESRS_INCLUDE       ${OGREPATH}/RenderSystems/GLES/include)
		set(ORKIGE_GLRS_INCLUDE         ${OGREPATH}/RenderSystems/GL/include)
	endif()


	if (WIN32 AND ORKIGE_MINGW_DIRECT3D)

		if (DirectX_FOUND)
			option(ORKIGE_BUILD_D3D9RS	 "Enable the Direct3D 9 render system" ON)
			option(ORKIGE_BUILD_D3D10RS "Enable the Direct3D 10 render system" OFF)
			option(ORKIGE_BUILD_D3D11RS "Enable the Direct3D 11 render system" OFF)
		endif()

		if (DirectX_FOUND AND ORKIGE_BUILD_D3D9RS)
			set(OGRE_BUILD_RENDERSYSTEM_D3D9   TRUE)
			set(ORKIGE_D3D9_LIBS              RenderSystem_Direct3D9)
			set(ORKIGE_D3D9_ROOT              ${OGREPATH}/RenderSystems/Direct3D9)
			set(ORKIGE_DX9RS_INCLUDE          ${OGREPATH}/RenderSystems/Direct3D9/include)
		endif()

		if (DirectX_FOUND AND ORKIGE_BUILD_D3D10RS)

			set(OGRE_BUILD_RENDERSYSTEM_D3D10 TRUE)
			set(ORKIGE_D3D10_LIBS            RenderSystem_Direct3D10)
			set(ORKIGE_D3D10_ROOT            ${OGREPATH}/RenderSystems/Direct3D10)
			set(ORKIGE_DX10RS_INCLUDE        ${OGREPATH}/RenderSystems/Direct3D10/include)
		endif()


		if (DirectX_FOUND AND ORKIGE_BUILD_D3D11RS)

			set(OGRE_BUILD_RENDERSYSTEM_D3D11  TRUE)
			set(ORKIGE_D3D11_LIBS             RenderSystem_Direct3D11)
			set(ORKIGE_D3D11_ROOT             ${OGREPATH}/RenderSystems/Direct3D11)
			set(ORKIGE_DX11RS_INCLUDE         ${OGREPATH}/RenderSystems/Direct3D11/include)
		endif()


	endif()
	
	#if (0)
		# disable until support is added  
		option(ORKIGE_BUILD_CG	 "Enable the CG plugin" ON)

		if (ORKIGE_BUILD_CG)
			set(OGRE_BUILD_PLUGIN_CG       TRUE)
			set(ORKIGE_CG_LIBS            Plugin_CgProgramManager)
			set(ORKIGE_CG_ROOT            ${OGREPATH}/PlugIns/CgProgramManager)
			set(ORKIGE_CG_INCLUDE         ${OGREPATH}/PlugIns/CgProgramManager/include)
		endif()

	#endif()

	set(ORKIGE_OGRE_LIBS 
		OgreMain 
		${ORKIGE_FREEIMAGE_TARGET} 
		${ORKIGE_FREETYPE_TARGET} 
		${ORKIGE_ZLIB_TARGET} 
		${ORKIGE_ZZIP_TARGET}
		${ORKIGE_GLRS_LIBS}
		${ORKIGE_D3D9_LIBS}
		${ORKIGE_D3D10_LIBS}
		${ORKIGE_D3D11_LIBS}
		${ORKIGE_RECAST_TARGET}
		${ORKIGE_DETOUR_TARGET}
		${ORKIGE_OPENSTEER_TARGET}
		${ORKIGE_OPENAL_LIBRARY}
		)
		
	if (APPLE)
		if (ORKIGE_BUILD_IPHONE)
			set(OGRE_BUILD_RENDERSYSTEM_GL CACHE BOOL "Forcing remove OpenGL RenderSystem for iPhone" FORCE)
			set(OGRE_BUILD_RENDERSYSTEM_GLES TRUE CACHE BOOL "Forcing use OpenGLES RenderSystem for iPhone" FORCE)
						
			set(ORKIGE_BUILD_CG   CACHE BOOL "Forcing remove CG for iPhone"   FORCE)
			set(ORKIGE_USE_COCOA  TRUE CACHE BOOL "Forcing use COCOA for iPhone" FORCE)
			
			if (ORKIGE_BUILD_GLRS)
				message(SEND_ERROR "Turn OFF ORKIGE_BUILD_GLRS Option for iPhone")
			endif()
			if (NOT ORKIGE_BUILD_GLESRS)
				message(SEND_ERROR "Turn ON ORKIGE_BUILD_GLESRS Option for iPhone")
			endif()
		else()
			set(OGRE_BUILD_RENDERSYSTEM_GL TRUE CACHE BOOL "Forcing use OpenGL RenderSystem for OS X" FORCE)
			set(OGRE_BUILD_RENDERSYSTEM_GLES CACHE BOOL "Forcing remove OpenGLES RenderSystem for OS X" FORCE)
			
			if (NOT ORKIGE_BUILD_GLRS)
				message(SEND_ERROR "Turn ON ORKIGE_BUILD_GLRS Option for OS X")
			endif()
			if (ORKIGE_BUILD_GLESRS)
				message(SEND_ERROR "Turn OFF ORKIGE_BUILD_GLESRS Option for OS X")
			endif()
		endif()
	endif()

endmacro(configure_orkige)


macro(configure_rendersystem)

	if (OGRE_BUILD_RENDERSYSTEM_GL)
		
		add_definitions(-DORKIGE_GLRS)

		include_directories(
			${ORKIGE_GLRS_ROOT}/include
			${ORKIGE_GLRS_ROOT}/src/GLSL/include
			${ORKIGE_GLRS_ROOT}/src/atifs/include
		)
	
		link_libraries(
			${ORKIGE_GLRS_LIBS} 
			${OPENGL_gl_LIBRARY}
			${OPENGL_glu_LIBRARY}
		)
		
	endif()

	if (OGRE_BUILD_RENDERSYSTEM_GLES)
		
		add_definitions(-DORKIGE_GLESRS)

		include_directories(
			${ORKIGE_GLESRS_ROOT}/include
			${ORKIGE_GLESRS_ROOT}/include/EAGL			
		)
	
		link_libraries(
			${ORKIGE_GLESRS_LIBS} 
		)
		
	endif()

	if (ORKIGE_BUILD_D3D9RS)

		add_definitions(-DORKIGE_D3D9RS)
		
		include_directories(
			${ORKIGE_D3D9_ROOT}/include
		)
		
		link_libraries(
			${DirectX_D3D9_LIBRARY}
			${ORKIGE_D3D9_LIBS} 
		)
			
	endif()
	
	if (ORKIGE_BUILD_D3D10RS AND DirectX_D3D10_FOUND)
		add_definitions(-DORKIGE_D3D10RS)
		
		include_directories(
			${ORKIGE_D3D10_ROOT}/include
		)
		
		link_libraries(
			${ORKIGE_D3D10_LIBS} 
			${DirectX_D3D10_LIBRARIES}
		)
			
	endif()

	if (ORKIGE_BUILD_D3D11RS AND DirectX_D3D11_FOUND)
		add_definitions(-DORKIGE_D3D11RS)
		
		include_directories(
			${ORKIGE_D3D11_ROOT}/include
		)
		
		link_libraries(
			${ORKIGE_D3D11_LIBS} 
			${DirectX_D3D10_LIBRARIES}
		)

	endif()
	
	if (0)
		add_definitions(-DORKIGE_CG)
		
		include_directories(
			${ORKIGE_CG_ROOT}/include
		)

		link_libraries(
			${ORKIGE_CG_LIBS} 
			${Cg_LIBRARY_REL}
		)

	endif()
endmacro(configure_rendersystem)

