/********************************************************************
	created:	Friday 2026/07/25 at 12:00
	filename: 	LineMeshClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file LineMeshClassic.cpp
//! @brief classic-OGRE implementation of the LineMesh facade
//! @remarks The 3D line sibling of VectorMeshClassic: one Ogre::ManualObject
//! rebuilt from the owner's CPU vertex array as a single OT_LINE_STRIP /
//! OT_LINE_LIST section. It shares the unlit vertex-colour material with the
//! cube/grid gizmos ("VertexColour", depth-tested) or its overlay sibling
//! ("VertexColourOverlay", depth ignored), so line colour lives entirely in the
//! vertex data. Unlike the vector mesh this is 3D scene content: it renders in
//! the main render queue (NOT the sprite 2D window), casts no shadow and is
//! never picked.

#include "engine_render_classic/ClassicBackend.h"
#include "engine_util/PrimitiveUtil.h"

namespace Orkige
{
	//---------------------------------------------------------
	Ogre::MaterialPtr RenderBackend::getOrCreateLineMaterial(bool depthTest)
	{
		Ogre::MaterialManager & materialManager =
			Ogre::MaterialManager::getSingleton();
		// the depth-tested variant IS the shared cube/grid "VertexColour"
		// material (unlit, vertex-colour tracked, opaque, depth checked+written)
		if(depthTest)
		{
			PrimitiveUtil::createVertexColourMaterial();
			return materialManager.getByName("VertexColour",
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		}
		const String materialName = "VertexColourOverlay";
		if(materialManager.resourceExists(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			return materialManager.getByName(materialName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		}
		// the overlay recipe: same unlit vertex-colour look, depth check AND
		// write off so the lines draw on top of the scene (authoring/debug)
		Ogre::MaterialPtr material = materialManager.create(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		material->setReceiveShadows(false);
		Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
		pass->setLightingEnabled(false);
		pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
		pass->setDepthCheckEnabled(false);
		pass->setDepthWriteEnabled(false);
		return material;
	}
	//---------------------------------------------------------
	optr<LineMesh> RenderBackend::createLineMesh(Ogre::SceneManager* sceneManager)
	{
		oAssert(sceneManager);
		optr<LineMesh> handle(new LineMesh());
		RenderBackend::getOrCreateLineMaterial(true);
		handle->mImpl->creator = sceneManager;
		handle->mImpl->mesh = sceneManager->createManualObject(
			RenderBackend::generateName("RenderFacade/LineMesh"));
		// 3D scene content: never picked, never a shadow caster
		handle->mImpl->mesh->setQueryFlags(0);
		handle->mImpl->mesh->setCastShadows(false);
		// dynamic: the geometry is refilled (static once, per-change after)
		handle->mImpl->mesh->setDynamic(true);
		return handle;
	}
	//---------------------------------------------------------
	Ogre::RenderOperation::OperationType
		LineMesh::Impl::operationType() const
	{
		return this->topology == LineMesh::TOPOLOGY_SEGMENTS
			? Ogre::RenderOperation::OT_LINE_LIST
			: Ogre::RenderOperation::OT_LINE_STRIP;
	}
	//---------------------------------------------------------
	void LineMesh::Impl::rebuild(LineMesh::Vertex const * vertices,
		std::size_t count, LineMesh::Topology topo)
	{
		oAssert(this->mesh);
		this->mesh->clear();
		this->vertexCount = 0;
		this->topology = topo;
		// a line needs at least two points; segment lists consume point PAIRS
		if(topo == LineMesh::TOPOLOGY_SEGMENTS)
		{
			count -= (count % 2);	// drop a dangling half-segment
		}
		if(vertices == NULL || count < 2)
		{
			return;	// geometry-free (a valid empty state)
		}
		const String material = this->depthTest
			? "VertexColour" : "VertexColourOverlay";
		this->mesh->estimateVertexCount(count);
		this->mesh->begin(material, this->operationType());
		for(std::size_t each = 0; each < count; ++each)
		{
			this->mesh->position(vertices[each].position);
			this->mesh->colour(vertices[each].colour);
		}
		this->mesh->end();
		this->vertexCount = count;
	}
	//---------------------------------------------------------
	void LineMesh::Impl::updateVertices(LineMesh::Vertex const * vertices,
		std::size_t count)
	{
		oAssert(this->mesh);
		// only a topology-preserving refresh of a built mesh (setLines first);
		// a mismatch falls back silently so a stale caller can't corrupt buffers
		if(vertices == NULL || this->vertexCount == 0 ||
			count != this->vertexCount)
		{
			return;
		}
		this->mesh->beginUpdate(0);
		for(std::size_t each = 0; each < count; ++each)
		{
			this->mesh->position(vertices[each].position);
			this->mesh->colour(vertices[each].colour);
		}
		this->mesh->end();
	}
	//---------------------------------------------------------
	void LineMesh::Impl::applyMaterial()
	{
		// swap the section material in place when geometry exists (no rebuild)
		RenderBackend::getOrCreateLineMaterial(this->depthTest);
		if(this->mesh && this->vertexCount > 0)
		{
			this->mesh->setMaterialName(0, this->depthTest
				? "VertexColour" : "VertexColourOverlay");
		}
	}
	//---------------------------------------------------------
	LineMesh::LineMesh()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	LineMesh::~LineMesh()
	{
		if(this->mImpl->mesh)
		{
			if(this->mImpl->mesh->isAttached())
			{
				this->mImpl->mesh->detachFromParent();
			}
			this->mImpl->creator->destroyManualObject(this->mImpl->mesh);
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void LineMesh::attachTo(optr<RenderNode> const & node)
	{
		oAssert(node);
		if(this->mImpl->mesh->isAttached())
		{
			this->mImpl->mesh->detachFromParent();
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->mesh);
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void LineMesh::detach()
	{
		if(this->mImpl->mesh->isAttached())
		{
			this->mImpl->mesh->detachFromParent();
		}
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	void LineMesh::setLines(Vertex const * vertices, std::size_t vertexCount,
		Topology topology)
	{
		this->mImpl->rebuild(vertices, vertexCount, topology);
	}
	//---------------------------------------------------------
	void LineMesh::updateVertices(Vertex const * vertices,
		std::size_t vertexCount)
	{
		this->mImpl->updateVertices(vertices, vertexCount);
	}
	//---------------------------------------------------------
	std::size_t LineMesh::getVertexCount() const
	{
		return this->mImpl->vertexCount;
	}
	//---------------------------------------------------------
	std::size_t LineMesh::getLineCount() const
	{
		if(this->mImpl->vertexCount < 2)
		{
			return 0;
		}
		return this->mImpl->topology == TOPOLOGY_SEGMENTS
			? this->mImpl->vertexCount / 2
			: this->mImpl->vertexCount - 1;
	}
	//---------------------------------------------------------
	void LineMesh::setDepthTest(bool depthTest)
	{
		if(this->mImpl->depthTest == depthTest)
		{
			return;
		}
		this->mImpl->depthTest = depthTest;
		this->mImpl->applyMaterial();
	}
	//---------------------------------------------------------
	bool LineMesh::getDepthTest() const
	{
		return this->mImpl->depthTest;
	}
	//---------------------------------------------------------
	void LineMesh::setVisible(bool visible)
	{
		this->mImpl->mesh->setVisible(visible);
	}
	//---------------------------------------------------------
	void LineMesh::setQueryFlags(unsigned int flags)
	{
		this->mImpl->mesh->setQueryFlags(flags);
	}
}
