/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	RenderSystemClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file RenderSystemClassic.cpp
//! @brief classic-OGRE implementation of the RenderSystem facade
//! @remarks RenderSystem WRAPS the existing Engine (the classic
//! bootstrapper keeps owning root/window/config plumbing - kept working
//! because everything still calls it); the facade adds
//! the services apps used to reach raw Ogre for (Docs/render-abstraction.md
//! bucket B). Engine::setup creates this via RenderBackend::createRenderSystem.

#include "engine_render_classic/ClassicBackend.h"
#include "engine_graphic/Engine.h"
#include <core_debug/DebugMacros.h>

#include <algorithm>
#include <set>
#include <unordered_map>

namespace Orkige
{
	namespace
	{
		//! per water-material shimmer scroll speed (UV units per second), so the
		//! per-frame setWaterMaterialTime can drive the overlay UV. Keyed by the
		//! material name; a stale entry (its material dropped on a project
		//! switch) is harmless - the scroll looks the name up and no-ops.
		std::unordered_map<String, float> gWaterScrollSpeeds;
	}
	//---------------------------------------------------------
	RenderSystem::FrameStats::FrameStats()
		: lastFPS(0.0f)
		, avgFPS(0.0f)
		, bestFPS(0.0f)
		, worstFPS(0.0f)
		, triangleCount(0)
		, batchCount(0)
		, textureMemoryBytes(0)
	{
	}
	//---------------------------------------------------------
	RenderSystem::RenderSystem()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	RenderSystem::~RenderSystem()
	{
		this->mImpl->windowCamera.reset();
		delete this->mImpl->world;
		delete this->mImpl;
	}
	//---------------------------------------------------------
	RenderSystem* RenderSystem::get()
	{
		return RenderBackend::system();
	}
	//---------------------------------------------------------
	bool RenderSystem::renderOneFrame()
	{
		return this->mImpl->engine->renderOneFrame();
	}
	//---------------------------------------------------------
	void RenderSystem::showCameraOnWindow(optr<RenderCamera> const & camera)
	{
		Ogre::Camera* backendCamera = RenderBackend::ogreCamera(camera);
		oAssert(backendCamera);
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		// "replaces the previous window camera": one full-window viewport
		// (single-window/single-viewport is frozen by design).
		// NOTE for the migration window: this drops a viewport created via
		// Engine::createDefaultCameraAndViewport - an app uses the Engine
		// path OR the facade path for the window camera, never both.
		window->removeAllViewports();
		Ogre::Viewport* viewport = window->addViewport(backendCamera);
		viewport->setBackgroundColour(this->mImpl->windowBackground);
		viewport->setShadowsEnabled(true);
		RenderBackend::applyRTSSScheme(viewport);
		backendCamera->setAspectRatio(
			Ogre::Real(viewport->getActualWidth()) /
			Ogre::Real(viewport->getActualHeight()));
		this->mImpl->windowCamera = camera;
		this->mImpl->uiOnlyWindow = false;
	}
	//---------------------------------------------------------
	void RenderSystem::showUIOnlyWindow()
	{
		// the editor-shell mode: the window carries one full-window viewport
		// whose visibility mask hides ALL scene content - only the clear
		// colour and the DrawLayer2D composition (a RenderQueueListener,
		// mask-independent) reach the screen. A viewport needs a camera, so
		// an internal one feeds it; it never sees content and is never
		// handed out (getWindowCamera answers NULL in this mode).
		Ogre::SceneManager* sceneManager =
			this->mImpl->engine->getSceneManager();
		if(!this->mImpl->uiOnlyCamera)
		{
			this->mImpl->uiOnlyCamera = sceneManager->createCamera(
				RenderBackend::generateName("Orkige/UIOnlyCamera"));
			sceneManager->getRootSceneNode()->createChildSceneNode()
				->attachObject(this->mImpl->uiOnlyCamera);
		}
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		window->removeAllViewports();
		Ogre::Viewport* viewport =
			window->addViewport(this->mImpl->uiOnlyCamera);
		viewport->setBackgroundColour(this->mImpl->windowBackground);
		viewport->setVisibilityMask(0);	// 2D layers only - no scene objects
		viewport->setShadowsEnabled(false);
		RenderBackend::applyRTSSScheme(viewport);
		this->mImpl->windowCamera.reset();
		this->mImpl->engineWindowCamera.reset();
		this->mImpl->uiOnlyWindow = true;
	}
	//---------------------------------------------------------
	optr<RenderCamera> RenderSystem::getWindowCamera() const
	{
		if(this->mImpl->uiOnlyWindow)
		{
			// UI-only mode shows no scene camera by contract (the internal
			// viewport camera is plumbing, not scene surface)
			return optr<RenderCamera>();
		}
		if(this->mImpl->windowCamera)
		{
			return this->mImpl->windowCamera;	// the facade path
		}
		// the Engine path (every app): wrap the camera the
		// window viewport shows - non-owning, Engine keeps destroying it
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		if(window->getNumViewports() == 0)
		{
			return optr<RenderCamera>();	// no camera shown yet
		}
		Ogre::Camera* backendCamera = window->getViewport(0)->getCamera();
		if(!backendCamera)
		{
			return optr<RenderCamera>();
		}
		if(RenderBackend::ogreCamera(this->mImpl->engineWindowCamera)
			!= backendCamera)
		{
			this->mImpl->engineWindowCamera =
				RenderBackend::wrapCamera(backendCamera, false /*owned*/);
		}
		return this->mImpl->engineWindowCamera;
	}
	//---------------------------------------------------------
	void RenderSystem::setWindowBackgroundColour(Color const & colour)
	{
		this->mImpl->windowBackground = colour;
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		if(window->getNumViewports() > 0)
		{
			window->getViewport(0)->setBackgroundColour(colour);
		}
	}
	//---------------------------------------------------------
	void RenderSystem::getWindowSize(unsigned int & outWidth,
		unsigned int & outHeight) const
	{
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		if(window->getNumViewports() > 0)
		{
			outWidth = static_cast<unsigned int>(
				window->getViewport(0)->getActualWidth());
			outHeight = static_cast<unsigned int>(
				window->getViewport(0)->getActualHeight());
			return;
		}
		outWidth = window->getWidth();
		outHeight = window->getHeight();
	}
	//---------------------------------------------------------
	void RenderSystem::notifyWindowResized()
	{
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		window->windowMovedOrResized();
		// keep the shown camera's projection in step with the new drawable
		Ogre::Camera* backendCamera =
			RenderBackend::ogreCamera(this->mImpl->windowCamera);
		if(backendCamera && window->getNumViewports() > 0)
		{
			Ogre::Viewport* viewport = window->getViewport(0);
			backendCamera->setAspectRatio(
				Ogre::Real(viewport->getActualWidth()) /
				Ogre::Real(viewport->getActualHeight()));
		}
	}
	//---------------------------------------------------------
	void RenderSystem::saveWindowContents(String const & fileName) const
	{
		this->mImpl->engine->getRenderWindow(0)->writeContentsToFile(fileName);
	}
	//---------------------------------------------------------
	optr<RenderTexture> RenderSystem::createRenderTexture(String const & name,
		unsigned int width, unsigned int height)
	{
		return RenderBackend::createRenderTexture(name, width, height);
	}
	//---------------------------------------------------------
	optr<DrawLayer2D> RenderSystem::createDrawLayer2D(int zOrder)
	{
		return RenderBackend::createDrawLayer2D(zOrder);
	}
	//---------------------------------------------------------
	bool RenderSystem::getTextureSize(String const & textureName,
		unsigned int & outWidth, unsigned int & outHeight) const
	{
		outWidth = outHeight = 0;
		// resolve through EVERY resource group, same rule as sprite/2D
		// batch textures: engine media and project assets both work by
		// plain file name
		Ogre::TexturePtr texture;
		try
		{
			texture = Ogre::TextureManager::getSingleton().load(textureName,
				Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
		}
		catch(Ogre::Exception const & e)
		{
			oDebugError("engine", 0, "RenderSystem: texture '"
				<< textureName << "' failed to load: " << e.getDescription());
			return false;
		}
		if(!texture)
		{
			oDebugError("engine", 0, "RenderSystem: texture '"
				<< textureName << "' not found");
			return false;
		}
		outWidth = static_cast<unsigned int>(texture->getWidth());
		outHeight = static_cast<unsigned int>(texture->getHeight());
		return true;
	}
	//---------------------------------------------------------
	bool RenderSystem::createTexture2D(String const & name,
		unsigned char const * rgbaPixels,
		unsigned int width, unsigned int height)
	{
		if(name.empty() || !rgbaPixels || width == 0 || height == 0)
		{
			oDebugError("engine", 0, "RenderSystem::createTexture2D('" << name
				<< "'): refused (empty name/pixels/size)");
			return false;
		}
		Ogre::TexturePtr texture = Ogre::TextureManager::getSingleton()
			.getByName(name,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		if(texture && (texture->getWidth() != width ||
			texture->getHeight() != height))
		{
			// replace-by-recreate: a size change cannot blit in place; the
			// cached 2D-layer material would keep the dead incarnation
			Ogre::TextureManager::getSingleton().remove(texture);
			texture.reset();
			RenderBackend::invalidateDrawLayer2DTexture(name);
		}
		if(!texture)
		{
			// no mipmaps: 2D-layer batches sample point-filtered at 1:1
			texture = Ogre::TextureManager::getSingleton().createManual(
				name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
				Ogre::TEX_TYPE_2D, width, height, 0 /*numMipmaps*/,
				Ogre::PF_BYTE_RGBA, Ogre::TU_DEFAULT);
		}
		const Ogre::PixelBox source(width, height, 1, Ogre::PF_BYTE_RGBA,
			const_cast<unsigned char*>(rgbaPixels));
		texture->getBuffer()->blitFromMemory(source);
		return true;
	}
	//---------------------------------------------------------
	void RenderSystem::destroyTexture2D(String const & name)
	{
		Ogre::TexturePtr texture = Ogre::TextureManager::getSingleton()
			.getByName(name,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		if(!texture)
		{
			return;	// idempotent
		}
		// the cached 2D-layer material would keep the texture (and its GPU
		// memory) alive past the render system - drop it first
		RenderBackend::invalidateDrawLayer2DTexture(name);
		Ogre::TextureManager::getSingleton().remove(texture);
	}
	//---------------------------------------------------------
	Ogre::MaterialPtr RenderBackend::createOrUpdateSurfaceMaterial(
		String const & name, RenderMaterialDesc const & desc, bool & outComplete)
	{
		oAssert(!name.empty());
		outComplete = true;
		Ogre::MaterialManager & materialManager =
			Ogre::MaterialManager::getSingleton();
		Ogre::MaterialPtr material = materialManager.getByName(name,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		if(!material)
		{
			material = materialManager.create(name,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		}
		Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
		// the Blinn-Phong SUBSET of the metal-rough description (an
		// approximation by design - no pixel parity for lit content):
		// dielectrics get a faint neutral specular highlight, metals tint it
		// with the albedo; roughness dampens its intensity and widens it
		// (lower shininess exponent). Diffuse keeps the full albedo either
		// way (metals rendering their tint diffusely is part of the subset).
		const float metal = std::clamp(desc.metalness, 0.0f, 1.0f);
		const float gloss = 1.0f - std::clamp(desc.roughness, 0.0f, 1.0f);
		const float baseSpecular = 0.04f;	// the common dielectric F0
		const Ogre::ColourValue specular(
			(baseSpecular + (desc.albedo.r - baseSpecular) * metal) * gloss,
			(baseSpecular + (desc.albedo.g - baseSpecular) * metal) * gloss,
			(baseSpecular + (desc.albedo.b - baseSpecular) * metal) * gloss,
			1.0f);
		pass->setLightingEnabled(true);
		pass->setDiffuse(desc.albedo);
		pass->setAmbient(desc.albedo.r, desc.albedo.g, desc.albedo.b);
		pass->setSpecular(specular);
		pass->setShininess(std::max(gloss * gloss * 128.0f, 1.0f));
		pass->setSelfIllumination(desc.emissive.r, desc.emissive.g,
			desc.emissive.b);
		// texture units are rebuilt from scratch (the update path must be
		// able to REMOVE the albedo map)
		pass->removeAllTextureUnitStates();
		if(!desc.albedoTexture.empty())
		{
			try
			{
				// resolve through EVERY resource group, like sprite textures
				Ogre::TexturePtr texture =
					Ogre::TextureManager::getSingleton().load(desc.albedoTexture,
						Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
				pass->createTextureUnitState()->setTexture(texture);
			}
			catch(Ogre::Exception const & e)
			{
				oDebugError("engine", 0, "RenderSystem::createMaterial('" << name
					<< "'): albedo texture '" << desc.albedoTexture
					<< "' failed to load: " << e.getDescription());
				outComplete = false;
			}
		}
		// the honest classic subset: normal/emissive MAPS are not rendered on
		// this flavor (@see engine_render/RenderMaterial.h) - say so once per
		// material name, then stay quiet
		if(!desc.normalTexture.empty() || !desc.emissiveTexture.empty())
		{
			static std::set<String> warned;
			if(warned.insert(name).second)
			{
				oDebugWarning(false, "RenderSystem::createMaterial('" << name
					<< "'): this render flavor draws the Blinn-Phong "
					"subset - the normal/emissive maps are ignored");
			}
		}
		return material;
	}
	//---------------------------------------------------------
	Ogre::MaterialPtr RenderBackend::createOrUpdateWaterMaterial(
		String const & name, RenderWaterDesc const & desc, bool & outComplete)
	{
		oAssert(!name.empty());
		outComplete = true;
		Ogre::MaterialManager & materialManager =
			Ogre::MaterialManager::getSingleton();
		Ogre::MaterialPtr material = materialManager.getByName(name,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		if(!material)
		{
			material = materialManager.create(name,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		}
		Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
		// the honest subset: the deep/shallow colours blend into ONE flat water
		// tint (no depth-graded transmission here), a glossy specular highlight
		// (the fresnelPower knob widens/brightens it), and alpha transparency.
		const float blend = 0.4f;	// lean toward the deep body colour
		const Ogre::ColourValue tint(
			desc.deepColour.r + (desc.shallowColour.r - desc.deepColour.r) * blend,
			desc.deepColour.g + (desc.shallowColour.g - desc.deepColour.g) * blend,
			desc.deepColour.b + (desc.shallowColour.b - desc.deepColour.b) * blend,
			std::clamp(desc.opacity, 0.0f, 1.0f));
		const float spec = std::clamp(0.25f * std::max(desc.fresnelPower, 0.0f),
			0.0f, 1.0f);
		pass->setLightingEnabled(true);
		pass->setDiffuse(tint);
		pass->setAmbient(tint.r, tint.g, tint.b);
		pass->setSpecular(spec, spec, spec, 1.0f);
		pass->setShininess(96.0f);
		pass->setSelfIllumination(0.0f, 0.0f, 0.0f);
		// transparent surface: alpha-blend over the scene, no depth write (the
		// lakebed shows through)
		pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
		pass->setDepthWriteEnabled(false);
		// the scrolling shimmer overlay (approximating moving ripples): the
		// tiling water normal map bound as a modulating texture unit and
		// scrolled by setWaterMaterialTime. On the Blinn-Phong subset a normal
		// map cannot light the surface, so it reads as a moving colour shimmer -
		// say so once, then stay quiet.
		pass->removeAllTextureUnitStates();
		if(!desc.normalTexture.empty())
		{
			try
			{
				Ogre::TexturePtr texture =
					Ogre::TextureManager::getSingleton().load(desc.normalTexture,
						Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
				Ogre::TextureUnitState* unit = pass->createTextureUnitState();
				unit->setTexture(texture);
				unit->setTextureScale(1.0f / std::max(desc.waveScale, 0.001f),
					1.0f / std::max(desc.waveScale, 0.001f));
				// modulate lightly so the water tint stays dominant
				unit->setColourOperationEx(Ogre::LBX_BLEND_MANUAL,
					Ogre::LBS_TEXTURE, Ogre::LBS_CURRENT,
					Ogre::ColourValue::White, Ogre::ColourValue::White, 0.35f);
			}
			catch(Ogre::Exception const & e)
			{
				oDebugError("engine", 0, "RenderSystem::createWaterMaterial('"
					<< name << "'): normal map '" << desc.normalTexture
					<< "' failed to load: " << e.getDescription());
				outComplete = false;
			}
			static std::set<String> warned;
			if(warned.insert(name).second)
			{
				oDebugWarning(false, "RenderSystem::createWaterMaterial('" << name
					<< "'): this render flavor draws the transparent Blinn-Phong "
					"subset - the normal map animates as a scrolling shimmer "
					"(true fresnel normal-mapped water is next-only)");
			}
		}
		gWaterScrollSpeeds[name] = desc.waveSpeed;
		return material;
	}
	//---------------------------------------------------------
	void RenderBackend::setWaterMaterialTime(String const & name, float seconds)
	{
		std::unordered_map<String, float>::const_iterator it =
			gWaterScrollSpeeds.find(name);
		if(it == gWaterScrollSpeeds.end())
		{
			return;	// no water material by that name - silent no-op (dormancy)
		}
		Ogre::MaterialPtr material = Ogre::MaterialManager::getSingleton()
			.getByName(name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		if(!material)
		{
			return;	// material dropped (project switch) - harmless
		}
		Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
		if(pass->getNumTextureUnitStates() == 0)
		{
			return;	// a flat (no-normal-map) water surface has nothing to scroll
		}
		const float travel = seconds * it->second;
		pass->getTextureUnitState(0)->setTextureScroll(travel, travel * 0.6f);
	}
	//---------------------------------------------------------
	bool RenderSystem::createMaterial(String const & name,
		RenderMaterialDesc const & desc)
	{
		oAssert(!name.empty());
		bool complete = true;
		return RenderBackend::createOrUpdateSurfaceMaterial(name, desc,
			complete) && complete;
	}
	//---------------------------------------------------------
	bool RenderSystem::createWaterMaterial(String const & name,
		RenderWaterDesc const & desc)
	{
		oAssert(!name.empty());
		bool complete = true;
		return RenderBackend::createOrUpdateWaterMaterial(name, desc,
			complete) && complete;
	}
	//---------------------------------------------------------
	void RenderSystem::setWaterTime(String const & name, float seconds)
	{
		RenderBackend::setWaterMaterialTime(name, seconds);
	}
	//---------------------------------------------------------
	RenderWorld* RenderSystem::getWorld() const
	{
		return this->mImpl->world;
	}
	//---------------------------------------------------------
	void RenderSystem::addResourceLocation(String const & path,
		LocationType type, String const & groupName, bool recursive)
	{
		// LT_BIGZIP requires the BigZip archive factory, which Engine
		// registers when constructed with a zip file (unchanged plumbing)
		static const char* const locationTypeNames[] =
			{ "FileSystem", "Zip", "BigZip" };
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			path, locationTypeNames[type],
			groupName.empty()
				? Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME
				: groupName,
			recursive);
	}
	//---------------------------------------------------------
	void RenderSystem::initialiseResourceGroups()
	{
		Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();
	}
	//---------------------------------------------------------
	void RenderSystem::removeResourceLocation(String const & path,
		String const & groupName)
	{
		// idempotent by contract: probing first keeps the facade free of the
		// backend's ItemIdentityException on unknown locations
		Ogre::ResourceGroupManager& resourceGroups =
			Ogre::ResourceGroupManager::getSingleton();
		const String group = groupName.empty()
			? String(Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME)
			: groupName;
		if(resourceGroups.resourceGroupExists(group) &&
			resourceGroups.resourceLocationExists(path, group))
		{
			resourceGroups.removeResourceLocation(path, group);
		}
	}
	//---------------------------------------------------------
	bool RenderSystem::resourceGroupExists(String const & groupName) const
	{
		return Ogre::ResourceGroupManager::getSingleton()
			.resourceGroupExists(groupName);
	}
	//---------------------------------------------------------
	void RenderSystem::destroyResourceGroup(String const & groupName)
	{
		Ogre::ResourceGroupManager& resourceGroups =
			Ogre::ResourceGroupManager::getSingleton();
		if(resourceGroups.resourceGroupExists(groupName))
		{
			resourceGroups.destroyResourceGroup(groupName);
		}
	}
	//---------------------------------------------------------
	bool RenderSystem::resourceExists(String const & resourceName,
		String const & groupName) const
	{
		return Ogre::ResourceGroupManager::getSingleton().resourceExists(
			groupName.empty()
				? Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME
				: groupName,
			resourceName);
	}
	//---------------------------------------------------------
	bool RenderSystem::readResourceText(String const & resourceName,
		String & outText) const
	{
		try
		{
			Ogre::DataStreamPtr stream =
				Ogre::ResourceGroupManager::getSingleton().openResource(
					resourceName,
					Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
			if(!stream)
			{
				return false;
			}
			outText = stream->getAsString();
			return true;
		}
		catch(Ogre::Exception const &)
		{
			return false;	// not found in any group - honest miss
		}
	}
	//---------------------------------------------------------
	RenderSystem::FrameStats RenderSystem::getFrameStats() const
	{
		Ogre::RenderTarget::FrameStats const & backendStats =
			this->mImpl->engine->getRenderWindow(0)->getStatistics();
		FrameStats stats;
		stats.lastFPS = backendStats.lastFPS;
		stats.avgFPS = backendStats.avgFPS;
		stats.bestFPS = backendStats.bestFPS;
		stats.worstFPS = backendStats.worstFPS;
		stats.triangleCount = backendStats.triangleCount;
		stats.batchCount = backendStats.batchCount;
		stats.textureMemoryBytes =
			Ogre::TextureManager::getSingleton().getMemoryUsage();
		return stats;
	}
	//---------------------------------------------------------
	void RenderSystem::resetFrameStats()
	{
		this->mImpl->engine->getRenderWindow(0)->resetStatistics();
	}
}
