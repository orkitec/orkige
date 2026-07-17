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

#include <algorithm>
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
		// gradient sky dome, and nothing else in the set. The rest (dynamic
		// shadows, hemisphere ambient, sun-exposure linkage, animated
		// normal-mapped water, offscreen-owned 2D layers) are registered next-only
		// deltas; screen-space refraction + IBL are absent on both.
		system->mImpl->caps = (1u << static_cast<int>(RenderCaps::SkyDome));
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
	Ogre::uint8 RenderBackend::renderQueueForZOrder(int zOrder)
	{
		// RENDER_QUEUE_MAIN (50) +- 40 keeps sprites inside the valid
		// render queue range and clear of the overlay queues (>= 95)
		const int queue = static_cast<int>(Ogre::RENDER_QUEUE_MAIN) +
			std::clamp(zOrder, SpriteQuad::ZORDER_MIN, SpriteQuad::ZORDER_MAX);
		return static_cast<Ogre::uint8>(queue);
	}
}
