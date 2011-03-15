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

namespace Orkige
{
	//! Engine core responsible for config dialog, plugin loading, RenderWindow's, SceneManager, Camera's etc
	class ORKIGE_DLL Engine : public Singleton<Engine>, public Interface, public Ogre::FrameListener
	{
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
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		optr<Ogre::Root>			root;
		Ogre::RenderWindow*			renderWindow;
		Ogre::SceneManager*			sceneManager;
		Ogre::SceneType				sceneType;
		Ogre::Camera*				camera;
		Ogre::Viewport*				viewport;
		EventManager*				eventManager;
		Event						frameStartedEvent;
		Event						frameRenderingQueuedEvent;
		Event						frameEndedEvent;
		optr<FrameEventData>		data;
		String						externalWindowHandle;
		String						topLevelWindowHandle;
		unsigned long				lastFrameTime;
		//--- Methods -----------------------------------------------
	public:
		//! construct Engine and set basic parameters
		Engine(Ogre::SceneType st = Ogre::ST_GENERIC, 
#ifdef ORKIGE_IPHONE
			String const & resourceCfgFileName =  Orkige::PlatformUtil::getResourceDirectory() + "data/Config/resources_iphone.cfg;" 
												+ Orkige::PlatformUtil::getResourceDirectory() + "data/Config/resources_iphone4.cfg;" 
												+ Orkige::PlatformUtil::getResourceDirectory() + "data/Config/resources_ipad.cfg", 
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
#else
			String const & renderCfgFileName = Orkige::PlatformUtil::getResourceDirectory() + "data/Config/orkitec.cfg", 
#endif
			String const & engineLogFileName = Orkige::PlatformUtil::getResourceDirectory() + "orkitec.log");
		//! destructor
		virtual ~Engine();

		//! @brief setup Engine
		//! @copydoc Engine::configure
		//! @param alwaysShowConfigDialog show config dialog on every startup or just on the first start?
		//! @param windowTitle for tray and windowed mode
		//! @param externalHandle can be used for embedding Engine into custom created window
		//! @param topLevelHandle can be used for if externalHandle is not the topmost window
		bool setup(bool alwaysShowConfigDialog = false, String const & windowTitle = StringUtil::BLANK, String const & externalHandle = StringUtil::BLANK, String const & topLevelHandle = StringUtil::BLANK);

		//! optional: you can have as many Cameras and Viewports as you wish but in most cases 1 is enough for a game
		void createDefaultCameraAndViewport();

		//! as the name says Render one Frame
		bool renderOneFrame();
		//! faster frame rendering skips Framelisteners and some other ogre related stuff
		bool renderOneFrameFast();

		//! get Engine SceneManager
		inline Ogre::SceneManager* getSceneManager();
		//! get Engine RenderWindow
		inline Ogre::RenderWindow* getRenderWindow();
		//! get Engine default Camera if it was created through Engine::createDefaultCameraAndViewport
		inline Ogre::Camera* getCamera();
		//! get Engine default Viewport if it was created through Engine::createDefaultCameraAndViewport
		inline Ogre::Viewport* getViewort();
		//! get external window handle if Engine is embedded
		inline String const & getExternalWindowHandle(); 
		//! get top level window handle if Engine is embedded into multi window app
		inline String const & getTopLevelWindowHandle(); 
		//! define the source of resources (other than current folder) but doesn't load them
		void resetupResources(String const & resourceCfgFileName);
		
		/** \addtogroup Debug
		*  @{ */
		//! Wireframe rendering mode
		inline void enableWireframeMode();
		//! Solid rendering mode
		inline void disableWireframeMode();
		/** @} End of "addtogroup Debug"*/
	protected:
		//! define the source of resources (other than current folder) but doesn't load them
		void setupResources(String const & resourceCfgFileName);
		//! Configures the Engine shows dialog on first configuration - returns false if the user chooses to abandon configuration.
		bool configure(bool alwaysShowConfigDialog, String const & windowTitle);
		//! @see Ogre::FrameListener::frameStarted
		virtual bool frameStarted(const Ogre::FrameEvent& evt);
		//! @see Ogre::FrameListener::frameRenderingQueued
		virtual bool frameRenderingQueued(const Ogre::FrameEvent& evt);
		//! @see Ogre::FrameListener::frameEnded
		virtual bool frameEnded(const Ogre::FrameEvent& evt);
	private:
	};
	//---------------------------------------------------------------
	inline Ogre::SceneManager* Engine::getSceneManager() 
	{
		return this->sceneManager;
	}
	//---------------------------------------------------------------
	inline Ogre::RenderWindow* Engine::getRenderWindow() 
	{
		return this->renderWindow;
	}
	//---------------------------------------------------------------
	inline Ogre::Camera* Engine::getCamera()
	{
		return this->camera;
	}
	//---------------------------------------------------------------
	inline Ogre::Viewport* Engine::getViewort()
	{
		return this->viewport;
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
		this->renderWindow->getViewport(0)->getCamera()->setPolygonMode(Ogre::PM_WIREFRAME);     /* wireframe */
	}
	//---------------------------------------------------------------
	inline void Engine::disableWireframeMode()
	{
		this->renderWindow->getViewport(0)->getCamera()->setPolygonMode(Ogre::PM_SOLID);         /* solid */
	}
	//---------------------------------------------------------------
}

#endif //__Engine_h__7_9_2010__16_34_16__
