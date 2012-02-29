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
#include <boost/algorithm/string.hpp>
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
		virtual void messageLogged( const Ogre::String& message, Ogre::LogMessageLevel lml, bool maskDebug, const String &logName
#if OGRE_VERSION_MINOR >= 8
			, bool& skipThisMessage
#endif	
			)
		{
#if OGRE_VERSION_MINOR >= 8
			if(skipThisMessage) return;
#endif	
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
	Engine::Engine(Ogre::SceneType st, String const & resourceCfgFileName, String const & pluginCfgFileName, String const & renderCfgFileName, String const & engineLogFileName, unsigned int _numberOfWindows, String const & zipFileName, String const & zipInternalPathPrefix) 
		: frameStartedEvent(Engine::FrameStartedEvent), 
		frameRenderingQueuedEvent(Engine::FrameRenderingQueuedEvent),
		frameEndedEvent(Engine::FrameEndedEvent),
		sceneType(st),
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
		
		this->root = optr<Ogre::Root>(new Ogre::Root(pluginCfgFileName, renderCfgPlatformFileName, engineLogFileName));
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
		this->root.reset();
		this->bigZipArchiveFactory.reset();

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
	void Engine::setCustomWindowParam(Orkige::String paramName, Orkige::String paramValue, unsigned int windowNumber)
	{
		this->windowParams[windowNumber][paramName] = paramValue;
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
		for (unsigned int each = 0; each < this->numberOfWindows; ++each)
		{
			this->renderWindow[each]->_updateViewport(this->viewport[each], true);
		}
		
		this->eventManager->trigger(this->frameRenderingQueuedEvent);
		
		for (unsigned int each = 0; each < this->numberOfWindows; ++each)
		{
			this->renderWindow[each]->_beginUpdate();
			this->renderWindow[each]->swapBuffers();
			this->renderWindow[each]->_endUpdate();
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
			// Create one viewport, entire window
			this->viewport[each] = this->renderWindow[each]->addViewport(this->camera[each]);
			this->viewport[each]->setBackgroundColour(Ogre::ColourValue(0,0,0));
			this->viewport[each]->setShadowsEnabled(true);

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
		for(std::size_t i = Engine::AspectRatio_Unknown-1; i >= 0; i--)
		{
			if(aspectFactor - aspectRatioFactors[i] < maxErrorDist)
			{
				return Engine::AspectRatio(i);
			}
		}
		return Engine::AspectRatio_Unknown;
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
					//if there are no params for window 0 we úse the auto create window
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

						// fallback device, this may set video mode and anti-aliasing settings
						const Ogre::ConfigOptionMap& options = renderSystem->getConfigOptions();
						Ogre::ConfigOptionMap::const_iterator optIt = options.find( "Rendering Device" );
						if( optIt != options.end() )
						{
							Ogre::StringVector possibleVideoModes = optIt->second.possibleValues;
							if (!possibleVideoModes.empty())
							{
								renderSystem->setConfigOption("Rendering Device", possibleVideoModes.at(0)); 
							}
						}
					}
					this->renderWindow[0] = root->initialise(true, windowTitle);
				}
				oAssert(this->renderWindow[0]);
			}
			else
			{
				this->renderWindow[0] = this->root->initialise(false/*, windowTitle*/);
				
				unsigned int width = 640;
				unsigned int height = 480;
				unsigned int depth; 
				int left = 0;
				int top = 0;
				if(this->renderWindow[0])
				{
					this->renderWindow[0]->getMetrics(width, height, depth, left, top);
				}

				Ogre::NameValuePairList params;
				params["parentWindowHandle"] = this->topLevelWindowHandle;
				params["externalWindowHandle"] = this->externalWindowHandle;

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
				catch (...)
				{
					oDebugMsg("core", 0, "Error while creating external RenderWindow showing config");
					this->root->showConfigDialog();
					oDebugMsg("core", 0, "Trying to create external window with handle: " << this->externalWindowHandle);
					this->renderWindow[0] = this->root->createRenderWindow(windowTitle, width, height, false, &params);
				}
				oAssert(this->renderWindow[0]);
				this->renderWindow[0]->getMetrics( width, height, depth, left, top );
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
	OOBJECT_END
}
