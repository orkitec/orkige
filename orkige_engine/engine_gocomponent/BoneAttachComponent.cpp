/**************************************************************
	created:	2026/07/19 at 12:00
	filename: 	BoneAttachComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/BoneAttachComponent.h"
#include <core_script/ScriptRuntime.h>	// OSCRIPT_HANDLE: ScriptComponentAccess registry
#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/MeshInstance.h"
#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	BoneAttachComponent::BoneAttachComponent()
	{
		this->mOffset = Vec3::ZERO;
		this->mFollowRotation = true;
		this->mFollowScale = false;
		this->addDependency<TransformComponent>();
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	BoneAttachComponent::~BoneAttachComponent()
	{
	}
	//---------------------------------------------------------
	void BoneAttachComponent::setTarget(String const & targetId)
	{
		this->mTargetId = targetId;
	}
	//---------------------------------------------------------
	void BoneAttachComponent::setBone(String const & boneName)
	{
		this->mBoneName = boneName;
	}
	//---------------------------------------------------------
	void BoneAttachComponent::setOffset(Vec3 const & offset)
	{
		this->mOffset = offset;
	}
	//---------------------------------------------------------
	void BoneAttachComponent::setOffsetXYZ(float x, float y, float z)
	{
		this->mOffset = Vec3(x, y, z);
	}
	//---------------------------------------------------------
	void BoneAttachComponent::setFollowRotation(bool follow)
	{
		this->mFollowRotation = follow;
	}
	//---------------------------------------------------------
	void BoneAttachComponent::setFollowScale(bool follow)
	{
		this->mFollowScale = follow;
	}
	//---------------------------------------------------------
	bool BoneAttachComponent::applyAttachment()
	{
		if(this->mBoneName.empty())
		{
			return false;
		}
		GameObject* owner = this->getComponentOwner();
		if(!owner)
		{
			return false;
		}
		// resolve the character: an explicit target id, else the owner's OWN
		// mesh (a bone of the object's own skeleton). Hold the lock across the
		// read so the target cannot expire mid-call.
		optr<GameObject> targetLock;
		GameObject* target = owner;
		if(!this->mTargetId.empty())
		{
			if(GameObjectManager::getSingletonPtr() == NULL)
			{
				return false;
			}
			targetLock = GameObjectManager::getSingleton()
				.getGameObject(this->mTargetId).lock();
			if(!targetLock)
			{
				return false;
			}
			target = targetLock.get();
		}
		if(!target->hasComponent<ModelComponent>())
		{
			return false;
		}
		ModelComponent* model = target->getComponentPtr<ModelComponent>();
		optr<MeshInstance> mesh =
			model ? model->getMeshInstance() : optr<MeshInstance>();
		if(!mesh)
		{
			return false;
		}
		Vec3 bonePosition;
		Quat boneOrientation;
		Vec3 boneScale;
		if(!mesh->getBoneWorldTransform(this->mBoneName, bonePosition,
			boneOrientation, boneScale))
		{
			return false;
		}
		optr<TransformComponent> transform =
			owner->getComponent<TransformComponent>().lock();
		if(!transform)
		{
			return false;
		}
		// the offset rides in the bone's own frame so a "0.1 forward of the
		// hand" reads the same as the hand turns
		const Vec3 attachPosition = bonePosition + boneOrientation * this->mOffset;
		transform->setWorldPosition(attachPosition);
		if(this->mFollowRotation)
		{
			transform->setWorldOrientation(boneOrientation);
		}
		if(this->mFollowScale)
		{
			transform->setScale(boneScale);
		}
		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void BoneAttachComponent::onAdd()
	{
	}
	//---------------------------------------------------------
	void BoneAttachComponent::onRemove()
	{
	}
	//---------------------------------------------------------
	void BoneAttachComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: the target id, bone name,
		// bone-local offset and the follow flags (@see loadComponentProperties)
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void BoneAttachComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void BoneAttachComponent::onUpdateComponent(float deltaTime)
	{
		// follow the current animated bone pose (the facade poses the skeleton
		// to this frame's clip time on read); a no-op until the target resolves
		this->applyAttachment();
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(BoneAttachComponent)
		GAMEOBJECTCOMPONENT()
		OFUNCCR(getTarget)
		OFUNC(setTarget)
		OFUNCCR(getBone)
		OFUNC(setBone)
		OFUNC(setOffsetXYZ)
		OFUNC(getFollowRotation)
		OFUNC(setFollowRotation)
		OFUNC(getFollowScale)
		OFUNC(setFollowScale)
		OFUNC(applyAttachment)
		// reflected schema: the target character, the bone name, the bone-local
		// offset and the follow flags - the ONE schema the Inspector, scene
		// serialization, the debug protocol and MCP all consume
		OPROPERTY("target", Orkige::PropertyKind::String, getTarget, setTarget, Orkige::PROP_NONE)
		OPROPERTY("bone", Orkige::PropertyKind::String, getBone, setBone, Orkige::PROP_NONE)
		OPROPERTY("offset", Orkige::PropertyKind::Vec3, getOffset, setOffset, Orkige::PROP_NONE)
		OPROPERTY("followRotation", Orkige::PropertyKind::Bool, getFollowRotation, setFollowRotation, Orkige::PROP_NONE)
		OPROPERTY("followScale", Orkige::PropertyKind::Bool, getFollowScale, setFollowScale, Orkige::PROP_NONE)
		// self.boneAttach / world.getBoneAttach(id) hand Lua a WEAK handle: locks
		// per call, raises an honest error naming the owner once gone. @see
		// TransformComponent / AnimationComponent.
		OWEAKHANDLE_BEGIN(Orkige::BoneAttachComponent, "BoneAttachComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(getTarget)
			OWEAKHANDLE_BASEMETHOD(setTarget)
			OWEAKHANDLE_BASEMETHOD(getBone)
			OWEAKHANDLE_BASEMETHOD(setBone)
			OWEAKHANDLE_BASEMETHOD(setOffsetXYZ)
			OWEAKHANDLE_BASEMETHOD(setFollowRotation)
			OWEAKHANDLE_BASEMETHOD(setFollowScale)
			OWEAKHANDLE_BASEMETHOD(applyAttachment)
		OWEAKHANDLE_END
		// ONE declaration wires self.boneAttach + world.getBoneAttach(id) +
		// getComponent("boneAttach") off the ScriptComponentAccess registry
		OSCRIPT_HANDLE("boneAttach", true, "getBoneAttach")
	OOBJECT_END
}
