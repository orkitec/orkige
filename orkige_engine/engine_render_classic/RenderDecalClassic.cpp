/********************************************************************
	created:	Friday 2026/07/18 at 00:00
	filename: 	RenderDecalClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderDecalClassic.cpp
//! @brief classic-OGRE implementation of the RenderDecal facade - the honest
//! subset: a surface-aligned textured QUAD (Ogre::ManualObject in the node's
//! local XZ plane) floating just above the surface, depth-biased against
//! z-fighting. Classic has no deferred/projective decal path, so this is a flat
//! mark that does NOT wrap over uneven geometry (@see the next flavor's true
//! projected Decal). The world's visible-decal budget hides the oldest over the
//! cap.

#include "engine_render_classic/ClassicBackend.h"
#include <core_debug/DebugMacros.h>

#include <algorithm>
#include <vector>

namespace Orkige
{
	namespace
	{
		//! the world's visible-decal budget + the creation-order registry it
		//! enforces (front = oldest). A generous desktop default until the app
		//! host's r.maxDecals cvar sets the project value.
		unsigned int			gMaxDecals = 256u;
		std::vector<RenderDecal*>	gDecals;
	}
	//---------------------------------------------------------
	void RenderBackend::enforceDecalBudget()
	{
		// newest gMaxDecals decals get budgetVisible, the rest are hidden
		const size_t count = gDecals.size();
		for(size_t each = 0; each < count; ++each)
		{
			const bool withinBudget =
				(count - each) <= static_cast<size_t>(gMaxDecals);
			RenderDecal::Impl* impl = gDecals[each]->mImpl;
			if(impl->budgetVisible != withinBudget)
			{
				impl->budgetVisible = withinBudget;
				impl->applyVisibility();
			}
		}
	}
	//---------------------------------------------------------
	void RenderBackend::registerDecal(RenderDecal* decal)
	{
		gDecals.push_back(decal);
		RenderBackend::enforceDecalBudget();
	}
	//---------------------------------------------------------
	void RenderBackend::unregisterDecal(RenderDecal* decal)
	{
		gDecals.erase(std::remove(gDecals.begin(), gDecals.end(), decal),
			gDecals.end());
		RenderBackend::enforceDecalBudget();
	}
	//---------------------------------------------------------
	void RenderBackend::setMaxDecals(unsigned int maxDecals)
	{
		gMaxDecals = maxDecals;
		RenderBackend::enforceDecalBudget();
	}
	//---------------------------------------------------------
	unsigned int RenderBackend::maxDecals()
	{
		return gMaxDecals;
	}
	//---------------------------------------------------------
	unsigned int RenderBackend::visibleDecalCount()
	{
		unsigned int visible = 0;
		for(RenderDecal* decal : gDecals)
		{
			RenderDecal::Impl* impl = decal->mImpl;
			if(impl->budgetVisible && impl->userVisible)
			{
				++visible;
			}
		}
		return visible;
	}
	//---------------------------------------------------------
	void RenderBackend::resetDecalState()
	{
		gDecals.clear();
		gMaxDecals = 256u;
	}
	//---------------------------------------------------------
	String RenderBackend::getOrCreateDecalMaterial(String const & textureName,
		Ogre::TexturePtr & outTexture)
	{
		if(textureName.empty())
		{
			return String();
		}
		Ogre::TexturePtr texture;
		try
		{
			// cooked-payload fallback (foo.png -> foo.dds/.ktx in exports)
			texture = Ogre::TextureManager::getSingleton().load(
				resolveTextureResourceName(textureName),
				Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
		}
		catch(Ogre::Exception const & e)
		{
			oDebugError("engine", 0, "RenderDecal: decal texture '"
				<< textureName << "' failed to load: " << e.getDescription());
			return String();
		}
		if(!texture)
		{
			oDebugError("engine", 0, "RenderDecal: decal texture '"
				<< textureName << "' not found");
			return String();
		}
		outTexture = texture;
		const String materialName = "Decal/" + texture->getName();
		Ogre::MaterialManager & materialManager =
			Ogre::MaterialManager::getSingleton();
		if(materialManager.resourceExists(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			return materialName;
		}
		Ogre::MaterialPtr material = materialManager.create(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		material->setReceiveShadows(false);
		Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
		pass->setLightingEnabled(false);
		// the fade opacity rides the vertex alpha (per-vertex, smooth)
		pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
		pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
		// depth-CHECK on (geometry in front occludes the mark) but no depth
		// WRITE, plus a DEPTH BIAS pulling the quad toward the camera so it
		// floats above a coplanar surface without z-fighting
		pass->setDepthWriteEnabled(false);
		pass->setDepthBias(4.0f, 1.0f);
		pass->setCullingMode(Ogre::CULL_NONE);
		Ogre::TextureUnitState* textureUnit = pass->createTextureUnitState();
		textureUnit->setTexture(texture);
		textureUnit->setTextureAddressingMode(
			Ogre::TextureUnitState::TAM_CLAMP);
		textureUnit->setTextureFiltering(Ogre::FO_LINEAR, Ogre::FO_LINEAR,
			Ogre::FO_NONE);
		return materialName;
	}
	//---------------------------------------------------------
	void RenderDecal::Impl::rebuild()
	{
		oAssert(this->quad);
		this->quad->clear();
		if(this->materialName.empty())
		{
			return;	// no texture set yet - leave the quad geometry-free
		}
		const float halfWidth = this->sizeX * 0.5f;
		const float halfDepth = this->sizeZ * 0.5f;
		// a quad in the LOCAL XZ plane (y = 0) facing +Y - the surface-aligned
		// mark; the owning node places/orients it at the surface
		const Ogre::Vector3 corners[4] = {
			{ -halfWidth, 0.0f, -halfDepth },
			{  halfWidth, 0.0f, -halfDepth },
			{  halfWidth, 0.0f,  halfDepth },
			{ -halfWidth, 0.0f,  halfDepth },
		};
		const Ogre::Vector2 uv[4] = {
			{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f },
		};
		const Ogre::ColourValue colour(1.0f, 1.0f, 1.0f,
			std::max(0.0f, std::min(this->opacity, 1.0f)));
		this->quad->estimateVertexCount(4);
		this->quad->estimateIndexCount(6);
		this->quad->begin(this->materialName,
			Ogre::RenderOperation::OT_TRIANGLE_LIST);
		for(int each = 0; each < 4; ++each)
		{
			this->quad->position(corners[each]);
			this->quad->normal(Ogre::Vector3::UNIT_Y);
			this->quad->colour(colour);
			this->quad->textureCoord(uv[each]);
		}
		// two triangles; CULL_NONE so winding is immaterial
		this->quad->triangle(0, 2, 1);
		this->quad->triangle(0, 3, 2);
		this->quad->end();
	}
	//---------------------------------------------------------
	void RenderDecal::Impl::applyVisibility()
	{
		if(this->quad)
		{
			this->quad->setVisible(this->userVisible && this->budgetVisible);
		}
	}
	//---------------------------------------------------------
	optr<RenderDecal> RenderBackend::createDecal(Ogre::SceneManager* sceneManager)
	{
		oAssert(sceneManager);
		optr<RenderDecal> handle(new RenderDecal());
		handle->mImpl->creator = sceneManager;
		handle->mImpl->quad = sceneManager->createManualObject(
			RenderBackend::generateName("RenderFacade/Decal"));
		// decals are not pick targets and never join the shadow pass
		handle->mImpl->quad->setQueryFlags(0);
		handle->mImpl->quad->setCastShadows(false);
		RenderBackend::registerDecal(handle.get());
		handle->mImpl->applyVisibility();
		return handle;
	}
	//---------------------------------------------------------
	RenderDecal::RenderDecal()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderDecal::~RenderDecal()
	{
		if(RenderBackend::system())
		{
			RenderBackend::unregisterDecal(this);
			if(this->mImpl->quad)
			{
				if(this->mImpl->quad->isAttached())
				{
					this->mImpl->quad->detachFromParent();
				}
				this->mImpl->creator->destroyManualObject(this->mImpl->quad);
			}
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void RenderDecal::attachTo(optr<RenderNode> const & node)
	{
		oAssert(node);
		if(this->mImpl->quad->isAttached())
		{
			this->mImpl->quad->detachFromParent();
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->quad);
		this->mImpl->attachedTo = node;
	}
	//---------------------------------------------------------
	void RenderDecal::detach()
	{
		if(this->mImpl->quad->isAttached())
		{
			this->mImpl->quad->detachFromParent();
		}
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	void RenderDecal::setDiffuseTexture(String const & textureName)
	{
		this->mImpl->textureName = textureName;
		this->mImpl->materialName =
			RenderBackend::getOrCreateDecalMaterial(textureName,
				this->mImpl->texture);
		// a missing texture leaves the material name empty (rebuild renders no
		// geometry - an honest empty mark)
		this->mImpl->rebuild();
	}
	//---------------------------------------------------------
	void RenderDecal::setSize(Real width, Real depth, Real /*projectionDepth*/)
	{
		// classic aligns a flat quad; the projection-box depth (next only) is
		// ignored here
		this->mImpl->sizeX = width;
		this->mImpl->sizeZ = depth;
		this->mImpl->rebuild();
	}
	//---------------------------------------------------------
	void RenderDecal::setOpacity(Real opacity)
	{
		// classic dims smoothly - the opacity rides the quad's vertex alpha
		this->mImpl->opacity = opacity;
		this->mImpl->rebuild();
	}
	//---------------------------------------------------------
	void RenderDecal::setVisible(bool visible)
	{
		if(this->mImpl->userVisible != visible)
		{
			this->mImpl->userVisible = visible;
			this->mImpl->applyVisibility();
		}
	}
	//---------------------------------------------------------
	bool RenderDecal::isVisible() const
	{
		return this->mImpl->userVisible;
	}
}
