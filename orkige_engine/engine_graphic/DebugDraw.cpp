/********************************************************************
	created:	Friday 2026/07/25 at 12:00
	filename: 	DebugDraw.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_graphic/DebugDraw.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"

namespace Orkige
{
	//---------------------------------------------------------
	IMPL_OSINGLETON(DebugDraw);
	//---------------------------------------------------------
	DebugDraw::DebugDraw()
		: mVisible(false)
	{
	}
	//---------------------------------------------------------
	DebugDraw::~DebugDraw()
	{
		// content first, then the node (a node must outlive its content)
		this->mMesh.reset();
		this->mNode.reset();
	}
	//---------------------------------------------------------
	void DebugDraw::line(Vec3 const & p1, Vec3 const & p2,
		Color const & colour, float seconds)
	{
		this->mBuffer.addLine(p1, p2, colour, seconds);
	}
	//---------------------------------------------------------
	void DebugDraw::box(Vec3 const & centre, Vec3 const & halfExtents,
		Color const & colour, float seconds)
	{
		this->mBuffer.addBox(centre, halfExtents, colour, seconds);
	}
	//---------------------------------------------------------
	void DebugDraw::sphere(Vec3 const & centre, float radius,
		Color const & colour, float seconds)
	{
		this->mBuffer.addSphere(centre, radius, colour, seconds);
	}
	//---------------------------------------------------------
	void DebugDraw::ensureMesh()
	{
		if(this->mMesh || !RenderSystem::get() || !RenderSystem::get()->getWorld())
		{
			return;
		}
		RenderWorld* world = RenderSystem::get()->getWorld();
		// a world-space (identity) node under the root the debug lines ride: the
		// draw.* coordinates are already world coordinates
		this->mNode = world->createNode();
		this->mMesh = world->createLineMesh();
		this->mMesh->attachTo(this->mNode);
		this->mMesh->setVisible(false);
	}
	//---------------------------------------------------------
	void DebugDraw::update(float deltaTime)
	{
		if(this->mBuffer.empty())
		{
			// nothing queued: hide any existing mesh, but stay zero-cost when
			// this frame's scripts drew nothing (no mesh is created)
			if(this->mMesh && this->mVisible)
			{
				this->mMesh->setLines(NULL, 0, LineMesh::TOPOLOGY_SEGMENTS);
				this->mMesh->setVisible(false);
				this->mVisible = false;
			}
			return;
		}
		this->ensureMesh();
		if(this->mMesh)
		{
			// expand every live primitive to line segments and rebuild the mesh
			// (topology + count change every frame as primitives come and go, so
			// this is a setLines rebuild - ONE per frame, one draw call; the
			// per-frame updateVertices fast path is the LineComponent case)
			this->mBuffer.buildSegments(this->mVertices);
			this->mMesh->setLines(this->mVertices.data(), this->mVertices.size(),
				LineMesh::TOPOLOGY_SEGMENTS);
			this->mMesh->setVisible(true);
			this->mVisible = true;
		}
		// age the lifetimes AFTER the flush: frame-only primitives rendered their
		// one frame and drop; TTL primitives survive until their time runs out
		this->mBuffer.advance(deltaTime);
	}
	//---------------------------------------------------------
	void DebugDraw::clear()
	{
		this->mBuffer.clear();
		if(this->mMesh)
		{
			this->mMesh->setLines(NULL, 0, LineMesh::TOPOLOGY_SEGMENTS);
			this->mMesh->setVisible(false);
		}
		this->mVisible = false;
	}
	//---------------------------------------------------------
	std::size_t DebugDraw::getPrimitiveCount() const
	{
		return this->mBuffer.getPrimitiveCount();
	}
	//---------------------------------------------------------
	std::size_t DebugDraw::getMeshVertexCount() const
	{
		return this->mMesh ? this->mMesh->getVertexCount() : 0;
	}
}
