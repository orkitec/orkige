/********************************************************************
	created:	Friday 2026/07/18 at 00:00
	filename: 	RenderDecalNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderDecalNext.cpp
//! @brief Ogre-Next implementation of the RenderDecal facade - a TRUE
//! projected decal (HlmsPbs forward-clustered Decal). Every decal-diffuse
//! texture is pooled into ONE fixed-size array so the SceneManager can take a
//! single master (setDecalsDiffuse) and each decal references a slice; the
//! world's visible-decal budget (RenderWorld::setMaxDecals) hides the oldest
//! decals over the cap.

#include "engine_render_next/NextBackend.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreDecal.h>
#include <OgreTextureGpu.h>
#include <OgreTextureGpuManager.h>
#include <OgreTextureFilters.h>
#include <OgrePixelFormatGpu.h>
#include <OgreImage2.h>
#include <OgreResourceGroupManager.h>
#include <OgreLogManager.h>
#include <OgreRoot.h>
#include <OgreRenderSystem.h>
#include <OgreVector2.h>

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
		//! true once the scene's decal-diffuse array is registered (the first
		//! loaded decal texture's auto-batch pool becomes the array)
		bool					gDecalDiffuseRegistered = false;
	}
	//---------------------------------------------------------
	void RenderBackend::enforceDecalBudget()
	{
		// newest gMaxDecals decals get budgetVisible, the rest are hidden
		const size_t count = gDecals.size();
		for(size_t each = 0; each < count; ++each)
		{
			// visible when within the newest gMaxDecals of the list
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
	void RenderDecal::Impl::applyVisibility()
	{
		// Ogre-Next forbids setVisible on an UNATTACHED movable; the flags are
		// stored and re-applied by attachTo once the decal hangs off a node
		if(!this->decal || !this->decal->isAttached())
		{
			return;
		}
		// three gates ANDed: the owner/component, the world budget, and the
		// opacity threshold (next has no per-decal alpha uniform)
		this->decal->setVisible(this->userVisible && this->budgetVisible
			&& this->opacityVisible);
	}
	//---------------------------------------------------------
	Ogre::TextureGpu* RenderBackend::loadDecalDiffuseTexture(
		String const & rawName)
	{
		if(rawName.empty() || !RenderBackend::ogreRoot())
		{
			return NULL;
		}
		Ogre::TextureGpuManager* textureManager = RenderBackend::ogreRoot()
			->getRenderSystem()->getTextureGpuManager();
		const String textureName = resolveTextureResourceName(rawName);
		// already pooled (a second decal reusing the same mark texture)
		if(Ogre::TextureGpu* existing =
			textureManager->findTextureNoThrow(textureName))
		{
			return existing;
		}
		try
		{
			Ogre::ResourceGroupManager & resourceGroups =
				Ogre::ResourceGroupManager::getSingleton();
			const String group =
				resourceGroups.findGroupContainingResource(textureName);
			// decode-probe on this (main) thread first (same rule as
			// loadTexture2D) AND read the resolution: a decal texture must match
			// the fixed pool size, else it would land in its own pool whose
			// master the scene never samples - reject it up front.
			{
				Ogre::DataStreamPtr probe =
					resourceGroups.openResource(textureName, group);
				Ogre::Image2 probeImage;
				probeImage.load2(probe, textureName);
				if(probeImage.getWidth() != DECAL_TEXTURE_SIZE ||
					probeImage.getHeight() != DECAL_TEXTURE_SIZE)
				{
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige next backend: decal texture '" + textureName +
						"' is not " + std::to_string(DECAL_TEXTURE_SIZE) + "x" +
						std::to_string(DECAL_TEXTURE_SIZE) +
						" - decals pool at one resolution, the mark will not "
						"render (author decal textures at the pool size)");
					return NULL;
				}
			}
			// AutomaticBatching auto-pools textures by (resolution, format), so
			// every decal texture at DECAL_TEXTURE_SIZE + sRGB shares ONE pool
			// whose master is the decal-diffuse array (the size guard above keeps
			// them all in it). sRGB so the mark colour reads correctly; default
			// mipmaps for clean minification.
			Ogre::TextureGpu* texture = textureManager->createOrRetrieveTexture(
				textureName, textureName, Ogre::GpuPageOutStrategy::Discard,
				Ogre::TextureFlags::AutomaticBatching
					| Ogre::TextureFlags::PrefersLoadingFromFileAsSRGB,
				Ogre::TextureTypes::Type2D, group,
				Ogre::TextureFilter::TypeGenerateDefaultMipmaps);
			// register the scene's decal-diffuse array ONCE: pass the FIRST decal
			// texture (an AutomaticBatching texture exposes its pool) - NOT the
			// pool-owner master, which has no system-ram copy and trips
			// setDecalsDiffuse's unconditional schedule-to-Resident. setDecalsDiffuse
			// does that ONE schedule; a subsequent texture self-schedules.
			Ogre::SceneManager* sceneManager = worldSceneManager();
			if(sceneManager && !gDecalDiffuseRegistered)
			{
				sceneManager->setDecalsDiffuse(texture);
				gDecalDiffuseRegistered = true;
			}
			else if(texture->getResidencyStatus() == Ogre::GpuResidency::OnStorage)
			{
				texture->scheduleTransitionTo(Ogre::GpuResidency::Resident);
			}
			// force residency deterministically (a file texture has a system-ram
			// copy, so this never hits the pool-owner assert); the slice is then
			// assigned and the Decal auto path derives it
			texture->waitForData();
			return texture;
		}
		catch(Ogre::Exception const & e)
		{
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: decal texture '" + textureName +
				"' failed to load: " + e.getDescription());
			return NULL;
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
			if(impl->budgetVisible && impl->userVisible && impl->opacityVisible)
			{
				++visible;
			}
		}
		return visible;
	}
	//---------------------------------------------------------
	void RenderBackend::resetDecalState()
	{
		// the pool texture + decals die with the texture manager / scene manager;
		// only the facade-side registry statics are dropped here
		gDecals.clear();
		gDecalDiffuseRegistered = false;
		gMaxDecals = 256u;
	}
	//---------------------------------------------------------
	optr<RenderDecal> RenderBackend::createDecal(Ogre::SceneManager* sceneManager)
	{
		oAssert(sceneManager);
		Ogre::Decal* decal = sceneManager->createDecal();
		optr<RenderDecal> handle(new RenderDecal());
		handle->mImpl->decal = decal;
		handle->mImpl->creator = sceneManager;
		// a fresh decal joins the world budget in creation order (may hide the
		// oldest if it pushes the count past the cap)
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
		// leave the budget registry BEFORE the backend object dies
		if(RenderBackend::system())
		{
			RenderBackend::unregisterDecal(this);
			if(this->mImpl->decal)
			{
				if(this->mImpl->decal->isAttached())
				{
					this->mImpl->decal->detachFromParent();
				}
				this->mImpl->creator->destroyDecal(this->mImpl->decal);
			}
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void RenderDecal::attachTo(optr<RenderNode> const & node)
	{
		oAssert(node);
		if(this->mImpl->decal->isAttached())
		{
			this->mImpl->decal->detachFromParent();
		}
		RenderBackend::sceneNode(node)->attachObject(this->mImpl->decal);
		this->mImpl->attachedTo = node;
		// now attached: apply the visibility gates that were stored while detached
		this->mImpl->applyVisibility();
	}
	//---------------------------------------------------------
	void RenderDecal::detach()
	{
		if(this->mImpl->decal->isAttached())
		{
			this->mImpl->decal->detachFromParent();
		}
		this->mImpl->attachedTo.reset();
	}
	//---------------------------------------------------------
	void RenderDecal::setDiffuseTexture(String const & textureName)
	{
		this->mImpl->textureName = textureName;
		Ogre::TextureGpu* texture =
			RenderBackend::loadDecalDiffuseTexture(textureName);
		this->mImpl->texture = texture;
		// a NULL (missing/mismatched) texture leaves the decal with no mark; the
		// auto-slice path derives the array slice from the pooled texture
		this->mImpl->decal->setDiffuseTexture(texture);
	}
	//---------------------------------------------------------
	void RenderDecal::setSize(Real width, Real depth, Real projectionDepth)
	{
		// the projected footprint on the surface + the box depth reaching into
		// geometry along -Y (Decal::setRectSize scales the parent node)
		this->mImpl->decal->setRectSize(Ogre::Vector2(width, depth),
			projectionDepth);
	}
	//---------------------------------------------------------
	void RenderDecal::setOpacity(Real opacity)
	{
		// next has no per-decal alpha uniform: treat opacity as a visibility
		// threshold (> 0 shows the full-alpha mark, <= 0 hides it)
		const bool visible = opacity > Real(0);
		if(this->mImpl->opacityVisible != visible)
		{
			this->mImpl->opacityVisible = visible;
			this->mImpl->applyVisibility();
		}
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
