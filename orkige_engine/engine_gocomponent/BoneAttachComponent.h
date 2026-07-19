/**************************************************************
	created:	2026/07/19 at 12:00
	filename: 	BoneAttachComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __BoneAttachComponent_h__19_7_2026__12_00_00__
#define __BoneAttachComponent_h__19_7_2026__12_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/RenderMath.h"

namespace Orkige
{
	//! @brief make this GameObject follow a named BONE of a skinned character -
	//! a prop-in-hand / marker-on-head attachment, both render flavors.
	//! @remarks each tick the component reads the target's animated bone pose
	//! off the facade (`MeshInstance::getBoneWorldTransform`) and copies it onto
	//! its OWN TransformComponent's WORLD pose (so it works whether the follower
	//! is a root object or parented). The target is another GameObject carrying a
	//! ModelComponent (an empty target follows the owner's OWN mesh - a bone of
	//! the object's own skeleton). TICK ORDER: the copy runs in the component
	//! update phase and the facade poses the skeleton to the current clip time on
	//! read, so a prop tracks the live animation (at most one clip-advance behind
	//! on the derived-cache flavor). Dormant in the editor (no tick, so no
	//! follow) - the reflected target/bone/offset drive it off the ONE registry.
	class ORKIGE_ENGINE_DLL BoneAttachComponent : public GameObjectComponent
	{
		OOBJECT(BoneAttachComponent, GameObjectComponent)
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		String	mTargetId;			//!< the character GameObject id ("" = the owner's own mesh)
		String	mBoneName;			//!< the bone to follow
		Vec3	mOffset;			//!< a positional offset in BONE-local space
		bool	mFollowRotation;	//!< copy the bone orientation (default true)
		bool	mFollowScale;		//!< copy the bone scale (default false)
		//--- Methods -----------------------------------------
	public:
		//! constructor
		BoneAttachComponent();
		//! destructor
		virtual ~BoneAttachComponent();

		//! the target character GameObject id ("" = the owner's own mesh)
		inline String const & getTarget() const;
		//! set the target character GameObject id
		void setTarget(String const & targetId);
		//! the bone name this object follows
		inline String const & getBone() const;
		//! set the bone name to follow
		void setBone(String const & boneName);
		//! the positional offset applied in the bone's local frame
		inline Vec3 const & getOffset() const;
		//! set the positional offset (bone-local)
		void setOffset(Vec3 const & offset);
		//! set the offset from x/y/z (the Lua-friendly setter)
		void setOffsetXYZ(float x, float y, float z);
		//! whether the bone orientation is copied
		inline bool getFollowRotation() const;
		//! set whether the bone orientation is copied
		void setFollowRotation(bool follow);
		//! whether the bone scale is copied
		inline bool getFollowScale() const;
		//! set whether the bone scale is copied
		void setFollowScale(bool follow);

		//! @brief pull the current bone pose and copy it onto the owner's
		//! transform NOW (the per-tick body, also callable on demand); a no-op
		//! when the target/bone cannot be resolved
		bool applyAttachment();
	protected:
		//! component override gets called after the component is attached
		virtual void onAdd();
		//! component override gets called before the component is removed
		virtual void onRemove();
		//--- SERIALIZATION ---
		//! save the target/bone/offset schema
		virtual void save(optr<IArchive> const & ar);
		//! load the target/bone/offset schema
		virtual void load(optr<IArchive> const & ar);
	private:
		//! overridable per-frame update: follow the bone
		virtual void onUpdateComponent(float deltaTime);
	};
	//---------------------------------------------------------
	inline String const & BoneAttachComponent::getTarget() const
	{
		return this->mTargetId;
	}
	//---------------------------------------------------------
	inline String const & BoneAttachComponent::getBone() const
	{
		return this->mBoneName;
	}
	//---------------------------------------------------------
	inline Vec3 const & BoneAttachComponent::getOffset() const
	{
		return this->mOffset;
	}
	//---------------------------------------------------------
	inline bool BoneAttachComponent::getFollowRotation() const
	{
		return this->mFollowRotation;
	}
	//---------------------------------------------------------
	inline bool BoneAttachComponent::getFollowScale() const
	{
		return this->mFollowScale;
	}
}

#endif //__BoneAttachComponent_h__19_7_2026__12_00_00__
