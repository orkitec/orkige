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
//! @remarks REAL at B1: root node, node factory, camera factory,
//! hemisphere ambient. Stubs (B2, WP-A2.2/A2.3): mesh instances
//! (importV1 path), sprite quads (HlmsUnlit datablocks), lights,
//! the cube-mesh service and ray queries.

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>

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
		(void)meshName;
		RenderBackend::notImplementedOnce("RenderWorld::createMeshInstance");
		return optr<MeshInstance>();
	}
	//---------------------------------------------------------
	optr<SpriteQuad> RenderWorld::createSpriteQuad(String const & textureName)
	{
		(void)textureName;
		RenderBackend::notImplementedOnce("RenderWorld::createSpriteQuad");
		return optr<SpriteQuad>();
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
		RenderBackend::notImplementedOnce("RenderWorld::createLight");
		return optr<RenderLight>();
	}
	//---------------------------------------------------------
	void RenderWorld::createVertexColourCubeMesh(String const & meshName,
		Real halfExtent)
	{
		(void)meshName; (void)halfExtent;
		RenderBackend::notImplementedOnce(
			"RenderWorld::createVertexColourCubeMesh");
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
		(void)ray; (void)queryMask;
		RenderBackend::notImplementedOnce("RenderWorld::queryRay");
		return std::vector<RayQueryHit>();
	}
}
