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
		// material). The clone must drop the source's RTSS-generated
		// technique: its shaders were built WITH the receiver stage, and the
		// resolver only regenerates techniques it does not find.
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
				variant = original->clone(variantName);
				variant->setReceiveShadows(false);
#ifdef USE_RTSHADER_SYSTEM
				if(Ogre::RTShader::ShaderGenerator* generator =
					Ogre::RTShader::ShaderGenerator::getSingletonPtr())
				{
					const String scheme =
						Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME;
					// drop the cloned generated technique(s) so the resolver
					// builds fresh receiver-less shaders for the variant
					for(unsigned short technique = variant->getNumTechniques();
						technique > 0; --technique)
					{
						if(variant->getTechnique(technique - 1)->getSchemeName()
							== scheme)
						{
							variant->removeTechnique(technique - 1);
						}
					}
					// carry the source's CUSTOM render state over (the
					// Cook-Torrance/normal-map stages of a generated surface
					// material live there; without the copy the variant would
					// fall back to plain FFP lighting and change its look)
					Ogre::RTShader::RenderState* source =
						generator->getRenderState(scheme, original->getName(),
							original->getGroup(), 0);
					Ogre::RTShader::RenderState* target =
						generator->getRenderState(scheme, variantName,
							original->getGroup(), 0);
					for(Ogre::RTShader::SubRenderState* srs :
						source->getSubRenderStates())
					{
						Ogre::RTShader::SubRenderState* copy =
							generator->createSubRenderState(srs->getType());
						copy->copyFrom(*srs);
						target->addTemplateSubRenderState(copy);
					}
				}
#endif // USE_RTSHADER_SYSTEM
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
		for(unsigned int each = 0;
			each < this->mImpl->entity->getNumSubEntities(); ++each)
		{
			this->mImpl->entity->getSubEntity(each)->setMaterial(material);
		}
		// a fresh material assignment resets the instance to that material's
		// own (receiving) shadow state - callers re-apply setReceiveShadows
		// after it (@see MeshInstance::setReceiveShadows)
		this->mImpl->receiveRestore.clear();
		return true;
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
}
