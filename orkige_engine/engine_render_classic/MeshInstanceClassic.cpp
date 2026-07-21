/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	MeshInstanceClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file MeshInstanceClassic.cpp
//! @brief classic-OGRE implementation of the MeshInstance facade
//! @remarks wraps an Ogre::Entity; meshes resolve through EVERY resource
//! group (AUTODETECT) so engine media and project assets both work by
//! plain file name - the same rule ModelComponent applies today

#include "engine_render_classic/ClassicBackend.h"
#include <core_debug/DebugMacros.h>

#ifdef USE_RTSHADER_SYSTEM
#	include <OgreRTShaderSystem.h>
#endif

namespace Orkige
{
	namespace
	{
		//! the animation state by name or NULL (never throws)
		Ogre::AnimationState* animationState(Ogre::Entity* entity,
			String const & name)
		{
			Ogre::AnimationStateSet* states = entity->getAllAnimationStates();
			if(!states || !states->hasAnimationState(name))
			{
				return NULL;
			}
			return states->getAnimationState(name);
		}
		//! true when any of @p mesh's sub-meshes already declares a vertex
		//! element with @p semantic (shared or dedicated vertex data)
		bool meshDeclares(Ogre::MeshPtr const & mesh,
			Ogre::VertexElementSemantic semantic)
		{
			for(unsigned short each = 0; each < mesh->getNumSubMeshes(); ++each)
			{
				Ogre::SubMesh* sub = mesh->getSubMesh(each);
				Ogre::VertexData* data =
					sub->useSharedVertices ? mesh->sharedVertexData : sub->vertexData;
				if(data && data->vertexDeclaration
					->findElementBySemantic(semantic))
				{
					return true;
				}
			}
			return false;
		}
		//! @brief clone @p original as a per-instance VARIANT that keeps its
		//! generated look: the cloned RTSS technique is dropped (its shaders
		//! were built for the SOURCE's state; the resolver only regenerates
		//! techniques it does not find) and the source's CUSTOM render state
		//! (the Cook-Torrance/normal-map stages of a generated surface
		//! material) is copied over, so the variant regenerates to the same
		//! shading instead of falling back to plain FFP lighting. The shared
		//! recipe of the no-receive and accent variants.
		Ogre::MaterialPtr cloneMaterialKeepingRenderState(
			Ogre::MaterialPtr const & original, String const & variantName)
		{
			Ogre::MaterialPtr variant = original->clone(variantName);
#ifdef USE_RTSHADER_SYSTEM
			if(Ogre::RTShader::ShaderGenerator* generator =
				Ogre::RTShader::ShaderGenerator::getSingletonPtr())
			{
				const String scheme =
					Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME;
				for(unsigned short technique = variant->getNumTechniques();
					technique > 0; --technique)
				{
					if(variant->getTechnique(technique - 1)->getSchemeName()
						== scheme)
					{
						variant->removeTechnique(technique - 1);
					}
				}
				// the per-material render state only EXISTS for materials with
				// a REGISTERED shader-based technique (the scheme's technique
				// entries; a bare getRenderState on an unregistered name
				// answers NULL). The source has one when it is a generated
				// surface material - a plain/imported material answers NULL
				// and needs no copy, the resolver's default FFP emulation is
				// its look. The variant must be registered FIRST, then its
				// fresh render state can take the copied stages.
				Ogre::RTShader::RenderState* source =
					generator->getRenderState(scheme, original->getName(),
						original->getGroup(), 0);
				if(source && generator->createShaderBasedTechnique(*variant,
					Ogre::MaterialManager::DEFAULT_SCHEME_NAME, scheme))
				{
					Ogre::RTShader::RenderState* target =
						generator->getRenderState(scheme, variantName,
							variant->getGroup(), 0);
					if(target)
					{
						for(Ogre::RTShader::SubRenderState* srs :
							source->getSubRenderStates())
						{
							Ogre::RTShader::SubRenderState* copy =
								generator->createSubRenderState(srs->getType());
							copy->copyFrom(*srs);
							target->addTemplateSubRenderState(copy);
						}
					}
					generator->invalidateMaterial(scheme, variantName,
						variant->getGroup());
				}
			}
#endif // USE_RTSHADER_SYSTEM
			return variant;
		}
	}
	//---------------------------------------------------------
	//! restore the pre-accent materials and retire the accent variants
	//! (@see MeshInstance::setTint; a no-op while unaccented)
	void RenderBackend::resetMeshAccents(MeshInstance::Impl* impl)
	{
		if(impl->accentVariants.empty())
		{
			return;
		}
		Ogre::Entity* entity = impl->entity;
		for(unsigned int each = 0; each < entity->getNumSubEntities() &&
			each < impl->accentRestore.size(); ++each)
		{
			entity->getSubEntity(each)->setMaterial(
				impl->accentRestore[each]);
		}
		for(Ogre::MaterialPtr const & variant : impl->accentVariants)
		{
			if(!variant)
			{
				continue;
			}
#ifdef USE_RTSHADER_SYSTEM
			// unregister the variant's shader-based technique first - the
			// generator must not keep entries pointing into a dead material
			if(Ogre::RTShader::ShaderGenerator* generator =
				Ogre::RTShader::ShaderGenerator::getSingletonPtr())
			{
				generator->removeAllShaderBasedTechniques(variant->getName(),
					variant->getGroup());
			}
#endif // USE_RTSHADER_SYSTEM
			Ogre::MaterialManager::getSingleton().remove(variant);
		}
		impl->accentRestore.clear();
		impl->accentVariants.clear();
	}
	//---------------------------------------------------------
	//! @brief realize the accent state (@see MeshInstance::setTint):
	//! neutral values restore-exactly; anything else swaps each
	//! sub-entity onto its per-instance variant clone (built once) and
	//! parameter-drives it from the ORIGINAL material's values - diffuse/
	//! ambient scaled by the tint, self-illumination raised by the boost
	void RenderBackend::applyMeshAccents(MeshInstance::Impl* impl)
	{
		Color const & tint = impl->accentTint;
		Color const & boost = impl->accentBoost;
		const bool neutral =
			tint.r == 1.0f && tint.g == 1.0f && tint.b == 1.0f &&
			boost.r == 0.0f && boost.g == 0.0f && boost.b == 0.0f;
		if(neutral)
		{
			RenderBackend::resetMeshAccents(impl);
			return;
		}
		Ogre::Entity* entity = impl->entity;
		if(impl->accentVariants.empty())
		{
			for(unsigned int each = 0;
				each < entity->getNumSubEntities(); ++each)
			{
				Ogre::MaterialPtr original =
					entity->getSubEntity(each)->getMaterial();
				impl->accentRestore.push_back(original);
				if(!original)
				{
					impl->accentVariants.push_back(Ogre::MaterialPtr());
					continue;
				}
				Ogre::MaterialPtr variant = cloneMaterialKeepingRenderState(
					original, RenderBackend::generateName(
						original->getName() + "/Accent"));
				impl->accentVariants.push_back(variant);
				entity->getSubEntity(each)->setMaterial(variant);
			}
		}
		for(size_t each = 0; each < impl->accentVariants.size(); ++each)
		{
			Ogre::MaterialPtr const & variant = impl->accentVariants[each];
			Ogre::MaterialPtr const & original = impl->accentRestore[each];
			if(!variant || !original)
			{
				continue;
			}
			Ogre::Pass* source =
				original->getTechnique(0)->getPass(0);
			Ogre::Pass* target = variant->getTechnique(0)->getPass(0);
			const Ogre::ColourValue diffuse = source->getDiffuse();
			const Ogre::ColourValue ambient = source->getAmbient();
			const Ogre::ColourValue glow = source->getSelfIllumination();
			target->setDiffuse(diffuse.r * tint.r, diffuse.g * tint.g,
				diffuse.b * tint.b, diffuse.a);
			target->setAmbient(ambient.r * tint.r, ambient.g * tint.g,
				ambient.b * tint.b);
			target->setSelfIllumination(glow.r + boost.r,
				glow.g + boost.g, glow.b + boost.b);
		}
	}
	//---------------------------------------------------------
	optr<MeshInstance> RenderBackend::createMeshInstance(
		Ogre::SceneManager* sceneManager, String const & meshName)
	{
		oAssert(sceneManager);
		Ogre::Entity* entity = NULL;
		try
		{
			entity = sceneManager->createEntity(
				RenderBackend::generateName("RenderFacade/Mesh"), meshName,
				Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
		}
		catch(Ogre::Exception const & e)
		{
			oDebugError("engine", 0, "RenderWorld: mesh '" << meshName
				<< "' failed to load: " << e.getDescription());
			return optr<MeshInstance>();
		}
		entity->setQueryFlags(RenderWorld::QUERYFLAG_DEFAULT);
		// tag the 3D tier so the bloom scene split feeds it to the glow source
		RenderBackend::tagScene3D(entity);
		optr<MeshInstance> handle(new MeshInstance());
		handle->mImpl->entity = entity;
		handle->mImpl->creator = sceneManager;
		handle->mImpl->meshName = meshName;
		return handle;
	}
	//---------------------------------------------------------
	MeshInstance::MeshInstance()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	MeshInstance::~MeshInstance()
	{
		if(this->mImpl->entity)
		{
			// a baked entity leaves the static regions with its handle
			RenderBackend::staticBakeUnregister(this->mImpl->entity);
			// drop a refractive-water entity from the grab-hide registry so the
			// scene-grab listener never dereferences a dying handle (empty name
			// = "not a refractive material" -> unregister)
			RenderBackend::noteMeshMaterialForRefraction(this->mImpl->entity,
				String());
			// per-instance accent clones die with the instance
			RenderBackend::resetMeshAccents(this->mImpl);
			if(this->mImpl->entity->isAttached())
			{
				this->mImpl->entity->detachFromParent();
			}
			this->mImpl->creator->destroyEntity(this->mImpl->entity);
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void MeshInstance::attachTo(optr<RenderNode> const & node)
	{
		oAssert(node);
		if(this->mImpl->entity->isAttached())
		{
			this->mImpl->entity->detachFromParent();
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->entity);
		// mesh content on a static node joins the static bake (content
		// created after the node's flag flip registers here; a flip over
		// already-attached content sweeps in RenderNode::setStatic)
		if(RenderBackend::nodeIsStatic(node))
		{
			RenderBackend::staticBakeRegister(this->mImpl->entity,
				RenderBackend::sceneNode(node));
		}
		else
		{
			RenderBackend::staticBakeUnregister(this->mImpl->entity);
		}
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void MeshInstance::detach()
	{
		RenderBackend::staticBakeUnregister(this->mImpl->entity);
		if(this->mImpl->entity->isAttached())
		{
			this->mImpl->entity->detachFromParent();
		}
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	String const & MeshInstance::getMeshName() const
	{
		return this->mImpl->meshName;
	}
	//---------------------------------------------------------
	void MeshInstance::setVisible(bool visible)
	{
		this->mImpl->entity->setVisible(visible);
		if(RenderBackend::staticBakeContains(this->mImpl->entity))
		{
			// region membership follows the visible flag - re-filter
			RenderBackend::staticBakeMarkDirty();
		}
	}
	//---------------------------------------------------------
	void MeshInstance::setCastShadows(bool cast)
	{
		this->mImpl->entity->setCastShadows(cast);
		if(RenderBackend::staticBakeContains(this->mImpl->entity))
		{
			// the cast flag picks the region bucket - re-sort
			RenderBackend::staticBakeMarkDirty();
		}
	}
	//---------------------------------------------------------
	void MeshInstance::setReceiveShadows(bool receive)
	{
		Ogre::Entity* entity = this->mImpl->entity;
		// accents layer ON TOP of the receive variant - drop them first so
		// the walk below sees the real assignment; the component layer
		// re-applies accents after the flag (@see MeshInstance::setTint)
		RenderBackend::resetMeshAccents(this->mImpl);
		this->mImpl->accentTint = Color(1.0f, 1.0f, 1.0f, 1.0f);
		this->mImpl->accentBoost = Color(0.0f, 0.0f, 0.0f, 1.0f);
		if(receive)
		{
			// restore the exact pre-toggle assignment (a no-op while the
			// instance never opted out)
			if(this->mImpl->receiveRestore.empty())
			{
				return;
			}
			for(unsigned int each = 0; each < entity->getNumSubEntities() &&
				each < this->mImpl->receiveRestore.size(); ++each)
			{
				entity->getSubEntity(each)->setMaterial(
					this->mImpl->receiveRestore[each]);
			}
			this->mImpl->receiveRestore.clear();
			return;
		}
		if(!this->mImpl->receiveRestore.empty())
		{
			return;	// already switched to the no-receive variants
		}
		// shadow receipt is a MATERIAL property here, so a per-instance
		// opt-out swaps each sub-entity to a no-receive VARIANT of its
		// current material ("<name>/NoRecv", created once per source
		// material). The clone drops the source's RTSS-generated technique
		// and carries its custom render state over (the shared variant
		// recipe, @see cloneMaterialKeepingRenderState) - its shaders were
		// built WITH the receiver stage, and the resolver only regenerates
		// techniques it does not find.
		for(unsigned int each = 0; each < entity->getNumSubEntities(); ++each)
		{
			Ogre::MaterialPtr original =
				entity->getSubEntity(each)->getMaterial();
			this->mImpl->receiveRestore.push_back(original);
			if(!original)
			{
				continue;
			}
			const String variantName = original->getName() + "/NoRecv";
			Ogre::MaterialPtr variant = Ogre::MaterialManager::getSingleton()
				.getByName(variantName, original->getGroup());
			if(!variant)
			{
				variant = cloneMaterialKeepingRenderState(original, variantName);
				variant->setReceiveShadows(false);
			}
			entity->getSubEntity(each)->setMaterial(variant);
		}
	}
	//---------------------------------------------------------
	AABB MeshInstance::getLocalBounds() const
	{
		return this->mImpl->entity->getBoundingBox();
	}
	//---------------------------------------------------------
	void MeshInstance::setQueryFlags(unsigned int flags)
	{
		this->mImpl->entity->setQueryFlags(flags);
	}
	//---------------------------------------------------------
	void MeshInstance::setVertexColourUnlit()
	{
		// PrimitiveUtil::makeEntityVertexColourUnlit, folded into the
		// facade: Codec_Assimp keeps lighting on for imported materials
		// (it generates normals), which drowns vertex colours under
		// ambient-only light; idempotent by construction
		for(unsigned int each = 0;
			each < this->mImpl->entity->getNumSubEntities(); ++each)
		{
			Ogre::Pass* pass = this->mImpl->entity->getSubEntity(each)
				->getMaterial()->getTechnique(0)->getPass(0);
			pass->setLightingEnabled(false);
			pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
		}
	}
	//---------------------------------------------------------
	size_t MeshInstance::getNumSubMeshes() const
	{
		return this->mImpl->entity->getNumSubEntities();
	}
	//---------------------------------------------------------
	bool MeshInstance::subMeshHasTexture(size_t index) const
	{
		if(index >= this->mImpl->entity->getNumSubEntities())
		{
			return false;
		}
		Ogre::MaterialPtr material = this->mImpl->entity
			->getSubEntity(static_cast<unsigned int>(index))->getMaterial();
		if(!material || material->getNumTechniques() == 0)
		{
			return false;
		}
		Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
		return pass && pass->getNumTextureUnitStates() > 0;
	}
	//---------------------------------------------------------
	bool MeshInstance::setMaterial(String const & materialName)
	{
		Ogre::MaterialPtr material = Ogre::MaterialManager::getSingleton()
			.getByName(materialName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		if(!material)
		{
			oDebugError("engine", 0, "MeshInstance('"
				<< this->mImpl->entity->getMesh()->getName()
				<< "'): no material '" << materialName
				<< "' (create it via RenderSystem::createMaterial first)");
			return false;
		}
		// a normal-mapped material needs tangents to perturb the lit normal;
		// build them on demand (mirrors the next flavor's load-time build) so a
		// plain material never pays for it. UV-less meshes can't be normal-mapped
		// anyway - nothing to build.
		if(RenderBackend::materialUsesNormalMap(materialName))
		{
			Ogre::MeshPtr mesh = this->mImpl->entity->getMesh();
			if(mesh && !meshDeclares(mesh, Ogre::VES_TANGENT)
				&& meshDeclares(mesh, Ogre::VES_TEXTURE_COORDINATES))
			{
				try
				{
					mesh->buildTangentVectors();
				}
				catch(Ogre::Exception const & e)
				{
					oDebugWarning(false, "MeshInstance('" << mesh->getName()
						<< "'): tangent generation failed (" << e.getDescription()
						<< ") - the normal-mapped material '" << materialName
						<< "' lights without normal perturbation");
				}
			}
		}
		// the fresh assignment below replaces any accent variants outright -
		// retire them first (their clones would leak; the remembered accent
		// resets too, the component layer re-applies it after the material,
		// @see MeshInstance::setTint)
		RenderBackend::resetMeshAccents(this->mImpl);
		this->mImpl->accentTint = Color(1.0f, 1.0f, 1.0f, 1.0f);
		this->mImpl->accentBoost = Color(0.0f, 0.0f, 0.0f, 1.0f);
		for(unsigned int each = 0;
			each < this->mImpl->entity->getNumSubEntities(); ++each)
		{
			this->mImpl->entity->getSubEntity(each)->setMaterial(material);
		}
		// a fresh material assignment resets the instance to that material's
		// own (receiving) shadow state - callers re-apply setReceiveShadows
		// after it (@see MeshInstance::setReceiveShadows)
		this->mImpl->receiveRestore.clear();
		// register/unregister this entity with the screen-space refraction
		// grab target: a refractive water surface must be HIDDEN from the grab it
		// samples (a no-op for every non-refractive material)
		RenderBackend::noteMeshMaterialForRefraction(this->mImpl->entity,
			materialName);
		return true;
	}
	//---------------------------------------------------------
	void MeshInstance::setTint(Color const & tint)
	{
		this->mImpl->accentTint = Color(tint.r, tint.g, tint.b, 1.0f);
		RenderBackend::applyMeshAccents(this->mImpl);
	}
	//---------------------------------------------------------
	void MeshInstance::setEmissiveBoost(Color const & boost)
	{
		this->mImpl->accentBoost = Color(boost.r, boost.g, boost.b, 1.0f);
		RenderBackend::applyMeshAccents(this->mImpl);
	}
	//---------------------------------------------------------
	StringVector MeshInstance::getAnimationNames() const
	{
		StringVector names;
		Ogre::AnimationStateSet* states =
			this->mImpl->entity->getAllAnimationStates();
		if(states)
		{
			for(auto const & each : states->getAnimationStates())
			{
				names.push_back(each.first);
			}
		}
		return names;
	}
	//---------------------------------------------------------
	bool MeshInstance::hasAnimation(String const & name) const
	{
		return animationState(this->mImpl->entity, name) != NULL;
	}
	//---------------------------------------------------------
	StringVector MeshInstance::getEnabledAnimations() const
	{
		StringVector names;
		Ogre::AnimationStateSet* states =
			this->mImpl->entity->getAllAnimationStates();
		if(states)
		{
			for(Ogre::AnimationState const * each :
				states->getEnabledAnimationStates())
			{
				names.push_back(each->getAnimationName());
			}
		}
		return names;
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimationEnabled(String const & name, bool enabled)
	{
		if(Ogre::AnimationState* state =
			animationState(this->mImpl->entity, name))
		{
			state->setEnabled(enabled);
		}
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimationLoop(String const & name, bool loop)
	{
		if(Ogre::AnimationState* state =
			animationState(this->mImpl->entity, name))
		{
			state->setLoop(loop);
		}
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimationWeight(String const & name, float weight)
	{
		if(Ogre::AnimationState* state =
			animationState(this->mImpl->entity, name))
		{
			state->setWeight(weight);
		}
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimatedBounds(bool enabled)
	{
		// a swinging limb must keep the culling AABB honest - OGRE recomputes
		// the entity bounds from the live bone positions when this is set (only
		// meaningful on a skinned entity)
		if(this->mImpl->entity->hasSkeleton())
		{
			this->mImpl->entity->setUpdateBoundingBoxFromSkeleton(enabled);
		}
	}
	//---------------------------------------------------------
	void MeshInstance::addAnimationTime(String const & name, float deltaSeconds)
	{
		if(Ogre::AnimationState* state =
			animationState(this->mImpl->entity, name))
		{
			state->addTime(deltaSeconds);
		}
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimationTime(String const & name, float seconds)
	{
		if(Ogre::AnimationState* state =
			animationState(this->mImpl->entity, name))
		{
			state->setTimePosition(seconds);
		}
	}
	//---------------------------------------------------------
	float MeshInstance::getAnimationTime(String const & name) const
	{
		Ogre::AnimationState* state = animationState(this->mImpl->entity, name);
		return state ? state->getTimePosition() : 0.0f;
	}
	//---------------------------------------------------------
	float MeshInstance::getAnimationLength(String const & name) const
	{
		Ogre::AnimationState* state = animationState(this->mImpl->entity, name);
		return state ? state->getLength() : 0.0f;
	}
	//---------------------------------------------------------
	bool MeshInstance::hasAnimationEnded(String const & name) const
	{
		Ogre::AnimationState* state = animationState(this->mImpl->entity, name);
		return state ? state->hasEnded() : false;
	}
	//---------------------------------------------------------
	bool MeshInstance::getBoneWorldTransform(String const & boneName,
		Vec3 & outPosition, Quat & outOrientation, Vec3 & outScale) const
	{
		Ogre::Entity* entity = this->mImpl->entity;
		if(!entity->hasSkeleton())
		{
			return false;
		}
		Ogre::SkeletonInstance* skeleton = entity->getSkeleton();
		if(!skeleton || !skeleton->hasBone(boneName))
		{
			return false;
		}
		// pose the skeleton to its CURRENT animation state before reading, so a
		// prop follows THIS frame's clip time (the entity would otherwise only
		// update its bones when it renders)
		entity->_updateAnimation();
		Ogre::Bone* bone = skeleton->getBone(boneName);
		Ogre::Node* node = entity->getParentNode();
		// the bone's full transform is skeleton-local (relative to the entity's
		// node); the node's full transform carries it to world space
		const Ogre::Affine3 world = (node ? node->_getFullTransform()
			: Ogre::Affine3::IDENTITY) * bone->_getFullTransform();
		world.decomposition(outPosition, outScale, outOrientation);
		return true;
	}
}
