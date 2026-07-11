/********************************************************************
	created:	Thursday 2026/07/10 at 11:00
	filename: 	VectorMeshClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file VectorMeshClassic.cpp
//! @brief classic-OGRE implementation of the VectorMesh facade
//! @remarks The arbitrary-triangle sibling of SpriteBatchClassic: one
//! Ogre::ManualObject rebuilt from the owner's CPU vertex+index array.
//! Untextured - all shapes share ONE generated "VectorFill" material (unlit,
//! vertex colours tracked, alpha-blended, depth-checked/not-written,
//! two-sided; no texture unit), so colour lives entirely in the vertex data.
//! zOrder maps onto the SAME render-queue painter window SpriteQuad uses.

#include "engine_render_classic/ClassicBackend.h"
#include <core_debug/DebugMacros.h>

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	Ogre::MaterialPtr RenderBackend::getOrCreateVectorFillMaterial()
	{
		const String materialName = "VectorFill";
		Ogre::MaterialManager & materialManager =
			Ogre::MaterialManager::getSingleton();
		if(materialManager.resourceExists(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			return materialManager.getByName(materialName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		}
		// the honest 2D recipe with NO texture unit: the fill colour and the
		// feather alpha ramp ride the tracked vertex colour (TVC_DIFFUSE)
		Ogre::MaterialPtr material = materialManager.create(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
		pass->setLightingEnabled(false);
		pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
		pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
		pass->setDepthWriteEnabled(false);
		pass->setCullingMode(Ogre::CULL_NONE);
		return material;
	}
	//---------------------------------------------------------
	optr<VectorMesh> RenderBackend::createVectorMesh(
		Ogre::SceneManager* sceneManager)
	{
		oAssert(sceneManager);
		optr<VectorMesh> handle(new VectorMesh());
		RenderBackend::getOrCreateVectorFillMaterial();
		handle->mImpl->creator = sceneManager;
		handle->mImpl->mesh = sceneManager->createManualObject(
			RenderBackend::generateName("RenderFacade/VectorMesh"));
		// shapes never participate in editor picking
		handle->mImpl->mesh->setQueryFlags(0);
		// dynamic: the geometry is refilled (static once, deformable per frame)
		handle->mImpl->mesh->setDynamic(true);
		handle->mImpl->mesh->setRenderQueueGroup(
			RenderBackend::renderQueueForZOrder(0));
		return handle;
	}
	//---------------------------------------------------------
	void VectorMesh::Impl::rebuild(VectorMesh::Vertex const * vertices,
		std::size_t vertexCount, unsigned int const * indices,
		std::size_t indexCount)
	{
		oAssert(this->mesh);
		this->mesh->clear();
		this->triangleCount = indexCount / 3;
		this->vertexCount = 0;
		this->indices.clear();
		if(vertexCount == 0 || vertices == NULL || indexCount < 3 ||
			indices == NULL)
		{
			this->triangleCount = 0;
			return;	// nothing to draw
		}
		// cache the topology so a later beginUpdate can re-emit it without the
		// owner passing indices again (the dynamic deform only sends vertices)
		this->vertexCount = vertexCount;
		this->indices.resize(indexCount);
		for(std::size_t each = 0; each < indexCount; ++each)
		{
			this->indices[each] = static_cast<Ogre::uint32>(indices[each]);
		}
		this->mesh->estimateVertexCount(vertexCount);
		this->mesh->estimateIndexCount(indexCount);
		this->mesh->begin("VectorFill", Ogre::RenderOperation::OT_TRIANGLE_LIST);
		for(std::size_t each = 0; each < vertexCount; ++each)
		{
			VectorMesh::Vertex const & vertex = vertices[each];
			this->mesh->position(
				Ogre::Vector3(vertex.position.x, vertex.position.y, 0.0f));
			this->mesh->colour(vertex.colour);
		}
		const std::size_t triangles = indexCount / 3;
		for(std::size_t t = 0; t < triangles; ++t)
		{
			this->mesh->triangle(this->indices[t * 3 + 0],
				this->indices[t * 3 + 1], this->indices[t * 3 + 2]);
		}
		this->mesh->end();
		this->mesh->setRenderQueueGroup(
			RenderBackend::renderQueueForZOrder(this->zOrder));
	}
	//---------------------------------------------------------
	void VectorMesh::Impl::updateVertices(VectorMesh::Vertex const * vertices,
		std::size_t count)
	{
		oAssert(this->mesh);
		// only a topology-preserving refresh of a built section (setMesh first);
		// a mismatch falls back silently so a stale caller can't corrupt the buffer
		if(this->vertexCount == 0 || count != this->vertexCount ||
			vertices == NULL || this->indices.empty())
		{
			return;
		}
		// beginUpdate reuses the existing hardware buffers (no realloc when the
		// counts match): re-emit the moved vertices and the cached topology
		this->mesh->beginUpdate(0);
		for(std::size_t each = 0; each < count; ++each)
		{
			VectorMesh::Vertex const & vertex = vertices[each];
			this->mesh->position(
				Ogre::Vector3(vertex.position.x, vertex.position.y, 0.0f));
			this->mesh->colour(vertex.colour);
		}
		const std::size_t triangles = this->indices.size() / 3;
		for(std::size_t t = 0; t < triangles; ++t)
		{
			this->mesh->triangle(this->indices[t * 3 + 0],
				this->indices[t * 3 + 1], this->indices[t * 3 + 2]);
		}
		this->mesh->end();
	}
	//---------------------------------------------------------
	VectorMesh::VectorMesh()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	VectorMesh::~VectorMesh()
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
	void VectorMesh::attachTo(optr<RenderNode> const & node)
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
	void VectorMesh::detach()
	{
		if(this->mImpl->mesh->isAttached())
		{
			this->mImpl->mesh->detachFromParent();
		}
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	void VectorMesh::setMesh(Vertex const * vertices, std::size_t vertexCount,
		unsigned int const * indices, std::size_t indexCount)
	{
		this->mImpl->rebuild(vertices, vertexCount, indices, indexCount);
	}
	//---------------------------------------------------------
	void VectorMesh::updateVertices(Vertex const * vertices,
		std::size_t vertexCount)
	{
		this->mImpl->updateVertices(vertices, vertexCount);
	}
	//---------------------------------------------------------
	std::size_t VectorMesh::getTriangleCount() const
	{
		return this->mImpl->triangleCount;
	}
	//---------------------------------------------------------
	void VectorMesh::setZOrder(int zOrder)
	{
		this->mImpl->zOrder = std::clamp(zOrder,
			SpriteQuad::ZORDER_MIN, SpriteQuad::ZORDER_MAX);
		this->mImpl->mesh->setRenderQueueGroup(
			RenderBackend::renderQueueForZOrder(this->mImpl->zOrder));
	}
	//---------------------------------------------------------
	void VectorMesh::setVisible(bool visible)
	{
		this->mImpl->mesh->setVisible(visible);
	}
	//---------------------------------------------------------
	void VectorMesh::setQueryFlags(unsigned int flags)
	{
		this->mImpl->mesh->setQueryFlags(flags);
	}
}
