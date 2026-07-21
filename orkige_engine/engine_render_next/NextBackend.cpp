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
#include <core_util/SkyEnvMap.h>

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
#include <Compositor/Pass/PassQuad/OgreCompositorPassQuadDef.h>	// bloom quad passes
#include <OgreMaterialManager.h>	// bloom material param push
#include <OgreTechnique.h>
#include <OgrePass.h>
#include <OgreGpuProgramParams.h>
#include <OgreMovableObject.h>		// scene default visibility (bloom 2D split)

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
		//! did the four bloom materials (bright/blur-h/blur-v/combine) resolve at
		//! first use? (false on a media-less/headless boot - setBloom then
		//! degrades to no pass, byte-identical). Checked once (gBloomChecked).
		bool gBloomMaterialsAvailable = false;
		bool gBloomChecked = false;
		//! the one live sky/fog atmosphere (RenderWorld::setAtmosphere), NULL
		//! while disabled; owned here, destroyed before the root teardown
		Ogre::AtmosphereNpr* gAtmosphere = NULL;
		//! did the atmosphere sky material media register at boot? (false on a
		//! media-less/headless boot - setAtmosphere then degrades honestly)
		bool gAtmosphereMediaAvailable = false;
		//! is the AtmosphereNpr's procedural sky quad currently shown? (the
		//! sky VISUAL follows AtmosphereDesc::skyType; fog + sun linkage stay
		//! with gAtmosphere on every type)
		bool gAtmosphereSkyVisible = false;
		//! the cubemap the native SceneManager sky quad currently shows
		//! ("" = none), so per-frame atmosphere re-applies skip the rebuild
		String gSkyboxTexture;
		//! the cubemap name last warned about (missing/unloadable/not a
		//! cubemap), so the honest degrade logs ONCE per name
		String gSkyboxWarnedTexture;
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

		//--- image-based lighting (skybox-sourced) - IBL block ------------
		//! is image lighting realized on the generated PBS datablocks right
		//! now? (the facade opt-in AND the quality knob AND a loaded skybox
		//! cubemap - @see RenderBackend::applyImageLighting)
		bool gIblActive = false;
		//! the environment chain the PBS datablocks bind while active: the
		//! skybox cubemap itself when it fits the tier cap, else the derived
		//! tier-capped copy (leading mips dropped)
		Ogre::TextureGpu* gIblTexture = NULL;
		//! true while gIblTexture is the derived copy this backend created
		//! (the name below); false while it aliases the skybox texture
		bool gIblTextureOwned = false;
		//! the one derived-chain texture name (recreated on source/tier change)
		const char* const kIblChainTexture = "Orkige/IblChain";
		//! which (skybox, tier) pair the bound chain was built from, so
		//! repeated applies with unchanged inputs skip the rebuild
		String gIblChainSource;
		IblPreset::Quality gIblChainQuality = IblPreset::IQ_OFF;
		//! the reason last warned about (the honest one-line degrade of an
		//! opt-in without a usable skybox source), so it logs ONCE per state
		String gIblWarnedReason;
		//! the synthetic source identity of a runtime-captured procedural-sky
		//! environment (@see gSkyboxTexture for the authored-skybox source);
		//! the two sources share the ONE downstream consumer below
		const char* const kProceduralSource = "<procedural-sky>";
		//! the atmosphere/sun inputs the bound procedural capture was built
		//! from - a fresh capture happens only when they move materially
		//! (@see SkyEnvMap::materiallyDiffers), never per frame
		SkyEnvMap::CaptureKey gProceduralIblKey;
		bool gProceduralIblHasKey = false;
		//! the max sun swing (as a cosine) tolerated before a recapture - a
		//! coarse cadence so a day/night arc recaptures a handful of times, not
		//! every frame (~6 degrees; the capture is cheap but not free)
		const float kSunMoveCosThreshold = 0.9945f;	// cos(6 degrees)
		//--- end IBL block ------------------------------------------------

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
		// ride the pass buffer either way). The grid dimensions come from
		// the shared RenderBackend constants so RenderSystem::lightBudget
		// derives the many-lights ceiling from the SAME lightsPerCell bound.
		sceneManager->setForwardClustered(true,
			RenderBackend::FORWARD_CLUSTERED_WIDTH,
			RenderBackend::FORWARD_CLUSTERED_HEIGHT,
			RenderBackend::FORWARD_CLUSTERED_SLICES,
			RenderBackend::FORWARD_CLUSTERED_LIGHTS_PER_CELL,
			RenderBackend::FORWARD_CLUSTERED_DECALS_PER_CELL, 0u,
			2.0f, 100.0f);

		// clear the 2D-tier visibility bit from the process default so all 3D
		// content is created without it (only tagScene2D sets it) - the bloom
		// scene split relies on 3D and 2D content carrying disjoint bits. Inert
		// while bloom is off (the scene pass masks nothing), so byte-stable.
		RenderBackend::setSceneDefaultVisibility();

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
		// animated normal-mapped water, offscreen-owned 2D layers, and the
		// skybox-sourced image-based lighting (the native HlmsPbs reflection
		// map + diffuse-GI env feature - @see applyImageLighting). Screen-space
		// refraction is absent on both flavors (a v1 boundary), so it stays
		// out of the set.
		system->mImpl->caps =
			(1u << static_cast<int>(RenderCaps::SkyDome)) |
			(1u << static_cast<int>(RenderCaps::DynamicShadows)) |
			(1u << static_cast<int>(RenderCaps::HemisphereAmbient)) |
			(1u << static_cast<int>(RenderCaps::SunExposureLinkage)) |
			(1u << static_cast<int>(RenderCaps::AnimatedNormalMappedWater)) |
			(1u << static_cast<int>(RenderCaps::OffscreenOwnedLayers)) |
			(1u << static_cast<int>(RenderCaps::ProjectedDecals)) |
			(1u << static_cast<int>(RenderCaps::IblReflections)) |
			(1u << static_cast<int>(RenderCaps::Bloom));
		// the sane concurrent dynamic-light ceiling (@see RenderSystem::
		// lightBudget), derived from the clustered-forward config set above
		system->mImpl->lightBudget = RenderSystem::defaultLightBudget();
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
		gAtmosphereSkyVisible = false;
		// tear the cubemap sky quad down BEFORE the root teardown, same rule as
		// gAtmosphere above: setSky created a Rectangle2D (mSky) attached to the
		// SCENE_STATIC root, and SceneManager::clearScene (run from
		// Root::shutdown) destroys every movable then re-attaches its cached
		// mSky UNCONDITIONALLY - a still-live skybox leaves mSky dangling and
		// clearScene re-attaches freed memory (a PAC/EXC_BAD_ACCESS at
		// teardown). setSky(false) detaches, destroys AND nulls mSky, so the
		// re-attach is skipped. The material/cubemap texture then die with the
		// root as before; only the bookkeeping resets here.
		if(!gSkyboxTexture.empty())
		{
			if(Ogre::SceneManager* sceneManager =
				RenderBackend::worldSceneManager())
			{
				sceneManager->setSky(false, Ogre::SceneManager::SkyCubemap,
					static_cast<Ogre::TextureGpu*>(NULL));
			}
		}
		gSkyboxTexture.clear();
		gSkyboxWarnedTexture.clear();
		// image lighting: the chain texture + the datablocks it was bound to
		// die with the root; only the bookkeeping resets here
		gIblActive = false;
		gIblTexture = NULL;
		gIblTextureOwned = false;
		gIblChainSource.clear();
		gIblChainQuality = IblPreset::IQ_OFF;
		gIblWarnedReason.clear();
		gProceduralIblHasKey = false;
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
		RenderBackend::resetDecalState();	// registry + pool statics (pool dies with the root)
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
	//--- LDR bloom (CompositorManager2 quad passes) ----------------------
	//---------------------------------------------------------
	bool RenderBackend::bloomSupported()
	{
		// desktop-capable Metal/Vulkan render systems all carry the RGBA8
		// off-screen render targets the bloom chain needs; the flavor answers
		// true unconditionally (the classic GLES2/WebGL runtime gate lives on
		// the other backend).
		return true;
	}
	//---------------------------------------------------------
	void RenderBackend::ensureBloomMaterials()
	{
		if(gBloomChecked)
		{
			return;
		}
		gBloomChecked = true;
		// the four bloom materials come from the auto-parsed OrkigeBloom.material
		// (the host registers orkige_engine/media/bloom/next before
		// initialiseResourceGroups). A media-less/headless boot has none - bloom
		// then degrades to no pass (byte-identical), logged once.
		Ogre::MaterialManager & materials = Ogre::MaterialManager::getSingleton();
		char const * names[4] = { "Orkige/Bloom/Bright", "Orkige/Bloom/BlurH",
			"Orkige/Bloom/BlurV", "Orkige/Bloom/Combine" };
		bool allPresent = true;
		for(char const * name : names)
		{
			if(!materials.getByName(name))
			{
				allPresent = false;
				break;
			}
		}
		gBloomMaterialsAvailable = allPresent;
		if(!allPresent)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: bloom post-process media not registered - "
				"rendering without bloom (an enabled scene bloom is ignored)");
		}
	}
	//---------------------------------------------------------
	bool RenderBackend::bloomActive()
	{
		if(!gRenderSystem)
		{
			return false;
		}
		RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
		// the pass renders only while the tier knob is on AND a scene enabled
		// bloom AND the materials resolved
		if(world->bloomQuality == BloomPreset::BQ_OFF || !world->bloom.enabled)
		{
			return false;
		}
		RenderBackend::ensureBloomMaterials();
		return gBloomMaterialsAvailable;
	}
	//---------------------------------------------------------
	void RenderBackend::applyBloomConfig()
	{
		if(!gRenderSystem)
		{
			return;
		}
		RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
		RenderBackend::ensureBloomMaterials();
		if(gBloomMaterialsAvailable)
		{
			// push the live threshold + intensity onto the low-level bloom
			// materials (the compositor quad passes read their pass params)
			const BloomDesc desc = world->bloom.sanitised();
			Ogre::MaterialManager & materials =
				Ogre::MaterialManager::getSingleton();
			if(Ogre::MaterialPtr bright = materials.getByName("Orkige/Bloom/Bright"))
			{
				bright->load();
				// the knob is DISPLAY-referred (an authored 0.15 means
				// "brighter than 0.15 as seen on screen") but the bright pass
				// samples the sRGB scene RT LINEARLY - a dim-scene lamp pool
				// reading 0.22 on screen is ~0.04 linear, so an unconverted
				// threshold can never catch non-emissive content. Convert
				// once here (gamma 2.2 approximates the sRGB curve; the
				// residual luminance-order error is below the knob's
				// authoring granularity).
				const Ogre::Real linearThreshold = std::pow(
					Ogre::Real(desc.threshold), Ogre::Real(2.2f));
				bright->getTechnique(0)->getPass(0)->getFragmentProgramParameters()
					->setNamedConstant("Threshold", linearThreshold);
			}
			if(Ogre::MaterialPtr combine =
				materials.getByName("Orkige/Bloom/Combine"))
			{
				combine->load();
				Ogre::GpuProgramParametersSharedPtr params =
					combine->getTechnique(0)->getPass(0)
						->getFragmentProgramParameters();
				params->setNamedConstant("OriginalImageWeight", Ogre::Real(1.0));
				params->setNamedConstant("Intensity", Ogre::Real(desc.intensity));
			}
		}
		// the window workspace's pass structure references the bloom chain at
		// BUILD time - rebuild it so it picks the chain up / drops it. Offscreen
		// render targets never bloom (byte-stable), so they are not rebuilt here.
		RenderBackend::recreateWindowWorkspace();
	}
	//---------------------------------------------------------
	void RenderBackend::setSceneDefaultVisibility()
	{
		// clear the 2D-tier bit from the process default so all 3D content is
		// created without it (only tagScene2D sets it); reserved layer bits are
		// preserved by setDefaultVisibilityFlags's user-range write
		Ogre::MovableObject::setDefaultVisibilityFlags(
			Ogre::MovableObject::getDefaultVisibilityFlags() &
			~RenderBackend::SCENE_2D_VISIBILITY);
	}
	//---------------------------------------------------------
	void RenderBackend::tagScene2D(Ogre::MovableObject* movable)
	{
		if(!movable)
		{
			return;
		}
		// exactly the 2D bit (disjoint from 3D content, which lacks it): the
		// bloom-on scene split renders the 3D bright-pass source without these
		// and composites them un-bloomed on top. setVisibilityFlags preserves
		// the reserved layer bits, so setVisible keeps working.
		movable->setVisibilityFlags(RenderBackend::SCENE_2D_VISIBILITY);
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
	namespace
	{
		//! show/hide the native SceneManager cubemap sky quad: @p textureName
		//! is a single cubemap image (a cubemap .dds - what
		//! Util/make_sky_assets.py bakes), "" disables. The texture loads
		//! WITHOUT the sRGB flag (colour parity rule: texels sample raw, like
		//! loadTexture2D) and without AutomaticBatching (cubemaps never pool).
		//! A missing/unloadable/non-cubemap file degrades honestly to the flat
		//! sky tint with one log line per name.
		void applySceneSkybox(Ogre::SceneManager* sceneManager,
			String const & requestedName)
		{
			// a cooked cubemap ships block-compressed: BCn stays the .dds name,
			// but ASTC/ETC2 renamed it to the native .oitd - resolve a missing
			// .dds to its cooked sibling (the same fallback loadTexture2D uses)
			const String textureName =
				RenderBackend::resolveTextureResourceName(requestedName);
			if(textureName == gSkyboxTexture)
			{
				return;	// already showing this cubemap (or already disabled)
			}
			if(textureName.empty())
			{
				sceneManager->setSky(false, Ogre::SceneManager::SkyCubemap,
					static_cast<Ogre::TextureGpu*>(NULL));
				gSkyboxTexture.clear();
				return;
			}
			Ogre::TextureGpuManager* textureManager =
				sceneManager->getDestinationRenderSystem()
					->getTextureGpuManager();
			try
			{
				Ogre::ResourceGroupManager & resourceGroups =
					Ogre::ResourceGroupManager::getSingleton();
				const String group =
					resourceGroups.findGroupContainingResource(textureName);
				// decode-PROBE on this thread before the async loader sees the
				// file (the loadTexture2D worker-recovery rule), and verify it
				// really is a cubemap - SceneManager::setSky throws otherwise
				{
					Ogre::DataStreamPtr probe =
						resourceGroups.openResource(textureName, group);
					Ogre::Image2 probeImage;
					probeImage.load2(probe, textureName);
					if(probeImage.getTextureType() !=
						Ogre::TextureTypes::TypeCube)
					{
						OGRE_EXCEPT(Ogre::Exception::ERR_INVALIDPARAMS,
							"'" + textureName + "' is not a cubemap image",
							"applySceneSkybox");
					}
				}
				Ogre::TextureGpu* texture =
					textureManager->createOrRetrieveTexture(textureName,
						textureName, Ogre::GpuPageOutStrategy::Discard,
						0u /*flags: no batching, no sRGB*/,
						Ogre::TextureTypes::TypeCube, group,
						Ogre::TextureFilter::TypeGenerateDefaultMipmaps);
				if(texture->getResidencyStatus() ==
					Ogre::GpuResidency::OnStorage)
				{
					texture->scheduleTransitionTo(
						Ogre::GpuResidency::Resident);
				}
				// wait for the texels, not just the metadata: the sky covers
				// the whole background, so a partially-streamed first frame
				// would flash black (and setSky reads the texture type)
				texture->waitForData();
				sceneManager->setSky(true, Ogre::SceneManager::SkyCubemap,
					texture);
				// same queue lesson as the NprSky quad: the upstream default
				// (212, late) sits past this backend's scene passes and would
				// overdraw non-depth-writing 3D alpha content - the sky
				// belongs in the skies-early queue. setSky exposes its quad
				// directly, so no material-name scan is needed here.
				sceneManager->getSky()->setRenderQueueGroup(kSkyRenderQueue);
				gSkyboxTexture = textureName;
			}
			catch(Ogre::Exception const & e)
			{
				if(gSkyboxWarnedTexture != textureName)
				{
					gSkyboxWarnedTexture = textureName;
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige next backend: skybox cubemap '" + textureName +
						"' failed to load - rendering the flat sky colour "
						"instead: " + e.getDescription());
				}
				sceneManager->setSky(false, Ogre::SceneManager::SkyCubemap,
					static_cast<Ogre::TextureGpu*>(NULL));
				gSkyboxTexture.clear();
			}
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
				gAtmosphereSkyVisible = false;
			}
			applySceneSkybox(sceneManager, String());
			// the linked sun returns EXACTLY to its authored colour/power
			restoreLinkedSun();
			// no skybox source left - image lighting deactivates with it
			RenderBackend::applyImageLighting();
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
			gAtmosphereSkyVisible = false;
		}

		// the sky VISUAL per type (AtmosphereDesc::skyType): the procedural
		// NprSky quad, the cubemap sky quad, or neither (flat sky-tint clear).
		// gAtmosphere itself stays alive on EVERY type - it owns the HlmsPbs
		// object fog and the native sun linkage, which are sky-type-
		// independent by the desc's contract.
		const bool wantProceduralSky =
			desc.skyType == AtmosphereSky::ST_PROCEDURAL;
		if(wantProceduralSky != gAtmosphereSkyVisible)
		{
			gAtmosphere->setSky(sceneManager, wantProceduralSky);
			gAtmosphereSkyVisible = wantProceduralSky;
			if(wantProceduralSky)
			{
				// setSky attached the sky as a Rectangle2D in a LATE render
				// queue (drawn after most content upstream) - past this
				// backend's scene passes, and it would overdraw non-depth-
				// writing 3D alpha content (sprites/particles) where only sky
				// is behind them. Move it to the skies-early queue instead
				// (@see kSkyRenderQueue); identified by its cloned
				// "Ogre/Atmo/NprSky*" material - the atmosphere does not
				// expose its quad.
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
		}
		if(desc.skyType == AtmosphereSky::ST_SKYBOX)
		{
			if(desc.skyboxTexture.empty())
			{
				// skybox mode without a cubemap: the honest flat-tint
				// degrade, said once
				if(gSkyboxWarnedTexture != "<empty>")
				{
					gSkyboxWarnedTexture = "<empty>";
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige next backend: skybox sky type without a "
						"cubemap texture - rendering the flat sky colour");
				}
				applySceneSkybox(sceneManager, String());
			}
			else
			{
				applySceneSkybox(sceneManager, desc.skyboxTexture);
			}
		}
		else
		{
			applySceneSkybox(sceneManager, String());
		}
		// the environment chain follows the skybox shown above (activates,
		// deactivates or rebuilds; a cheap no-op while the opt-in is off) -
		// BEFORE the preset fill below, so envmapScale reads the fresh state
		RenderBackend::applyImageLighting();

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
		// the atmosphere's ambient sync re-writes the scene envmapScale every
		// frame - hand it the image-lighting intensity so the two never fight
		preset.envmapScale = RenderBackend::imageLightingEnvmapScale();
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
	//--- image-based lighting (skybox-sourced) - IBL block ----------------
	namespace
	{
		//! the honest one-line degrade: an image-lighting opt-in that cannot
		//! render right now says WHY, once per distinct reason
		void warnImageLightingOnce(String const & reason)
		{
			if(gIblWarnedReason != reason)
			{
				gIblWarnedReason = reason;
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: image lighting " + reason +
					" - rendering unchanged");
			}
		}

		//! @brief (re)build the environment chain texture for @p source under
		//! @p quality and return it (NULL on failure). The chain is the skybox
		//! cubemap's own mip chain (prefiltered offline - the roughness chain
		//! both flavors sample); a tier cap below the source edge drops the
		//! leading mips into the derived kIblChainTexture copy, otherwise the
		//! loaded skybox texture binds directly.
		Ogre::TextureGpu* buildIblChainTexture(String const & source,
			IblPreset::Quality quality, bool & outOwned)
		{
			outOwned = false;
			Ogre::TextureGpuManager* textureManager = Ogre::Root::getSingleton()
				.getRenderSystem()->getTextureGpuManager();
			Ogre::TextureGpu* skybox =
				textureManager->findTextureNoThrow(source);
			if(!skybox)
			{
				return NULL;	// the sky shows, so this cannot happen in practice
			}
			const IblPreset::Settings settings = IblPreset::forQuality(quality);
			unsigned int skip = IblPreset::mipSkipForSource(
				skybox->getWidth(), settings);
			if(skip == 0u)
			{
				return skybox;	// within the tier cap - bind the skybox itself
			}
			try
			{
				// decode the cubemap again CPU-side (the baked .dds carries the
				// full chain) and re-image the tail below the tier cap
				Ogre::ResourceGroupManager & resourceGroups =
					Ogre::ResourceGroupManager::getSingleton();
				const String group =
					resourceGroups.findGroupContainingResource(source);
				Ogre::DataStreamPtr stream =
					resourceGroups.openResource(source, group);
				Ogre::Image2 image;
				image.load2(stream, source);
				const unsigned int sourceMips = image.getNumMipmaps();
				if(skip >= sourceMips)
				{
					skip = sourceMips - 1u;	// keep at least the 1-texel tail
				}
				const unsigned int chainMips = sourceMips - skip;
				const unsigned int chainEdge =
					std::max(1u, image.getWidth() >> skip);
				// one SIMD buffer in Image2's own layout (mip-major, the six
				// faces inside each mip) filled from the source's tail mips
				const size_t sizeBytes =
					Ogre::PixelFormatGpuUtils::calculateSizeBytes(chainEdge,
						chainEdge, 1u, 6u, image.getPixelFormat(),
						static_cast<Ogre::uint8>(chainMips), 4u);
				void* chainData = OGRE_MALLOC_SIMD(sizeBytes,
					Ogre::MEMCATEGORY_RESOURCE);
				size_t written = 0;
				for(unsigned int mip = 0; mip < chainMips; ++mip)
				{
					Ogre::TextureBox box = image.getData(
						static_cast<Ogre::uint8>(mip + skip));
					const size_t mipBytes =
						box.bytesPerImage * size_t(box.numSlices);
					oAssert(written + mipBytes <= sizeBytes);
					memcpy(static_cast<unsigned char*>(chainData) + written,
						box.data, mipBytes);
					written += mipBytes;
				}
				Ogre::Image2 chainImage;
				chainImage.loadDynamicImage(chainData, chainEdge, chainEdge,
					6u, Ogre::TextureTypes::TypeCube, image.getPixelFormat(),
					true /*autoDelete*/,
					static_cast<Ogre::uint8>(chainMips));
				// a MANUAL texture: no file behind the name (a non-manual
				// create would try to stream "Orkige/IblChain" from a
				// resource group); uploaded synchronously below
				Ogre::TextureGpu* chain = textureManager->createTexture(
					kIblChainTexture, Ogre::GpuPageOutStrategy::Discard,
					Ogre::TextureFlags::ManualTexture,
					Ogre::TextureTypes::TypeCube);
				chain->setResolution(chainEdge, chainEdge);
				chain->setPixelFormat(image.getPixelFormat());
				chain->setNumMipmaps(static_cast<Ogre::uint8>(chainMips));
				chain->scheduleTransitionTo(Ogre::GpuResidency::Resident);
				// wait for residency, then upload the whole chain (the chain
				// lights the whole scene - it must be complete before the
				// next frame samples it)
				chain->waitForData();
				chainImage.uploadTo(chain, 0u,
					static_cast<Ogre::uint8>(chainMips - 1u));
				outOwned = true;
				return chain;
			}
			catch(Ogre::Exception const & e)
			{
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: image-lighting chain for '" + source +
					"' failed to build - binding the skybox cubemap unreduced: "
					+ e.getDescription());
				return skybox;	// honest fallback: full-resolution chain
			}
		}

		//! @brief synthesize the procedural-sky environment chain: a manual
		//! cubemap at the tier resolution, its RGBA8 mip chain built on the CPU
		//! from the atmosphere + sun (@see core_util/SkyEnvMap - the same sky
		//! model the classic dome draws). Uploaded whole, like the derived
		//! skybox chain; always OWNED. NULL (outOwned false) on failure. The
		//! caller drops any prior owned chain (they share kIblChainTexture -
		//! the two sources never coexist).
		Ogre::TextureGpu* buildProceduralIblChainTexture(
			AtmosphereDesc const & desc, Ogre::Vector3 const & toSun,
			IblPreset::Quality quality, bool & outOwned)
		{
			outOwned = false;
			const unsigned int edge =
				IblPreset::forQuality(quality).chainResolution;
			if(edge == 0u)
			{
				return NULL;
			}
			std::vector<unsigned char> chain;
			unsigned int chainMips = 0u;
			SkyEnvMap::buildCubemapChainRgba8(edge, desc,
				static_cast<float>(toSun.x), static_cast<float>(toSun.y),
				static_cast<float>(toSun.z), chain, chainMips);
			try
			{
				Ogre::TextureGpuManager* textureManager =
					Ogre::Root::getSingleton().getRenderSystem()
						->getTextureGpuManager();
				const Ogre::PixelFormatGpu format = Ogre::PFG_RGBA8_UNORM;
				const size_t sizeBytes =
					Ogre::PixelFormatGpuUtils::calculateSizeBytes(edge, edge,
						1u, 6u, format,
						static_cast<Ogre::uint8>(chainMips), 4u);
				if(sizeBytes != chain.size())
				{
					// RGBA8 rows are 4-aligned, so the tight CPU layout must
					// equal the GPU layout - a mismatch means a format/padding
					// surprise; refuse rather than upload garbage
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige next backend: procedural-sky environment layout "
						"mismatch - skipping capture");
					return NULL;
				}
				void* data = OGRE_MALLOC_SIMD(sizeBytes,
					Ogre::MEMCATEGORY_RESOURCE);
				memcpy(data, chain.data(), sizeBytes);
				Ogre::Image2 image;
				image.loadDynamicImage(data, edge, edge, 6u,
					Ogre::TextureTypes::TypeCube, format, true /*autoDelete*/,
					static_cast<Ogre::uint8>(chainMips));
				Ogre::TextureGpu* texture = textureManager->createTexture(
					kIblChainTexture, Ogre::GpuPageOutStrategy::Discard,
					Ogre::TextureFlags::ManualTexture,
					Ogre::TextureTypes::TypeCube);
				texture->setResolution(edge, edge);
				texture->setPixelFormat(format);
				texture->setNumMipmaps(static_cast<Ogre::uint8>(chainMips));
				texture->scheduleTransitionTo(Ogre::GpuResidency::Resident);
				// the chain lights the whole scene - complete it before the
				// next frame samples it
				texture->waitForData();
				image.uploadTo(texture, 0u,
					static_cast<Ogre::uint8>(chainMips - 1u));
				outOwned = true;
				return texture;
			}
			catch(Ogre::Exception const & e)
			{
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: procedural-sky environment capture "
					"failed: " + e.getDescription());
				return NULL;
			}
		}

		//! destroy the derived chain copy if this backend owns one
		void dropOwnedIblChainTexture()
		{
			if(gIblTexture && gIblTextureOwned)
			{
				Ogre::TextureGpuManager* textureManager =
					Ogre::Root::getSingleton().getRenderSystem()
						->getTextureGpuManager();
				if(!gIblTexture->isDataReady())
				{
					gIblTexture->waitForData();	// a deferred destroy would
				}								// hold the name (see destroyTexture2DByName)
				textureManager->destroyTexture(gIblTexture);
			}
			gIblTexture = NULL;
			gIblTextureOwned = false;
		}
	}
	//---------------------------------------------------------
	void RenderBackend::applyImageLighting()
	{
		if(!gRenderSystem)
		{
			return;
		}
		RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
		bool want = world->iblEnabled &&
			world->iblQuality != IblPreset::IQ_OFF;
		// SOURCE selection - one downstream consumer, two sources: an authored
		// skybox cubemap (gSkyboxTexture, the offline-baked prefiltered chain)
		// OR, when the procedural sky is showing with no skybox, a runtime
		// capture of that sky (@see buildProceduralIblChainTexture). Colour
		// skies and a disabled atmosphere still have no meaningful environment.
		const bool proceduralSource = want && gSkyboxTexture.empty() &&
			world->atmosphere.enabled &&
			world->atmosphere.skyType == AtmosphereSky::ST_PROCEDURAL &&
			gAtmosphere && gAtmosphereSkyVisible;
		if(want && gSkyboxTexture.empty() && !proceduralSource)
		{
			warnImageLightingOnce("is enabled without a skybox cubemap or a "
				"procedural sky (needs an enabled atmosphere showing a "
				"skybox or procedural sky)");
			want = false;
		}
		if(!want)
		{
			if(gIblActive)
			{
				// unbind the reflection map from every generated PBS datablock
				// (restore-exactly: an untouched datablock is a no-op write)
				for(Ogre::HlmsDatablock* each : gContentDatablocks)
				{
					if(each->getCreator()->getType() == Ogre::HLMS_PBS)
					{
						static_cast<Ogre::HlmsPbsDatablock*>(each)->setTexture(
							Ogre::PBSM_REFLECTION,
							static_cast<Ogre::TextureGpu*>(NULL));
					}
				}
				dropOwnedIblChainTexture();
				gIblChainSource.clear();
				gIblChainQuality = IblPreset::IQ_OFF;
				gProceduralIblHasKey = false;
				gIblActive = false;
			}
			return;
		}
		gIblWarnedReason.clear();	// active again - a future refusal logs anew
		if(proceduralSource)
		{
			// the sun the sky is lit by (first directional light, toward-sun) -
			// the same convention applyAtmosphere reads
			Ogre::Vector3 toSun(0.3f, 0.9f, 0.2f);
			if(Ogre::Light* sun = RenderBackend::firstDirectionalLight())
			{
				toSun = -sun->getDerivedDirectionUpdated();
			}
			toSun.normalise();
			const SkyEnvMap::CaptureKey nowKey = SkyEnvMap::keyFor(
				world->atmosphere, static_cast<float>(toSun.x),
				static_cast<float>(toSun.y), static_cast<float>(toSun.z));
			// recapture on a source/tier switch, a first capture, or a material
			// sky move (sun swing / colour change) - never per frame otherwise
			const bool rebuild = !gIblActive ||
				gIblChainSource != kProceduralSource ||
				gIblChainQuality != world->iblQuality ||
				!gProceduralIblHasKey ||
				SkyEnvMap::materiallyDiffers(gProceduralIblKey, nowKey,
					kSunMoveCosThreshold);
			if(rebuild)
			{
				dropOwnedIblChainTexture();
				gIblTexture = buildProceduralIblChainTexture(world->atmosphere,
					toSun, world->iblQuality, gIblTextureOwned);
				if(!gIblTexture)
				{
					warnImageLightingOnce(
						"could not capture the procedural sky");
					return;
				}
				gProceduralIblKey = nowKey;
				gProceduralIblHasKey = true;
				gIblChainSource = kProceduralSource;
				gIblChainQuality = world->iblQuality;
				// the observable recapture marker (one line per capture) - the
				// day/night arc's cadence and the selfcheck's recapture proof
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: procedural-sky image-lighting "
					"capture");
			}
		}
		else
		{
			// the authored-skybox source: (re)build the chain on source/tier move
			if(!gIblActive || gIblChainSource != gSkyboxTexture ||
				gIblChainQuality != world->iblQuality)
			{
				dropOwnedIblChainTexture();
				gIblTexture = buildIblChainTexture(gSkyboxTexture,
					world->iblQuality, gIblTextureOwned);
				if(!gIblTexture)
				{
					warnImageLightingOnce(
						"found no loaded skybox cubemap to source");
					return;
				}
				gIblChainSource = gSkyboxTexture;
				gIblChainQuality = world->iblQuality;
			}
			gProceduralIblHasKey = false;	// not a procedural capture now
		}
		gIblActive = true;
		// bind the chain to every generated PBS datablock (surface + water);
		// datablocks created later register through registerContentDatablock,
		// which routes them here via applyImageLightingToDatablock
		for(Ogre::HlmsDatablock* each : gContentDatablocks)
		{
			RenderBackend::applyImageLightingToDatablock(each);
		}
	}
	//---------------------------------------------------------
	void RenderBackend::applyImageLightingToDatablock(
		Ogre::HlmsDatablock* datablock)
	{
		if(!gIblActive || !gIblTexture || !datablock ||
			datablock->getCreator()->getType() != Ogre::HLMS_PBS)
		{
			return;
		}
		static_cast<Ogre::HlmsPbsDatablock*>(datablock)->setTexture(
			Ogre::PBSM_REFLECTION, gIblTexture);
	}
	//---------------------------------------------------------
	float RenderBackend::imageLightingEnvmapScale()
	{
		if(!gIblActive || !gRenderSystem)
		{
			return 1.0f;
		}
		return gRenderSystem->getWorld()->mImpl->iblIntensity;
	}
	//--- end IBL block ----------------------------------------------------
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
		// ORKIGE_BLOOM_BEGIN (delimited so the concurrent IBL edits to this hub
		// stay separable at landing): when a scene enabled LDR bloom and the tier
		// knob is on, the window node grows a bright-pass -> separable blur ->
		// additive-combine quad chain BETWEEN the 3D scene pass and the 2D/UI
		// passes. The 3D scene renders into an off-screen SceneRT (masked to the
		// 3D tier - the 2D bit is excluded), the chain glows it into WindowRT,
		// then the 2D tier (sprites/vector meshes, the SCENE_2D_VISIBILITY bit)
		// and the GUI draw un-bloomed on top. Bloom off -> the byte-identical
		// two-pass node below.
		const bool useBloom = !impl->uiOnlyWindow && backendCamera &&
			RenderBackend::bloomActive();
		if(useBloom)
		{
			RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
			const BloomPreset::Settings tier =
				BloomPreset::forQuality(world->bloomQuality);
			const float downFactor = 1.0f /
				static_cast<float>(std::max(tier.downsampleFactor, 1));
			const int blurPasses = std::max(tier.blurPasses, 1);
			// off-screen textures: full-res scene (with depth), two downsampled
			// ping-pong bloom buffers (no depth)
			Ogre::TextureDefinitionBase::TextureDefinition* sceneTex =
				nodeDefinition->addTextureDefinition("SceneRT");
			sceneTex->widthFactor = 1.0f;
			sceneTex->heightFactor = 1.0f;
			sceneTex->format = Ogre::PFG_RGBA8_UNORM_SRGB;
			nodeDefinition->addRenderTextureView("SceneRT")
				->setForTextureDefinition("SceneRT", sceneTex);
			for(char const * bloomBuf : { "BloomA", "BloomB" })
			{
				Ogre::TextureDefinitionBase::TextureDefinition* tex =
					nodeDefinition->addTextureDefinition(bloomBuf);
				tex->widthFactor = downFactor;
				tex->heightFactor = downFactor;
				tex->format = Ogre::PFG_RGBA8_UNORM_SRGB;
				tex->depthBufferId = 0;	// a blurred quad target needs no depth
				nodeDefinition->addRenderTextureView(bloomBuf)
					->setForTextureDefinition(bloomBuf, tex);
			}
			// SceneRT(1) + bright(1) + 2*blur + WindowRT(1)
			nodeDefinition->setNumTargetPass(3 + 2 * blurPasses);
			// --- the 3D scene into SceneRT (2D tier masked out) ---
			{
				Ogre::CompositorTargetDef* sceneTarget =
					nodeDefinition->addTargetPass("SceneRT");
				sceneTarget->setNumPasses(1);
				Ogre::CompositorPassSceneDef* scenePass =
					static_cast<Ogre::CompositorPassSceneDef*>(
						sceneTarget->addPass(Ogre::PASS_SCENE));
				scenePass->setAllLoadActions(Ogre::LoadAction::Clear);
				scenePass->setAllClearColours(impl->windowBackground);
				scenePass->mFirstRQ = 0;
				scenePass->mLastRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE;
				scenePass->setVisibilityMask(
					~RenderBackend::SCENE_2D_VISIBILITY);
				const String shadowNode = RenderBackend::activeShadowNodeName();
				if(!shadowNode.empty())
				{
					scenePass->mShadowNode = Ogre::IdString(shadowNode);
				}
			}
			// --- bright-pass: SceneRT -> BloomA ---
			{
				Ogre::CompositorTargetDef* brightTarget =
					nodeDefinition->addTargetPass("BloomA");
				brightTarget->setNumPasses(1);
				Ogre::CompositorPassQuadDef* brightPass =
					static_cast<Ogre::CompositorPassQuadDef*>(
						brightTarget->addPass(Ogre::PASS_QUAD));
				brightPass->setAllLoadActions(Ogre::LoadAction::DontCare);
				brightPass->mMaterialName = "Orkige/Bloom/Bright";
				brightPass->addQuadTextureSource(0, "SceneRT");
			}
			// --- separable gaussian blur, ping-ponging A<->B (blurPasses V+H) ---
			for(int pass = 0; pass < blurPasses; ++pass)
			{
				Ogre::CompositorTargetDef* vTarget =
					nodeDefinition->addTargetPass("BloomB");
				vTarget->setNumPasses(1);
				Ogre::CompositorPassQuadDef* vPass =
					static_cast<Ogre::CompositorPassQuadDef*>(
						vTarget->addPass(Ogre::PASS_QUAD));
				vPass->setAllLoadActions(Ogre::LoadAction::DontCare);
				vPass->mMaterialName = "Orkige/Bloom/BlurV";
				vPass->addQuadTextureSource(0, "BloomA");

				Ogre::CompositorTargetDef* hTarget =
					nodeDefinition->addTargetPass("BloomA");
				hTarget->setNumPasses(1);
				Ogre::CompositorPassQuadDef* hPass =
					static_cast<Ogre::CompositorPassQuadDef*>(
						hTarget->addPass(Ogre::PASS_QUAD));
				hPass->setAllLoadActions(Ogre::LoadAction::DontCare);
				hPass->mMaterialName = "Orkige/Bloom/BlurH";
				hPass->addQuadTextureSource(0, "BloomB");
			}
			// --- combine + un-bloomed 2D + GUI onto WindowRT ---
			{
				Ogre::CompositorTargetDef* windowTarget =
					nodeDefinition->addTargetPass("WindowRT");
				windowTarget->setNumPasses(3);
				Ogre::CompositorPassQuadDef* combinePass =
					static_cast<Ogre::CompositorPassQuadDef*>(
						windowTarget->addPass(Ogre::PASS_QUAD));
				combinePass->setAllLoadActions(Ogre::LoadAction::DontCare);
				combinePass->mMaterialName = "Orkige/Bloom/Combine";
				combinePass->addQuadTextureSource(0, "SceneRT");
				combinePass->addQuadTextureSource(1, "BloomA");
				// the 2D tier (sprites/vector meshes) un-bloomed, on top
				Ogre::CompositorPassSceneDef* twoDPass =
					static_cast<Ogre::CompositorPassSceneDef*>(
						windowTarget->addPass(Ogre::PASS_SCENE));
				twoDPass->setAllLoadActions(Ogre::LoadAction::Load);
				twoDPass->mFirstRQ = 0;
				twoDPass->mLastRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE;
				twoDPass->setVisibilityMask(RenderBackend::SCENE_2D_VISIBILITY);
				// the GUI / 2D layers, un-bloomed, on top
				Ogre::CompositorPassSceneDef* uiPass =
					static_cast<Ogre::CompositorPassSceneDef*>(
						windowTarget->addPass(Ogre::PASS_SCENE));
				uiPass->setAllLoadActions(Ogre::LoadAction::Load);
				uiPass->mFirstRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE;
				uiPass->mLastRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE + 1;
				uiPass->mCameraName = RenderBackend::drawLayer2DCameraName();
				uiPass->setVisibilityMask(RenderBackend::UI_WINDOW_VISIBILITY);
			}
		}
		else
		{
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
		}
		// ORKIGE_BLOOM_END
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
				// Only a Type2D image can ride the AutomaticBatching 2D texture
				// created below. A cubemap/volume/array image (e.g. a skybox
				// .dds handed to the 2D loader) makes the async upload abort
				// mid-map and leaves a staging texture in the pump - the process
				// then SIGABRTs on the next map-region. Reject it HERE, on the
				// main thread, BEFORE any GPU texture is scheduled: honest NULL +
				// one log line, mirroring applySceneSkybox's cubemap probe. This
				// guards every caller against any unsupported texture SHAPE, not
				// just cubemaps.
				if(probeImage.getTextureType() != Ogre::TextureTypes::Type2D)
				{
					OGRE_EXCEPT(Ogre::Exception::ERR_INVALIDPARAMS,
						"'" + textureName + "' is not a 2D texture "
						"(a cubemap/volume/array image cannot load as a 2D "
						"texture)", "RenderBackend::loadTexture2D");
				}
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
		// content-surface textures TILE: the baked terrain (and any tiling
		// ground/wall material) carries UVs that run past [0,1], so the maps
		// must WRAP or the texture collapses to a single averaged colour (a flat
		// muddy tint instead of the repeating detail). The classic RTSS path
		// wraps by default; next must set the addressing mode explicitly - the
		// same reason the sprite path builds its own samplerblock above.
		Ogre::HlmsSamplerblock samplerblock;
		samplerblock.setAddressingMode(Ogre::TAM_WRAP);
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
				datablock->setTexture(binding.slot, texture, &samplerblock);
				outComplete = outComplete && texture != NULL;
			}
			else
			{
				datablock->setTexture(binding.slot, resolvedName, &samplerblock);
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
		// late-created PBS content joins the active image-lighting set
		RenderBackend::applyImageLightingToDatablock(datablock);
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
