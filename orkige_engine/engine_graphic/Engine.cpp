/********************************************************************
	created:	Tuesday 2010/09/07 at 17:01
	filename: 	Engine.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "engine_graphic/Engine.h"
#include "engine_module/EnginePrerequisites.h"
#include "engine_util/StringUtil.h"
#include <core_event/GlobalEventManager.h>
#include <core_debug/Profile.h>
#ifdef OGRE_STATIC_LIB
// static build: render systems are linked in and registered via installPlugin
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS || OGRE_PLATFORM == OGRE_PLATFORM_ANDROID
#include <OgreGLES2Plugin.h>
#else
#include <OgreGL3PlusPlugin.h>
#endif
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE || OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
#include <OgreMetalPlugin.h>
#endif
#ifdef ORKIGE_HAVE_VULKAN
#include <OgreVulkanPlugin.h>
#include <OgreGLSLangProgramManager.h>
#endif
#include <OgreSTBICodec.h>
#include <OgreAssimpLoader.h>
#endif
#include <cctype>
#ifdef ORKIGE_HAVE_VULKAN
#include <cstdlib>
#include <filesystem>
#endif
#ifdef ORKIGE_IPHONE
#   ifdef __OBJC__
#       import <UIKit/UIKit.h>
#   endif
#endif
namespace Orkige
{
#if defined(WIN32) || defined(__ANDROID__)
#ifdef ORKIGE_DEBUG
	struct LogListener : public Ogre::LogListener
	{
		virtual void messageLogged( const Ogre::String& message, Ogre::LogMessageLevel lml, bool maskDebug, const String &logName, bool& skipThisMessage )
		{
			if(skipThisMessage) return;
			if(lml < Ogre::LML_CRITICAL)
			{
				OutputDebugStringA(("Ogre Info: " + message + "\n").c_str());
			}
			else
			{
				OutputDebugStringA(("Ogre Error: " + message + "\n").c_str());
			}
		}
	};
#endif
#endif
	IMPL_OWNED_EVENTTYPE(Engine, FrameStartedEvent);
	IMPL_OWNED_EVENTTYPE(Engine, FrameRenderingQueuedEvent);
	IMPL_OWNED_EVENTTYPE(Engine, FrameEndedEvent);
	IMPL_OSINGLETON(Engine);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Engine::Engine(Ogre::String const & smTypeName, String const & resourceCfgFileName, String const & pluginCfgFileName, String const & renderCfgFileName, String const & engineLogFileName, unsigned int _numberOfWindows, String const & zipFileName, String const & zipInternalPathPrefix)
		: frameStartedEvent(Engine::FrameStartedEvent),
		frameRenderingQueuedEvent(Engine::FrameRenderingQueuedEvent),
		frameEndedEvent(Engine::FrameEndedEvent),
		sceneManagerTypeName(smTypeName),
		eventManager(NULL),
		sceneManager(NULL),
		defaultLocationType("FileSystem"),
/*
		renderWindow(),
		camera(),
		viewport(),*/

		lastFrameTime(0),
		numberOfWindows(_numberOfWindows)
	{
#ifdef USE_RTSHADER_SYSTEM
			mShaderGenerator	 = NULL;		
			mMaterialMgrListener = NULL;
#endif // USE_RTSHADER_SYSTEM
		String renderCfgPlatformFileName = this->getPlatformSpecificConfig(renderCfgFileName);
		String resourceCfgPlatformFileName = this->getPlatformSpecificConfig(resourceCfgFileName);

		oDebugMsg("core", 0, "Setting 2 RenderConfigFile to: " << renderCfgPlatformFileName);
		oDebugMsg("core", 0, "Setting 2 ResourceConfigFile to: " << resourceCfgPlatformFileName);
		
		oAssert((this->numberOfWindows > 0) && (this->numberOfWindows <= MAX_MUMBER_OF_WINDOWS));
		oDebugMsg("core", 0, "Setting number of windows to: " << this->numberOfWindows);
		for (unsigned int each = 0; each < MAX_MUMBER_OF_WINDOWS; ++each)
		{
			this->renderWindow[each] = NULL;
			this->camera[each] = NULL;
			this->viewport[each] = NULL;
			this->windowParams[each] = Ogre::NameValuePairList();
		}

		this->data = onew(new FrameEventData());

		this->frameStartedEvent.setData(this->data);
		this->frameRenderingQueuedEvent.setData(this->data);
		this->frameEndedEvent.setData(this->data);
		//create the ogre root Master of Disaster

#ifdef OGRE_STATIC_LIB
		// static build: no plugin config file, render systems are registered below
		this->root = optr<Ogre::Root>(new Ogre::Root(Ogre::BLANKSTRING, renderCfgPlatformFileName, engineLogFileName));
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS || OGRE_PLATFORM == OGRE_PLATFORM_ANDROID
		// iOS/Android: the GLES2 render system (OGRE forces GLES2 on for
		// APPLE_IOS/ANDROID; deprecated by Apple but works incl. the simulator
		// and emulator and has a full RTSS path - the Metal RS below does not,
		// see the comment there)
		this->renderSystemPlugin = onew(new Ogre::GLES2Plugin());
#else
		this->renderSystemPlugin = onew(new Ogre::GL3PlusPlugin());
#endif
		this->root->installPlugin(this->renderSystemPlugin.get());
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE || OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
		// Metal render system (OGRE 14.5): selectable via setPreferredRenderSystem
		// but NOT the default - the RTShaderSystem has no MSL backend, so Metal
		// runs on OGRE's built-in default shaders only (see configure())
		this->metalRenderSystemPlugin = onew(new Ogre::MetalPlugin());
		this->root->installPlugin(this->metalRenderSystemPlugin.get());
#endif
#ifdef ORKIGE_HAVE_VULKAN
		// Vulkan render system (OGRE 14.5 + Orkige's VK_EXT_metal_surface port
		// patch): selectable via setPreferredRenderSystem("Vulkan"), NOT the
		// default. Unlike Metal it has a full RTSS path - the glslang plugin
		// below compiles the RTSS-generated GLSL to SPIR-V ("glslang" language,
		// "spirv" profile), which MoltenVK translates to MSL on Apple platforms.
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE
		// MoltenVK is treated as the platform's Vulkan DRIVER (system-tier,
		// installed via 'brew install molten-vk' - the documented exception to
		// the vcpkg-only dependency rule; the loader itself is vcpkg's).
		// Point the Khronos loader at the brew ICD manifest unless the caller
		// already configured driver discovery.
		if(std::getenv("VK_ICD_FILENAMES") == NULL && std::getenv("VK_DRIVER_FILES") == NULL)
		{
			static char const * const moltenVkIcdManifest = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
			if(std::filesystem::exists(moltenVkIcdManifest))
			{
				setenv("VK_DRIVER_FILES", moltenVkIcdManifest, 0);
			}
		}
#endif
		this->vulkanRenderSystemPlugin = onew(new Ogre::VulkanPlugin());
		this->root->installPlugin(this->vulkanRenderSystemPlugin.get());
		// registers the "glslang" program factory once the selected render
		// system reports SPIR-V support (deferred to Root initialisation)
		this->glslangProgramPlugin = onew(new Ogre::GLSLangPlugin());
		this->root->installPlugin(this->glslangProgramPlugin.get());
#endif
		Ogre::STBIImageCodec::startup(); // png/jpg image codecs
		// assimp mesh codecs (glTF/glb, obj, ...) - AssimpPlugin::install
		// registers one Ogre::Codec per assimp file extension
		this->assimpCodecPlugin = onew(new Ogre::AssimpPlugin());
		this->root->installPlugin(this->assimpCodecPlugin.get());
#else
		this->root = optr<Ogre::Root>(new Ogre::Root(pluginCfgFileName, renderCfgPlatformFileName, engineLogFileName));
#endif
		if(!zipFileName.empty())
		{
			this->bigZipArchiveFactory = onew(new BigZipArchiveFactory(zipFileName, zipInternalPathPrefix));
			Ogre::ArchiveManager::getSingleton().addArchiveFactory( this->bigZipArchiveFactory.get() );
			this->defaultLocationType = "BigZip";
		}
#if defined(WIN32) || defined(__ANDROID__)
#ifdef ORKIGE_DEBUG
		static LogListener logListener;
		Ogre::LogManager::getSingleton().getDefaultLog()->addListener(&logListener);
#endif
#endif
		
		if(!resourceCfgPlatformFileName.empty())
		{
			this->setupResources(resourceCfgPlatformFileName);
		}
	}
	//---------------------------------------------------------
	Engine::~Engine()
	{
#ifdef USE_RTSHADER_SYSTEM
		// Finalize the RT Shader System while the root (and its render system)
		// is still alive - OGRE 14 crashes on the reverse order.
		this->finalizeRTShaderSystem();
#endif // USE_RTSHADER_SYSTEM
		this->root.reset();
		this->bigZipArchiveFactory.reset();
		// the plugins have to outlive the root
		this->assimpCodecPlugin.reset();
		this->glslangProgramPlugin.reset();
		this->vulkanRenderSystemPlugin.reset();
		this->metalRenderSystemPlugin.reset();
		this->renderSystemPlugin.reset();
	}
	//---------------------------------------------------------
	String Engine::getPlatformSpecificConfig(String const & cfgFileName)
	{
#ifdef ORKIGE_IPHONE
		// iphone and ipad need different configs
		if(cfgFileName.find(';') != String::npos)
		{
			String cfgPlatformFileName = cfgFileName;

			bool iPad = false;
//#ifdef UI_USER_INTERFACE_IDIOM
			iPad = ((UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad) == YES);
//#endif
			std::vector<std::string> strs;
			{
				std::istringstream splitStream(cfgFileName);
				std::string part;
				while(std::getline(splitStream, part, ';'))
				{
					strs.push_back(part);
				}
			}
			oAssert(strs.size() == 3);
			if (iPad) 
			{
				// iPad specific code here
				cfgPlatformFileName = strs[2];
			} 
			else 
			{
				// iPhone/iPod specific code here

				if ([[UIScreen mainScreen] respondsToSelector:@selector(scale)] && [[UIScreen mainScreen] scale] == 2.0)
				{
					//>=iphone4
					cfgPlatformFileName = strs[1];
				}
				else 
				{
					//older iphones
					cfgPlatformFileName = strs[0];
				}
			}

			return cfgPlatformFileName;
		}
#endif
		return cfgFileName;
	}
	//---------------------------------------------------------
	void Engine::setCustomWindowParam(Orkige::String paramName, Orkige::String paramValue, unsigned int windowNumber)
	{
		this->windowParams[windowNumber][paramName] = paramValue;
	}
	//---------------------------------------------------------
	bool Engine::matchRenderSystemName(String const & renderSystemName, String const & nameHint)
	{
		// normalized, case-insensitive substring match: spaces are dropped and
		// '+' is spelled out, so "GL3Plus" (and "GL3+") finds
		// "OpenGL 3+ Rendering Subsystem", "Metal" finds
		// "Metal Rendering Subsystem" and "GL" any OpenGL flavour
		auto normalize = [](String const & value) -> String
		{
			String normalized;
			normalized.reserve(value.size());
			for (char character : value)
			{
				if(character == ' ')
					continue;
				if(character == '+')
				{
					normalized += "plus";
					continue;
				}
				normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			}
			return normalized;
		};
		String const normalizedHint = normalize(nameHint);
		if(normalizedHint.empty())
			return false;
		return normalize(renderSystemName).find(normalizedHint) != String::npos;
	}
	//---------------------------------------------------------
	bool Engine::setup(String const & windowTitle, ShowConfigBehavior showConfigBehavior, String const & externalHandle, String const & topLevelHandle)
	{
		this->externalWindowHandle = externalHandle;
		
		if(topLevelHandle.empty() && !externalHandle.empty())
		{ 
			this->topLevelWindowHandle = externalHandle;
		}
		else
		{
			this->topLevelWindowHandle = topLevelHandle;
		}
		if(!this->configure(windowTitle, showConfigBehavior))
			return false;

		// Create the SceneManager
		this->sceneManager = root->createSceneManager(this->sceneManagerTypeName, "OrkigeSceneManager");
		oAssert(this->sceneManager);
#ifdef USE_RTSHADER_SYSTEM
			// Initialize shader generator.
			// Must be before resource loading in order to allow parsing extended material attributes.
			// The old "clone the BaseWhite/BaseWhiteNoLighting technique programs" block is gone:
			// since OGRE 13 the RTSS generates techniques for the base materials on demand
			// through the material manager listener (see OgreBites::SGTechniqueResolverListener).
			bool success = initializeRTShaderSystem(this->sceneManager);
			if (!success)
			{
				OGRE_EXCEPT(Ogre::Exception::ERR_FILE_NOT_FOUND,
					"Shader Generator Initialization failed - Core shader libs path not found",
					"Sample::_setup");
			}
            Ogre::MaterialManager::getSingleton().setActiveScheme(Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
#endif // USE_RTSHADER_SYSTEM

		// Set default mipmap level (NB some APIs ignore this)
		Ogre::TextureManager::getSingleton().setDefaultNumMipmaps(5);

		this->eventManager = GlobalEventManager::getSingletonPtr();
		oAssert(this->eventManager);
		this->root->addFrameListener(this);

		this->lastFrameTime = Timer::getMilliseconds();
		return true;
	}
	//---------------------------------------------------------
	bool Engine::renderOneFrame()
	{
		OPROFILEFUNC();
		// OGRE 14: WindowEventUtilities moved to OgreBites - SDL owns the event loop now

		return this->root->renderOneFrame();
	}
	//---------------------------------------------------------
	bool Engine::renderOneFrameFast()
	{
		OPROFILEFUNC();
		unsigned long currentFrameTime = Timer::getMilliseconds();
		unsigned long timeDiff = currentFrameTime - this->lastFrameTime;
		this->lastFrameTime = currentFrameTime;
		Ogre::Real delta = Ogre::Real(timeDiff) / 1000.f;
		this->data->timeSinceLastFrame = delta;
		this->data->timeSinceLastEvent = delta;
		this->eventManager->trigger(this->frameStartedEvent);
		// OGRE 14: _updateViewport must be called between _beginUpdate and _endUpdate
		for (unsigned int each = 0; each < this->numberOfWindows; ++each)
		{
			this->renderWindow[each]->_beginUpdate();
			this->renderWindow[each]->_updateViewport(this->viewport[each], true);
			this->renderWindow[each]->_endUpdate();
		}

		this->eventManager->trigger(this->frameRenderingQueuedEvent);

		for (unsigned int each = 0; each < this->numberOfWindows; ++each)
		{
			this->renderWindow[each]->swapBuffers();
		}
		this->eventManager->trigger(this->frameEndedEvent);
		return true;
	}
	//---------------------------------------------------------
	void Engine::createDefaultCameraAndViewport()
	{
		Orkige::String name;
		for (unsigned int each = 0; each < this->numberOfWindows; ++each)
		{
			// Create the camera
			name = "OrkigeCamera";
			std::stringstream number;
			number << each;
			name.append(number.str());
			this->camera[each] = this->sceneManager->createCamera(name);
			this->camera[each]->setNearClipDistance(1.0f);
			this->camera[each]->setFarClipDistance(100000.0f);
			// OGRE 14: cameras have to be attached to a SceneNode to be placed in the scene
			Ogre::SceneNode* cameraNode = this->sceneManager->getRootSceneNode()->createChildSceneNode(name + "Node");
			cameraNode->attachObject(this->camera[each]);
			// Fixed world-up yaw axis: Node::lookAt/setDirection rotate by the shortest arc
			// from the CURRENT orientation - calling them every frame (follow cameras)
			// accumulates roll. With a fixed yaw axis Ogre keeps lookAt roll-free by
			// construction, so game cameras cannot tilt the horizon.
			cameraNode->setFixedYawAxis(true, Ogre::Vector3::UNIT_Y);
			// Create one viewport, entire window
			this->viewport[each] = this->renderWindow[each]->addViewport(this->camera[each]);
			this->viewport[each]->setBackgroundColour(Ogre::ColourValue::Blue);
			this->viewport[each]->setShadowsEnabled(true);
#ifdef USE_RTSHADER_SYSTEM
			 if(this->root->getRenderSystem()->getCapabilities()->hasCapability(Ogre::RSC_FIXED_FUNCTION) == false)
            {
                this->viewport[each]->setMaterialScheme(Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
            }
#endif
			// Alter the camera aspect ratio to match the viewport
			this->camera[each]->setAspectRatio(Ogre::Real(this->viewport[each]->getActualWidth())
											   / Ogre::Real(this->viewport[each]->getActualHeight()));
		}
	}
	//---------------------------------------------------------
	void Engine::resetupResources(String const & resourceCfgFileName)
	{
		Ogre::ResourceGroupManager::getSingleton().shutdownAll();
		this->setupResources(resourceCfgFileName);
	}
	//---------------------------------------------------------
	Engine::AspectRatio Engine::getCurrentAspectRatio(unsigned int num, double maxErrorDist)
	{
		static const double aspectRatioFactors[] = { 5.0/4.0, 4.0/3.0, 16.0/10.0, 16.0/9.0 };
		double aspectFactor = double(this->getViewport(0)->getActualWidth()) / double(this->getViewport(0)->getActualHeight());
		for(std::size_t i = Engine::AspectRatio_Unknown-1; true; i--)
		{
			if(aspectFactor - aspectRatioFactors[i] < maxErrorDist)
			{
				return Engine::AspectRatio(i);
			}
            if(i == 0)
            {
                break;
            }
		}
		return Engine::AspectRatio_Unknown;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	Engine::Engine(Ogre::Root* _root, Ogre::SceneManager* _sceneManager, Ogre::RenderWindow* _window, Ogre::Viewport* _viewport, Ogre::Camera* _camera)
		: frameStartedEvent(Engine::FrameStartedEvent),
		frameRenderingQueuedEvent(Engine::FrameRenderingQueuedEvent),
		frameEndedEvent(Engine::FrameEndedEvent),
		sceneManagerTypeName(Ogre::SMT_DEFAULT),
		eventManager(NULL),
		sceneManager(NULL),
		defaultLocationType("FileSystem"),
		lastFrameTime(0),
		numberOfWindows(1)
	{
		for (unsigned int each = 0; each < MAX_MUMBER_OF_WINDOWS; ++each)
		{
			this->renderWindow[each] = NULL;
			this->camera[each] = NULL;
			this->viewport[each] = NULL;
			this->windowParams[each] = Ogre::NameValuePairList();
		}

		this->data = onew(new FrameEventData());

		this->frameStartedEvent.setData(this->data);
		this->frameRenderingQueuedEvent.setData(this->data);
		this->frameEndedEvent.setData(this->data);

		this->root = oBadPointer(_root);
		this->sceneManager = _sceneManager;
		this->renderWindow[0] = _window;
		this->camera[0] = _camera;
		this->viewport[0] = _viewport;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------

#ifdef USE_RTSHADER_SYSTEM

		/*-----------------------------------------------------------------------------
		| Initialize the RT Shader system.	
		-----------------------------------------------------------------------------*/
		bool Engine::initializeRTShaderSystem(Ogre::SceneManager* sceneMgr)
		{			
			if (Ogre::RTShader::ShaderGenerator::initialize())
			{
				mShaderGenerator = Ogre::RTShader::ShaderGenerator::getSingletonPtr();

				mShaderGenerator->addSceneManager(sceneMgr);

#if OGRE_PLATFORM != OGRE_PLATFORM_ANDROID
				// Setup core libraries and shader cache path.
				// OGRE 14 resolves the RTSS core shader libs through the resource system at
				// runtime; this only sanity checks that a "RTShaderLib" location was added.
				Ogre::StringVector groupVector = Ogre::ResourceGroupManager::getSingleton().getResourceGroups();
				Ogre::String shaderCoreLibsPath;
				Ogre::String shaderCachePath;

				for (Ogre::String const & group : groupVector)
				{
					Ogre::ResourceGroupManager::LocationList const & resLocationsList = Ogre::ResourceGroupManager::getSingleton().getResourceLocationList(group);
					bool coreLibsFound = false;

					// Try to find the location of the core shader lib functions and use it
					// as shader cache path as well - this will reduce the number of generated files
					// when running from different directories.
					for (Ogre::ResourceGroupManager::ResourceLocation const & location : resLocationsList)
					{
						if (location.archive->getName().find("RTShaderLib") != Ogre::String::npos)
						{
							shaderCoreLibsPath = location.archive->getName() + "/cache/";
							shaderCachePath = shaderCoreLibsPath;
							coreLibsFound = true;
							break;
						}
					}
					// Core libs path found in the current group.
					if (coreLibsFound)
						break;
				}

				// Core shader libs not found -> shader generating will fail.
				if (shaderCoreLibsPath.empty())
					return false;

#ifdef _RTSS_WRITE_SHADERS_TO_DISK
				// Set shader cache path.
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
                shaderCachePath = Ogre::macCachePath();
#endif
				mShaderGenerator->setShaderCachePath(shaderCachePath);
#endif
#endif
				// Create and register the material manager listener if it doesn't exist yet.
				if (mMaterialMgrListener == NULL) {
					mMaterialMgrListener = new ShaderGeneratorTechniqueResolverListener(mShaderGenerator);
					Ogre::MaterialManager::getSingleton().addListener(mMaterialMgrListener);
				}
			}

			return true;
		}

		/*-----------------------------------------------------------------------------
		| Finalize the RT Shader system.	
		-----------------------------------------------------------------------------*/
		void Engine::finalizeRTShaderSystem()
		{
			// Restore default scheme.
			if(Ogre::MaterialManager::getSingletonPtr())
			{
				Ogre::MaterialManager::getSingleton().setActiveScheme(Ogre::MaterialManager::DEFAULT_SCHEME_NAME);
			}

			// Unregister the material manager listener.
			if (mMaterialMgrListener != NULL)
			{	
				if(Ogre::MaterialManager::getSingletonPtr())
				{
					Ogre::MaterialManager::getSingleton().removeListener(mMaterialMgrListener);
				}
				delete mMaterialMgrListener;
				mMaterialMgrListener = NULL;
			}

			// Finalize RTShader system (OGRE 14 renamed finalize() to destroy()).
			if (mShaderGenerator != NULL)
			{
				if(Ogre::MaterialManager::getSingletonPtr())
				{
					Ogre::RTShader::ShaderGenerator::destroy();
				}
				mShaderGenerator = NULL;
			}
		}
#endif // USE_RTSHADER_SYSTEM
	void Engine::setupResources(String const & resourceCfgFileName)
	{
		// Load resource paths from config file
		Ogre::ConfigFile cf;
		cf.load(resourceCfgFileName);

		// Go through all sections & settings in the file
		// (OGRE 14: ConfigFile::SectionIterator became getSettingsBySection())
		Ogre::String typeName, archName;
		for (auto const & section : cf.getSettingsBySection())
		{
			Ogre::String const & secName = section.first;
			for (auto const & setting : section.second)
			{
				typeName = setting.first;
				archName = setting.second;
				if(typeName == "FileSystem")
				{
					archName = PlatformUtil::getResourceDirectory() + archName;
				}
				Ogre::ResourceGroupManager::getSingleton().addResourceLocation(archName, typeName, secName);
			}
		}
	}
	//---------------------------------------------------------
	bool Engine::configure(String const & windowTitle, ShowConfigBehavior showConfigBehavior)
	{
		// OGRE 14 removed the built in config dialog (Root::showConfigDialog now wants an
		// externally supplied ConfigDialog) so configuration is restoreConfig() plus
		// programmatic defaults. ShowConfigBehavior survives for API compatibility:
		// SHOW_ALWAYS ignores a stored config, everything else just falls back to the
		// defaults below when no stored config exists.
		// note: restoreConfig() returns true (without selecting a RenderSystem)
		// when the render config filename is empty, so double-check a renderer
		// is actually active before trusting it
		// an explicit render system preference (setPreferredRenderSystem) wins
		// over a stored config - callers that set it expect exactly that system
		bool configRestored = this->preferredRenderSystem.empty()
			&& (showConfigBehavior != SHOW_ALWAYS) && this->root->restoreConfig()
			&& (this->root->getRenderSystem() != NULL);
		if(!configRestored)
		{
			const Ogre::RenderSystemList& renderers = this->root->getAvailableRenderers();
			if(renderers.empty())
			{
				oDebugMsg("core", 0, "No RenderSystem available - Engine configuration failed.");
				return false;
			}
			Ogre::RenderSystem* renderSystem = NULL;
			if(this->preferredRenderSystem.empty())
			{
				// historical default: first available render system
				renderSystem = renderers.front();
				oDebugMsg("core", 0, "No stored config - picking RenderSystem: " << renderSystem->getName());
			}
			else
			{
				for (Ogre::RenderSystem* each : renderers)
				{
					if(Engine::matchRenderSystemName(each->getName(), this->preferredRenderSystem))
					{
						renderSystem = each;
						break;
					}
				}
				if(renderSystem == NULL)
				{
					// fail instead of silently rendering through another API -
					// automated runs rely on getting exactly what they asked for
					oDebugMsg("core", 0, "Preferred RenderSystem '" << this->preferredRenderSystem << "' not available - Engine configuration failed. Available RenderSystems:");
					for (Ogre::RenderSystem* each : renderers)
					{
						oDebugMsg("core", 0, "    " << each->getName());
					}
					return false;
				}
				oDebugMsg("core", 0, "Picking preferred RenderSystem: " << renderSystem->getName());
			}

			// sane defaults: windowed, size from windowParams if provided
			int width = 800;
			int height = 600;
			if(this->windowParams[0].find("width") != this->windowParams[0].end())
			{
				width = Orkige::StringUtil::Converter::fromString<int>(this->windowParams[0]["width"]);
			}
			if(this->windowParams[0].find("height") != this->windowParams[0].end())
			{
				height = Orkige::StringUtil::Converter::fromString<int>(this->windowParams[0]["height"]);
			}
			const Ogre::ConfigOptionMap& options = renderSystem->getConfigOptions();
			if(options.find("Full Screen") != options.end())
			{
				renderSystem->setConfigOption("Full Screen", "No");
			}
			if(options.find("FSAA") != options.end())
			{
				renderSystem->setConfigOption("FSAA", "0");
			}
			if(options.find("Video Mode") != options.end())
			{
				std::stringstream videoMode;
				videoMode << width << " x " << height;
				renderSystem->setConfigOption("Video Mode", videoMode.str());
			}
			this->root->setRenderSystem(renderSystem);
		}
		{
			// Here we choose to let the system create a default rendering window or a embedded window in externalhandle
			if(this->externalWindowHandle.empty())
			{
				try
				{
					//if there are no params for window 0 we use the auto create window
					bool autoCreateWindow = this->windowParams[0].empty();

					this->renderWindow[0] = this->root->initialise(autoCreateWindow, windowTitle);
					
					for (unsigned int each = (autoCreateWindow ? 1 : 0); each < this->numberOfWindows; ++each)
					{
						Ogre::NameValuePairList params = this->windowParams[each];
						int width = 800;
						int height = 600;
						bool fullscreen = false;
						if(params.find("width") != params.end())
						{
							width = Orkige::StringUtil::Converter::fromString<int>(params["width"]);
						}
						if(params.find("height") != params.end())
						{
							height = Orkige::StringUtil::Converter::fromString<int>(params["height"]);
						}
						if(params.find("fullscreen") != params.end())
						{
							fullscreen = Orkige::StringUtil::Converter::fromString<bool>(params["fullscreen"]);
						}

						Ogre::String nextWindowTitle;
						nextWindowTitle.append(windowTitle);
						std::stringstream number;
						number << each;
						nextWindowTitle.append(number.str());
						this->renderWindow[each] = this->root->createRenderWindow(nextWindowTitle , width, height, fullscreen, &params);
						this->renderWindow[each]->setDeactivateOnFocusChange(false);
						this->renderWindow[each]->setActive(true);
						//this->renderWindow[each]->reposition(40 + each * 20, 40 + each * 20);
						this->renderWindow[each]->resize(width, height);
						
						this->renderWindow[each]->windowMovedOrResized();
					}
				}
				catch (Ogre::Exception const & e)
				{
					// no config dialog to fall back to anymore - fail configuration honestly
					oDebugMsg("core", 0, "Exception while creating RenderWindow: " << e.what());
					return false;
				}
				oAssert(this->renderWindow[0]);
			}
			else
			{
				this->renderWindow[0] = this->root->initialise(false/*, windowTitle*/);

				unsigned int width = 640;
				unsigned int height = 480;
				int left = 0;
				int top = 0;
				if(this->renderWindow[0])
				{
					// OGRE 14: getMetrics lost the colour depth out parameter
					this->renderWindow[0]->getMetrics(width, height, left, top);
				}

				Ogre::NameValuePairList params = this->windowParams[0];
				params["parentWindowHandle"] = this->topLevelWindowHandle;
				params["externalWindowHandle"] = this->externalWindowHandle;
				if(params.find("width") != params.end())
				{
					width = Orkige::StringUtil::Converter::fromString<unsigned int>(params["width"]);
				}
				if(params.find("height") != params.end())
				{
					height = Orkige::StringUtil::Converter::fromString<unsigned int>(params["height"]);
				}

				try
				{
					oDebugMsg("core", 0, "Trying to create external RenderWindow with handle: " << this->externalWindowHandle << " and size: " << width << "x" << height);
					this->renderWindow[0] = this->root->createRenderWindow(windowTitle, width, height, false, &params);

					this->renderWindow[0]->setDeactivateOnFocusChange(false);
					this->renderWindow[0]->setActive(true);
					this->renderWindow[0]->reposition(left,top);
					this->renderWindow[0]->resize(width, height);

					this->renderWindow[0]->windowMovedOrResized();
				}
				catch (Ogre::Exception const & e)
				{
					// no config dialog to fall back to anymore - fail configuration honestly
					oDebugMsg("core", 0, "Error while creating external RenderWindow: " << e.what());
					return false;
				}
				oAssert(this->renderWindow[0]);
				this->renderWindow[0]->getMetrics( width, height, left, top );
				oDebugMsg("core", 0, "external RenderWindow initialized! width, height, left, top: " << width <<", "<< height<<", "<< left<<", "<< top);
			}
			return true;
		}
	}
	//---------------------------------------------------------
	bool Engine::frameStarted(const Ogre::FrameEvent& evt)
	{
		OPROFILEFUNC();
		if(this->externalWindowHandle.empty() && this->renderWindow[0]->isClosed())
			return false;
		this->data->timeSinceLastEvent = evt.timeSinceLastEvent;
		this->data->timeSinceLastFrame = evt.timeSinceLastFrame;
		this->eventManager->trigger(this->frameStartedEvent);
		return true;
	}
	//---------------------------------------------------------
	bool Engine::frameRenderingQueued(const Ogre::FrameEvent& evt)
	{
		OPROFILEFUNC();
		if(this->externalWindowHandle.empty() && this->renderWindow[0]->isClosed())
			return false;
		this->data->timeSinceLastEvent = evt.timeSinceLastEvent;
		this->data->timeSinceLastFrame = evt.timeSinceLastFrame;
		this->eventManager->trigger(this->frameRenderingQueuedEvent);
		return true;
	}
	//---------------------------------------------------------
	bool Engine::frameEnded(const Ogre::FrameEvent& evt)
	{
		OPROFILEFUNC();
		if(this->externalWindowHandle.empty() && this->renderWindow[0]->isClosed())
			return false;
		this->data->timeSinceLastEvent = evt.timeSinceLastEvent;
		this->data->timeSinceLastFrame = evt.timeSinceLastFrame;
		this->eventManager->trigger(this->frameEndedEvent);
		return true;
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(Engine)
		OCONSTRUCTOR0()
		OSINGLETON()
		OFUNC(getSceneManager)
		OFUNC(setup)
		OFUNC(getTopLevelWindowHandle)
		OFUNC(createDefaultCameraAndViewport)
		OFUNC(renderOneFrame)
		OFUNC(enableWireframeMode)
		OFUNC(disableWireframeMode)
		// camera access for scripts (Lua passes the index - no default args
		// across the binding): Engine.getSingleton():getCamera(0)
		OFUNC(getCamera)
	OOBJECT_END
}
