/********************************************************************
	created:	Friday 2026/07/25 at 12:00
	filename: 	LineMeshNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file LineMeshNext.cpp
//! @brief Ogre-Next implementation of the LineMesh facade
//! @remarks The 3D line sibling of VectorMeshNext: one v2 Ogre::ManualObject
//! (SCENE_DYNAMIC) rebuilt from the owner's CPU vertex array as a single
//! OT_LINE_STRIP / OT_LINE_LIST section, in the DEFAULT 3D item render queue
//! (NOT the sprite 2D pass). It binds the shared unlit vertex-colour
//! "VertexColour" HlmsUnlit datablock (depth-tested - the cube/grid look) or a
//! "VertexColourOverlay" sibling (depth check + write off), so line colour lives
//! in the vertex data. Casts no shadow, never picked.

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreManualObject2.h>
#include <OgreRoot.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsUnlit.h>
#include <OgreHlmsUnlitDatablock.h>

namespace Orkige
{
	//---------------------------------------------------------
	Ogre::HlmsDatablock* RenderBackend::getOrCreateLineDatablock(bool depthTest)
	{
		// the depth-tested variant IS the shared cube/grid "VertexColour"
		// unlit vertex-colour datablock (opaque, depth checked+written)
		if(depthTest)
		{
			return RenderBackend::getOrCreateVertexColourUnlitDatablock(
				"VertexColour", NULL);
		}
		Ogre::HlmsManager* hlmsManager =
			RenderBackend::ogreRoot()->getHlmsManager();
		const String name = "VertexColourOverlay";
		if(Ogre::HlmsDatablock* existing =
			hlmsManager->getDatablockNoDefault(name))
		{
			return existing;
		}
		// the overlay recipe: the same unlit vertex-colour look with a macroblock
		// that neither checks nor writes depth, so the lines draw on top
		Ogre::HlmsUnlit* unlit = static_cast<Ogre::HlmsUnlit*>(
			hlmsManager->getHlms(Ogre::HLMS_UNLIT));
		Ogre::HlmsMacroblock macroblock;
		macroblock.mDepthCheck = false;
		macroblock.mDepthWrite = false;
		Ogre::HlmsUnlitDatablock* datablock =
			static_cast<Ogre::HlmsUnlitDatablock*>(unlit->createDatablock(
				name, name, macroblock, Ogre::HlmsBlendblock(),
				Ogre::HlmsParamVec()));
		// dynamic lines + debug/editor overlays are the 2D/overlay tier - never
		// wireframed (line lists ignore polygon mode anyway, and the editor's own
		// grid/gizmo overlays must stay solid while the Scene wireframe is armed)
		RenderBackend::registerContentDatablock(datablock,
			RenderBackend::DT_UI);
		return datablock;
	}
	//---------------------------------------------------------
	optr<LineMesh> RenderBackend::createLineMesh(Ogre::SceneManager* sceneManager)
	{
		oAssert(sceneManager);
		optr<LineMesh> handle(new LineMesh());
		RenderBackend::getOrCreateLineDatablock(true);
		handle->mImpl->creator = sceneManager;
		handle->mImpl->mesh = sceneManager->createManualObject(Ogre::SCENE_DYNAMIC);
		handle->mImpl->mesh->setName(
			RenderBackend::generateName("RenderFacade/LineMesh"));
		handle->mImpl->mesh->setQueryFlags(0);		// never picked
		handle->mImpl->mesh->setCastShadows(false);	// 3D lines throw no shadow
		// 3D scene content: the default item queue the scene pass draws
		handle->mImpl->mesh->setRenderQueueGroup(
			RenderBackend::DEFAULT_ITEM_RENDER_QUEUE);
		return handle;
	}
	//---------------------------------------------------------
	void LineMesh::Impl::rebuild(LineMesh::Vertex const * vertices,
		std::size_t count, LineMesh::Topology topo)
	{
		oAssert(this->mesh);
		this->mesh->clear();
		this->vertexCount = 0;
		this->topology = topo;
		if(topo == LineMesh::TOPOLOGY_SEGMENTS)
		{
			count -= (count % 2);	// drop a dangling half-segment
		}
		if(vertices == NULL || count < 2)
		{
			return;	// geometry-free (a valid empty state)
		}
		const String datablock = this->depthTest
			? "VertexColour" : "VertexColourOverlay";
		const Ogre::OperationType op = topo == LineMesh::TOPOLOGY_SEGMENTS
			? Ogre::OT_LINE_LIST : Ogre::OT_LINE_STRIP;
		this->mesh->estimateVertexCount(count);
		this->mesh->estimateIndexCount(count);
		this->mesh->begin(datablock, op);
		for(std::size_t each = 0; each < count; ++each)
		{
			this->mesh->position(vertices[each].position);
			this->mesh->colour(vertices[each].colour);
			this->mesh->index(static_cast<Ogre::uint32>(each));
		}
		this->mesh->end();
		this->vertexCount = count;
		if(this->mesh->isStatic())
		{
			this->creator->notifyStaticAabbDirty(this->mesh);
		}
	}
	//---------------------------------------------------------
	void LineMesh::Impl::updateVertices(LineMesh::Vertex const * vertices,
		std::size_t count)
	{
		oAssert(this->mesh);
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
			this->mesh->index(static_cast<Ogre::uint32>(each));
		}
		this->mesh->end();
		if(this->mesh->isStatic())
		{
			this->creator->notifyStaticAabbDirty(this->mesh);
		}
	}
	//---------------------------------------------------------
	void LineMesh::Impl::applyDatablock()
	{
		RenderBackend::getOrCreateLineDatablock(this->depthTest);
		if(this->mesh && this->vertexCount > 0)
		{
			this->mesh->setDatablock(0u, this->depthTest
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
		// late destruction guard, same rule as VectorMesh/SpriteBatch/RenderNode
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
	void LineMesh::attachTo(optr<RenderNode> const & node)
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
		this->mImpl->applyDatablock();
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
