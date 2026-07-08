/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	NextBackend.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file NextBackend.cpp
//! @brief backend hub: Ogre-Next boot/teardown, node registry, services
//! @remarks the per-class facade method bodies live in the sibling
//! *Next.cpp TUs; this TU owns the process-wide backend state. The boot
//! is the Next-flavor replacement of classic's Engine::setup (on Next
//! the RenderSystem facade IS the boot - see Docs/render-abstraction.md).

#include "engine_render_next/NextBackend.h"

#include <OgreRoot.h>
#include <OgreWindow.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreCamera.h>
#include <OgreLogManager.h>
#include <OgreArchiveManager.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsPbs.h>
#include <OgreHlmsUnlit.h>
#include <OgreMetalPlugin.h>
#include <Compositor/OgreCompositorManager2.h>

#include <cstdio>
#include <filesystem>
#include <set>
#include <unordered_map>

namespace Orkige
{
	namespace
	{
		//! the live render system behind RenderSystem::get (one per
		//! process - the build-time backend rule, no runtime switch)
		RenderSystem* gRenderSystem = NULL;
		//! the statically linked Metal render system plugin
		Ogre::MetalPlugin* gMetalPlugin = NULL;
		//! back-mapping registry (same contract as the classic backend)
		std::unordered_map<Ogre::SceneNode*, woptr<RenderNode>> gNodeRegistry;
		//! monotonic counter behind RenderBackend::generateName
		unsigned long gNameCounter = 0;

