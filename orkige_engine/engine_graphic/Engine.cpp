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
#include <core_event/GlobalEventManager.h>
#include <core_debug/Profile.h>
#include <boost/algorithm/string.hpp>
#ifdef ORKIGE_IPHONE
#   ifdef __OBJC__
#       import <UIKit/UIKit.h>
#   endif
#endif
namespace Orkige
{
#ifdef WIN32
#ifdef ORKIGE_DEBUG
	struct LogListener : public Ogre::LogListener
	{
		virtual void messageLogged( const Ogre::String& message, Ogre::LogMessageLevel lml, bool maskDebug, const String &logName )
		{
			OutputDebugStringA(message.c_str());
			OutputDebugStringA("\n");
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
	Engine::Engine(Ogre::SceneType st, String const & resourceCfgFileName, String const & pluginCfgFileName, String const & renderCfgFileName, String const & engineLogFileName) 
		: frameStartedEvent(Engine::FrameStartedEvent), 
		frameRenderingQueuedEvent(Engine::FrameRenderingQueuedEvent),
		frameEndedEvent(Engine::FrameEndedEvent),
		sceneType(st),
		eventManager(NULL),
		sceneManager(NULL),
		renderWindow(NULL),
		camera(NULL),
		viewport(NULL),
		lastFrameTime(0)
	{
		String renderCfgPlatformFileName = this->getPlatformSpecificConfig(renderCfgFileName);
		String resourceCfgPlatformFileName = this->getPlatformSpecificConfig(resourceCfgFileName);

		oDebugMsg("core", 0, "Setting 2 RenderConfigFile to: " << renderCfgPlatformFileName);
		oDebugMsg("core", 0, "Setting 2 ResourceConfigFile to: " << resourceCfgPlatformFileName);

		this->data = onew(new FrameEventData());

		this->frameStartedEvent.setData(this->data);
		this->frameRenderingQueuedEvent.setData(this->data);
		this->frameEndedEvent.setData(this->data);
		//create the ogre root Master of Disaster
		
		this->root = optr<Ogre::Root>(new Ogre::Root(pluginCfgFileName, renderCfgPlatformFileName, engineLogFileName));
#ifdef WIN32
#ifdef ORKIGE_DEBUG
		static LogListener logListener;
		Ogre::LogManager::getSingleton().getDefaultLog()->addListener(&logListener);
#endif
#endif
		
		this->setupResources(resourceCfgPlatformFileName);
	}
	//---------------------------------------------------------
	Engine::~Engine()
	{
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
			boost::split(strs, cfgFileName, boost::is_any_of(";"));
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
		this->sceneManager = root->createSceneManager(this->sceneType, "OrkigeSceneManager");
		oAssert(this->sceneManager);

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
		Ogre::WindowEventUtilities::messagePump();
		
		return this->root->renderOneFrame();
	}
	//---------------------------------------------------------
	bool Engine::renderOneFrameFast()
	{
		OPROFILEFUNC();
		Ogre::WindowEventUtilities::messagePump();
		unsigned long currentFrameTime = Timer::getMilliseconds();
		unsigned long timeDiff = currentFrameTime - this->lastFrameTime;
		if(timeDiff < 0)
			timeDiff = 0;
		this->lastFrameTime = currentFrameTime;
		Ogre::Real delta = Ogre::Real(timeDiff) / 1000.f;
		this->data->timeSinceLastFrame = delta;
		this->data->timeSinceLastEvent = delta;
		this->eventManager->trigger(this->frameStartedEvent);
		//this->viewport->update();
		this->renderWindow->_updateViewport(this->viewport, true);
		this->eventManager->trigger(this->frameRenderingQueuedEvent);
		this->renderWindow->_beginUpdate();
		this->renderWindow->swapBuffers();
		this->renderWindow->_endUpdate();
		this->eventManager->trigger(this->frameEndedEvent);
		return true;
	}
	//---------------------------------------------------------
	void Engine::createDefaultCameraAndViewport()
	{
		// Create the camera
		this->camera = this->sceneManager->createCamera("OrkigeCamera");
		this->camera->setNearClipDistance(1.0f);
		this->camera->setFarClipDistance(100000.0f);
		// Create one viewport, entire window
		this->viewport = this->renderWindow->addViewport(this->camera);
		this->viewport->setBackgroundColour(Ogre::ColourValue(0,0,0));
		this->viewport->setShadowsEnabled(true);

		// Alter the camera aspect ratio to match the viewport
		this->camera->setAspectRatio(Ogre::Real(this->viewport->getActualWidth()) / Ogre::Real(this->viewport->getActualHeight()));
	}
	//---------------------------------------------------------
	void Engine::resetupResources(String const & resourceCfgFileName)
	{
		Ogre::ResourceGroupManager::getSingleton().shutdownAll();
		this->setupResources(resourceCfgFileName);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void Engine::setupResources(String const & resourceCfgFileName)
	{
		// Load resource paths from config file
		Ogre::ConfigFile cf;
		cf.load(resourceCfgFileName);

		// Go through all sections & settings in the file
		Ogre::ConfigFile::SectionIterator seci = cf.getSectionIterator();

		Ogre::String secName, typeName, archName;
		while (seci.hasMoreElements())
		{
			secName = seci.peekNextKey();	
			Ogre::ConfigFile::SettingsMultiMap *settings = seci.getNext();
			Ogre::ConfigFile::SettingsMultiMap::iterator i;
			for (i = settings->begin(); i != settings->end(); ++i)
			{
				typeName = i->first;
				archName = i->second;
				Ogre::ResourceGroupManager::getSingleton().addResourceLocation(PlatformUtil::getResourceDirectory() + archName, typeName, secName);
			}
		}
	}
	//---------------------------------------------------------
	bool Engine::configure(String const & windowTitle, ShowConfigBehavior showConfigBehavior)
	{
		// Show the configuration dialog and initialise the system
		if( (showConfigBehavior != SHOW_ALWAYS && this->root->restoreConfig()) || this->root->showConfigDialog())
		{
			// If returned true, user clicked OK so initialise
			// Here we choose to let the system create a default rendering window or a embedded window in externalhandle
			if(this->externalWindowHandle.empty())
			{
				try
				{
					this->renderWindow = this->root->initialise(true, windowTitle);
				}
				catch (...)
				{
					if (showConfigBehavior != SHOW_NEVER)
					{
						this->root->showConfigDialog();
					}
					else
					{
						oDebugMsg("core", 0, "Not allowed to show config dialog. using fallback config values.");

						// fallback values
						Ogre::RenderSystem* renderSystem = Ogre::Root::getSingleton().getRenderSystem();
						renderSystem->setConfigOption("Video Mode", "640 x 480 @ 32-bit colour");
						renderSystem->setConfigOption("VSync", "No");
						renderSystem->setConfigOption("Full Screen", "No");
						renderSystem->setConfigOption("Resource Creation Policy", "Create on all devices");
						renderSystem->setConfigOption("FSAA", "0");
						renderSystem->setConfigOption("Rendering Device", ""); 
					}
					this->renderWindow = root->initialise(true, windowTitle);
				}
				oAssert(this->renderWindow);
			}
			else
			{
				this->renderWindow = this->root->initialise(false/*, windowTitle*/);
				
				unsigned int width = 640;
				unsigned int height = 480;
				unsigned int depth; 
				int left = 0;
				int top = 0;
				if(this->renderWindow)
				{
					this->renderWindow->getMetrics(width, height, depth, left, top);
				}

				Ogre::NameValuePairList params;
				params["parentWindowHandle"] = this->topLevelWindowHandle;
				params["externalWindowHandle"] = this->externalWindowHandle;

				try
				{
					oDebugMsg("core", 0, "Trying to create external RenderWindow with handle: " << this->externalWindowHandle << " and size: " << width << "x" << height);
					this->renderWindow = this->root->createRenderWindow(windowTitle, width, height, false, &params);

					this->renderWindow->setDeactivateOnFocusChange(false);
					this->renderWindow->setActive(true);
					this->renderWindow->reposition(left,top);
					this->renderWindow->resize(width, height);

					this->renderWindow->windowMovedOrResized();
				}
				catch (...)
				{
					oDebugMsg("core", 0, "Error while creating external RenderWindow showing config");
					this->root->showConfigDialog();
					oDebugMsg("core", 0, "Trying to create external window with handle: " << this->externalWindowHandle);
					this->renderWindow = this->root->createRenderWindow(windowTitle, width, height, false, &params);
				}
				oAssert(this->renderWindow);
				this->renderWindow->getMetrics( width, height, depth, left, top );
				oDebugMsg("core", 0, "external RenderWindow initialized! width, height, depth, left, top: " << width <<", "<< height<<", "<< depth<<", "<< left<<", "<< top);
			}
			return true;
		}
		else
		{
			return false;
		}
	}
	//---------------------------------------------------------
	bool Engine::frameStarted(const Ogre::FrameEvent& evt)
	{
		OPROFILEFUNC();
		if(this->externalWindowHandle.empty() && this->renderWindow->isClosed())
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
		if(this->externalWindowHandle.empty() && this->renderWindow->isClosed())
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
		if(this->externalWindowHandle.empty() && this->renderWindow->isClosed())
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
	OOBJECT_END
}
