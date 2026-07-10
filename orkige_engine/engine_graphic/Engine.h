/********************************************************************
	created:	Tuesday 2010/09/07 at 16:34
	filename: 	Engine.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game Engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __Engine_h__7_9_2010__16_34_16__
#define __Engine_h__7_9_2010__16_34_16__

// Engine is the app/Lua singleton on BOTH render flavors
// (Docs/render-abstraction.md): apps and scripts spell
// "engine_graphic/Engine.h" / Engine.getSingleton() regardless of the
// backend. On the classic flavor Engine below IS the classic bootstrapper
// (Ogre::Root/RTSS/window plumbing); the Ogre-Next flavor compiles the
// facade-only sibling from EngineNext.h instead (same name, same script/app
// surface, boot via RenderBackend::createRenderSystem).
#ifdef ORKIGE_RENDER_NEXT
#	include "engine_graphic/EngineNext.h"
#else

#include <core_event/Event.h>
#include <core_event/EventManager.h>
#include "engine_graphic/FrameEventData.h"
#include "engine_module/EnginePrerequisitesClassic.h"
// facade forward declarations only - Engine's Lua/app-facing camera and
// window surface answers facade types (Docs/render-abstraction.md)
#include "engine_render/RenderPrerequisites.h"
#include <core_util/StringUtil.h>
#include <core_util/PlatformUtil.h>
#include <core_util/SafeArea.h>
#include "engine_filesystem/BigZipArchiveFactory.h"

#define MAX_MUMBER_OF_WINDOWS 8
#ifdef USE_RTSHADER_SYSTEM
#include "OgreRTShaderSystem.h"

// Remove the comment below in order to make the RTSS use valid path for writing down the generated shaders.
// If cache path is not set - all shaders are generated to system memory.
//#define _RTSS_WRITE_SHADERS_TO_DISK
#endif // USE_RTSHADER_SYSTEM
namespace Orkige
{
#ifdef USE_RTSHADER_SYSTEM

/** This class demonstrates basic usage of the RTShader system.
It sub class the material manager listener class and when a target scheme callback
is invoked with the shader generator scheme it tries to create an equivalent shader
based technique based on the default technique of the given material.
*/
class ShaderGeneratorTechniqueResolverListener : public Ogre::MaterialManager::Listener
{
public:

	ShaderGeneratorTechniqueResolverListener(Ogre::RTShader::ShaderGenerator* pShaderGenerator)
	{
		mShaderGenerator = pShaderGenerator;			
	}

