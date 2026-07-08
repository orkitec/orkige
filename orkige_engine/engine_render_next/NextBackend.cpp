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
#include <OgreResourceGroupManager.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsDatablock.h>
#include <OgreHlmsPbs.h>
#include <OgreHlmsPbsDatablock.h>
#include <OgreHlmsUnlit.h>
#include <OgreHlmsUnlitDatablock.h>
#if defined(__APPLE__)
#include <OgreMetalPlugin.h>
#else
// non-Apple: Vulkan is the Ogre-Next render system (Metal is Apple-only;
// ports/ogre-next builds the Vulkan RS with XCB windowing on Linux)
#include <OgreVulkanPlugin.h>
#endif
#include <OgrePlugin.h>
#include <OgreRenderSystem.h>
#include <OgreTextureGpuManager.h>
#include <OgreTextureFilters.h>
#include <OgreTextureBox.h>
#include <OgreImage2.h>
#include <OgreDataStream.h>
#include <OgrePixelFormatGpuUtils.h>
#include <OgreException.h>
#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorNodeDef.h>
#include <Compositor/OgreCompositorWorkspaceDef.h>
#include <Compositor/Pass/PassScene/OgreCompositorPassSceneDef.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <unordered_map>
#include <vector>

namespace Orkige
{
	namespace
	{
		//! the live render system behind RenderSystem::get (one per
		//! process - the build-time backend rule, no runtime switch)
		RenderSystem* gRenderSystem = NULL;
		//! the statically linked render system plugin (Metal on Apple,
		//! Vulkan elsewhere - one RS per platform, installed at boot)
		Ogre::Plugin* gRenderSystemPlugin = NULL;
		//! back-mapping registry (same contract as the classic backend)
		std::unordered_map<Ogre::SceneNode*, woptr<RenderNode>> gNodeRegistry;
		//! monotonic counter behind RenderBackend::generateName
		unsigned long gNameCounter = 0;
		//! every datablock the backend generated (wireframe toggle target);
		//! datablocks are shared by name and live until teardown
		std::vector<Ogre::HlmsDatablock*> gContentDatablocks;
		//! current global wireframe state (applied to late datablocks too)
		bool gWireframe = false;

		//! apply the global wireframe state to one datablock (keeps the
		//! datablock's other macroblock state - culling, depth - intact)
		void applyWireframe(Ogre::HlmsDatablock* datablock, bool enabled)
		{
			Ogre::HlmsMacroblock macroblock = *datablock->getMacroblock();
			const Ogre::PolygonMode mode =
				enabled ? Ogre::PM_WIREFRAME : Ogre::PM_SOLID;
			if(macroblock.mPolygonMode == mode)
			{
				return;
			}
			macroblock.mPolygonMode = mode;
			datablock->setMacroblock(macroblock);
		}

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
					rootFolder + "' - materials will not work (mesh/sprite content needs Hlms)");
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
#if defined(__APPLE__)
		gRenderSystemPlugin = OGRE_NEW Ogre::MetalPlugin();
#else
		// TODO(linux): authored against the Ogre-Next 3.0 sources, first
		// real Linux run pending (verified in CI - see .github/workflows)
		gRenderSystemPlugin = OGRE_NEW Ogre::VulkanPlugin();
#endif
		root->installPlugin(gRenderSystemPlugin, NULL);
		Ogre::RenderSystemList const & renderers =
			root->getAvailableRenderers();
		oAssert(!renderers.empty());
		root->setRenderSystem(renderers.front());
		// v2 draws only count into RenderingMetrics while recording is on -
		// the facade FrameStats (triangles/batches) read those metrics
		root->getRenderSystem()->setMetricsRecordingEnabled(true);
		root->initialise(false /*autoCreateWindow*/);

