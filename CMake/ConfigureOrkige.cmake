
macro (ConfigureOrkige)

	set(ROOT ${CMAKE_SOURCE_DIR})
	
	
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

	#ogre options
	option(OGRE_BUILD_PLUGIN_OCTREE              "Build Octree SceneManager plugin"    TRUE)
	option(OGRE_BUILD_PLUGIN_PFX                 "Build ParticleFX plugin"             TRUE)
	option(OGRE_BUILD_PLUGIN_CG                  "Build CG plugin"                     FALSE)
	option(OGRE_BUILD_COMPONENT_PAGING           "Build Paging component"              FALSE)
	option(OGRE_BUILD_COMPONENT_TERRAIN          "Build Terrain component"             FALSE)
	option(OGRE_BUILD_COMPONENT_RTSHADERSYSTEM   "Build OgreRTShaderSystem component"  FALSE)
	#end of ogre options
	option(ORKIGE_USE_OGRE_UNSTABLE              "Use Ogre Development Branch"         OFF)
	option(ORKIGE_BROWSERPLUGIN			"Build for Browser"							OFF)
	option(ORKIGE_NOSCRIPT				"Use Scripting Language"						ON)
	option(ORKIGE_ENABLE_MEMORYMANAGER	"Enable meory leak check (in debug builds)"			ON)
	option(ORKIGE_DEBUG					"Enable debugging information in release builds"			ON)
	option(ORKIGE_EXTERN_LOG				"Enable specifiying an external logfile"				OFF)

	if(ORKIGE_BUILD_IPHONE)
		option(ORKIGE_ENABLE_FLURRYANALYTICS "Use FlurryAnalytics" OFF)
		option(ORKIGE_ENABLE_TESTFLIGHT "Use FlurryAnalytics" OFF)
	endif()
	if(NOT ORKIGE_NOSCRIPT)
		option(ORKIGE_USE_LUA			"Use Lua script bindings"						OFF)
	endif()
	option(ORKIGE_BUILD_GAME				"Build Game"								ON)
	option(ORKIGE_BUILD_MAXEXPORTER		"Build 3dsmax Exporter"							OFF)
	option(ORKIGE_BUILD_FONTCONVERTER 		"Build orkige_fontconverter" 						OFF)
	option(ORKIGE_BUILD_MENUVIEWER 		"Build orkige_menuviewer" 						OFF)
	option(ORKIGE_BUILD_SCENEOPTIMIZER 		"Build orkige_sceneoptimizer" 						OFF)
	option(ORKIGE_UPDATE_DOCS			"Update Orkige API documentation(Requires doxygen)."	OFF)
	option(ORKIGE_BUILD_OGGSOUNDMANAGER 	"enable ogg vorbis sound playback" 				ON)
	option(ORKIGE_BUILD_THEORAVIDEOMANAGER	"enable theora video player"						OFF)
	option(ORKIGE_BUILD_OGITOR			"enable Ogitor build"							OFF)
	option(ORKIGE_BUILD_BOOST_REGEX		"enable building of boost regex build"				OFF)
	option(ORKIGE_ENABLE_PROFILER			"enable engine profiling"							ON)
	option(ORKIGE_ENABLE_GAMESWF			"enable gameswf"							OFF)
	option(ORKIGE_ENABLE_MYGUI			"enable mygui gui system"							OFF)
	option(ORKIGE_OPTIMIZE_SIZE			"heavily siize optimisations"						OFF)
	option(ORKIGE_MINIMAL_FREEIMAGE_CODEC	"Compile minimal FreeImage Codec(PNG/JPEG/TGA)" OFF)
	if (WIN32)
				option(ORKIGE_ENABLE_WINSDK			"enable AWINSDK"							OFF)
				option(ORKIGE_ENABLE_APPUP			"enable APPUP"							OFF)	
				option(ORKIGE_ENABLE_JADEDS			"enable Jade:DS / Little Indie"			OFF)	
				option(ORKIGE_BUILD_POCO			"enable Poco network library"			OFF)
				option(ORKIGE_ENABLE_TUIO			"enable TUIO"			OFF)
	endif (WIN32)
	option(ORKIGE_ENABLE_PARTICLE_UNIVERSE		"enable Particle Universe Plugin"			OFF)
	option(ORKIGE_BUILD_ANDROID	"Build GameKit on Android SDK"	OFF)
	option(ORKIGE_OPENALSOFT_SOUND "Uses software implementation of OpenAL" OFF)
	option(ORKIGE_STATIC	"Build Orkige Engine Static Library Version"	ON)
  
	# Unity build options
	# A Unity build includes all sources files in just a few actual compilation units
	# to potentially speed up the compilation.
	option(OGRE_UNITY_BUILD "Enable unity build for Ogre." ON)
	set(OGRE_UNITY_FILES_PER_UNIT "50" CACHE STRING "Number of files per compilation unit in Unity build.")


  if (WIN32 AND OGRE_UNITY_BUILD)
    # object files can get large with Unity builds
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
  endif ()
	
	if(ORKIGE_BROWSERPLUGIN)
		set(ORKIGE_ENABLE_PROFILER FALSE CACHE BOOL "enable engine profiling"   FORCE)
		set(ORKIGE_ENABLE_MEMORYMANAGER FALSE CACHE BOOL "Enable meory leak check (in debug builds)"   FORCE)
		set(ORKIGE_EXTERN_LOG TRUE CACHE BOOL "Enable specifiying an external logfile"   FORCE)
	endif(ORKIGE_BROWSERPLUGIN)
	
	if(ORKIGE_BUILD_OGITOR)
		set(ORKIGE_BUILD_BOOST_REGEX TRUE CACHE BOOL "enable building of boost regex build"   FORCE)
	endif(ORKIGE_BUILD_OGITOR)
	
	add_definitions(-DBOOST_ALL_NO_LIB)	
	
	if(ORKIGE_ENABLE_APPUP)
		add_definitions(-DORKIGE_ENABLE_APPUP)	
	endif(ORKIGE_ENABLE_APPUP)

	if(ORKIGE_BUILD_POCO)
		add_definitions(-DORKIGE_BUILD_POCO)	
	endif(ORKIGE_BUILD_POCO)

	if(ORKIGE_ENABLE_TUIO)
		add_definitions(-DORKIGE_ENABLE_TUIO)	
	endif(ORKIGE_ENABLE_TUIO)


	if(ORKIGE_ENABLE_WINSDK)
		add_definitions(-DORKIGE_ENABLE_WINSDK)	
	endif(ORKIGE_ENABLE_WINSDK)
	
	if(ORKIGE_ENABLE_JADEDS)
		add_definitions(-DORKIGE_ENABLE_JADEDS)	
	endif(ORKIGE_ENABLE_JADEDS)
	
	if(ORKIGE_ENABLE_PARTICLE_UNIVERSE)
		add_definitions(-DOGRE_STATIC_ParticleUniverse)
	endif(ORKIGE_ENABLE_PARTICLE_UNIVERSE)

	if(ORKIGE_ENABLE_GAMESWF)
		add_definitions(-DORKIGE_ENABLE_GAMESWF)	
	endif(ORKIGE_ENABLE_GAMESWF)
	
	if(ORKIGE_ENABLE_MYGUI)
		add_definitions(-DORKIGE_ENABLE_MYGUI)	
	endif(ORKIGE_ENABLE_MYGUI)
	
	if(ORKIGE_USE_OGRE_UNSTABLE)
		set(OGRELITEDIRECTORY OgreLiteUnstable)
	else()
		set(OGRELITEDIRECTORY OgreLite)
	endif(ORKIGE_USE_OGRE_UNSTABLE)
	
	if(OGRE_BUILD_COMPONENT_RTSHADERSYSTEM)
		add_definitions(-DUSE_RTSHADER_SYSTEM)
	endif(OGRE_BUILD_COMPONENT_RTSHADERSYSTEM)

  if(ORKIGE_STATIC)
     add_definitions(-DORKIGE_STATIC)
     set(ORKIGE_LIB_TYPE STATIC)
  else()
     set(ORKIGE_LIB_TYPE SHARED)
  endif(ORKIGE_STATIC)
  
	set(OGREPATH ${CMAKE_SOURCE_DIR}/${OGRELITEDIRECTORY})
	
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
	
	if (ORKIGE_ENABLE_PROFILER)
		add_definitions(-DORKIGE_ENABLE_PROFILER)	
	endif()
	
	if (ORKIGE_BUILD_OGGSOUNDMANAGER)
		add_definitions(-DHAVE_EFX=0)
		add_definitions(-DORKIGE_OGGSOUNDMANAGER)	
		add_definitions(-DOGREOGGSOUND_STATIC=1)
		add_definitions(-DOGGSOUND_THREADED=0)
	endif()
	
	if (ORKIGE_BUILD_THEORAVIDEOMANAGER)
		set(ORKIGE_BUILD_OGGSOUNDMANAGER TRUE CACHE BOOL "enable ogg vorbis sound playback" FORCE)
		add_definitions(-DORKIGE_THEORAVIDEOMANAGER)	
		add_definitions(-DTHEORAVIDEO_STATIC)
		add_definitions(-DOGREVIDEO_STATIC=1)
	endif()

	

	set(ORKIGE_ZLIB_TARGET ZLib)
	set(ORKIGE_ZZIP_TARGET ZZipLib)
	set(ORKIGE_FREEIMAGE_TARGET FreeImage)
	set(ORKIGE_FREETYPE_TARGET freetype)
	set(ORKIGE_LUA_TARGET Lua)
	
	set(OGRE_BINARY_DIR ${OGREPATH}/Bin)
	set(OGRE_TEMPLATES_DIR ${ROOT}/CMake/Templates)
	set(OGRELITE_SOURCE_DIR ${OGREPATH})

	include(OgreAddTargets)
	include(OgreConfigTargets)
	include(DependenciesOrkige)
	include(MacroLogFeature)

	if (APPLE)
		set( CMAKE_CXX_FLAGS "-Wno-reorder -Wno-deprecated -Wno-write-strings -Wno-switch" )
		add_definitions(-Wno-unused)
		option(ORKIGE_BUILD_IPHONE	"Build Orkige on IPhone SDK"	OFF)
		option(ORKIGE_BUILD_IPAD	"Build Orkige on IPhone SDK"	OFF)
		if (ORKIGE_BUILD_IPAD)
			set(ORKIGE_BUILD_IPHONE ON CACHE BOOL "Build Orkige on IPhone SDK"  FORCE)
			add_definitions(-DORKIGE_IPAD)
		endif(ORKIGE_BUILD_IPAD)
		
		if (ORKIGE_BUILD_IPHONE)
			option(ORKIGE_USE_COCOA "Use Cocoa" ON)
			option(ORKIGE_COMPILE_FOR_THUMB "Thumb Mode" OFF)
			option(ORKIGE_IPHONE_ADHOC_BUILD "Adhoc build" OFF)
			option(ORKIGE_MULTITOUCH_TO_MOUSE	"Convert Multitouch events to mouse events" ON)
			option(ORKIGE_OPTIMIZED_ARMV7		"Build code optimized for armv7 processor (iPhone 3GS and above)"	TRUE)

			if(NOT ORKIGE_OPTIMIZE_SIZE)
				if(ORKIGE_COMPILE_FOR_THUMB)
					set(XCODE_ATTRIBUTE_GCC_THUMB_SUPPORT "YES")
					add_definitions(-mthumb)
				else()
					set(XCODE_ATTRIBUTE_GCC_THUMB_SUPPORT "NO")
					add_definitions(-mno-thumb)	
				endif()
			endif()
			
			if(ORKIGE_MULTITOUCH_TO_MOUSE)
				add_definitions(-DORKIGE_MULTITOUCH_TO_MOUSE)
			endif()
			set(OGRE_BUILD_PLATFORM_IPHONE TRUE)
			set(OGRE_BUILD_PLATFORM_APPLE_IOS TRUE)
			add_definitions(-DORKIGE_IPHONE)
			if(ORKIGE_USE_OGRE_UNSTABLE)
				set(ORKIGE_PLATFORM ${OGREPATH}/OgreMain/include/iOS )
				include_directories("${OGREPATH}/OgreMain/include/iOS")
			else(ORKIGE_USE_OGRE_UNSTABLE)
				set(ORKIGE_PLATFORM ${OGREPATH}/OgreMain/include/IPhone )
				include_directories("${OGREPATH}/OgreMain/include/iPhone")
			endif(ORKIGE_USE_OGRE_UNSTABLE)
			# Set build variables
			set(CMAKE_OSX_SYSROOT iphoneos5.1)
			set(CMAKE_OSX_DEPLOYMENT_TARGET "")
			set(CMAKE_EXE_LINKER_FLAGS "-framework Foundation -framework CoreGraphics -framework QuartzCore -framework UIKit -framework AudioToolbox -framework MediaPlayer -framework SystemConfiguration -weak_framework GameKit -weak_framework iAd")
			set(XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer: Steffen Roemer")
			set(XCODE_ATTRIBUTE_SDKROOT iphoneos4.2)
			set(OGRE_STATIC TRUE CACHE BOOL "Forcing static build for iPhone" FORCE)
			set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.orkitec.\${PRODUCT_NAME:rfc1034identifier}")
			set(OGRE_CONFIG_ENABLE_VIEWPORT_ORIENTATIONMODE TRUE CACHE BOOL "Forcing viewport orientation support for iPhone" FORCE)
	
			# CMake 2.8.1 added the ability to specify per-target architectures.
			# As a side effect, it creates corrupt Xcode projects if you try do it for the whole project.
			if(ORKIGE_OPTIMIZED_ARMV7)
				set(CMAKE_OSX_ARCHITECTURES armv7)
				set(XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH YES)
				set(XCODE_ATTRIBUTE_VALID_ARCHS armv7)
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
			set(CMAKE_XCODE_EFFECTIVE_PLATFORMS "-iphoneos;-iphonesimulator")
		else(ORKIGE_BUILD_IPHONE)
			set(ORKIGE_PLATFORM ${OGREPATH}/OgreMain/include/OSX )
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
		endif(ORKIGE_BUILD_IPHONE)
	else (APPLE)
		if (UNIX)
			set(ORKIGE_PLATFORM ${OGREPATH}/OgreMain/include/GLX )
		else (UNIX)
			if (WIN32)
				set(ORKIGE_PLATFORM ${OGREPATH}/OgreMain/include/WIN32 )
			endif (WIN32)
		endif (UNIX)
	endif (APPLE)
	
	set(Boost_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/Dependencies/Source/boost)
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
	set(ORKIGE_CORE_INCLUDE ${ROOT}/orkige_core)
	set(ORKIGE_ENGINE_INCLUDE ${ROOT}/orkige_engine)
	
	set(ORKIGE_DEP_INCLUDE
		${ORKIGE_FREEIMAGE_INCLUDE}
		${ORKIGE_FREETYPE_INCLUDE}
		${ORKIGE_ZLIB_INCLUDE}
		${ORKIGE_ZZIP_INCLUDE}
		${ORKIGE_OIS_INCLUDE}
		${ORKIGE_LUA_INCLUDE}
		${ORKIGE_OGGVORBIS_INCLUDE}
	)

	if (WIN32 AND NOT ORKIGE_BUILD_ANDROID)
		# Use static library. No SDK needed at build time.
		# Must have OpenAL32.dll installed on the system 
		# In order to use OpenAL sound.
		set(OPENAL_FOUND TRUE)
  elseif(APPLE)
    #do nothing on mac and iOS
  else()
    #build special version of openal soft (from source) to support android)
    set(ORKIGE_OPENALSOFT_SOUND TRUE CACHE BOOL "Enable building of the OpenAL subsystem" FORCE)
	endif()
	
	
	
	if(ORKIGE_OPENALSOFT_SOUND)
		set(OPENAL_INCLUDE_DIR 
		    ${ORKIGE_DEP_DIR}/OpenALsoft/include
		    ${ORKIGE_DEP_DIR}/OpenALsoft/include/AL 
		)
		add_definitions(-DORKIGE_OPENALSOFT_SOUND)

    if (WIN32)
		  set(OPENAL_LIBRARY OpenAL32)
    else()
      set(OPENAL_LIBRARY openal)
    endif(WIN32)
		set(OPENAL_FOUND TRUE)
	endif()

	if (OPENAL_FOUND)
		option(ORKIGE_OPENAL_SOUND "Enable building of the OpenAL subsystem" ON)
		if(ORKIGE_OPENALSOFT_SOUND)
		  set(ORKIGE_OPENAL_SOUND TRUE CACHE BOOL "Enable building of the OpenAL subsystem" FORCE)
		endif()

    if(ORKIGE_OPENAL_SOUND)
      if (WIN32 AND NOT ORKIGE_OPENALSOFT_SOUND)
			 add_definitions(-DAL_STATIC_LIB -DALC_STATIC_LIB)
			 set(ORKIGE_OPENAL_INCLUDE ${ORKIGE_DEP_DIR}/OpenAL/)
			 set(ORKIGE_OPENAL_LIBRARY OpenAL)
		  else()
			 set(ORKIGE_OPENAL_INCLUDE ${OPENAL_INCLUDE_DIR})
			 set(ORKIGE_OPENAL_LIBRARY ${OPENAL_LIBRARY})
		  endif()
    endif(ORKIGE_OPENAL_SOUND)
	else()
		option(ORKIGE_OPENAL_SOUND "Enable building of the OpenAL subsystem" OFF)
	endif()

	if (WIN32)
		if (DirectX_FOUND)
			# Default use OIS without dinput 
			option(ORKIGE_OIS_WIN32_NATIVE "Enable building of the OIS Win32 backend" OFF)
		else ()
			# Use standard OIS build.
			option(ORKIGE_OIS_WIN32_NATIVE "Enable building of the OIS Win32 backend" ON)
		endif()
	endif()

	if (DirectX_FOUND)
		option(OGRE_BUILD_RENDERSYSTEM_D3D9 "Enable the Direct3D9 render system" ON)
		option(OGRE_BUILD_RENDERSYSTEM_D3D10 "Enable the Direct3D10 render system" OFF)
		option(OGRE_BUILD_RENDERSYSTEM_D3D11 "Enable the Direct3D11 render system" OFF)
	endif()
	
	if (OPENGL_FOUND)
		option(OGRE_BUILD_RENDERSYSTEM_GL "Enable the OpenGL render system" ON)
	endif()
	
	if (OPENGLES_FOUND)
		option(OGRE_BUILD_RENDERSYSTEM_GLES "Enable the OpenGLES system" ON)
	endif()
  
  if (OPENGLES2_FOUND)
		option(OGRE_BUILD_RENDERSYSTEM_GLES2 "Enable the OpenGLES2 system" OFF)
	endif()
		
	if (APPLE)
		if (ORKIGE_BUILD_IPHONE)
			set(OGRE_BUILD_RENDERSYSTEM_GL CACHE BOOL "Forcing remove OpenGL RenderSystem for iPhone" FORCE)
			if(ORKIGE_USE_OGRE_UNSTABLE)
				set(OGRE_BUILD_RENDERSYSTEM_GLES2 TRUE CACHE BOOL "Forcing use OpenGLES RenderSystem for iPhone" FORCE)
			else()
				set(OGRE_BUILD_RENDERSYSTEM_GLES TRUE CACHE BOOL "Forcing use OpenGLES2 RenderSystem for iPhone" FORCE)
			endif()			
			
			set(OGRE_BUILD_PLUGIN_CG CACHE BOOL "Forcing remove CG for iPhone"   FORCE)
			set(ORKIGE_USE_COCOA  TRUE CACHE BOOL "Forcing use COCOA for iPhone" FORCE)
			
			if (OGRE_BUILD_RENDERSYSTEM_GL)
				message(SEND_ERROR "Turn OFF OGRE_BUILD_RENDERSYSTEM_GL Option for iPhone")
			endif()
			if(ORKIGE_USE_OGRE_UNSTABLE)
				if (NOT OGRE_BUILD_RENDERSYSTEM_GLES2)
					message(SEND_ERROR "Turn ON OGRE_BUILD_RENDERSYSTEM_GLES2 Option for iPhone")
				endif()
			else()
				if (NOT OGRE_BUILD_RENDERSYSTEM_GLES)
					message(SEND_ERROR "Turn ON OGRE_BUILD_RENDERSYSTEM_GLES Option for iPhone")
				endif()
			endif()			
			
			
		else()
			set(OGRE_BUILD_RENDERSYSTEM_GL TRUE CACHE BOOL "Forcing use OpenGL RenderSystem for OS X" FORCE)
			set(OGRE_BUILD_RENDERSYSTEM_GLES CACHE BOOL "Forcing remove OpenGLES RenderSystem for OS X" FORCE)
			
			if (NOT OGRE_BUILD_RENDERSYSTEM_GL)
				message(SEND_ERROR "Turn ON OGRE_BUILD_RENDERSYSTEM_GL Option for OS X")
			endif()
			if (OGRE_BUILD_RENDERSYSTEM_GLES)
				message(SEND_ERROR "Turn OFF OGRE_BUILD_RENDERSYSTEM_GLES Option for OS X")
			endif()
		endif()
	endif()		
  
  if (ORKIGE_BUILD_ANDROID)
    set(OGRE_BUILD_RENDERSYSTEM_GLES2 TRUE CACHE BOOL "Forcing use OpenGLES2 RenderSystem for Android" FORCE)
  endif()
  
  if (OGRE_BUILD_RENDERSYSTEM_GLES2)
      if(NOT ORKIGE_BUILD_IPHONE)
        add_definitions(-DFREEIMAGE_BIGENDIAN)
      endif()
      #we us gles2 so disable gl and gles(1.1) since else we get compile conflicts with gl.h
      set(OGRE_BUILD_RENDERSYSTEM_GL CACHE BOOL "Forcing remove OpenGL RenderSystem for iPhone" FORCE)
      if (OPENGLES_FOUND)
        set(OGRE_BUILD_RENDERSYSTEM_GLES CACHE BOOL "Forcing use OpenGLES RenderSystem for iPhone" FORCE)
      endif(OPENGLES_FOUND)
      #android specific settings
      if (ORKIGE_BUILD_ANDROID)
        if(DirectX_FOUND)
          #disable cg and direct3d on windows
          set(OGRE_BUILD_RENDERSYSTEM_D3D9 CACHE BOOL "Direct3D9 render system" FORCE)
          set(OGRE_BUILD_RENDERSYSTEM_D3D10 CACHE BOOL "Direct3D10 render system" FORCE)
          set(OGRE_BUILD_RENDERSYSTEM_D3D11 CACHE BOOL "Direct3D11 render system" FORCE)
          set(OGRE_BUILD_PLUGIN_CG CACHE BOOL "Forcing remove CG for iPhone"   FORCE)
        elseif(APPLE)
          #disable cocoa on mac
          set(ORKIGE_USE_COCOA  CACHE BOOL "Forcing use COCOA for iPhone" FORCE)
        endif()
			endif(ORKIGE_BUILD_ANDROID)

      #disable paging end terrain since its not supported for gles
      set(OGRE_BUILD_COMPONENT_PAGING CACHE BOOL "Build Paging component"   FORCE)
      set(OGRE_BUILD_COMPONENT_TERRAIN CACHE BOOL "Build Terrain component"   FORCE)
      #disable memory manager for gles2 sinceits crashing
      set(ORKIGE_ENABLE_MEMORYMANAGER CACHE BOOL "Enable meory leak check (in debug builds)"   FORCE)
      
      #enable ogre unstable (1.8)
      set(ORKIGE_USE_OGRE_UNSTABLE	TRUE CACHE BOOL "Use Ogre Development Branch" FORCE)
      set(OGRE_CONFIG_ENABLE_PVRTC	TRUE CACHE BOOL "Enable PowerVR Texture compressio" FORCE)
      
      #enable rtsahder and core shaders
      set(OGRE_BUILD_COMPONENT_RTSHADERSYSTEM TRUE CACHE BOOL "Forcing use OpenGLES2 RenderSystem for Android" FORCE)
  endif()
  
  #build rtshader builtin shaders
  cmake_dependent_option(OGRE_BUILD_RTSHADERSYSTEM_CORE_SHADERS "Build RTShader System FFP core shaders" TRUE "OGRE_BUILD_COMPONENT_RTSHADERSYSTEM" FALSE)
  cmake_dependent_option(OGRE_BUILD_RTSHADERSYSTEM_EXT_SHADERS "Build RTShader System extensions shaders" TRUE "OGRE_BUILD_COMPONENT_RTSHADERSYSTEM" FALSE)

  if (OGRE_CONFIG_ENABLE_PVRTC)
	 set(OGRE_NO_PVRTC_CODEC 0)
  else ()
	 set(OGRE_NO_PVRTC_CODEC 1)
  endif ()
  
  if (OGRE_BUILD_RTSHADERSYSTEM_CORE_SHADERS)
	 set(RTSHADER_SYSTEM_BUILD_CORE_SHADERS 1)
  else ()
	 set(RTSHADER_SYSTEM_BUILD_CORE_SHADERS 0)
  endif ()

  if (OGRE_BUILD_RTSHADERSYSTEM_EXT_SHADERS)	
	 set(RTSHADER_SYSTEM_BUILD_EXT_SHADERS 1)
  else ()
	 set(RTSHADER_SYSTEM_BUILD_EXT_SHADERS 0)
  endif ()

endmacro(ConfigureOrkige)

include(ConfigureOrkigeDependencies)
