/**************************************************************
	created:	2010/08/30 at 20:36
	filename: 	CameraComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/CameraComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_gocomponent/CameraDefaultModes.h"

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
		this->cameraFunction(this,deltaTime,1.0f);
		// the historical Ogre auto-tracking, spelled out: the camera node
		// always looks at the target node (fixed yaw axis keeps it roll-free)
		this->attachNode->lookAt(this->targetNode->getWorldPosition(),
			RenderNode::TS_WORLD);
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

		// take over the window camera (the Engine default camera until
		// WP-A1.3 - getWindowCamera wraps it into a facade handle).
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
			// the ortho half-extent keeps the camera's aspect ratio - the
			// window width follows the viewport automatically
			this->camera->setOrthographic(this->orthoSize,
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
		// reflection-driven NAMED serialization (task #94 P2): projectionMode
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
		OENUM_START(ProjectionMode)
			OENUM_VALUE(PM_PERSPECTIVE)
			OENUM_VALUE(PM_ORTHOGRAPHIC)
		OENUM_END
		// neutral enum value<->label table (task #94, P1) so the reflected
		// projectionMode property can resolve labels in every config (the sol
		// OENUM_START block above stays - it feeds Lua; this feeds the registry)
		OENUM_REGISTER_START("ProjectionMode", CameraComponent::ProjectionMode)
			OENUM_REGISTER_VALUE(PM_PERSPECTIVE)
			OENUM_REGISTER_VALUE(PM_ORTHOGRAPHIC)
		OENUM_REGISTER_END
		// reflected schema (P1): projection mode (Enum) + orthoSize (Float with
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
	OOBJECT_END
}