		//! load the Hlms shader template archives the material system
		//! compiles from (the sample-framework recipe against the media
		//! the ogre-next port ships); no-op when the directory is absent -
		//! clear-only rendering works without any Hlms
		void registerHlms(String const & hlmsMediaDir)
		{
			if(hlmsMediaDir.empty())
			{
				return;
			}
			String rootFolder = hlmsMediaDir;
			if(rootFolder.back() != '/')
			{
				rootFolder += '/';
			}
			if(!std::filesystem::exists(rootFolder + "Hlms"))
			{
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: no Hlms templates under '" +
					rootFolder + "' - materials will not work (B1 skeleton)");
				return;
			}
			Ogre::ArchiveManager & archiveManager =
				Ogre::ArchiveManager::getSingleton();
			Ogre::HlmsManager* hlmsManager =
				Ogre::Root::getSingleton().getHlmsManager();
			String mainFolderPath;
			Ogre::StringVector libraryFoldersPaths;
			{
				Ogre::HlmsUnlit::getDefaultPaths(mainFolderPath,
					libraryFoldersPaths);
				Ogre::Archive* archiveUnlit = archiveManager.load(
					rootFolder + mainFolderPath, "FileSystem", true);
				Ogre::ArchiveVec libraryUnlit;
				for(String const & each : libraryFoldersPaths)
				{
					libraryUnlit.push_back(archiveManager.load(
						rootFolder + each, "FileSystem", true));
				}
				// registerHlms takes ownership (deleteOnExit default)
				hlmsManager->registerHlms(
					OGRE_NEW Ogre::HlmsUnlit(archiveUnlit, &libraryUnlit));
			}
			{
				Ogre::HlmsPbs::getDefaultPaths(mainFolderPath,
					libraryFoldersPaths);
				Ogre::Archive* archivePbs = archiveManager.load(
					rootFolder + mainFolderPath, "FileSystem", true);
				Ogre::ArchiveVec libraryPbs;
				for(String const & each : libraryFoldersPaths)
				{
					libraryPbs.push_back(archiveManager.load(
						rootFolder + each, "FileSystem", true));
				}
				hlmsManager->registerHlms(
					OGRE_NEW Ogre::HlmsPbs(archivePbs, &libraryPbs));
			}
		}
	}
	//---------------------------------------------------------
	RenderSystem* RenderBackend::createRenderSystem(
		NextBootOptions const & options)
	{
		if(gRenderSystem)
		{
			return gRenderSystem;	// boot runs once; be idempotent anyway
		}
		// no plugins.cfg / ogre.cfg - the render system is linked
		// statically and installed right here
		Ogre::Root* root = OGRE_NEW Ogre::Root(NULL /*abiCookie*/,
			"" /*pluginFileName*/, "" /*configFileName*/,
			options.logFileName, "Orkige");
		gMetalPlugin = OGRE_NEW Ogre::MetalPlugin();
		root->installPlugin(gMetalPlugin, NULL);
		Ogre::RenderSystemList const & renderers =
			root->getAvailableRenderers();
		oAssert(!renderers.empty());
		root->setRenderSystem(renderers.front());
		root->initialise(false /*autoCreateWindow*/);

		Ogre::NameValuePairList windowParams;
		if(!options.nativeWindowHandle.empty())
		{
			// the SDL-hosted window (Next's Metal window embeds its own
			// OgreMetalView into the NSWindow's content view)
			windowParams["externalWindowHandle"] = options.nativeWindowHandle;
		}
		Ogre::Window* window = root->createRenderWindow(options.windowTitle,
			options.width, options.height, false /*fullScreen*/,
			&windowParams);
		// facade screenshots read the window back - Metal disallows that
		// on framebufferOnly layers unless the window opts in
		window->setWantsToDownload(true);
		window->_setVisible(true);

		registerHlms(options.hlmsMediaDir);

		// one worker thread: the engine's scenes are small during the
		// revival; tune when a real workload appears
		Ogre::SceneManager* sceneManager = root->createSceneManager(
			Ogre::ST_GENERIC, 1, "OrkigeNextWorld");

		RenderSystem* system = new RenderSystem();
		system->mImpl->root = root;
		system->mImpl->window = window;
		RenderWorld* world = new RenderWorld();
		world->mImpl->sceneManager = sceneManager;
		system->mImpl->world = world;
		gRenderSystem = system;
		return gRenderSystem;
	}
	//---------------------------------------------------------
	void RenderBackend::destroyRenderSystem()
	{
		if(!gRenderSystem)
		{
			return;
		}
		Ogre::Root* root = gRenderSystem->mImpl->root;
		delete gRenderSystem;	// ~RenderSystem deletes the world first
		gRenderSystem = NULL;
		// same late-handle rule as classic: handles that outlive the
		// backend free facade memory only (their dtors check system())
		gNodeRegistry.clear();
		OGRE_DELETE root;
		OGRE_DELETE gMetalPlugin;
		gMetalPlugin = NULL;
	}
	//---------------------------------------------------------
	RenderSystem* RenderBackend::system()
	{
		return gRenderSystem;
	}
	//---------------------------------------------------------
	Ogre::SceneNode* RenderBackend::sceneNode(optr<RenderNode> const & node)
	{
		return node ? node->mImpl->node : NULL;
	}
	//---------------------------------------------------------
	Ogre::Camera* RenderBackend::ogreCamera(optr<RenderCamera> const & camera)
	{
		return camera ? camera->mImpl->camera : NULL;
	}
	//---------------------------------------------------------
	void RenderBackend::registerNode(Ogre::SceneNode* node,
		optr<RenderNode> const & handle)
	{
		oAssert(node);
		gNodeRegistry[node] = handle;
	}
	//---------------------------------------------------------
	void RenderBackend::unregisterNode(Ogre::SceneNode* node)
	{
		gNodeRegistry.erase(node);
	}
	//---------------------------------------------------------
	optr<RenderNode> RenderBackend::findNode(Ogre::SceneNode* node)
	{
		if(!node)
		{
			return optr<RenderNode>();
		}
		auto found = gNodeRegistry.find(node);
		if(found == gNodeRegistry.end())
		{
			return optr<RenderNode>();
		}
		return found->second.lock();
	}
	//---------------------------------------------------------
	void* RenderBackend::findUserPointerUpwards(Ogre::SceneNode* node)
	{
		// walk the BACKEND parent chain (not the facade graph) so the walk
		// also crosses nodes that were never wrapped into facade handles
		for(Ogre::Node* each = node; each != NULL; each = each->getParent())
		{
			optr<RenderNode> handle =
				findNode(static_cast<Ogre::SceneNode*>(each));
			if(handle && handle->mImpl->userPointer)
			{
				return handle->mImpl->userPointer;
			}
		}
		return NULL;
	}
	//---------------------------------------------------------
	String RenderBackend::generateName(String const & prefix)
	{
		return prefix + "." + std::to_string(++gNameCounter);
	}
	//---------------------------------------------------------
	void RenderBackend::recreateWindowWorkspace()
	{
		oAssert(gRenderSystem);
		RenderSystem::Impl* impl = gRenderSystem->mImpl;
		Ogre::CompositorManager2* compositorManager =
			impl->root->getCompositorManager2();
		if(impl->workspace)
		{
			compositorManager->removeWorkspace(impl->workspace);
			impl->workspace = NULL;
		}
		Ogre::Camera* backendCamera =
			RenderBackend::ogreCamera(impl->windowCamera);
		if(!backendCamera)
		{
			return;	// nothing shown on the window yet
		}
		// one basic (clear + scene) workspace per camera/background state;
		// definitions are cheap and names must be unique, so each rebuild
		// gets a fresh one (background colour bakes into the clear pass)
		const String definitionName =
			RenderBackend::generateName("Orkige/WindowWorkspace");
		compositorManager->createBasicWorkspaceDef(definitionName,
			impl->windowBackground);
		impl->workspace = compositorManager->addWorkspace(
			gRenderSystem->getWorld()->mImpl->sceneManager,
			impl->window->getTexture(), backendCamera, definitionName,
			true /*enabled*/);
		if(impl->window->getHeight() > 0)
		{
			backendCamera->setAspectRatio(
				Ogre::Real(impl->window->getWidth()) /
				Ogre::Real(impl->window->getHeight()));
		}
	}
	//---------------------------------------------------------
	void RenderBackend::notImplementedOnce(char const * feature)
	{
		static std::set<String> alreadyLogged;
		if(!alreadyLogged.insert(feature).second)
		{
			return;
		}
		const String message = String("Orkige next backend: '") + feature +
			"' is not implemented on the next backend yet (B2, see "
			"Docs/render-abstraction.md phase A2) - returning a safe default";
		if(Ogre::LogManager::getSingletonPtr())
		{
			Ogre::LogManager::getSingleton().logMessage(message);
		}
		std::fprintf(stderr, "%s\n", message.c_str());
	}
}
