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
#include <OgreLight.h>
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
#include <OgreSceneManager.h>
#include <OgreLight.h>
#include <OgreMath.h>
#include <OgreAtmosphereNpr.h>
#include <OgreRectangle2D2.h>
#include <OgreMaterial.h>
#include <OgreTextureGpuManager.h>
#include <OgreTextureFilters.h>
#include <OgreTextureBox.h>
#include <OgreImage2.h>
#include <OgreDataStream.h>
#include <OgrePixelFormatGpuUtils.h>
#include <OgreException.h>
#include <OgreResourceTransition.h>
#include <OgreTextureGpu.h>
#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorNodeDef.h>
#include <Compositor/OgreCompositorShadowNode.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <Compositor/OgreCompositorWorkspaceDef.h>
#include <Compositor/OgreCompositorWorkspaceListener.h>
#include <Compositor/Pass/OgreCompositorPass.h>
#include <Compositor/Pass/PassScene/OgreCompositorPassSceneDef.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <filesystem>
#include <set>
#include <unordered_map>
#include <vector>

#if defined(ORKIGE_IPHONE)
//! Splice a render-system-created Metal view into the SDL-hosted UIKit
//! window (defined in engine_util/OgreMetalViewBridge.mm - takes opaque
//! pointers so the ObjC++ bridge stays free of Ogre types). metalView is
//! the OgreMetalView* fetched from the window's "UIView" attribute;
//! uiWindow is SDL's UIWindow* (the stringified native handle).
extern "C" void orkige_ios_attach_metal_view(void* metalView, void* uiWindow);
#endif

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
		//! per-incarnation RTT datablocks whose incarnation has died but whose
		//! batch may still link them; destroyed once unlinked (@see
		//! RenderBackend::retireRTTDatablock / flushRetiredRTTDatablocks)
		std::set<String> gRetiredRTTDatablocks;
		//! current global wireframe state (applied to late datablocks too)
		bool gWireframe = false;
		//! per water-datablock ripple tunables (waveScale/waveSpeed), so the
		//! per-frame setWaterDatablockTime can recompute the two detail-normal
		//! scroll offsets. Keyed by the datablock name; a stale entry (its
		//! datablock destroyed on a project switch) is harmless - the scroll
		//! looks the name up again and no-ops when the datablock is gone.
		struct WaterAnim
		{
			float waveScale;	//!< detail-normal tiling factor
			float waveSpeed;	//!< scroll speed (UV units per second)
		};
		std::unordered_map<String, WaterAnim> gWaterAnims;
		//! how many lights currently ask to cast shadows (RenderLight::
		//! setCastShadows tally); shadows render only while > 0
		int gShadowCasterCount = 0;
		//! every live render target (RenderTexture) - applyShadowConfig
		//! rebuilds their workspaces so scene passes follow the shadow state
		std::vector<RenderTexture*> gRenderTargets;
		//! the one live sky/fog atmosphere (RenderWorld::setAtmosphere), NULL
		//! while disabled; owned here, destroyed before the root teardown
		Ogre::AtmosphereNpr* gAtmosphere = NULL;
		//! did the atmosphere sky material media register at boot? (false on a
		//! media-less/headless boot - setAtmosphere then degrades honestly)
		bool gAtmosphereMediaAvailable = false;
		//! restore-exactly bookkeeping: the atmosphere OVERRIDES its linked
		//! sun's colour/power (AtmosphereNpr::syncToLight), so the light's
		//! authored values are snapshotted the moment the atmosphere takes it
		//! and written back EXACTLY when the atmosphere lets go (disable, sun
		//! change, teardown) - the recover-then-reapply rule (@see ScreenShake)
		Ogre::Light* gLinkedSun = NULL;
		Ogre::ColourValue gLinkedSunDiffuse;
		Ogre::ColourValue gLinkedSunSpecular;
		Ogre::Real gLinkedSunPower = 1.0f;

		//! the render queue the atmosphere sky quad draws from: the FIRST v2
		//! queue, before all scene content (depth-checked + write-off, so
		//! opaque geometry covers it and 3D alpha content composites on top -
		//! the classic dome's skies-early placement). The upstream default
		//! (212, "after most stuff") sits past this backend's scene passes AND
		//! would overdraw non-depth-writing sprites/particles.
		const unsigned char kSkyRenderQueue = 0;

		//! give the linked sun its authored colour/power back (no-op when the
		//! atmosphere holds no light)
		void restoreLinkedSun()
		{
			if(gLinkedSun)
			{
				gLinkedSun->setDiffuseColour(gLinkedSunDiffuse);
				gLinkedSun->setSpecularColour(gLinkedSunSpecular);
				gLinkedSun->setPowerScale(gLinkedSunPower);
				gLinkedSun = NULL;
			}
		}
		//! directional lights in creation order - the sun the atmosphere links
		//! to is the FIRST of these (@see RenderBackend::firstDirectionalLight)
		std::vector<Ogre::Light*> gDirectionalLights;

		//! @brief puts every live offscreen target back into the SAMPLEABLE
		//! resource layout before the window's passes run
		//! @remarks this backend tracks a GPU resource layout per texture and
		//! derives the barriers a pass needs from what the COMPOSITOR declares.
		//! An offscreen target is rendered by its own workspace and then
		//! SAMPLED by a 2D batch of the WINDOW workspace (the editor's scene
		//! and preview panels bind a RenderTexture into DrawLayer2D) - a
		//! dependency no workspace definition carries, so nothing moves the
		//! target out of the render-target layout it was left in. Backends with
		//! explicit layouts REJECT sampling a texture in that layout; implicit
		//! ones tolerate it. This listener rides the SAMPLING workspace and
		//! resolves the transition itself, which is the same barrier the
		//! compositor would insert for a declared input (the layout tracker
		//! dedupes, so a target already sampleable costs nothing). It runs from
		//! passPreExecute - before the pass opens its render pass, the only
		//! point at which a barrier may be issued.
		class RenderTargetSampleBarrier
			: public Ogre::CompositorWorkspaceListener
		{
		public:
			virtual void passPreExecute(Ogre::CompositorPass* /*pass*/)
			{
				RenderBackend::transitionRenderTargetsForSampling();
			}
		};
		//! one instance, attached to the window workspace on every rebuild
		//! (a listener is a plain observer - the workspace does not own it)
		RenderTargetSampleBarrier gRenderTargetSampleBarrier;

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
				// registerHlms takes ownership (deleteOnExit default). Silence
				// the debug shader dump first: a Debug build defaults the Hlms
				// debug output to on, so every generated shader is written into
				// the process working directory - the engine keeps its runtime
				// directories clean.
				Ogre::HlmsUnlit* unlit =
					OGRE_NEW Ogre::HlmsUnlit(archiveUnlit, &libraryUnlit);
				unlit->setDebugOutputPath(false, false);
				hlmsManager->registerHlms(unlit);
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
				Ogre::HlmsPbs* pbs =
					OGRE_NEW Ogre::HlmsPbs(archivePbs, &libraryPbs);
				pbs->setDebugOutputPath(false, false);	// as above
				hlmsManager->registerHlms(pbs);
			}
		}

		//! register the AtmosphereNpr sky material media (the ogre-next port
		//! ships it under <mediaRoot>/Atmosphere, beside Hlms/). These low-level
		//! material + program scripts define "Ogre/Atmo/NprSky" that the
		//! AtmosphereNpr ctor looks up; they parse into the DEFAULT group when
		//! the app initialises its resource groups. Sets the availability flag
		//! setAtmosphere reads (a media-less boot leaves it false → honest
		//! no-op). The HlmsPbs object-fog integration pieces ride in the Hlms
		//! Pbs templates registered above.
		void registerAtmosphereMedia(String const & mediaRoot)
		{
			gAtmosphereMediaAvailable = false;
			if(mediaRoot.empty())
			{
				return;
			}
			String root = mediaRoot;
			if(root.back() != '/')
			{
				root += '/';
			}
			const String atmosphereDir = root + "Atmosphere";
			if(!std::filesystem::exists(atmosphereDir))
			{
				return;	// port without the atmosphere media (older tree)
			}
			// Ogre resolves a script's `source X.metal` / shader include by the
			// BARE filename via each location's open() - a recursive location
			// indexes subdir files but does NOT open them by bare name. So the
			// script dir AND each per-language shader subdir register as their
			// own (non-recursive) location: the .material/.program parse from
			// Atmosphere/, and the shader sources resolve from Metal/GLSL/HLSL/
			// Any beside them.
			Ogre::ResourceGroupManager & resourceGroups =
				Ogre::ResourceGroupManager::getSingleton();
			char const * const subdirs[] =
				{ "", "Any", "Metal", "GLSL", "HLSL" };
			for(char const * sub : subdirs)
			{
				const String location = *sub
					? atmosphereDir + "/" + sub : atmosphereDir;
				if(std::filesystem::exists(location))
				{
					resourceGroups.addResourceLocation(location, "FileSystem",
						Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
						false /*recursive*/);
				}
			}
			gAtmosphereMediaAvailable = true;
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
#if defined(ORKIGE_IPHONE)
			// iOS: the Metal window only honours an external handle that is
			// already an OgreMetalView; SDL's UIWindow is silently ignored,
			// so pass no handle here and let the render system build its own
			// view, then splice it into the SDL UIKit window afterwards
			// (see the UIView attach below).
#elif defined(__APPLE__)
			// the SDL-hosted window (Next's Metal window embeds its own
			// OgreMetalView into the NSWindow's content view)
			windowParams["externalWindowHandle"] = options.nativeWindowHandle;
#elif defined(__ANDROID__)
			// Android: the Vulkan window attaches its swapchain surface
			// directly to the ANativeWindow* - its required misc param
			// (no external-handle or X11 route exists on this platform).
			// engine_util/SDLNativeWindowAndroid.cpp hands out that pointer.
			windowParams["ANativeWindow"] = options.nativeWindowHandle;
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

#if defined(ORKIGE_IPHONE)
		// iOS: the render system created its own OgreMetalView detached from
		// SDL's UIWindow (which the Metal window path could not adopt). Fetch
		// that view and add it into the SDL window so it becomes visible and
		// tracks the screen; the ObjC++ bridge sets frame + contentScaleFactor.
		if(!options.nativeWindowHandle.empty() &&
			options.nativeWindowHandle != "0")
		{
			void* metalView = NULL;
			window->getCustomAttribute("UIView", &metalView);
			void* uiWindow = reinterpret_cast<void*>(static_cast<uintptr_t>(
				std::strtoull(options.nativeWindowHandle.c_str(), NULL, 10)));
			orkige_ios_attach_metal_view(metalView, uiWindow);
		}
#endif

		registerHlms(options.hlmsMediaDir);
		registerAtmosphereMedia(options.hlmsMediaDir);

		// one worker thread: the engine's scenes are small during the
		// revival; tune when a real workload appears
		Ogre::SceneManager* sceneManager = root->createSceneManager(
			Ogre::ST_GENERIC, 1, "OrkigeNextWorld");
		// clustered forward light lists: without a Forward+ system this
		// backend's HlmsPbs shades only SHADOW-CASTING point/spot lights -
		// a plain dynamic lamp (RenderLight LT_POINT/LT_SPOT, no shadows)
		// never lit anything. The 16x8x24 cluster grid is the standard
		// shape; 96 lights per cell is generous headroom for the mobile
		// budget, and the 2..100 unit depth range covers the engine's
		// tens-of-units scenes. Directional lights are unaffected (they
		// ride the pass buffer either way).
		sceneManager->setForwardClustered(true, 16u, 8u, 24u, 96u, 0u, 0u,
			2.0f, 100.0f);

		RenderSystem* system = new RenderSystem();
		system->mImpl->root = root;
		system->mImpl->window = window;
		RenderWorld* world = new RenderWorld();
		world->mImpl->sceneManager = sceneManager;
		system->mImpl->world = world;
		// the next backend's render capabilities (@see RenderSystem::supports; the
		// register leg of render_facade_selfcheck asserts this fill matches
		// engine_render_next/RenderCapsExpectedNext.inc): the sky dome, dynamic
		// shadows, hemisphere ambient, the atmosphere's sun-exposure linkage,
		// animated normal-mapped water, and offscreen-owned 2D layers. Screen-space
		// refraction + IBL reflections are absent on both flavors (v1 boundaries),
		// so they stay out of the set.
		system->mImpl->caps =
			(1u << static_cast<int>(RenderCaps::SkyDome)) |
			(1u << static_cast<int>(RenderCaps::DynamicShadows)) |
			(1u << static_cast<int>(RenderCaps::HemisphereAmbient)) |
			(1u << static_cast<int>(RenderCaps::SunExposureLinkage)) |
			(1u << static_cast<int>(RenderCaps::AnimatedNormalMappedWater)) |
			(1u << static_cast<int>(RenderCaps::OffscreenOwnedLayers));
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
		// the atmosphere owns a sky Rectangle2D attached to the scene manager +
		// a material/const buffer - it must die BEFORE the root tears the scene
		// manager down (its dtor touches both)
		if(gAtmosphere)
		{
			OGRE_DELETE gAtmosphere;
			gAtmosphere = NULL;
		}
		gAtmosphereMediaAvailable = false;
		gDirectionalLights.clear();
		gLinkedSun = NULL;	// the light dies with the scene manager - no restore
		delete gRenderSystem;	// ~RenderSystem deletes the world first
		gRenderSystem = NULL;
		// same late-handle rule as classic: handles that outlive the
		// backend free facade memory only (their dtors check system())
		gNodeRegistry.clear();
		gContentDatablocks.clear();	// owned by their Hlms, die with the root
		gRetiredRTTDatablocks.clear();	// their datablocks died with the root
		gWaterAnims.clear();		// datablocks die with the root
		gWireframe = false;
		gShadowCasterCount = 0;		// late light handles no-op (system() gate)
		gRenderTargets.clear();		// their workspaces died with the root
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
	bool RenderBackend::nodeIsStatic(optr<RenderNode> const & node)
	{
		return node ? node->mImpl->isStatic : false;
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
	String RenderBackend::activeShadowNodeName()
	{
		// shadows are active only while the world knob is on AND a light
		// asked to cast - 2D/unlit scenes never allocate an atlas
		if(!gRenderSystem || gShadowCasterCount <= 0)
		{
			return String();
		}
		const ShadowPreset::Quality quality =
			gRenderSystem->getWorld()->mImpl->shadowQuality;
		if(quality == ShadowPreset::SQ_OFF)
		{
			return String();
		}
		Ogre::Root* root = gRenderSystem->mImpl->root;
		// PBS is the lit material system; a boot without Hlms templates
		// (clear-only tests) has nothing that could receive a shadow
		if(!root->getHlmsManager()->getHlms(Ogre::HLMS_PBS))
		{
			return String();
		}
		const String name = String("Orkige/ShadowNode/") +
			ShadowPreset::qualityName(quality);
		Ogre::CompositorManager2* compositorManager =
			root->getCompositorManager2();
		if(!compositorManager->hasShadowNodeDefinition(name))
		{
			// one PSSM (cascaded) shadow map set for DIRECTIONAL casters -
			// the v1 scope; budgets (splits/atlas/filter) come from the pure
			// preset table so both flavors and the unit tests read the same
			// numbers. Built once per quality step, reused by every
			// workspace rebuild (each workspace instantiates its own node).
			const ShadowPreset::Settings preset =
				ShadowPreset::forQuality(quality);
			Ogre::ShadowNodeHelper::ShadowParam param;
			std::memset(&param, 0, sizeof(param));
			// the 1-split low tier IS the single focused map (the compositor
			// requires PSSM splits in [2;4]; a 1-split PSSM is that same
			// focused map by construction - no parallel code path)
			param.technique = preset.splitCount > 1
				? Ogre::SHADOWMAP_PSSM : Ogre::SHADOWMAP_FOCUSED;
			param.numPssmSplits = static_cast<Ogre::uint8>(
				std::max(preset.splitCount, 2));
			param.atlasId = 0;
			param.addLightType(Ogre::Light::LT_DIRECTIONAL);
			for(int split = 0; split < preset.splitCount; ++split)
			{
				unsigned int offsetX = 0, offsetY = 0;
				ShadowPreset::splitAtlasOffset(preset, split, offsetX, offsetY);
				const unsigned int resolution =
					ShadowPreset::splitResolution(preset, split);
				param.atlasStart[split] =
					Ogre::ShadowNodeHelper::Resolution(offsetX, offsetY);
				param.resolution[split] =
					Ogre::ShadowNodeHelper::Resolution(resolution, resolution);
			}
			Ogre::ShadowNodeHelper::ShadowParamVec shadowParams;
			shadowParams.push_back(param);
			try
			{
				Ogre::ShadowNodeHelper::createShadowNodeWithSettings(
					compositorManager,
					root->getRenderSystem()->getCapabilities(),
					name, shadowParams,
					false /*useEsm - blur passes, mobile-hostile*/);
			}
			catch(Ogre::Exception const & e)
			{
				// degrade honestly: the scene renders shadowless
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: shadow node creation failed - "
					"rendering without shadows: " + e.getDescription());
				return String();
			}
		}
		return name;
	}
	//---------------------------------------------------------
	void RenderBackend::shadowCasterCountChanged(int delta)
	{
		const bool activeBefore = gShadowCasterCount > 0;
		gShadowCasterCount = std::max(0, gShadowCasterCount + delta);
		if(activeBefore != (gShadowCasterCount > 0))
		{
			// first caster arrived / last caster left: (de)attach the shadow
			// node by rebuilding the workspaces
			RenderBackend::applyShadowConfig();
		}
	}
	//---------------------------------------------------------
	void RenderBackend::applyShadowConfig()
	{
		if(!gRenderSystem)
		{
			return;
		}
		RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
		const ShadowPreset::Settings preset =
			ShadowPreset::forQuality(world->shadowQuality);
		if(preset.splitCount > 0)
		{
			// PSSM derives its split scheme from the scene's shadow far
			// distance; the extrusion distance bounds directional casters
			world->sceneManager->setShadowFarDistance(preset.maxDistance);
			world->sceneManager->setShadowDirectionalLightExtrusionDistance(
				preset.maxDistance);
			// the preset's PCF tap width (2x2 = the hardware-filtered floor)
			if(Ogre::Hlms* hlms = gRenderSystem->mImpl->root->getHlmsManager()
				->getHlms(Ogre::HLMS_PBS))
			{
				static_cast<Ogre::HlmsPbs*>(hlms)->setShadowSettings(
					preset.filterTaps >= 4 ? Ogre::HlmsPbs::PCF_4x4
					: preset.filterTaps >= 3 ? Ogre::HlmsPbs::PCF_3x3
					: Ogre::HlmsPbs::PCF_2x2);
			}
		}
		// scene passes reference the shadow node at BUILD time - rebuild the
		// window workspace and every live render target so they pick the
		// node up / drop it
		RenderBackend::recreateWindowWorkspace();
		for(RenderTexture* each : gRenderTargets)
		{
			each->mImpl->recreate();
		}
	}
	//---------------------------------------------------------
	String RenderBackend::shadowStateDescription()
	{
		// the whole arming state in one comparable line: which shadow node
		// the workspaces reference ("" = none) and how many lights ask to
		// cast (the tally RenderLight::setCastShadows maintains)
		std::ostringstream state;
		state << "shadowNode=" << RenderBackend::activeShadowNodeName()
			<< " casters=" << gShadowCasterCount;
		return state.str();
	}
	//---------------------------------------------------------
	void RenderBackend::registerRenderTarget(RenderTexture* target)
	{
		gRenderTargets.push_back(target);
	}
	//---------------------------------------------------------
	void RenderBackend::unregisterRenderTarget(RenderTexture* target)
	{
		gRenderTargets.erase(std::remove(gRenderTargets.begin(),
			gRenderTargets.end(), target), gRenderTargets.end());
	}
	//---------------------------------------------------------
	void RenderBackend::transitionRenderTargetsForSampling()
	{
		if(gRenderTargets.empty() || !gRenderSystem)
		{
			return;
		}
		Ogre::RenderSystem* renderSystem =
			gRenderSystem->mImpl->root->getRenderSystem();
		Ogre::BarrierSolver & solver = renderSystem->getBarrierSolver();
		Ogre::ResourceTransitionArray & barriers =
			solver.getNewResourceTransitionsArrayTmp();
		for(RenderTexture* each : gRenderTargets)
		{
			if(!each->mImpl->texture)
			{
				continue;
			}
			// no-op unless the target actually sits in another layout (the
			// solver compares against what it last recorded)
			solver.resolveTransition(barriers, each->mImpl->texture,
				Ogre::ResourceLayout::Texture, Ogre::ResourceAccess::Read,
				Ogre::c_allGraphicStagesMask);
		}
		// empty (the steady state) = a cheap early-out inside the render system
		renderSystem->executeResourceTransition(barriers);
	}
	//---------------------------------------------------------
	Ogre::Light* RenderBackend::firstDirectionalLight()
	{
		return gDirectionalLights.empty() ? NULL : gDirectionalLights.front();
	}
	//---------------------------------------------------------
	bool RenderBackend::noteAuthoredSunColour(Ogre::Light* light,
		Ogre::ColourValue const & colour, bool specular)
	{
		if(!light || light != gLinkedSun)
		{
			return false;	// not driven - the caller writes the live light
		}
		// the atmosphere owns the live colour; record the authored value so
		// disabling restores the LATEST one (restore-exactly)
		if(specular)
		{
			gLinkedSunSpecular = colour;
		}
		else
		{
			gLinkedSunDiffuse = colour;
		}
		return true;
	}
	//---------------------------------------------------------
	void RenderBackend::noteDirectionalLight(Ogre::Light* light,
		bool isDirectional)
	{
		if(!light)
		{
			return;
		}
		const auto found = std::find(gDirectionalLights.begin(),
			gDirectionalLights.end(), light);
		const bool present = found != gDirectionalLights.end();
		if(isDirectional && !present)
		{
			gDirectionalLights.push_back(light);
		}
		else if(!isDirectional && present)
		{
			gDirectionalLights.erase(found);
		}
		else
		{
			return;	// no membership change
		}
		// the sun set changed: while the atmosphere is live, re-resolve it to
		// the new first directional light (drops a dangling pointer when the
		// linked sun leaves/dies, or promotes a freshly-authored sun)
		if(gAtmosphere && gRenderSystem)
		{
			RenderBackend::applyAtmosphere(
				gRenderSystem->getWorld()->mImpl->atmosphere);
		}
	}
	//---------------------------------------------------------
	void RenderBackend::applyAtmosphere(AtmosphereDesc const & desc)
	{
		if(!gRenderSystem)
		{
			return;
		}
		Ogre::SceneManager* sceneManager = RenderBackend::worldSceneManager();
		// the flat window clear colour tracks the sky tint on BOTH flavors, so
		// the window edges / a media-less boot / a disabled atmosphere still
		// read as sky (the classic subset is entirely this path)
		gRenderSystem->setWindowBackgroundColour(
			Color(desc.skyRed, desc.skyGreen, desc.skyBlue));

		if(!desc.enabled)
		{
			// tear the sky + object fog down again (revert to plain clear);
			// UNLINK the sun before restoring it - setSky runs one last light
			// sync, which would stomp the restored colours otherwise
			if(gAtmosphere)
			{
				gAtmosphere->setLight(NULL);
				gAtmosphere->setSky(sceneManager, false);
				OGRE_DELETE gAtmosphere;
				gAtmosphere = NULL;
			}
			// the linked sun returns EXACTLY to its authored colour/power
			restoreLinkedSun();
			return;
		}

		if(!gAtmosphereMediaAvailable)
		{
			// enabled but the sky material never registered (headless/older
			// media): honest no-op beyond the flat sky colour above
			RenderBackend::notImplementedOnce(
				"sky/fog atmosphere (sky material media not registered)");
			return;
		}

		if(!gAtmosphere)
		{
			Ogre::VaoManager* vaoManager =
				sceneManager->getDestinationRenderSystem()->getVaoManager();
			try
			{
				gAtmosphere = OGRE_NEW Ogre::AtmosphereNpr(vaoManager);
			}
			catch(Ogre::Exception const & e)
			{
				// the sky material failed to load (e.g. resource groups not
				// initialised yet): degrade honestly, keep the flat sky colour
				gAtmosphereMediaAvailable = false;
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: atmosphere sky unavailable - "
					"rendering flat sky colour + no object fog: " +
					e.getDescription());
				return;
			}
			gAtmosphere->setSky(sceneManager, true);
			// setSky attached the sky as a Rectangle2D in a LATE render queue
			// (drawn after most content upstream) - past this backend's scene
			// passes, and it would overdraw non-depth-writing 3D alpha content
			// (sprites/particles) where only sky is behind them. Move it to
			// the skies-early queue instead (@see kSkyRenderQueue); identified
			// by its cloned "Ogre/Atmo/NprSky*" material - the atmosphere does
			// not expose its quad.
			Ogre::SceneManager::MovableObjectIterator rectangles =
				sceneManager->getMovableObjectIterator(
					Ogre::Rectangle2DFactory::FACTORY_TYPE_NAME);
			while(rectangles.hasMoreElements())
			{
				Ogre::MovableObject* movable = rectangles.getNext();
				Ogre::Rectangle2D* rectangle =
					static_cast<Ogre::Rectangle2D*>(movable);
				Ogre::MaterialPtr material = std::static_pointer_cast<
					Ogre::Material>(rectangle->getMaterial());
				if(material &&
					material->getName().rfind("Ogre/Atmo/NprSky", 0) == 0)
				{
					rectangle->setRenderQueueGroup(kSkyRenderQueue);
				}
			}
		}

		// SUN LINKAGE: the first directional light is the sun; read its current
		// direction (authored via its node) so orienting the light sweeps the
		// day-night arc, then the atmosphere drives that light's colour/power.
		// Read the direction and snapshot the light's node BEFORE any
		// atmosphere call: setLight/setPreset/setSunDir each run
		// AtmosphereNpr::syncToLight, which steers the node.
		Ogre::Light* sun = RenderBackend::firstDirectionalLight();
		Ogre::Vector3 toSun(0.3f, 0.9f, 0.2f);	// default: high daytime sun
		Ogre::Node* sunNode = NULL;
		Ogre::Quaternion sunNodeOrientation = Ogre::Quaternion::IDENTITY;
		if(sun)
		{
			// -direction points FROM the surface TOWARD the sun
			toSun = -sun->getDerivedDirectionUpdated();
			sunNode = sun->getParentNode();
			if(sunNode)
			{
				sunNodeOrientation = sunNode->getOrientation();
			}
		}
		toSun.normalise();
		// restore-exactly: a sun-set change hands the PREVIOUS sun its
		// authored colour/power back (it is still alive here - the registry
		// is updated before a dying light is destroyed) and snapshots the new
		// one before the atmosphere starts driving it
		if(gLinkedSun != sun)
		{
			restoreLinkedSun();
			if(sun)
			{
				gLinkedSun = sun;
				gLinkedSunDiffuse = sun->getDiffuseColour();
				gLinkedSunSpecular = sun->getSpecularColour();
				gLinkedSunPower = sun->getPowerScale();
			}
		}
		gAtmosphere->setLight(sun);
		// the native day/night phase from the sun's elevation: sunHeight in the
		// shader is sin(normTime * PI), so normTime = asin(elevation)/PI maps
		// overhead(+1)->0.5 (noon), horizon(0)->0 and below(-1)->-0.5 (night)
		const float elevation =
			std::max(-1.0f, std::min(1.0f, static_cast<float>(toSun.y)));
		const float normTime = std::asin(elevation) /
			static_cast<float>(Ogre::Math::PI);

		Ogre::AtmosphereNpr::Preset preset;	// starts from the sane midday defaults
		preset.skyColour =
			Ogre::Vector3(desc.skyRed, desc.skyGreen, desc.skyBlue);
		preset.skyPower = desc.skyPower;
		preset.densityCoeff = desc.density;
		preset.fogDensity = desc.fogDensity;
		// EXPOSURE: this pipeline has no tonemapper, so the native sun/ambient
		// powers (linkedLightPower = PI, linkedSceneAmbient*Power = 0.1/0.01 PI)
		// clip lit surfaces to white. Drive them from the desc's un-tonemapped
		// exposure knobs instead (AtmosphereDesc::sunPower/ambientPower carry the
		// safe defaults); the ambient multiplier scales the native hemisphere
		// fill so it stays proportioned to the sun.
		preset.linkedLightPower = desc.sunPower;
		preset.linkedSceneAmbientUpperPower =
			0.1f * static_cast<float>(Ogre::Math::PI) * desc.ambientPower;
		preset.linkedSceneAmbientLowerPower =
			0.01f * static_cast<float>(Ogre::Math::PI) * desc.ambientPower;
		gAtmosphere->setPreset(preset);			// preset first (syncToLight reads it)
		// CONVENTION: AtmosphereNpr's Vector3 setSunDir takes the LIGHT-TRAVEL
		// direction (sun -> surface, what Light::setDirection holds) - it
		// negates it into its toward-the-sun mSunDir and syncToLight writes
		// the linked light's direction back as -mSunDir. Passing the
		// toward-the-sun vector here would sample the sun colour BELOW the
		// horizon (night-blue daylight) and flip the light's direction on
		// every call.
		gAtmosphere->setSunDir(-toSun, normTime);	// then place the sun
		if(sunNode)
		{
			// syncToLight steered the light's node (Light::setDirection
			// writes the node in PARENT space, so repeated syncs COMPOUND
			// with a transform-driven parent). The transform is the
			// direction's source of truth here - restore the node, the
			// atmosphere keeps only the light's colour/power
			sunNode->setOrientation(sunNodeOrientation);
		}
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
		// instead of createBasicWorkspaceDef:
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
			// dynamic shadows: while active (world knob on + a casting
			// light) the scene pass renders with the PSSM shadow node
			const String shadowNode = RenderBackend::activeShadowNodeName();
			if(!shadowNode.empty())
			{
				scenePass->mShadowNode = Ogre::IdString(shadowNode);
			}
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
			// draw ONLY the window's 2D batches: offscreen preview targets
			// put their batches in the same queue under a different visibility
			// bit, and this mask keeps them out of the window (and vice versa)
			uiPass->setVisibilityMask(RenderBackend::UI_WINDOW_VISIBILITY);
		}
		Ogre::CompositorWorkspaceDef* workspaceDefinition =
			compositorManager->addWorkspaceDefinition(definitionName);
		workspaceDefinition->connectExternal(0, definitionName + "/Node", 0);
		impl->workspace = compositorManager->addWorkspace(
			gRenderSystem->getWorld()->mImpl->sceneManager,
			impl->window->getTexture(),
			backendCamera ? backendCamera : uiCamera, definitionName,
			true /*enabled*/);
		// the window is the surface that SAMPLES the offscreen targets (a 2D
		// batch binds a RenderTexture): hand its passes the resource-layout
		// barrier the compositor cannot derive (@see RenderTargetSampleBarrier)
		impl->workspace->addListener(&gRenderTargetSampleBarrier);
		if(backendCamera && impl->window->getHeight() > 0)
		{
			backendCamera->setAspectRatio(
				Ogre::Real(impl->window->getWidth()) /
				Ogre::Real(impl->window->getHeight()));
		}
	}
	//---------------------------------------------------------
	String RenderBackend::resolveTextureResourceName(
		String const & textureName)
	{
		Ogre::ResourceGroupManager & resourceGroups =
			Ogre::ResourceGroupManager::getSingleton();
		if(textureName.empty() ||
			resourceGroups.resourceExistsInAnyGroup(textureName))
		{
			return textureName;	// the raw name wins (the dev-loop path)
		}
		const String::size_type dot = textureName.find_last_of('.');
		if(dot == String::npos)
		{
			return textureName;
		}
		// the containers the export cook emits for THIS flavor: BCn rides
		// .dds, ASTC/ETC2 ride the native .oitd (@see Util/cook_textures.py)
		for(const char* extension : { ".dds", ".oitd" })
		{
			const String candidate = textureName.substr(0, dot) + extension;
			if(resourceGroups.resourceExistsInAnyGroup(candidate))
			{
				return candidate;
			}
		}
		return textureName;
	}
	//---------------------------------------------------------
	Ogre::TextureGpu* RenderBackend::loadTexture2D(String const & rawName)
	{
		oAssert(gRenderSystem);
		Ogre::TextureGpuManager* textureManager = gRenderSystem->mImpl->root
			->getRenderSystem()->getTextureGpuManager();
		// backend-object textures first (createTexture2DFromPixels uploads -
		// e.g. an ImGui font atlas - have no resource-group entry)
		if(Ogre::TextureGpu* existing =
			textureManager->findTextureNoThrow(rawName))
		{
			return existing;
		}
		// cooked-payload fallback (foo.png -> foo.dds/.oitd in exports)
		const String textureName = resolveTextureResourceName(rawName);
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
			// Decode-PROBE on this (main) thread before handing the file to the
			// texture manager's ASYNC loader. An undecodable file (a non-image
			// asset given a texture name) makes the worker's error-recovery path
			// corrupt the heap and abort the process - unreachable and
			// uncatchable from here (the worker's codec throw itself is handled,
			// but the recovery that follows is not). A clean main-thread codec
			// failure throws here and degrades to the NULL + log contract. The
			// async path below then only ever sees a decodable file.
			{
				Ogre::DataStreamPtr probe =
					resourceGroups.openResource(textureName, group);
				Ogre::Image2 probeImage;
				probeImage.load2(probe, textureName);
			}
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
		// replace-by-recreate (atlas rebuilds): drop any existing incarnation -
		// which frees the NAME, see destroyTexture2DByName - then re-point the
		// 2D-layer datablock below
		RenderBackend::destroyTexture2DByName(name);
		Ogre::TextureGpu* texture = NULL;
		try
		{
			texture = textureManager->createTexture(name,
				Ogre::GpuPageOutStrategy::Discard,
				Ogre::TextureFlags::AutomaticBatching,
				Ogre::TextureTypes::Type2D);
		}
		catch(Ogre::Exception const & e)
		{
			// the name is somehow still taken: degrade honestly (the caller
			// keeps its pixels and can retry) instead of taking the app down
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: createTexture2DFromPixels('" + name +
				"') refused: " + e.getDescription());
			return NULL;
		}
		// hand a SIMD-allocated copy to Image2 (it owns + frees it)
		const size_t sizeBytes = Ogre::PixelFormatGpuUtils::getSizeBytes(
			width, height, 1u, 1u, Ogre::PFG_RGBA8_UNORM, 4u);
		void* pixelCopy = OGRE_MALLOC_SIMD(sizeBytes, Ogre::MEMCATEGORY_RESOURCE);
		memcpy(pixelCopy, rgbaPixels, size_t(width) * size_t(height) * 4u);
		Ogre::Image2* image = OGRE_NEW Ogre::Image2();
		image->loadDynamicImage(pixelCopy, width, height, 1u,
			Ogre::TextureTypes::Type2D, Ogre::PFG_RGBA8_UNORM,
			true /*autoDelete*/, 1u);
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
		// THE NAME MUST BE FREE WHEN WE RETURN. The texture manager DEFERS a
		// destroy while the texture's own upload is still in flight: the entry
		// then LINGERS under its name (merely flagged destroy-requested) until
		// the streaming queue catches up - invisible to a lookup, but a create
		// under the same name meanwhile fails as a duplicate. That is exactly
		// what a create-then-replace does (a runtime font atlas re-uploading
		// its page after baking a glyph on demand, before a single frame has
		// been rendered). Wait for the pixels to land so the destroy runs
		// immediately; whether the wait is needed at all is timing- and
		// platform-dependent, so it must not be left to luck.
		if(!existing->isDataReady())
		{
			existing->waitForData();
		}
		textureManager->destroyTexture(existing);
	}
	//---------------------------------------------------------
	Ogre::HlmsDatablock* RenderBackend::getOrCreateSpriteDatablock(
		String const & textureName, Ogre::TextureGpu* texture,
		SpriteQuad::FilterMode filter, SpriteQuad::AddressMode addressing)
	{
		oAssert(gRenderSystem);
		Ogre::HlmsManager* hlmsManager =
			gRenderSystem->mImpl->root->getHlmsManager();
		// the sampler is baked into the name (SpriteQuad::samplerName) so two
		// sprites of one texture but different sampling get DISTINCT datablocks
		// instead of stomping each other's filter/addressing
		const String name =
			SpriteQuad::samplerName(textureName, filter, addressing);
		if(Ogre::HlmsDatablock* existing =
			hlmsManager->getDatablockNoDefault(name))
		{
			return existing;
		}
		// the honest sprite rules carried over from classic: unlit,
		// alpha-blended, depth-checked/not-written, two-sided; tint and
		// flips live in the quad's vertex data
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
			// same DrawLayer2D recipe, generalized to a runtime choice: point
			// (TFO_NONE) vs bilinear (TFO_BILINEAR), clamp vs wrap addressing
			Ogre::HlmsSamplerblock samplerblock;
			samplerblock.setFiltering(
				(filter == SpriteQuad::FILTER_POINT)
				? Ogre::TFO_NONE : Ogre::TFO_BILINEAR);
			samplerblock.setAddressingMode(
				(addressing == SpriteQuad::ADDRESS_WRAP)
				? Ogre::TAM_WRAP : Ogre::TAM_CLAMP);
			datablock->setTexture(0u, texture, &samplerblock);
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
	Ogre::HlmsDatablock* RenderBackend::createOrUpdatePbsDatablock(
		String const & name, RenderMaterialDesc const & desc, bool & outComplete)
	{
		oAssert(gRenderSystem);
		oAssert(!name.empty());
		outComplete = true;
		Ogre::HlmsManager* hlmsManager =
			gRenderSystem->mImpl->root->getHlmsManager();
		Ogre::HlmsPbsDatablock* datablock = NULL;
		if(Ogre::HlmsDatablock* existing =
			hlmsManager->getDatablockNoDefault(name))
		{
			// update-in-place is only safe within the PBS family - refuse to
			// stomp a generated sprite/unlit/2D-layer datablock of that name
			if(!existing->getCreator() ||
				existing->getCreator()->getType() != Ogre::HLMS_PBS)
			{
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: material '" + name +
					"' collides with an existing non-PBS datablock - refused");
				outComplete = false;
				return NULL;
			}
			datablock = static_cast<Ogre::HlmsPbsDatablock*>(existing);
		}
		else
		{
			Ogre::HlmsPbs* pbs = static_cast<Ogre::HlmsPbs*>(
				hlmsManager->getHlms(Ogre::HLMS_PBS));
			datablock = static_cast<Ogre::HlmsPbsDatablock*>(pbs->createDatablock(
				name, name, Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(),
				Ogre::HlmsParamVec()));
			RenderBackend::registerContentDatablock(datablock);
		}

		// metallic workflow: metalness/roughness are native scalars - exactly
		// the RenderMaterialDesc vocabulary (the specular-colour slot of the
		// other workflows stays out of the facade)
		datablock->setWorkflow(Ogre::HlmsPbsDatablock::MetallicWorkflow);
		datablock->setDiffuse(Ogre::Vector3(desc.albedo.r, desc.albedo.g,
			desc.albedo.b));
		datablock->setMetalness(std::clamp(desc.metalness, 0.0f, 1.0f));
		// the Hlms floors roughness itself (a hard 0 breaks the BRDF); mirror
		// the floor here so the update path never trips its warning
		datablock->setRoughness(std::max(desc.roughness, 0.02f));
		datablock->setEmissive(Ogre::Vector3(desc.emissive.r, desc.emissive.g,
			desc.emissive.b));
		// CUTOUT: the Hlms alpha test discards when (threshold CMP alpha)
		// holds, so keeping alpha >= threshold is CMPF_GREATER - and the
		// caster shader carries the test + the diffuse texture natively
		// (a cutout caster shadows as a cutout, no extra material). The
		// update path must be able to turn it off (CMPF_ALWAYS_PASS).
		datablock->setAlphaTest(desc.alphaTest > 0.0f
			? Ogre::CMPF_GREATER : Ogre::CMPF_ALWAYS_PASS);
		datablock->setAlphaTestThreshold(
			std::clamp(desc.alphaTest, 0.0f, 1.0f));
		// TWO-SIDED: the macroblock owns the cull mode - set it EXPLICITLY
		// both ways (setTwoSidedLighting(false) leaves a stale CULL_NONE
		// macroblock behind), and keep the caster two-sided as well so a
		// foliage plane casts from both sides; the lighting normal flips
		// through the datablock's two-sided flag
		{
			Ogre::HlmsMacroblock macroblock;
			macroblock.mCullMode = desc.twoSided
				? Ogre::CULL_NONE : Ogre::CULL_CLOCKWISE;
			datablock->setMacroblock(macroblock);
			datablock->setMacroblock(macroblock, true /*caster*/);
			datablock->setTwoSidedLighting(desc.twoSided,
				false /*changeMacroblock*/);
		}

		// textures: a set name binds the map, an empty one detaches it (the
		// update path must be able to REMOVE a map). Albedo/emissive ride
		// loadTexture2D (raw texels, AutomaticBatching, mipmaps - the
		// gamma-space colour-parity convention of every content texture);
		// the normal map goes through the PBS slot's string setter, whose
		// suggested filters run the normal-map preparation (no sRGB there).
		Ogre::ResourceGroupManager & resourceGroups =
			Ogre::ResourceGroupManager::getSingleton();
		struct MapBinding
		{
			String const &			textureName;
			Ogre::PbsTextureTypes	slot;
			bool					viaLoadTexture2D;
		};
		const MapBinding bindings[] = {
			{ desc.albedoTexture,	Ogre::PBSM_DIFFUSE,		true },
			{ desc.emissiveTexture,	Ogre::PBSM_EMISSIVE,	true },
			{ desc.normalTexture,	Ogre::PBSM_NORMAL,		false },
		};
		for(MapBinding const & binding : bindings)
		{
			if(binding.textureName.empty())
			{
				datablock->setTexture(binding.slot,
					static_cast<Ogre::TextureGpu*>(NULL));
				continue;
			}
			// cooked-payload fallback (foo.png -> foo.dds/.oitd in exports)
			const String resolvedName =
				RenderBackend::resolveTextureResourceName(binding.textureName);
			if(!resourceGroups.resourceExistsInAnyGroup(resolvedName))
			{
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: material '" + name + "' texture '" +
					binding.textureName + "' not found - map skipped");
				datablock->setTexture(binding.slot,
					static_cast<Ogre::TextureGpu*>(NULL));
				outComplete = false;
				continue;
			}
			if(binding.viaLoadTexture2D)
			{
				Ogre::TextureGpu* texture =
					RenderBackend::loadTexture2D(resolvedName);
				datablock->setTexture(binding.slot, texture);
				outComplete = outComplete && texture != NULL;
			}
			else
			{
				datablock->setTexture(binding.slot, resolvedName);
			}
		}
		return datablock;
	}
	//---------------------------------------------------------
	Ogre::HlmsDatablock* RenderBackend::createOrUpdateWaterDatablock(
		String const & name, RenderWaterDesc const & desc, bool & outComplete)
	{
		oAssert(gRenderSystem);
		oAssert(!name.empty());
		outComplete = true;
		Ogre::HlmsManager* hlmsManager =
			gRenderSystem->mImpl->root->getHlmsManager();
		Ogre::HlmsPbsDatablock* datablock = NULL;
		if(Ogre::HlmsDatablock* existing =
			hlmsManager->getDatablockNoDefault(name))
		{
			// update-in-place stays within the PBS family (the surface-material
			// guard - never stomp a generated sprite/unlit/2D-layer block)
			if(!existing->getCreator() ||
				existing->getCreator()->getType() != Ogre::HLMS_PBS)
			{
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: water material '" + name +
					"' collides with an existing non-PBS datablock - refused");
				outComplete = false;
				return NULL;
			}
			datablock = static_cast<Ogre::HlmsPbsDatablock*>(existing);
		}
		else
		{
			Ogre::HlmsPbs* pbs = static_cast<Ogre::HlmsPbs*>(
				hlmsManager->getHlms(Ogre::HLMS_PBS));
			datablock = static_cast<Ogre::HlmsPbsDatablock*>(pbs->createDatablock(
				name, name, Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(),
				Ogre::HlmsParamVec()));
			RenderBackend::registerContentDatablock(datablock);
		}

		// water is a dielectric: the specular-as-fresnel workflow lets us set
		// the fresnel (F0) directly (the metallic workflow derives it from
		// metalness and forbids setFresnel). The deep colour is the water-body
		// albedo (the "background diffuse" the detail normals ripple over), a
		// low roughness keeps the surface glossy so the sun/sky read as a
		// highlight, and the shallow colour rides as a subtle scatter emissive -
		// an honest stand-in for depth-graded transmission (the true depth
		// gradient waits on the refraction/depth pass; @see RenderWaterDesc).
		datablock->setWorkflow(Ogre::HlmsPbsDatablock::SpecularAsFresnelWorkflow);
		// water is a per-instance material, so the surface's receive flag maps
		// 1:1 (@see RenderWaterDesc::receiveShadows); water never casts - the
		// component turns its plane's caster flag off
		datablock->setReceiveShadows(desc.receiveShadows);
		// the deep colour IS the diffuse: the background-diffuse slot only
		// shows through a diffuse map's alpha and is inert without one, so a
		// water body coloured there renders plain white (this surface has no
		// diffuse map - the detail normals only perturb shading)
		datablock->setDiffuse(Ogre::Vector3(
			desc.deepColour.r, desc.deepColour.g, desc.deepColour.b));
		// the specular balance: a fairly tight lobe + a cool-tinted specular
		// colour make the sun read as LIVELY glints riding the ripple instead
		// of a diffuse sheen. Grazing-angle honesty still bounds both knobs:
		// Schlick fresnel reaches 1 at the horizon no matter how small F0 is,
		// and with no reflection source (no IBL yet) a mirror-tight lobe over
		// a white specular colour would render the whole far plane as a white
		// sheet - the roughness floor and the sub-unit specular colour are
		// what keep the distance reading as water
		datablock->setRoughness(0.24f);
		datablock->setSpecular(Ogre::Vector3(0.45f, 0.47f, 0.5f));
		const float scatter = 0.18f;
		datablock->setEmissive(Ogre::Vector3(desc.shallowColour.r * scatter,
			desc.shallowColour.g * scatter, desc.shallowColour.b * scatter));
		// fresnel (F0): water sits near 0.02; the knob scales the edge
		// reflectivity up from there (clamped to a plausible band)
		const float f0 = std::clamp(0.02f * std::max(desc.fresnelPower, 0.0f),
			0.0f, 0.2f);
		datablock->setFresnel(Ogre::Vector3(f0, f0, f0), false);
		// realistic transparency that preserves the fresnel edge reflection
		datablock->setTransparency(std::clamp(desc.opacity, 0.0f, 1.0f),
			Ogre::HlmsPbsDatablock::Transparent);

		// TWO detail normal maps carry the ripple: same tiling water normal,
		// bound to both detail slots and scrolled in different directions/
		// speeds by setWaterDatablockTime so their interference reads as moving
		// water. The detail normals go through the slot's own string setter
		// (its suggested filters run the normal-map preparation).
		const Ogre::PbsTextureTypes detailSlots[2] =
			{ Ogre::PBSM_DETAIL0_NM, Ogre::PBSM_DETAIL1_NM };
		const float detailScales[2] = { desc.waveScale, desc.waveScale * 1.7f };
		const float detailWeights[2] = { 1.0f, 0.6f };
		// cooked-payload fallback (foo.png -> foo.dds/.oitd in exports)
		const String resolvedNormal =
			RenderBackend::resolveTextureResourceName(desc.normalTexture);
		if(desc.normalTexture.empty())
		{
			for(int slot = 0; slot < 2; ++slot)
			{
				datablock->setTexture(detailSlots[slot],
					static_cast<Ogre::TextureGpu*>(NULL));
			}
		}
		else if(!Ogre::ResourceGroupManager::getSingleton()
			.resourceExistsInAnyGroup(resolvedNormal))
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: water material '" + name +
				"' normal map '" + desc.normalTexture +
				"' not found - the surface renders flat");
			for(int slot = 0; slot < 2; ++slot)
			{
				datablock->setTexture(detailSlots[slot],
					static_cast<Ogre::TextureGpu*>(NULL));
			}
			outComplete = false;
		}
		else
		{
			for(int slot = 0; slot < 2; ++slot)
			{
				datablock->setTexture(detailSlots[slot], resolvedNormal);
				datablock->setDetailNormalWeight(static_cast<Ogre::uint8>(slot),
					detailWeights[slot]);
				datablock->setDetailMapOffsetScale(
					static_cast<Ogre::uint8>(slot),
					Ogre::Vector4(0.0f, 0.0f, detailScales[slot],
						detailScales[slot]));
			}
		}

		// remember the wave tunables for the per-frame scroll
		gWaterAnims[name] = WaterAnim{ desc.waveScale, desc.waveSpeed };
		return datablock;
	}
	//---------------------------------------------------------
	void RenderBackend::setWaterDatablockTime(String const & name, float seconds)
	{
		std::unordered_map<String, WaterAnim>::const_iterator it =
			gWaterAnims.find(name);
		if(it == gWaterAnims.end())
		{
			return;	// no water material by that name - silent no-op (dormancy)
		}
		oAssert(gRenderSystem);
		Ogre::HlmsManager* hlmsManager =
			gRenderSystem->mImpl->root->getHlmsManager();
		Ogre::HlmsDatablock* existing = hlmsManager->getDatablockNoDefault(name);
		if(!existing || !existing->getCreator() ||
			existing->getCreator()->getType() != Ogre::HLMS_PBS)
		{
			return;	// datablock gone (project switch) - harmless
		}
		Ogre::HlmsPbsDatablock* datablock =
			static_cast<Ogre::HlmsPbsDatablock*>(existing);
		const WaterAnim & anim = it->second;
		const float travel = seconds * anim.waveSpeed;
		// two fixed, non-parallel scroll directions; the second drifts slower
		// so the interference pattern never locks into a repeating march
		const Ogre::Vector2 dir0(1.0f, 0.35f);
		const Ogre::Vector2 dir1(-0.4f, 0.9f);
		const float scale0 = anim.waveScale;
		const float scale1 = anim.waveScale * 1.7f;
		datablock->setDetailMapOffsetScale(0u, Ogre::Vector4(
			dir0.x * travel, dir0.y * travel, scale0, scale0));
		datablock->setDetailMapOffsetScale(1u, Ogre::Vector4(
			dir1.x * travel * 0.85f, dir1.y * travel * 0.85f, scale1, scale1));
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
	void RenderBackend::retireRTTDatablock(String const & name)
	{
		if(!name.empty())
		{
			gRetiredRTTDatablocks.insert(name);
		}
	}
	//---------------------------------------------------------
	void RenderBackend::flushRetiredRTTDatablocks()
	{
		if(gRetiredRTTDatablocks.empty() || !gRenderSystem)
		{
			return;
		}
		Ogre::HlmsManager* hlmsManager =
			RenderBackend::ogreRoot()->getHlmsManager();
		for(auto it = gRetiredRTTDatablocks.begin();
			it != gRetiredRTTDatablocks.end();)
		{
			Ogre::HlmsDatablock* datablock =
				hlmsManager->getDatablockNoDefault(*it);
			if(!datablock)
			{
				it = gRetiredRTTDatablocks.erase(it);	// already gone
				continue;
			}
			// a datablock still drawn by a batch cannot be destroyed
			// (~HlmsDatablock asserts on linked renderables) - wait a frame
			if(!datablock->getLinkedRenderables().empty())
			{
				++it;
				continue;
			}
			gContentDatablocks.erase(std::remove(gContentDatablocks.begin(),
				gContentDatablocks.end(), datablock), gContentDatablocks.end());
			datablock->getCreator()->destroyDatablock(datablock->getName());
			it = gRetiredRTTDatablocks.erase(it);
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
			"' is not implemented on the next backend yet (see "
			"Docs/render-abstraction.md) - returning a safe default";
		if(Ogre::LogManager::getSingletonPtr())
		{
			Ogre::LogManager::getSingleton().logMessage(message);
		}
		std::fprintf(stderr, "%s\n", message.c_str());
	}
}
