/********************************************************************
	created:	Tuesday 2010/09/07 at 17:01
	filename: 	Engine.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/

#include "engine_graphic/Engine.h"
#include "engine_module/EnginePrerequisites.h"
#include <core_event/GlobalEventManager.h>
#include <core_debug/Profile.h>

namespace Orkige
{
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
		viewport(NULL)
	{
		this->data = onew(new FrameEventData());

		this->frameStartedEvent.setData(this->data);
		this->frameRenderingQueuedEvent.setData(this->data);
		this->frameEndedEvent.setData(this->data);
		//create the ogre root Master of Disaster
		this->root = optr<Ogre::Root>(new Ogre::Root(pluginCfgFileName, renderCfgFileName, engineLogFileName));

		this->setupResources(resourceCfgFileName);
	}
	//---------------------------------------------------------
	Engine::~Engine()
	{
	}
	//---------------------------------------------------------
	bool Engine::setup(bool alwaysShowConfigDialog, String const & windowTitle, String const & externalHandle)
	{
		this->externalWindowHandle = externalHandle;
		this->topLevelWindowHandle = externalHandle;
		if(!this->configure(alwaysShowConfigDialog, windowTitle))
			return false;

		// Create the SceneManager
		this->sceneManager = root->createSceneManager(this->sceneType, "OrkigeSceneManager");
		oAssert(this->sceneManager);

		// Set default mipmap level (NB some APIs ignore this)
		Ogre::TextureManager::getSingleton().setDefaultNumMipmaps(5);

		this->eventManager = GlobalEventManager::getSingletonPtr();
		oAssert(this->eventManager);
		this->root->addFrameListener(this);

		return true;
	}
	//---------------------------------------------------------
	bool Engine::renderOneFrame()
	{
		Ogre::WindowEventUtilities::messagePump();
		return this->root->renderOneFrame();
	}
	//---------------------------------------------------------
	void Engine::createDefaultCameraAndViewport()
	{
		// Create the camera
		this->camera = this->sceneManager->createCamera("OrkigeCamera");
		this->camera->setNearClipDistance(1.0f);
		this->camera->setFarClipDistance(100000);
		// Create one viewport, entire window
		this->viewport = this->renderWindow->addViewport(this->camera);
		this->viewport->setBackgroundColour(Ogre::ColourValue(0,0,0));
		this->viewport->setShadowsEnabled(true);

		// Alter the camera aspect ratio to match the viewport
		this->camera->setAspectRatio(	Ogre::Real(this->viewport->getActualWidth()) / Ogre::Real(this->viewport->getActualHeight()));
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

		//Load Log Config


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
				Ogre::ResourceGroupManager::getSingleton().addResourceLocation(archName, typeName, secName);
			}
		}
	}
	//---------------------------------------------------------
	bool Engine::configure(bool alwaysShowConfigDialog, String const & windowTitle)
	{
		// Show the configuration dialog and initialise the system
		if( (!alwaysShowConfigDialog && this->root->restoreConfig()) || this->root->showConfigDialog())
		{
			// If returned true, user clicked OK so initialise
			// Here we choose to let the system create a default rendering window or a embedded windo in externalhandle
			if(this->externalWindowHandle.empty())
			{
				try
				{
					this->renderWindow = this->root->initialise(true, windowTitle);
				}
				catch (...)
				{
					this->root->showConfigDialog();
					this->renderWindow = root->initialise(true, windowTitle);
				}

			}
			else
			{
				this->renderWindow = this->root->initialise(false, windowTitle);
				oAssert(this->renderWindow);

				unsigned int width;
				unsigned int height;
				unsigned int colourDepth; 
				int left;
				int top;
				this->renderWindow->getMetrics(width, height, colourDepth, left, top);

				Ogre::NameValuePairList params;
				params["externalWindowHandle"] = this->externalWindowHandle;

				try
				{
					this->renderWindow = this->root->createRenderWindow(windowTitle, width, height, false, &params);
				}
				catch (...)
				{
					this->root->showConfigDialog();
					this->renderWindow = this->root->createRenderWindow(windowTitle, width, height, false, &params);
				}

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
		if(this->renderWindow->isClosed())
			return false;
		this->data->timeSinceLastEvent = evt.timeSinceLastEvent;
		this->data->timeSinceLastFrame = evt.timeSinceLastFrame;
		this->eventManager->trigger(this->frameStartedEvent);
		return true;
	}
	//---------------------------------------------------------
	bool Engine::frameRenderingQueued(const Ogre::FrameEvent& evt)
	{
		if(this->renderWindow->isClosed())
			return false;
		this->data->timeSinceLastEvent = evt.timeSinceLastEvent;
		this->data->timeSinceLastFrame = evt.timeSinceLastFrame;
		this->eventManager->trigger(this->frameRenderingQueuedEvent);
		return true;
	}
	//---------------------------------------------------------
	bool Engine::frameEnded(const Ogre::FrameEvent& evt)
	{
		if(this->renderWindow->isClosed())
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
		OFUNC(setTopLevelWindowHandle)
		OFUNC(getTopLevelWindowHandle)
		OFUNC(createDefaultCameraAndViewport)
		OFUNC(renderOneFrame)
		OFUNC(enableWireframeMode)
		OFUNC(disableWireframeMode)
	OOBJECT_END
}
