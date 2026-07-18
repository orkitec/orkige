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
	optr<SpriteBatch> RenderWorld::createSpriteBatch(String const & textureName,
		SpriteBatch::BlendMode blendMode, SpriteQuad::FilterMode filter,
		SpriteQuad::AddressMode addressing)
	{
		return RenderBackend::createSpriteBatch(
			this->mImpl->sceneManager, textureName, blendMode, filter,
			addressing);
	}
	//---------------------------------------------------------
	optr<VectorMesh> RenderWorld::createVectorMesh()
	{
		return RenderBackend::createVectorMesh(this->mImpl->sceneManager);
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
	optr<RenderDecal> RenderWorld::createDecal()
	{
		return RenderBackend::createDecal(this->mImpl->sceneManager);
	}
	//---------------------------------------------------------
	void RenderWorld::setMaxDecals(unsigned int maxDecals)
	{
		RenderBackend::setMaxDecals(maxDecals);
	}
	//---------------------------------------------------------
	unsigned int RenderWorld::getMaxDecals() const
	{
		return RenderBackend::maxDecals();
	}
	//---------------------------------------------------------
	unsigned int RenderWorld::getVisibleDecalCount() const
	{
		return RenderBackend::visibleDecalCount();
	}
	//---------------------------------------------------------
	void RenderWorld::createVertexColourCubeMesh(String const & meshName,
		Real halfExtent)
	{
		RenderBackend::createVertexColourCubeMesh(this->mImpl->sceneManager,
			meshName, halfExtent);
	}
	//---------------------------------------------------------
	void RenderWorld::createLineListMesh(String const & meshName,
		Vec3 const * points, Color const * colours, size_t pointCount)
	{
		RenderBackend::createVertexColourLineListMesh(
			this->mImpl->sceneManager, meshName, points, colours, pointCount);
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
		return this->mImpl->ambient;
	}
	//---------------------------------------------------------
	void RenderWorld::setAmbientHemisphere(Color const & upperHemisphere,
		Color const & lowerHemisphere)
	{
		this->mImpl->ambient = upperHemisphere;
		this->mImpl->ambientLower = lowerHemisphere;
		// Next carries the native two-colour sky/ground ambient term; the
		// envmapScale slot rides along so an ambient write never resets the
		// image-lighting intensity (@see RenderBackend::applyImageLighting)
		this->mImpl->sceneManager->setAmbientLight(upperHemisphere,
			lowerHemisphere, Ogre::Vector3::UNIT_Y,
			RenderBackend::imageLightingEnvmapScale());
	}
	//---------------------------------------------------------
	Color const & RenderWorld::getAmbientHemisphereUpper() const
	{
		return this->mImpl->ambient;
	}
	//---------------------------------------------------------
	Color const & RenderWorld::getAmbientHemisphereLower() const
	{
		return this->mImpl->ambientLower;
	}
	//---------------------------------------------------------
	void RenderWorld::setShadowQuality(ShadowPreset::Quality quality)
	{
		if(this->mImpl->shadowQuality == quality)
		{
			return;
		}
		this->mImpl->shadowQuality = quality;
		RenderBackend::applyShadowConfig();
	}
	//---------------------------------------------------------
	ShadowPreset::Quality RenderWorld::getShadowQuality() const
	{
		return this->mImpl->shadowQuality;
	}
	//---------------------------------------------------------
	void RenderWorld::setImageLighting(bool enabled, Real intensity)
	{
		this->mImpl->iblEnabled = enabled;
		this->mImpl->iblIntensity = static_cast<float>(intensity);
		RenderBackend::applyImageLighting();
		// the intensity rides the scene envmapScale, which the live
		// atmosphere re-writes per sync - hand it the new value now
		if(this->mImpl->atmosphere.enabled)
		{
			RenderBackend::applyAtmosphere(this->mImpl->atmosphere);
		}
	}
	//---------------------------------------------------------
	bool RenderWorld::getImageLightingEnabled() const
	{
		return this->mImpl->iblEnabled;
	}
	//---------------------------------------------------------
	Real RenderWorld::getImageLightingIntensity() const
	{
		return Real(this->mImpl->iblIntensity);
	}
	//---------------------------------------------------------
	void RenderWorld::setIblQuality(IblPreset::Quality quality)
	{
		if(this->mImpl->iblQuality == quality)
		{
			return;
		}
		this->mImpl->iblQuality = quality;
		RenderBackend::applyImageLighting();
	}
	//---------------------------------------------------------
	IblPreset::Quality RenderWorld::getIblQuality() const
	{
		return this->mImpl->iblQuality;
	}
	//---------------------------------------------------------
	void RenderWorld::setAtmosphere(AtmosphereDesc const & desc)
	{
		this->mImpl->atmosphere = desc;
		RenderBackend::applyAtmosphere(desc);
	}
	//---------------------------------------------------------
	AtmosphereDesc const & RenderWorld::getAtmosphere() const
	{
		return this->mImpl->atmosphere;
	}
	//---------------------------------------------------------
	void RenderWorld::setBloom(BloomDesc const & desc)
	{
		this->mImpl->bloom = desc.sanitised();
		RenderBackend::applyBloomConfig();
	}
	//---------------------------------------------------------
	BloomDesc const & RenderWorld::getBloom() const
	{
		return this->mImpl->bloom;
	}
	//---------------------------------------------------------
	void RenderWorld::setBloomQuality(BloomPreset::Quality quality)
	{
		if(this->mImpl->bloomQuality == quality)
		{
			return;
		}
		this->mImpl->bloomQuality = quality;
		RenderBackend::applyBloomConfig();
	}
	//---------------------------------------------------------
	BloomPreset::Quality RenderWorld::getBloomQuality() const
	{
		return this->mImpl->bloomQuality;
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
