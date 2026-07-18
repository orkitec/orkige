/********************************************************************
	created:	Thursday 2026/07/10 at 11:00
	filename: 	VectorMeshNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file VectorMeshNext.cpp
//! @brief Ogre-Next implementation of the VectorMesh facade
//! @remarks The arbitrary-triangle sibling of SpriteBatchNext: one v2
//! Ogre::ManualObject (SCENE_DYNAMIC) rebuilt from the owner's CPU section
//! list (one v2 section per facade Section). Flat sections share ONE
//! "VectorFill" HlmsUnlit datablock (the DrawLayer2D empty-texture recipe:
//! unlit, alpha-blended, depth-checked/not-written, two-sided; colour flows
//! from VES_DIFFUSE), so their colour lives entirely in the vertex data;
//! TEXTURED sections bind the per-(texture,sampler) sprite datablock
//! (getOrCreateSpriteDatablock - reused wholesale, same 2D rules) and
//! additionally emit a UV stream. zOrder maps onto the SAME render-queue
//! painter window SpriteQuad/SpriteBatch use.

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreManualObject2.h>
#include <OgreRoot.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsUnlit.h>
#include <OgreHlmsUnlitDatablock.h>

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	Ogre::HlmsDatablock* RenderBackend::getOrCreateVectorFillDatablock()
	{
		Ogre::HlmsManager* hlmsManager =
			RenderBackend::ogreRoot()->getHlmsManager();
		const String name = "VectorFill";
		if(Ogre::HlmsDatablock* existing =
			hlmsManager->getDatablockNoDefault(name))
		{
			return existing;
		}
		// the honest 2D rules, untextured: unlit, alpha-blended,
		// depth-checked/not-written, two-sided; the vertex colour (fill times
		// tint, or the feather alpha ramp) flows from VES_DIFFUSE with no
		// texture bound - the same empty-texture path DrawLayer2D relies on
		Ogre::HlmsUnlit* unlit = static_cast<Ogre::HlmsUnlit*>(
			hlmsManager->getHlms(Ogre::HLMS_UNLIT));
		Ogre::HlmsMacroblock macroblock;
		macroblock.mDepthWrite = false;
		macroblock.mCullMode = Ogre::CULL_NONE;
		Ogre::HlmsBlendblock blendblock;
		blendblock.setBlendType(Ogre::SBT_TRANSPARENT_ALPHA);
		Ogre::HlmsUnlitDatablock* datablock =
			static_cast<Ogre::HlmsUnlitDatablock*>(unlit->createDatablock(
				name, name, macroblock, blendblock, Ogre::HlmsParamVec()));
		RenderBackend::registerContentDatablock(datablock);
		return datablock;
	}
	//---------------------------------------------------------
	optr<VectorMesh> RenderBackend::createVectorMesh(
		Ogre::SceneManager* sceneManager)
	{
		oAssert(sceneManager);
		optr<VectorMesh> handle(new VectorMesh());
		RenderBackend::getOrCreateVectorFillDatablock();
		handle->mImpl->creator = sceneManager;
		handle->mImpl->mesh = sceneManager->createManualObject(Ogre::SCENE_DYNAMIC);
		handle->mImpl->mesh->setName(
			RenderBackend::generateName("RenderFacade/VectorMesh"));
		handle->mImpl->mesh->setQueryFlags(0);	// never picked
		// 2D content never throws shadows into a lit 3D scene
		handle->mImpl->mesh->setCastShadows(false);
		// tag the 2D tier so the bloom scene split keeps it out of the glow
		RenderBackend::tagScene2D(handle->mImpl->mesh);
		return handle;
	}
	//---------------------------------------------------------
	//! @brief resolve one facade section to its datablock: the shared flat
	//! "VectorFill" recipe, or the per-(texture,sampler) SPRITE datablock
	//! (reused wholesale - same 2D rules, bilinear+clamp). A texture that
	//! fails to load logs and falls back to the flat recipe, so bad content
	//! shows a tint-coloured silhouette, never crashes.
	static String resolveVectorSectionDatablock(
		VectorMesh::Section const & section, bool & outTextured)
	{
		outTextured = false;
		if(section.texture.empty())
		{
			return "VectorFill";
		}
		Ogre::TextureGpu* texture =
			RenderBackend::loadTexture2D(section.texture);
		if(!texture)
		{
			oDebugError("engine", 0, "RenderWorld: vector-mesh texture '"
				<< section.texture << "' not found");
			return "VectorFill";
		}
		RenderBackend::getOrCreateSpriteDatablock(section.texture, texture,
			SpriteQuad::FILTER_BILINEAR, SpriteQuad::ADDRESS_CLAMP);
		outTextured = true;
		return SpriteQuad::samplerName(section.texture,
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
			built.datablock = resolveVectorSectionDatablock(source,
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
			this->mesh->begin(built.datablock, Ogre::OT_TRIANGLE_LIST);
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
		this->mesh->setRenderQueueGroup(
			RenderBackend::renderQueueForZOrder(this->zOrder));
		if(this->mesh->isStatic())
		{
			// a mesh on a static node has a frozen AABB - re-snapshot after
			// a rebuild so a legitimate late geometry edit culls correctly
			this->creator->notifyStaticAabbDirty(this->mesh);
		}
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
		// beginUpdate reuses the VaoManager-backed buffers when the counts match:
		// re-emit the moved vertices and the cached topology
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
		if(this->mesh->isStatic())
		{
			this->creator->notifyStaticAabbDirty(this->mesh);
		}
	}
	//---------------------------------------------------------
	VectorMesh::VectorMesh()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	VectorMesh::~VectorMesh()
	{
		// late destruction guard, same rule as SpriteBatch/RenderNode
		if(this->mImpl->mesh && RenderBackend::system())
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
		// align the movable's mobility with the target node (@see MeshInstance)
		if(RenderBackend::nodeIsStatic(node) != this->mImpl->mesh->isStatic())
		{
			this->mImpl->mesh->setStatic(RenderBackend::nodeIsStatic(node));
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->mesh);
		if(this->mImpl->mesh->isStatic())
		{
			this->mImpl->creator->notifyStaticAabbDirty(this->mImpl->mesh);
		}
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
