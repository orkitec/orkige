/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	ClassicBackend.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file ClassicBackend.cpp
//! @brief backend hub: lifecycle, node registry and shared services
//! @remarks the per-class facade method bodies live in the sibling
//! *Classic.cpp TUs; this TU owns the process-wide backend state

#include "engine_render_classic/ClassicBackend.h"
#include "engine_graphic/Engine.h"
#include "engine_util/StringUtil.h"

#include <OgreShadowCameraSetupPSSM.h>
#include <OgreShadowCameraSetupFocused.h>
#include <OgreCompositorManager.h>
#include <OgreCompositionTechnique.h>
#include <OgreCompositionTargetPass.h>
#include <OgreCompositionPass.h>

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace Orkige
{
	namespace
	{
		//! the live render system behind RenderSystem::get (one per
		//! process - the build-time backend rule, no runtime switch)
		RenderSystem* gRenderSystem = NULL;
		//! back-mapping registry: every facade-created node registers
		//! here so ray query hits / getParent / user-pointer walks can
		//! resolve backend nodes to facade handles
		std::unordered_map<Ogre::SceneNode*, woptr<RenderNode>> gNodeRegistry;
		//! monotonic counter behind RenderBackend::generateName
		unsigned long gNameCounter = 0;
		//! directional lights in creation order - the sun the sky dome links
		//! to is the FIRST of these (@see RenderBackend::firstDirectionalLight,
		//! mirrors the next flavor's registry)
		std::vector<Ogre::Light*> gDirectionalLights;

		//--- dynamic-shadow state (@see RenderBackend::applyShadowConfig) --
		//! the tier the scene technique is currently ARMED with (SQ_OFF =
		//! disarmed); a recompute that lands on the same value is a no-op
		ShadowPreset::Quality gArmedShadowQuality = ShadowPreset::SQ_OFF;
		//! the atmosphere drive dimmed the sun to night - the pass is skipped
		//! while true (@see RenderBackend::noteSunDimmedForShadows)
		bool gSunDimmedForShadows = false;
		//! the one honest per-process refusal line was written
		bool gShadowRefusalLogged = false;

		//--- LDR bloom state (@see RenderBackend::applyBloomConfig) --------
		//! the bloom compositor is added to this viewport (NULL = not added);
		//! re-added when the window viewport identity or the tier changes
		Ogre::Viewport* gBloomViewport = NULL;
		//! the live bloom compositor instance on gBloomViewport
		Ogre::CompositorInstance* gBloomInstance = NULL;
		//! is the bloom compositor currently ENABLED on gBloomViewport
		bool gBloomEnabled = false;
		//! the quality tier the armed compositor was built for (each tier is
		//! its own compositor resource - the blur chain length and buffer
		//! resolution are baked into the technique, @see buildBloomCompositor)
		BloomPreset::Quality gBloomArmedQuality = BloomPreset::BQ_OFF;
		//! the one honest per-process bloom refusal line was written
		bool gBloomRefusalLogged = false;
		//! restore-exactly snapshot of the scene manager's shadow state,
		//! taken at arm time and written back verbatim at disarm
		size_t gPreArmTextureCount = 0;
		size_t gPreArmPerDirectional = 0;
		float gPreArmFarDistance = 0.0f;
		bool gPreArmSelfShadow = false;
		bool gPreArmBackFaces = true;
		Ogre::ShadowCameraSetupPtr gPreArmCameraSetup;

		//! every live offscreen render target - applyShadowConfig re-applies
		//! their viewport state on arm/disarm (the shadow-toggle override,
		//! @see RenderBackend::shadowsArmed)
		std::vector<RenderTexture*> gRenderTargets;

		//! does any live directional light ask to cast (the arming trigger -
		//! v1 shadow maps are directional-only on both flavors)
		bool anyDirectionalCaster()
		{
			for(Ogre::Light* light : gDirectionalLights)
			{
				if(light->getCastShadows())
				{
					return true;
				}
			}
			return false;
		}
	}
	//---------------------------------------------------------
	Ogre::Light* RenderBackend::firstDirectionalLight()
	{
		return gDirectionalLights.empty() ? NULL : gDirectionalLights.front();
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
		// the sun set changed: re-resolve a live sky dome to the new first
		// directional light (drops a dangling sun when it leaves/dies,
		// promotes a freshly-authored one)
		RenderBackend::refreshSkyDome();
		// and a directional caster may have appeared/left - recompute the
		// shadow arming (a dying caster disarms, a fresh one arms)
		RenderBackend::applyShadowConfig();
	}
	//---------------------------------------------------------
	bool RenderBackend::dynamicShadowsSupported()
	{
#ifdef USE_RTSHADER_SYSTEM
		if(!Ogre::RTShader::ShaderGenerator::getSingletonPtr())
		{
			return false;	// no shader generator - no receiver injection
		}
		// depth-texture render targets are the ONE hardware requirement: the
		// caster pass writes real depth, the receiver samples it with a
		// hardware-compare fetch. GL3Plus/Vulkan always have them; a GLES2/
		// WebGL context answers per device (OES_depth_texture /
		// WEBGL_depth_texture - near-universal on the API-28+ floor).
		return Ogre::TextureManager::getSingleton().isFormatSupported(
			Ogre::TEX_TYPE_2D, Ogre::PF_DEPTH16, Ogre::TU_RENDERTARGET);
#else
		return false;
#endif
	}
	//---------------------------------------------------------
	bool RenderBackend::shadowsArmed()
	{
		return gArmedShadowQuality != ShadowPreset::SQ_OFF;
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
	void RenderBackend::noteSunDimmedForShadows(bool dimmed)
	{
		if(gSunDimmedForShadows == dimmed)
		{
			return;
		}
		gSunDimmedForShadows = dimmed;
		RenderBackend::applyShadowConfig();
	}
	//---------------------------------------------------------
	void RenderBackend::applyShadowConfig()
	{
#ifdef USE_RTSHADER_SYSTEM
		if(!gRenderSystem)
		{
			return;
		}
		RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
		Ogre::SceneManager* sceneManager = world->sceneManager;

		// the target state: the knob's tier while a directional light casts
		// and the atmosphere-driven sun is not night-dark, else disarmed
		ShadowPreset::Quality target = world->shadowQuality;
		if(target != ShadowPreset::SQ_OFF &&
			(!anyDirectionalCaster() || gSunDimmedForShadows))
		{
			target = ShadowPreset::SQ_OFF;
		}
		if(target != ShadowPreset::SQ_OFF &&
			!RenderBackend::dynamicShadowsSupported())
		{
			// the honest per-device refusal (a GLES2 context without depth
			// textures) - said ONCE, then the knob keeps round-tripping
			if(!gShadowRefusalLogged)
			{
				gShadowRefusalLogged = true;
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige classic backend: dynamic shadows are not supported "
					"on this render backend - the quality knob is recorded but "
					"no shadow maps render on this flavor");
			}
			target = ShadowPreset::SQ_OFF;
		}
		if(target == gArmedShadowQuality)
		{
			return;	// nothing changes (covers disarmed-stays-disarmed)
		}

		Ogre::RTShader::ShaderGenerator* generator =
			Ogre::RTShader::ShaderGenerator::getSingletonPtr();
		const String scheme =
			Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME;
		// createOrRetrieve: arming can precede the FIRST generated material
		// (a scene's lights load before its first frame renders), and the
		// scheme entry only exists once something generated through it
		Ogre::RTShader::RenderState* schemeState = generator
			? generator->createOrRetrieveRenderState(scheme).first : NULL;

		// DISARM first - also the first half of a tier change (re-arming from
		// the restored baseline keeps arm/disarm a strict pair): technique
		// NONE frees the shadow maps, the counts/distances/camera setup go
		// back to their snapshotted pre-arm values, the receiver leaves the
		// generated-material scheme
		if(gArmedShadowQuality != ShadowPreset::SQ_OFF)
		{
			sceneManager->setShadowTechnique(Ogre::SHADOWTYPE_NONE);
			sceneManager->setShadowTextureCount(gPreArmTextureCount);
			sceneManager->setShadowTextureCountPerLightType(
				Ogre::Light::LT_DIRECTIONAL, gPreArmPerDirectional);
			sceneManager->setShadowFarDistance(gPreArmFarDistance);
			sceneManager->setShadowTextureSelfShadow(gPreArmSelfShadow);
			sceneManager->setShadowCasterRenderBackFaces(gPreArmBackFaces);
			if(gPreArmCameraSetup)
			{
				sceneManager->setShadowCameraSetup(gPreArmCameraSetup);
				gPreArmCameraSetup.reset();
			}
			if(schemeState)
			{
				if(Ogre::RTShader::SubRenderState* receiver =
					schemeState->getSubRenderState(
						Ogre::RTShader::SRS_SHADOW_MAPPING))
				{
					schemeState->removeSubRenderState(receiver);
				}
				generator->invalidateScheme(scheme);
			}
			gArmedShadowQuality = ShadowPreset::SQ_OFF;
		}
		if(target == ShadowPreset::SQ_OFF || !schemeState)
		{
			// disarmed: every offscreen target returns to its AUTHORED
			// shadow toggle (the arm-time override lifts, restore-exactly)
			for(RenderTexture* each : gRenderTargets)
			{
				each->mImpl->applyViewportState();
			}
			return;
		}

		// ARM: the scene-level integrated technique - the shader owns the
		// darkening (the receiver folds the shadow factor into the SAME
		// Cook-Torrance/FFP lighting stage every generated material uses),
		// the scene renders ONCE plus one depth pass per split
		const ShadowPreset::Settings preset = ShadowPreset::forQuality(target);
		gPreArmTextureCount = sceneManager->getShadowTextureConfigList().size();
		gPreArmPerDirectional = sceneManager->getShadowTextureCountPerLightType(
			Ogre::Light::LT_DIRECTIONAL);
		gPreArmFarDistance = sceneManager->getShadowFarDistance();
		gPreArmSelfShadow = sceneManager->getShadowTextureSelfShadow();
		gPreArmBackFaces = sceneManager->getShadowCasterRenderBackFaces();
		gPreArmCameraSetup = sceneManager->getShadowCameraSetup();

		sceneManager->setShadowTechnique(
			Ogre::SHADOWTYPE_TEXTURE_ADDITIVE_INTEGRATED);
		sceneManager->setShadowFarDistance(preset.maxDistance);
		sceneManager->setShadowTextureCountPerLightType(
			Ogre::Light::LT_DIRECTIONAL, preset.splitCount);
		sceneManager->setShadowTextureCount(preset.splitCount);
		for(int split = 0; split < preset.splitCount; ++split)
		{
			const unsigned int edge = ShadowPreset::splitResolution(preset,
				split);
			sceneManager->setShadowTextureConfig(split, edge, edge,
				Ogre::PF_DEPTH16);
		}
		// real depth maps: self-shadowing works, acne is kept down by
		// rendering caster BACK faces instead of a depth bias
		sceneManager->setShadowTextureSelfShadow(true);
		sceneManager->setShadowCasterRenderBackFaces(true);

		// the split scheme: near plane from the shown camera (PSSM squeezes
		// its crisp first cascade against the near plane), far = the preset's
		// shadow reach; ONE split degenerates to a single focused map (the
		// same collapse the next flavor renders for the low tier)
		float nearClip = 0.5f;
		Ogre::RenderWindow* window =
			gRenderSystem->mImpl->engine->getRenderWindow(0);
		if(window && window->getNumViewports() > 0 &&
			window->getViewport(0)->getCamera())
		{
			nearClip = std::max(0.1f,
				window->getViewport(0)->getCamera()->getNearClipDistance());
		}
		std::vector<Ogre::Real> splitPoints;
		if(preset.splitCount > 1)
		{
			Ogre::PSSMShadowCameraSetup* pssm =
				new Ogre::PSSMShadowCameraSetup();
			pssm->setSplitPadding(nearClip);
			pssm->calculateSplitPoints(preset.splitCount, nearClip,
				preset.maxDistance);
			for(int split = 0; split < preset.splitCount; ++split)
			{
				// the canonical near-to-far focus falloff (tight first cascade)
				pssm->setOptimalAdjustFactor(split,
					split == 0 ? 2.0f : (split == 1 ? 1.0f : 0.5f));
			}
			splitPoints = pssm->getSplitPoints();
			sceneManager->setShadowCameraSetup(
				Ogre::ShadowCameraSetupPtr(pssm));
		}
		else
		{
			// ONE split is the single focused map (the PSSM setup refuses
			// < 2 splits - and a 1-split PSSM IS a focused map): the whole
			// [near; maxDistance] range rides one texture
			sceneManager->setShadowCameraSetup(Ogre::ShadowCameraSetupPtr(
				new Ogre::FocusedShadowCameraSetup()));
			splitPoints.push_back(nearClip);
			splitPoints.push_back(preset.maxDistance);
		}

		// receiver injection, ONCE and centrally: the template sub-render-
		// state joins the scheme render state, so EVERY generated material
		// (Cook-Torrance surfaces, water, imported meshes) grows the receiver
		// stage on the invalidate - materials that opted out
		// (setReceiveShadows(false): all 2D/unlit materials) are skipped by
		// the sub-render-state itself
		Ogre::RTShader::SubRenderState* receiver =
			generator->createSubRenderState(Ogre::RTShader::SRS_SHADOW_MAPPING);
		receiver->setParameter("split_points", splitPoints);
		if(preset.filterTaps >= 4)
		{
			receiver->setParameter("filter", "pcf16");
		}
		schemeState->addTemplateSubRenderState(receiver);
		generator->invalidateScheme(scheme);
		gArmedShadowQuality = target;
		// armed: every viewport must PREPARE shadow state - the integrated
		// receiver is baked into the scene's generated shaders, and a
		// shadows-disabled viewport rendering them reads unmaintained
		// shadow projectors (@see RenderBackend::shadowsArmed). Re-apply
		// each target's viewport state so the override takes effect.
		for(RenderTexture* each : gRenderTargets)
		{
			each->mImpl->applyViewportState();
		}
#endif // USE_RTSHADER_SYSTEM
	}
	//---------------------------------------------------------
	String RenderBackend::shadowStateDescription()
	{
		if(!gRenderSystem)
		{
			return "no-render-system";
		}
		Ogre::SceneManager* sceneManager =
			gRenderSystem->getWorld()->mImpl->sceneManager;
		std::ostringstream state;
		state << "technique=" << static_cast<int>(
				sceneManager->getShadowTechnique())
			<< " textures=" << sceneManager->getShadowTextureConfigList().size()
			<< " perDirectional=" << sceneManager
				->getShadowTextureCountPerLightType(Ogre::Light::LT_DIRECTIONAL)
			<< " far=" << sceneManager->getShadowFarDistance()
			<< " selfShadow=" << sceneManager->getShadowTextureSelfShadow()
			<< " backFaces=" << sceneManager->getShadowCasterRenderBackFaces();
#ifdef USE_RTSHADER_SYSTEM
		if(Ogre::RTShader::ShaderGenerator* generator =
			Ogre::RTShader::ShaderGenerator::getSingletonPtr())
		{
			// createOrRetrieve: the probe may run before the first generated
			// material brought the scheme entry into existence
			Ogre::RTShader::RenderState* schemeState =
				generator->createOrRetrieveRenderState(
					Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME).first;
			state << " scheme='"
				<< Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME
				<< "' receiver=" << (schemeState->getSubRenderState(
					Ogre::RTShader::SRS_SHADOW_MAPPING) ? 1 : 0);
		}
#endif
		return state.str();
	}
	//--- LDR bloom (viewport compositor) --------------------------------
	//---------------------------------------------------------
	bool RenderBackend::bloomSupported()
	{
#ifdef USE_RTSHADER_SYSTEM
		// the compositor's off-screen scene pass renders through the SAME
		// generated-material scheme as the main viewport (its target passes
		// carry the window viewport's RTSS scheme, @see buildBloomCompositor),
		// so the one hard requirement beyond the generator is render-to-texture
		// support for the scene/blur buffers
		Ogre::RTShader::ShaderGenerator* generator =
			Ogre::RTShader::ShaderGenerator::getSingletonPtr();
		if(!generator)
		{
			return false;	// no shader generator - no generated scene pass
		}
		Ogre::RenderSystem* renderSystem =
			Ogre::Root::getSingleton().getRenderSystem();
		if(!renderSystem || !renderSystem->getCapabilities()->hasCapability(
			Ogre::RSC_HWRENDER_TO_TEXTURE))
		{
			return false;	// no off-screen colour targets - no bloom chain
		}
		// GLES2/WebGL contexts are runtime-GATED pending an on-device/browser
		// proof run of the compositor chain (FBOs exist there, so this is an
		// unproven-not-incapable gate - the honest refusal line says so once,
		// @see applyBloomConfig and Docs/render-abstraction.md)
		if(generator->getTargetLanguage() == "glsles")
		{
			return false;
		}
		return true;
#else
		return false;	// no shader generator built - no generated scene pass
#endif
	}
	//---------------------------------------------------------
	bool RenderBackend::bloomActive()
	{
		if(!gRenderSystem)
		{
			return false;
		}
		RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
		if(world->bloomQuality == BloomPreset::BQ_OFF || !world->bloom.enabled)
		{
			return false;
		}
		return RenderBackend::bloomSupported();
	}
	//---------------------------------------------------------
	void RenderBackend::tagScene3D(Ogre::MovableObject* movable)
	{
		if(movable)
		{
			movable->setVisibilityFlags(RenderBackend::SCENE_3D_VISIBILITY);
		}
	}
	//---------------------------------------------------------
	void RenderBackend::tagScene2D(Ogre::MovableObject* movable)
	{
		if(movable)
		{
			movable->setVisibilityFlags(RenderBackend::SCENE_2D_VISIBILITY);
		}
	}
	//---------------------------------------------------------
	bool RenderBackend::isBloomOutputViewport(Ogre::Viewport* viewport)
	{
		// while bloom is active the compositor's target_output pass renders to
		// the WINDOW render target (possibly through a chain-managed viewport,
		// not the identical window viewport(0) object) - accept any viewport
		// whose target IS the window so the 2D overlay-queue GUI listener still
		// composites over the combined result (@see DrawLayer2DClassic)
		if(!gBloomEnabled || !viewport || !gRenderSystem ||
			!gRenderSystem->mImpl->engine)
		{
			return false;
		}
		Ogre::RenderWindow* window =
			gRenderSystem->mImpl->engine->getRenderWindow(0);
		return window && viewport->getTarget() == window;
	}
	namespace
	{
		//! push a float named constant onto a bloom material's fragment program
		//! (the compositor quad passes read the material's pass params live)
		void setBloomFragParam(char const * material, char const * name,
			float value)
		{
			Ogre::MaterialPtr handle =
				Ogre::MaterialManager::getSingleton().getByName(material);
			if(!handle)
			{
				return;
			}
			handle->load();
			if(handle->getNumTechniques() == 0 ||
				handle->getTechnique(0)->getNumPasses() == 0)
			{
				return;
			}
			Ogre::Pass* pass = handle->getTechnique(0)->getPass(0);
			if(!pass->hasFragmentProgram())
			{
				return;
			}
			// an unsupported/compile-broken program has no named constants -
			// touching its params would throw mid-frame
			Ogre::GpuProgramPtr program = pass->getFragmentProgram();
			if(!program || !program->isSupported())
			{
				return;
			}
			pass->getFragmentProgramParameters()->setNamedConstant(
				name, Ogre::Real(value));
		}
		//! the per-tier compositor resource name (each tier bakes its own blur
		//! chain length and buffer resolution into the technique)
		String bloomCompositorName(BloomPreset::Quality quality)
		{
			return String("Orkige/Bloom/") + BloomPreset::qualityName(quality);
		}
		//! append a fullscreen-quad target pass (material + input textures) to
		//! @p technique - the bright/blur building block of the bloom chain
		void addBloomQuadTarget(Ogre::CompositionTechnique* technique,
			String const & outputName, String const & materialName,
			String const & input0, String const & input1 = String())
		{
			Ogre::CompositionTargetPass* target = technique->createTargetPass();
			target->setOutputName(outputName);
			target->setInputMode(Ogre::CompositionTargetPass::IM_NONE);
			Ogre::CompositionPass* quad =
				target->createPass(Ogre::CompositionPass::PT_RENDERQUAD);
			quad->setMaterialName(materialName);
			quad->setInput(0, input0);
			if(!input1.empty())
			{
				quad->setInput(1, input1);
			}
		}
		//! @brief build (once per tier) the bloom compositor RESOURCE: the 3D
		//! scene into an off-screen colour texture -> bright-pass -> separable
		//! blur ping-pong -> additive combine into the window, then the 2D tier
		//! + GUI un-bloomed on top. Mirrors the next flavor's quad chain
		//! (NextBackend window workspace) on the classic compositor framework.
		//!
		//! The two integration seams that make the off-screen passes correct:
		//! - MATERIAL SCHEME: every target pass carries the WINDOW viewport's
		//!   material scheme (the RTSS generated-material scheme applied at
		//!   boot, @see applyRTSSScheme). The chain applies it to the local RT
		//!   viewports per operation, so queue-time technique resolution runs
		//!   the SAME shader-generation path as the main viewport. (A pass-level
		//!   material_scheme would instead ride RSSetSchemeOperation's
		//!   late-material-resolving, which bypasses that path.)
		//! - VISIBILITY: the tier split is TARGET-level visibility masks (the
		//!   chain applies them to the operation's viewport and restores after,
		//!   so bloom-off state is untouched): the scene target renders
		//!   everything BUT the 2D tier (~SCENE_2D - untagged movables default
		//!   into the 3D tier, @see createRenderSystem), the output target
		//!   renders ONLY the 2D tier over the combined result. The sky is the
		//!   one mask-exempt renderable (SceneManager queues it outside the
		//!   visibility walk), so the output scene pass starts ABOVE the sky
		//!   queues - the sky is already in the combined base image.
		Ogre::CompositorPtr buildBloomCompositor(BloomPreset::Quality quality,
			Ogre::Viewport* windowViewport)
		{
			Ogre::CompositorManager & manager =
				Ogre::CompositorManager::getSingleton();
			const String name = bloomCompositorName(quality);
			Ogre::CompositorPtr compositor = manager.getByName(name,
				Ogre::ResourceGroupManager::INTERNAL_RESOURCE_GROUP_NAME);
			if(compositor)
			{
				return compositor;
			}
			const BloomPreset::Settings tier = BloomPreset::forQuality(quality);
			const float downFactor = 1.0f /
				static_cast<float>(std::max(tier.downsampleFactor, 1));
			const int blurPasses = std::max(tier.blurPasses, 1);
			const String & scheme = windowViewport->getMaterialScheme();
			compositor = manager.create(name,
				Ogre::ResourceGroupManager::INTERNAL_RESOURCE_GROUP_NAME);
			Ogre::CompositionTechnique* technique = compositor->createTechnique();
			// off-screen textures: full-res scene, two downsampled ping-pong
			// bloom buffers (width/height 0 = adapt to the window size)
			Ogre::CompositionTechnique::TextureDefinition* sceneDef =
				technique->createTextureDefinition("scene");
			sceneDef->formatList.push_back(Ogre::PF_R8G8B8A8);
			for(char const * bloomBuf : { "rt0", "rt1" })
			{
				Ogre::CompositionTechnique::TextureDefinition* def =
					technique->createTextureDefinition(bloomBuf);
				def->widthFactor = downFactor;
				def->heightFactor = downFactor;
				def->formatList.push_back(Ogre::PF_R8G8B8A8);
			}
			// --- the 3D scene into the off-screen texture (2D tier excluded) ---
			{
				Ogre::CompositionTargetPass* target =
					technique->createTargetPass();
				target->setOutputName("scene");
				target->setInputMode(Ogre::CompositionTargetPass::IM_NONE);
				target->setVisibilityMask(~RenderBackend::SCENE_2D_VISIBILITY);
				target->setMaterialScheme(scheme);
				Ogre::CompositionPass* clear =
					target->createPass(Ogre::CompositionPass::PT_CLEAR);
				clear->setAutomaticColour(true);	// the window background
				Ogre::CompositionPass* scenePass =
					target->createPass(Ogre::CompositionPass::PT_RENDERSCENE);
				scenePass->setFirstRenderQueue(Ogre::RENDER_QUEUE_BACKGROUND);
				scenePass->setLastRenderQueue(Ogre::RENDER_QUEUE_SKIES_LATE);
			}
			// --- bright-pass -> downsampled bloom buffer ---
			addBloomQuadTarget(technique, "rt0", "Orkige/Bloom/Bright", "scene");
			// --- separable blur, ping-ponging rt0 <-> rt1 per tier pass ---
			for(int pass = 0; pass < blurPasses; ++pass)
			{
				addBloomQuadTarget(technique, "rt1", "Orkige/Bloom/BlurV", "rt0");
				addBloomQuadTarget(technique, "rt0", "Orkige/Bloom/BlurH", "rt1");
			}
			// --- additive combine into the window, 2D tier + GUI on top ---
			{
				Ogre::CompositionTargetPass* output =
					technique->getOutputTargetPass();
				output->setInputMode(Ogre::CompositionTargetPass::IM_NONE);
				output->setVisibilityMask(RenderBackend::SCENE_2D_VISIBILITY);
				output->setShadowsEnabled(false);	// no caster re-prep for 2D
				output->setMaterialScheme(scheme);
				// fresh colour+depth: the combine quad covers every pixel, the
				// depth clear keeps the depth-checked 2D tier off stale 3D depth
				output->createPass(Ogre::CompositionPass::PT_CLEAR);
				Ogre::CompositionPass* combine =
					output->createPass(Ogre::CompositionPass::PT_RENDERQUAD);
				combine->setMaterialName("Orkige/Bloom/Combine");
				combine->setInput(0, "scene");
				combine->setInput(1, "rt0");
				Ogre::CompositionPass* twoDPass =
					output->createPass(Ogre::CompositionPass::PT_RENDERSCENE);
				// above the mask-exempt sky queues (see the builder doc), up to
				// the overlay queue where the DrawLayer2D GUI listener fires
				twoDPass->setFirstRenderQueue(Ogre::RENDER_QUEUE_SKIES_EARLY + 1);
				twoDPass->setLastRenderQueue(Ogre::RENDER_QUEUE_OVERLAY);
			}
			compositor->load();
			return compositor;
		}
	}
	//---------------------------------------------------------
	void RenderBackend::applyBloomConfig()
	{
		if(!gRenderSystem || !gRenderSystem->mImpl->engine)
		{
			return;
		}
		Ogre::Viewport* viewport = gRenderSystem->mImpl->engine->getViewport(0);
		if(!viewport)
		{
			return;
		}
		Ogre::CompositorManager & compositors =
			Ogre::CompositorManager::getSingleton();
		const bool wantActive = RenderBackend::bloomActive();
		// honest refusal on a bloom-less context (a bare GLES2/WebGL context,
		// @see bloomSupported): the knob + desc keep round-tripping, no
		// compositor is added, said once
		RenderWorld::Impl* world = gRenderSystem->getWorld()->mImpl;
		if(!wantActive && world->bloom.enabled &&
			world->bloomQuality != BloomPreset::BQ_OFF &&
			!RenderBackend::bloomSupported() && !gBloomRefusalLogged)
		{
			gBloomRefusalLogged = true;
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige classic backend: bloom post-process is not supported on "
				"this render backend - the setting is recorded but no bloom "
				"renders on this flavor");
		}
		if(wantActive)
		{
			// push the live threshold + intensity onto the bloom materials
			const BloomDesc desc = world->bloom.sanitised();
			setBloomFragParam("Orkige/Bloom/Bright", "Threshold", desc.threshold);
			setBloomFragParam("Orkige/Bloom/Combine", "OriginalImageWeight", 1.0f);
			setBloomFragParam("Orkige/Bloom/Combine", "Intensity", desc.intensity);
			// re-arm when the window viewport identity or the tier changed (each
			// tier is its own compositor resource - blur chain + buffers differ)
			if(gBloomViewport && (gBloomViewport != viewport ||
				gBloomArmedQuality != world->bloomQuality))
			{
				compositors.setCompositorEnabled(gBloomViewport,
					bloomCompositorName(gBloomArmedQuality), false);
				compositors.removeCompositor(gBloomViewport,
					bloomCompositorName(gBloomArmedQuality));
				gBloomViewport = NULL;
				gBloomInstance = NULL;
			}
			if(!gBloomViewport)
			{
				buildBloomCompositor(world->bloomQuality, viewport);
				gBloomInstance = compositors.addCompositor(viewport,
					bloomCompositorName(world->bloomQuality));
				if(!gBloomInstance)
				{
					if(!gBloomRefusalLogged)
					{
						gBloomRefusalLogged = true;
						Ogre::LogManager::getSingleton().logMessage(
							"Orkige classic backend: bloom post-process is not "
							"supported on this render backend - the compositor "
							"could not be created");
					}
					return;
				}
				gBloomViewport = viewport;
				gBloomArmedQuality = world->bloomQuality;
			}
			compositors.setCompositorEnabled(viewport,
				bloomCompositorName(gBloomArmedQuality), true);
			gBloomEnabled = true;
		}
		else if(gBloomViewport)
		{
			// disable only (the compositor stays added for a cheap re-enable):
			// a fully disabled chain renders the viewport the normal path, so
			// bloom-off output is byte-identical - the toggle-identity contract
			compositors.setCompositorEnabled(gBloomViewport,
				bloomCompositorName(gBloomArmedQuality), false);
			gBloomEnabled = false;
		}
	}
	//---------------------------------------------------------
	RenderSystem* RenderBackend::createRenderSystem(Engine* engine)
	{
		oAssert(engine);
		oAssert(engine->getSceneManager());
		oAssert(engine->getRenderWindow(0));
		if(gRenderSystem)
		{
			return gRenderSystem;	// Engine::setup runs once; be idempotent anyway
		}
		RenderSystem* system = new RenderSystem();
		system->mImpl->engine = engine;
		RenderWorld* world = new RenderWorld();
		world->mImpl->sceneManager = engine->getSceneManager();
		system->mImpl->world = world;
		// the classic backend's render capabilities (@see RenderSystem::supports;
		// the register leg of render_facade_selfcheck asserts this fill matches
		// engine_render_classic/RenderCapsExpectedClassic.inc): a vertex-colour
		// gradient sky dome, the sun-exposure linkage (the atmosphere
		// drives the linked sun's colour and an averaged-flat ambient through
		// the shared curve - core_util/AtmosphereSunDrive.h) and dynamic
		// shadows (RTSS integrated PSSM, @see applyShadowConfig) - the shadow
		// bit is RUNTIME-determined: a GLES2 context without depth-texture
		// render targets answers false per device, and the image-lighting
		// bit likewise (the generated stage needs GLSL ES 3.0 on a GLES
		// context, @see imageBasedLightingSupported). The rest (hemisphere
		// ambient, animated normal-mapped water, offscreen-owned 2D layers)
		// are registered next-only deltas; screen-space refraction is
		// absent on both.
		system->mImpl->caps = (1u << static_cast<int>(RenderCaps::SkyDome)) |
			(1u << static_cast<int>(RenderCaps::SunExposureLinkage));
		if(RenderBackend::dynamicShadowsSupported())
		{
			system->mImpl->caps |=
				(1u << static_cast<int>(RenderCaps::DynamicShadows));
		}
		if(RenderBackend::imageBasedLightingSupported())
		{
			system->mImpl->caps |=
				(1u << static_cast<int>(RenderCaps::IblReflections));
		}
		// LDR bloom (@see applyBloomConfig): RUNTIME-determined like shadows -
		// desktop (GL3Plus/Vulkan) answers true, a GLES2/WebGL context is gated
		// pending an on-device/browser proof run and an enabled bloom degrades
		// to no pass with one honest log line there (@see bloomSupported).
		if(RenderBackend::bloomSupported())
		{
			system->mImpl->caps |=
				(1u << static_cast<int>(RenderCaps::Bloom));
		}
		// screen-space water refraction (@see createOrUpdateWaterMaterial): a
		// grab-pass RenderTexture sampled at a normal-perturbed screen UV -
		// RUNTIME-determined like shadows/bloom. Desktop GL3Plus (what the facade
		// selfcheck boots) answers true; a Vulkan/GLES/WebGL context answers false
		// per device (byte-stable fallback pending its shader variant + proof run).
		if(RenderBackend::screenSpaceRefractionSupported())
		{
			system->mImpl->caps |=
				(1u << static_cast<int>(RenderCaps::ScreenSpaceRefraction));
		}
		// planar (mirror-of-scene) water reflection (@see createOrUpdateWaterMaterial):
		// a mirror-camera reflection RenderTexture sampled at the ripple-perturbed
		// screen UV - RUNTIME-determined like refraction. Desktop GL3Plus answers
		// true; a Vulkan/GLES/WebGL context answers false per device (byte-stable
		// sky-reflection fallback pending its shader variant + proof run).
		if(RenderBackend::screenSpacePlanarReflectionSupported())
		{
			system->mImpl->caps |=
				(1u << static_cast<int>(RenderCaps::PlanarReflection));
		}
		// the bloom tier split rides visibility flags: the 2D tier carries the
		// SCENE_2D bit (tagScene2D), so every OTHER movable must default into
		// the 3D tier - clear the 2D bit from the process default (mirrors the
		// next backend's boot trim; idempotent across create/destroy cycles).
		// Byte-inert while bloom is off: every viewport mask stays full, and a
		// full mask ANDs visible against both flag values.
		Ogre::MovableObject::setDefaultVisibilityFlags(
			Ogre::MovableObject::getDefaultVisibilityFlags() &
			~RenderBackend::SCENE_2D_VISIBILITY);
		// the sane concurrent dynamic-light ceiling (@see RenderSystem::
		// lightBudget): the classic forward renderer's per-pass headroom
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
		// static regions reference the dying scene manager - drop them first
		RenderBackend::staticBakeTeardown();
		RenderBackend::resetDecalState();	// facade-side decal registry statics
		// the water-refraction grab target auto-updates off the main camera -
		// destroy it (+ clear its registries) before the scene manager dies
		RenderBackend::refractionTeardown();
		delete gRenderSystem;	// ~RenderSystem deletes the world
		gRenderSystem = NULL;
		// handles may still be alive in script states (Lua userdata lives
		// until the Lua state closes, which happens after ~Engine) - their
		// destructors detect the dead backend via RenderBackend::system()
		// and free facade memory only. Drop the mappings so late lookups
		// resolve to NULL instead of dangling.
		gNodeRegistry.clear();
		// the sun registry points at lights the scene manager is tearing down
		gDirectionalLights.clear();
		// the shadow state dies with the scene manager - reset the arming
		// bookkeeping for a future render system (no restore: the snapshot's
		// scene manager is gone)
		gArmedShadowQuality = ShadowPreset::SQ_OFF;
		gSunDimmedForShadows = false;
		gPreArmCameraSetup.reset();
		// the bloom compositor dies with the viewport/scene manager - reset the
		// arming bookkeeping for a future render system
		gBloomViewport = NULL;
		gBloomInstance = NULL;
		gBloomEnabled = false;
		gBloomArmedQuality = BloomPreset::BQ_OFF;
		// render-target handles may outlive the backend (script states) -
		// drop the registry so their destructors stop unregistering
		gRenderTargets.clear();
	}
	//---------------------------------------------------------
	RenderSystem* RenderBackend::system()
	{
		return gRenderSystem;
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
		// .dds, ASTC/ETC2 ride KTX1 (@see Util/cook_textures.py)
		for(const char* extension : { ".dds", ".ktx" })
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
	Ogre::Entity* RenderBackend::ogreEntity(optr<MeshInstance> const & mesh)
	{
		return mesh ? mesh->mImpl->entity : NULL;
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
		return prefix + "." + StringUtil::Converter::toString(++gNameCounter);
	}
	//---------------------------------------------------------
	void RenderBackend::applyRTSSScheme(Ogre::Viewport* viewport)
	{
		oAssert(viewport);
#ifdef USE_RTSHADER_SYSTEM
		// same wiring Engine::createDefaultCameraAndViewport and the
		// editor's RTT apply: without the RTSS scheme nothing renders on
		// the shader-only render systems (no fixed function)
		if(Ogre::Root::getSingleton().getRenderSystem()->getCapabilities()
			->hasCapability(Ogre::RSC_FIXED_FUNCTION) == false)
		{
			viewport->setMaterialScheme(
				Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
		}
#endif
	}
	//---------------------------------------------------------
	Ogre::MaterialPtr RenderBackend::getOrCreateSpriteMaterial(
		Ogre::TexturePtr const & texture, SpriteQuad::FilterMode filter,
		SpriteQuad::AddressMode addressing)
	{
		oAssert(texture);
		// the honest v1 sprite recipe: unlit, vertex colours tracked (the
		// tint), alpha-BLENDED, depth-checked/not-written, two-sided;
		// generated - material scripts stay banned (Ogre-Next has no material
		// scripts). The name carries the sampler (SpriteQuad::samplerName) so
		// two sprites of one texture but different sampling get DISTINCT
		// materials instead of stomping each other's filter/addressing.
		const String materialName =
			SpriteQuad::samplerName(texture->getName(), filter, addressing);
		Ogre::MaterialManager & materialManager =
			Ogre::MaterialManager::getSingleton();
		if(materialManager.resourceExists(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			return materialManager.getByName(materialName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		}
		Ogre::MaterialPtr material = materialManager.create(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		// the 2D layer never joins the shadow pass: unlit sprites cannot show
		// a received shadow (no lighting stage consumes the factor), so the
		// receiver stage is kept out of their generated shaders entirely
		material->setReceiveShadows(false);
		Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
		pass->setLightingEnabled(false);
		pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
		pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
		pass->setDepthWriteEnabled(false);
		pass->setCullingMode(Ogre::CULL_NONE);
		Ogre::TextureUnitState* textureUnit = pass->createTextureUnitState();
		textureUnit->setTexture(texture);
		textureUnit->setTextureAddressingMode(
			(addressing == SpriteQuad::ADDRESS_WRAP)
			? Ogre::TextureUnitState::TAM_WRAP
			: Ogre::TextureUnitState::TAM_CLAMP);
		// FO_NONE = point (crisp pixel art), FO_LINEAR = bilinear (the
		// DrawLayer2D point-sampling recipe, generalized to a runtime choice)
		const Ogre::FilterOptions filterOption =
			(filter == SpriteQuad::FILTER_POINT)
			? Ogre::FO_NONE : Ogre::FO_LINEAR;
		textureUnit->setTextureFiltering(filterOption, filterOption,
			Ogre::FO_NONE);
		return material;
	}
	//---------------------------------------------------------
	void RenderBackend::applyZOrder(Ogre::MovableObject* object, int zOrder)
	{
		oAssert(object);
		// all 2D content lives in RENDER_QUEUE_MAIN; zOrder maps to render
		// PRIORITY (the only painter key classic honours for these transparent
		// depth-write-off renderables - see the header). Priority is a ushort
		// centred on the renderable default so the clamped zOrder span stays
		// positive and straddles other MAIN transparents by sign.
		const int priority = static_cast<int>(Ogre::Renderable::DEFAULT_PRIORITY)
			+ std::clamp(zOrder, SpriteQuad::ZORDER_MIN, SpriteQuad::ZORDER_MAX);
		object->setRenderQueueGroupAndPriority(Ogre::RENDER_QUEUE_MAIN,
			static_cast<Ogre::ushort>(priority));
	}
}
