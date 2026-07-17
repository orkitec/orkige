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

		//! surface materials that bind a normal map (createOrUpdateSurfaceMaterial
		//! records them). MeshInstance::setMaterial reads this to build tangents
		//! on the mesh a normal-mapped material lands on - and ONLY then, so a
		//! plain material never pays for tangent generation.
		std::set<String> gNormalMappedMaterials;

#ifdef USE_RTSHADER_SYSTEM
		//! @brief pin @p material's RTSS render state to a metal-rough Cook-
		//! Torrance lighting stage (reading albedo/metalness/roughness/emissive
		//! off the pass) plus, when @p normalTuIndex >= 0, a normal-map stage
		//! that perturbs the lit normal from the texture unit at that index.
		//! Rebuilds the shader technique so create AND update both re-derive
		//! cleanly. A no-op when no shader generator is active (fixed function).
		void configureSurfaceShaderState(Ogre::RTShader::ShaderGenerator* generator,
			Ogre::MaterialPtr const & material, int normalTuIndex)
		{
			oAssert(generator);
			const String & name = material->getName();
			const String & group = material->getGroup();
			const String scheme =
				Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME;
			// drop any technique generated from a previous description, then
			// clone the current fixed-function technique (all passes) into the
			// shader scheme so the render state below drives freshly built shaders
			generator->removeAllShaderBasedTechniques(name, group);
			if(!generator->createShaderBasedTechnique(*material,
				Ogre::MaterialManager::DEFAULT_SCHEME_NAME, scheme))
			{
				return;	// no shader technique (unexpected on a shader RS) - the
						// pass values still stand for a fixed-function flavor
			}
			// removeAllShaderBasedTechniques dropped the prior render state, so
			// this one is fresh/empty - build it up from scratch
			Ogre::RTShader::RenderState* renderState =
				generator->getRenderState(scheme, name, group, 0);
			// the fixed-function equivalents EXCEPT lighting (Cook-Torrance
			// replaces it): transform, vertex colour, texturing, fog, alpha test
			renderState->addTemplateSubRenderStates(
				{ Ogre::RTShader::SRS_TRANSFORM,
				  Ogre::RTShader::SRS_VERTEX_COLOUR,
				  Ogre::RTShader::SRS_TEXTURING,
				  Ogre::RTShader::SRS_FOG,
				  Ogre::RTShader::SRS_ALPHA_TEST });
			// the metal-rough lighting stage (specular.xy = metalness/roughness,
			// diffuse = albedo, derived scene colour carries the emissive)
			renderState->addTemplateSubRenderState(generator->createSubRenderState(
				Ogre::RTShader::SRS_COOK_TORRANCE_LIGHTING));
			// the normal-map stage runs one step before lighting (it writes the
			// view-space normal the lighting stage then reads)
			if(normalTuIndex >= 0)
			{
				Ogre::RTShader::SubRenderState* normalMap =
					generator->createSubRenderState(Ogre::RTShader::SRS_NORMALMAP);
				normalMap->setParameter("texture_index",
					Ogre::StringConverter::toString(normalTuIndex));
				normalMap->setParameter("normalmap_space", "tangent_space");
				renderState->addTemplateSubRenderState(normalMap);
			}
			generator->invalidateMaterial(scheme, name, group);
		}