		Ogre::NameValuePairList windowParams;
		// "0" = the platform bridge found no native handle (e.g. a pure
		// Wayland session on Linux, see SDLNativeWindowLinux.cpp) - fall
		// back to letting the render system create its own window
		if(!options.nativeWindowHandle.empty() &&
			options.nativeWindowHandle != "0")
		{
#if defined(__APPLE__)
			// the SDL-hosted window (Next's Metal window embeds its own
			// OgreMetalView into the NSWindow's content view)
			windowParams["externalWindowHandle"] = options.nativeWindowHandle;
#else
			// Linux: VulkanXcbWindow's external-window path is the "SDL2x11"
			// misc param - a (stringified) pointer to {Display*, ::Window},
			// exactly what engine_util/SDLNativeWindowLinux.cpp hands out on
			// this flavor ("externalWindowHandle" is ignored by the xcb
			// windowing). TODO(linux): first real run pending.
			windowParams["SDL2x11"] = options.nativeWindowHandle;
#endif
		}
		// CLASSIC COLOUR PARITY (the WYSIWYG rule - backends must render
		// the same image): the classic backend runs a gamma-space pipeline
		// with no hardware sRGB conversion anywhere (non-sRGB swapchain,
		// textures sampled raw). Ogre-Next defaults the window to an sRGB
		// swapchain, which re-encodes on write and rendered everything
		// brighter than classic. Opt out - together with the non-sRGB
		// texture loads (loadTexture2D) and the non-sRGB RTT format this
		// makes the whole Next pipeline gamma-space passthrough, byte-
		// matching classic for unlit/vertex-colour/textured content.
		windowParams["gamma"] = "false";
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
		gContentDatablocks.clear();	// owned by their Hlms, die with the root
		gWireframe = false;
		RenderBackend::resetDrawLayer2DState();
		OGRE_DELETE root;
		OGRE_DELETE gRenderSystemPlugin;
		gRenderSystemPlugin = NULL;
	}
	//---------------------------------------------------------
	RenderSystem* RenderBackend::system()
	{
		return gRenderSystem;
	}
	//---------------------------------------------------------
	Ogre::Root* RenderBackend::ogreRoot()
	{
		return gRenderSystem ? gRenderSystem->mImpl->root : NULL;
	}
	//---------------------------------------------------------
	Ogre::SceneManager* RenderBackend::worldSceneManager()
	{
		return gRenderSystem
			? gRenderSystem->getWorld()->mImpl->sceneManager : NULL;
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
		if(!backendCamera && !impl->uiOnlyWindow)
		{
			return;	// nothing shown on the window yet
		}
		// one workspace definition per camera/background state; definitions
		// are cheap and names must be unique, so each rebuild gets a fresh
		// one (background colour bakes into the clear pass). Hand-built
		// instead of createBasicWorkspaceDef since the DrawLayer2D port:
		// pass 1 clears + renders the scene queues (< the UI queue), pass 2
		// composites the 2D layers - the UI render queue only - through the
		// pixel-space UI camera (referenced by name; created up front so
		// the pass can resolve it whether or not any layer exists yet).
		// UI-ONLY mode (showUIOnlyWindow, the editor shell) drops pass 1:
		// the whole workspace is one clear + UI-queue pass on the UI camera.
		Ogre::Camera* uiCamera = RenderBackend::ensureDrawLayer2DCamera();
		const String definitionName =
			RenderBackend::generateName("Orkige/WindowWorkspace");
		Ogre::CompositorNodeDef* nodeDefinition =
			compositorManager->addNodeDefinition(definitionName + "/Node");
		nodeDefinition->addTextureSourceName("WindowRT", 0,
			Ogre::TextureDefinitionBase::TEXTURE_INPUT);
		nodeDefinition->setNumTargetPass(1);
		Ogre::CompositorTargetDef* targetDefinition =
			nodeDefinition->addTargetPass("WindowRT");
		targetDefinition->setNumPasses(impl->uiOnlyWindow ? 1 : 2);
		if(!impl->uiOnlyWindow)
		{
			Ogre::CompositorPassSceneDef* scenePass =
				static_cast<Ogre::CompositorPassSceneDef*>(
					targetDefinition->addPass(Ogre::PASS_SCENE));
			scenePass->setAllLoadActions(Ogre::LoadAction::Clear);
			scenePass->setAllClearColours(impl->windowBackground);
			scenePass->mFirstRQ = 0;
			scenePass->mLastRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE;
		}
		{
			Ogre::CompositorPassSceneDef* uiPass =
				static_cast<Ogre::CompositorPassSceneDef*>(
					targetDefinition->addPass(Ogre::PASS_SCENE));
			if(impl->uiOnlyWindow)
			{
				uiPass->setAllLoadActions(Ogre::LoadAction::Clear);
				uiPass->setAllClearColours(impl->windowBackground);
			}
			else
			{
				uiPass->setAllLoadActions(Ogre::LoadAction::Load);
			}
			uiPass->mFirstRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE;
			uiPass->mLastRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE + 1;
			uiPass->mCameraName = RenderBackend::drawLayer2DCameraName();
		}
		Ogre::CompositorWorkspaceDef* workspaceDefinition =
			compositorManager->addWorkspaceDefinition(definitionName);
		workspaceDefinition->connectExternal(0, definitionName + "/Node", 0);
		impl->workspace = compositorManager->addWorkspace(
			gRenderSystem->getWorld()->mImpl->sceneManager,
			impl->window->getTexture(),
			backendCamera ? backendCamera : uiCamera, definitionName,
			true /*enabled*/);
		if(backendCamera && impl->window->getHeight() > 0)
		{
			backendCamera->setAspectRatio(
				Ogre::Real(impl->window->getWidth()) /
				Ogre::Real(impl->window->getHeight()));
		}
	}
	//---------------------------------------------------------
	Ogre::TextureGpu* RenderBackend::loadTexture2D(String const & textureName)
	{
		oAssert(gRenderSystem);
		Ogre::TextureGpuManager* textureManager = gRenderSystem->mImpl->root
			->getRenderSystem()->getTextureGpuManager();
		// backend-object textures first (createTexture2DFromPixels uploads -
		// e.g. an ImGui font atlas - have no resource-group entry)
		if(Ogre::TextureGpu* existing =
			textureManager->findTextureNoThrow(textureName))
		{
			return existing;
		}
		try
		{
			// resolve through EVERY resource group, same rule as classic:
			// engine media and project assets both work by plain file name
			Ogre::ResourceGroupManager & resourceGroups =
				Ogre::ResourceGroupManager::getSingleton();
			const String group =
				resourceGroups.findGroupContainingResource(textureName);
			// NOT CommonTextureTypes::Diffuse: that would add
			// PrefersLoadingFromFileAsSRGB, decoding texels in the shader -
			// the classic pipeline samples texels raw (colour parity rule,
			// see the boot's "gamma" note); mipmaps stay
			Ogre::TextureGpu* texture = textureManager->createOrRetrieveTexture(
				textureName, textureName, Ogre::GpuPageOutStrategy::Discard,
				Ogre::TextureFlags::AutomaticBatching,
				Ogre::TextureTypes::Type2D, group,
				Ogre::TextureFilter::TypeGenerateDefaultMipmaps);
			if(texture->getResidencyStatus() == Ogre::GpuResidency::OnStorage)
			{
				texture->scheduleTransitionTo(Ogre::GpuResidency::Resident);
			}
			// the facade hands out texel sizes synchronously
			texture->waitForMetadata();
			return texture;
		}
		catch(Ogre::Exception const & e)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: texture '" + textureName +
				"' failed to load: " + e.getDescription());
			return NULL;
		}
	}
	//---------------------------------------------------------
	Ogre::TextureGpu* RenderBackend::createTexture2DFromMemory(
		String const & name, void const * bytes, size_t sizeBytes,
		String const & formatHint)
	{
		oAssert(gRenderSystem);
		Ogre::TextureGpuManager* textureManager = gRenderSystem->mImpl->root
			->getRenderSystem()->getTextureGpuManager();
		if(Ogre::TextureGpu* existing =
			textureManager->findTextureNoThrow(name))
		{
			return existing;	// idempotent per name (shared imports)
		}
		try
		{
			// decode through the registered image codecs (FreeImage), then
			// hand the Image2 to the streaming path (it owns + deletes it)
			Ogre::DataStreamPtr stream(OGRE_NEW Ogre::MemoryDataStream(
				const_cast<void*>(bytes), sizeBytes, false /*freeOnClose*/));
			Ogre::Image2* image = OGRE_NEW Ogre::Image2();
			image->load(stream, formatHint);
			Ogre::TextureGpu* texture = textureManager->createTexture(name,
				Ogre::GpuPageOutStrategy::Discard,
				Ogre::TextureFlags::AutomaticBatching,
				Ogre::TextureTypes::Type2D);
			texture->setResolution(image->getWidth(), image->getHeight());
			texture->setPixelFormat(image->getPixelFormat());
			texture->setNumMipmaps(1u);
			texture->scheduleTransitionTo(Ogre::GpuResidency::Resident, image,
				true /*autoDeleteImage*/);
			texture->waitForMetadata();
			return texture;
		}
		catch(Ogre::Exception const & e)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: embedded texture '" + name +
				"' failed to decode: " + e.getDescription());
			return NULL;
		}
	}
	//---------------------------------------------------------
	Ogre::TextureGpu* RenderBackend::createTexture2DFromPixels(
		String const & name, unsigned char const * rgbaPixels,
		unsigned int width, unsigned int height)
	{
		oAssert(gRenderSystem);
		if(name.empty() || !rgbaPixels || width == 0 || height == 0)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: createTexture2DFromPixels('" + name +
				"') refused (empty name/pixels/size)");
			return NULL;
		}
		Ogre::TextureGpuManager* textureManager = gRenderSystem->mImpl->root
			->getRenderSystem()->getTextureGpuManager();
		// replace-by-recreate (atlas rebuilds): drop any existing
		// incarnation, then re-point the 2D-layer datablock below
		RenderBackend::destroyTexture2DByName(name);
		// hand a SIMD-allocated copy to Image2 (it owns + frees it)
		const size_t sizeBytes = Ogre::PixelFormatGpuUtils::getSizeBytes(
			width, height, 1u, 1u, Ogre::PFG_RGBA8_UNORM, 4u);
		void* pixelCopy = OGRE_MALLOC_SIMD(sizeBytes, Ogre::MEMCATEGORY_RESOURCE);
		memcpy(pixelCopy, rgbaPixels, size_t(width) * size_t(height) * 4u);
		Ogre::Image2* image = OGRE_NEW Ogre::Image2();
		image->loadDynamicImage(pixelCopy, width, height, 1u,
			Ogre::TextureTypes::Type2D, Ogre::PFG_RGBA8_UNORM,
			true /*autoDelete*/, 1u);
		Ogre::TextureGpu* texture = textureManager->createTexture(name,
			Ogre::GpuPageOutStrategy::Discard,
			Ogre::TextureFlags::AutomaticBatching,
			Ogre::TextureTypes::Type2D);
		texture->setResolution(width, height);
		texture->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
		texture->setNumMipmaps(1u);
		texture->scheduleTransitionTo(Ogre::GpuResidency::Resident, image,
			true /*autoDeleteImage*/);
		texture->waitForMetadata();
		// a replaced texture must reach batches that already resolved the
		// old one: re-point the 2D-layer datablock (created lazily otherwise)
		{
			Ogre::HlmsManager* hlmsManager =
				RenderBackend::ogreRoot()->getHlmsManager();
			if(Ogre::HlmsDatablock* datablock =
				hlmsManager->getDatablockNoDefault("DrawLayer2D/" + name))
			{
				static_cast<Ogre::HlmsUnlitDatablock*>(datablock)
					->setTexture(0u, texture);
			}
		}
		return texture;
	}
	//---------------------------------------------------------
	void RenderBackend::destroyTexture2DByName(String const & name)
	{
		if(!gRenderSystem)
		{
			return;
		}
		Ogre::TextureGpuManager* textureManager = gRenderSystem->mImpl->root
			->getRenderSystem()->getTextureGpuManager();
		Ogre::TextureGpu* existing = textureManager->findTextureNoThrow(name);
		if(!existing)
		{
			return;	// idempotent
		}
		// detach from the generated 2D-layer datablock first (it would
		// otherwise reference a destroyed texture)
		Ogre::HlmsManager* hlmsManager =
			RenderBackend::ogreRoot()->getHlmsManager();
		if(Ogre::HlmsDatablock* datablock =
			hlmsManager->getDatablockNoDefault("DrawLayer2D/" + name))
		{
			Ogre::HlmsUnlitDatablock* unlitBlock =
				static_cast<Ogre::HlmsUnlitDatablock*>(datablock);
			if(unlitBlock->getTexture(0u) == existing)
			{
				unlitBlock->setTexture(0u, (Ogre::TextureGpu*)NULL);
			}
		}
		textureManager->destroyTexture(existing);
	}
	//---------------------------------------------------------
	Ogre::HlmsDatablock* RenderBackend::getOrCreateSpriteDatablock(
		String const & textureName, Ogre::TextureGpu* texture)
	{
		oAssert(gRenderSystem);
		Ogre::HlmsManager* hlmsManager =
			gRenderSystem->mImpl->root->getHlmsManager();
		const String name = "Sprite/" + textureName;
		if(Ogre::HlmsDatablock* existing =
			hlmsManager->getDatablockNoDefault(name))
		{
			return existing;
		}
		// the honest sprite rules carried over from classic: unlit,
		// alpha-blended, depth-checked/not-written, two-sided; tint and
		// flips live in the quad's vertex data so all sprites of one
		// texture share this one datablock
		Ogre::HlmsUnlit* unlit = static_cast<Ogre::HlmsUnlit*>(
			hlmsManager->getHlms(Ogre::HLMS_UNLIT));
		Ogre::HlmsMacroblock macroblock;
		macroblock.mDepthWrite = false;
		macroblock.mCullMode = Ogre::CULL_NONE;
		Ogre::HlmsBlendblock blendblock;
		blendblock.setBlendType(Ogre::SBT_TRANSPARENT_ALPHA);
		Ogre::HlmsUnlitDatablock* datablock =
			static_cast<Ogre::HlmsUnlitDatablock*>(unlit->createDatablock(
				name, name, macroblock, blendblock, Ogre::HlmsParamVec()));
		if(texture)
		{
			datablock->setTexture(0u, texture);
		}
		RenderBackend::registerContentDatablock(datablock);
		return datablock;
	}
	//---------------------------------------------------------
	Ogre::HlmsDatablock* RenderBackend::getOrCreateVertexColourUnlitDatablock(
		String const & datablockName, Ogre::TextureGpu* texture)
	{
		oAssert(gRenderSystem);
		Ogre::HlmsManager* hlmsManager =
			gRenderSystem->mImpl->root->getHlmsManager();
		if(Ogre::HlmsDatablock* existing =
			hlmsManager->getDatablockNoDefault(datablockName))
		{
			return existing;
		}
		// vertex colours flow automatically: HlmsUnlit sets hlms_colour
		// when the vertex format carries VES_DIFFUSE - no datablock knob
		// needed (the classic counterpart is Pass::setVertexColourTracking)
		Ogre::HlmsUnlit* unlit = static_cast<Ogre::HlmsUnlit*>(
			hlmsManager->getHlms(Ogre::HLMS_UNLIT));
		Ogre::HlmsUnlitDatablock* datablock =
			static_cast<Ogre::HlmsUnlitDatablock*>(unlit->createDatablock(
				datablockName, datablockName, Ogre::HlmsMacroblock(),
				Ogre::HlmsBlendblock(), Ogre::HlmsParamVec()));
		if(texture)
		{
			datablock->setTexture(0u, texture);
		}
		RenderBackend::registerContentDatablock(datablock);
		return datablock;
	}
	//---------------------------------------------------------
	Ogre::TextureGpu* RenderBackend::datablockDiffuseTexture(
		Ogre::HlmsDatablock* datablock)
	{
		if(!datablock || !datablock->getCreator())
		{
			return NULL;
		}
		// no RTTI needed: the creating Hlms type identifies the datablock
		switch(datablock->getCreator()->getType())
		{
		case Ogre::HLMS_PBS:
			return static_cast<Ogre::HlmsPbsDatablock*>(datablock)
				->getTexture(Ogre::PBSM_DIFFUSE);
		case Ogre::HLMS_UNLIT:
			return static_cast<Ogre::HlmsUnlitDatablock*>(datablock)
				->getTexture(0u);
		default:
			return NULL;
		}
	}
	//---------------------------------------------------------
	void RenderBackend::registerContentDatablock(Ogre::HlmsDatablock* datablock)
	{
		oAssert(datablock);
		gContentDatablocks.push_back(datablock);
		if(gWireframe)
		{
			applyWireframe(datablock, true);	// late-created content joins
		}
	}
	//---------------------------------------------------------
	void RenderBackend::setGlobalWireframe(bool enabled)
	{
		if(gWireframe == enabled)
		{
			return;
		}
		gWireframe = enabled;
		for(Ogre::HlmsDatablock* each : gContentDatablocks)
		{
			applyWireframe(each, enabled);
		}
	}
	//---------------------------------------------------------
	unsigned char RenderBackend::renderQueueForZOrder(int zOrder)
	{
		// same painter's mapping as classic: queue 50 +- 40; the whole
		// span sits inside Next's default-FAST (v2) queues 0..99
		const int clamped = std::clamp(zOrder,
			SpriteQuad::ZORDER_MIN, SpriteQuad::ZORDER_MAX);
		return static_cast<unsigned char>(50 + clamped);
	}
	//---------------------------------------------------------
	void RenderBackend::makeImageAlphaOpaque(Ogre::Image2 & image)
	{
		// screenshots are OPAQUE images (classic parity): render targets
		// carry alpha only as a rendering byproduct. Rewrite the alpha of
		// the 4-byte-per-pixel formats; anything else stays untouched.
		const Ogre::PixelFormatGpu format = image.getPixelFormat();
		if(Ogre::PixelFormatGpuUtils::getBytesPerPixel(format) != 4u ||
			!Ogre::PixelFormatGpuUtils::hasAlpha(format))
		{
			return;
		}
		for(Ogre::uint8 mip = 0; mip < image.getNumMipmaps(); ++mip)
		{
			Ogre::TextureBox box = image.getData(mip);
			for(Ogre::uint32 y = 0; y < box.height; ++y)
			{
				Ogre::uint8* row = reinterpret_cast<Ogre::uint8*>(
					box.at(0, y, 0));
				for(Ogre::uint32 x = 0; x < box.width; ++x)
				{
					row[x * 4u + 3u] = 0xFF;	// RGBA8/BGRA8: alpha is byte 3
				}
			}
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
