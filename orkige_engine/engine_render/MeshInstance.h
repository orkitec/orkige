/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	MeshInstance.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __MeshInstance_h__8_7_2026__12_00_00__
#define __MeshInstance_h__8_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include <core_util/String.h>

namespace Orkige
{
	//! @brief one placed instance of a mesh resource - what ModelComponent
	//! (and AnimationComponent) need from the renderer
	//! @remarks API sized by the audit (Docs/render-abstraction.md):
	//! ModelComponent needs load/attach/detach; the editor/player/samples
	//! need sub-entity material introspection only for the vertex-colour
	//! unlit fixup (folded into setVertexColourUnlit here) and a
	//! texture-unit probe for self-checks; AnimationComponent needs the
	//! animation-state surface below. The bone/keyframe root-motion
	//! extraction of AnimationComponent stays a classic-only backdoor for
	//! now (open question in the doc).
	//!
	//! Backend mapping (whole class): classic = Ogre::Entity;
	//! next = Ogre::Item (v2; assimp-imported meshes arrive as v1 and are
	//! converted via Mesh::importV1) ; filament = renderable entity built
	//! by gltfio's AssetLoader + RenderableManager.
	class ORKIGE_ENGINE_DLL MeshInstance
	{
		//--- Types -------------------------------------------------
	public:
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
		//! the selected backend's plumbing (@see RenderPrerequisites.h)
		friend struct RenderBackend;
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Impl*	mImpl;	//!< backend entity guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - detaches and destroys the backend entity
		//! map: classic=SceneManager::destroyEntity | next=destroyItem | filament=RenderableManager::destroy
		~MeshInstance();

		//--- placement ---
		//! @brief attach to a node (detaches from a previous one first)
		//! map: classic/next=SceneNode::attachObject | filament=re-parent renderable entity via TransformManager
		void attachTo(optr<RenderNode> const & node);
		//! map: classic/next=SceneNode::detachObject | filament=TransformManager::setParent(null)+Scene::remove
		void detach();

		//--- state ---
		//! the mesh resource name this instance was created from
		String const & getMeshName() const;
		//! map: classic/next=MovableObject::setVisible | filament=Scene::add/remove
		void setVisible(bool visible);
		//! map: classic/next=MovableObject::setCastShadows | filament=RenderableManager::setCastShadows
		void setCastShadows(bool cast);
		//! local-space bounds of the mesh
		//! map: classic=Entity::getBoundingBox | next=Item::getLocalAabb | filament=RenderableManager::getAxisAlignedBoundingBox
		AABB getLocalBounds() const;
		//! @see RenderWorld::queryRay - content only shows up in queries whose mask overlaps these flags
		//! map: classic/next=MovableObject::setQueryFlags | filament=facade-side filter in the impl AABB walk
		void setQueryFlags(unsigned int flags);

		//--- material services (the audited call sites, not a material system) ---
		//! @brief render all sub-entities unlit with vertex colours (the
		//! shared "VertexColour" editor/primitive look; idempotent)
		//! map: classic=Pass::setLightingEnabled(false)+setVertexColourTracking | next=HlmsUnlit datablock | filament=unlit filamat with vertex-color attribute
		void setVertexColourUnlit();
		//! number of sub-entities/sub-meshes (self-check introspection)
		//! map: classic=Entity::getNumSubEntities | next=Item::getNumSubItems | filament=RenderableManager primitive count
		size_t getNumSubMeshes() const;
		//! @brief does sub-mesh i's material sample at least one texture
		//! (self-check introspection used by editor/jumper today)
		//! map: classic=SubEntity material Pass::getNumTextureUnitStates | next=HlmsDatablock texture probe | filament=MaterialInstance parameter probe
		bool subMeshHasTexture(size_t index) const;

		//--- animation (what AnimationComponent uses) ---
		//! names of all animations the mesh's skeleton/resource carries
		//! map: classic=Entity::getAllAnimationStates | next=v2 SkeletonInstance/SkeletonAnimation list | filament=gltfio Animator::getAnimationName/count
		StringVector getAnimationNames() const;
		//! does the named animation exist
		bool hasAnimation(String const & name) const;
		//! names of the currently enabled (playing) animations
		//! map: classic=AnimationStateSet::getEnabledAnimationStates | next=facade bookkeeping over SkeletonAnimation | filament=facade bookkeeping
		StringVector getEnabledAnimations() const;
		//! @brief enable/disable playback of an animation
		//! map: classic=AnimationState::setEnabled | next=SkeletonAnimation::setEnabled | filament=facade flag driving Animator::applyAnimation
		void setAnimationEnabled(String const & name, bool enabled);
		//! map: classic=AnimationState::setLoop | next=SkeletonAnimation::setLoop | filament=facade time wrap
		void setAnimationLoop(String const & name, bool loop);
		//! map: classic=AnimationState::addTime | next=SkeletonAnimation::addTime | filament=advance facade clock + Animator::applyAnimation
		void addAnimationTime(String const & name, float deltaSeconds);
		//! map: classic=AnimationState::setTimePosition | next=SkeletonAnimation::setTime | filament=facade clock
		void setAnimationTime(String const & name, float seconds);
		//! map: classic=AnimationState::getTimePosition | next=SkeletonAnimation::getCurrentTime | filament=facade clock
		float getAnimationTime(String const & name) const;
		//! map: classic=AnimationState::getLength | next=SkeletonAnimation duration | filament=Animator::getAnimationDuration
		float getAnimationLength(String const & name) const;
		//! non-looping animation reached its end
		//! map: classic=AnimationState::hasEnded | next/filament=facade clock >= length
		bool hasAnimationEnded(String const & name) const;
	protected:
		//! instances are created by RenderWorld::createMeshInstance only
		MeshInstance();
	private:
		MeshInstance(MeshInstance const &);					// non-copyable
		MeshInstance & operator=(MeshInstance const &);		// non-copyable
	};
}

#endif //__MeshInstance_h__8_7_2026__12_00_00__