#endif // USE_RTSHADER_SYSTEM
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
	bool RenderSystem::supports(RenderCaps cap) const
	{
		if(cap >= RenderCaps::Count)
		{
			return false;
		}
		return (this->mImpl->caps >> static_cast<int>(cap)) & 1u;
	}
	//---------------------------------------------------------
	unsigned int RenderSystem::lightBudget() const
	{
		return this->mImpl->lightBudget;
	}
	//---------------------------------------------------------
	unsigned int RenderSystem::defaultLightBudget()
	{
		// the classic forward renderer's per-pass dynamic-light headroom
		return RenderBackend::FORWARD_LIGHT_BUDGET;
	}
	//---------------------------------------------------------
	bool RenderSystem::renderOneFrame()
	{
		// coalesced static-region maintenance: membership/visibility changes
		// since the last frame land as ONE rebuild (@see StaticBakeClassic.cpp)
		RenderBackend::staticBakeFlush();
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
		Ogre::RenderWindow* window = this->mImpl->engine->getRenderWindow(0);
		// neither buffer can be read reliably AFTER the swap: with a
		// flip-style swap (macOS GL) the back buffer holds the PREVIOUS
		// frame, and the front buffer of a double-buffered window is
		// undefined under a copy-style swap on a virtual display (black on
		// llvmpipe/Xvfb). Re-render the current state WITHOUT swapping and
		// read the freshly drawn back buffer - deterministic under both swap
		// strategies - then swap the extra frame out normally.
		window->update(false);
		Ogre::Image image(window->suggestPixelFormat(), window->getWidth(),
			window->getHeight());
		Ogre::PixelBox pixels = image.getPixelBox();
		window->copyContentsToMemory(pixels, pixels,
			Ogre::RenderTarget::FB_BACK);
		window->swapBuffers();
		image.save(fileName);
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
		// plain file name (cooked payloads fall back to .dds/.ktx siblings)
		Ogre::TexturePtr texture;
		try
		{
			texture = Ogre::TextureManager::getSingleton().load(
				RenderBackend::resolveTextureResourceName(textureName),
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
		Ogre::Technique* technique = material->getTechnique(0);
		// an UPDATE may have appended an emissive-map pass last time - collapse
		// back to the single surface pass before rebuilding it
		while(technique->getNumPasses() > 1)
		{
			technique->removePass(1);
		}
		Ogre::Pass* pass = technique->getPass(0);
		pass->setLightingEnabled(true);
		pass->setDiffuse(desc.albedo);
		pass->setAmbient(desc.albedo.r, desc.albedo.g, desc.albedo.b);
		pass->setSelfIllumination(desc.emissive.r, desc.emissive.g,
			desc.emissive.b);
		// texture units are rebuilt from scratch (the update path must be
		// able to REMOVE the albedo map). Albedo lands at texture unit 0.
		pass->removeAllTextureUnitStates();
		Ogre::TexturePtr albedoTexture;	// kept for the cutout caster below
		if(!desc.albedoTexture.empty())
		{
			try
			{
				// resolve through EVERY resource group, like sprite textures
				albedoTexture = Ogre::TextureManager::getSingleton().load(
					RenderBackend::resolveTextureResourceName(
						desc.albedoTexture),
					Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
				pass->createTextureUnitState()->setTexture(albedoTexture);
			}
			catch(Ogre::Exception const & e)
			{
				oDebugError("engine", 0, "RenderSystem::createMaterial('" << name
					<< "'): albedo texture '" << desc.albedoTexture
					<< "' failed to load: " << e.getDescription());
				outComplete = false;
			}
		}
		// CUTOUT + TWO-SIDED, honoured in every branch: alpha rejection keeps
		// texels whose albedo alpha passes (CMPF_GREATER_EQUAL threshold - the
		// RTSS render state below includes SRS_ALPHA_TEST, so the generated
		// shaders discard too); two-sided disables back-face culling (the lit
		// normal is NOT flipped for back faces here - the registered subset,
		// the other backend lights both sides)
		const bool cutout = desc.alphaTest > 0.0f;
		if(cutout)
		{
			pass->setAlphaRejectSettings(Ogre::CMPF_GREATER_EQUAL,
				static_cast<unsigned char>(
					std::clamp(desc.alphaTest, 0.0f, 1.0f) * 255.0f + 0.5f));
		}
		else
		{
			pass->setAlphaRejectSettings(Ogre::CMPF_ALWAYS_PASS, 0);
		}
		pass->setCullingMode(desc.twoSided
			? Ogre::CULL_NONE : Ogre::CULL_CLOCKWISE);
		// a re-created material may have been normal-mapped before this update
		gNormalMappedMaterials.erase(name);
#ifdef USE_RTSHADER_SYSTEM
		if(Ogre::RTShader::ShaderGenerator* generator =
			Ogre::RTShader::ShaderGenerator::getSingletonPtr())
		{
			// metal-rough response: the Cook-Torrance lighting stage reads
			// ROUGHNESS from specular.x and METALNESS from specular.y (its
			// ormParams.yz take specular.xy - the orm layout, occlusion/
			// roughness/metalness); the shininess exponent is unused there.
			// The historical swapped order turned every rough dielectric into
			// a mirror-smooth metal: black diffuse + a pin specular.
			pass->setSpecular(std::clamp(desc.roughness, 0.0f, 1.0f),
				std::clamp(desc.metalness, 0.0f, 1.0f), 0.0f, 1.0f);
			pass->setShininess(0.0f);
			// the tangent-space normal map, bound as a texture unit the normal-
			// map stage samples (kept OUT of the colour texturing stage)
			int normalTuIndex = -1;
			if(!desc.normalTexture.empty())
			{
				try
				{
					Ogre::TexturePtr texture = Ogre::TextureManager::getSingleton()
						.load(RenderBackend::resolveTextureResourceName(
							desc.normalTexture),
							Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
					Ogre::TextureUnitState* unit = pass->createTextureUnitState();
					unit->setTexture(texture);
					normalTuIndex =
						static_cast<int>(pass->getNumTextureUnitStates()) - 1;
					Ogre::RTShader::ShaderGenerator::_markNonFFP(unit);
					gNormalMappedMaterials.insert(name);
				}
				catch(Ogre::Exception const & e)
				{
					oDebugError("engine", 0, "RenderSystem::createMaterial('"
						<< name << "'): normal map '" << desc.normalTexture
						<< "' failed to load: " << e.getDescription());
					outComplete = false;
				}
			}
			// the emissive MAP as an additive self-illumination pass. Surface
			// materials are opaque (RenderMaterial.h), so additive-over-scene is
			// safe; guard the odd translucent albedo (additive on a transparent
			// base double-counts the backdrop) and say so once.
			if(!desc.emissiveTexture.empty())
			{
				if(desc.albedo.a < 1.0f)
				{
					static std::set<String> warned;
					if(warned.insert(name).second)
					{
						oDebugWarning(false, "RenderSystem::createMaterial('"
							<< name << "'): the emissive map is skipped on a "
							"translucent surface (the additive glow pass is "
							"opaque-material only)");
					}
				}
				else
				{
					try
					{
						Ogre::TexturePtr texture =
							Ogre::TextureManager::getSingleton().load(
								RenderBackend::resolveTextureResourceName(
									desc.emissiveTexture),
								Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
						Ogre::Pass* glow = technique->createPass();
						glow->setLightingEnabled(false);
						glow->setSceneBlending(Ogre::SBT_ADD);
						glow->setDepthWriteEnabled(false);
						Ogre::TextureUnitState* unit =
							glow->createTextureUnitState();
						unit->setTexture(texture);
						// modulate the emissive map by the emissive colour factor
						unit->setColourOperationEx(Ogre::LBX_MODULATE,
							Ogre::LBS_TEXTURE, Ogre::LBS_MANUAL,
							Ogre::ColourValue::White, Ogre::ColourValue(
								desc.emissive.r, desc.emissive.g, desc.emissive.b));
						// with a map, the emissive colour is the MAP's tint, not
						// a whole-surface glow: the additive pass above carries
						// colour x map (the other backend's emissive = colour x
						// texture), so the flat self-illumination must not ALSO
						// light the full surface
						pass->setSelfIllumination(0.0f, 0.0f, 0.0f);
					}
					catch(Ogre::Exception const & e)
					{
						oDebugError("engine", 0, "RenderSystem::createMaterial('"
							<< name << "'): emissive map '" << desc.emissiveTexture
							<< "' failed to load: " << e.getDescription());
						outComplete = false;
					}
				}
			}
			// pin the shader render state LAST, so createShaderBasedTechnique
			// clones both the surface pass and any emissive pass
			configureSurfaceShaderState(generator, material, normalTuIndex);
			// CUTOUT CASTER: the scene's derived-caster machinery mutates ONE
			// shared plain-black pass per renderable at render time - state the
			// RTSS-generated caster program (built once) cannot follow. A
			// per-material shadow-caster OVERRIDE material is the mechanism the
			// backend honours verbatim (deriveShadowCasterPass returns its best
			// technique untouched): it re-binds the albedo texture with the
			// SAME alpha rejection and culling, the resolver generates its
			// FFP-emulating shader (texturing + alpha test), and the depth
			// pass discards the cutout texels - a leaf shadows as a leaf.
			Ogre::MaterialPtr caster;
			const String casterName = name + "/Caster";
			if(cutout && albedoTexture)
			{
				caster = materialManager.getByName(casterName,
					Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
				if(!caster)
				{
					caster = materialManager.create(casterName,
						Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
				}
				// a caster never receives (keeps the scheme's receiver stage
				// out of its generated shader - it renders INTO the map)
				caster->setReceiveShadows(false);
				Ogre::Pass* casterPass =
					caster->getTechnique(0)->getPass(0);
				casterPass->setLightingEnabled(false);
				casterPass->removeAllTextureUnitStates();
				casterPass->createTextureUnitState()->setTexture(albedoTexture);
				casterPass->setAlphaRejectSettings(
					pass->getAlphaRejectFunction(), pass->getAlphaRejectValue());
				casterPass->setCullingMode(pass->getCullingMode());
				caster->load();	// the override path reads getBestTechnique
			}
			else if(Ogre::MaterialPtr stale = materialManager.getByName(
				casterName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
			{
				// an update dropped the cutout - retire the override material
				materialManager.remove(stale);
			}
			// bind (or clear) on EVERY technique, including the generated one
			for(unsigned short each = 0;
				each < material->getNumTechniques(); ++each)
			{
				material->getTechnique(each)->setShadowCasterMaterial(caster);
			}
			return material;
		}
#endif // USE_RTSHADER_SYSTEM
		// fixed-function fallback (no shader generator active): the Blinn-Phong
		// SUBSET of the metal-rough description - dielectrics get a faint neutral
		// highlight, metals tint it with the albedo, roughness dampens/widens it;
		// the normal/emissive MAPS cannot render here (logged once).
		const float metal = std::clamp(desc.metalness, 0.0f, 1.0f);
		const float gloss = 1.0f - std::clamp(desc.roughness, 0.0f, 1.0f);
		const float baseSpecular = 0.04f;	// the common dielectric F0
		pass->setSpecular(
			(baseSpecular + (desc.albedo.r - baseSpecular) * metal) * gloss,
			(baseSpecular + (desc.albedo.g - baseSpecular) * metal) * gloss,
			(baseSpecular + (desc.albedo.b - baseSpecular) * metal) * gloss,
			1.0f);
		pass->setShininess(std::max(gloss * gloss * 128.0f, 1.0f));
		if(!desc.normalTexture.empty() || !desc.emissiveTexture.empty())
		{
			static std::set<String> warned;
			if(warned.insert(name).second)
			{
				oDebugWarning(false, "RenderSystem::createMaterial('" << name
					<< "'): no shader generator active - drawing the Blinn-Phong "
					"subset, the normal/emissive maps are ignored");
			}
		}
		return material;
	}
	//---------------------------------------------------------
	bool RenderBackend::materialUsesNormalMap(String const & name)
	{
		return gNormalMappedMaterials.find(name) != gNormalMappedMaterials.end();
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
		Ogre::Technique* technique = material->getTechnique(0);
		Ogre::Pass* pass = technique->getPass(0);
		// the water body colour: the deep/shallow colours blend into ONE tint
		// (no depth-graded transmission here), alpha = opacity so the lakebed
		// shows through. The metal-rough lighting below adds the sun reflection.
		const float blend = 0.4f;	// lean toward the deep body colour
		const Ogre::ColourValue tint(
			desc.deepColour.r + (desc.shallowColour.r - desc.deepColour.r) * blend,
			desc.deepColour.g + (desc.shallowColour.g - desc.deepColour.g) * blend,
			desc.deepColour.b + (desc.shallowColour.b - desc.deepColour.b) * blend,
			std::clamp(desc.opacity, 0.0f, 1.0f));
		// water is a per-instance material, so the surface's receive flag maps
		// 1:1 (@see RenderWaterDesc::receiveShadows); water never casts - the
		// component turns its plane's caster flag off
		material->setReceiveShadows(desc.receiveShadows);
		pass->setLightingEnabled(true);
		pass->setDiffuse(tint);				// alpha carries the opacity
		pass->setAmbient(tint.r, tint.g, tint.b);
		pass->setSelfIllumination(0.0f, 0.0f, 0.0f);
		// transparent surface: alpha-blend over the scene, no depth write (the
		// lakebed shows through)
		pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
		pass->setDepthWriteEnabled(false);
		pass->removeAllTextureUnitStates();
		gNormalMappedMaterials.erase(name);	// a re-create may have been mapped
#ifdef USE_RTSHADER_SYSTEM
		if(Ogre::RTShader::ShaderGenerator* generator =
			Ogre::RTShader::ShaderGenerator::getSingletonPtr())
		{
			// a glossy dielectric: Cook-Torrance reads ROUGHNESS from
			// specular.x and METALNESS from specular.y (the orm layout - same
			// order note as createMaterial above; 0 = non-metal water).
			// fresnelPower sharpens the reflection (lower roughness = a
			// tighter, brighter sun glint riding the ripples).
			const float roughness = std::clamp(
				0.15f / std::max(desc.fresnelPower, 0.25f), 0.03f, 0.4f);
			pass->setSpecular(roughness, 0.0f, 0.0f, 1.0f);
			pass->setShininess(0.0f);
			// A COMPOSITE of two water cues, because RTSS classic can light a
			// normal map OR scroll a texture, not both on one unit (the
			// NormalMap stage samples the RAW texcoord, ignoring the scroll):
			//  - TU 0 = the normal map as a scrolling COLOUR shimmer (the FFP
			//    texturing stage applies setWaterTime's scroll), carrying the
			//    visible MOTION - subtle, so the water tint stays dominant;
			//  - TU 1 = the SAME normal map marked non-FFP, which the NormalMap
			//    stage samples to LIGHT the ripples (a static relief that catches
			//    the sun). Together: lit ripples + a moving surface. Fully
			//    animated normal-mapped water is next-only.
			int normalTuIndex = -1;
			if(!desc.normalTexture.empty())
			{
				try
				{
					Ogre::TexturePtr texture = Ogre::TextureManager::getSingleton()
						.load(RenderBackend::resolveTextureResourceName(
							desc.normalTexture),
							Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
					const float invScale = 1.0f / std::max(desc.waveScale, 0.001f);
					// TU 0: the scrolling colour shimmer (motion) - the one
					// setWaterMaterialTime scrolls
					Ogre::TextureUnitState* shimmer = pass->createTextureUnitState();
					shimmer->setTexture(texture);
					shimmer->setTextureScale(invScale, invScale);
					shimmer->setColourOperationEx(Ogre::LBX_BLEND_MANUAL,
						Ogre::LBS_TEXTURE, Ogre::LBS_CURRENT,
						Ogre::ColourValue::White, Ogre::ColourValue::White, 0.18f);
					// TU 1: the lit normal map (static relief catching the sun)
					Ogre::TextureUnitState* normalUnit =
						pass->createTextureUnitState();
					normalUnit->setTexture(texture);
					normalUnit->setTextureScale(invScale, invScale);
					normalTuIndex =
						static_cast<int>(pass->getNumTextureUnitStates()) - 1;
					Ogre::RTShader::ShaderGenerator::_markNonFFP(normalUnit);
					gNormalMappedMaterials.insert(name);
				}
				catch(Ogre::Exception const & e)
				{
					oDebugError("engine", 0, "RenderSystem::createWaterMaterial('"
						<< name << "'): normal map '" << desc.normalTexture
						<< "' failed to load: " << e.getDescription());
					outComplete = false;
				}
			}
			// Cook-Torrance (+ the normal-map stage): the same metal-rough path
			// the surface materials use, so the ripples light + the intrinsic
			// Fresnel term brightens the grazing-angle reflection
			configureSurfaceShaderState(generator, material, normalTuIndex);
			gWaterScrollSpeeds[name] = desc.waveSpeed;
			return material;
		}
#endif // USE_RTSHADER_SYSTEM
		// fixed-function fallback (no shader generator): the transparent Blinn-
		// Phong subset - a glossy specular highlight (fresnelPower widens it) and
		// the normal map bound as a scrolling colour shimmer (it cannot light the
		// surface here), logged once.
		const float spec = std::clamp(0.25f * std::max(desc.fresnelPower, 0.0f),
			0.0f, 1.0f);
		pass->setSpecular(spec, spec, spec, 1.0f);
		pass->setShininess(96.0f);
		if(!desc.normalTexture.empty())
		{
			try
			{
				Ogre::TexturePtr texture =
					Ogre::TextureManager::getSingleton().load(
						RenderBackend::resolveTextureResourceName(
							desc.normalTexture),
						Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
				Ogre::TextureUnitState* unit = pass->createTextureUnitState();
				unit->setTexture(texture);
				unit->setTextureScale(1.0f / std::max(desc.waveScale, 0.001f),
					1.0f / std::max(desc.waveScale, 0.001f));
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
					<< "'): no shader generator - drawing the transparent Blinn-"
					"Phong subset, the normal map animates as a colour shimmer");
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
