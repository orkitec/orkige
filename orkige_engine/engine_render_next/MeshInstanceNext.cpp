/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	MeshInstanceNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file MeshInstanceNext.cpp
//! @brief Ogre-Next implementation of the MeshInstance facade
//! @remarks wraps an Ogre::Item over a v2 mesh; the mesh arrives
//! through RenderBackend::ensureV2Mesh (assimp/.mesh -> importV1, see
//! MeshLoaderNext.cpp). setVertexColourUnlit swaps every sub-item onto
//! a generated HlmsUnlit datablock (vertex colours flow via the
//! hlms_colour vertex-format property), keeping a diffuse texture when
//! the original PBS datablock had one - the Next analog of classic's
//! Pass::setLightingEnabled(false)+setVertexColourTracking. The
//! animation surface runs over the v2 SkeletonInstance/
//! SkeletonAnimation when the mesh has one; the assimp importer
//! produces static meshes only (see MeshLoaderNext.cpp), so today that
//! path only answers honestly with "no animations".

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreItem.h>
#include <OgreSubItem.h>
#include <OgreMesh2.h>
#include <OgreResourceGroupManager.h>
#include <Animation/OgreSkeletonInstance.h>
#include <Animation/OgreSkeletonAnimation.h>

namespace Orkige
{
	namespace
	{
		//! the skeleton animation by name or NULL (never throws)
		Ogre::SkeletonAnimation* skeletonAnimation(Ogre::Item* item,
			String const & name)
		{
			Ogre::SkeletonInstance* skeleton = item->getSkeletonInstance();
			if(!skeleton)
			{
				return NULL;
			}
			const Ogre::IdString wanted(name);
			for(Ogre::SkeletonAnimation & each :
				skeleton->getAnimationsNonConst())
			{
				if(each.getName() == wanted)
				{
					return &each;
				}
			}
			return NULL;
		}
	}
	//---------------------------------------------------------
	optr<MeshInstance> RenderBackend::createMeshInstance(
		Ogre::SceneManager* sceneManager, String const & meshName)
	{
		oAssert(sceneManager);
		if(!RenderBackend::ensureV2Mesh(sceneManager, meshName))
		{
			return optr<MeshInstance>();	// error already logged
		}
		Ogre::Item* item = sceneManager->createItem(meshName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
			Ogre::SCENE_DYNAMIC);
		item->setQueryFlags(RenderWorld::QUERYFLAG_DEFAULT);
		optr<MeshInstance> handle(new MeshInstance());
		handle->mImpl->item = item;
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
		// late destruction guard, same rule as RenderNode
		if(this->mImpl->item && RenderBackend::system())
		{
			if(this->mImpl->item->isAttached())
			{
				this->mImpl->item->detachFromParent();
			}
			this->mImpl->creator->destroyItem(this->mImpl->item);
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void MeshInstance::attachTo(optr<RenderNode> const & node)
	{
		oAssert(node);
		if(this->mImpl->item->isAttached())
		{
			this->mImpl->item->detachFromParent();
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->item);
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void MeshInstance::detach()
	{
		if(this->mImpl->item->isAttached())
		{
			this->mImpl->item->detachFromParent();
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
		this->mImpl->item->setVisible(visible);
	}
	//---------------------------------------------------------
	void MeshInstance::setCastShadows(bool cast)
	{
		this->mImpl->item->setCastShadows(cast);
	}
	//---------------------------------------------------------
	AABB MeshInstance::getLocalBounds() const
	{
		// v2 carries center/half-size Aabbs; the facade speaks min/max
		const Ogre::Aabb localAabb = this->mImpl->item->getLocalAabb();
		return AABB(localAabb.getMinimum(), localAabb.getMaximum());
	}
	//---------------------------------------------------------
	void MeshInstance::setQueryFlags(unsigned int flags)
	{
		this->mImpl->item->setQueryFlags(flags);
	}
	//---------------------------------------------------------
	void MeshInstance::setVertexColourUnlit()
	{
		// the Next analog of classic's pass fixup: per sub-item, swap the
		// generated PBS datablock for an Unlit one ("<orig>/VCUnlit",
		// idempotent by name) that keeps the diffuse texture; vertex
		// colours multiply in automatically when the vertex format has
		// VES_DIFFUSE (the hlms_colour property)
		for(size_t each = 0; each < this->mImpl->item->getNumSubItems(); ++each)
		{
			Ogre::SubItem* subItem = this->mImpl->item->getSubItem(each);
			Ogre::HlmsDatablock* current = subItem->getDatablock();
			const String unlitName = this->mImpl->meshName + "/VCUnlit" +
				std::to_string(each);
			subItem->setDatablock(
				RenderBackend::getOrCreateVertexColourUnlitDatablock(unlitName,
					RenderBackend::datablockDiffuseTexture(current)));
		}
	}
	//---------------------------------------------------------
	size_t MeshInstance::getNumSubMeshes() const
	{
		return this->mImpl->item->getNumSubItems();
	}
	//---------------------------------------------------------
	bool MeshInstance::subMeshHasTexture(size_t index) const
	{
		if(index >= this->mImpl->item->getNumSubItems())
		{
			return false;
		}
		return RenderBackend::datablockDiffuseTexture(
			this->mImpl->item->getSubItem(index)->getDatablock()) != NULL;
	}
	//---------------------------------------------------------
	StringVector MeshInstance::getAnimationNames() const
	{
		StringVector names;
		if(Ogre::SkeletonInstance* skeleton =
			this->mImpl->item->getSkeletonInstance())
		{
			for(Ogre::SkeletonAnimation const & each :
				skeleton->getAnimations())
			{
				names.push_back(each.getName().getFriendlyText());
			}
		}
		return names;
	}
	//---------------------------------------------------------
	bool MeshInstance::hasAnimation(String const & name) const
	{
		return skeletonAnimation(this->mImpl->item, name) != NULL;
	}
	//---------------------------------------------------------
	StringVector MeshInstance::getEnabledAnimations() const
	{
		StringVector names;
		if(Ogre::SkeletonInstance* skeleton =
			this->mImpl->item->getSkeletonInstance())
		{
			for(Ogre::SkeletonAnimation const & each :
				skeleton->getAnimations())
			{
				if(each.getEnabled())
				{
					names.push_back(each.getName().getFriendlyText());
				}
			}
		}
		return names;
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimationEnabled(String const & name, bool enabled)
	{
		if(Ogre::SkeletonAnimation* animation =
			skeletonAnimation(this->mImpl->item, name))
		{
			animation->setEnabled(enabled);
		}
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimationLoop(String const & name, bool loop)
	{
		if(Ogre::SkeletonAnimation* animation =
			skeletonAnimation(this->mImpl->item, name))
		{
			animation->setLoop(loop);
		}
	}
	//---------------------------------------------------------
	void MeshInstance::addAnimationTime(String const & name, float deltaSeconds)
	{
		if(Ogre::SkeletonAnimation* animation =
			skeletonAnimation(this->mImpl->item, name))
		{
			animation->addTime(deltaSeconds);
		}
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimationTime(String const & name, float seconds)
	{
		if(Ogre::SkeletonAnimation* animation =
			skeletonAnimation(this->mImpl->item, name))
		{
			animation->setTime(seconds);
		}
	}
	//---------------------------------------------------------
	float MeshInstance::getAnimationTime(String const & name) const
	{
		Ogre::SkeletonAnimation* animation =
			skeletonAnimation(this->mImpl->item, name);
		return animation ? animation->getCurrentTime() : 0.0f;
	}
	//---------------------------------------------------------
	float MeshInstance::getAnimationLength(String const & name) const
	{
		Ogre::SkeletonAnimation* animation =
			skeletonAnimation(this->mImpl->item, name);
		return animation ? animation->getDuration() : 0.0f;
	}
	//---------------------------------------------------------
	bool MeshInstance::hasAnimationEnded(String const & name) const
	{
		// the v2 SkeletonAnimation has no hasEnded - clock vs duration
		// (the facade contract for non-looping playback)
		Ogre::SkeletonAnimation* animation =
			skeletonAnimation(this->mImpl->item, name);
		if(!animation)
		{
			return false;	// same unknown-name answer as classic
		}
		return !animation->getLoop() &&
			animation->getCurrentTime() >= animation->getDuration();
	}
}
