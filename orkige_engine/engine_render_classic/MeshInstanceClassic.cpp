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
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void MeshInstance::detach()
	{
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
	}
	//---------------------------------------------------------
	void MeshInstance::setCastShadows(bool cast)
	{
		this->mImpl->entity->setCastShadows(cast);
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
		for(unsigned int each = 0;
			each < this->mImpl->entity->getNumSubEntities(); ++each)
		{
			this->mImpl->entity->getSubEntity(each)->setMaterial(material);
		}
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
