/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	RenderWorldNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderWorldNext.cpp
//! @brief Ogre-Next implementation of the RenderWorld facade
//! @remarks root node + all content factories (which live in the
//! per-class TUs/the backend hub), hemisphere ambient, the cube-mesh
//! service (MeshLoaderNext.cpp) and AABB ray picking. queryRay uses
//! Next's DefaultRaySceneQuery - still present in v2, SIMD AABB tests
//! over the entity memory managers (lights/cameras live elsewhere and
//! are never returned), same sort-by-distance contract as classic.

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreSceneQuery.h>

namespace Orkige
{
	//---------------------------------------------------------
	const unsigned int RenderWorld::QUERYFLAG_DEFAULT = 1;
	//---------------------------------------------------------
	RenderWorld::RayQueryHit::RayQueryHit()
		: distance(0)
		, userPointer(NULL)
	{
	}
	//---------------------------------------------------------
	RenderWorld::RenderWorld()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderWorld::~RenderWorld()
	{
		// the scene manager itself is torn down with the Ogre root
		// (RenderBackend::destroyRenderSystem)
		this->mImpl->rootNode.reset();
		delete this->mImpl;
	}
	//---------------------------------------------------------
	optr<RenderNode> RenderWorld::getRootNode() const
	{
		if(!this->mImpl->rootNode)
		{
			this->mImpl->rootNode = RenderBackend::wrapNode(
				this->mImpl->sceneManager->getRootSceneNode(),
				false /*owned*/, optr<RenderNode>());
		}
		return this->mImpl->rootNode;
	}
	//---------------------------------------------------------
	optr<RenderNode> RenderWorld::createNode(String const & name)
	{
		// v2 child nodes are created nameless (SoA allocation) and named
		// after the fact - names are informational on Next
		Ogre::SceneNode* node = this->mImpl->sceneManager->getRootSceneNode()
			->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		node->setName(name.empty()
			? RenderBackend::generateName("OrkigeNode") : name);
		return RenderBackend::wrapNode(node, true, this->getRootNode());
	}
	//---------------------------------------------------------
	optr<MeshInstance> RenderWorld::createMeshInstance(String const & meshName)
	{
		return RenderBackend::createMeshInstance(
			this->mImpl->sceneManager, meshName);
	}
	//---------------------------------------------------------
	optr<SpriteQuad> RenderWorld::createSpriteQuad(String const & textureName)
	{
		return RenderBackend::createSpriteQuad(
			this->mImpl->sceneManager, textureName);
	}
	//---------------------------------------------------------
	optr<RenderCamera> RenderWorld::createCamera(String const & name)
	{
		return RenderBackend::createCamera(this->mImpl->sceneManager,
			name.empty() ? RenderBackend::generateName("OrkigeCamera") : name);
	}
	//---------------------------------------------------------
	optr<RenderLight> RenderWorld::createLight()
	{
		return RenderBackend::createLight(this->mImpl->sceneManager);
	}
	//---------------------------------------------------------
	void RenderWorld::createVertexColourCubeMesh(String const & meshName,
		Real halfExtent)
	{
		RenderBackend::createVertexColourCubeMesh(this->mImpl->sceneManager,
			meshName, halfExtent);
	}
	//---------------------------------------------------------
	void RenderWorld::setAmbientLight(Color const & colour)
	{
		this->mImpl->ambient = colour;
		// facade takes one colour; Next splits ambient into hemispheres -
		// pass it to both (the documented mapping in RenderWorld.h)
		this->mImpl->sceneManager->setAmbientLight(colour, colour,
			Ogre::Vector3::UNIT_Y);
	}
	//---------------------------------------------------------
	Color const & RenderWorld::getAmbientLight() const
	{
		return this->mImpl->ambient;
	}
	//---------------------------------------------------------
	std::vector<RenderWorld::RayQueryHit> RenderWorld::queryRay(
		Ray3 const & ray, unsigned int queryMask) const
	{
		// mirror of the classic backend over v2's DefaultRaySceneQuery
		std::vector<RayQueryHit> hits;
		Ogre::RaySceneQuery* query =
			this->mImpl->sceneManager->createRayQuery(ray, queryMask);
		query->setSortByDistance(true);
		for(Ogre::RaySceneQueryResultEntry const & entry : query->execute())
		{
			if(!entry.movable)
			{
				continue;	// world-fragment hits are not scene content
			}
			Ogre::SceneNode* backendNode = entry.movable->getParentSceneNode();
			RayQueryHit hit;
			hit.distance = entry.distance;
			hit.node = RenderBackend::findNode(backendNode);
			hit.userPointer = RenderBackend::findUserPointerUpwards(backendNode);
			hits.push_back(hit);
		}
		this->mImpl->sceneManager->destroyQuery(query);
		return hits;
	}
}
