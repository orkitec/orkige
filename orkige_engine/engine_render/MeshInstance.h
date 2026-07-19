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
	//! Backend mapping (whole class): classic = Ogre::Entity (meshes via
	//! OGRE's Codec_Assimp); next = Ogre::Item over a v2 mesh - Next has
	//! no assimp codec, the backend drives assimp itself and imports via
	//! v1::ManualObject -> Mesh::importV1, incl. skinned sources whole
	//! (skeleton + clips + weights over the neutral SkinnedRig extraction,
	//! see engine_render_next/MeshLoaderNext.cpp); filament = renderable
	//! entity built by gltfio's AssetLoader + RenderableManager.
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
		//! @brief whether cast shadows darken THIS instance (on by default).
		//! Shadow receiving lives on the material/datablock in both backends, so
		//! turning it OFF swaps every sub-mesh to a per-instance no-receive
		//! VARIANT of its current material ("<name>/NoRecv", created on demand);
		//! turning it back ON restores the original assignment exactly. Assign
		//! materials first (setMaterial), then the receive flag - a later
		//! setMaterial resets the instance to that material's own (receiving)
		//! state, and the component layer re-applies the flag after it.
		//! map: classic=SubEntity::setMaterial(clone with Material::setReceiveShadows(false)) | next=SubItem::setDatablock(clone with HlmsDatablock::setReceiveShadows(false)) | filament=RenderableManager::setReceiveShadows (native per-renderable)
		void setReceiveShadows(bool receive);
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
		//! @brief render ALL sub-meshes with a RenderSystem::createMaterial
		//! material (whole-instance assignment - per-sub-mesh granularity
		//! waits for a real need). The assignment is per INSTANCE: other
		//! instances of the same mesh resource keep their materials.
		//! @return false + a log line when no such material exists, or (next)
		//! when the mesh cannot host the material's maps - a normal map needs
		//! tangents, any texture needs UVs; the importer provides both for
		//! UV-mapped imports, but e.g. the procedural cube has neither. On
		//! refusal the previous materials stay.
		//! map: classic=SubEntity::setMaterial | next=SubItem::setDatablock (Hlms shader-gen guarded) | filament=RenderableManager::setMaterialInstanceAt
		bool setMaterial(String const & materialName);

		//--- runtime accents (per-instance, never serialized) ---
		//! @brief multiply THIS instance's albedo/diffuse by @p tint (RGB;
		//! white = no tint). A runtime-only accent - the assigned material
		//! (the authored `.omat`) stays untouched and other instances keep
		//! their look: the instance swaps onto a per-instance VARIANT of its
		//! current material/datablock ("<name>/Accent.<n>", cloned once, then
		//! parameter-driven in place - cheap per call). Tint back to white
		//! (with a black boost) restores the original assignment EXACTLY and
		//! retires the variants. Assign materials and the receive flag first:
		//! a later setMaterial or setReceiveShadows resets the accents, and
		//! the component layer re-applies them after (the setReceiveShadows
		//! variant discipline).
		//! map: classic=pass diffuse/ambient scaled on the accent clone | next=HlmsPbsDatablock::setDiffuse on the accent clone | filament=MaterialInstance parameter
		void setTint(Color const & tint);
		//! @brief ADD @p boost (RGB; black = none) to this instance's emitted
		//! colour - the hit-flash/highlight accent (a tint can only darken,
		//! the boost brightens lighting-independently). Same per-instance
		//! variant machinery, same reset discipline as setTint.
		//! map: classic=pass self-illumination raised on the accent clone | next=HlmsPbsDatablock::setEmissive on the accent clone | filament=emissive parameter
		void setEmissiveBoost(Color const & boost);

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
		//! @brief blend weight of an animation (1 = full). Several animations
		//! enabled at once blend by their weights - the crossfade primitive.
		//! map: classic=AnimationState::setWeight | next=SkeletonAnimation::mWeight | filament=Animator applyAnimation weight
		void setAnimationWeight(String const & name, float weight);
		//! @brief make the instance's bounds follow the animated skeleton (so a
		//! swinging limb keeps the culling AABB correct); a no-op on an
		//! unskinned instance / a flavor whose bounds already track animation.
		//! map: classic=Entity::setUpdateBoundingBoxFromSkeleton | next=native (v2 bounds already animate)
		void setAnimatedBounds(bool enabled);
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

		//--- bone attachment (weapon-in-hand / prop-on-bone) ---
		//! @brief the WORLD-space pose of a skeleton bone by name - the seam a
		//! bone-follower copies onto its render node so a prop tracks a
		//! character's hand/head as the skeleton animates. The bone's live pose
		//! is composed up the joint chain and carried through the instance's own
		//! world transform. Classic forces the skeleton to its current animation
		//! pose before reading (no lag); the derived-cache flavors read the last
		//! applied pose (at most one frame behind the clip advance).
		//! @return false when the instance carries no skeleton or the bone name
		//! is unknown - the outputs are then left untouched
		//! map: classic=Entity::_updateAnimation + parent-node full transform * Bone::_getFullTransform (decomposed) | next=parent SceneNode derived transform * the bone pose TRS composed up the parent chain
		bool getBoneWorldTransform(String const & boneName, Vec3 & outPosition,
			Quat & outOrientation, Vec3 & outScale) const;
	protected:
		//! instances are created by RenderWorld::createMeshInstance only
		MeshInstance();
	private:
		MeshInstance(MeshInstance const &);					// non-copyable
		MeshInstance & operator=(MeshInstance const &);		// non-copyable
	};
}

#endif //__MeshInstance_h__8_7_2026__12_00_00__
