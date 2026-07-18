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
//! Ogre::ManualObject rebuilt from the owner's CPU section list (one
//! ManualObject section per facade Section). Flat sections share ONE
//! generated "VectorFill" material (unlit, vertex colours tracked,
//! alpha-blended, depth-checked/not-written, two-sided; no texture unit), so
//! their colour lives entirely in the vertex data; TEXTURED sections bind
//! the per-(texture,sampler) sprite material (getOrCreateSpriteMaterial -
//! reused wholesale, same 2D rules) and additionally emit a UV stream.
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
		// 2D content stays out of the shadow pass (@see the sprite material)
		material->setReceiveShadows(false);
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
		// the 2D layer neither casts nor receives shadows by construction
		handle->mImpl->mesh->setCastShadows(false);
		// tag the 2D tier so the bloom scene split keeps it out of the glow
		RenderBackend::tagScene2D(handle->mImpl->mesh);
		// dynamic: the geometry is refilled (static once, deformable per frame)
		handle->mImpl->mesh->setDynamic(true);
		RenderBackend::applyZOrder(handle->mImpl->mesh, 0);
		return handle;
	}
	//---------------------------------------------------------
	//! @brief resolve one facade section to its material: the shared flat
	//! "VectorFill" recipe, or the per-(texture,sampler) SPRITE material
	//! (reused wholesale - same 2D rules, bilinear+clamp). A texture that
	//! fails to load logs once (per mesh+name) and falls back to the flat
	//! recipe, so bad content shows a tint-coloured silhouette, never crashes.
	static String resolveVectorSectionMaterial(
		VectorMesh::Section const & section, bool & outTextured)
	{
		outTextured = false;
		if(section.texture.empty())
		{
			return "VectorFill";
		}
		Ogre::TexturePtr texture;
		try
		{
			// cooked-payload fallback (foo.png -> foo.dds/.ktx in exports),
			// resolved through every resource group like a sprite texture
			texture = Ogre::TextureManager::getSingleton().load(
				RenderBackend::resolveTextureResourceName(section.texture),
				Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
		}
		catch(Ogre::Exception const & e)
		{
			oDebugError("engine", 0, "RenderWorld: vector-mesh texture '"
				<< section.texture << "' failed to load: "
				<< e.getDescription());
			return "VectorFill";
		}
		if(!texture)
		{
			oDebugError("engine", 0, "RenderWorld: vector-mesh texture '"
				<< section.texture << "' not found");
			return "VectorFill";
		}
		RenderBackend::getOrCreateSpriteMaterial(texture,
			SpriteQuad::FILTER_BILINEAR, SpriteQuad::ADDRESS_CLAMP);
		outTextured = true;
		return SpriteQuad::samplerName(texture->getName(),
			SpriteQuad::FILTER_BILINEAR, SpriteQuad::ADDRESS_CLAMP);
	}
	//---------------------------------------------------------
	void VectorMesh::Impl::rebuild(VectorMesh::Section const * list,
		std::size_t count)
	{
		oAssert(this->mesh);
		this->mesh->clear();
		this->triangleCount = 0;
		this->sections.clear();
		std::size_t ogreSection = 0;
		for(std::size_t s = 0; s < count; ++s)
		{
			VectorMesh::Section const & source = list[s];
			if(source.vertexCount == 0 || source.vertices == NULL ||
				source.indexCount < 3 || source.indices == NULL)
			{
				// keep a placeholder so the caller's section indices stay
				// stable (its dynamic updates are ignored)
				this->sections.push_back(BuiltSection());
				continue;
			}
			BuiltSection built;
			built.material = resolveVectorSectionMaterial(source,
				built.textured);
			built.ogreSection = ogreSection++;
			// cache the topology so a later beginUpdate can re-emit it without
			// the owner passing indices again (the dynamic upload only sends
			// vertices)
			built.vertexCount = source.vertexCount;
			built.indices.resize(source.indexCount);
			for(std::size_t each = 0; each < source.indexCount; ++each)
			{
				built.indices[each] =
					static_cast<Ogre::uint32>(source.indices[each]);
			}
			this->mesh->estimateVertexCount(source.vertexCount);
			this->mesh->estimateIndexCount(source.indexCount);
			this->mesh->begin(built.material,
				Ogre::RenderOperation::OT_TRIANGLE_LIST);
			for(std::size_t each = 0; each < source.vertexCount; ++each)
			{
				VectorMesh::Vertex const & vertex = source.vertices[each];
				this->mesh->position(
					Ogre::Vector3(vertex.position.x, vertex.position.y, 0.0f));
				this->mesh->colour(vertex.colour);
				if(built.textured)
				{
					this->mesh->textureCoord(vertex.uv.x, vertex.uv.y);
				}
			}
			const std::size_t triangles = source.indexCount / 3;
			for(std::size_t t = 0; t < triangles; ++t)
			{
				this->mesh->triangle(built.indices[t * 3 + 0],
					built.indices[t * 3 + 1], built.indices[t * 3 + 2]);
			}
			this->mesh->end();
			this->triangleCount += triangles;
			this->sections.push_back(std::move(built));
		}
		RenderBackend::applyZOrder(this->mesh, this->zOrder);
	}
	//---------------------------------------------------------
	void VectorMesh::Impl::updateSection(std::size_t index,
		VectorMesh::Vertex const * vertices, std::size_t count)
	{
		oAssert(this->mesh);
		// only a topology-preserving refresh of a built section (setMesh
		// first); a mismatch falls back silently so a stale caller can't
		// corrupt the buffer
		if(index >= this->sections.size() || vertices == NULL)
		{
			return;
		}
		BuiltSection const & built = this->sections[index];
		if(built.vertexCount == 0 || count != built.vertexCount ||
			built.indices.empty())
		{
			return;
		}
		// beginUpdate reuses the existing hardware buffers (no realloc when the
		// counts match): re-emit the moved vertices and the cached topology
		this->mesh->beginUpdate(built.ogreSection);
		for(std::size_t each = 0; each < count; ++each)
		{
			VectorMesh::Vertex const & vertex = vertices[each];
			this->mesh->position(
				Ogre::Vector3(vertex.position.x, vertex.position.y, 0.0f));
			this->mesh->colour(vertex.colour);
			if(built.textured)
			{
				this->mesh->textureCoord(vertex.uv.x, vertex.uv.y);
			}
		}
		const std::size_t triangles = built.indices.size() / 3;
		for(std::size_t t = 0; t < triangles; ++t)
		{
			this->mesh->triangle(built.indices[t * 3 + 0],
				built.indices[t * 3 + 1], built.indices[t * 3 + 2]);
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
		// exactly one untextured section (the flat identity path)
		Section section;
		section.vertices = vertices;
		section.vertexCount = vertexCount;
		section.indices = indices;
		section.indexCount = indexCount;
		this->mImpl->rebuild(&section, 1);
	}
	//---------------------------------------------------------
	void VectorMesh::updateVertices(Vertex const * vertices,
		std::size_t vertexCount)
	{
		this->mImpl->updateSection(0, vertices, vertexCount);
	}
	//---------------------------------------------------------
	void VectorMesh::setMeshSections(Section const * sections,
		std::size_t sectionCount)
	{
		this->mImpl->rebuild(sections, sectionCount);
	}
	//---------------------------------------------------------
	void VectorMesh::updateSectionVertices(std::size_t sectionIndex,
		Vertex const * vertices, std::size_t vertexCount)
	{
		this->mImpl->updateSection(sectionIndex, vertices, vertexCount);
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
		RenderBackend::applyZOrder(this->mImpl->mesh, this->mImpl->zOrder);
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
