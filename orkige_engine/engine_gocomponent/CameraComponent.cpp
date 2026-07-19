/**************************************************************
	created:	2010/08/30 at 20:36
	filename: 	CameraComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/CameraComponent.h"
#include <core_script/ScriptRuntime.h>	// OSCRIPT_HANDLE: ScriptComponentAccess registry
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_gocomponent/CameraDefaultModes.h"
#include <core_game/GameObjectManager.h>
#include <core_util/CameraFit.h>
#include <core_util/CameraFollow.h>

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	CameraComponent::CameraComponent()
	{
		this->projectionMode = CameraComponent::PM_PERSPECTIVE;
		this->orthoSize = 5.0f;
		this->fitMode = CameraComponent::FM_HEIGHT;
		this->designWidth = 10.0f;
		this->designHeight = 10.0f;
		this->lastAspect = 0.0f;
		this->followTarget = "";
		this->followDamping = 0.15f;
		this->followOffset = Vec3::ZERO;
		this->addDependency<TransformComponent>();
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	CameraComponent::~CameraComponent()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void CameraComponent::onUpdateComponent(float deltaTime)
	{
		// aspect-driven ortho fit: FM_WIDTH/FM_EXPAND derive the half-extent from
		// the live viewport aspect, so a window resize (or a device rotation)
		// must re-apply the projection. FM_HEIGHT needs no per-frame work - the
		// camera keeps a fixed half-height and lets the width follow the aspect.
		if(this->camera && this->projectionMode == CameraComponent::PM_ORTHOGRAPHIC
			&& this->fitMode != CameraComponent::FM_HEIGHT)
		{
			const float aspect = this->currentAspect();
			if(std::abs(aspect - this->lastAspect) > 1.0e-4f)
			{
				this->applyProjection();
			}
		}
		// SMOOTH FOLLOW (2D): when a target is set, the camera eases its XY
		// toward the target's XY (+ offset) and looks straight down -Z at it,
		// bypassing the 3D chase rig. This only moves the camera POSITION, so it
		// composes with the ortho fit above (which only sizes the projection).
		if(this->camera && !this->followTarget.empty() &&
			this->updateFollow(deltaTime))
		{
			this->attachNode->lookAt(this->targetNode->getWorldPosition(),
				RenderNode::TS_WORLD);
			return;
		}
		this->cameraFunction(this,deltaTime,1.0f);
		// the historical Ogre auto-tracking, spelled out: the camera node
		// always looks at the target node (fixed yaw axis keeps it roll-free)
		this->attachNode->lookAt(this->targetNode->getWorldPosition(),
			RenderNode::TS_WORLD);
	}
	//---------------------------------------------------------
	bool CameraComponent::updateFollow(float deltaTime)
	{
		// re-fetch the target by id every frame (never cache a component
		// pointer - it dangles when the object dies); a missing target just
		// holds the camera where it is (no snap, no error spam)
		if(GameObjectManager::getSingletonPtr() == 0)
		{
			return false;
		}
		optr<GameObject> target =
			GameObjectManager::getSingleton().getGameObject(
				this->followTarget).lock();
		if(!target || !target->hasComponent<TransformComponent>())
		{
			return false;
		}
		optr<TransformComponent> targetTransform =
			target->getComponent<TransformComponent>().lock();
		if(!targetTransform || !targetTransform->getNode())
		{
			return false;
		}
		const Vec3 targetPos = targetTransform->getNode()->getWorldPosition();
		const Vec3 current = this->attachNode->getPosition();
		// ease XY toward target + offset; the camera's Z (its ortho framing
		// distance) is preserved - 2D follow pans, it never dollies
		const float factor = CameraFollow::smoothFactor(this->followDamping,
			deltaTime);
		const float goalX = targetPos.x + this->followOffset.x;
		const float goalY = targetPos.y + this->followOffset.y;
		const Vec3 next(current.x + (goalX - current.x) * factor,
			current.y + (goalY - current.y) * factor, current.z);
		this->attachNode->setPosition(next);
		// look straight ahead along -Z at the followed plane (roll-free)
		this->targetNode->setPosition(Vec3(next.x, next.y, next.z - 1.0f));
		return true;
	}
	//---------------------------------------------------------
	void CameraComponent::onAdd()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);

		this->actorNode = transformComponent->getNode();
		oAssert(this->actorNode);

		String const & componentOwnerObjectId = componentOwner->getObjectID();
		oAssert(!componentOwnerObjectId.empty());

		RenderWorld* world = RenderSystem::get()->getWorld();

		this->controlNode = transformComponent->createChildNode(componentOwnerObjectId + "_control");
		this->controlNode->setPosition(Vec3(0, 1, 0)); //probably somewhere in the head
		this->sightNode = this->controlNode->createChild(componentOwnerObjectId + "_sight");
		this->sightNode->setPosition(Vec3(0, 0, 1));
		this->cameraNode = this->controlNode->createChild(componentOwnerObjectId + "_camera");
		this->cameraNode->setPosition(Vec3(0, 0, -85));
		this->attachNode = world->createNode(componentOwnerObjectId + "_attach");
		this->targetNode = world->createNode(componentOwnerObjectId + "_target");

		this->attachNode->setFixedYawAxis(true); // keeps the per-update lookAt roll-free

		// take over the window camera (the Engine default camera -
		// getWindowCamera wraps it into a facade handle).
		// A UI-only window (showUIOnlyWindow - the editor shell) has NO
		// window camera by contract: the component then stays inactive
		// (rig nodes exist, nothing drives the view) instead of asserting -
		// the editor legitimately loads scenes that carry a CameraComponent.
		this->camera = RenderSystem::get()->getWindowCamera();
		if(!this->camera)
		{
			return;
		}

		this->camera->attachTo(this->attachNode);
		this->setMode(CameraDefaultModes::ThirdPersonChaseCamera);
		this->applyProjection();
	}
	//---------------------------------------------------------
	void CameraComponent::onRemove()
	{
		// hand the window camera back the way we found it
		if(this->camera)
		{
			if(this->projectionMode != CameraComponent::PM_PERSPECTIVE)
			{
				this->camera->setPerspective(this->camera->getFOVy(),
					this->camera->getNearClip(), this->camera->getFarClip());
			}
			this->camera->detach();
		}
		// RAII: dropping the handles detaches and destroys the rig nodes -
		// children before their parent (sight/camera hang off control)
		this->camera.reset();
		this->sightNode.reset();
		this->cameraNode.reset();
		this->controlNode.reset();
		this->attachNode.reset();
		this->targetNode.reset();
		this->actorNode.reset();
	}
	//---------------------------------------------------------
	void CameraComponent::setProjectionMode(ProjectionMode mode)
	{
		this->projectionMode = mode;
		this->applyProjection();
	}
	//---------------------------------------------------------
	void CameraComponent::setOrthoSize(float verticalHalfExtent)
	{
		oDebugWarning(verticalHalfExtent > 0.0f,
			"CameraComponent::setOrthoSize wants a positive half-extent");
		this->orthoSize = std::max(verticalHalfExtent, 0.001f);
		this->applyProjection();
	}
	//---------------------------------------------------------
	void CameraComponent::setFitMode(FitMode mode)
	{
		this->fitMode = mode;
		this->applyProjection();
	}
	//---------------------------------------------------------
	void CameraComponent::setDesignWidth(float worldWidth)
	{
		this->designWidth = std::max(worldWidth, 0.0f);
		this->applyProjection();
	}
	//---------------------------------------------------------
	void CameraComponent::setDesignHeight(float worldHeight)
	{
		this->designHeight = std::max(worldHeight, 0.0f);
		this->applyProjection();
	}
	//---------------------------------------------------------
	void CameraComponent::follow(String const & targetId, float damping)
	{
		this->followTarget = targetId;
		this->followDamping = std::max(damping, 0.0f);
	}
	//---------------------------------------------------------
	void CameraComponent::stopFollow()
	{
		this->followTarget = "";
	}
	//---------------------------------------------------------
	void CameraComponent::setFollowTarget(String const & targetId)
	{
		this->followTarget = targetId;
	}
	//---------------------------------------------------------
	void CameraComponent::setFollowDamping(float damping)
	{
		this->followDamping = std::max(damping, 0.0f);
	}
	//---------------------------------------------------------
	void CameraComponent::setFollowOffset(Vec3 const & offset)
	{
		this->followOffset = offset;
	}
	//---------------------------------------------------------
	float CameraComponent::currentAspect() const
	{
		unsigned int width = 0;
		unsigned int height = 0;
		if(RenderSystem::get())
		{
			RenderSystem::get()->getWindowSize(width, height);
		}
		return (width > 0 && height > 0)
			? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
	}
	//---------------------------------------------------------
	void CameraComponent::applyProjection()
	{
		if(!this->camera)
		{
			return;	// detached: the state applies on onAdd/load
		}
		// the clip planes are read back so switching the projection type
		// never alters them (the historical behavior)
		if(this->projectionMode == CameraComponent::PM_ORTHOGRAPHIC)
		{
			// the effective half-extent depends on the 2D fit policy: FM_HEIGHT
			// uses the authored orthoSize directly (width follows the aspect);
			// FM_WIDTH/FM_EXPAND derive it from the design rect + live aspect
			// through the pure CameraFit math.
			float halfExtent = this->orthoSize;
			if(this->fitMode != CameraComponent::FM_HEIGHT)
			{
				const float aspect = this->currentAspect();
				this->lastAspect = aspect;
				halfExtent = CameraFit::orthoHalfHeight(
					static_cast<CameraFit::FitMode>(this->fitMode),
					this->designWidth, this->designHeight, aspect);
			}
			this->camera->setOrthographic(halfExtent,
				this->camera->getNearClip(), this->camera->getFarClip());
		}
		else
		{
			this->camera->setPerspective(this->camera->getFOVy(),
				this->camera->getNearClip(), this->camera->getFarClip());
		}
	}
	//---------------------------------------------------------
	// @TODO: serialize the camera mode FUNCTION and the camera/sight node offsets
	// - projectionMode + orthoSize round-trip through the reflected schema
	void CameraComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: projectionMode
		// (enum) + orthoSize (float) are written by name off the declared schema
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void CameraComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		// the setProjectionMode / setOrthoSize reflected setters re-applyProjection
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(CameraComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(setProjectionMode)
		OFUNC(getProjectionMode)
		OFUNC(setOrthoSize)
		OFUNC(getOrthoSize)
		OFUNC(setFitMode)
		OFUNC(getFitMode)
		OFUNC(setDesignWidth)
		OFUNC(getDesignWidth)
		OFUNC(setDesignHeight)
		OFUNC(getDesignHeight)
		// smooth-follow convenience + reflected setters (Lua: a script reaches
		// the active camera via world.getCamera(id) and drives these)
		OFUNC(follow)
		OFUNC(stopFollow)
		OFUNC(setFollowTarget)
		OFUNC(getFollowTarget)
		OFUNC(setFollowDamping)
		OFUNC(getFollowDamping)
		OFUNC(setFollowOffset)
		OFUNC(getFollowOffset)
		OENUM_START(ProjectionMode)
			OENUM_VALUE(PM_PERSPECTIVE)
			OENUM_VALUE(PM_ORTHOGRAPHIC)
		OENUM_END
		OENUM_START(FitMode)
			OENUM_VALUE(FM_HEIGHT)
			OENUM_VALUE(FM_WIDTH)
			OENUM_VALUE(FM_EXPAND)
		OENUM_END
		// neutral enum value<->label table so the reflected
		// projectionMode property can resolve labels in every config (the sol
		// OENUM_START block above stays - it feeds Lua; this feeds the registry)
		OENUM_REGISTER_START("ProjectionMode", CameraComponent::ProjectionMode)
			OENUM_REGISTER_VALUE(PM_PERSPECTIVE)
			OENUM_REGISTER_VALUE(PM_ORTHOGRAPHIC)
		OENUM_REGISTER_END
		OENUM_REGISTER_START("FitMode", CameraComponent::FitMode)
			OENUM_REGISTER_VALUE(FM_HEIGHT)
			OENUM_REGISTER_VALUE(FM_WIDTH)
			OENUM_REGISTER_VALUE(FM_EXPAND)
		OENUM_REGISTER_END
		// reflected schema: projection mode (Enum) + orthoSize (Float with
		// reserved inspector range metadata). The component's hand-written
		// save/load is untouched - this only ADDS the queryable schema.
		OPROPERTY_ENUM("projectionMode", "ProjectionMode", getProjectionMode, setProjectionMode, Orkige::PROP_NONE)
		Orkige::PropertyMeta orkigeOrthoMeta;
		orkigeOrthoMeta.hasRange = true;
		orkigeOrthoMeta.minValue = 0.1f;
		orkigeOrthoMeta.maxValue = 100.0f;
		orkigeOrthoMeta.step = 0.1f;
		orkigeOrthoMeta.tooltip = "orthographic vertical half-extent in world units";
		OPROPERTY_META("orthoSize", Orkige::PropertyKind::Float, getOrthoSize, setOrthoSize, Orkige::PROP_NONE, orkigeOrthoMeta)
		// 2D aspect fit policy: the mode enum + the design rectangle it fits to.
		// FM_HEIGHT (default) keeps orthoSize; FM_WIDTH/FM_EXPAND derive it.
		OPROPERTY_ENUM("fitMode", "FitMode", getFitMode, setFitMode, Orkige::PROP_NONE)
		OPROPERTY("designWidth", Orkige::PropertyKind::Float, getDesignWidth, setDesignWidth, Orkige::PROP_NONE)
		OPROPERTY("designHeight", Orkige::PropertyKind::Float, getDesignHeight, setDesignHeight, Orkige::PROP_NONE)
		// 2D smooth follow: the tracked object id (ObjectRef), the smoothing
		// time constant and the XY offset - reflected so they serialize, show in
		// the Inspector and reach get/set_component over MCP like any property.
		OPROPERTY_REF("followTarget", Orkige::PropertyKind::ObjectRef, "", getFollowTarget, setFollowTarget, Orkige::PROP_NONE)
		OPROPERTY("followDamping", Orkige::PropertyKind::Float, getFollowDamping, setFollowDamping, Orkige::PROP_NONE)
		OPROPERTY("followOffset", Orkige::PropertyKind::Vec3, getFollowOffset, setFollowOffset, Orkige::PROP_NONE)

		// world.getCamera(id) hands Lua a WEAK handle: locks per call, raises an
		// honest error naming the owner once gone. @see TransformComponent.
		OWEAKHANDLE_BEGIN(Orkige::CameraComponent, "CameraComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(setProjectionMode)
			OWEAKHANDLE_BASEMETHOD(getProjectionMode)
			OWEAKHANDLE_BASEMETHOD(setOrthoSize)
			OWEAKHANDLE_BASEMETHOD(getOrthoSize)
			OWEAKHANDLE_BASEMETHOD(setFitMode)
			OWEAKHANDLE_BASEMETHOD(getFitMode)
			OWEAKHANDLE_BASEMETHOD(setDesignWidth)
			OWEAKHANDLE_BASEMETHOD(getDesignWidth)
			OWEAKHANDLE_BASEMETHOD(setDesignHeight)
			OWEAKHANDLE_BASEMETHOD(getDesignHeight)
			OWEAKHANDLE_BASEMETHOD(follow)
			OWEAKHANDLE_BASEMETHOD(stopFollow)
			OWEAKHANDLE_BASEMETHOD(setFollowTarget)
			OWEAKHANDLE_BASEMETHOD(getFollowTarget)
			OWEAKHANDLE_BASEMETHOD(setFollowDamping)
			OWEAKHANDLE_BASEMETHOD(getFollowDamping)
			OWEAKHANDLE_BASEMETHOD(setFollowOffset)
			OWEAKHANDLE_BASEMETHOD(getFollowOffset)
		OWEAKHANDLE_END
		// world.getCamera(id) + getComponent("camera") (no self.<name> - a
		// camera is reached by id, not as a behavior sibling)
		OSCRIPT_HANDLE("camera", false, "getCamera")
	OOBJECT_END
}
