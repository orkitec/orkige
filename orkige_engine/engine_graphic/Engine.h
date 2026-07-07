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

#include <core_event/Event.h>
#include <core_event/EventManager.h>
#include "engine_graphic/FrameEventData.h"
#include "engine_module/EnginePrerequisites.h"
#include <core_util/StringUtil.h>
#include <core_util/PlatformUtil.h>
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

		//! @brief setup Engine
		//! @copydoc Engine::configure
		//! @param showConfigBehavior see Engine::configure - OGRE 14 has no built in config dialog anymore
		//! @param windowTitle for tray and windowed mode
		//! @param externalHandle can be used for embedding Engine into custom created window
		//! @param topLevelHandle can be used for if externalHandle is not the topmost window
		bool setup(String const & windowTitle = StringUtil::BLANK, ShowConfigBehavior showConfigBehavior = Engine::SHOW_IFERROR, String const & externalHandle = StringUtil::BLANK, String const & topLevelHandle = StringUtil::BLANK);

		//! optional: you can have as many Cameras and Viewports as you wish but in most cases 1 is enough for a game
		void createDefaultCameraAndViewport();

		//! as the name says Render one Frame
		bool renderOneFrame();
		//! faster frame rendering skips Framelisteners and some other ogre related stuff
		bool renderOneFrameFast();

		//! get Engine SceneManager
		inline Ogre::SceneManager* getSceneManager();
		//! get Engine RenderWindow
		inline Ogre::RenderWindow* getRenderWindow( unsigned int num = 0 );
		//! get Engine default Camera if it was created through Engine::createDefaultCameraAndViewport
		inline Ogre::Camera* getCamera( unsigned int num = 0 );
		//! get Engine default Viewport if it was created through Engine::createDefaultCameraAndViewport
		inline Ogre::Viewport* getViewport( unsigned int num = 0 );
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
}

#endif //__Engine_h__7_9_2010__16_34_16__
