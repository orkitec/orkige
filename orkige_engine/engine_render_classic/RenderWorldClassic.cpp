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
#include "engine_util/PrimitiveUtil.h"

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
	optr<SpriteBatch> RenderWorld::createSpriteBatch(String const & textureName,
		SpriteBatch::BlendMode blendMode)
	{
		return RenderBackend::createSpriteBatch(
			this->mImpl->sceneManager, textureName, blendMode);
	}
	//---------------------------------------------------------
	optr<VectorMesh> RenderWorld::createVectorMesh()
	{
		return RenderBackend::createVectorMesh(this->mImpl->sceneManager);
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
	void RenderWorld::createVertexColourCubeMesh(String const & meshName,
		Real halfExtent)
	{
		// one source of truth: the editor's PrimitiveUtil recipe (ManualObject
		// guts stay backend-private per Docs/render-abstraction.md); it also
		// creates the shared unlit "VertexColour" material, both idempotent
		PrimitiveUtil::createVertexColourCubeMesh(this->mImpl->sceneManager,
			meshName, halfExtent);
	}
	//---------------------------------------------------------
	void RenderWorld::createLineListMesh(String const & meshName,
		Vec3 const * points, Color const * colours, size_t pointCount)
	{
		oAssert(!meshName.empty());
		oAssert(points && colours && pointCount >= 2 && pointCount % 2 == 0);
		Ogre::MeshManager & meshManager = Ogre::MeshManager::getSingleton();
		if(meshManager.resourceExists(meshName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			return;	// idempotent, same contract as the cube service
		}
		// same shared unlit vertex-colour look as the cube service
		PrimitiveUtil::createVertexColourMaterial();
		Ogre::ManualObject* lines =
			this->mImpl->sceneManager->createManualObject(meshName + ".manual");
		lines->begin("VertexColour", Ogre::RenderOperation::OT_LINE_LIST);
		for(size_t each = 0; each < pointCount; ++each)
		{
			lines->position(points[each]);
			lines->colour(colours[each]);
		}
		lines->end();
		lines->convertToMesh(meshName);
		this->mImpl->sceneManager->destroyManualObject(lines);
	}
	//---------------------------------------------------------
	void RenderWorld::setAmbientLight(Color const & colour)
	{
		// the flat ambient is the hemisphere term with both colours equal
		this->setAmbientHemisphere(colour, colour);
	}
	//---------------------------------------------------------
	Color const & RenderWorld::getAmbientLight() const
	{
		return this->mImpl->sceneManager->getAmbientLight();
	}
	//---------------------------------------------------------
	void RenderWorld::setAmbientHemisphere(Color const & upperHemisphere,
		Color const & lowerHemisphere)
	{
		// classic has flat ambient only: cache both hemisphere colours for the
		// getters and drive the scene with their average (the honest subset)
		this->mImpl->ambientUpper = upperHemisphere;
		this->mImpl->ambientLower = lowerHemisphere;
		this->mImpl->sceneManager->setAmbientLight(
			(upperHemisphere + lowerHemisphere) * 0.5f);
	}
	//---------------------------------------------------------
	Color const & RenderWorld::getAmbientHemisphereUpper() const
	{
		return this->mImpl->ambientUpper;
	}
	//---------------------------------------------------------
	bool RenderWorld::shadowsSupported()
	{
		// honest "no": the compatibility flavor renders no dynamic shadows
		// (wiring classic texture shadows would duplicate the whole shadow
		// package for the deprecated flavor) - the Ogre-Next flavor does
		return false;
	}
	//---------------------------------------------------------
	void RenderWorld::setShadowQuality(ShadowPreset::Quality quality)
	{
		if(quality == this->mImpl->shadowQuality)
		{
			return;
		}
		// the knob is ACCEPTED (round-trips through the getter, so quality
		// settings survive on a scene authored against the next flavor) but
		// renders nothing here; say so ONCE per process, then stay silent
		this->mImpl->shadowQuality = quality;
		if(quality != ShadowPreset::SQ_OFF)
		{
			static bool warnedOnce = false;
			if(!warnedOnce)
			{
				warnedOnce = true;
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige classic backend: dynamic shadows are not supported "
					"on this render backend - the quality knob is recorded but "
					"no shadow maps render on this flavor");
			}
		}
	}
	//---------------------------------------------------------
	ShadowPreset::Quality RenderWorld::getShadowQuality() const
	{
		return this->mImpl->shadowQuality;
	}
	//---------------------------------------------------------
	Color const & RenderWorld::getAmbientHemisphereLower() const
	{
		return this->mImpl->ambientLower;
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