	/** This is the hook point where shader based technique will be created.
	It will be called whenever the material manager won't find appropriate technique
	that satisfy the target scheme name. If the scheme name is out target RT Shader System
	scheme name we will try to create shader generated technique for it.
	*/
	virtual Ogre::Technique* handleSchemeNotFound(unsigned short schemeIndex,
		const Ogre::String& schemeName, Ogre::Material* originalMaterial, unsigned short lodIndex,
		const Ogre::Renderable* rend) override
	{
		Ogre::Technique* generatedTech = NULL;

		// Case this is the default shader generator scheme.
		if (schemeName == Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME)
		{
			bool techniqueCreated;

			// Create shader generated technique for this material.
			techniqueCreated = mShaderGenerator->createShaderBasedTechnique(
				*originalMaterial,
				Ogre::MaterialManager::DEFAULT_SCHEME_NAME,
				schemeName);

			// Case technique registration succeeded.
			if (techniqueCreated)
			{
				// Force creating the shaders for the generated technique.
				mShaderGenerator->validateMaterial(schemeName, *originalMaterial);

				// Grab the generated technique.
				for (Ogre::Technique* curTech : originalMaterial->getTechniques())
				{
					if (curTech->getSchemeName() == schemeName)
					{
						generatedTech = curTech;
						break;
					}
				}
			}
		}

		return generatedTech;
	}

protected:	
	Ogre::RTShader::ShaderGenerator*	mShaderGenerator;			// The shader generator instance.		
};
#endif // USE_RTSHADER_SYSTEM

	//! Engine core responsible for config dialog, plugin loading, RenderWindow's, SceneManager, Camera's etc
	class ORKIGE_ENGINE_DLL Engine : public Singleton<Engine>, public Interface, public Ogre::FrameListener
	{
		friend class OrkigePluginApplication;
		OOBJECT(Engine,Interface)
		DECL_OSINGLETON(Engine)
		//--- Types -------------------------------------------------
	public:
		/** \addtogroup EngineEvents
		*  @{ */
		//! Called when a frame is about to begin rendering.
		DECL_EVENTTYPE(FrameStartedEvent);
		//! @brief Called after all render targets have had their rendering commands issued, 
		//! but before render windows have been asked to flip their	buffers over.
		DECL_EVENTTYPE(FrameRenderingQueuedEvent);
		//! Called just after a frame has been rendered.
		DECL_EVENTTYPE(FrameEndedEvent);
		/** @} End of "addtogroup EngineEvents"*/
		enum ShowConfigBehavior
		{
			SHOW_IFERROR = 0,
			SHOW_ALWAYS,
			SHOW_NEVER,
		};
		enum AspectRatio
		{
			AspectRatio_5_4 = 0, 
			AspectRatio_4_3, 
			AspectRatio_16_10, 
			AspectRatio_16_9,
			AspectRatio_Unknown
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		optr<Ogre::Root>			root;
		//! statically linked render system plugin - must outlive the root (OGRE 14 static build)
		optr<Ogre::Plugin>			renderSystemPlugin;
		//! statically linked Metal render system plugin (Apple platforms) - same lifetime rule
		optr<Ogre::Plugin>			metalRenderSystemPlugin;
		//! statically linked Vulkan render system plugin (driver: MoltenVK on Apple) - same lifetime rule
		optr<Ogre::Plugin>			vulkanRenderSystemPlugin;
		//! statically linked glslang program manager plugin - compiles the RTSS GLSL output to SPIR-V for Vulkan
		optr<Ogre::Plugin>			glslangProgramPlugin;
		//! statically linked assimp mesh codec plugin (glTF/glb etc.) - same lifetime rule
		optr<Ogre::Plugin>			assimpCodecPlugin;
		optr<BigZipArchiveFactory>  bigZipArchiveFactory;
		Ogre::RenderWindow*			renderWindow[MAX_MUMBER_OF_WINDOWS];
		Ogre::SceneManager*			sceneManager;
		//! OGRE 14 dropped Ogre::SceneType - scene managers are selected by type name now
		Ogre::String				sceneManagerTypeName;
		Ogre::Camera*				camera[MAX_MUMBER_OF_WINDOWS];
		Ogre::Viewport*				viewport[MAX_MUMBER_OF_WINDOWS];
		Ogre::NameValuePairList		windowParams[MAX_MUMBER_OF_WINDOWS];
		EventManager*				eventManager;
		Event						frameStartedEvent;
		Event						frameRenderingQueuedEvent;
		Event						frameEndedEvent;
		optr<FrameEventData>		data;
		String						externalWindowHandle;
		String						topLevelWindowHandle;
		unsigned long				lastFrameTime;
		unsigned int				numberOfWindows;
		String						defaultLocationType;
		//! name hint for the render system configure() should pick (empty = first available)
		String						preferredRenderSystem;
#ifdef USE_RTSHADER_SYSTEM
		Ogre::RTShader::ShaderGenerator*			mShaderGenerator;			// The Shader generator instance.
		ShaderGeneratorTechniqueResolverListener*	mMaterialMgrListener;		// Shader generator material manager listener.	
#endif // USE_RTSHADER_SYSTEM
		//--- Methods -----------------------------------------------
	public:
		//! construct Engine and set basic parameters
		Engine(Ogre::String const & smTypeName = Ogre::SMT_DEFAULT,
#ifdef ORKIGE_IPHONE
			String const & resourceCfgFileName =  Orkige::PlatformUtil::getResourceDirectory() + "data/Config/resources_iphone.cfg;" 
												+ Orkige::PlatformUtil::getResourceDirectory() + "data/Config/resources_iphone4.cfg;" 
												+ Orkige::PlatformUtil::getResourceDirectory() + "data/Config/resources_ipad.cfg", 
#elif __ANDROID__
			String const & resourceCfgFileName = Orkige::PlatformUtil::getResourceDirectory() + "data/Config/resources_android.cfg",
#else
			String const & resourceCfgFileName = Orkige::PlatformUtil::getResourceDirectory() + "data/Config/resources.cfg",
#endif
#ifdef _DEBUG
			String const & pluginCfgFileName = Orkige::PlatformUtil::getResourceDirectory() + "data/Config/plugins_d.cfg",
#else
			String const & pluginCfgFileName = Orkige::PlatformUtil::getResourceDirectory() + "data/Config/plugins.cfg",
#endif
#ifdef ORKIGE_IPHONE
			String const & renderCfgFileName =    Orkige::PlatformUtil::getResourceDirectory() + "data/Config/orkitec_iphone.cfg;" 
												+ Orkige::PlatformUtil::getResourceDirectory() + "data/Config/orkitec_iphone4.cfg;" 
												+ Orkige::PlatformUtil::getResourceDirectory() + "data/Config/orkitec_ipad.cfg", 
#elif __ANDROID__
			String const & renderCfgFileName = Orkige::PlatformUtil::getResourceDirectory() + "data/Config/orkitec_android.cfg", 
#else
			String const & renderCfgFileName = Orkige::PlatformUtil::getResourceDirectory() + "data/Config/orkitec.cfg", 
#endif
			String const & engineLogFileName = Orkige::PlatformUtil::getResourceDirectory() + "orkitec.log",
			unsigned int _numberOfWindows = 1,
			String const & zipFileName = Orkige::StringUtil::BLANK,
			String const & zipInternalPathPrefix = Orkige::StringUtil::BLANK);
		//! destructor
		virtual ~Engine();

		//! set custom window properties (has to be called before calling setup(...) to take effect
		//! @see Ogre::RenderSystem::_createRenderWindow for parameters
		void setCustomWindowParam(Orkige::String paramName, Orkige::String paramValue, unsigned int windowNumber = 0);

		//! @brief prefer a specific render system when configure() picks one (call before setup(...))
		//! @param nameHint matched against Ogre::RenderSystem::getName() by
		//! Engine::matchRenderSystemName, so "Metal", "GL3Plus", "GL3+" and "GL"
		//! all work. An empty hint (the default) keeps the historical
		//! first-available behavior. A non-empty hint overrides a stored render
		//! config; if it matches no available render system, configure() fails
		//! instead of silently rendering through another API.
		inline void setPreferredRenderSystem(String const & nameHint);
		//! @brief the pure matching rule behind setPreferredRenderSystem (unit tested)
		//! Case-insensitive substring match after dropping spaces and spelling
		//! '+' as "plus": "GL3Plus" finds "OpenGL 3+ Rendering Subsystem",
		//! "Metal" finds "Metal Rendering Subsystem". Empty hints match nothing.
		static bool matchRenderSystemName(String const & renderSystemName, String const & nameHint);

		//! @brief setup Engine
		//! @copydoc Engine::configure
		//! @param showConfigBehavior see Engine::configure - OGRE 14 has no built in config dialog anymore
		//! @param windowTitle for tray and windowed mode
		//! @param externalHandle can be used for embedding Engine into custom created window
		//! @param topLevelHandle can be used for if externalHandle is not the topmost window
		bool setup(String const & windowTitle = StringUtil::BLANK, ShowConfigBehavior showConfigBehavior = Engine::SHOW_IFERROR, String const & externalHandle = StringUtil::BLANK, String const & topLevelHandle = StringUtil::BLANK);

		//! @brief DEPRECATED classic camera path: apps create a
		//! facade camera rig instead (RenderWorld::createCamera + createNode +
		//! RenderSystem::showCameraOnWindow) - no live caller remains; kept
		//! only until embedding scenarios are re-audited
		void createDefaultCameraAndViewport();

		//! as the name says Render one Frame
		bool renderOneFrame();
		//! faster frame rendering skips Framelisteners and some other ogre related stuff
		bool renderOneFrameFast();

		//! @brief get Engine SceneManager - DEPRECATED scene accessor:
		//! classic-only internals (fastgui, editor bootstrap) may
		//! keep using it; everything else goes through RenderSystem::getWorld
		inline Ogre::SceneManager* getSceneManager();
		//! get Engine RenderWindow
		inline Ogre::RenderWindow* getRenderWindow( unsigned int num = 0 );
		//! @brief DEPRECATED scene accessor: only non-NULL on the
		//! legacy createDefaultCameraAndViewport path; use getWindowCamera()
		inline Ogre::Camera* getCamera( unsigned int num = 0 );
		//! @brief DEPRECATED scene accessor: fastgui (classic-only)
		//! is the last consumer; new code uses RenderSystem window services
		inline Ogre::Viewport* getViewport( unsigned int num = 0 );

		//--- engine_render facade surface -----------------------
		// Engine stays the app/Lua singleton; its scene-facing surface
		// answers facade types. All of these assert that setup() ran.
		//! the facade camera currently shown on the main window
		//! (Lua: engine:getCamera() - scripts place it via getNode())
		optr<RenderCamera> getWindowCamera();
		//! the render facade entry point (Lua: engine:getRenderSystem())
		RenderSystem* getRenderSystem();
		//! main-window drawable width in pixels (UI layout)
		unsigned int getWindowWidth();
		//! main-window drawable height in pixels (UI layout)
		unsigned int getWindowHeight();
		//! @brief window-safe insets in PIXELS (notch / rounded corners / home
		//! indicator); all zero on an unnotched desktop window. Queried from
		//! the platform window (SDL_GetWindowSafeArea) each call. Games anchor
		//! HUD/menu content inside the box these describe (@see UiAnchor).
		SafeAreaInsets getSafeAreaInsets();
		//! @brief the display's content scale: 1.0 on standard-DPI desktops,
		//! ~2-3 on retina / phone screens. The fastgui UI system snaps this to
		//! an integer and drives UiGlyph::scale from it at boot so pixel text
		//! and touch targets keep a stable physical size.
		float getContentScale();
		//! @brief switch the window camera to ORTHOGRAPHIC projection (2D games)
		//! @param verticalHalfExtent world units from the view center to the
		//! top edge (the camera sees 2x this height; width follows the
		//! aspect); clip distances are preserved
		void setCameraOrthographic(float verticalHalfExtent);
		//! switch the window camera back to PERSPECTIVE projection (FOV and
		//! clip distances are preserved)
		void setCameraPerspective();
		//! window clear colour (games pick their sky/void)
		void setWindowBackgroundColour(float red, float green, float blue);
		//! @brief does this build carry the fastgui UI system?
		//! @remarks true on BOTH flavors
		//! (fastgui renders through the engine_render facade); the probe
		//! stays registered so scripts written against older builds keep
		//! working - and so a future UI-less flavor can answer honestly
		bool hasUISystem() const { return true; }
		//! get external window handle if Engine is embedded
		inline String const & getExternalWindowHandle(); 
		//! get top level window handle if Engine is embedded into multi window app
		inline String const & getTopLevelWindowHandle(); 
		//! get default location type of current setting
		inline String const & getDefaultLocationType();
		//! define the source of resources (other than current folder) but doesn't load them
		void resetupResources(String const & resourceCfgFileName);
		//! get aspect ratio based on resolution
		AspectRatio getCurrentAspectRatio(unsigned int num = 0, double maxErrorDist = 0.1);
		/** \addtogroup Debug
		*  @{ */
		//! Wireframe rendering mode
		inline void enableWireframeMode();
		//! Solid rendering mode
		inline void disableWireframeMode();
		/** @} End of "addtogroup Debug"*/
	protected:
		//! special constructor for creation from Editor Plugin
		Engine(Ogre::Root* _root, Ogre::SceneManager* _sceneManager, Ogre::RenderWindow* _window, Ogre::Viewport* _viewport, Ogre::Camera* _camera);
		//! define the source of resources (other than current folder) but doesn't load them
		void setupResources(String const & resourceCfgFileName);
		//! @brief Configures the Engine - returns false if no usable render system configuration was found.
		//! OGRE 14 removed the built in config dialog, so this restores a stored config
		//! (skipped for SHOW_ALWAYS) and otherwise picks the first available render system
		//! with sane windowed defaults programmatically.
		bool configure(String const & windowTitle, ShowConfigBehavior showConfigBehavior = Engine::SHOW_IFERROR);
		//! @see Ogre::FrameListener::frameStarted
		virtual bool frameStarted(const Ogre::FrameEvent& evt);
		//! @see Ogre::FrameListener::frameRenderingQueued
		virtual bool frameRenderingQueued(const Ogre::FrameEvent& evt);
		//! @see Ogre::FrameListener::frameEnded
		virtual bool frameEnded(const Ogre::FrameEvent& evt);
	private:
#ifdef USE_RTSHADER_SYSTEM
		//! Initialize the RT Shader system.
		bool initializeRTShaderSystem(Ogre::SceneManager* sceneMgr);
		//! Finalize the RT Shader system.
		void finalizeRTShaderSystem();
#endif // USE_RTSHADER_SYSTEM
		//! split comma separated string and returns according to current platform
		String getPlatformSpecificConfig(String const & cfgFileName);
	};
	//---------------------------------------------------------------
	inline Ogre::SceneManager* Engine::getSceneManager() 
	{
		return this->sceneManager;
	}
	//---------------------------------------------------------------
	inline Ogre::RenderWindow* Engine::getRenderWindow( unsigned int num ) 
	{
		return this->renderWindow[num];
	}
	//---------------------------------------------------------------
	inline Ogre::Camera* Engine::getCamera( unsigned int num )
	{
		return this->camera[num];
	}
	//---------------------------------------------------------------
	inline Ogre::Viewport* Engine::getViewport( unsigned int num )
	{
		// classic bridge (A1, Docs/render-abstraction.md): when the window
		// camera was set up through the facade
		// (RenderSystem::showCameraOnWindow), this->viewport[num] stays NULL
		// but the window carries the viewport - hand that one out. The last
		// consumer is fastgui (classic-only); the Lua
		// getViewport binding was retired. Goes away with the
		// fastgui draw-surface seam.
		if(!this->viewport[num] && this->renderWindow[num] &&
			this->renderWindow[num]->getNumViewports() > 0)
		{
			return this->renderWindow[num]->getViewport(0);
		}
		return this->viewport[num];
	}
	//---------------------------------------------------------------
	inline String const & Engine::getExternalWindowHandle()
	{
		return this->externalWindowHandle;
	}
	//---------------------------------------------------------------
	inline String const & Engine::getTopLevelWindowHandle()
	{
		return this->topLevelWindowHandle;
	}
	//---------------------------------------------------------------
	inline void Engine::enableWireframeMode()
	{
		this->renderWindow[0]->getViewport(0)->getCamera()->setPolygonMode(Ogre::PM_WIREFRAME);     /* wireframe */
	}
	//---------------------------------------------------------------
	inline void Engine::disableWireframeMode()
	{
		this->renderWindow[0]->getViewport(0)->getCamera()->setPolygonMode(Ogre::PM_SOLID);         /* solid */
	}
	//---------------------------------------------------------------
	inline String const & Engine::getDefaultLocationType()
	{
		return this->defaultLocationType;
	}
	//---------------------------------------------------------------
	inline void Engine::setPreferredRenderSystem(String const & nameHint)
	{
		this->preferredRenderSystem = nameHint;
	}
}

#endif //ORKIGE_RENDER_NEXT (classic Engine)
#endif //__Engine_h__7_9_2010__16_34_16__
