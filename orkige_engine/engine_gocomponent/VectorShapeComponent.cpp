/**************************************************************
	created:	2026/07/10 at 12:00
	filename: 	VectorShapeComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/VectorShapeComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"
#include "core_util/VectorShapeAsset.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include <core_debug/DebugMacros.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Orkige
{
	// the clamp range mirrors the facade quad's window (shared 2D painter's
	// window: sprites and shapes sort against each other by the same rule)
	const int VectorShapeComponent::ZORDER_MIN = -40;
	const int VectorShapeComponent::ZORDER_MAX = 40;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	VectorShapeComponent::VectorShapeComponent()
	{
		this->mShapeName = "";
		this->mTint = Color::White;
		this->mScale = 1.0f;
		this->mEdgeSoftness = 0.0f;	// auto: derived from the shape bounds
		this->mZOrder = 0;
		this->mVisible = true;
		this->mSoftBody = false;
		this->mMorphClip = -1;
		this->mMorphSpeed = 1.0f;
		this->mMorphLoop = false;
		this->mBodyVelX = 0.0f;
		this->mBodyVelY = 0.0f;
		this->mDeformDirty = false;
		this->mDeformFreshBuild = false;
		this->addDependency<TransformComponent>();
	}
	//---------------------------------------------------------
	VectorShapeComponent::~VectorShapeComponent()
	{
	}
	//---------------------------------------------------------
	void VectorShapeComponent::loadShape(String const & shapeName)
	{
		oAssert(!shapeName.empty());
		oAssert(this->getComponentOwner());
		oAssert(this->mNode);

		if(this->mMesh)
		{
			this->removeShape();
		}
		// the `.oshape` is a small text resource read through the facade so this
		// stays backend-neutral (no renderer/Ogre here); AUTODETECT resolves
		// engine media and project assets alike
		String text;
		if(!RenderSystem::get()->readResourceText(shapeName, text))
		{
			oDebugError("engine", 0, "VectorShapeComponent: shape '" << shapeName
				<< "' not found");
			return;
		}
		VectorShapeAsset::ParsedShape parsed;
		if(!VectorShapeAsset::parse(text, parsed))
		{
			oDebugError("engine", 0, "VectorShapeComponent: shape '" << shapeName
				<< "' is malformed");
			return;
		}

		this->mMesh = RenderSystem::get()->getWorld()->createVectorMesh();
		if(!this->mMesh)
		{
			return;
		}
		this->mShapeName = shapeName;
		this->mRegions.swap(parsed.base);
		this->mMorphTargets.swap(parsed.morphs);
		this->rebuildMesh();		// tessellate + push the static mesh
		this->rebuildDeformer();	// build the soft-body deformer if enabled
		this->mMesh->attachTo(this->getNode());
		this->applyVisibility();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::removeShape()
	{
		// RAII: dropping the handle detaches and destroys the mesh geometry
		this->mMesh.reset();
		this->mShapeName = "";
		this->mRegions.clear();
		this->mMorphTargets.clear();
		this->mBuilt.clear();
		this->mRuns.clear();
		this->mDeform = SoftBodyDeform();	// drop the soft-body state
		this->mDeformDirty = false;
		this->setWantsUpdates(false);
	}
	//---------------------------------------------------------
	std::size_t VectorShapeComponent::getTriangleCount() const
	{
		return this->mBuilt.triangleCount();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setTint(float red, float green, float blue,
		float alpha)
	{
		this->mTint = Color(red, green, blue, alpha);
		// the tint lives in the vertex colours - refill to apply it
		if(this->mMesh)
		{
			this->rebuildMesh();
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setScale(float scale)
	{
		this->mScale = scale;
		if(this->mNode)
		{
			// uniform scale on the shape's own node (z is flat, so z-scale is
			// harmless); the feather width stays shape-local and scales with it
			this->getNode()->setScale(Vec3(scale, scale, scale));
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setEdgeSoftness(float width)
	{
		this->mEdgeSoftness = width;
		if(this->mMesh)
		{
			this->rebuildMesh();	// feather is baked geometry
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setZOrder(int zOrder)
	{
		this->mZOrder = std::clamp(zOrder, ZORDER_MIN, ZORDER_MAX);
		if(this->mMesh)
		{
			this->mMesh->setZOrder(this->mZOrder);
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setShapeVisible(bool visible)
	{
		this->mVisible = visible;
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	bool VectorShapeComponent::isShapeVisible() const
	{
		return this->mVisible;
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setShapeReference(String const & shapeName)
	{
		if(shapeName.empty())
		{
			// a live mesh tears down; a detached component just clears the name
			if(this->mMesh)
			{
				this->removeShape();
			}
			else
			{
				this->mShapeName = "";
			}
			return;
		}
		// a detached load (unit tests, tooling) only records the state; the mesh
		// needs the scene node the component gets on attachment
		if(this->mNode)
		{
			this->loadShape(shapeName);
			return;
		}
		this->mShapeName = shapeName;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void VectorShapeComponent::onAdd()
	{
		oAssert(!this->mMesh);
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		optr<RenderNode> node = transformComponent->createChildNode(
			componentOwner->getObjectID() + ".VectorShapeComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
		// a sibling RigidBodyComponent's contact drives the soft-body squash;
		// the handler no-ops unless soft-body is on, so registering always is safe
		this->registerEvent(RigidBodyComponent::ContactBeganEvent,
			&VectorShapeComponent::onContactBegan, this);
		// a shape reference recorded while detached loads now the node exists
		if(!this->mShapeName.empty() && !this->mMesh)
		{
			this->loadShape(this->mShapeName);
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::onRemove()
	{
		this->unregisterEvent(RigidBodyComponent::ContactBeganEvent);
		// content first, then the node (a node must outlive its content)
		this->mMesh.reset();
		this->mShapeName = "";
		this->mRegions.clear();
		this->mMorphTargets.clear();
		this->mBuilt.clear();
		this->mRuns.clear();
		this->mDeform = SoftBodyDeform();
		this->mDeformDirty = false;
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::onSetActive(bool activeInHierarchy)
	{
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::rebuildMesh()
	{
		if(!this->mMesh)
		{
			return;
		}
		// feather width: an explicit edgeSoftness, else a default proportional
		// to the shape bounds (constant visual weight at any authored scale)
		const VectorTessellator::Bounds bounds =
			VectorTessellator::computeBounds(this->mRegions);
		const float feather = (this->mEdgeSoftness > 0.0f)
			? this->mEdgeSoftness
			: VectorTessellator::defaultFeatherWidth(bounds);
		VectorTessellator::build(this->mRegions, feather, this->mBuilt);

		// convert the POD mesh to facade vertices, multiplying the instance
		// tint into every fill/feather colour (textured regions' UVs ride
		// along). Kept as a member so the per-frame deform reuses it: the
		// tinted COLOURS and UVs are fixed, only positions move.
		VectorMeshRuns::buildVertices(this->mBuilt, this->mTint,
			this->mDeformVertices);
		// an all-flat shape takes the plain setMesh identity path; textured
		// runs split into one facade section (= one draw) per texture
		this->mRuns.push(*this->mMesh, this->mBuilt, this->mDeformVertices);
		this->applyStateToMesh();
		// setMesh mapped the (dynamic) buffer this frame: the next backend forbids
		// a second map before the frame renders, so defer any deform upload a tick
		this->mDeformFreshBuild = true;
	}
	//---------------------------------------------------------
	void VectorShapeComponent::applyStateToMesh()
	{
		if(this->mMesh)
		{
			this->mMesh->setZOrder(this->mZOrder);
		}
		if(this->mNode)
		{
			this->getNode()->setScale(
				Vec3(this->mScale, this->mScale, this->mScale));
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::applyVisibility()
	{
		oAssert(this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		const bool ownerActive =
			!componentOwner || componentOwner->isActiveInHierarchy();
		// only over the shape's OWN node (child GameObjects gate themselves)
		this->setVisible(this->mVisible && ownerActive);
	}
	//---------------------------------------------------------
	//--- soft body -------------------------------------------
	//---------------------------------------------------------
	void VectorShapeComponent::setSoftBodyEnabled(bool enabled)
	{
		if(this->mSoftBody == enabled)
		{
			return;
		}
		this->mSoftBody = enabled;
		if(enabled)
		{
			// build the deformer over the current shape and start ticking
			this->rebuildDeformer();
		}
		else
		{
			// restore the exact rest geometry and stop ticking
			this->mDeform = SoftBodyDeform();
			this->mDeformDirty = false;
			this->setWantsUpdates(false);
			if(this->mMesh && !this->mDeformVertices.empty())
			{
				this->mRuns.update(*this->mMesh, this->mDeformVertices);
			}
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::rebuildDeformer()
	{
		if(!this->mSoftBody || !this->mMesh || this->mBuilt.positions.empty())
		{
			return;
		}
		this->mDeform.build(this->mRegions, this->mBuilt.positions,
			this->mDeformParams);
		if(!this->mDeform.isBuilt())
		{
			return;
		}
		// register the morph poses; a target that does not share the base
		// topology is reported and skipped (never a crash)
		for(VectorShapeAsset::MorphTarget const & morph : this->mMorphTargets)
		{
			if(!this->mDeform.addMorphTarget(morph.name, this->mRegions,
				morph.regions))
			{
				oDebugError("engine", 0, "VectorShapeComponent: morph target '"
					<< morph.name << "' of shape '" << this->mShapeName
					<< "' does not match the base topology - skipped");
			}
		}
		this->mDeformPositions.resize(this->mBuilt.positions.size());
		this->mDeformDirty = false;
		// a runtime that ticks GameObjects now drives the deform
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	void VectorShapeComponent::applyDeformParams()
	{
		if(this->mDeform.isBuilt())
		{
			this->mDeform.setParams(this->mDeformParams);
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setWobbleStiffness(float stiffness)
	{
		this->mDeformParams.wobbleStiffness = stiffness;
		this->applyDeformParams();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setWobbleDamping(float damping)
	{
		this->mDeformParams.wobbleDamping = damping;
		this->applyDeformParams();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setWobbleAmount(float amount)
	{
		this->mDeformParams.wobbleAmount = amount;
		this->applyDeformParams();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setSquashAmount(float amount)
	{
		this->mDeformParams.squashAmount = amount;
		this->applyDeformParams();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setMorphClip(int index)
	{
		this->mMorphClip = index;
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setMorphSpeed(float speed)
	{
		this->mMorphSpeed = speed;
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setMorphLoop(bool loop)
	{
		this->mMorphLoop = loop;
	}
	//---------------------------------------------------------
	void VectorShapeComponent::impulse(float dirX, float dirY, float magnitude)
	{
		if(this->mSoftBody && this->mDeform.isBuilt())
		{
			this->mDeform.applyImpulse(dirX, dirY, magnitude);
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::playMorph(int index, float speed, bool loop)
	{
		if(this->mSoftBody && this->mDeform.isBuilt())
		{
			this->mDeform.playMorph(index, speed, loop);
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::stopMorph()
	{
		if(this->mDeform.isBuilt())
		{
			this->mDeform.stopMorph();
		}
	}
	//---------------------------------------------------------
	float VectorShapeComponent::getDeformDisplacement() const
	{
		return this->mDeform.isBuilt()
			? this->mDeform.maxControlDisplacement() : 0.0f;
	}
	//---------------------------------------------------------
	float VectorShapeComponent::getSquash() const
	{
		return this->mDeform.isBuilt() ? this->mDeform.getSquash() : 0.0f;
	}
	//---------------------------------------------------------
	bool VectorShapeComponent::isDeforming() const
	{
		return this->mSoftBody && this->mDeform.isBuilt() &&
			!this->mDeform.isAtRest();
	}
	//---------------------------------------------------------
	std::size_t VectorShapeComponent::getControlPointCount() const
	{
		return this->mDeform.controlPointCount();
	}
	//---------------------------------------------------------
	std::size_t VectorShapeComponent::getMorphTargetCount() const
	{
		return this->mDeform.morphTargetCount();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::onUpdateComponent(float deltaTime)
	{
		// dormant unless soft-body is on and the deformer built (like
		// ScriptComponent, only a GameObject-ticking runtime reaches here)
		if(!this->mSoftBody || !this->mMesh || !this->mDeform.isBuilt())
		{
			return;
		}
		// cache the sibling body's velocity: it drives the velocity stretch AND
		// is the impact magnitude/direction a contact this frame squashes along
		// (sampled BEFORE the physics step in the player tick order, so it is the
		// pre-collision approach velocity)
		this->mBodyVelX = 0.0f;
		this->mBodyVelY = 0.0f;
		GameObject* componentOwner = this->getComponentOwner();
		if(componentOwner && componentOwner->hasComponent<RigidBodyComponent>())
		{
			RigidBodyComponent* body =
				componentOwner->getComponentPtr<RigidBodyComponent>();
			if(body && body->hasBody())
			{
				const Vec3 velocity = body->getLinearVelocity();
				this->mBodyVelX = velocity.x;
				this->mBodyVelY = velocity.y;
			}
		}
		this->mDeform.setBodyVelocity(this->mBodyVelX, this->mBodyVelY);
		this->mDeform.update(deltaTime);

		// upload while deforming, plus ONE final frame to land the exact rest
		// pose (mDeformDirty), then fall silent (no per-frame cost at rest)
		const bool atRest = this->mDeform.isAtRest();
		if(!atRest || this->mDeformDirty)
		{
			// a setMesh already mapped the buffer this frame - skip THIS upload
			// (defer a tick) so the next backend never maps it twice per frame
			if(this->mDeformFreshBuild)
			{
				this->mDeformFreshBuild = false;
				return;
			}
			this->mDeform.writePositions(this->mDeformPositions);
			for(std::size_t v = 0; v < this->mDeformPositions.size() &&
				v < this->mDeformVertices.size(); ++v)
			{
				this->mDeformVertices[v].position = Vec2(
					this->mDeformPositions[v].x, this->mDeformPositions[v].y);
			}
			this->mRuns.update(*this->mMesh, this->mDeformVertices);
			this->mDeformDirty = !atRest;
		}
	}
	//---------------------------------------------------------
	bool VectorShapeComponent::onContactBegan(Event const & event)
	{
		// a contact squashes the soft body along the pre-collision approach
		// velocity (the impact normal) with a depth from the impact speed, and
		// kicks the wobble the same way. The physics body stays rigid.
		if(this->mSoftBody && this->mDeform.isBuilt())
		{
			const float speed = std::sqrt(this->mBodyVelX * this->mBodyVelX +
				this->mBodyVelY * this->mBodyVelY);
			if(speed > 1.0e-3f)
			{
				this->mDeform.applyImpulse(this->mBodyVelX, this->mBodyVelY,
					speed);
			}
		}
		return false;	// never consume the contact (others still observe it)
	}
	//---------------------------------------------------------
	void VectorShapeComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: tint, scale, edge softness,
		// zOrder, visibility and the shape AssetRef (its stable id rides the
		// record for rename survival) are written by name off the declared
		// schema. The shape is declared LAST so the scalar state is set before
		// the mesh rebuild reads it on load (@see loadComponentProperties)
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void VectorShapeComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(VectorShapeComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(loadShape)
		OFUNC(removeShape)
		OFUNCCR(getShapeName)
		OFUNC(hasShape)
		OFUNC(getTriangleCount)
		OFUNC(setTint)
		OFUNC(setScale)
		OFUNC(getScale)
		OFUNC(setEdgeSoftness)
		OFUNC(getEdgeSoftness)
		OFUNC(setZOrder)
		OFUNC(getZOrder)
		OFUNC(setShapeVisible)
		OFUNC(isShapeVisible)
		// soft-body drive + introspection (Lua + native): a script kicks a
		// squash/wobble or plays a morph; the getters feed selfchecks/MCP
		OFUNC(setSoftBodyEnabled)
		OFUNC(isSoftBodyEnabled)
		OFUNC(impulse)
		OFUNC(playMorph)
		OFUNC(stopMorph)
		OFUNC(getDeformDisplacement)
		OFUNC(getSquash)
		OFUNC(isDeforming)
		OFUNC(getControlPointCount)
		OFUNC(getMorphTargetCount)
		// reflected schema: scalar/colour state THEN the shape reference last
		// (its setter rebuilds the mesh + deformer from the state set just above)
		OPROPERTY("tint", Orkige::PropertyKind::Color, getTint, setTintColor, Orkige::PROP_NONE)
		OPROPERTY("scale", Orkige::PropertyKind::Float, getScale, setScale, Orkige::PROP_NONE)
		OPROPERTY("edgeSoftness", Orkige::PropertyKind::Float, getEdgeSoftness, setEdgeSoftness, Orkige::PROP_NONE)
		OPROPERTY("zOrder", Orkige::PropertyKind::Int, getZOrder, setZOrder, Orkige::PROP_NONE)
		OPROPERTY("visible", Orkige::PropertyKind::Bool, isShapeVisible, setShapeVisible, Orkige::PROP_NONE)
		// soft-body tunables (set before the shape, so the deformer that the
		// shape reference builds picks them up): inspector/serialization/MCP free
		OPROPERTY("softBody", Orkige::PropertyKind::Bool, isSoftBodyEnabled, setSoftBodyEnabled, Orkige::PROP_NONE)
		OPROPERTY("wobbleStiffness", Orkige::PropertyKind::Float, getWobbleStiffness, setWobbleStiffness, Orkige::PROP_NONE)
		OPROPERTY("wobbleDamping", Orkige::PropertyKind::Float, getWobbleDamping, setWobbleDamping, Orkige::PROP_NONE)
		OPROPERTY("wobbleAmount", Orkige::PropertyKind::Float, getWobbleAmount, setWobbleAmount, Orkige::PROP_NONE)
		OPROPERTY("squashAmount", Orkige::PropertyKind::Float, getSquashAmount, setSquashAmount, Orkige::PROP_NONE)
		OPROPERTY("morphClip", Orkige::PropertyKind::Int, getMorphClip, setMorphClip, Orkige::PROP_NONE)
		OPROPERTY("morphSpeed", Orkige::PropertyKind::Float, getMorphSpeed, setMorphSpeed, Orkige::PROP_NONE)
		OPROPERTY("morphLoop", Orkige::PropertyKind::Bool, getMorphLoop, setMorphLoop, Orkige::PROP_NONE)
		OPROPERTY_REF("shape", Orkige::PropertyKind::AssetRef, "shape", getShapeName, setShapeReference, Orkige::PROP_NONE)

		// self.shape / world.get(id):getShape... hand Lua a WEAK handle: locks per
		// call, raises an honest error naming the owner once gone. @see TransformComponent.
		OWEAKHANDLE_BEGIN(Orkige::VectorShapeComponent, "VectorShapeComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(loadShape)
			OWEAKHANDLE_BASEMETHOD(removeShape)
			OWEAKHANDLE_BASEMETHOD(getShapeName)
			OWEAKHANDLE_BASEMETHOD(hasShape)
			OWEAKHANDLE_BASEMETHOD(getTriangleCount)
			OWEAKHANDLE_BASEMETHOD(setTint)
			OWEAKHANDLE_BASEMETHOD(setScale)
			OWEAKHANDLE_BASEMETHOD(getScale)
			OWEAKHANDLE_BASEMETHOD(setEdgeSoftness)
			OWEAKHANDLE_BASEMETHOD(getEdgeSoftness)
			OWEAKHANDLE_BASEMETHOD(setZOrder)
			OWEAKHANDLE_BASEMETHOD(getZOrder)
			OWEAKHANDLE_BASEMETHOD(setShapeVisible)
			OWEAKHANDLE_BASEMETHOD(isShapeVisible)
			OWEAKHANDLE_BASEMETHOD(setSoftBodyEnabled)
			OWEAKHANDLE_BASEMETHOD(isSoftBodyEnabled)
			OWEAKHANDLE_BASEMETHOD(impulse)
			OWEAKHANDLE_BASEMETHOD(playMorph)
			OWEAKHANDLE_BASEMETHOD(stopMorph)
			OWEAKHANDLE_BASEMETHOD(getDeformDisplacement)
			OWEAKHANDLE_BASEMETHOD(getSquash)
			OWEAKHANDLE_BASEMETHOD(isDeforming)
			OWEAKHANDLE_BASEMETHOD(getControlPointCount)
			OWEAKHANDLE_BASEMETHOD(getMorphTargetCount)
		OWEAKHANDLE_END
	OOBJECT_END
}
