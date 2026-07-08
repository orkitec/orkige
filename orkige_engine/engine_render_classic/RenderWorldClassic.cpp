/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	RenderWorldClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderWorldClassic.cpp
//! @brief classic-OGRE implementation of the RenderWorld facade
//! @remarks wraps the Ogre::SceneManager Engine created (Engine keeps
//! owning it during the A1 migration window)

#include "engine_render_classic/ClassicBackend.h"

#include <algorithm>

namespace Orkige
{
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
		// the scene manager itself stays with Engine (classic bootstrap);
		// dropping the root handle unregisters it (owned=false, so the
		// backend root node is not destroyed)
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
		Ogre::SceneNode* root = this->mImpl->sceneManager->getRootSceneNode();
		Ogre::SceneNode* node = name.empty()
			? root->createChildSceneNode()
			: root->createChildSceneNode(name);
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
		return RenderBackend::createCamera(this->mImpl->sceneManager, name);
	}
	//---------------------------------------------------------
	optr<RenderLight> RenderWorld::createLight()
	{
		return RenderBackend::createLight(this->mImpl->sceneManager);
	}
	//---------------------------------------------------------
	void RenderWorld::setAmbientLight(Color const & colour)
	{
		this->mImpl->sceneManager->setAmbientLight(colour);
	}
	//---------------------------------------------------------
	Color const & RenderWorld::getAmbientLight() const
	{
		return this->mImpl->sceneManager->getAmbientLight();
	}
	//---------------------------------------------------------
	std::vector<RenderWorld::RayQueryHit> RenderWorld::queryRay(
		Ray3 const & ray, unsigned int queryMask) const
	{
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
