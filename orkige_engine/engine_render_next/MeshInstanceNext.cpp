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
//! SkeletonAnimation of a skinned import (MeshLoaderNext.cpp carries
//! skeleton + clips through importV1); clip NAMES come from the
//! backend's skinned-mesh registry where one exists, because the v2
//! IdString only keeps readable names in debug builds. Animated
//! bounds are derived here (setAnimatedBounds): Ogre-Next keeps an
//! item's local Aabb at the bind pose, so the armed instance rebuilds
//! the box from the LIVE bone poses (composed from the pose TRS
//! values, never the SoA derived caches - those are only valid
//! mid-frame) expanded by the import-time bone radius, the classic
//! bounds-from-skeleton semantics.

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreItem.h>
#include <OgreSubItem.h>
#include <OgreMesh2.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>
#include <OgreHlms.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsDatablock.h>
#include <OgreHlmsPbsDatablock.h>
#include <OgreLogManager.h>
#include <OgreException.h>
#include <Animation/OgreSkeletonInstance.h>
#include <Animation/OgreSkeletonAnimation.h>
#include <Animation/OgreBone.h>

#include <vector>

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

		//! the readable name of a v2 skeleton animation: resolved through
		//! the skinned-mesh registry when the mesh imported through the
		//! assimp road (exact strings in EVERY build config), else the
		//! IdString's friendly text (readable in debug builds, a hash
		//! elsewhere - the .mesh-serializer road)
		String animationDisplayName(Ogre::SkeletonAnimation const & animation,
			String const & meshName)
		{
			if(StringVector const * clipNames =
				RenderBackend::skinnedMeshClipNames(meshName))
			{
				for(String const & each : *clipNames)
				{
					if(Ogre::IdString(each) == animation.getName())
					{
						return each;
					}
				}
			}
			return animation.getName().getFriendlyText();
		}

		//! a bone's pose position in SKELETON-LOCAL space, composed from
		//! the live pose TRS values up the parent chain. The SoA derived
		//! caches are deliberately not read - they are only refreshed
		//! during the frame update, and this must answer at any time.
		Ogre::Vector3 boneLocalPosePosition(Ogre::Bone const * bone)
		{
			Ogre::Vector3 position = bone->getPosition();
			Ogre::Bone const * parent = bone->getParent();
			while(parent)
			{
				position = parent->getPosition() + parent->getOrientation() *
					(parent->getScale() * position);
				parent = parent->getParent();
			}
			return position;
		}
	}
	//---------------------------------------------------------
	void RenderBackend::resetMeshAccents(MeshInstance::Impl* impl)
	{
		// restore the pre-accent datablocks and retire the accent variant
		// clones (@see MeshInstance::setTint; a no-op while unaccented)
		if(impl->accentVariants.empty())
		{
			return;
		}
		Ogre::Item* item = impl->item;
		for(size_t each = 0; each < item->getNumSubItems() &&
			each < impl->accentRestore.size(); ++each)
		{
			item->getSubItem(each)->setDatablock(impl->accentRestore[each]);
		}
		for(Ogre::HlmsDatablock* variant : impl->accentVariants)
		{
			if(variant && variant->getCreator())
			{
				variant->getCreator()->destroyDatablock(variant->getName());
			}
		}
		impl->accentRestore.clear();
		impl->accentVariants.clear();
	}
	//---------------------------------------------------------
	void RenderBackend::applyMeshAccents(MeshInstance::Impl* impl)
	{
		// realize the accent state (@see MeshInstance::setTint): neutral
		// values restore-exactly; anything else swaps each PBS sub-item onto
		// its per-instance datablock clone (built once) and parameter-drives
		// it from the ORIGINAL's values - diffuse scaled by the tint,
		// emissive raised by the boost (datablock param writes are
		// const-buffer updates, cheap per call). Non-PBS sub-items (unlit
		// content) are skipped - vertex-colour/sprite tints own that path.
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
		Ogre::Item* item = impl->item;
		if(impl->accentVariants.empty())
		{
			for(size_t each = 0; each < item->getNumSubItems(); ++each)
			{
				Ogre::HlmsDatablock* original =
					item->getSubItem(each)->getDatablock();
				impl->accentRestore.push_back(original);
				if(!original || original->mType != Ogre::HLMS_PBS)
				{
					impl->accentVariants.push_back(NULL);
					continue;
				}
				const String* originalName = original->getNameStr();
				Ogre::HlmsDatablock* variant = original->clone(
					RenderBackend::generateName(
						(originalName ? *originalName : String("datablock"))
						+ "/Accent"));
				impl->accentVariants.push_back(variant);
				item->getSubItem(each)->setDatablock(variant);
			}
		}
		for(size_t each = 0; each < impl->accentVariants.size(); ++each)
		{
			Ogre::HlmsPbsDatablock* variant =
				static_cast<Ogre::HlmsPbsDatablock*>(
					impl->accentVariants[each]);
			Ogre::HlmsPbsDatablock* original =
				static_cast<Ogre::HlmsPbsDatablock*>(
					impl->accentRestore[each]);
			if(!variant || !original)
			{
				continue;
			}
			const Ogre::Vector3 diffuse = original->getDiffuse();
			const Ogre::Vector3 emissive = original->getEmissive();
			variant->setDiffuse(Ogre::Vector3(diffuse.x * tint.r,
				diffuse.y * tint.g, diffuse.z * tint.b));
			variant->setEmissive(Ogre::Vector3(emissive.x + boost.r,
				emissive.y + boost.g, emissive.z + boost.b));
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
			// per-instance accent clones die with the instance
			RenderBackend::resetMeshAccents(this->mImpl);
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
		// mesh content created AFTER its node went static must join the
		// static memory manager too (a node flip carries attached objects
		// along, but a late attach starts dynamic) - align before attaching
		if(RenderBackend::nodeIsStatic(node) != this->mImpl->item->isStatic())
		{
			this->mImpl->item->setStatic(RenderBackend::nodeIsStatic(node));
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->item);
		if(this->mImpl->item->isStatic())
		{
			// snapshot the item's frozen transform/AABB on the next frame
			this->mImpl->creator->notifyStaticAabbDirty(this->mImpl->item);
		}
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void MeshInstance::detach()
	{
		if(this->mImpl->item->isAttached())
		{
			this->mImpl->item->detachFromParent();
		}
		if(this->mImpl->item->isStatic())
		{
			// a detached item is placement-less; return it to the dynamic
			// managers so a later attach starts from the default worldview
			this->mImpl->item->setStatic(false);
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
	void MeshInstance::setReceiveShadows(bool receive)
	{
		Ogre::Item* item = this->mImpl->item;
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
			for(size_t each = 0; each < item->getNumSubItems() &&
				each < this->mImpl->receiveRestore.size(); ++each)
			{
				item->getSubItem(each)->setDatablock(
					this->mImpl->receiveRestore[each]);
			}
			this->mImpl->receiveRestore.clear();
			return;
		}
		if(!this->mImpl->receiveRestore.empty())
		{
			return;	// already switched to the no-receive variants
		}
		// shadow receipt is a DATABLOCK property here, so a per-instance
		// opt-out swaps each sub-item to a no-receive VARIANT of its current
		// PBS datablock ("<name>/NoRecv", cloned once per source datablock);
		// non-PBS datablocks (unlit content) never receive - left untouched
		Ogre::HlmsManager* hlmsManager =
			RenderBackend::ogreRoot()->getHlmsManager();
		for(size_t each = 0; each < item->getNumSubItems(); ++each)
		{
			Ogre::HlmsDatablock* original =
				item->getSubItem(each)->getDatablock();
			this->mImpl->receiveRestore.push_back(original);
			if(!original || original->mType != Ogre::HLMS_PBS)
			{
				continue;
			}
			const String* originalName = original->getNameStr();
			if(!originalName)
			{
				continue;
			}
			const String variantName = *originalName + "/NoRecv";
			Ogre::HlmsDatablock* variant =
				hlmsManager->getDatablockNoDefault(variantName);
			if(!variant)
			{
				variant = original->clone(variantName);
				static_cast<Ogre::HlmsPbsDatablock*>(variant)
					->setReceiveShadows(false);
			}
			item->getSubItem(each)->setDatablock(variant);
		}
	}
	//---------------------------------------------------------
	AABB MeshInstance::getLocalBounds() const
	{
		// v2 carries center/half-size Aabbs; the facade speaks min/max
		const Ogre::Aabb localAabb = this->mImpl->item->getLocalAabb();
		if(this->mImpl->animatedBounds)
		{
			if(Ogre::SkeletonInstance* skeleton =
				this->mImpl->item->getSkeletonInstance())
			{
				// the animated box: union of the LIVE bone pose positions,
				// each expanded by the import-time bone radius (max skinned-
				// vertex distance from its bone) - classic's bounds-from-
				// skeleton semantics; the bind-pose Aabb pads meshes that
				// arrived without a registered radius (.mesh serializer road)
				Real radius = RenderBackend::skinnedMeshBoneRadius(
					this->mImpl->meshName);
				if(radius <= 0)
				{
					radius = localAabb.getRadius();
				}
				const Ogre::Vector3 padding(radius, radius, radius);
				AABB bounds;
				bounds.setNull();
				for(size_t each = 0; each < skeleton->getNumBones(); ++each)
				{
					const Ogre::Vector3 position = boneLocalPosePosition(
						skeleton->getBone(each));
					bounds.merge(AABB(position - padding, position + padding));
				}
				if(!bounds.isNull())
				{
					return bounds;
				}
			}
		}
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
	bool MeshInstance::setMaterial(String const & materialName)
	{
		Ogre::Root* root = RenderBackend::ogreRoot();
		oAssert(root);
		Ogre::HlmsDatablock* datablock =
			root->getHlmsManager()->getDatablockNoDefault(materialName);
		if(!datablock)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: mesh '" + this->mImpl->meshName +
				"': no material '" + materialName +
				"' (create it via RenderSystem::createMaterial first)");
			return false;
		}
		// the fresh assignment below replaces any accent variants outright -
		// retire them first (their clones would leak; the remembered accent
		// resets too, the component layer re-applies it after the material,
		// @see MeshInstance::setTint)
		RenderBackend::resetMeshAccents(this->mImpl);
		this->mImpl->accentTint = Color(1.0f, 1.0f, 1.0f, 1.0f);
		this->mImpl->accentBoost = Color(0.0f, 0.0f, 0.0f, 1.0f);
		// assign atomically: Hlms shader generation REFUSES a datablock the
		// mesh cannot host (a normal map needs tangents, any texture needs
		// UVs - it throws from the sub-item's hash calculation). Remember the
		// previous datablocks and roll back on refusal, so a mesh that cannot
		// take the material keeps rendering with what it had.
		std::vector<Ogre::HlmsDatablock*> previous;
		const size_t subItemCount = this->mImpl->item->getNumSubItems();
		previous.reserve(subItemCount);
		for(size_t each = 0; each < subItemCount; ++each)
		{
			previous.push_back(this->mImpl->item->getSubItem(each)
				->getDatablock());
		}
		try
		{
			for(size_t each = 0; each < subItemCount; ++each)
			{
				this->mImpl->item->getSubItem(each)->setDatablock(datablock);
			}
		}
		catch(Ogre::Exception const & e)
		{
			for(size_t each = 0; each < subItemCount; ++each)
			{
				this->mImpl->item->getSubItem(each)
					->setDatablock(previous[each]);	// known-good, cannot throw
			}
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: mesh '" + this->mImpl->meshName +
				"' cannot render material '" + materialName + "': " +
				e.getDescription());
			return false;
		}
		// a fresh material assignment resets the instance to that material's
		// own (receiving) shadow state - callers re-apply setReceiveShadows
		// after it (@see MeshInstance::setReceiveShadows)
		this->mImpl->receiveRestore.clear();
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
		if(Ogre::SkeletonInstance* skeleton =
			this->mImpl->item->getSkeletonInstance())
		{
			for(Ogre::SkeletonAnimation const & each :
				skeleton->getAnimations())
			{
				names.push_back(animationDisplayName(each,
					this->mImpl->meshName));
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
					names.push_back(animationDisplayName(each,
						this->mImpl->meshName));
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
	void MeshInstance::setAnimationWeight(String const & name, float weight)
	{
		if(Ogre::SkeletonAnimation* animation =
			skeletonAnimation(this->mImpl->item, name))
		{
			animation->mWeight = weight;	// the SkeletonAnimation blend weight
		}
	}
	//---------------------------------------------------------
	void MeshInstance::setAnimatedBounds(bool enabled)
	{
		// a swinging limb must keep the reported bounds honest: Ogre-Next
		// keeps an item's local Aabb at the BIND pose, so an armed instance
		// derives getLocalBounds from the live bone poses instead
		// (@see getLocalBounds; meaningless without a skeleton)
		this->mImpl->animatedBounds = enabled;
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
