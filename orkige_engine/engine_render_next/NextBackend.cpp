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
#include "engine_render/RenderSystemSelection.h"	// the deviceless boot word
#include "engine_render/RenderMaterialCache.h"	// the create-or-update memo
#include <core_util/PngWriter.h>	// THE image encode: the engine owns it
#include <core_util/SkyEnvMap.h>
#include <core_util/PlanarReflectionGuard.h>
#include <core_debug/Breadcrumbs.h>	// crash-survivable trail at the mid-run mirror init points
#include <core_debug/CVarManager.h>	// the r.planarReflection gate

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
#include <OgreNULLPlugin.h>	// the deviceless render system (@see bootRenderSystemPlugin)
#include <OgrePlugin.h>
#include <OgreRenderSystem.h>
#include <OgreSceneManager.h>
#include <OgreLight.h>
#include <OgreMath.h>
#include <OgreAtmosphereNpr.h>
#include <OgreItem.h>				// planar-reflection renderable registration
#include <OgreSubItem.h>
#include <OgreMesh2.h>				// wireframe overlay companion shares the source mesh
#include <OgrePlanarReflections.h>	// mirror-of-scene water reflection subsystem
#include <OgreHlmsListener.h>	// the pass-buffer seam the water swell clock rides
#include <OgreRectangle2D2.h>
#include <OgreMaterial.h>
#include <OgreDepthBuffer.h>			// the deviceless window's depth-less declaration
#include <Vao/OgreVaoManager.h>			// the deviceless vao-name reservation
#include <Vao/OgreVertexArrayObject.h>
#include <Vao/OgreVertexBufferPacked.h>
#include <OgreTextureGpuManager.h>
#include <OgreTextureFilters.h>
#include <OgreTextureBox.h>
#include <OgreImage2.h>
#include <OgreDataStream.h>
#include <OgrePixelFormatGpuUtils.h>
#include <OgreBitwise.h>			// half-float encode for the water env chain
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
#include <Compositor/Pass/PassMipmap/OgreCompositorPassMipmapDef.h>	// planar reflection mip chain
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
		//! every 3D-SCENE datablock the backend generated (PBS mesh/material/
		//! water - @see RenderBackend::DT_SCENE): the Scene-view wireframe toggle
		//! target AND the image-lighting bind set (all PBS live here). Datablocks
		//! are shared by name and live until teardown.
		std::vector<Ogre::HlmsDatablock*> gSceneDatablocks;
		//! every 2D/UI datablock the backend generated (sprites, vector shapes,
		//! dynamic lines, DrawLayer2D - the editor's own ImGui chrome + gui go
		//! through here - @see RenderBackend::DT_UI): NEVER wireframed, so the
		//! editor UI stays solid while the Scene-view wireframe is armed. Kept
		//! only for the teardown clear + the RTT-datablock retire bookkeeping.
		std::vector<Ogre::HlmsDatablock*> gUiDatablocks;
		//! per-incarnation RTT datablocks whose incarnation has died but whose
		//! batch may still link them; destroyed once unlinked (@see
		//! RenderBackend::retireRTTDatablock / flushRetiredRTTDatablocks)
		std::set<String> gRetiredRTTDatablocks;
		//! current global wireframe state (applied to late datablocks too)
		bool gWireframe = false;
		//! every scene-tier mesh Item the backend created (@see createMeshInstance;
		//! unregistered by ~MeshInstance) - the source set the SHADED+WIREFRAME
		//! overlay shadows with wireframe companions (@see setSceneWireframeOverlay)
		std::vector<Ogre::Item*> gSceneItems;
		//! the live wireframe companion per source Item while the overlay is armed
		//! (@see setSceneWireframeOverlay); a companion shares its source's mesh +
		//! node and dies with its source (unregisterSceneItem) or on disarm
		std::unordered_map<Ogre::Item*, Ogre::Item*> gWireframeOverlays;
		//! the SHADED+WIREFRAME overlay armed flag + the editor-only visibility bit
		//! the companions carry (masked off the Game Preview target)
		bool gOverlayArmed = false;
		unsigned int gOverlayFlags = 0;
		//! the ONE shared unlit wireframe datablock the companions render with
		//! (near-black, macroblock polygon-mode wireframe + a small depth bias so
		//! the lines sit on the shaded surface); created lazily, dies with the root
		Ogre::HlmsDatablock* gWireframeOverlayDatablock = NULL;
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
		//! did the grade material (Orkige/Grade/Apply) resolve at first use?
		//! (false on a media-less/headless boot - setOutputGrade then degrades to
		//! no pass, byte-identical). Checked once (gGradeChecked).
		bool gGradeMaterialsAvailable = false;
		bool gGradeChecked = false;
		//! the live REFRACTIVE water materials (createOrUpdateWaterDatablock records
		//! a name here when screen-space refraction is on + the copy media resolved).
		//! Non-empty => the window workspace grows the refraction scene split
		//! (@see recreateWindowWorkspace); MeshInstance::setMaterial reads it to put
		//! a refractive surface in WATER_REFRACTION_RENDER_QUEUE.
		std::set<String> gRefractiveWaterMaterials;
		//! did the refraction copy material (Orkige/Refraction/Copy) resolve at
		//! first use? (false on a media-less/headless boot - refraction then stays
		//! the byte-stable Transparent look). Checked once (gRefractionChecked).
		bool gRefractionMaterialsAvailable = false;
		bool gRefractionChecked = false;

		//--- planar water reflection (Ogre::PlanarReflections) - PLANAR block ---
		//! the live PLANAR-REFLECTIVE water materials (createOrUpdateWaterDatablock
		//! records a name here when planar reflection is on + the cap is present).
		//! Non-empty => the reflection subsystem below is stood up; MeshInstance::
		//! setMaterial reads it (isPlanarReflectiveWaterMaterial) to keep the
		//! surface out of its own mirror.
		std::set<String> gPlanarReflectiveWaterMaterials;
		//! the mirror-of-scene reflection subsystem, NULL while no scene opts in;
		//! owned here, torn down BEFORE the root (its cameras/RTTs/workspaces live
		//! on the scene manager + compositor manager). @see ensurePlanarReflections
		Ogre::PlanarReflections* gPlanarReflections = NULL;
		//! the single reflection actor (the water plane, normal +Y); its plane is
		//! re-set as the water's world Y / extents change
		Ogre::PlanarReflectionActor* gPlanarReflectionActor = NULL;
		//! the HlmsPbs the subsystem is set on (to unset at teardown)
		Ogre::HlmsPbs* gPlanarReflectionPbs = NULL;
		//! the reflection RTT resolution baked at subsystem build (window size at
		//! that moment, capped); quality only - the HlmsPbs reflection projection
		//! is aspect-correct regardless (the reflection camera carries the aspect)
		Ogre::uint32 gPlanarReflectionWidth = 0;
		Ogre::uint32 gPlanarReflectionHeight = 0;
		//! the plane the live actor mirrors across (world Y + surface half-extents),
		//! so an unchanged re-apply skips the actor re-set
		float gPlanarReflectionPlaneY = 0.0f;
		float gPlanarReflectionHalfX = 0.0f;
		float gPlanarReflectionHalfZ = 0.0f;
		//! water Items currently tracked as reflection renderables (their SubItems
		//! were addRenderable'd), so a material change / teardown can drop them and
		//! reset each renderable's tracking parameter
		std::set<Ogre::Item*> gPlanarTrackedItems;
		//! the reflection scene-render workspace DEFINITION name ("" until built);
		//! one hand-built node renders the scene (water excluded) into the RTT
		String gPlanarReflectionWorkspaceDef;
		//--- end PLANAR block --------------------------------------------

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
		//! a directional light was added/removed/retyped since the last frame:
		//! re-resolve the enabled atmosphere's sun once at the next frame
		//! boundary, when all scene transforms are composed (@see
		//! RenderBackend::flushAtmosphereSunReresolve - the red-sky-on-load fix)
		bool gAtmosphereSunReresolvePending = false;
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
		//! the AUTHORED local orientation of the linked sun's node, snapshotted
		//! the moment the atmosphere takes the light (before it ever drives it).
		//! AtmosphereNpr::syncToLight steers the linked light's node
		//! (Light::setDirection writes it in PARENT space from a world-space sun
		//! vector), which - on the sun's LightComponent mount, a child of the
		//! authored transform - bakes the parent's rotation into the mount and
		//! doubles the authored sun angle. The node's authored orientation is the
		//! sun direction's single source of truth (identity for a mount, the
		//! authored value for a directly-attached facade light), so it is pinned
		//! back before each direction read and after each drive
		Ogre::Quaternion gLinkedSunNodeLocal = Ogre::Quaternion::IDENTITY;

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
		//! the water mirror's RATIO-TRUE sibling of the clamped chain: the SAME
		//! procedural capture re-encoded HDR (RGBA16F, texel = the scaled-chain
		//! ratio x the capture scale - the exact radiance the classic water
		//! program reconstructs in-shader from the same capture bytes), bound
		//! ONLY to water datablocks' reflection slot so both flavors' mirrors
		//! read the same warm HDR sky and breathe equally under the swell.
		//! Every OTHER IBL consumer stays on the clamped chain (the
		//! image-lighting fill is deliberately calibrated LDR on both flavors -
		//! @see applyImageLightingToDatablock for the routing). NULL while
		//! image lighting is inactive or the source is an authored skybox
		//! (LDR content: the mirror samples the clamped chain there, capture
		//! scale 1, exactly like the classic water's skybox fallback).
		Ogre::TextureGpu* gWaterEnvTexture = NULL;
		const char* const kWaterEnvTexture = "Orkige/WaterSkyEnv";
		//! the generated PBS water datablocks (createOrUpdateWaterDatablock) -
		//! the identity the reflection bind routes on; pointers are stable
		//! until the backend teardown (datablocks die with the root)
		std::set<Ogre::HlmsDatablock*> gWaterDatablocks;
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
				// hand the node its authored orientation back too, so a released
				// sun keeps pointing where it was authored (@see gLinkedSunNodeLocal)
				if(Ogre::Node* node = gLinkedSun->getParentNode())
				{
					node->setOrientation(gLinkedSunNodeLocal);
				}
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

		//! @brief drives the planar water reflection each frame from INSIDE the
		//! window workspace update
		//! @remarks the reflection subsystem renders a mirror camera into its RTT
		//! by nesting a workspace _update, which culls against the scene manager -
		//! so it MUST run AFTER Root::renderOneFrame's per-frame updateSceneGraph
		//! populates the cull lists and BEFORE clearFrameData wipes them. The
		//! window workspace's workspacePreUpdate fires exactly in that window (the
		//! compositor updates workspaces after updateSceneGraph), so the reflection
		//! renders here rather than ahead of renderOneFrame. No-op unless a scene
		//! opted a water surface into planar reflection.
		class PlanarReflectionUpdater
			: public Ogre::CompositorWorkspaceListener
		{
		public:
			virtual void workspacePreUpdate(Ogre::CompositorWorkspace* /*ws*/)
			{
				RenderBackend::updatePlanarReflections();
			}
		};
		//! one instance, attached to the window workspace on every rebuild
		PlanarReflectionUpdater gPlanarReflectionUpdater;

		//! @brief suppresses the nested planar reflection update for the single
		//! frame in which the window workspace was (re)built (@see
		//! PlanarReflectionGuard). recreateWindowWorkspace notes the rebuild;
		//! updatePlanarReflections consumes the one-shot skip. Byte-inert in the
		//! steady state (no rebuild => never skips).
		PlanarReflectionGuard gPlanarReflectionGuard;

		//! @brief false until the FIRST real (non-skipped) mirror update runs on
		//! the current subsystem instance. Reset when the subsystem is (re)created
		//! or destroyed, so each mid-run stand-up of the mirror drops exactly one
		//! "first mirror update" breadcrumb pair - the crash-survivable evidence
		//! that pinpoints whether a mid-tour death lands in the nested reflection
		//! render (the mirrorlake arrival) vs. the subsystem allocation before it.
		bool gPlanarFirstUpdateLogged = false;

		//! @brief false until the SECOND real (non-skipped) mirror update runs
		//! on the current subsystem instance - the one-shot "steady state
		//! entered" marker. Reset alongside gPlanarFirstUpdateLogged. A recurrence
		//! whose last crumb is "second mirror render done" died AFTER two full
		//! mirror frames completed (deep steady-state render, a live-subsystem
		//! window-workspace rebuild, or teardown), NOT on the fragile first or
		//! second nested render; its ABSENCE with a completed first render pins
		//! death on the second nested render. Exactly one crumb per stand-up -
		//! never per-frame spam.
		bool gPlanarSecondUpdateLogged = false;

		//! @brief count of REAL (non-skipped) nested mirror renders on the current
		//! subsystem instance. Reset alongside the first/second markers. A crumb is
		//! dropped every 64th render so a mid-tour death names how DEEP into steady
		//! state it fell (the trail otherwise dead-ends at "second mirror render
		//! done" no matter how many hundreds of frames followed) - bounded to a
		//! handful of crumbs per vignette, never per-frame spam.
		unsigned long long gPlanarRenderCount = 0;

		//! @brief false until the FIRST guard skip that lands AFTER steady state was
		//! entered (i.e. after the second real render). In the steady state the
		//! guard is provably never armed - a skip here means a window-workspace
		//! rebuild (or a subsystem re-stand) re-armed it mid-tour, the
		//! rebuild-interplay shape. One crumb, so a recurrence shows whether a
		//! post-steady skip preceded the death (reviving the interplay hypothesis)
		//! or the death is a plain deep steady-state render. Reset alongside the
		//! other per-instance markers.
		bool gPlanarPostSteadySkipLogged = false;

		//--- geometric water swell (vertex-stage displacement) ------------
		//! the world-space swell frequency + phase rate BOTH flavors' water
		//! vertex stages share (the classic program pushes the same numbers
		//! through its waveParams constant), so the two flavors' swells move
		//! in lockstep. Wavelength ~12.5 world units, ~1.2 rad/s travel.
		constexpr float kSwellWorldFrequency = 0.5f;
		constexpr float kSwellPhaseRate = 1.2f;
		//! the live swell clock (seconds, from setWaterDatablockTime - every
		//! surface shares the one clock; per-surface amplitude bakes into the
		//! surface's own custom piece)
		float gWaterSwellClock = 0.0f;
		//! the swell-displaced water materials (waveHeight > 0) - while any
		//! exist, the listener below publishes the clock into the pass buffer
		std::set<String> gSwellWaterMaterials;
		//! @brief appends the swell clock to every PBS pass buffer while the
		//! swell is live: the water datablock's custom vertex piece reads
		//! passBuf.orkigeWaterSwell.x as its phase. The member sits at the
		//! STRUCT TAIL (custom_passBuffer inserts last), so shaders that do
		//! not declare it read their smaller view of the same buffer safely.
		class WaterSwellHlmsListener : public Ogre::HlmsListener
		{
		public:
			void preparePassHash(const Ogre::CompositorShadowNode*,
				bool /*casterPass*/, bool /*dualParaboloid*/,
				Ogre::SceneManager*, Ogre::Hlms* hlms) override
			{
				// the property gates the piece's passBuf declaration; setting
				// it recompiles the PBS set ONCE when the first swell surface
				// appears (cached afterwards)
				hlms->_setProperty(Ogre::Hlms::kNoTid,
					"orkige_water_swell", 1);
			}
			Ogre::uint32 getPassBufferSize(const Ogre::CompositorShadowNode*,
				bool /*casterPass*/, bool /*dualParaboloid*/,
				Ogre::SceneManager*) const override
			{
				return 16u;	// float4 orkigeWaterSwell (x = clock, yzw pad)
			}
			float* preparePassBuffer(const Ogre::CompositorShadowNode*,
				bool /*casterPass*/, bool /*dualParaboloid*/,
				Ogre::SceneManager*, float* passBufferPtr) override
			{
				*passBufferPtr++ = gWaterSwellClock;
				*passBufferPtr++ = 0.0f;
				*passBufferPtr++ = 0.0f;
				*passBufferPtr++ = 0.0f;
				return passBufferPtr;
			}
		};
		WaterSwellHlmsListener gWaterSwellListener;
		bool gWaterSwellListenerSet = false;
		//--- end water swell ----------------------------------------------

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
			// the error_code overload: a directory the process cannot READ
			// (a permission-denied media root) must reach the same honest
			// warning as a missing one - the throwing overload would abort the
			// boot the comment above promises to survive
			std::error_code hlmsProbeError;
			if(!std::filesystem::exists(rootFolder + "Hlms", hlmsProbeError))
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
				// ORKIGE_HLMS_DUMP_DIR: write every generated PBS shader into
				// the given directory - the read-the-generated-shader
				// diagnostic for cross-flavor shading questions (the classic
				// sibling is ORKIGE_RTSS_DUMP_DIR, @see Engine.cpp). Inert
				// without the env var: the debug dump stays silenced as above.
				if(const char* dumpDir = std::getenv("ORKIGE_HLMS_DUMP_DIR"))
				{
					pbs->setDebugOutputPath(true, false, String(dumpDir) + "/");
				}
				else
				{
					pbs->setDebugOutputPath(false, false);	// as above
				}
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
		//! the vertex array object + buffer a deviceless boot parks forever
		//! (@see reserveDevicelessVaoName)
		Ogre::VertexArrayObject* gDevicelessVao = NULL;
		Ogre::VertexBufferPacked* gDevicelessVaoBuffer = NULL;

		//! @brief park one live vertex array object so no later one is named 0
		//! @remarks The deviceless render system names a vertex array object
		//! after the COUNT of live ones, so the first one it hands out is
		//! named zero - and zero is the render queue's reserved "no vao bound
		//! yet" value, which a debug build asserts on at the first draw call.
		//! Keeping one object alive from boot moves every real name to one or
		//! above; the parked one is never attached to anything and is released
		//! with the render system. Costs one vertex of memory, and only in a
		//! run that already asked for no GPU.
		void reserveDevicelessVaoName(Ogre::VaoManager* vaoManager)
		{
			if(!vaoManager || gDevicelessVao)
			{
				return;
			}
			Ogre::VertexElement2Vec elements;
			elements.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT3,
				Ogre::VES_POSITION));
			const float vertex[3] = { 0.0f, 0.0f, 0.0f };
			gDevicelessVaoBuffer = vaoManager->createVertexBuffer(elements,
				1u /*numVertices*/, Ogre::BT_DEFAULT,
				const_cast<float*>(vertex), false /*keepAsShadow*/);
			Ogre::VertexBufferPackedVec buffers;
			buffers.push_back(gDevicelessVaoBuffer);
			gDevicelessVao = vaoManager->createVertexArrayObject(buffers,
				NULL /*indexBuffer*/, Ogre::OT_TRIANGLE_LIST);
		}

		//! @brief release the parked vertex array object (@see above)
		void releaseDevicelessVaoName(Ogre::VaoManager* vaoManager)
		{
			if(!vaoManager)
			{
				gDevicelessVao = NULL;
				gDevicelessVaoBuffer = NULL;
				return;
			}
			if(gDevicelessVao)
			{
				vaoManager->destroyVertexArrayObject(gDevicelessVao);
				gDevicelessVao = NULL;
			}
			if(gDevicelessVaoBuffer)
			{
				vaoManager->destroyVertexBuffer(gDevicelessVaoBuffer);
				gDevicelessVaoBuffer = NULL;
			}
		}

		//! Pbs templates registered above.
		void registerAtmosphereMedia(String const & mediaRoot)
		{
			gAtmosphereMediaAvailable = false;
			if(mediaRoot.empty())
			{
				return;
			}
			// DEVICELESS: this media is `.material`/`.program` SCRIPTS - the
			// low-level shader tier. A render system with no device has no
			// GPU program manager to parse them into, and nothing they define
			// could run, so the sky media stays unregistered and setAtmosphere
			// keeps its honest media-less no-op.
			if(RenderSystemSelection::devicelessRequested())
			{
				return;
			}
			String root = mediaRoot;
			if(root.back() != '/')
			{
				root += '/';
			}
			const String atmosphereDir = root + "Atmosphere";
			// error_code overload for the same reason as the Hlms probe above:
			// an unreadable media root degrades to "no sky", never a throw
			std::error_code atmosphereProbeError;
			if(!std::filesystem::exists(atmosphereDir, atmosphereProbeError))
			{
				return;	// media root without the atmosphere media
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
		// the DEVICELESS boot (@see engine_render/RenderSystemSelection.h): a
		// process that asked for no window and no GPU installs the deviceless
		// render system INSTEAD of the platform's graphics one. Everything
		// below stays the one boot sequence - the deviceless window, its Vao
		// manager and its texture manager satisfy the same interfaces, so no
		// call site downstream learns which render system it got.
		const bool deviceless = RenderSystemSelection::devicelessRequested();
		if(deviceless)
		{
			gRenderSystemPlugin = OGRE_NEW Ogre::NULLPlugin();
		}
		else
		{
#if defined(__APPLE__)
			gRenderSystemPlugin = OGRE_NEW Ogre::MetalPlugin();
#else
			// TODO(linux): authored against the Ogre-Next 3.0 sources, first
			// real Linux run pending (verified in CI - see .github/workflows)
			gRenderSystemPlugin = OGRE_NEW Ogre::VulkanPlugin();
#endif
		}
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
		if(deviceless)
		{
			reserveDevicelessVaoName(
				root->getRenderSystem()->getVaoManager());
			// The deviceless window's colour texture reports itself as
			// window-specific but answers no "Window" custom attribute, so
			// the depth-buffer lookup for a pass targeting it would read an
			// UNINITIALISED window pointer. Declare the window depth-less
			// instead: the lookup returns "no depth buffer" before it ever
			// asks, and a run that draws nothing has no use for one. Render
			// textures are unaffected - they are not window-specific and take
			// the ordinary pooled path.
			window->getTexture()->_setDepthBufferDefaults(
				Ogre::DepthBuffer::POOL_NO_DEPTH, false /*preferDepthTexture*/,
				Ogre::PFG_UNKNOWN);
		}

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
		// refraction ships on BOTH flavors now: this flavor renders it through the
		// HlmsPbs Refractive transparency mode fed by a compositor scene-colour+
		// depth split (@see createOrUpdateWaterDatablock + recreateWindowWorkspace);
		// the desktop-capable Metal/Vulkan render targets carry the split
		// unconditionally, so the bit is set here like Bloom (default water stays
		// Transparent, so the pass structure is byte-stable until a scene opts in).
		system->mImpl->caps =
			(1u << static_cast<int>(RenderCaps::SkyDome)) |
			(1u << static_cast<int>(RenderCaps::DynamicShadows)) |
			(1u << static_cast<int>(RenderCaps::HemisphereAmbient)) |
			(1u << static_cast<int>(RenderCaps::SunExposureLinkage)) |
			(1u << static_cast<int>(RenderCaps::AnimatedNormalMappedWater)) |
			(1u << static_cast<int>(RenderCaps::OffscreenOwnedLayers)) |
			(1u << static_cast<int>(RenderCaps::ProjectedDecals)) |
			(1u << static_cast<int>(RenderCaps::IblReflections)) |
			(1u << static_cast<int>(RenderCaps::ScreenSpaceRefraction)) |
			// PlanarReflection: the native Ogre::PlanarReflections subsystem
			// renders the mirror RTT with its mip chain (the hand-built
			// workspace's PASS_MIPMAP), and the planar-on water material
			// compensates HlmsPbs's physically-attenuated env term (specular
			// colour x env BRDF x roughness-mip blur) so the mirrored scene
			// reads at the classic mirror-paint's strength - probe-verified by
			// water_reflection_looks_right on this flavor. Diagnose with
			// ORKIGE_DUMP_MIRROR=<png> (@see updatePlanarReflections).
			(1u << static_cast<int>(RenderCaps::PlanarReflection)) |
			// OutputGrade: the CompositorManager2 grade quad after the scene pass
			// (and after the bloom combine when both are on); the desktop/mobile
			// render targets carry the off-screen scene texture the quad samples,
			// so the bit is set here like Bloom (default grade is OFF, so the pass
			// structure is byte-stable until a scene opts in).
			(1u << static_cast<int>(RenderCaps::OutputGrade)) |
			// SceneUnlitView: the editor Scene view's flat lighting-off inspection
			// look (@see RenderWorld::setLightingSuppressed) - a GLOBAL per-frame
			// state (snapshot + hide every light + flat-white ambient, exact
			// restore) armed only on a Scene-only frame via the dock-tab visibility
			// rule; available here (a per-TARGET route is impossible on next, so the
			// editor uses the global path).
			(1u << static_cast<int>(RenderCaps::SceneUnlitView)) |
			// SceneWireframeView: the editor Scene view's WIREFRAME look (@see
			// RenderWorld::setSceneWireframe). Ogre-Next bakes polygon mode into the
			// PSO with no per-target override, so this is the SAME global-under-the-
			// invariant road as SceneUnlitView: the 3D-scene datablock set (DT_SCENE)
			// flips to line-fill while the 2D/UI set (the editor's own ImGui chrome +
			// gui/sprites) stays solid, armed only on a Scene-only frame so it never
			// leaks into the Game Preview / Play. classic does it per-target instead.
			(1u << static_cast<int>(RenderCaps::SceneWireframeView)) |
			// SceneWireframeOverlayView: the editor Scene view's SHADED+WIREFRAME
			// look (@see RenderWorld::setSceneWireframeOverlay). NOT a mid-frame
			// polygon flip (impossible to bracket per-target on this baked-PSO
			// flavor); instead a second Ogre::Item per scene mesh with a shared
			// unlit wireframe datablock, depth-biased onto the shaded surface, so
			// the shaded pass renders untouched and the overlay items add the lines.
			(1u << static_cast<int>(RenderCaps::SceneWireframeOverlayView)) |
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
		// the parked deviceless vertex array object (@see
		// reserveDevicelessVaoName) goes back to the Vao manager that made it,
		// before the root tears that manager down. A no-op on a graphics boot.
		if(root->getRenderSystem())
		{
			releaseDevicelessVaoName(root->getRenderSystem()->getVaoManager());
		}
		// the planar reflection subsystem owns reflection cameras, RTTs and
		// workspaces on the scene + compositor managers, and holds a pointer set
		// on HlmsPbs - it must die BEFORE the root tears the scene manager down
		// (same rule as the atmosphere below)
		RenderBackend::destroyPlanarReflections();
		gPlanarReflectiveWaterMaterials.clear();
		gPlanarReflectionWorkspaceDef.clear();	// its def dies with the compositor
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
		gAtmosphereSunReresolvePending = false;
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
		gWaterEnvTexture = NULL;	// dies with the root, like the chain
		gWaterDatablocks.clear();	// datablocks die with the root
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
		// the scene-item + overlay bookkeeping dies with the scene manager: the
		// companion Items were owned by the manager, the datablock by the Hlms -
		// just drop the dangling pointers so a re-init starts clean
		gSceneItems.clear();
		gWireframeOverlays.clear();
		gOverlayArmed = false;
		gOverlayFlags = 0;
		gWireframeOverlayDatablock = NULL;	// died with the root's Hlms
		gSceneDatablocks.clear();	// owned by their Hlms, die with the root
		gUiDatablocks.clear();		// owned by their Hlms, die with the root
		gRetiredRTTDatablocks.clear();	// their datablocks died with the root
		gWaterAnims.clear();		// datablocks die with the root
		gRefractiveWaterMaterials.clear();	// datablocks die with the root
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
	//--- output grade (CompositorManager2 quad) -----------------------
	//---------------------------------------------------------
	bool RenderBackend::gradeSupported()
	{
		// desktop/mobile-capable Metal/Vulkan render systems all carry the RGBA8
		// off-screen render targets the grade quad samples; true unconditionally
		// (the classic GLES2/WebGL1 runtime gate lives on the other backend).
		return true;
	}
	//---------------------------------------------------------
	void RenderBackend::ensureGradeMaterials()
	{
		if(gGradeChecked)
		{
			return;
		}
		gGradeChecked = true;
		// the grade material comes from the auto-parsed OrkigeGrade.material (the
		// host registers orkige_engine/media/grade/next before
		// initialiseResourceGroups). A media-less/headless boot has none - grade
		// then degrades to no pass (byte-identical), logged once.
		// the primary sRGB-source material gates availability; the display-source
		// sibling (the refraction path) ships in the same OrkigeGrade.material
		gGradeMaterialsAvailable = Ogre::MaterialManager::getSingleton()
			.getByName("Orkige/Grade/Apply").get() != NULL;
		if(!gGradeMaterialsAvailable)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: output grade media not registered - "
				"rendering without a grade (an enabled scene grade is ignored)");
		}
	}
	//---------------------------------------------------------
	bool RenderBackend::gradeActive()
	{
		if(!gRenderSystem)
		{
			return false;
		}
		RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
		if(!world->grade.enabled)
		{
			return false;
		}
		RenderBackend::ensureGradeMaterials();
		return gGradeMaterialsAvailable;
	}
	//---------------------------------------------------------
	void RenderBackend::applyGradeConfig()
	{
		if(!gRenderSystem)
		{
			return;
		}
		RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
		RenderBackend::ensureGradeMaterials();
		if(gGradeMaterialsAvailable)
		{
			// push the live contrast + saturation onto the grade material (the
			// grade quad pass reads its pass params)
			const GradeDesc desc = world->grade.sanitised();
			Ogre::MaterialManager & materials =
				Ogre::MaterialManager::getSingleton();
			// both source variants share the same curve params (sRGB scene/bloom
			// path + the display-source refraction path)
			for(char const * name :
				{ "Orkige/Grade/Apply", "Orkige/Grade/ApplyDisplay" })
			{
				if(Ogre::MaterialPtr grade = materials.getByName(name))
				{
					grade->load();
					Ogre::GpuProgramParametersSharedPtr params =
						grade->getTechnique(0)->getPass(0)
							->getFragmentProgramParameters();
					params->setNamedConstant("Contrast",
						Ogre::Real(desc.contrast));
					params->setNamedConstant("Saturation",
						Ogre::Real(desc.saturation));
				}
			}
		}
		// the window workspace inserts/drops the grade quad at BUILD time -
		// rebuild it so it picks the quad up / drops it. Offscreen render targets
		// never grade (byte-stable), so they are not rebuilt here.
		RenderBackend::recreateWindowWorkspace();
	}
	//--- screen-space water refraction (HlmsPbs Refractive) -----------
	//---------------------------------------------------------
	bool RenderBackend::screenSpaceRefractionSupported()
	{
		// desktop-capable Metal/Vulkan render systems carry the colour+depth
		// off-screen render targets the HlmsPbs Refractive mode reads; the flavor
		// answers true unconditionally (the RenderCaps::ScreenSpaceRefraction fill).
		// The PASS additionally needs the copy media (@see refractionActive).
		return true;
	}
	//---------------------------------------------------------
	void RenderBackend::ensureRefractionMaterials()
	{
		if(gRefractionChecked)
		{
			return;
		}
		gRefractionChecked = true;
		// the refraction copy material comes from the auto-parsed OrkigeRefraction.
		// material (shipped in the same media dir as the bloom chain, registered by
		// the host before initialiseResourceGroups). A media-less/headless boot has
		// none - refraction then stays the byte-stable Transparent look, logged once.
		gRefractionMaterialsAvailable = Ogre::MaterialManager::getSingleton()
			.getByName("Orkige/Refraction/Copy").operator bool();
		if(!gRefractionMaterialsAvailable)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: water refraction media not registered - "
				"the water renders without screen-space refraction");
		}
	}
	//---------------------------------------------------------
	bool RenderBackend::refractionActive()
	{
		if(gRefractiveWaterMaterials.empty())
		{
			return false;
		}
		RenderBackend::ensureRefractionMaterials();
		return gRefractionMaterialsAvailable;
	}
	//---------------------------------------------------------
	bool RenderBackend::isRefractiveWaterMaterial(String const & name)
	{
		return gRefractiveWaterMaterials.find(name) !=
			gRefractiveWaterMaterials.end();
	}
	//---------------------------------------------------------
	void RenderBackend::noteWaterMaterialRefractive(String const & name,
		bool refractive)
	{
		const bool activeBefore = RenderBackend::refractionActive();
		if(refractive)
		{
			gRefractiveWaterMaterials.insert(name);
		}
		else
		{
			gRefractiveWaterMaterials.erase(name);
		}
		// the refraction scene split is referenced at workspace BUILD time - a
		// transition into / out of the active state rebuilds the window workspace
		// so the split appears / disappears (the bloom/shadow-config precedent)
		if(activeBefore != RenderBackend::refractionActive())
		{
			RenderBackend::recreateWindowWorkspace();
		}
	}
	//---------------------------------------------------------
	bool RenderBackend::isPlanarReflectiveWaterMaterial(String const & name)
	{
		return gPlanarReflectiveWaterMaterials.find(name) !=
			gPlanarReflectiveWaterMaterials.end();
	}
	//---------------------------------------------------------
	//! hand-build (once) the compositor workspace DEFINITION the reflection
	//! subsystem renders each active actor's mirror through: one node whose
	//! single target renders the scene (sky through the opaque + transparent
	//! 3D content, but NOT the water surface itself - that sits in the water
	//! render queue this pass stops below) into the reflection RTT the
	//! subsystem supplies as external channel 0. Returns the definition name.
	String RenderBackend::ensurePlanarReflectionWorkspaceDef()
	{
		if(!gPlanarReflectionWorkspaceDef.empty())
		{
			return gPlanarReflectionWorkspaceDef;
		}
		oAssert(gRenderSystem);
		RenderSystem::Impl* impl = gRenderSystem->mImpl;
		Ogre::CompositorManager2* compositorManager =
			impl->root->getCompositorManager2();
		const String definitionName =
			RenderBackend::generateName("Orkige/PlanarReflectionWorkspace");
		Ogre::CompositorNodeDef* nodeDefinition =
			compositorManager->addNodeDefinition(definitionName + "/Node");
		nodeDefinition->addTextureSourceName("ReflectionRT", 0,
			Ogre::TextureDefinitionBase::TEXTURE_INPUT);
		nodeDefinition->setNumTargetPass(1);
		Ogre::CompositorTargetDef* targetDefinition =
			nodeDefinition->addTargetPass("ReflectionRT");
		// two passes: the scene render into mip 0, THEN a mipmap-generation pass
		// that fills the RTT's mip chain. The reflection texture is allocated
		// WITH a mip chain (PlanarReflections::setMaxActiveActors, mipmaps=true) so
		// the water samples it at a roughness-driven LOD for a glossy ripple
		// (HlmsPbs: perceptualRoughness * planarReflNumMips). Without this second
		// pass the mips above 0 are never populated - the water reads uninitialised
		// mip levels and the whole surface shows a uniform garbage wash instead of
		// the mirrored scene. (The upstream default PlanarReflections workspace
		// carries the same generate_mipmaps pass; our hand-built def must too.)
		targetDefinition->setNumPasses(2);
		Ogre::CompositorPassSceneDef* scenePass =
			static_cast<Ogre::CompositorPassSceneDef*>(
				targetDefinition->addPass(Ogre::PASS_SCENE));
		scenePass->setAllLoadActions(Ogre::LoadAction::Clear);
		scenePass->setAllClearColours(impl->windowBackground);
		// the sky (queue 0) + all opaque/transparent 3D content, but STOP below
		// the water queue so a reflective water surface never appears in its own
		// mirror (@see isPlanarReflectiveWaterMaterial / MeshInstance::setMaterial)
		scenePass->mFirstRQ = 0;
		scenePass->mLastRQ = RenderBackend::WATER_REFRACTION_RENDER_QUEUE;
		// generate the mip chain from the just-rendered mirror. ApiDefault uses the
		// graphics auto-mipmap path, which matches the texture's AllowAutomipmaps
		// flag (the subsystem stands up with the non-compute mipmap method, so the
		// RTT is NOT a UAV and a compute filter would not apply).
		targetDefinition->addPass(Ogre::PASS_MIPMAP);
		// no shadow node: the mirror renders the lit scene + sky without a
		// second PSSM pass (a robust first tier; shadows-in-reflections is a
		// later quality knob) - the reflection stays capability/tier honest
		Ogre::CompositorWorkspaceDef* workspaceDefinition =
			compositorManager->addWorkspaceDefinition(definitionName);
		workspaceDefinition->connectExternal(0, definitionName + "/Node", 0);
		gPlanarReflectionWorkspaceDef = definitionName;
		return gPlanarReflectionWorkspaceDef;
	}
	//---------------------------------------------------------
	//! stand the reflection subsystem up (idempotent): construct it against
	//! the world scene manager + compositor manager, allocate ONE reflection
	//! slot rendering through the workspace def above at (a capped) window
	//! resolution, and hand it to HlmsPbs so tracked water samples the mirror.
	void RenderBackend::ensurePlanarReflectionsSubsystem()
	{
		if(gPlanarReflections)
		{
			return;
		}
		oAssert(gRenderSystem);
		RenderSystem::Impl* impl = gRenderSystem->mImpl;
		Ogre::SceneManager* sceneManager =
			RenderBackend::worldSceneManager();
		Ogre::CompositorManager2* compositorManager =
			impl->root->getCompositorManager2();
		if(!sceneManager || !compositorManager)
		{
			return;
		}
		const String definitionName = ensurePlanarReflectionWorkspaceDef();
		// maxDistance: how far an actor may be from the camera and still
		// reflect - the single water plane reserves its slot, so this is a
		// generous ceiling, not a per-actor cull knob
		gPlanarReflections = new Ogre::PlanarReflections(
			sceneManager, compositorManager, 200.0f, NULL);
		// the reflection RTT resolution: the window size (quality only - the
		// HlmsPbs reflection projection is aspect-correct via the reflection
		// camera), capped so a huge window does not over-allocate; a headless/
		// zero-size window falls back to a sane square
		Ogre::uint32 width = impl->window ? impl->window->getWidth() : 0u;
		Ogre::uint32 height = impl->window ? impl->window->getHeight() : 0u;
		width = width ? std::min<Ogre::uint32>(width, 2048u) : 512u;
		height = height ? std::min<Ogre::uint32>(height, 2048u) : 512u;
		gPlanarReflectionWidth = width;
		gPlanarReflectionHeight = height;
		// crash-survivable trail: the mirror subsystem stands up MID-RUN the first
		// time a scene opts water into planar reflection (the benchmark's mirrorlake
		// arrival - lake before it is refraction-only). The RTT + internal
		// reflection workspace are allocated in setMaxActiveActors below; this crumb
		// (before) and the "subsystem up" crumb (after) bracket that allocation so a
		// hard crash names whether death fell inside it. @see gPlanarFirstUpdateLogged
		gPlanarFirstUpdateLogged = false;
		gPlanarSecondUpdateLogged = false;
		gPlanarRenderCount = 0;
		gPlanarPostSteadySkipLogged = false;
		if(Breadcrumbs::getSingletonPtr())
		{
			Breadcrumbs::getSingleton().record("planar",
				"reflection subsystem allocating (" + std::to_string(width) + "x" +
				std::to_string(height) + " mirror RTT)");
		}
		gPlanarReflections->setMaxActiveActors(1u,
			Ogre::IdString(definitionName), true /*accurate lighting*/,
			width, height, true /*mipmaps (glossy ripple)*/,
			Ogre::PFG_RGBA8_UNORM_SRGB, false /*no compute mip filter*/);
		Ogre::HlmsPbs* pbs = static_cast<Ogre::HlmsPbs*>(
			impl->root->getHlmsManager()->getHlms(Ogre::HLMS_PBS));
		pbs->setPlanarReflections(gPlanarReflections);
		gPlanarReflectionPbs = pbs;
		Ogre::LogManager::getSingleton().logMessage(
			"Orkige next backend: planar water reflection subsystem up (" +
			std::to_string(width) + "x" +
			std::to_string(height) + " mirror)");
		if(Breadcrumbs::getSingletonPtr())
		{
			Breadcrumbs::getSingleton().record("planar",
				"reflection subsystem up (RTT allocated)");
		}
		// the subsystem stands up at a scene-load boundary, in the SAME loop
		// iteration whose renderOneFrame would otherwise drive the FIRST nested
		// mirror update - the freshly created reflection workspace/RTT gets its
		// first nested use with NO completed frame between creation and use, and
		// the Windows software-Vulkan driver faults inside that first update
		// (the breadcrumb trail named the phase: "first mirror render begin",
		// never "done"; allocation itself completed). Arm the one-shot skip so
		// one full frame passes between stand-up and the first nested update -
		// the same one-frame-of-stale-mirror cost the rebuild path already pays,
		// invisible at a scene switch.
		gPlanarReflectionGuard.noteWorkspaceRebuilt();
	}
	//---------------------------------------------------------
	void RenderBackend::noteWaterMaterialPlanarReflective(String const & name,
		bool reflective, float planeHeightY, float halfSizeX, float halfSizeZ)
	{
		// the r.planarReflection gate (default ON) is the single choke point:
		// OFF treats EVERY surface as non-reflective, so the reflection subsystem
		// never stands up regardless of caller. The datablock path (@see
		// createOrUpdateWaterDatablock) reads the same gate so the surface's
		// non-mirror fallback params match. A boot-seed knob, not per-frame live.
		if(reflective &&
			!CVarManager::getSingleton().getBool("r.planarReflection", true))
		{
			reflective = false;
		}
		if(reflective)
		{
			gPlanarReflectiveWaterMaterials.insert(name);
		}
		else
		{
			gPlanarReflectiveWaterMaterials.erase(name);
		}
		const bool activeNow = !gPlanarReflectiveWaterMaterials.empty();
		if(!activeNow)
		{
			// the last reflective surface went away - tear the subsystem down
			// (restores tracked renderables, frees the cameras/RTTs/workspaces)
			RenderBackend::destroyPlanarReflections();
			return;
		}
		ensurePlanarReflectionsSubsystem();
		if(!gPlanarReflections)
		{
			return;	// headless/no scene manager - honest no-op
		}
		// the mirror plane (world Y, normal +Y) and the actor rectangle. The
		// reflection math uses the (infinite) plane; the rectangle only gates
		// frustum activation, so it is generous (and the slot is reserved) to keep
		// the single water plane always reflecting. A quaternion whose local +Z
		// maps to world +Y gives the plane its up-normal (getNormal() == zAxis()).
		const Ogre::Quaternion orientation =
			Ogre::Vector3::UNIT_Z.getRotationTo(Ogre::Vector3::UNIT_Y);
		const Ogre::Vector3 center(0.0f, planeHeightY, 0.0f);
		const Ogre::Vector2 halfSize(
			std::max(halfSizeX, 1000.0f), std::max(halfSizeZ, 1000.0f));
		const bool planeChanged =
			!gPlanarReflectionActor ||
			std::abs(planeHeightY - gPlanarReflectionPlaneY) > 1e-4f ||
			std::abs(halfSize.x - gPlanarReflectionHalfX) > 1e-4f ||
			std::abs(halfSize.y - gPlanarReflectionHalfZ) > 1e-4f;
		if(!gPlanarReflectionActor)
		{
			gPlanarReflectionActor = gPlanarReflections->addActor(
				Ogre::PlanarReflectionActor(center, halfSize, orientation));
			// priority 0 = win any contention; reserve slot 0 so this single
			// water plane never loses its reflection to distance sorting
			gPlanarReflectionActor->mActivationPriority = 0;
			gPlanarReflections->reserve(0, gPlanarReflectionActor);
		}
		else if(planeChanged)
		{
			gPlanarReflectionActor->setPlane(center, halfSize, orientation);
		}
		gPlanarReflectionPlaneY = planeHeightY;
		gPlanarReflectionHalfX = halfSize.x;
		gPlanarReflectionHalfZ = halfSize.y;
	}
	//---------------------------------------------------------
	void RenderBackend::registerPlanarReflectionItem(Ogre::Item* item,
		bool reflective)
	{
		if(!item)
		{
			return;
		}
		const bool tracked = gPlanarTrackedItems.find(item) !=
			gPlanarTrackedItems.end();
		if(reflective && gPlanarReflections)
		{
			if(tracked)
			{
				return;	// already registered - idempotent (re-applies are frequent)
			}
			// the water plane mesh is a unit XZ grid centred at its node: its
			// predominant reflection normal is local +Y, its centre local origin
			const size_t subItems = item->getNumSubItems();
			for(size_t each = 0; each < subItems; ++each)
			{
				Ogre::SubItem* subItem = item->getSubItem(each);
				if(subItem->mCustomParameter != 0)
				{
					continue;	// already tracked by something - never double-add
				}
				gPlanarReflections->addRenderable(
					Ogre::PlanarReflections::TrackedRenderable(subItem, item,
						Ogre::Vector3::UNIT_Y, Ogre::Vector3::ZERO));
			}
			gPlanarTrackedItems.insert(item);
		}
		else if(tracked && gPlanarReflections)
		{
			// the surface stopped being reflective (or is being dropped) - restore
			// its renderables so a later re-add does not trip the tracking guard
			const size_t subItems = item->getNumSubItems();
			for(size_t each = 0; each < subItems; ++each)
			{
				Ogre::SubItem* subItem = item->getSubItem(each);
				if(subItem->mCustomParameter != 0)
				{
					gPlanarReflections->removeRenderable(subItem);
				}
			}
			gPlanarTrackedItems.erase(item);
			if(gPlanarTrackedItems.empty())
			{
				// the last mirrored surface died (a scene switch destroys the
				// water while its datablock persists) - drop the subsystem so
				// the rest of the session never pays for an unused mirror
				// render each frame; a later reflective water re-stands it
				RenderBackend::destroyPlanarReflections();
			}
		}
	}
	//---------------------------------------------------------
	void RenderBackend::updatePlanarReflections()
	{
		if(!gPlanarReflections)
		{
			return;
		}
		oAssert(gRenderSystem);
		Ogre::Camera* camera =
			RenderBackend::ogreCamera(gRenderSystem->mImpl->windowCamera);
		if(!camera)
		{
			return;	// UI-only window / no scene camera - nothing to mirror
		}
		// skip the nested update for the one frame a window-workspace rebuild
		// belongs to (@see gPlanarReflectionGuard): the reflection subsystem
		// renders by nesting a workspace _update that culls the scene, and a
		// just-rebuilt window workspace has not yet reconciled that state.
		// Consumed here (after the live-subsystem/camera checks) so the single
		// skipped frame lands on a real mirror update. Debug-level, and at most
		// once per rebuild, so no per-frame spam.
		if(gPlanarReflectionGuard.consumeSkip())
		{
			oDebugMsg("render", 0, "planar reflection: skipping the nested "
				"mirror update for one frame after a window-workspace rebuild");
			// crash-survivable trail: the guard is provably never armed in the
			// steady state (a rebuild is a scene-switch/config event, not a
			// per-frame one), so the FIRST skip that lands AFTER steady-state entry
			// means a mid-tour rebuild re-armed it - the rebuild-interplay shape. A
			// one-shot crumb so a recurrence shows whether a post-steady skip
			// preceded the death (interplay) rather than a plain deep-steady render.
			if(gPlanarSecondUpdateLogged && !gPlanarPostSteadySkipLogged)
			{
				gPlanarPostSteadySkipLogged = true;
				if(Breadcrumbs::getSingletonPtr())
				{
					Breadcrumbs::getSingleton().record("planar",
						"mirror update skipped post-steady (guard armed)");
				}
			}
			return;
		}
		// begin the reflection frame, then update against the window camera: the
		// subsystem reflects a mirror camera across the actor plane and renders
		// the scene into the reflection RTT NOW (before the window workspace
		// renders and HlmsPbs samples it). Called ahead of every renderOneFrame.
		// The window camera's aspect ratio is kept current by the window workspace
		// build / resize path, so it is the mirror camera's aspect too.
		// crash-survivable trail: the FIRST real (non-skipped) mirror render on a
		// freshly stood-up subsystem is the mid-tour death spot the guard only ever
		// DELAYED (it moves this render one frame past the workspace rebuild, never
		// prevents it). Bracket that first nested update so a hard crash names it as
		// the faulting phase rather than the subsystem allocation before it. One
		// pair per stand-up (@see gPlanarFirstUpdateLogged) - never per-frame spam.
		const bool logFirstUpdate = !gPlanarFirstUpdateLogged;
		if(logFirstUpdate && Breadcrumbs::getSingletonPtr())
		{
			Breadcrumbs::getSingleton().record("planar",
				"first mirror render begin (nested reflection update)");
		}
		gPlanarReflections->beginFrame();
		gPlanarReflections->update(camera, camera->getAspectRatio());
		// crash-survivable trail: a completed mirror render is counted, and every
		// 64th one drops a crumb. The first/second markers bound steady-state
		// ENTRY but say nothing about how deep a mid-tour death fell past it - this
		// paces the trail through the whole vignette (a handful of crumbs, never
		// per-frame spam), so a recurrence's last "mirror render #N" names the
		// depth. Reset with the subsystem (@see gPlanarRenderCount).
		++gPlanarRenderCount;
		// the first handful of renders crumb INDIVIDUALLY: two CI trails have
		// now died between render #2 and #64 with the second render carrying a
		// multi-second first-use pipeline-compile stall - the suspect is a
		// further cold shader-variant compile in the renders right after it,
		// so the exact early index is the discriminating evidence
		if((gPlanarRenderCount <= 8ull || (gPlanarRenderCount % 64ull) == 0ull)
			&& Breadcrumbs::getSingletonPtr())
		{
			Breadcrumbs::getSingleton().record("planar",
				"mirror render #" + std::to_string(gPlanarRenderCount));
		}
		if(logFirstUpdate)
		{
			gPlanarFirstUpdateLogged = true;
			if(Breadcrumbs::getSingletonPtr())
			{
				Breadcrumbs::getSingleton().record("planar",
					"first mirror render done");
			}
		}
		// crash-survivable trail: the SECOND completed mirror render bounds
		// steady-state entry. If a recurrence's last crumb is "second mirror
		// render done", death fell AFTER two full mirror frames (deep steady
		// state, a live-subsystem window rebuild, or the mirrorlake-exit
		// teardown) - not on the first/second nested render itself. One crumb
		// per stand-up (@see gPlanarSecondUpdateLogged), never per-frame spam.
		else if(!gPlanarSecondUpdateLogged)
		{
			gPlanarSecondUpdateLogged = true;
			if(Breadcrumbs::getSingletonPtr())
			{
				Breadcrumbs::getSingleton().record("planar",
					"second mirror render done (steady state entered)");
			}
		}
		// diagnostics: ORKIGE_DUMP_MIRROR=<path.png> writes the mirror RTT
		// once, so a wrong-looking reflection can be inspected directly
		static bool sMirrorDumped = false;
		if(!sMirrorDumped)
		{
			if(char const * dumpPath = std::getenv("ORKIGE_DUMP_MIRROR"))
			{
				sMirrorDumped = true;
				if(Ogre::TextureGpu* mirror = gPlanarReflections->getTexture(0u))
				{
					try
					{
						Ogre::Image2 image;
						image.convertFromTexture(mirror, 0u, 0u);
						saveImageAsPng(image, dumpPath);
					}
					catch(Ogre::Exception const &)
					{
						// diagnostics only - never take the frame down
					}
				}
			}
		}
	}
	//---------------------------------------------------------
	void RenderBackend::destroyPlanarReflections()
	{
		if(!gPlanarReflections)
		{
			return;
		}
		// crash-survivable trail: teardown runs at a scene-switch frame boundary
		// (the mirrorlake exit destroys the water while the subsystem lives on)
		// and deletes GPU state - the reflection cameras, RTTs and internal
		// workspaces - that the immediately preceding frame's mirror render used.
		// This ENTRY crumb (paired with the EXIT crumb below) brackets the whole
		// teardown so a hard crash here names it as the faulting phase rather
		// than the last steady-state mirror render before it. Fired only on a
		// real teardown (an active subsystem), never per frame.
		if(Breadcrumbs::getSingletonPtr())
		{
			Breadcrumbs::getSingleton().record("planar",
				"reflection subsystem teardown begin");
		}
		// restore every tracked water renderable BEFORE the subsystem dies (they
		// outlive a mid-session teardown; resets each renderable's Hlms hash +
		// tracking parameter so a later reflective surface can re-register)
		for(Ogre::Item* item : gPlanarTrackedItems)
		{
			const size_t subItems = item->getNumSubItems();
			for(size_t each = 0; each < subItems; ++each)
			{
				Ogre::SubItem* subItem = item->getSubItem(each);
				if(subItem->mCustomParameter != 0)
				{
					gPlanarReflections->removeRenderable(subItem);
				}
			}
		}
		gPlanarTrackedItems.clear();
		if(gPlanarReflectionPbs)
		{
			gPlanarReflectionPbs->setPlanarReflections(NULL);
			gPlanarReflectionPbs = NULL;
		}
		// ~PlanarReflections destroys the actors, reflection cameras, RTTs and
		// workspaces (all on the still-live scene/compositor managers)
		delete gPlanarReflections;
		gPlanarReflections = NULL;
		gPlanarReflectionActor = NULL;
		// a later reflective water re-stands the subsystem: let that stand-up drop
		// its own mirror-render crumbs again (@see gPlanarFirstUpdateLogged /
		// gPlanarSecondUpdateLogged)
		gPlanarFirstUpdateLogged = false;
		gPlanarSecondUpdateLogged = false;
		gPlanarRenderCount = 0;
		gPlanarPostSteadySkipLogged = false;
		gPlanarReflectionPlaneY = 0.0f;
		gPlanarReflectionHalfX = 0.0f;
		gPlanarReflectionHalfZ = 0.0f;
		gPlanarReflectionWidth = 0;
		gPlanarReflectionHeight = 0;
		// the workspace DEFINITION (node + workspace def) is owned by the
		// compositor manager and reusable - keep the name so a re-activation
		// reuses it rather than leaking a fresh unique definition each time
		// crash-survivable trail: teardown completed - the EXIT crumb paired with
		// the ENTRY crumb above, so a death INSIDE delete gPlanarReflections
		// leaves "teardown begin" as the last crumb, and a death downstream of a
		// clean teardown shows this "teardown done" instead.
		if(Breadcrumbs::getSingletonPtr())
		{
			Breadcrumbs::getSingleton().record("planar",
				"reflection subsystem teardown done");
		}
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
		// this apply may have read the sun before its TransformComponent
		// oriented the node (a light registers directional ahead of its
		// transform in scene-load order - a horizon sun, a red sky). Latch a
		// re-resolve for the next frame boundary, by when the load has
		// composed every transform (@see flushAtmosphereSunReresolve)
		gAtmosphereSunReresolvePending = true;
	}
	//---------------------------------------------------------
	void RenderBackend::flushAtmosphereSunReresolve()
	{
		if(!gAtmosphereSunReresolvePending)
		{
			return;
		}
		gAtmosphereSunReresolvePending = false;
		// re-read the sun and re-arm the sky only when an atmosphere is live and
		// enabled; a disabled/absent atmosphere has no sun to resolve. applyAtmosphere
		// does not re-latch, so this runs exactly once per directional-set change.
		if(gAtmosphere && gRenderSystem &&
			gRenderSystem->getWorld()->mImpl->atmosphere.enabled)
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
		// read as sky (the classic subset is entirely this path) - EXCEPT the
		// editor's UI-ONLY window, whose clear is theme chrome, not a scene
		// backdrop (its scenes render offscreen into a RenderTexture); a scene's
		// atmosphere must never stomp that chrome colour back to sky blue
		if(!gRenderSystem->mImpl->uiOnlyWindow)
		{
			gRenderSystem->setWindowBackgroundColour(
				Color(desc.skyRed, desc.skyGreen, desc.skyBlue));
		}

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
		// independent by the desc's contract. The `sky` part switch hides the
		// visible dome/cubemap entirely (fog + sun linkage stay live): with
		// sky=false NOTHING draws over the flat sky-tint clear, whatever the type.
		const bool wantProceduralSky = desc.sky &&
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
		if(desc.sky && desc.skyType == AtmosphereSky::ST_SKYBOX)
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
		// The node's AUTHORED orientation - snapshotted CLEAN when the sun is
		// first linked (@see gLinkedSunNodeLocal) - is pinned back before the
		// direction read and after the drive, because setLight/setPreset/
		// setSunDir each run AtmosphereNpr::syncToLight, which steers the node.
		Ogre::Light* sun = RenderBackend::firstDirectionalLight();
		Ogre::Vector3 toSun(0.3f, 0.9f, 0.2f);	// default: high daytime sun
		Ogre::Node* sunNode = NULL;
		if(sun)
		{
			sunNode = sun->getParentNode();
			// restore-exactly: a sun-set change hands the PREVIOUS sun its
			// authored colour/power (and node orientation) back, then snapshots
			// the new one - CLEAN, before the atmosphere ever drives it - as the
			// direction's source of truth (@see gLinkedSunNodeLocal)
			if(gLinkedSun != sun)
			{
				restoreLinkedSun();
				gLinkedSun = sun;
				gLinkedSunDiffuse = sun->getDiffuseColour();
				gLinkedSunSpecular = sun->getSpecularColour();
				gLinkedSunPower = sun->getPowerScale();
				gLinkedSunNodeLocal = sunNode ? sunNode->getOrientation()
					: Ogre::Quaternion::IDENTITY;
			}
			else if(sunNode)
			{
				// an already-linked sun: undo the atmosphere's residual node
				// steering BEFORE reading the authored direction below, so a
				// prior sync's baked-in parent rotation never compounds
				sunNode->setOrientation(gLinkedSunNodeLocal);
			}
			// -direction points FROM the surface TOWARD the sun
			toSun = -sun->getDerivedDirectionUpdated();
		}
		else if(gLinkedSun)
		{
			// the sun left the scene: release the light we still hold
			restoreLinkedSun();
		}
		toSun.normalise();
		gAtmosphere->setLight(sun);
		// the native day/night phase from the sun's elevation: sunHeight in the
		// shader is sin(normTime * PI), so normTime = asin(elevation)/PI maps
		// overhead(+1)->0.5 (noon) and horizon(0)->0. A BELOW-horizon sun is
		// parked NEAR the horizon for the sky model: the model's haze weight
		// is exp2(-1/sunHeight) and its light-linkage terms divide by the sun
		// elevation, so both a small negative elevation (a just-set sun
		// whited the whole night sky out) AND an exact 0 (the linked
		// sun/ambient colours clamp to a saturated blue that floods every lit
		// surface once the sun sets - the night terrain wash) explode. A
		// hair above the horizon keeps the model finite; the night look
		// comes from the night PRESET's dark power/tint, while the linked
		// light keeps its true below-horizon direction
		const float elevation =
			std::max(0.02f, std::min(1.0f, static_cast<float>(toSun.y)));
		const float normTime = std::asin(elevation) /
			static_cast<float>(Ogre::Math::PI);

		// twilight fade: with the model parked just above the horizon, its
		// light density (~1/elevation) would keep the DOME at full sunset
		// blaze all night. The true (un-parked) elevation drives a short
		// fade-to-dark as the sun sinks below the horizon, so dusk ends and
		// night actually darkens while the presets keep shaping the colours.
		const float duskFade = std::max(0.0f, std::min(1.0f,
			(static_cast<float>(toSun.y) + 0.12f) / 0.12f));

		Ogre::AtmosphereNpr::Preset preset;	// starts from the sane midday defaults
		preset.skyColour =
			Ogre::Vector3(desc.skyRed, desc.skyGreen, desc.skyBlue);
		preset.skyPower = desc.skyPower * duskFade;
		preset.densityCoeff = desc.density;
		// the `fog` part switch drops the HlmsPbs object fog while the dome +
		// sun linkage stay live (fog=false = a clear-air look, still lit/skied)
		preset.fogDensity = desc.fog ? desc.fogDensity : 0.0f;
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
			// syncToLight steered the light's node in PARENT space from the
			// world-space sun vector; pin it back to its authored orientation so
			// only the light's colour/power survive - the transform stays the
			// sun direction's single source of truth (@see gLinkedSunNodeLocal)
			sunNode->setOrientation(gLinkedSunNodeLocal);
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

		//! destroy the water mirror's ratio-true chain if one is live (always
		//! owned; the bound water datablocks are re-routed by the caller)
		void dropWaterEnvTexture()
		{
			if(gWaterEnvTexture)
			{
				Ogre::TextureGpuManager* textureManager =
					Ogre::Root::getSingleton().getRenderSystem()
						->getTextureGpuManager();
				if(!gWaterEnvTexture->isDataReady())
				{
					gWaterEnvTexture->waitForData();	// as above
				}
				textureManager->destroyTexture(gWaterEnvTexture);
			}
			gWaterEnvTexture = NULL;
		}

		//! @brief synthesize the water mirror's ratio-true environment sibling
		//! from the SAME procedural capture the clamped chain came from: the
		//! ratio-preserving scaled encode (SkyEnvMap::buildCubemapChainScaledRgba8)
		//! decoded to half-float radiance at upload (texel/255 x the capture
		//! scale), so the mirror SAMPLE is the HDR radiance directly - the
		//! per-datablock scale lane the shader would otherwise need is folded
		//! into the texels, and the numbers equal what the classic water
		//! program reconstructs in-shader (texel x skyParams.z) from the
		//! identical capture bytes. NULL on failure: the water keeps the
		//! clamped chain - the mirror hue pales, never breaks.
		Ogre::TextureGpu* buildProceduralWaterEnvTexture(
			AtmosphereDesc const & desc, Ogre::Vector3 const & toSun,
			IblPreset::Quality quality)
		{
			const unsigned int edge =
				IblPreset::forQuality(quality).chainResolution;
			if(edge == 0u)
			{
				return NULL;
			}
			std::vector<unsigned char> chain;
			unsigned int chainMips = 0u;
			float scale = 1.0f;
			SkyEnvMap::buildCubemapChainScaledRgba8(edge, desc,
				static_cast<float>(toSun.x), static_cast<float>(toSun.y),
				static_cast<float>(toSun.z), chain, chainMips, scale);
			try
			{
				Ogre::TextureGpuManager* textureManager =
					Ogre::Root::getSingleton().getRenderSystem()
						->getTextureGpuManager();
				const Ogre::PixelFormatGpu format = Ogre::PFG_RGBA16_FLOAT;
				const size_t sizeBytes =
					Ogre::PixelFormatGpuUtils::calculateSizeBytes(edge, edge,
						1u, 6u, format,
						static_cast<Ogre::uint8>(chainMips), 4u);
				if(sizeBytes != chain.size() * 2u)
				{
					// RGBA16F texels are 8 bytes (4-aligned rows for free), so
					// the half chain must be exactly twice the tight RGBA8
					// capture - a mismatch means a format/padding surprise;
					// refuse rather than upload garbage
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige next backend: water sky-mirror layout "
						"mismatch - skipping capture");
					return NULL;
				}
				Ogre::uint16* data = reinterpret_cast<Ogre::uint16*>(
					OGRE_MALLOC_SIMD(sizeBytes, Ogre::MEMCATEGORY_RESOURCE));
				const Ogre::uint16 halfOne = Ogre::Bitwise::floatToHalf(1.0f);
				for(size_t i = 0; i < chain.size(); i += 4u)
				{
					data[i] = Ogre::Bitwise::floatToHalf(
						chain[i] * (scale / 255.0f));
					data[i + 1u] = Ogre::Bitwise::floatToHalf(
						chain[i + 1u] * (scale / 255.0f));
					data[i + 2u] = Ogre::Bitwise::floatToHalf(
						chain[i + 2u] * (scale / 255.0f));
					data[i + 3u] = halfOne;
				}
				Ogre::Image2 image;
				image.loadDynamicImage(data, edge, edge, 6u,
					Ogre::TextureTypes::TypeCube, format, true /*autoDelete*/,
					static_cast<Ogre::uint8>(chainMips));
				Ogre::TextureGpu* texture = textureManager->createTexture(
					kWaterEnvTexture, Ogre::GpuPageOutStrategy::Discard,
					Ogre::TextureFlags::ManualTexture,
					Ogre::TextureTypes::TypeCube);
				texture->setResolution(edge, edge);
				texture->setPixelFormat(format);
				texture->setNumMipmaps(static_cast<Ogre::uint8>(chainMips));
				texture->scheduleTransitionTo(Ogre::GpuResidency::Resident);
				texture->waitForData();
				image.uploadTo(texture, 0u,
					static_cast<Ogre::uint8>(chainMips - 1u));
				return texture;
			}
			catch(Ogre::Exception const & e)
			{
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: water sky-mirror capture failed "
					"(the water reflects the clamped environment chain): " +
					e.getDescription());
				return NULL;
			}
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
				// (restore-exactly: an untouched datablock is a no-op write).
				// Every PBS datablock lives in the scene set (the UI set is all
				// unlit), so this is the whole image-lighting consumer set.
				for(Ogre::HlmsDatablock* each : gSceneDatablocks)
				{
					if(each->getCreator()->getType() == Ogre::HLMS_PBS)
					{
						static_cast<Ogre::HlmsPbsDatablock*>(each)->setTexture(
							Ogre::PBSM_REFLECTION,
							static_cast<Ogre::TextureGpu*>(NULL));
					}
				}
				dropOwnedIblChainTexture();
				dropWaterEnvTexture();
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
			Ogre::Light* sun = RenderBackend::firstDirectionalLight();
			if(sun)
			{
				toSun = -sun->getDerivedDirectionUpdated();
			}
			else if(gIblActive && gProceduralIblHasKey)
			{
				// no directional light RIGHT NOW (a transient state: scene
				// teardown, a probe that blacked the sun out) but a capture of
				// the real sky exists - the sky itself has not moved, so KEEP
				// the capture instead of re-lighting the environment from the
				// default sun (that wrong-sun recapture visibly re-colours the
				// bound reflections; the classic backend keeps its capture in
				// the same state)
				toSun = Ogre::Vector3(gProceduralIblKey.sunX,
					gProceduralIblKey.sunY, gProceduralIblKey.sunZ);
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
				// the water mirror's ratio-true sibling rides the SAME
				// recapture cadence - one capture event, two encodes of one
				// sky (the classic backend does exactly this)
				dropWaterEnvTexture();
				gWaterEnvTexture = buildProceduralWaterEnvTexture(
					world->atmosphere, toSun, world->iblQuality);
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
				// an authored skybox is LDR content: the mirror samples the
				// clamped chain as stored (capture scale 1), exactly like the
				// classic water's skybox path - no ratio-true sibling
				dropWaterEnvTexture();
			}
			gProceduralIblHasKey = false;	// not a procedural capture now
		}
		gIblActive = true;
		// bind the chain to every generated PBS datablock (surface + water);
		// datablocks created later register through registerContentDatablock,
		// which routes them here via applyImageLightingToDatablock. Every PBS
		// datablock lives in the scene set (the UI set is all unlit).
		for(Ogre::HlmsDatablock* each : gSceneDatablocks)
		{
			RenderBackend::applyImageLightingToDatablock(each);
		}
		// re-push the scene ambient so its envMapScale lane picks the fresh
		// image-lighting intensity up: the scale rides the ambient write's
		// alpha, and the LAST ambient write may predate this activation (a
		// stale scale of 1.0 rendered the fill at full native strength no
		// matter the authored intensity)
		if(RenderWorld* world = gRenderSystem->getWorld())
		{
			world->setAmbientHemisphere(world->getAmbientHemisphereUpper(),
				world->getAmbientHemisphereLower());
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
		// water datablocks mirror the sky RATIO-TRUE (the HDR sibling of the
		// one capture) while every other PBS consumer keeps the calibrated
		// clamped chain; without a live sibling (skybox source / capture
		// failure) the water falls back to the clamped chain like the rest
		Ogre::TextureGpu* chain = gIblTexture;
		if(gWaterEnvTexture &&
			gWaterDatablocks.find(datablock) != gWaterDatablocks.end())
		{
			chain = gWaterEnvTexture;
		}
		static_cast<Ogre::HlmsPbsDatablock*>(datablock)->setTexture(
			Ogre::PBSM_REFLECTION, chain);
	}
	//---------------------------------------------------------
	float RenderBackend::imageLightingEnvmapScale()
	{
		if(!gIblActive || !gRenderSystem)
		{
			return 1.0f;
		}
		// the effective env scale: authored intensity x the shared fill
		// weight (@see IblPreset::fillScale - the one number BOTH flavors'
		// env consumers carry; the classic image-lighting stage runs this
		// backend's exact env term at this exact scale, so the flavors match
		// by formula, not by calibration)
		return gRenderSystem->getWorld()->mImpl->iblIntensity *
			IblPreset::fillScale();
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
		// screen-space water refraction (@see createOrUpdateWaterDatablock): when a
		// scene enabled it on a water surface the window node splits into an OPAQUE
		// scene pass into a colour(+depth) SceneRT, then a SECOND scene pass
		// rendering ONLY the refractive water (its own render queue) that samples
		// the captured SceneRT colour+depth (HlmsPbs Refractive, setUseRefractions)
		// so what sits under the surface bends, then a full-screen COPY resolving
		// SceneRT onto the window. Refraction off -> the byte-identical structure
		// below. (Refraction takes precedence over bloom for now: a scene that asks
		// for both renders refraction without bloom - a v1 limitation, the two
		// compositor splits are not yet nested.)
		// the output grade (@see RenderBackend::applyGradeConfig): the shared
		// look stage - a single grade quad appended AFTER the 3D result (and
		// after the bloom combine when both are on), LAST before the 2D/UI pass.
		// It composes with the plain path, with bloom (its own branch runs the
		// bloom chain internally), and with refraction (the refraction path's
		// final copy becomes a display-source grade). Grade off -> the
		// byte-identical structure below.
		const bool useRefraction = !impl->uiOnlyWindow && backendCamera &&
			RenderBackend::refractionActive();
		const bool useGrade = !impl->uiOnlyWindow && backendCamera &&
			RenderBackend::gradeActive();
		// the grade branch owns the bloom composition when both are on, so the
		// standalone bloom branch only runs when grade is off
		const bool useBloom = !impl->uiOnlyWindow && backendCamera &&
			!useRefraction && !useGrade && RenderBackend::bloomActive();
		if(useRefraction)
		{
			// Three textures, chosen so NO resource is read and written in the same
			// pass (Metal returns garbage for such feedback):
			//   SceneRT   - the opaque scene colour the water refracts (SAMPLED by
			//               the water pass, never written by it). NON-sRGB, so the
			//               final copy to the non-sRGB window is a byte passthrough
			//               (the colour-parity rule).
			//   SceneDepth- an explicit, SAMPLEABLE depth texture: written by the
			//               opaque pass, then read-only for the water pass (its
			//               depth-test AND the refraction depth-fallback).
			//   WaterRT   - the composited target the water renders INTO (a copy of
			//               SceneRT with the refractive water on top); distinct from
			//               SceneRT so the water samples a clean source.
			for(char const * colourTex : { "SceneRT", "WaterRT" })
			{
				Ogre::TextureDefinitionBase::TextureDefinition* tex =
					nodeDefinition->addTextureDefinition(colourTex);
				tex->widthFactor = 1.0f;
				tex->heightFactor = 1.0f;
				tex->format = Ogre::PFG_RGBA8_UNORM;
			}
			Ogre::TextureDefinitionBase::TextureDefinition* depthTex =
				nodeDefinition->addTextureDefinition("SceneDepth");
			depthTex->widthFactor = 1.0f;
			depthTex->heightFactor = 1.0f;
			depthTex->format = Ogre::PFG_D32_FLOAT;
			depthTex->preferDepthTexture = true;	// sampleable depth
			// both colour targets share the one SceneDepth as their depth
			// attachment (the opaque pass fills it; the water pass tests read-only
			// against it), so the water depth-tests against the opaque scene
			for(char const * colourTex : { "SceneRT", "WaterRT" })
			{
				Ogre::RenderTargetViewDef* rtv =
					nodeDefinition->addRenderTextureView(colourTex);
				rtv->colourAttachments.push_back(Ogre::RenderTargetViewEntry());
				rtv->colourAttachments.back().textureName =
					Ogre::IdString(colourTex);
				rtv->depthAttachment.textureName = Ogre::IdString("SceneDepth");
				rtv->preferDepthTexture = true;
				rtv->depthBufferFormat = Ogre::PFG_D32_FLOAT;
			}
			// SceneRT opaque(1) + WaterRT copy+water(2) + WindowRT copy+UI(2)
			nodeDefinition->setNumTargetPass(3);
			const String shadowNode = RenderBackend::activeShadowNodeName();
			// --- the opaque scene (below the water queue) into SceneRT ---
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
				scenePass->mLastRQ = RenderBackend::WATER_REFRACTION_RENDER_QUEUE;
				if(!shadowNode.empty())
				{
					scenePass->mShadowNode = Ogre::IdString(shadowNode);
				}
			}
			// --- copy the opaque scene into WaterRT, then the refractive water
			//     ONLY on top (sampling the untouched SceneRT + read-only depth) ---
			{
				Ogre::CompositorTargetDef* waterTarget =
					nodeDefinition->addTargetPass("WaterRT");
				waterTarget->setNumPasses(2);
				Ogre::CompositorPassQuadDef* seedPass =
					static_cast<Ogre::CompositorPassQuadDef*>(
						waterTarget->addPass(Ogre::PASS_QUAD));
				seedPass->setAllLoadActions(Ogre::LoadAction::DontCare);
				// the depth was written by the opaque pass and must survive for the
				// water depth-test - keep it (only the colour is seeded here)
				seedPass->mLoadActionDepth = Ogre::LoadAction::Load;
				seedPass->mMaterialName = "Orkige/Refraction/Copy";
				seedPass->addQuadTextureSource(0, "SceneRT");
				Ogre::CompositorPassSceneDef* waterPass =
					static_cast<Ogre::CompositorPassSceneDef*>(
						waterTarget->addPass(Ogre::PASS_SCENE));
				waterPass->setAllLoadActions(Ogre::LoadAction::Load);
				waterPass->mFirstRQ = RenderBackend::WATER_REFRACTION_RENDER_QUEUE;
				waterPass->mLastRQ =
					RenderBackend::WATER_REFRACTION_RENDER_QUEUE + 1u;
				// the HlmsPbs Refractive datablocks read the captured scene colour
				// at a normal-perturbed screen UV + the scene depth (the fallback
				// when the refracted pixel is in front); read-only depth preserves
				// the opaque depth this pass still tests against
				waterPass->setUseRefractions(Ogre::IdString("SceneDepth"),
					Ogre::IdString("SceneRT"));
			}
			// --- copy WaterRT onto the window, then the GUI / 2D layers on top ---
			{
				Ogre::CompositorTargetDef* windowTarget =
					nodeDefinition->addTargetPass("WindowRT");
				windowTarget->setNumPasses(2);
				Ogre::CompositorPassQuadDef* copyPass =
					static_cast<Ogre::CompositorPassQuadDef*>(
						windowTarget->addPass(Ogre::PASS_QUAD));
				copyPass->setAllLoadActions(Ogre::LoadAction::DontCare);
				// grade composes onto refraction: the final resolve of the
				// (display-space, non-sRGB) WaterRT onto the window becomes a
				// display-source grade quad instead of a plain copy - the grade is
				// still the LAST thing before the 2D/UI pass. Grade off -> the
				// byte-identical plain copy.
				copyPass->mMaterialName = useGrade
					? "Orkige/Grade/ApplyDisplay" : "Orkige/Refraction/Copy";
				copyPass->addQuadTextureSource(0, "WaterRT");
				// the GUI / 2D layers (the UI queue), un-refracted, on top - the
				// window's own 2D batches only (the same mask the plain path uses)
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
		else if(useGrade)
		{
			// the shared output grade: render the 3D scene into an off-screen
			// SceneRT, optionally run the bloom chain (grade owns the composition
			// when both are on - the grade is applied AFTER the bloom combine),
			// then a single grade quad resolves the 3D result onto the window,
			// followed by the un-graded 2D tier + GUI.
			RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
			const bool gradeBloom = RenderBackend::bloomActive();
			const BloomPreset::Settings tier =
				BloomPreset::forQuality(world->bloomQuality);
			const float downFactor = 1.0f /
				static_cast<float>(std::max(tier.downsampleFactor, 1));
			const int blurPasses = std::max(tier.blurPasses, 1);
			// full-res sRGB scene target (so the scene encodes to display on
			// store, like the bloom path); the grade shader samples it and
			// applies the curve in display space (@see GradeApply_ps.glsl)
			Ogre::TextureDefinitionBase::TextureDefinition* sceneTex =
				nodeDefinition->addTextureDefinition("SceneRT");
			sceneTex->widthFactor = 1.0f;
			sceneTex->heightFactor = 1.0f;
			sceneTex->format = Ogre::PFG_RGBA8_UNORM_SRGB;
			nodeDefinition->addRenderTextureView("SceneRT")
				->setForTextureDefinition("SceneRT", sceneTex);
			if(gradeBloom)
			{
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
				// the full-res combined (scene + glow) source the grade reads -
				// distinct from SceneRT so the combine never reads and writes one
				// target
				Ogre::TextureDefinitionBase::TextureDefinition* postTex =
					nodeDefinition->addTextureDefinition("PostRT");
				postTex->widthFactor = 1.0f;
				postTex->heightFactor = 1.0f;
				postTex->format = Ogre::PFG_RGBA8_UNORM_SRGB;
				nodeDefinition->addRenderTextureView("PostRT")
					->setForTextureDefinition("PostRT", postTex);
			}
			// SceneRT(1) + WindowRT(1) [+ bright(1) + 2*blur + PostRT(1) if bloom]
			nodeDefinition->setNumTargetPass(
				gradeBloom ? 4 + 2 * blurPasses : 2);
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
				scenePass->setVisibilityMask(~RenderBackend::SCENE_2D_VISIBILITY);
				const String shadowNode = RenderBackend::activeShadowNodeName();
				if(!shadowNode.empty())
				{
					scenePass->mShadowNode = Ogre::IdString(shadowNode);
				}
			}
			if(gradeBloom)
			{
				// bright-pass: SceneRT -> BloomA
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
				// separable gaussian blur, ping-ponging A<->B
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
				// additive combine (scene + glow) into PostRT (the grade source)
				{
					Ogre::CompositorTargetDef* postTarget =
						nodeDefinition->addTargetPass("PostRT");
					postTarget->setNumPasses(1);
					Ogre::CompositorPassQuadDef* combinePass =
						static_cast<Ogre::CompositorPassQuadDef*>(
							postTarget->addPass(Ogre::PASS_QUAD));
					combinePass->setAllLoadActions(Ogre::LoadAction::DontCare);
					combinePass->mMaterialName = "Orkige/Bloom/Combine";
					combinePass->addQuadTextureSource(0, "SceneRT");
					combinePass->addQuadTextureSource(1, "BloomA");
				}
			}
			// --- grade the 3D result onto WindowRT, then 2D + GUI un-graded ---
			{
				Ogre::CompositorTargetDef* windowTarget =
					nodeDefinition->addTargetPass("WindowRT");
				windowTarget->setNumPasses(3);
				Ogre::CompositorPassQuadDef* gradePass =
					static_cast<Ogre::CompositorPassQuadDef*>(
						windowTarget->addPass(Ogre::PASS_QUAD));
				gradePass->setAllLoadActions(Ogre::LoadAction::DontCare);
				gradePass->mMaterialName = "Orkige/Grade/Apply";
				gradePass->addQuadTextureSource(0,
					gradeBloom ? "PostRT" : "SceneRT");
				// the 2D tier (sprites/vector meshes) un-graded, on top
				Ogre::CompositorPassSceneDef* twoDPass =
					static_cast<Ogre::CompositorPassSceneDef*>(
						windowTarget->addPass(Ogre::PASS_SCENE));
				twoDPass->setAllLoadActions(Ogre::LoadAction::Load);
				twoDPass->mFirstRQ = 0;
				twoDPass->mLastRQ = RenderBackend::DRAWLAYER2D_RENDER_QUEUE;
				twoDPass->setVisibilityMask(RenderBackend::SCENE_2D_VISIBILITY);
				// the GUI / 2D layers, un-graded, on top
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
		else if(useBloom)
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
		// drive the planar water reflection from inside this workspace's update
		// (after updateSceneGraph, before clearFrameData) - @see PlanarReflectionUpdater
		impl->workspace->addListener(&gPlanarReflectionUpdater);
		// this rebuild swapped the window workspace out from under the nested
		// reflection update: suppress that update for the ONE frame this rebuild
		// belongs to so it never renders the mirror against a just-rebuilt
		// workspace's not-yet-reconciled cull state (a mid-play switch into a
		// refractive-water scene rebuilds here). One frame of stale mirror,
		// hidden by the scene-switch wipe; the mirror resumes next frame. Inert
		// in the steady state - a rebuild is a scene-switch/config event, never
		// a per-frame one. @see PlanarReflectionGuard
		gPlanarReflectionGuard.noteWorkspaceRebuilt();
		// crash-survivable trail: a window-workspace rebuild WHILE the planar
		// reflection subsystem is live is the mirrorlake-interplay shape - the
		// window compositor that consumes the mirror texture is torn down and
		// rebuilt (a refractive/grade/bloom pass-structure flip, or a resize)
		// with the reflection RTT, cameras and internal workspaces still alive
		// and freshly rendered the prior frame. This crumb fires only on that
		// coincidence (an active subsystem), so a death here names the rebuild
		// interplay rather than a plain steady-state render. Never per-frame -
		// a rebuild is a scene-switch/config event.
		if(gPlanarReflections && Breadcrumbs::getSingletonPtr())
		{
			Breadcrumbs::getSingleton().record("planar",
				"window workspace rebuilt while reflection subsystem live");
		}
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
		// .dds, ASTC/ETC2 ride the native .oitd
		// (@see tools/exporter/ExportTextureCook.h)
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
		// sprites are the 2D tier - never wireframed
		RenderBackend::registerContentDatablock(datablock, DT_UI);
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
		// vertex-colour unlit backs vector shapes + dynamic lines - the 2D tier
		RenderBackend::registerContentDatablock(datablock, DT_UI);
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
			// ALREADY REALIZED: this very datablock was last built completely
			// from an equal description, so every assignment below would write
			// back what is already there. Not merely wasted - each setter
			// flushes the datablock's linked renderables, so re-applying it
			// once per instance costs O(instances^2) on a scene where many
			// meshes share one material (@see RenderMaterialCache). An edited
			// asset parses to a different description and still rebuilds.
			if(!RenderMaterialCache::shared().needsBuild(name, datablock, desc))
			{
				return datablock;
			}
		}
		else
		{
			Ogre::HlmsPbs* pbs = static_cast<Ogre::HlmsPbs*>(
				hlmsManager->getHlms(Ogre::HLMS_PBS));
			datablock = static_cast<Ogre::HlmsPbsDatablock*>(pbs->createDatablock(
				name, name, Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(),
				Ogre::HlmsParamVec()));
			// a PBS surface material is 3D scene geometry - the wireframe target
			RenderBackend::registerContentDatablock(datablock, DT_SCENE);
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
			// a createTexture2D-uploaded texture (an emissive/albedo map handed in
			// as raw pixels, e.g. a generated pattern) has NO resource-group entry
			// but DOES exist as a backend texture - the resource-group gate would
			// reject it. Only when the name is NOT in a group do we try the backend
			// texture (loadTexture2D binds it), so the file-texture path stays
			// byte-identical (loadTexture2D already finds a resident texture first).
			const bool inGroup =
				resourceGroups.resourceExistsInAnyGroup(resolvedName);
			Ogre::TextureGpu* backendTexture =
				(!inGroup && binding.viaLoadTexture2D)
					? gRenderSystem->mImpl->root->getRenderSystem()
						->getTextureGpuManager()->findTextureNoThrow(
							binding.textureName)
					: NULL;
			if(!inGroup && !backendTexture)
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
				Ogre::TextureGpu* texture = backendTexture
					? backendTexture
					: RenderBackend::loadTexture2D(resolvedName);
				datablock->setTexture(binding.slot, texture, &samplerblock);
				outComplete = outComplete && texture != NULL;
			}
			else
			{
				datablock->setTexture(binding.slot, resolvedName, &samplerblock);
			}
		}
		// remember a COMPLETE build only: a description whose texture is
		// missing may resolve once the map arrives, so that one stays
		// un-memoized and honestly retries (@see RenderMaterialCache)
		if(outComplete)
		{
			RenderMaterialCache::shared().recordBuilt(name, datablock, desc);
		}
		else
		{
			RenderMaterialCache::shared().forget(name);
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
			// the water surface is 3D scene geometry - the wireframe target
			RenderBackend::registerContentDatablock(datablock, DT_SCENE);
		}
		// water identity first, then re-apply the reflection bind: the water
		// mirror samples the ratio-true HDR sibling of the environment chain
		// while every other consumer keeps the clamped chain (@see
		// applyImageLightingToDatablock; registerContentDatablock above bound
		// the clamped chain before this datablock was known to be water)
		gWaterDatablocks.insert(datablock);
		RenderBackend::applyImageLightingToDatablock(datablock);

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
		// of a diffuse sheen. When image-based lighting is active the water
		// reflects the SKY cubemap - it joins the same PBSM_REFLECTION seam as
		// every PBS surface through registerContentDatablock below - so a lower
		// roughness now reads as a genuine glossy reflection rather than a flat
		// sheen. The sub-unit specular colour still bounds the grazing-angle
		// Schlick fresnel (which reaches 1 at the horizon no matter how small F0
		// is) so the far plane keeps reading as water, not a white sheet.
		datablock->setRoughness(0.16f);
		datablock->setSpecular(Ogre::Vector3(0.45f, 0.47f, 0.5f));
		const float scatter = 0.18f;
		datablock->setEmissive(Ogre::Vector3(desc.shallowColour.r * scatter,
			desc.shallowColour.g * scatter, desc.shallowColour.b * scatter));
		// fresnel (F0): water sits near 0.02; the knob scales the edge
		// reflectivity up from there (clamped to a plausible band)
		const float f0 = std::clamp(0.02f * std::max(desc.fresnelPower, 0.0f),
			0.0f, 0.2f);
		datablock->setFresnel(Ogre::Vector3(f0, f0, f0), false);
		// transparency: default water is Transparent (realistic, preserves the
		// fresnel edge reflection). When the surface opts into screen-space
		// refraction AND the copy media resolved, it goes Refractive instead - the
		// HlmsPbs Refractive mode samples the captured scene colour at a
		// normal-perturbed screen UV (the distortion), fed by the compositor
		// scene-colour+depth split the window workspace grows for it (@see
		// recreateWindowWorkspace). The refractive surface renders in its own
		// dedicated render queue (WATER_REFRACTION_RENDER_QUEUE); MeshInstance::
		// setMaterial reads isRefractiveWaterMaterial to place it. A media-less
		// boot keeps Transparent (byte-stable), noted once by ensureRefractionMaterials.
		bool useRefraction = desc.screenSpaceRefraction &&
			RenderBackend::screenSpaceRefractionSupported();
		// EDITOR DOWNGRADE: with a UI-only window (the editor shell) there is NO
		// window scene workspace to grow the refraction scene-split pass, so the
		// scene renders ONLY through offscreen RTTs (Scene / Game Preview / the
		// selected-camera inset) whose single-pass workspaces cannot satisfy a
		// Refractive datablock - HlmsPbs THROWS during shader generation and the
		// editor aborts. Downgrade to the byte-stable Transparent fallback look
		// (one log line); a real player window keeps full screen-space refraction.
		if(useRefraction && gRenderSystem->mImpl->uiOnlyWindow)
		{
			static bool loggedEditorRefractionDowngrade = false;
			if(!loggedEditorRefractionDowngrade)
			{
				loggedEditorRefractionDowngrade = true;
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: screen-space water refraction is "
					"downgraded to a transparent look in the editor (no window "
					"scene workspace to render the refraction pass); it renders "
					"fully in the player");
			}
			useRefraction = false;
		}
		if(useRefraction)
		{
			RenderBackend::ensureRefractionMaterials();
			useRefraction = gRefractionMaterialsAvailable;
		}
		if(useRefraction)
		{
			datablock->setTransparency(std::clamp(desc.opacity, 0.0f, 1.0f),
				Ogre::HlmsPbsDatablock::Refractive);
			// map the abstract facade strength (roughly the screen fraction the
			// surface displaces by, @see RenderWaterDesc) to HlmsPbs's knob. The
			// HlmsPbs shader divides the screen-UV offset by the view-space depth
			// SQUARED, so at the tens-of-units distances the engine's scenes sit at
			// the raw offset all but vanishes; this scale compensates so the
			// authored strength reads as a comparable bend to the classic grab-pass
			// (which has no depth falloff). Tuned against the demo lake.
			const float kNextRefractionStrengthScale = 26.0f;
			datablock->setRefractionStrength(std::max(desc.refractionStrength, 0.0f)
				* kNextRefractionStrengthScale);
		}
		else
		{
			datablock->setTransparency(std::clamp(desc.opacity, 0.0f, 1.0f),
				Ogre::HlmsPbsDatablock::Transparent);
		}
		// track the live refractive-water set (rebuilds the workspace on an
		// empty<->non-empty transition) - AFTER the datablock is otherwise ready
		RenderBackend::noteWaterMaterialRefractive(name, useRefraction);

		// planar reflection: opt-in + capability-gated (always present on this
		// desktop-capable flavor). When on, the surface shows a MIRROR of the
		// actual scene through the native Ogre::PlanarReflections subsystem
		// instead of only the sky IBL cubemap - a stronger, glossier reflection
		// so the mirrored scene reads across the surface (not just the grazing
		// fresnel band). It COMPOSES with refraction (the datablock keeps its
		// Refractive/Transparent mode above; the planar reflection replaces the
		// cubemap reflection sampled on top). Off/unsupported keeps the byte-
		// stable sky-reflection look. The subsystem + reflection actor stand up
		// in noteWaterMaterialPlanarReflective; MeshInstance::setMaterial then
		// registers the Item's renderables so HlmsPbs samples the mirror.
		// the r.planarReflection gate (default ON): OFF forces the surface
		// non-reflective so the reflection subsystem never stands up (a boot-
		// seed quarantine for a host whose driver faults on the mirror path -
		// @see noteWaterMaterialPlanarReflective).
		const bool usePlanarReflection = desc.planarReflection &&
			CVarManager::getSingleton().getBool("r.planarReflection", true) &&
			RenderBackend::system() &&
			RenderBackend::system()->supports(RenderCaps::PlanarReflection);
		if(usePlanarReflection)
		{
			// reflectionStrength (0..1) scales the reflection's BASE reflectivity
			// on top of the physical water F0 - the mirror stays FRESNEL-modulated
			// (Schlick reaches 1 at grazing regardless), so looking DOWN shows the
			// water body/refracted scene and looking ACROSS shows the mirrored
			// scene: the surface reads as water, never as chrome. A higher
			// strength also DIMS the body albedo moderately (real water is
			// reflection-forward - its own albedo is tiny; the authored deep
			// colour is an artistic stand-in), so the mirrored scene stays
			// readable over a bright lit body without the old chrome look. The
			// SAME formulas drive the classic flavor's water program - keep the
			// two in lockstep (@see RenderSystemClassic
			// createOrUpdateWaterMaterial / applyReflectionParams).
			const float strength = std::clamp(desc.reflectionStrength, 0.0f, 1.0f);
			const float mirrorF0 = std::clamp(f0 + strength * 0.12f, 0.02f, 0.3f);
			datablock->setFresnel(
				Ogre::Vector3(mirrorF0, mirrorF0, mirrorF0), false);
			// COMPOSITION-ORDER parity with the classic mirror paint: classic
			// composes its mirror in DISPLAY space - out = mix(base, mirror, F)
			// after its sqrt display transfer - while HlmsPbs ADDS the env term
			// in LINEAR light before the same transfer. Both flavors ride the
			// SAME Schlick F (classic pushes F0 * opacity explicitly; HlmsPbs
			// premultiplies its fresnel by the transparency value), and the env
			// term here is kS * (F * envBRDF.x + envBRDF.y) * mirror
			// (envBRDF.x ~ 1 at this near-zero roughness), so kS is the one
			// angle-independent dial on the mirror's weight. Classic's
			// display-mix weight F is the target; kS sits where the water
			// reflection probe MEASURES the two flavors' mirrors equal:
			// probe-measured on_magenta over kS = 0.009 @ 0.151,
			// 0.017 @ 0.355, 0.020 @ 0.41, 0.022 @ 0.43, 0.032 @ 0.5,
			// 0.062 @ 1.0 (the response bends because the probe's per-sample
			// hue clamp gates how much of the band counts) - 0.43 lands next
			// at classic's measured 0.022 exactly. The former UNIT specular
			// was tuned against the pre-opacity-scaled classic mirror and
			// measured 3x strong once classic's fresnel took its opacity
			// scale. Constant-kS keeps the authored strength/opacity response
			// in lockstep: both flavors' mirror weights stay proportional to
			// the same premultiplied F.
			const float kMirrorSpecular = 0.43f;
			datablock->setSpecular(Ogre::Vector3(
				kMirrorSpecular, kMirrorSpecular, kMirrorSpecular));
			// the body under the mirror: classic's base carries the display
			// weight (1 - 0.35 * strength) * (1 - F) - the lockstep body dim
			// times the mix's own (1-F) attenuation. The body IS the dominant
			// term where the mirror is weak (the down-look), so there the sqrt
			// transfer maps a linear factor to its square root on screen -
			// next dims its LINEAR albedo (and the scatter emissive riding in
			// that base) by the SQUARE of classic's display weight, evaluated
			// at the view-independent mean fresnel Fmean = F0eff +
			// (1 - F0eff)/21 (the cosine-weighted hemisphere mean of Schlick:
			// the (1-cos)^5 lobe integrates to 1/21). The mirrored scene, not
			// the body glow, carries the surface; the fresnel gate still keeps
			// the look water, not chrome.
			// the refracted share is dimmed toward classic's compose:
			// classic's paint carries its scene share pre-dimmed AND renders
			// the underwater scene darker, while the Refractive mode
			// composes the captured scene at the full (1 - opacity) - at the
			// benchmark's bright sandy bed that share alone outshines the
			// whole paint. The factor is CALIBRATED, not derived: it covers
			// classic's own dim and the brighter captured content in one
			// measured number (the mirror scene's deep water band, classic
			// 48 against 95 undimmed, 57 at this value - the residual is the
			// sky-dome hue seam, not brightness). HlmsPbs premultiplies its
			// fresnel by this alpha, so the effective F0 below reads the
			// SAME value and the mirror math stays coherent
			const float kRefractedShareDim = 0.30f;
			const float alphaEff = 1.0f -
				(1.0f - std::clamp(desc.opacity, 0.0f, 1.0f)) * kRefractedShareDim;
			datablock->setTransparency(alphaEff, useRefraction
				? Ogre::HlmsPbsDatablock::Refractive
				: Ogre::HlmsPbsDatablock::Transparent);
			const float effectiveF0 = mirrorF0 * alphaEff;
			const float meanFresnel =
				effectiveF0 + (1.0f - effectiveF0) / 21.0f;
			const float classicBodyWeight =
				(1.0f - strength * 0.35f) * (1.0f - meanFresnel);
			const float albedoScale = classicBodyWeight * classicBodyWeight;
			datablock->setDiffuse(Ogre::Vector3(desc.deepColour.r * albedoScale,
				desc.deepColour.g * albedoScale,
				desc.deepColour.b * albedoScale));
			datablock->setEmissive(Ogre::Vector3(
				desc.shallowColour.r * scatter * albedoScale,
				desc.shallowColour.g * scatter * albedoScale,
				desc.shallowColour.b * scatter * albedoScale));
			// roughness stays the shared water value (@see the base branch
			// above and classic's kWaterMirrorRoughness): it shapes the SUN
			// GLINT, and narrowing it for the mirror's sake collapses the
			// GGX lobe below the pixel grid - the streak vanishes while the
			// lake vignette (no mirror) keeps it. The mirror's own sharpness
			// is a BAKED sample LOD in the overridden piece below, so the
			// two jobs roughness would otherwise do are split
		}
		// stand up / tear down the reflection subsystem for this surface (world Y
		// = the mirror plane; extents unknown here, the actor is generous +
		// slot-reserved so a near-origin water plane always reflects)
		RenderBackend::noteWaterMaterialPlanarReflective(name, usePlanarReflection,
			desc.planeHeightY, 0.0f, 0.0f);

		// MIRROR RIPPLE: Ogre's native planar reflection samples the mirror at
		// the fragment's FLAT screen-projected position - the ripple normal only
		// gates the plane distance/facing, never the sample UV, so the reflected
		// scene reads glassy no matter how the surface ripples (a flat-mirror
		// design). The classic water program wobbles its mirror by perturbing the
		// sample UV with the wave normal's horizontal slope (screenUv +
		// swellNormal.xz - @see RenderSystemClassic WaterReflect_fs); to match
		// that physical look, override the upstream DoPlanarReflectionsPS piece
		// for THIS datablock so the planar UV rides the swell + detail-normal
		// slope (a flat plane normal contributes 0 - calm water stays byte-stable)
		// and widen the harsh 20-degree normal-tilt gate that would otherwise
		// blank the mirror across wave crests. DoPlanarReflectionsPS lives in a
		// LIBRARY folder (Hlms/Pbs/Any, @see HlmsPbs::getDefaultPaths), and
		// library piece files are collected BEFORE datablock custom pieces in
		// Ogre::Hlms::compileShaderCode; collectPieces is first-definition-wins
		// and REJECTS a plain @piece redefinition ("@piece 'X' already defined"),
		// silently leaving the flat library piece in the shader on EVERY backend.
		// So the custom source @undefpiece's the library definition first (the
		// sanctioned override route - @see the Terra Hlms pieces) and then defines
		// its own; parseUndefPieces runs before collectPieces in parseCustomPiece,
		// so the slot is free when ours lands. The PixelShader piece slot is
		// independent of the swell VS piece, so the two coexist. Off/unsupported
		// clears the piece (the glassy sky-mirror look).
		if(usePlanarReflection)
		{
			// the mirror-UV distortion scale (planar UV units per unit of
			// horizontal ripple slope): calibrated so the mirrored scene wobbles
			// at the classic water program's measured strength without smearing
			// the reflection into noise. BAKED into the piece filename (like the
			// swell amplitude) so filename-equality implies content-equality -
			// Hlms throws when one custom-piece filename re-registers with
			// different content, and a fixed constant makes every registration
			// collision-free by construction.
			// ORKIGE_WATER_FLAT_MIRROR: a diagnostic seam (like ORKIGE_DUMP_MIRROR)
			// that zeroes the ripple perturbation so the planar mirror renders FLAT
			// - the water_mirror_wobble gate captures the same frame with and
			// without it and asserts the mirror region MOVES between them, a
			// wall-clock-pacing-INDEPENDENT existence check (both frames sit at the
			// identical wave phase, so only the perturbation differs; a mirror that
			// stopped rippling makes the two frames identical).
			float distort = 0.09f;
			if(std::getenv("ORKIGE_WATER_FLAT_MIRROR")) distort = 0.0f;
			// the mirror's sample sharpness, as a PERCEPTUAL-roughness-
			// equivalent mip fraction, baked into the piece instead of read
			// from pixelData.perceptualRoughness: the datablock's roughness
			// belongs to the sun glint's GGX lobe, and the mirror keeps its
			// own near-mip-0 look independently of it
			const float mirrorLod = 0.05f;
			char mirrorSource[4096];
			std::snprintf(mirrorSource, sizeof(mirrorSource),
				"@property( use_planar_reflections )\n"
				// free the piece slot the library already claimed - a plain
				// redefinition is a first-definition-wins error otherwise
				"@undefpiece( DoPlanarReflectionsPS )\n"
				"@piece( DoPlanarReflectionsPS )\n"
				"\t@property( syntax == metal || lower_gpu_overhead )\n"
				"\t\tushort planarReflectionIdx = inPs.planarReflectionIdx;\n"
				"\t@else\n"
				"\t\tuint planarReflectionIdx = worldMaterialIdx[inPs.drawId].w;\n"
				"\t@end\n"
				"\tfloat4 planarReflection = passBuf.planarReflections[planarReflectionIdx];\n"
				"\tfloat distanceToPlanarReflPlane = dot( planarReflection.xyz, inPs.pos.xyz )\n"
				"\t\t+ planarReflection.w;\n"
				"\tfloat3 pointInPlane = inPs.pos.xyz - pixelData.normal * distanceToPlanarReflPlane;\n"
				"\tfloat3 projPointInPlane = mul( float4( pointInPlane.xyz, 1.0 ),\n"
				"\t\tpassBuf.planarReflProjectionMat ).xyw;\n"
				"\tfloat2 planarReflUVs = projPointInPlane.xy / projPointInPlane.z;\n"
				// ORKIGE ripple: the wave normal's DEVIATION from the plane
				// normal shifts the mirror sample UV - the analog of the
				// classic program's screenUv + swellNormal.xz perturbation.
				// Both vectors are VIEW-space here (the plane is uploaded in
				// view space), so the deviation is exactly zero on the flat
				// plane whatever the camera's pitch - a raw normal component
				// would carry a constant sin(pitch) term that shifts the
				// whole mirror - and its x/y are screen-aligned, the axes of
				// the projected UV being perturbed
				"\tfloat3 rippleDeviation = pixelData.normal.xyz\n"
				"\t\t- planarReflection.xyz;\n"
				"\tplanarReflUVs.xy += rippleDeviation.xy * %.5ff;\n"
				"\tfloat3 planarReflectionS = OGRE_SampleLevel( planarReflectionTex,\n"
				"\t\tplanarReflectionSampler, planarReflUVs.xy,\n"
				"\t\t%.5ff * passBuf.planarReflNumMips ).xyz;\n"
				"\tfloat planarWeight = max( 1.0 - abs( distanceToPlanarReflPlane )\n"
				"\t\t* passBuf.invMaxDistanceToPlanarRefl.x, 0.0 );\n"
				"\tplanarWeight = sqrt( planarWeight );\n"
				"\tplanarWeight = smoothstep( 0.0, 1.0, planarWeight );\n"
				"\tfloat2 boundary = abs( planarReflUVs.xy - float2(0.5, 0.5) ) * 2.0;\n"
				"\tfloat fadeOnBorder = 1.0 - saturate( (boundary.x - 0.975) * 40 );\n"
				"\tfadeOnBorder *= 1.0 - saturate( (boundary.y - 0.975) * 40 );\n"
				"\tfadeOnBorder = smoothstep( 0.0, 1.0, fadeOnBorder );\n"
				"\tplanarWeight *= lerp( fadeOnBorder, 1.0,\n"
				"\t\t1.0 - min( abs(distanceToPlanarReflPlane) * 1000.0, 1.0 ) );\n"
				// ORKIGE: the upstream 20-degree tilt gate (dot*16.58-15.58)
				// zeroes the mirror where the ripple normal tilts past tolerance,
				// blanking the reflection across every wave crest; the classic
				// mirror has no such gate, so widen it to a gentle grazing rolloff
				// (fades only past ~60 degrees) - the water plane stays near-
				// horizontal and ripples only tilt it a little
				"\tplanarWeight *= saturate( dot( planarReflection.xyz,\n"
				"\t\tpixelData.normal.xyz ) * 2.0 - 1.0 );\n"
				"\t@property( hlms_use_ssr || vct_num_probes || ((envprobe_map &&\n"
				"\t\tenvprobe_map != target_envprobe_map) || parallax_correct_cubemaps) )\n"
				"\t\tpixelData.envColourS = lerp( pixelData.envColourS,\n"
				"\t\t\tplanarReflectionS, planarWeight );\n"
				"\t@else\n"
				"\t\tpixelData.envColourS += planarReflectionS * planarWeight;\n"
				"\t@end\n"
				"@end\n"
				"@end\n",
				distort, mirrorLod);
			// the mirrored surface's ambient stage, with the SPECULAR sky
			// fill suppressed: upstream's DoAmbientLighting adds the
			// hemisphere ambient onto envColourS AFTER the planar piece put
			// the mirror there - the sky is then counted twice on the water
			// (once inside the mirrored scene, once as the fill), which is a
			// large part of why this surface reads brighter than the classic
			// program, whose reflect path has no such fill. The body's
			// DIFFUSE ambient stays - the surface is still lit - and every
			// other branch reproduces the library piece verbatim, so this
			// must track that piece across a pin bump. Scoped to the
			// mirror-on water datablock alone by the custom-piece mechanism.
			static char const * const AMBIENT_NO_SKY_FILL =
				"@property( use_planar_reflections )\n"
				"@undefpiece( DoAmbientLighting )\n"
				"@piece( DoAmbientLighting )\n"
				"\t@property( ambient_sh )\n"
				"\t\t@property( vct_num_probes )\n"
				"\t\t\tif( vctSpecular.w == 0 )\n"
				"\t\t\t{\n"
				"\t\t@end\n"
				"\t\t\t\tfloat3 wsNormal = mul( passBuf.invViewMatCubemap, pixelData.normal );\n"
				"\t\t\t\twsNormal.x = -wsNormal.x;\n"
				"\t\t\t\tpixelData.envColourD += irradianceSH( wsNormal PASSBUF_ARG );\n"
				"\t\t@property( vct_num_probes )\n"
				"\t\t\t}\n"
				"\t\t@end\n"
				"\t@end\n"
				"\t@property( ambient_hemisphere )\n"
				"\t\t@property( ambient_hemisphere_inverted )\n"
				"\t\t\tfloat tmpAmbientWD = ambientWS;\n"
				"\t\t@else\n"
				"\t\t\tfloat tmpAmbientWD = ambientWD;\n"
				"\t\t@end\n"
				"\t\t@property( vct_num_probes )\n"
				"\t\t\tif( vctSpecular.w == 0 )\n"
				"\t\t\t{\n"
				"\t\t@end\n"
				"\t\t\t\tpixelData.envColourD += lerp( midf3_c( passBuf.ambientLowerHemi.xyz ),\n"
				"\t\t\t\t\t\t\t\t\t\t\t  midf3_c( passBuf.ambientUpperHemi.xyz ), tmpAmbientWD );\n"
				"\t\t@property( vct_num_probes )\n"
				"\t\t\t}\n"
				"\t\t@end\n"
				"\t@end\n"
				"\t@property( ambient_fixed && vct_num_probes )\n"
				"\t\tfinalColour += vctSpecular.w == 0 ? float3( 0, 0, 0 ) :\n"
				"\t\t\t\t\t\t\t\t\t\t(passBuf.ambientUpperHemi.xyz * pixelData.diffuse.xyz);\n"
				"\t@end\n"
				"@end\n"
				"@end\n";
			char mirrorTag[96];
			std::snprintf(mirrorTag, sizeof(mirrorTag),
				"_mirror_%.5f_%.5f_ambfill0_piece_ps.any",
				distort, mirrorLod);
			datablock->setCustomPieceCodeFromMemory(name + mirrorTag,
				String(mirrorSource) + AMBIENT_NO_SKY_FILL,
				Ogre::CustomPieceStage::PixelShader);
		}
		else
		{
			datablock->setCustomPieceCodeFromMemory(String(), String(),
				Ogre::CustomPieceStage::PixelShader);
		}

		// TWO detail normal maps carry the ripple: same tiling water normal,
		// bound to both detail slots and scrolled in different directions/
		// speeds by setWaterDatablockTime so their interference reads as moving
		// water. The detail normals go through the slot's own string setter
		// (its suggested filters run the normal-map preparation). The primary
		// weight sits ABOVE unit so the ripple relief visibly catches the
		// light like the classic flavor's full-strength lit normal map does
		// (at 1.0/0.6 this surface read notably flatter than its classic
		// sibling; far above ~1.4 the spread sparkle flattens the distinct
		// sun glint the water probe rightly demands).
		const Ogre::PbsTextureTypes detailSlots[2] =
			{ Ogre::PBSM_DETAIL0_NM, Ogre::PBSM_DETAIL1_NM };
		const float detailScales[2] = { desc.waveScale, desc.waveScale * 1.7f };
		const float detailWeights[2] = { 1.35f, 0.8f };
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

		// GEOMETRIC swell: a per-datablock custom vertex piece displaces the
		// plane's vertices with the two-sine travelling swell (the classic
		// water program runs the SAME formula/constants through its
		// waveParams push - keep them in lockstep). The amplitude bakes into
		// the piece source (a knob change re-sets the piece - rare); the
		// phase reads the pass-buffer clock the swell listener publishes.
		// waveHeight 0 clears the piece - the byte-stable flat plane.
		{
			Ogre::HlmsPbs* pbs = static_cast<Ogre::HlmsPbs*>(
				hlmsManager->getHlms(Ogre::HLMS_PBS));
			// quantized to 1e-5: the amplitude is BAKED into the piece source
			// below AND joins the piece FILENAME - Hlms keys custom pieces by
			// filename globally for the process lifetime and THROWS when one
			// filename re-registers with different content (a scene switch
			// reusing a water material name with another waveHeight, or a
			// live inspector edit). Quantizing first makes filename equality
			// imply content equality, so every registration is collision-free
			// by construction.
			const float swell = std::round(
				std::max(desc.waveHeight, 0.0f) * 100000.0f) / 100000.0f;
			if(swell > 0.0f)
			{
				// FOUR-component travelling spectrum (a pure two-sine swell
				// reads as an even lattice - real water needs incommensurate
				// wavelengths, skewed azimuths and unequal phase speeds so the
				// interference never visibly repeats). The component table is
				// LOCKSTEP with the classic water VS (WaterRefract_vs):
				//   g1 =  x*kf                    + ph        weight 0.75
				//   g2 = (z*1.3  + x*0.4 )*kf     - ph*1.7    weight 0.45
				//   g3 = (x*0.83 - z*0.62)*kf*2.17 + ph*2.3   weight 0.17
				//   g4 = (z*0.91 + x*0.47)*kf*3.71 - ph*3.1   weight 0.09
				// (weights sum ~1.46, slopes ~12%% over the old two-sine tune -
				// the calm point where the classic refraction distortion, tuned
				// on the old slopes, keeps the shore edge legible.)
				// The posExecution block re-derives the swell's analytic SLOPE
				// (the exact d/dx, d/dz of the displacement) and tilts the
				// output normal to match: without it the plane keeps its flat
				// up-normal and the displaced surface shades/reflects
				// piecewise-linearly per triangle - a sharp planar mirror
				// makes the tessellation read as visible polygons. View-space
				// via passBuf.view (pure rotation for normals), the
				// detail-normal ripple composes on top in the pixel stage.
				const float k1 = kSwellWorldFrequency;
				const float a2x = 0.4f * k1, a2z = 1.3f * k1;
				const float a3x = 0.83f * 2.17f * k1, a3z = 0.62f * 2.17f * k1;
				const float a4x = 0.47f * 3.71f * k1, a4z = 0.91f * 3.71f * k1;
				char source[2560];
				std::snprintf(source, sizeof(source),
					"@property( orkige_water_swell )\n"
					"@piece( custom_passBuffer )\n"
					"\tfloat4 orkigeWaterSwell;\n"
					"@end\n"
					"@piece( custom_vs_preTransform )\n"
					"\tfloat orkP = passBuf.orkigeWaterSwell.x;\n"
					"\tfloat orkG1 = worldPos.x * %.5ff + orkP;\n"
					"\tfloat orkG2 = worldPos.z * %.5ff + worldPos.x * %.5ff\n"
					"\t\t- orkP * 1.7f;\n"
					"\tfloat orkG3 = worldPos.x * %.5ff - worldPos.z * %.5ff\n"
					"\t\t+ orkP * 2.3f;\n"
					"\tfloat orkG4 = worldPos.z * %.5ff + worldPos.x * %.5ff\n"
					"\t\t- orkP * 3.1f;\n"
					"\tworldPos.y += %.6ff * ( 0.75f * sin( orkG1 )\n"
					"\t\t+ 0.45f * sin( orkG2 ) + 0.17f * sin( orkG3 )\n"
					"\t\t+ 0.09f * sin( orkG4 ) );\n"
					"@end\n"
					"@piece( custom_vs_posExecution )\n"
					"\tfloat orkQ = passBuf.orkigeWaterSwell.x;\n"
					"\tfloat orkH1 = worldPos.x * %.5ff + orkQ;\n"
					"\tfloat orkH2 = worldPos.z * %.5ff + worldPos.x * %.5ff\n"
					"\t\t- orkQ * 1.7f;\n"
					"\tfloat orkH3 = worldPos.x * %.5ff - worldPos.z * %.5ff\n"
					"\t\t+ orkQ * 2.3f;\n"
					"\tfloat orkH4 = worldPos.z * %.5ff + worldPos.x * %.5ff\n"
					"\t\t- orkQ * 3.1f;\n"
					"\tfloat orkDx = %.6ff * ( 0.75f * cos( orkH1 )\n"
					"\t\t+ 0.18f * cos( orkH2 ) + 0.3062f * cos( orkH3 )\n"
					"\t\t+ 0.1569f * cos( orkH4 ) );\n"
					"\tfloat orkDz = %.6ff * ( 0.585f * cos( orkH2 )\n"
					"\t\t- 0.2287f * cos( orkH3 ) + 0.3038f * cos( orkH4 ) );\n"
					"\toutVs.normal = normalize( mul(\n"
					"\t\tmidf3_c( -orkDx, 1.0f, -orkDz ),\n"
					"\t\ttoMidf3x3( passBuf.view ) ) );\n"
					"@end\n"
					"@end\n",
					k1, a2z, a2x, a3x, a3z, a4z, a4x,
					swell,
					k1, a2z, a2x, a3x, a3z, a4z, a4x,
					swell * k1, swell * k1);
				// the amplitude-keyed piece name (@see the swell quantization
				// above): one filename per (material, amplitude), so a swell
				// change registers a FRESH name instead of colliding with the
				// old registration's content
				char swellTag[32];
				std::snprintf(swellTag, sizeof(swellTag),
					"_swell_%.5f", swell);
				datablock->setCustomPieceCodeFromMemory(
					name + swellTag + "_piece_vs.any", source,
					Ogre::CustomPieceStage::VertexShader);
				gSwellWaterMaterials.insert(name);
				if(!gWaterSwellListenerSet)
				{
					pbs->setListener(&gWaterSwellListener);
					gWaterSwellListenerSet = true;
				}
			}
			else
			{
				datablock->setCustomPieceCodeFromMemory(String(), String(),
					Ogre::CustomPieceStage::VertexShader);
				gSwellWaterMaterials.erase(name);
				if(gSwellWaterMaterials.empty() && gWaterSwellListenerSet)
				{
					pbs->setListener(NULL);
					gWaterSwellListenerSet = false;
				}
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
		// the shared swell clock (every swell surface reads the one phase;
		// published into the pass buffer by the swell listener)
		gWaterSwellClock = seconds * kSwellPhaseRate;
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
	void RenderBackend::registerContentDatablock(Ogre::HlmsDatablock* datablock,
		DatablockTier tier)
	{
		oAssert(datablock);
		if(tier == DT_SCENE)
		{
			gSceneDatablocks.push_back(datablock);
			// late-created PBS content joins the active image-lighting set
			RenderBackend::applyImageLightingToDatablock(datablock);
			// a scene datablock created WHILE wireframe is armed joins it (so it
			// restores to solid on disarm like its siblings); the UI set never does
			if(gWireframe)
			{
				applyWireframe(datablock, true);
			}
		}
		else
		{
			gUiDatablocks.push_back(datablock);
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
			// an RTT datablock is a DT_UI 2D-layer block, but drop it from both
			// sets so no dangling pointer survives whichever it landed in
			gSceneDatablocks.erase(std::remove(gSceneDatablocks.begin(),
				gSceneDatablocks.end(), datablock), gSceneDatablocks.end());
			gUiDatablocks.erase(std::remove(gUiDatablocks.begin(),
				gUiDatablocks.end(), datablock), gUiDatablocks.end());
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
		// only the 3D-scene set flips - the 2D/UI set (sprites, vector shapes,
		// dynamic lines, the editor's own ImGui chrome + gui) stays solid
		for(Ogre::HlmsDatablock* each : gSceneDatablocks)
		{
			applyWireframe(each, enabled);
		}
	}
	//---------------------------------------------------------
	namespace
	{
		//! the ONE shared unlit wireframe datablock the SHADED+WIREFRAME overlay's
		//! companion Items render with: near-black, macroblock polygon-mode
		//! wireframe, depth compare LESS_EQUAL + no depth write. The companion
		//! shares the SOURCE mesh, so its rasterized depth is IDENTICAL to the
		//! shaded surface's - LESS_EQUAL lets the coincident lines pass the test
		//! (no depth bias / z-fighting needed), and not writing depth keeps the
		//! shaded depth buffer untouched. Created once, reused across arm cycles,
		//! dies with the root's Hlms (@see setSceneWireframeOverlay).
		Ogre::HlmsDatablock* ensureWireframeOverlayDatablock()
		{
			if(gWireframeOverlayDatablock)
			{
				return gWireframeOverlayDatablock;
			}
			Ogre::HlmsManager* hlmsManager =
				RenderBackend::ogreRoot()->getHlmsManager();
			Ogre::HlmsUnlit* unlit = static_cast<Ogre::HlmsUnlit*>(
				hlmsManager->getHlms(Ogre::HLMS_UNLIT));
			Ogre::HlmsMacroblock macroblock;
			macroblock.mPolygonMode = Ogre::PM_WIREFRAME;
			macroblock.mDepthFunc = Ogre::CMPF_LESS_EQUAL;	// coincident lines pass
			macroblock.mDepthWrite = false;	// shaded pass already wrote the depth
			Ogre::HlmsUnlitDatablock* datablock =
				static_cast<Ogre::HlmsUnlitDatablock*>(unlit->createDatablock(
					"Orkige/SceneWireframeOverlay", "Orkige/SceneWireframeOverlay",
					macroblock, Ogre::HlmsBlendblock(), Ogre::HlmsParamVec()));
			datablock->setUseColour(true);
			// a neutral dark tone that reads on both light and dark lit surfaces
			datablock->setColour(Ogre::ColourValue(0.04f, 0.04f, 0.05f, 1.0f));
			gWireframeOverlayDatablock = datablock;
			return datablock;
		}
		//! is a source Item eligible for a wireframe companion? Static scene
		//! geometry is the mode's habitat: SKINNED/animated meshes are excluded
		//! (a companion shares the mesh but NOT the source's animation state, so
		//! its lines would freeze at the bind pose) - the documented v1 boundary.
		bool overlayEligible(Ogre::Item* source)
		{
			return source && source->getSkeletonInstance() == NULL;
		}
		//! destroy the wireframe companion of @p source if one exists (guarded
		//! detach - a companion whose node already died is auto-detached by Ogre)
		void destroyWireframeOverlayFor(Ogre::Item* source)
		{
			std::unordered_map<Ogre::Item*, Ogre::Item*>::iterator found =
				gWireframeOverlays.find(source);
			if(found == gWireframeOverlays.end())
			{
				return;
			}
			Ogre::Item* companion = found->second;
			if(companion->isAttached())
			{
				companion->detachFromParent();
			}
			companion->_getManager()->destroyItem(companion);
			gWireframeOverlays.erase(found);
		}
		//! create the wireframe companion of @p source: a second Ogre::Item sharing
		//! the source's MESH (no reload) attached to the source's SAME node, drawn
		//! with the shared wireframe datablock, editor-only-visible, shadowless and
		//! unpickable (@see setSceneWireframeOverlay)
		void createWireframeOverlayFor(Ogre::Item* source)
		{
			Ogre::SceneNode* node = source->getParentSceneNode();
			if(!node)
			{
				return;	// source not placed yet - a later armed sync catches it
			}
			Ogre::SceneManager* manager = source->_getManager();
			Ogre::Item* companion = manager->createItem(source->getMesh(),
				Ogre::SCENE_DYNAMIC);
			Ogre::HlmsDatablock* datablock = ensureWireframeOverlayDatablock();
			for(size_t each = 0; each < companion->getNumSubItems(); ++each)
			{
				companion->getSubItem(each)->setDatablock(datablock);
			}
			companion->setCastShadows(false);
			companion->setQueryFlags(0);					// never pickable
			companion->setVisibilityFlags(gOverlayFlags);	// editor-only bit
			node->attachObject(companion);
			gWireframeOverlays[source] = companion;
		}
	}
	//---------------------------------------------------------
	void RenderBackend::registerSceneItem(Ogre::Item* item)
	{
		// createMeshInstance path: the item is NOT attached to a node yet
		// (attachTo comes later), so a companion is created on the next armed
		// sync (the editor re-arms every frame) rather than here
		gSceneItems.push_back(item);
	}
	//---------------------------------------------------------
	void RenderBackend::unregisterSceneItem(Ogre::Item* item)
	{
		// ~MeshInstance path: destroy the companion FIRST, while the source item
		// and its node are still alive, so a companion never dangles on a freed
		// node (and the freed source address cannot be re-mapped by a later item)
		destroyWireframeOverlayFor(item);
		gSceneItems.erase(std::remove(gSceneItems.begin(), gSceneItems.end(),
			item), gSceneItems.end());
	}
	//---------------------------------------------------------
	void RenderBackend::setSceneWireframeOverlay(bool enabled,
		unsigned int editorVisibilityFlags)
	{
		gOverlayFlags = editorVisibilityFlags;
		if(!enabled)
		{
			if(!gOverlayArmed && gWireframeOverlays.empty())
			{
				return;	// idempotent while already disarmed
			}
			// tear the whole companion set down; restore nothing else (nothing
			// else changed - the shaded scene rendered untouched throughout)
			std::vector<Ogre::Item*> sources;
			sources.reserve(gWireframeOverlays.size());
			for(std::pair<Ogre::Item* const, Ogre::Item*> const & each :
				gWireframeOverlays)
			{
				sources.push_back(each.first);
			}
			for(Ogre::Item* source : sources)
			{
				destroyWireframeOverlayFor(source);
			}
			gOverlayArmed = false;
			return;
		}
		gOverlayArmed = true;
		// RESYNC the companion set to the live source set every armed call: an
		// eligible unshadowed source gains a companion, an ineligible or reparented
		// one loses its stale companion - so live create/delete/mesh-swap track
		for(Ogre::Item* source : gSceneItems)
		{
			std::unordered_map<Ogre::Item*, Ogre::Item*>::iterator found =
				gWireframeOverlays.find(source);
			const bool eligible = overlayEligible(source) &&
				source->getParentSceneNode() != NULL;
			if(found != gWireframeOverlays.end())
			{
				// drop a companion whose source became ineligible or reparented
				// (the companion hangs off the old node) - rebuilt below if eligible
				if(!eligible ||
					found->second->getParentNode() != source->getParentNode())
				{
					destroyWireframeOverlayFor(source);
					found = gWireframeOverlays.end();
				}
				else
				{
					// keep it live at the current visibility bit (flags may change)
					found->second->setVisibilityFlags(gOverlayFlags);
				}
			}
			if(found == gWireframeOverlays.end() && eligible)
			{
				createWireframeOverlayFor(source);
			}
		}
	}
	//---------------------------------------------------------
	size_t RenderBackend::sceneWireframeOverlayCount()
	{
		return gWireframeOverlays.size();
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
	bool RenderBackend::saveImageAsPng(Ogre::Image2 & image,
		String const & fileName)
	{
		if(image.getNumMipmaps() == 0u || image.getWidth() == 0u ||
			image.getHeight() == 0u)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: cannot write '" + fileName +
				"' - the readback image is empty");
			return false;
		}
		makeImageAlphaOpaque(image);

		const Ogre::TextureBox source = image.getData(0u);
		if(!source.data || source.depth != 1u || source.numSlices != 1u)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: cannot write '" + fileName +
				"' - a PNG carries ONE 2D image, and this readback is not one");
			return false;
		}

		// an sRGB format describes how a SAMPLER reads the bytes, not the
		// bytes themselves. A screenshot is the framebuffer as it stands, so
		// the conversion below reorders channels and never rewrites values -
		// asking for a gamma step here would move every pixel the parity
		// gates compare
		Ogre::PixelFormatGpu sourceFormat = image.getPixelFormat();
		if(Ogre::PixelFormatGpuUtils::isSRgb(sourceFormat))
		{
			sourceFormat =
				Ogre::PixelFormatGpuUtils::getEquivalentLinear(sourceFormat);
		}

		std::vector<unsigned char> converted;
		unsigned char const * pixels = NULL;
		int strideBytes = 0;
		if(sourceFormat == Ogre::PFG_RGBA8_UNORM)
		{
			// already the encoder's layout: hand the readback rows over as
			// they lie. A GPU row is PADDED to an alignment, which is why the
			// stride travels with the pointer instead of being recomputed
			pixels = reinterpret_cast<unsigned char const *>(source.data);
			strideBytes = static_cast<int>(source.bytesPerRow);
		}
		else
		{
			const std::size_t packedRow =
				static_cast<std::size_t>(source.width) * 4u;
			const std::size_t packedImage =
				packedRow * static_cast<std::size_t>(source.height);
			converted.resize(packedImage);
			Ogre::TextureBox destination(source.width, source.height, 1u, 1u,
				4u, static_cast<Ogre::uint32>(packedRow), packedImage);
			destination.data = converted.data();
			try
			{
				Ogre::PixelFormatGpuUtils::bulkPixelConversion(source,
					sourceFormat, destination, Ogre::PFG_RGBA8_UNORM);
			}
			catch(Ogre::Exception const & e)
			{
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige next backend: cannot write '" + fileName +
					"' - the readback format does not convert to RGBA8: " +
					e.getDescription());
				return false;
			}
			pixels = converted.data();
			strideBytes = static_cast<int>(packedRow);
		}

		if(!PngWriter::writeFile(fileName, pixels,
			static_cast<int>(source.width),
			static_cast<int>(source.height), strideBytes))
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: writing '" + fileName + "' failed");
			return false;
		}
		return true;
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
