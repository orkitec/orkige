/**************************************************************
	created:	2026/07/25 at 12:00
	filename: 	LineComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/LineComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include <core_script/ScriptRuntime.h>	// OSCRIPT_HANDLE: ScriptComponentAccess registry

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	LineComponent::LineComponent()
		: mMode(LM_STRIP), mColour(Color::White), mDepthTest(true)
		, mUploadedCount(0), mRebuildCount(0)
		, mUploadedMode(LM_STRIP), mDirty(false), mTicked(false)
		, mFreshBuild(false)
	{
		this->addDependency<TransformComponent>();
	}
	//---------------------------------------------------------
	LineComponent::~LineComponent()
	{
	}
	//---------------------------------------------------------
	void LineComponent::setPoints(std::vector<Vec3> const & points)
	{
		this->mPoints = points;
		this->mDirty = true;
		// editor / pre-first-tick: upload synchronously so the change shows now.
		// under a ticking runtime, defer to onUpdateComponent so repeated
		// setPoints in one frame coalesce into one buffer map (the next backend
		// forbids mapping a buffer twice per frame)
		if(!this->mTicked)
		{
			// editor / pre-first-tick: a full rebuild (safe to repeat per frame,
			// spaced by renders in the editor - never the dynamic map fast path,
			// which could collide with the load-time setLines on the same frame)
			this->flushUpload(true);
		}
	}
	//---------------------------------------------------------
	void LineComponent::setPointsFlat(std::vector<float> const & coords)
	{
		std::vector<Vec3> points;
		points.reserve(coords.size() / 3);
		for(std::size_t each = 0; each + 2 < coords.size(); each += 3)
		{
			points.push_back(
				Vec3(coords[each], coords[each + 1], coords[each + 2]));
		}
		this->setPoints(points);
	}
	//---------------------------------------------------------
	void LineComponent::beginPoints()
	{
		this->mStaging.clear();
	}
	//---------------------------------------------------------
	void LineComponent::addPoint(float x, float y, float z)
	{
		this->mStaging.push_back(Vec3(x, y, z));
	}
	//---------------------------------------------------------
	void LineComponent::commitPoints()
	{
		this->setPoints(this->mStaging);
	}
	//---------------------------------------------------------
	void LineComponent::clearPoints()
	{
		this->setPoints(std::vector<Vec3>());
	}
	//---------------------------------------------------------
	std::size_t LineComponent::getVertexCount() const
	{
		return this->mMesh ? this->mMesh->getVertexCount() : 0;
	}
	//---------------------------------------------------------
	void LineComponent::setMode(LineMode mode)
	{
		if(this->mMode == mode)
		{
			return;
		}
		this->mMode = mode;
		this->mDirty = true;
		if(!this->mTicked)
		{
			// editor / pre-first-tick: a full rebuild (safe to repeat per frame,
			// spaced by renders in the editor - never the dynamic map fast path,
			// which could collide with the load-time setLines on the same frame)
			this->flushUpload(true);
		}
	}
	//---------------------------------------------------------
	void LineComponent::setColour(float red, float green, float blue,
		float alpha)
	{
		this->mColour = Color(red, green, blue, alpha);
		this->mDirty = true;
		if(!this->mTicked)
		{
			// editor / pre-first-tick: a full rebuild (safe to repeat per frame,
			// spaced by renders in the editor - never the dynamic map fast path,
			// which could collide with the load-time setLines on the same frame)
			this->flushUpload(true);
		}
	}
	//---------------------------------------------------------
	void LineComponent::setDepthTest(bool depthTest)
	{
		this->mDepthTest = depthTest;
		if(this->mMesh)
		{
			this->mMesh->setDepthTest(depthTest);	// cheap material swap, no re-upload
		}
	}
	//---------------------------------------------------------
	void LineComponent::setLineVisible(bool visible)
	{
		// line visibility IS the generic component enable switch - ONE flag
		this->setEnabled(visible);
	}
	//---------------------------------------------------------
	bool LineComponent::isLineVisible() const
	{
		return this->isEnabled();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void LineComponent::onAdd()
	{
		oAssert(!this->mMesh);
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		optr<RenderNode> node = transformComponent->createChildNode(
			componentOwner->getObjectID() + ".LineComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);

		// create the facade line mesh when a world is up (a UI-only host - or a
		// detached unit test - has none: the state stays on the component and
		// uploads when a mesh later exists)
		if(RenderSystem::get() && RenderSystem::get()->getWorld())
		{
			this->mMesh = RenderSystem::get()->getWorld()->createLineMesh();
			this->mMesh->setDepthTest(this->mDepthTest);
			this->mMesh->attachTo(this->getNode());
			this->flushUpload(true);	// empty points here - a no-op until load/setPoints
		}
		this->applyVisibility();
		// receive per-frame ticks under a runtime: onUpdateComponent is the ONE
		// coalesced upload site for runtime setPoints (cheap no-op when idle).
		// The editor never ticks GameObjects, so this stays dormant in edit mode.
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	void LineComponent::onRemove()
	{
		// content first, then the node (a node must outlive its content)
		this->mMesh.reset();
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void LineComponent::applyEffectiveEnabled()
	{
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	void LineComponent::onUpdateComponent(float deltaTime)
	{
		// a ticking runtime reaches here (the editor never does): from now on
		// dynamic uploads are coalesced to THIS single per-frame site
		this->mTicked = true;
		if(!this->mDirty)
		{
			return;
		}
		// one-tick defer after any setLines: a rebuild already mapped the buffer
		// this frame (or the load-time setLines mapped it before the first
		// render), so skip THIS update and clear the flag - the next backend
		// forbids mapping the same buffer twice per frame
		if(this->mFreshBuild)
		{
			this->mFreshBuild = false;
			return;
		}
		this->flushUpload(false);
	}
	//---------------------------------------------------------
	void LineComponent::flushUpload(bool forceRebuild)
	{
		if(!this->mMesh)
		{
			return;
		}
		// nothing authored yet: leave the mesh geometry-free without a rebuild
		// (so an empty component never bumps the churn probe)
		if(this->mPoints.empty() && this->mUploadedCount == 0)
		{
			this->mDirty = false;
			return;
		}
		this->mVertexScratch.clear();
		this->mVertexScratch.reserve(this->mPoints.size());
		for(Vec3 const & point : this->mPoints)
		{
			this->mVertexScratch.push_back(
				LineMesh::Vertex(point, this->mColour));
		}
		const std::size_t count = this->mVertexScratch.size();
		const LineMesh::Topology topology = this->mMode == LM_SEGMENTS
			? LineMesh::TOPOLOGY_SEGMENTS : LineMesh::TOPOLOGY_STRIP;
		// dynamic fast path only when NOT forced, the built vertex count AND the
		// topology are unchanged: rewrite the buffer in place (no reallocation).
		// Otherwise a full rebuild establishes the new topology/count AND arms the
		// one-tick defer so the next update never double-maps the fresh buffer.
		if(!forceRebuild && count >= 2 &&
			count == this->mMesh->getVertexCount() &&
			this->mMode == this->mUploadedMode)
		{
			this->mMesh->updateVertices(this->mVertexScratch.data(), count);
		}
		else
		{
			this->mMesh->setLines(this->mVertexScratch.data(), count, topology);
			this->mUploadedCount = this->mMesh->getVertexCount();
			this->mUploadedMode = this->mMode;
			this->mFreshBuild = true;
			++this->mRebuildCount;	// a topology/count-changing rebuild (churn probe)
		}
		this->mDirty = false;
	}
	//---------------------------------------------------------
	void LineComponent::applyVisibility()
	{
		oAssert(this->mNode);
		// enabled AND owner-active compose into effectivelyEnabled()
		this->setVisible(this->effectivelyEnabled());
	}
	//---------------------------------------------------------
	void LineComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: mode (enum), colour, depthTest,
		// visibility - written by name off the declared schema
		SceneSerializer::saveComponentProperties(ar, *this);
		// the POINTS are bulk data, not per-point reflected props: a count then
		// XYZ triples ride the record after the named block (the tag-list
		// precedent - @see SceneSerializer). load() reads the SAME sequence with
		// the `>>` operator.
		unsigned int pointCount = static_cast<unsigned int>(this->mPoints.size());
		ar << pointCount;
		for(Vec3 & point : this->mPoints)
		{
			float x = point.x, y = point.y, z = point.z;
			ar << x;
			ar << y;
			ar << z;
		}
	}
	//---------------------------------------------------------
	void LineComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
		unsigned int pointCount = 0;
		ar >> pointCount;
		this->mPoints.clear();
		this->mPoints.reserve(pointCount);
		for(unsigned int each = 0; each < pointCount; ++each)
		{
			float x = 0.0f, y = 0.0f, z = 0.0f;
			ar >> x;
			ar >> y;
			ar >> z;
			this->mPoints.push_back(Vec3(x, y, z));
		}
		this->mDirty = true;
		// upload now when a mesh is live (scene load is pre-first-tick, so this is
		// the synchronous editor/load path - a full rebuild) and re-apply
		// visibility
		if(this->mMesh)
		{
			this->flushUpload(true);
		}
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(LineComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(hasMesh)
		OFUNC(setPoints)
		OFUNC(setPointsFlat)
		OFUNC(beginPoints)
		OFUNC(addPoint)
		OFUNC(commitPoints)
		OFUNC(clearPoints)
		OFUNC(getPointCount)
		OFUNC(getVertexCount)
		OFUNC(getRebuildCount)
		OFUNC(setMode)
		OFUNC(getMode)
		OFUNC(setColour)
		OFUNC(setDepthTest)
		OFUNC(getDepthTest)
		OFUNC(setLineVisible)
		OFUNC(isLineVisible)
		// neutral enum value<->label table so the reflected `mode` property can
		// resolve labels in every scripting config (the LightType precedent)
		OENUM_REGISTER_START("LineMode", LineComponent::LineMode)
			OENUM_REGISTER_VALUE(LM_STRIP)
			OENUM_REGISTER_VALUE(LM_SEGMENTS)
		OENUM_REGISTER_END
		// reflected schema: connectivity, colour, depth test and visibility.
		// Order-independent (matched by name on load). The POINTS are NOT here -
		// bulk data serializes through save/load (@see the class remarks).
		OPROPERTY_ENUM("mode", "LineMode", getMode, setMode, Orkige::PROP_NONE)
		OPROPERTY("colour", Orkige::PropertyKind::Color, getColour, setColourValue, Orkige::PROP_NONE)
		OPROPERTY("depthTest", Orkige::PropertyKind::Bool, getDepthTest, setDepthTest, Orkige::PROP_NONE)
		// no reflected `visible` - line visibility IS the inherited base
		// `enabled` property; setLineVisible/isLineVisible stay as script aliases

		// self.line / getComponent("line") hand Lua a WEAK handle: locks per
		// call, raises an honest error naming the owner once gone. @see LightComponent.
		OWEAKHANDLE_BEGIN(Orkige::LineComponent, "LineComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(hasMesh)
			OWEAKHANDLE_BASEMETHOD(beginPoints)
			OWEAKHANDLE_BASEMETHOD(addPoint)
			OWEAKHANDLE_BASEMETHOD(commitPoints)
			OWEAKHANDLE_BASEMETHOD(clearPoints)
			OWEAKHANDLE_BASEMETHOD(getPointCount)
			OWEAKHANDLE_BASEMETHOD(getVertexCount)
			OWEAKHANDLE_BASEMETHOD(getRebuildCount)
			OWEAKHANDLE_BASEMETHOD(setMode)
			OWEAKHANDLE_BASEMETHOD(getMode)
			OWEAKHANDLE_BASEMETHOD(setColour)
			OWEAKHANDLE_BASEMETHOD(setDepthTest)
			OWEAKHANDLE_BASEMETHOD(getDepthTest)
			OWEAKHANDLE_BASEMETHOD(setLineVisible)
			OWEAKHANDLE_BASEMETHOD(isLineVisible)
		OWEAKHANDLE_END
		// self.line + getComponent("line") (no world convenience accessor)
		OSCRIPT_HANDLE("line", true, "")
	OOBJECT_END
}
