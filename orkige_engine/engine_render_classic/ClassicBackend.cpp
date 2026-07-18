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
