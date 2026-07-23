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
#include "engine_render_classic/HemisphereAmbientSrs.h"
#include "engine_render_classic/MetalRoughLightingSrs.h"
#include "engine_graphic/Engine.h"
#include <core_util/SkyEnvMap.h>
#include "engine_filesystem/PakMount.h"
#include <core_debug/DebugMacros.h>

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

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

		//--- screen-space water refraction (grab-pass) --- refraction block ---
		//! Names of the LIVE refractive water materials (createOrUpdateWaterMaterial
		//! records them when screen-space refraction is on + supported). Used to
		//! decide which mesh entities the scene-grab render target must hide (a
		//! refractive surface samples the scene BEHIND it, so it cannot be in the
		//! grab) and which materials setWaterMaterialTime scrolls through their
		//! program constants rather than a texture-unit scroll.
		std::set<String> gRefractiveWaterMaterials;
		//! per refractive-water-material build-time knobs, so the per-frame
		//! scroll update can re-push the FULL refractParams/skyParams 4-vectors
		//! (the scroll rides refractParams' zw lane; skyParams' yz lanes track
		//! the LIVE image-lighting state) without losing the build-time values
		struct RefractKnobs
		{
			float strength = 0.02f;
			float waveScale = 6.0f;
			//! the fresnel F0 for the sky-reflection blend (@see the refract
			//! program's skyParams - the same F0 formula as the reflect path)
			float skyF0 = 0.05f;
			//! geometric swell amplitude (waveParams.x - @see RenderWaterDesc)
			float waveHeight = 0.0f;
		};
		std::unordered_map<String, RefractKnobs> gRefractiveWaterKnobs;
		//! The mesh entities currently wearing a CUSTOM water material (refractive
		//! OR planar-reflective) - both the scene-grab and the reflection render
		//! targets hide these (and restore their prior visibility) while they
		//! capture the scene the water samples (the surface cannot appear in the
		//! grab it refracts, nor in the mirror it reflects).
		std::set<Ogre::Entity*> gWaterHideEntities;
		//! the ONE shared scene-grab render target: a window-sized colour texture
		//! re-rendered from the main camera each frame with the refractive water
		//! HIDDEN, so the water program can sample "the scene behind the surface"
		//! at a normal-perturbed screen UV. NULL until the first refractive water
		//! builds; recreated on a window-size change.
		Ogre::TexturePtr gSceneGrabTexture;
		unsigned int gSceneGrabWidth = 0;
		unsigned int gSceneGrabHeight = 0;
		//! the shared water-refraction GLSL programs, created once (GL3Plus)
		bool gRefractionProgramsBuilt = false;
		//! the grab render target's name (a stable resource name)
		const char* const kSceneGrabTexture = "Orkige/WaterRefraction/SceneGrab";
		//! @brief hides the refractive water while the scene-grab target renders
		//! (and restores its exact prior visibility after), so the captured scene
		//! is what sits BEHIND the water - the colour it refracts.
		struct SceneGrabListener : public Ogre::RenderTargetListener
		{
			std::vector<std::pair<Ogre::Entity*, bool>> mHidden;
			void preRenderTargetUpdate(const Ogre::RenderTargetEvent&) override
			{
				this->mHidden.clear();
				for(Ogre::Entity* entity : gWaterHideEntities)
				{
					this->mHidden.emplace_back(entity, entity->getVisible());
					entity->setVisible(false);
				}
				// track the window's animated sky/background colour (friend method
				// - needs the protected render-system window)
				RenderBackend::updateSceneGrabViewport();
			}
			void postRenderTargetUpdate(const Ogre::RenderTargetEvent&) override
			{
				for(std::pair<Ogre::Entity*, bool> const & each : this->mHidden)
				{
					each.first->setVisible(each.second);
				}
				this->mHidden.clear();
			}
		};
		SceneGrabListener gSceneGrabListener;
		//--- end refraction block -----------------------------------------

		//--- planar (mirror-of-scene) water reflection --- reflection block ---
		//! Names of the LIVE planar-reflective water materials
		//! (createOrUpdateWaterMaterial records them when planar reflection is on +
		//! supported). Non-empty => the shared mirror camera + reflection render
		//! target are live and setWaterMaterialTime scrolls the ripple through the
		//! water program constants.
		std::set<String> gReflectiveWaterMaterials;
		//! per reflective-water-material build-time knobs, so the per-frame scroll
		//! update re-pushes the FULL program 4-vectors (the scroll rides the zw
		//! lane) without losing them: {reflectStrength (the PRE-COMPUTED fresnel
		//! F0 - @see applyReflectionParams), waveScale, refractStrength,
		//! refractEnabled(0/1)}.
		struct ReflectKnobs
		{
			float reflectStrength = 0.12f;
			float waveScale = 6.0f;
			float refractStrength = 0.02f;
			float refractEnabled = 0.0f;
			//! the body-dim scale (reflectParams.z) - @see applyReflectionParams
			float baseScale = 1.0f;
			//! geometric swell amplitude (waveParams.x - @see RenderWaterDesc)
			float waveHeight = 0.0f;
		};
		std::unordered_map<String, ReflectKnobs> gReflectiveWaterKnobs;
		//! the ONE shared reflection render target: a window-sized colour texture
		//! re-rendered each frame from the MIRROR camera (the main camera reflected
		//! across the water plane, geometry below the plane custom-near-clipped, the
		//! water surface hidden), so the water program can sample the mirror image
		//! at the fragment's ripple-perturbed screen UV. NULL until the first
		//! reflective water builds; recreated on a window-size change.
		Ogre::TexturePtr gReflectionTexture;
		unsigned int gReflectionWidth = 0;
		unsigned int gReflectionHeight = 0;
		//! the dedicated mirror camera (owned by the scene manager) + the node it
		//! rides (OGRE positions cameras through a node); the node's transform is
		//! synced to the main camera each frame and the camera reflected across the
		//! plane. Both owned by the scene manager, destroyed at teardown.
		Ogre::Camera* gMirrorCamera = NULL;
		Ogre::SceneNode* gMirrorNode = NULL;
		//! the world-space swell frequency + phase rate BOTH flavors' water
		//! vertex stages share (the next backend bakes the same numbers into
		//! its water datablock piece), so the two flavors' swells move in
		//! lockstep. Wavelength ~12.5 world units, ~1.2 rad/s travel.
		constexpr float kSwellWorldFrequency = 0.5f;
		constexpr float kSwellPhaseRate = 1.2f;
		//! the mirror plane's world Y (the water surface height, normal +Y) the
		//! reflection is currently built for - a change recreates the reflection.
		float gReflectionPlaneY = 0.0f;
		bool gReflectionPlaneSet = false;
		//! the shared water-reflection GLSL programs, created once (GL3Plus)
		bool gReflectionProgramsBuilt = false;
		const char* const kReflectionTexture = "Orkige/WaterReflection/Mirror";
		const char* const kMirrorCamera = "Orkige/WaterReflection/MirrorCam";
		//! @brief hides the reflective water surface while the mirror target renders
		//! (restoring its exact prior visibility after) AND syncs the mirror camera
		//! to the main camera reflected across the water plane, so the captured
		//! image is the mirror of the scene ABOVE the surface.
		struct ReflectionListener : public Ogre::RenderTargetListener
		{
			std::vector<std::pair<Ogre::Entity*, bool>> mHidden;
			void preRenderTargetUpdate(const Ogre::RenderTargetEvent&) override;
			void postRenderTargetUpdate(const Ogre::RenderTargetEvent&) override
			{
				for(std::pair<Ogre::Entity*, bool> const & each : this->mHidden)
				{
					each.first->setVisible(each.second);
				}
				this->mHidden.clear();
			}
		};
		ReflectionListener gReflectionListener;
		//--- end reflection block -----------------------------------------

		//--- image-based lighting (skybox-sourced) - IBL block ------------
		//! the realized image-lighting state the shader-state builder reads:
		//! while active, every generated Cook-Torrance material appends the
		//! image-based-lighting stage over @c envTexture at @c luminance
		//! (@see RenderBackend::applyImageLighting)
		struct IblState
		{
			bool	active = false;		//!< append the IBL stage right now?
			String	envTexture;			//!< the environment chain cubemap name
			float	luminance = 1.0f;	//!< scales the added contribution
		};
		IblState gIbl;
		//! every generated Cook-Torrance material (surface + water) by name,
		//! with the normal-map texture-unit index its shader state was pinned
		//! with - the re-derive set a live image-lighting toggle walks. An
		//! entry whose material died (project switch) is skipped and pruned.
		std::map<String, int> gSurfaceMaterials;
		//! the subset of gSurfaceMaterials that carries the per-pixel hemisphere
		//! ambient sub-render-state (surface materials, NOT water - water keeps
		//! its own tuned flat ambient). The image-lighting re-derive consults it
		//! so a rebuilt material re-grows (or keeps out) the hemisphere stage.
		std::set<String> gHemisphereMaterials;
		//! the one derived-chain texture name (recreated on source/tier change)
		const char* const kIblChainTexture = "Orkige/IblChain";
		//! which (skybox, tier) pair the chain was built from (skip rebuilds)
		String gIblChainSource;
		IblPreset::Quality gIblChainQuality = IblPreset::IQ_OFF;
		//! the reason last warned about, so the honest degrade logs ONCE
		String gIblWarnedReason;
		//! the synthetic source identity of a runtime-captured procedural-sky
		//! environment (the authored-skybox source is its cubemap name); the
		//! two sources feed the ONE image-based-lighting stage below
		const char* const kProceduralSource = "<procedural-sky>";
		//! the atmosphere/sun inputs the bound procedural capture was built
		//! from - recapture only when they move materially, never per frame
		SkyEnvMap::CaptureKey gProceduralIblKey;
		bool gProceduralIblHasKey = false;
		//! the max sun swing (as a cosine) tolerated before a recapture (~6
		//! degrees) - the same coarse cadence as the next flavor
		const float kSunMoveCosThreshold = 0.9945f;	// cos(6 degrees)
		//--- end IBL block ------------------------------------------------

		//! @brief the shared 1x1 white texture behind the flat-emissive glow
		//! pass: the emissive-map recipe (additive pass, LBX_MODULATE texture x
		//! manual colour) with the map factored out to constant white, so a
		//! plain emissive COLOUR glows too (@see createOrUpdateSurfaceMaterial)
		Ogre::TexturePtr getOrCreateWhiteTexture()
		{
			char const * const kName = "Orkige/White1x1";
			Ogre::TextureManager & textures = Ogre::TextureManager::getSingleton();
			Ogre::TexturePtr texture = textures.getByName(kName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
			if(texture)
			{
				return texture;
			}
			unsigned char pixels[4] = { 255u, 255u, 255u, 255u };
			Ogre::Image image;
			image.loadDynamicImage(pixels, 1u, 1u, 1u, Ogre::PF_A8B8G8R8, false);
			return textures.loadImage(kName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, image);
		}

#ifdef USE_RTSHADER_SYSTEM
		//! @brief pin @p material's RTSS render state to a metal-rough Cook-
		//! Torrance lighting stage (reading albedo/metalness/roughness/emissive
		//! off the pass) plus, when @p normalTuIndex >= 0, a normal-map stage
		//! that perturbs the lit normal from the texture unit at that index.
		//! Rebuilds the shader technique so create AND update both re-derive
		//! cleanly. A no-op when no shader generator is active (fixed function).
		void configureSurfaceShaderState(Ogre::RTShader::ShaderGenerator* generator,
			Ogre::MaterialPtr const & material, int normalTuIndex,
			bool hemisphereAmbient)
		{
			oAssert(generator);
			const String & name = material->getName();
			const String & group = material->getGroup();
			const String scheme =
				Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME;
			// per-pixel hemisphere ambient OWNS the ambient fill for generated
			// surface materials: zero the source pass ambient reflectance so the
			// Cook-Torrance stage's DERIVED_SCENE_COLOUR ambient contribution
			// (sceneAmbient x material.ambient) collapses to nothing and the
			// hemisphere sub-render-state added below is the sole ambient path -
			// no double count. The scene manager still holds the flat average for
			// any non-generated consumer (@see RenderWorld::setAmbientHemisphere).
			// Water opts out (hemisphereAmbient=false) and keeps its tuned ambient.
			if(hemisphereAmbient)
			{
				material->getTechnique(0)->getPass(0)->setAmbient(
					Ogre::ColourValue::Black);
			}
			// the generator picks its clone source from the material's
			// SUPPORTED techniques, which only exist once the material has
			// compiled - a freshly created (never rendered) material answers
			// none and the pin would silently fail (idempotent on updates)
			material->load();
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
			// the metal-rough lighting stage (specular.xy = roughness/metalness,
			// diffuse = albedo, derived scene colour carries the emissive) - the
			// ENGINE-OWNED stage that reproduces the other backend's per-light
			// response (raw albedo, renormalised diffuse, sqrt display transfer;
			// @see MetalRoughLightingSrs.h for the four response-level
			// differences it removes vs the stock Cook-Torrance stage)
			addMetalRoughLightingSubRenderState(generator, renderState);
			// the per-pixel two-colour sky/ground ambient fill, evaluated right
			// after the Cook-Torrance stage - the classic mirror of next's HlmsPbs
			// ambient-hemisphere response (@see HemisphereAmbientSrs.h), so both
			// flavors light a surface's ambient from the same sky/ground split
			if(hemisphereAmbient)
			{
				addHemisphereAmbientSubRenderState(generator, renderState);
			}
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
			// while image lighting is active, the image-based-lighting stage
			// runs after the Cook-Torrance stage and ADDS the environment
			// chain's specular + diffuse contribution (it binds the DFG LUT +
			// the chain cubemap as its own texture units on the generated
			// pass, so the FFP unit indices above stay untouched) - @see
			// RenderBackend::applyImageLighting, which re-derives this state
			// on every toggle so an inactive state carries NO residue
			if(gIbl.active)
			{
				Ogre::RTShader::SubRenderState* imageLighting =
					generator->createSubRenderState(
						Ogre::RTShader::SRS_IMAGE_BASED_LIGHTING);
				imageLighting->setParameter("texture", gIbl.envTexture);
				imageLighting->setParameter("luminance",
					Ogre::StringConverter::toString(gIbl.luminance));
				renderState->addTemplateSubRenderState(imageLighting);
			}
			// remember the pinned state so a live image-lighting toggle can
			// re-derive every generated material with the same inputs (and whether
			// this one carries the hemisphere stage, so the re-derive matches)
			gSurfaceMaterials[name] = normalTuIndex;
			if(hemisphereAmbient)
			{
				gHemisphereMaterials.insert(name);
			}
			else
			{
				gHemisphereMaterials.erase(name);
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
			// a plain emissive COLOUR (no map) as the SAME additive glow pass,
			// over the shared 1x1 white texture: the Cook-Torrance stage folds
			// self-illumination into the derived scene colour MULTIPLIED by the
			// base colour (and drops it entirely in a light-less scene), so a
			// dark-albedo emissive surface would not glow - the additive pass
			// restores the other backend's independent emissive term (tolerance
			// parity). Translucent surfaces keep the (weaker) self-illumination
			// path - the additive pass is opaque-material only, like the map's.
			if(desc.emissiveTexture.empty() && desc.albedo.a >= 1.0f &&
				(desc.emissive.r > 0.0f || desc.emissive.g > 0.0f ||
					desc.emissive.b > 0.0f))
			{
				Ogre::Pass* glow = technique->createPass();
				glow->setLightingEnabled(false);
				glow->setSceneBlending(Ogre::SBT_ADD);
				glow->setDepthWriteEnabled(false);
				Ogre::TextureUnitState* unit = glow->createTextureUnitState();
				unit->setTexture(getOrCreateWhiteTexture());
				unit->setColourOperationEx(Ogre::LBX_MODULATE,
					Ogre::LBS_TEXTURE, Ogre::LBS_MANUAL,
					Ogre::ColourValue::White, Ogre::ColourValue(
						desc.emissive.r, desc.emissive.g, desc.emissive.b));
				// the additive pass carries the whole emissive term now
				pass->setSelfIllumination(0.0f, 0.0f, 0.0f);
			}
			// pin the shader render state LAST, so createShaderBasedTechnique
			// clones both the surface pass and any emissive pass. Surface
			// materials carry the per-pixel hemisphere ambient (unlike water).
			configureSurfaceShaderState(generator, material, normalTuIndex,
				/*hemisphereAmbient*/ true);
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
	//--- screen-space water refraction (grab-pass) --------------------
	//---------------------------------------------------------
	bool RenderBackend::screenSpaceRefractionSupported()
	{
#ifdef USE_RTSHADER_SYSTEM
		Ogre::RTShader::ShaderGenerator* generator =
			Ogre::RTShader::ShaderGenerator::getSingletonPtr();
		if(!generator)
		{
			return false;	// no shader generator - no programmable water pass
		}
		// the grab-pass water program ships two GLSL variants (@see
		// waterGlslProfile): desktop GL core (GL3Plus - the default classic
		// render system and what the facade selfcheck boots) and GLSL ES 3.0.
		// A desktop GLSL target always qualifies; a GLES/WebGL target qualifies
		// only where GLSL ES 3.0 exists - the grab program indexes the sky
		// cubemap's mip chain and needs the ES3 sampling that a bare
		// GLES2/WebGL1 context lacks, so gate on glsl300es exactly like the IBL
		// stage (WebGL2/GLES3 -> the advanced water; the GLES2/WebGL1 floor keeps
		// rendering the byte-stable Stage-1 look). A Vulkan/Metal target stays
		// false pending its own shader variant + an on-device proof run.
		const String language = generator->getTargetLanguage();
		if(language == "glsl")
		{
			return true;
		}
		if(language == "glsles")
		{
			return Ogre::GpuProgramManager::getSingleton().isSyntaxSupported(
				"glsl300es");
		}
		return false;
#else
		return false;
#endif
	}
	//---------------------------------------------------------
	bool RenderBackend::screenSpacePlanarReflectionSupported()
	{
		// the mirror-camera reflection RenderTexture is sampled by the SAME water
		// program family as refraction (desktop GL core + GLSL ES 3.0), so the
		// gate is identical: a desktop GLSL target (GL3Plus) OR a GLES/WebGL
		// target with GLSL ES 3.0 (WebGL2/GLES3). The GLES2/WebGL1 floor answers
		// false (byte-stable sky-reflection fallback); a Vulkan/Metal context
		// answers false pending its own shader variant.
		return RenderBackend::screenSpaceRefractionSupported();
	}
	//---------------------------------------------------------
	namespace
	{
		//! the RTSS target-language profile the water programs are authored for.
		//! The advanced-water programs carry two GLSL variants: the desktop GL
		//! core profile (#version 150, what the facade selfcheck boots) and GLSL
		//! ES 3.0 (#version 300 es - the WebGL2/GLES3 floor, enabled on the same
		//! glsl300es probe the IBL stage gates on, @see
		//! imageBasedLightingSupported). The program BODY is IDENTICAL between the
		//! two; only this preamble differs (the ES profile's mandatory default-
		//! precision declarations), so the desktop source stays byte-for-byte the
		//! same and the two variants never drift.
		struct WaterGlslProfile
		{
			const char* language;    //!< HighLevelGpuProgramManager language id
			const char* vsPreamble;  //!< #version (+ ES precision) for a VS
			const char* fsPreamble;  //!< #version (+ ES precision) for a FS
		};
		WaterGlslProfile waterGlslProfile()
		{
			bool es = false;
#ifdef USE_RTSHADER_SYSTEM
			if(Ogre::RTShader::ShaderGenerator* generator =
				Ogre::RTShader::ShaderGenerator::getSingletonPtr())
			{
				es = generator->getTargetLanguage() == "glsles";
			}
#endif
			if(es)
			{
				// GLSL ES 3.0: in/out + texture() are already the desktop
				// spelling (the #version 150 body uses them), so only the
				// version line and the ES-mandatory default precision differ.
				// samplerCube precision is declared explicitly so the fresnel
				// sky sample keeps its desktop fidelity (ES defaults samplers to
				// lowp in the fragment stage).
				return WaterGlslProfile{
					"glsles",
					"#version 300 es\n"
					"precision highp float;\n"
					"precision highp int;\n",
					"#version 300 es\n"
					"precision highp float;\n"
					"precision highp int;\n"
					"precision highp sampler2D;\n"
					"precision highp samplerCube;\n"};
			}
			return WaterGlslProfile{ "glsl", "#version 150\n", "#version 150\n" };
		}
		//! the shared water-refraction GLSL programs (GL3Plus), created once. The
		//! vertex program forwards the clip position (for the screen UV) and the
		//! plane UV (for the scrolling ripple normal); the fragment program
		//! samples the scene grab at a normal-perturbed screen UV and tints it by
		//! the water body. Authored inline (not a material script) and confined to
		//! this backend, mirroring the createManual RTT idiom.
		void ensureRefractionPrograms()
		{
			if(gRefractionProgramsBuilt)
			{
				return;
			}
			gRefractionProgramsBuilt = true;
			const WaterGlslProfile profile = waterGlslProfile();
			Ogre::HighLevelGpuProgramManager & programs =
				Ogre::HighLevelGpuProgramManager::getSingleton();
			if(!programs.getByName("Orkige/WaterRefract_vs",
				Ogre::RGN_INTERNAL))
			{
				Ogre::HighLevelGpuProgramPtr vs = programs.createProgram(
					"Orkige/WaterRefract_vs", Ogre::RGN_INTERNAL,
					profile.language, Ogre::GPT_VERTEX_PROGRAM);
				vs->setSource(profile.vsPreamble + std::string(
					"uniform mat4 worldViewProj;\n"
					"uniform mat4 world;\n"
					"uniform vec4 waveParams;\n"  // x=amplitude y=frequency z=phase w=unused
					"in vec4 vertex;\n"
					"in vec4 uv0;\n"
					"out vec2 vUv;\n"
					"out vec4 vClip;\n"
					"out vec3 vWorldPos;\n"
					"out vec3 vSwellNormal;\n"  // world-space swell normal (flat = (0,1,0))
					"void main()\n"
					"{\n"
					// GEOMETRIC swell: a travelling two-sine sum evaluated over the
					// plane's WORLD-space footprint. The water mesh is a unit plane
					// scaled to world size (Y-scale 1), so reading the swell over WORLD
					// x/z gives it the SAME world wavelength as the next flavor - the
					// two run the SAME formula/constants over world x/z and stay in
					// lockstep (a unit-plane object-space read would collapse the whole
					// surface into one broad dome). The FLAT vertex's world position
					// feeds the swell argument; the displacement adds to the object Y
					// (Y-scale 1 => it IS the world Y delta), so the clip transform
					// stays worldViewProj*pos and amplitude 0 leaves the flat plane
					// byte-exact.
					"    vec4 pos = vertex;\n"
					"    vec3 wp = (world * pos).xyz;\n"
					"    float A = waveParams.x;\n"
					"    float kf = waveParams.y;\n"
					"    float ph = waveParams.z;\n"
					// FOUR-component travelling spectrum (a pure two-sine swell reads
					// as an even lattice - real water needs incommensurate wavelengths,
					// skewed azimuths and unequal phase speeds so the interference
					// never visibly repeats). The component table is LOCKSTEP with the
					// next flavor's vertex piece (kSwellComponents there):
					//   arg1 =  wp.x*kf                    + ph        weight 0.75
					//   arg2 = (wp.z*1.3  + wp.x*0.4 )*kf  - ph*1.7    weight 0.45
					//   arg3 = (wp.x*0.83 - wp.z*0.62)*kf*2.17 + ph*2.3  weight 0.17
					//   arg4 = (wp.z*0.91 + wp.x*0.47)*kf*3.71 - ph*3.1  weight 0.09
					// (weights sum ~1.46, slopes ~12% over the old two-sine tune -
					// the calm point where the refraction distortion, tuned on the
					// old slopes, keeps the shore edge legible).
					"    float g1 = wp.x * kf + ph;\n"
					"    float g2 = (wp.z * 1.3 + wp.x * 0.4) * kf - ph * 1.7;\n"
					"    float g3 = (wp.x * 0.83 - wp.z * 0.62) * kf * 2.17 + ph * 2.3;\n"
					"    float g4 = (wp.z * 0.91 + wp.x * 0.47) * kf * 3.71 - ph * 3.1;\n"
					"    pos.y += A * (0.75 * sin(g1) + 0.45 * sin(g2)\n"
					"        + 0.17 * sin(g3) + 0.09 * sin(g4));\n"
					// analytic swell SLOPE -> world-space normal: the exact d/dx, d/dz
					// of the displacement above (per component weight*axis-frequency:
					// x: 0.75*1, 0.45*0.4=0.18, 0.17*0.83*2.17=0.3062, 0.09*0.47*3.71
					// =0.1569; z: 0.45*1.3=0.585, -0.17*0.62*2.17=-0.2287,
					// 0.09*0.91*3.71=0.3038 - the same bakes the next piece carries)
					// so the shading rides the swell smoothly per pixel instead of
					// faceting per triangle. Amplitude 0 collapses it to (0,1,0),
					// keeping the flat path stable.
					"    float dYdx = A * kf * (0.75 * cos(g1) + 0.18 * cos(g2)\n"
					"        + 0.3062 * cos(g3) + 0.1569 * cos(g4));\n"
					"    float dYdz = A * kf * (0.585 * cos(g2)\n"
					"        - 0.2287 * cos(g3) + 0.3038 * cos(g4));\n"
					"    vSwellNormal = normalize(vec3(-dYdx, 1.0, -dYdz));\n"
					"    vec4 clip = worldViewProj * pos;\n"
					"    gl_Position = clip;\n"
					"    vClip = clip;\n"
					"    vUv = uv0.xy;\n"
					"    vWorldPos = (world * pos).xyz;\n"
					"}\n"));
				vs->load();
				vs->getDefaultParameters()->setNamedAutoConstant("worldViewProj",
					Ogre::GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX);
				vs->getDefaultParameters()->setNamedAutoConstant("world",
					Ogre::GpuProgramParameters::ACT_WORLD_MATRIX);
			}
			if(!programs.getByName("Orkige/WaterRefract_fs",
				Ogre::RGN_INTERNAL))
			{
				Ogre::HighLevelGpuProgramPtr fs = programs.createProgram(
					"Orkige/WaterRefract_fs", Ogre::RGN_INTERNAL,
					profile.language, Ogre::GPT_FRAGMENT_PROGRAM);
				fs->setSource(profile.fsPreamble + std::string(
					"uniform sampler2D sceneMap;\n"
					"uniform sampler2D normalMap;\n"
					"uniform samplerCube skyMap;\n"  // the live IBL environment chain
					"uniform vec4 deepColour;\n"     // rgb, a = opacity
					"uniform vec4 shallowColour;\n"  // rgb
					"uniform vec4 refractParams;\n"  // x=strength y=waveScale z=scrollX w=scrollY
					"uniform vec4 skyParams;\n"      // x=fresnel F0 y=sky enabled z=luminance
					"uniform vec4 camPos;\n"         // world-space camera position
					"uniform vec4 sunTowards;\n"     // xyz = toward-the-sun, w = specular gate
					"uniform vec4 sunColour;\n"      // rgb = driven sun colour
					"uniform vec4 waterAmbient;\n"   // rgb = upper-hemisphere sky fill (linear-ish, calibrated)
					"in vec2 vUv;\n"
					"in vec4 vClip;\n"
					"in vec3 vWorldPos;\n"
					"in vec3 vSwellNormal;\n"
					"out vec4 fragColour;\n"
					"void main()\n"
					"{\n"
					"    vec2 ndc = vClip.xy / vClip.w;\n"
					"    vec2 screenUv = ndc * 0.5 + 0.5;\n"
					"    screenUv.y = 1.0 - screenUv.y;\n"  // GL render-target V flip
					"    vec2 nuv0 = vUv * refractParams.y + vec2(refractParams.z, refractParams.w);\n"
					"    vec2 nuv1 = vUv * refractParams.y * 1.7 - vec2(refractParams.w, refractParams.z);\n"
					"    vec3 n0 = texture(normalMap, nuv0).xyz * 2.0 - 1.0;\n"
					"    vec3 n1 = texture(normalMap, nuv1).xyz * 2.0 - 1.0;\n"
					"    vec2 disp = (n0.xy + n1.xy * 0.6) * refractParams.x;\n"
					"    vec2 uv = clamp(screenUv + disp + vSwellNormal.xz * 0.12,\n"
					"        vec2(0.002), vec2(0.998));\n"
					"    vec3 scene = texture(sceneMap, uv).rgb;\n"
					// next's HlmsPbs Refractive composition, matched in its NATIVE
					// space: finalColour(LINEAR) = bodyDiffuse + refraction*(1-opacity)
					// + emissive, output = sqrt(finalColour). The critical point is the
					// SPACE: next's refraction source is the LINEAR scene-colour buffer
					// and it adds it PRE-sqrt, so the transmitted scene reads as
					// sqrt(scene_linear*(1-op)) = scene_display*sqrt(1-op) - far more
					// than a display-space scene*(1-op) (at op 0.85, 0.39 vs 0.15). The
					// classic grab is the DISPLAY-space rendered scene and the authored
					// tints are display-space, so square them to linear, compose the
					// whole body in linear, and display-encode ONCE - the missing
					// translucency was the gamma-space compose, not the formula. The
					// body diffuse is the still plane's up-facing response to the driven
					// sun (NdotL on the swell normal) + the same hemisphere sky fill the
					// surface materials get (waterAmbient is already linear), scaled by
					// opacity^2 (next scales the diffuse kD by the transparency squared);
					// the shallow colour is next's depth-scatter emissive.
					"    float op = clamp(deepColour.a, 0.0, 1.0);\n"
					// the BODY diffuse reads off a CALMED normal, not the raw swell:
					// next's water body diffuse is dominated by the near-flat plane
					// (its detail normals feed the specular lobe, not the diffuse
					// response), so its body stays smooth/glassy. The full vSwellNormal
					// here churns the body light/dark across every crest; damping it
					// hard toward straight-up (0,1,0) gives the same glassy body while
					// the sun STREAK below keeps the strong ripple normal (nrm, disp*2.5)
					// that the parity gate's max-luma sparkle depends on.
					"    vec3 bodyNrm = normalize(mix(vSwellNormal, vec3(0.0, 1.0, 0.0), 0.8));\n"
					"    float ndl = max(dot(bodyNrm, normalize(sunTowards.xyz)), 0.0);\n"
					// the GRAZING ENERGY SPLIT, matched to next's HlmsPbs: next weights
					// its whole diffuse/ambient body by the DIFFUSE fresnel (1-F) and
					// its env reflection by the SPECULAR fresnel F (BRDFs_piece:
					// diffuse*fresnelD with fresnelD=1-F, Rs*fresnelS), so at a grazing
					// view the teal body fades and the surface becomes a bright glassy
					// SKY MIRROR - what the eye reads as "real water". Compute F first,
					// then split the energy in LINEAR (next composes linear, sqrt once).
					"    vec3 viewDir = normalize(camPos.xyz - vWorldPos);\n"
					"    vec3 nrm = normalize(vSwellNormal\n"
					"        + vec3(disp.x * 2.5, 0.0, disp.y * 2.5));\n"
					"    vec3 nF = normalize(vec3(\n"
					"        disp.x * 2.5 + vSwellNormal.x * 0.2, 1.0,\n"
					"        disp.y * 2.5 + vSwellNormal.z * 0.2));\n"
					"    float cosv = clamp(dot(viewDir, nF), 0.0, 1.0);\n"
					"    float f0 = clamp(skyParams.x, 0.0, 1.0);\n"
					"    float fres = clamp(f0 + (1.0 - f0) * pow(1.0 - cosv, 5.0), 0.0, 1.0);\n"
					// per-term SPACE: the authored deep/shallow are next's LINEAR
					// albedo/emissive (setDiffuse/setEmissive take them raw), the
					// driven sun colour arrives LINEAR (the atmosphere linkage's
					// classic scale is the linear power itself, matching the
					// generated materials' linear lighting), and waterAmbient is
					// already linear; only the DISPLAY-space grab is squared back
					// to linear (the scene display transfer is sqrt).
					"    vec3 sceneLin = scene * scene;\n"
					"    vec3 deepLin = deepColour.rgb;\n"
					"    vec3 shallowLin = shallowColour.rgb;\n"
					"    vec3 lightLin = waterAmbient.rgb\n"
					"        + sunColour.rgb * (ndl * sunTowards.w);\n"
					// the diffuse body + refracted transmission + scatter, faded by the
					// diffuse fresnel (1-F) as the reflection takes F
					"    vec3 bodyLin = deepLin * lightLin * (op * op)\n"
					"                  + sceneLin * (1.0 - op)\n"
					"                  + shallowLin * 0.18;\n"
					"    vec3 finalLin = bodyLin * (1.0 - fres);\n"
					// the SKY MIRROR takes F: the reflected ray sampled from the live
					// IBL environment cubemap (LINEAR-stored, the same sky next's
					// Refractive water reflects), at a mirror strength that reads as a
					// bright glassy reflection at grazing - not the dim 0.2 diffuse FILL
					// (next reflects the full sky, gated by F, so it never over-brightens
					// the head-on foreground where F is small). Gated on the sky being
					// live (IBL on); with it off the grazing surface keeps the faded
					// body (byte-stable for the IBL-off water demos).
					"    if(skyParams.y > 0.5)\n"
					"    {\n"
					// reflect off a CALM normal, not the strong crest normal (nrm,
					// disp*2.5) the streak needs: the shared sky model IS warm at the
					// sun-side horizon (measured), but the strong ripple perturbation
					// over-scatters the reflect ray up into the cooler green/blue sky
					// and abs(refl.y) folds the downward rays up too - averaging to the
					// grey-green cast. next mirrors the sharp warm horizon (roughness
					// 0.16); a gently-perturbed normal here samples that same warm
					// gradient, so the grazing surface reads warm like next.
					"        vec3 reflNrm = normalize(vSwellNormal\n"
					"            + vec3(disp.x * 0.5, 0.0, disp.y * 0.5));\n"
					"        vec3 refl = reflect(-viewDir, reflNrm);\n"
					// bias the reflected ray toward the low horizon: a calm water
					// surface at a grazing view mirrors the WARM sun-side HORIZON (R>G,
					// e.g. 248,239,124), but the raw reflect + abs(y) points higher into
					// the pale yellow-white / green upper sky, so the warm gradient next
					// mirrors was lost. Compressing y toward the horizon samples that
					// warm band. (abs keeps it above the horizon - water reflects sky,
					// never ground.)
					"        refl = normalize(vec3(refl.x, abs(refl.y) * 0.35, refl.z));\n"
					"        vec3 skyLin = max(texture(skyMap, refl).rgb, vec3(0.0))\n"
					"            * 0.30;\n"
					"        finalLin += skyLin * fres;\n"
					"    }\n"
					"    vec3 water = sqrt(max(finalLin, vec3(0.0)));\n"
					// the sun's specular streak riding the ripples (the same cue the
					// PBS flavor's glossy lobe gives its refractive water); the add
					// happens in DISPLAY space, so the linear sun enters through the
					// same sqrt display transfer the body went through
					"    vec3 halfVec = normalize(viewDir + normalize(sunTowards.xyz));\n"
					"    float spec = pow(clamp(dot(nrm, halfVec), 0.0, 1.0), 420.0);\n"
					"    water += sqrt(max(sunColour.rgb, vec3(0.0)))\n"
					"        * (spec * 1.0 * sunTowards.w);\n"
					"    fragColour = vec4(water, 1.0);\n"
					"}\n"));
				fs->load();
				fs->getDefaultParameters()->setNamedAutoConstant("camPos",
					Ogre::GpuProgramParameters::ACT_CAMERA_POSITION);
			}
		}
		//! @brief pins a programmed water technique to the viewport's RTSS scheme.
		//! The window viewport (and the grab/mirror render targets) render under
		//! ShaderGenerator::DEFAULT_SCHEME_NAME. Our refraction/reflection water
		//! carries EXPLICIT vertex/fragment programs on its default-scheme technique
		//! - a technique RTSS does not manage. Left on the "Default" scheme, the
		//! first render finds no technique matching the viewport scheme, so RTSS's
		//! scheme-not-found handler CLONES the technique into an RTSS-scheme copy and
		//! draws THAT clone; the per-frame parameter pushes (the ripple scroll and
		//! the geometric swell clock) target the original technique and never reach
		//! the rendered clone, freezing the water at its clone-time snapshot.
		//! Labelling the technique with the viewport scheme makes it the matched
		//! technique, so no clone is made and the per-frame pushes drive the drawn
		//! pass directly. RTSS still ignores the material (it manages only techniques
		//! it generated), so the explicit programs render verbatim.
		void pinWaterTechniqueToViewportScheme(Ogre::MaterialPtr const & material)
		{
			if(Ogre::RTShader::ShaderGenerator::getSingletonPtr())
			{
				material->getTechnique(0)->setSchemeName(
					Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
			}
		}
		//---------------------------------------------------------
		//! push the per-instance water body colour + refraction knobs onto a
		//! refractive water material's fragment program (create + per-knob update)
		void applyRefractionParams(Ogre::Pass* pass, RenderWaterDesc const & desc,
			float scrollX, float scrollY)
		{
			Ogre::GpuProgramParametersSharedPtr params =
				pass->getFragmentProgramParameters();
			params->setIgnoreMissingParams(true);
			params->setNamedConstant("deepColour", Ogre::Vector4(
				desc.deepColour.r, desc.deepColour.g, desc.deepColour.b,
				std::clamp(desc.opacity, 0.0f, 1.0f)));
			params->setNamedConstant("shallowColour", Ogre::Vector4(
				desc.shallowColour.r, desc.shallowColour.g,
				desc.shallowColour.b, 1.0f));
			params->setNamedConstant("refractParams", Ogre::Vector4(
				std::max(desc.refractionStrength, 0.0f),
				std::max(desc.waveScale, 0.001f), scrollX, scrollY));
			// an initial ambient fill so a not-yet-ticked surface (editor / frame
			// 0) lights its body; the per-frame setWaterMaterialTime re-pushes the
			// live hemisphere as the atmosphere animates
			Ogre::ColourValue upper(0.2f, 0.2f, 0.2f, 1.0f);
#ifdef USE_RTSHADER_SYSTEM
			Ogre::ColourValue lower;
			hemisphereAmbientColours(upper, lower);
#endif
			params->setNamedConstant("waterAmbient",
				Ogre::Vector4(upper.r, upper.g, upper.b, 1.0f));
		}
		//! the shared water-REFLECTION GLSL programs (GL3Plus), created once. The
		//! fragment program samples the mirror render target at the fragment's
		//! ripple-perturbed screen UV and blends it over the base water look by the
		//! reflection strength; when refraction is ALSO on it composes the two (the
		//! refracted scene becomes the base the reflection sits over). Authored
		//! inline (not a material script), confined to this backend.
		void ensureReflectionPrograms()
		{
			if(gReflectionProgramsBuilt)
			{
				return;
			}
			gReflectionProgramsBuilt = true;
			const WaterGlslProfile profile = waterGlslProfile();
			Ogre::HighLevelGpuProgramManager & programs =
				Ogre::HighLevelGpuProgramManager::getSingleton();
			// the vertex program is identical to the refraction one (clip pos for
			// the screen UV + the plane UV for the ripple) - reuse it
			ensureRefractionPrograms();
			if(!programs.getByName("Orkige/WaterReflect_fs", Ogre::RGN_INTERNAL))
			{
				Ogre::HighLevelGpuProgramPtr fs = programs.createProgram(
					"Orkige/WaterReflect_fs", Ogre::RGN_INTERNAL,
					profile.language, Ogre::GPT_FRAGMENT_PROGRAM);
				fs->setSource(profile.fsPreamble + std::string(
					"uniform sampler2D reflectMap;\n"  // the mirror render target
					"uniform sampler2D normalMap;\n"
					"uniform sampler2D sceneMap;\n"    // the refraction grab (dummy when off)
					"uniform vec4 deepColour;\n"       // rgb, a = opacity
					"uniform vec4 shallowColour;\n"    // rgb
					"uniform vec4 refractParams;\n"    // x=refractStrength y=waveScale z=scrollX w=scrollY
					"uniform vec4 reflectParams;\n"    // x=fresnel F0 y=refractEnabled z=bodyDim
					"uniform vec4 camPos;\n"           // world-space camera position
					"uniform vec4 sunTowards;\n"       // xyz = toward-the-sun (world), w = specular gate
					"uniform vec4 sunColour;\n"        // rgb = driven sun colour
					"in vec2 vUv;\n"
					"in vec4 vClip;\n"
					"in vec3 vWorldPos;\n"
					"in vec3 vSwellNormal;\n"
					"out vec4 fragColour;\n"
					"void main()\n"
					"{\n"
					"    vec2 ndc = vClip.xy / vClip.w;\n"
					"    vec2 screenUv = ndc * 0.5 + 0.5;\n"
					"    screenUv.y = 1.0 - screenUv.y;\n"  // GL render-target V flip
					"    vec2 nuv0 = vUv * refractParams.y + vec2(refractParams.z, refractParams.w);\n"
					"    vec2 nuv1 = vUv * refractParams.y * 1.7 - vec2(refractParams.w, refractParams.z);\n"
					"    vec3 n0 = texture(normalMap, nuv0).xyz * 2.0 - 1.0;\n"
					"    vec3 n1 = texture(normalMap, nuv1).xyz * 2.0 - 1.0;\n"
					"    vec2 disp = (n0.xy + n1.xy * 0.6);\n"
					// the base look: the refracted scene when refraction composes,
					// else the water body tint
					"    vec3 base;\n"
					"    if(reflectParams.y > 0.5)\n"
					"    {\n"
					"        vec2 suv = clamp(screenUv + disp * refractParams.x\n"
					"            + vSwellNormal.xz * 0.12, vec2(0.002), vec2(0.998));\n"
					"        vec3 scene = texture(sceneMap, suv).rgb;\n"
					"        base = mix(scene, deepColour.rgb, deepColour.a * 0.6)\n"
					"             + shallowColour.rgb * 0.12;\n"
					"    }\n"
					"    else\n"
					"    {\n"
					"        base = mix(deepColour.rgb, shallowColour.rgb, 0.35);\n"
					"    }\n"
					// body-dim: a stronger reflection dims the base so the mirror
					// stays readable (the next flavor's albedo scale sibling)
					"    base *= clamp(reflectParams.z, 0.0, 1.0);\n"
					// the mirror image, sampled at the fragment screen UV with a
					// small ripple perturbation (the reflection camera shares the
					// main projection, so the same-screen sample aligns the mirror)
					"    vec2 ruv = clamp(screenUv + disp * 0.03\n"
					"        + vSwellNormal.xz * 0.06, vec2(0.002), vec2(0.998));\n"
					"    vec3 reflectCol = texture(reflectMap, ruv).rgb;\n"
					// Schlick fresnel against the ripple-tilted surface normal:
					// looking DOWN shows the water body/refracted scene, grazing
					// shows the mirror - the same modulation the next flavor's PBS
					// applies natively, so the two flavors read alike.
					// reflectParams.x carries the PRE-COMPUTED F0 (base water F0 +
					// the reflectionStrength boost - @see applyReflectionParams).
					"    vec3 viewDir = normalize(camPos.xyz - vWorldPos);\n"
					"    vec3 nrm = normalize(vSwellNormal\n"
					"        + vec3(disp.x * 0.15, 0.0, disp.y * 0.15));\n"
					"    vec3 nF = normalize(vec3(\n"
					"        disp.x * 0.15 + vSwellNormal.x * 0.2, 1.0,\n"
					"        disp.y * 0.15 + vSwellNormal.z * 0.2));\n"
					"    float cosv = clamp(dot(viewDir, nF), 0.0, 1.0);\n"
					"    float f0 = clamp(reflectParams.x, 0.0, 1.0);\n"
					"    float fres = f0 + (1.0 - f0) * pow(1.0 - cosv, 5.0);\n"
					"    vec3 outc = mix(base, reflectCol, clamp(fres, 0.0, 1.0));\n"
					// the sun's specular streak riding the ripples (the signature
					// low-sun water cue the PBS flavor gets from its glossy lobe):
					// Blinn half-vector against the ripple-tilted normal, the tight
					// lobe elongating naturally over the perturbed surface. The sun
					// direction/colour are pushed per frame (@see setWaterMaterialTime);
					// the colour arrives LINEAR and this composition is DISPLAY-space,
					// so it enters through the sqrt display transfer
					"    vec3 halfVec = normalize(viewDir + normalize(sunTowards.xyz));\n"
					"    float spec = pow(clamp(dot(nrm, halfVec), 0.0, 1.0), 420.0);\n"
					"    outc += sqrt(max(sunColour.rgb, vec3(0.0)))\n"
					"        * (spec * 1.0 * sunTowards.w);\n"
					"    fragColour = vec4(outc, 1.0);\n"
					"}\n"));
				fs->load();
				fs->getDefaultParameters()->setNamedAutoConstant("camPos",
					Ogre::GpuProgramParameters::ACT_CAMERA_POSITION);
			}
		}
		//! push the water body colour + refraction/reflection knobs onto a
		//! reflective water material's fragment program (create + per-scroll update)
		void applyReflectionParams(Ogre::Pass* pass, RenderWaterDesc const & desc,
			bool refractComposed, float scrollX, float scrollY)
		{
			Ogre::GpuProgramParametersSharedPtr params =
				pass->getFragmentProgramParameters();
			params->setIgnoreMissingParams(true);
			params->setNamedConstant("deepColour", Ogre::Vector4(
				desc.deepColour.r, desc.deepColour.g, desc.deepColour.b,
				std::clamp(desc.opacity, 0.0f, 1.0f)));
			params->setNamedConstant("shallowColour", Ogre::Vector4(
				desc.shallowColour.r, desc.shallowColour.g,
				desc.shallowColour.b, 1.0f));
			params->setNamedConstant("refractParams", Ogre::Vector4(
				std::max(desc.refractionStrength, 0.0f),
				std::max(desc.waveScale, 0.001f), scrollX, scrollY));
			// reflectParams.x = the fresnel F0 the program's Schlick term rides:
			// the physical water F0 (0.02, scaled by fresnelPower like the base
			// water look) plus a modest reflectionStrength boost. reflectParams.z
			// = the BODY-dim scale (a higher strength dims the base so the
			// mirrored scene stays readable over a bright body). BOTH are the
			// SAME formulas the next flavor applies to its PBS fresnel/albedo,
			// so the two flavors' reflection reads alike
			// (@see createOrUpdateWaterDatablock)
			const float baseF0 = std::clamp(
				0.02f * std::max(desc.fresnelPower, 0.0f), 0.0f, 0.2f);
			const float strength = std::clamp(desc.reflectionStrength, 0.0f, 1.0f);
			const float f0 = std::clamp(baseF0 + strength * 0.12f, 0.02f, 0.3f);
			const float baseScale = 1.0f - strength * 0.35f;
			params->setNamedConstant("reflectParams", Ogre::Vector4(
				f0, refractComposed ? 1.0f : 0.0f, baseScale, 0.0f));
		}
		//! push the water body's ambient FILL - the upper-hemisphere sky colour
		//! the generated surface materials already receive - onto a water program,
		//! so the hand-written water pass lights its diffuse body at the SAME
		//! calibrated ambient level instead of unlit (@see the water FS bodyLit).
		//! Without the RTSS hemisphere (GLES2/no-RTSS floor) it falls back to a
		//! neutral mid fill so the body still reads.
		void pushWaterAmbient(Ogre::GpuProgramParametersSharedPtr const & params)
		{
			Ogre::ColourValue upper(0.2f, 0.2f, 0.2f, 1.0f);
#ifdef USE_RTSHADER_SYSTEM
			Ogre::ColourValue lower;
			hemisphereAmbientColours(upper, lower);
#endif
			params->setNamedConstant("waterAmbient",
				Ogre::Vector4(upper.r, upper.g, upper.b, 1.0f));
		}
		//! the 1x1 black cube bound to the refract program's sky unit while
		//! image lighting is off (GL validates the samplerCube binding even
		//! when the shader's sky branch is dynamically skipped); created once
		Ogre::TexturePtr ensureWaterSkyFallbackCube()
		{
			const char* const kName = "Orkige/WaterSkyFallback";
			Ogre::TextureManager & textures = Ogre::TextureManager::getSingleton();
			if(Ogre::TexturePtr existing = textures.getByName(kName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
			{
				return existing;
			}
			Ogre::TexturePtr cube = textures.createManual(kName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
				Ogre::TEX_TYPE_CUBE_MAP, 1u, 1u, 0, Ogre::PF_BYTE_RGBA,
				Ogre::TU_DEFAULT);
			unsigned char black[4] = { 0, 0, 0, 255 };
			for(size_t face = 0; face < 6; ++face)
			{
				Ogre::PixelBox box(1u, 1u, 1u, Ogre::PF_BYTE_RGBA, black);
				cube->getBuffer(face, 0)->blitFromMemory(box);
			}
			return cube;
		}
	}
	//---------------------------------------------------------
	// the water-hide, out-of-line (the mirror-camera sync needs the protected
	// RenderSystem::mImpl, so it lives in a RenderBackend friend method the
	// listener calls); same anonymous namespace as the ReflectionListener
	// declaration, so the definition binds
	namespace
	{
		void ReflectionListener::preRenderTargetUpdate(
			const Ogre::RenderTargetEvent&)
		{
			this->mHidden.clear();
			// hide every custom water surface: the mirror must not contain the
			// water plane itself (it reflects the scene ABOVE the surface)
			for(Ogre::Entity* entity : gWaterHideEntities)
			{
				this->mHidden.emplace_back(entity, entity->getVisible());
				entity->setVisible(false);
			}
			// sync the mirror camera to the main camera reflected across the plane
			RenderBackend::updateReflectionCamera();
		}
	}
	//---------------------------------------------------------
	void RenderBackend::updateReflectionCamera()
	{
		RenderSystem* system = RenderBackend::system();
		if(!gMirrorCamera || !gMirrorNode || !system || !system->mImpl->engine)
		{
			return;
		}
		Ogre::RenderWindow* window = system->mImpl->engine->getRenderWindow(0);
		if(!window || window->getNumViewports() == 0)
		{
			return;
		}
		Ogre::Camera* main = window->getViewport(0)->getCamera();
		if(!main)
		{
			return;
		}
		// the mirror's "sky" is its clear colour: track the window background
		// every frame (the atmosphere animates it - a sunset must not leave the
		// mirror clearing to a stale colour, which read as black water)
		if(gReflectionTexture)
		{
			Ogre::RenderTarget* target =
				gReflectionTexture->getBuffer()->getRenderTarget();
			if(target->getNumViewports() > 0)
			{
				target->getViewport(0)->setBackgroundColour(
					window->getViewport(0)->getBackgroundColour());
			}
		}
		// OGRE positions cameras through a node: drive the mirror node from the
		// main camera's world (real) pose, then reflect the camera across the water
		// plane (normal +Y at gReflectionPlaneY) and custom-near-clip so nothing
		// below the water leaks into the mirror
		gMirrorNode->setPosition(main->getRealPosition());
		gMirrorNode->setOrientation(main->getRealOrientation());
		gMirrorCamera->setNearClipDistance(main->getNearClipDistance());
		gMirrorCamera->setFarClipDistance(main->getFarClipDistance());
		gMirrorCamera->setFOVy(main->getFOVy());
		gMirrorCamera->setAspectRatio(main->getAspectRatio());
		Ogre::Plane plane(Ogre::Vector3::UNIT_Y, gReflectionPlaneY);
		gMirrorCamera->enableReflection(plane);
		gMirrorCamera->enableCustomNearClipPlane(plane);
	}
	//---------------------------------------------------------
	void RenderBackend::updateSceneGrabViewport()
	{
		RenderSystem* system = RenderBackend::system();
		if(!gSceneGrabTexture || !system || !system->mImpl->engine)
		{
			return;
		}
		Ogre::RenderWindow* window = system->mImpl->engine->getRenderWindow(0);
		if(!window || window->getNumViewports() == 0)
		{
			return;
		}
		Ogre::RenderTarget* target =
			gSceneGrabTexture->getBuffer()->getRenderTarget();
		if(target->getNumViewports() > 0)
		{
			target->getViewport(0)->setBackgroundColour(
				window->getViewport(0)->getBackgroundColour());
		}
	}
	//---------------------------------------------------------
	bool RenderBackend::isSceneCaptureCamera(Ogre::Camera const * camera)
	{
		if(!camera)
		{
			return false;
		}
		if(camera == gMirrorCamera)
		{
			return true;
		}
		// the refraction grab renders through the WINDOW camera (a shadow
		// camera is a distinct object, so it stays excluded)
		RenderSystem* system = RenderBackend::system();
		if(system && system->mImpl->engine)
		{
			Ogre::RenderWindow* window =
				system->mImpl->engine->getRenderWindow(0);
			if(window && window->getNumViewports() > 0 &&
				window->getViewport(0)->getCamera() == camera)
			{
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	void RenderBackend::ensureSceneGrabTexture()
	{
		RenderSystem* system = RenderBackend::system();
		if(!system || !system->mImpl->engine)
		{
			return;
		}
		Ogre::RenderWindow* window = system->mImpl->engine->getRenderWindow(0);
		if(!window || window->getNumViewports() == 0)
		{
			return;
		}
		Ogre::Viewport* mainViewport = window->getViewport(0);
		Ogre::Camera* camera = mainViewport->getCamera();
		if(!camera)
		{
			return;
		}
		const unsigned int width = static_cast<unsigned int>(
			mainViewport->getActualWidth());
		const unsigned int height = static_cast<unsigned int>(
			mainViewport->getActualHeight());
		if(width == 0 || height == 0)
		{
			return;
		}
		if(gSceneGrabTexture && gSceneGrabWidth == width &&
			gSceneGrabHeight == height)
		{
			return;	// already the right size
		}
		if(gSceneGrabTexture)
		{
			Ogre::RenderTarget* old =
				gSceneGrabTexture->getBuffer()->getRenderTarget();
			old->removeAllListeners();
			old->removeAllViewports();
			Ogre::TextureManager::getSingleton().remove(gSceneGrabTexture);
			gSceneGrabTexture.reset();
		}
		gSceneGrabTexture = Ogre::TextureManager::getSingleton().createManual(
			kSceneGrabTexture, Ogre::RGN_INTERNAL, Ogre::TEX_TYPE_2D,
			width, height, 0, Ogre::PF_BYTE_RGB, Ogre::TU_RENDERTARGET);
		Ogre::RenderTarget* target =
			gSceneGrabTexture->getBuffer()->getRenderTarget();
		Ogre::Viewport* viewport = target->addViewport(camera);
		viewport->setClearEveryFrame(true);
		viewport->setBackgroundColour(mainViewport->getBackgroundColour());
		viewport->setOverlaysEnabled(false);
		// the grab captures the same lit scene the window shows; keep shadows
		// enabled so an armed integrated technique never renders receiver
		// shaders against stale projectors (@see RenderTexture applyViewportState)
		viewport->setShadowsEnabled(true);
		RenderBackend::applyRTSSScheme(viewport);
		target->addListener(&gSceneGrabListener);
		// auto-updated render targets render BEFORE the window each frame, so
		// the grab of the opaque (water-hidden) scene is ready to sample
		target->setAutoUpdated(true);
		gSceneGrabWidth = width;
		gSceneGrabHeight = height;
	}
	//---------------------------------------------------------
	void RenderBackend::destroySceneGrabTexture()
	{
		if(!gSceneGrabTexture)
		{
			return;
		}
		Ogre::RenderTarget* target =
			gSceneGrabTexture->getBuffer()->getRenderTarget();
		target->removeAllListeners();
		target->removeAllViewports();
		Ogre::TextureManager::getSingleton().remove(gSceneGrabTexture);
		gSceneGrabTexture.reset();
		gSceneGrabWidth = 0;
		gSceneGrabHeight = 0;
	}
	//---------------------------------------------------------
	void RenderBackend::ensureReflectionTexture(float planeHeightY)
	{
		RenderSystem* system = RenderBackend::system();
		if(!system || !system->mImpl->engine)
		{
			return;
		}
		Ogre::SceneManager* sceneManager =
			system->mImpl->engine->getSceneManager();
		Ogre::RenderWindow* window = system->mImpl->engine->getRenderWindow(0);
		if(!sceneManager || !window || window->getNumViewports() == 0)
		{
			return;
		}
		Ogre::Viewport* mainViewport = window->getViewport(0);
		const unsigned int width = static_cast<unsigned int>(
			mainViewport->getActualWidth());
		const unsigned int height = static_cast<unsigned int>(
			mainViewport->getActualHeight());
		if(width == 0 || height == 0)
		{
			return;
		}
		// the plane the mirror reflects across (the ReflectionListener reads it)
		gReflectionPlaneY = planeHeightY;
		gReflectionPlaneSet = true;
		if(gReflectionTexture && gReflectionWidth == width &&
			gReflectionHeight == height)
		{
			return;	// already the right size (the plane Y updated above)
		}
		if(gReflectionTexture)
		{
			Ogre::RenderTarget* old =
				gReflectionTexture->getBuffer()->getRenderTarget();
			old->removeAllListeners();
			old->removeAllViewports();
			Ogre::TextureManager::getSingleton().remove(gReflectionTexture);
			gReflectionTexture.reset();
		}
		// the dedicated mirror camera on its own node (OGRE positions cameras
		// through a node); the node's transform is driven each frame from the main
		// camera and the camera reflected in updateReflectionCamera
		if(!gMirrorCamera)
		{
			gMirrorCamera = sceneManager->createCamera(kMirrorCamera);
			gMirrorCamera->setAutoAspectRatio(false);
			gMirrorNode = sceneManager->getRootSceneNode()
				->createChildSceneNode();
			gMirrorNode->attachObject(gMirrorCamera);
		}
		gReflectionTexture = Ogre::TextureManager::getSingleton().createManual(
			kReflectionTexture, Ogre::RGN_INTERNAL, Ogre::TEX_TYPE_2D,
			width, height, 0, Ogre::PF_BYTE_RGB, Ogre::TU_RENDERTARGET);
		Ogre::RenderTarget* target =
			gReflectionTexture->getBuffer()->getRenderTarget();
		Ogre::Viewport* viewport = target->addViewport(gMirrorCamera);
		viewport->setClearEveryFrame(true);
		// the mirror shows the sky + scene ABOVE the water: clear to the window's
		// sky/background colour so an empty reflection reads as sky, not black
		viewport->setBackgroundColour(mainViewport->getBackgroundColour());
		viewport->setOverlaysEnabled(false);
		// keep shadows enabled for the same reason the grab does (an armed
		// integrated technique must not render receivers against stale projectors)
		viewport->setShadowsEnabled(true);
		RenderBackend::applyRTSSScheme(viewport);
		target->addListener(&gReflectionListener);
		// render BEFORE the window each frame, so the mirror is ready to sample
		target->setAutoUpdated(true);
		gReflectionWidth = width;
		gReflectionHeight = height;
	}
	//---------------------------------------------------------
	void RenderBackend::destroyReflectionTexture()
	{
		if(gReflectionTexture)
		{
			Ogre::RenderTarget* target =
				gReflectionTexture->getBuffer()->getRenderTarget();
			target->removeAllListeners();
			target->removeAllViewports();
			Ogre::TextureManager::getSingleton().remove(gReflectionTexture);
			gReflectionTexture.reset();
		}
		gReflectionWidth = 0;
		gReflectionHeight = 0;
		gReflectionPlaneSet = false;
		// the mirror camera + its node are owned by the scene manager - destroy
		// them via the manager (NULL-safe: only when a scene manager is alive)
		if(gMirrorCamera || gMirrorNode)
		{
			RenderSystem* system = RenderBackend::system();
			Ogre::SceneManager* sceneManager =
				(system && system->mImpl->engine)
					? system->mImpl->engine->getSceneManager() : NULL;
			if(sceneManager)
			{
				if(gMirrorNode)
				{
					gMirrorNode->detachAllObjects();
					sceneManager->destroySceneNode(gMirrorNode);
				}
				if(gMirrorCamera)
				{
					sceneManager->destroyCamera(gMirrorCamera);
				}
			}
			gMirrorNode = NULL;
			gMirrorCamera = NULL;
		}
	}
	//---------------------------------------------------------
	void RenderBackend::refractionTeardown()
	{
		destroySceneGrabTexture();
		gRefractiveWaterMaterials.clear();
		gRefractiveWaterKnobs.clear();
		gSceneGrabListener.mHidden.clear();
		// planar reflection shares the water-hide set + teardown boundary
		destroyReflectionTexture();
		gReflectiveWaterMaterials.clear();
		gReflectiveWaterKnobs.clear();
		gReflectionListener.mHidden.clear();
		gWaterHideEntities.clear();
	}
	//---------------------------------------------------------
	void RenderBackend::noteMeshMaterialForRefraction(Ogre::Entity* entity,
		String const & materialName)
	{
		if(!entity)
		{
			return;
		}
		// a mesh wearing ANY custom water material (refractive OR reflective) must
		// be hidden from the grab it refracts AND the mirror it reflects
		const bool custom =
			gRefractiveWaterMaterials.find(materialName) !=
				gRefractiveWaterMaterials.end() ||
			gReflectiveWaterMaterials.find(materialName) !=
				gReflectiveWaterMaterials.end();
		if(custom)
		{
			gWaterHideEntities.insert(entity);
		}
		else
		{
			gWaterHideEntities.erase(entity);
			if(gRefractiveWaterMaterials.empty())
			{
				destroySceneGrabTexture();
			}
			if(gReflectiveWaterMaterials.empty())
			{
				destroyReflectionTexture();
			}
		}
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
		// the water body colour: mirror the next flavor's split so the two
		// backends read the SAME colour instead of a collapsed tint. The DEEP
		// colour is the diffuse body (alpha = opacity so the lakebed shows
		// through), and the SHALLOW colour rides as a subtle scatter self-
		// illumination at the SAME 0.18 weight the HlmsPbs water uses for its
		// emissive - an honest stand-in for depth-graded transmission until a
		// refraction pass lands. The metal-rough lighting below adds the sun
		// reflection.
		const float scatter = 0.18f;
		const Ogre::ColourValue diffuse(desc.deepColour.r, desc.deepColour.g,
			desc.deepColour.b, std::clamp(desc.opacity, 0.0f, 1.0f));
		// water is a per-instance material, so the surface's receive flag maps
		// 1:1 (@see RenderWaterDesc::receiveShadows); water never casts - the
		// component turns its plane's caster flag off
		material->setReceiveShadows(desc.receiveShadows);
		pass->setLightingEnabled(true);
		pass->setDiffuse(diffuse);			// alpha carries the opacity
		pass->setAmbient(desc.deepColour.r, desc.deepColour.g,
			desc.deepColour.b);
		pass->setSelfIllumination(desc.shallowColour.r * scatter,
			desc.shallowColour.g * scatter, desc.shallowColour.b * scatter);
		// transparent surface: alpha-blend over the scene, no depth write (the
		// lakebed shows through)
		pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
		pass->setDepthWriteEnabled(false);
		pass->removeAllTextureUnitStates();
		gNormalMappedMaterials.erase(name);	// a re-create may have been mapped
		// --- opt-in planar (mirror-of-scene) reflection ---
		// When planar reflection is requested AND supported, the surface renders
		// through a programmable pass that samples the MIRROR render target (the
		// scene reflected across the surface plane, water hidden) at the fragment's
		// ripple-perturbed screen UV and blends it over the base look. It composes
		// with screen-space refraction: when refraction is also on the refracted
		// scene becomes the base the reflection sits over. This REPLACES the
		// RTSS-lit water below; off/unsupported -> the byte-stable Stage-1 look.
		if(desc.planarReflection &&
			RenderBackend::screenSpacePlanarReflectionSupported() &&
			!desc.normalTexture.empty())
		{
			const bool composeRefract = desc.screenSpaceRefraction &&
				RenderBackend::screenSpaceRefractionSupported();
			try
			{
				Ogre::TexturePtr normal = Ogre::TextureManager::getSingleton()
					.load(RenderBackend::resolveTextureResourceName(
						desc.normalTexture),
						Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
				ensureReflectionPrograms();
				ensureReflectionTexture(desc.planeHeightY);
				if(composeRefract)
				{
					ensureSceneGrabTexture();
				}
				// a plain opaque programmable pass: we composite the reflection (and
				// optional refraction) ourselves, so no alpha blend, depth writes on
				pass->setLightingEnabled(false);
				pass->setSceneBlending(Ogre::SBT_REPLACE);
				pass->setDepthWriteEnabled(true);
				pass->setDepthCheckEnabled(true);
				pass->setVertexProgram("Orkige/WaterRefract_vs");
				pass->setFragmentProgram("Orkige/WaterReflect_fs");
				// TU 0 = the mirror, TU 1 = the ripple normal, TU 2 = the refraction
				// grab (or the mirror again as a harmless dummy when not composing)
				Ogre::TextureUnitState* reflectUnit =
					pass->createTextureUnitState();
				reflectUnit->setTextureName(kReflectionTexture);
				reflectUnit->setTextureAddressingMode(
					Ogre::TextureUnitState::TAM_CLAMP);
				reflectUnit->setTextureFiltering(Ogre::TFO_BILINEAR);
				Ogre::TextureUnitState* normalUnit =
					pass->createTextureUnitState();
				normalUnit->setTexture(normal);
				normalUnit->setTextureAddressingMode(
					Ogre::TextureUnitState::TAM_WRAP);
				normalUnit->setTextureFiltering(Ogre::TFO_BILINEAR);
				Ogre::TextureUnitState* sceneUnit =
					pass->createTextureUnitState();
				sceneUnit->setTextureName(composeRefract
					? kSceneGrabTexture : kReflectionTexture);
				sceneUnit->setTextureAddressingMode(
					Ogre::TextureUnitState::TAM_CLAMP);
				sceneUnit->setTextureFiltering(Ogre::TFO_BILINEAR);
				pass->getFragmentProgramParameters()->setNamedConstant(
					"reflectMap", 0);
				pass->getFragmentProgramParameters()->setNamedConstant(
					"normalMap", 1);
				pass->getFragmentProgramParameters()->setNamedConstant(
					"sceneMap", 2);
				applyReflectionParams(pass, desc, composeRefract, 0.0f, 0.0f);
				gReflectiveWaterMaterials.insert(name);
				ReflectKnobs knobs;
				// stored PRE-COMPUTED F0 (the same formula applyReflectionParams
				// pushes) - the per-frame scroll re-push sends it verbatim
				knobs.reflectStrength = std::clamp(
					std::clamp(0.02f * std::max(desc.fresnelPower, 0.0f),
						0.0f, 0.2f) +
					std::clamp(desc.reflectionStrength, 0.0f, 1.0f) * 0.12f,
					0.02f, 0.3f);
				knobs.baseScale = 1.0f -
					std::clamp(desc.reflectionStrength, 0.0f, 1.0f) * 0.35f;
				knobs.waveHeight = std::max(desc.waveHeight, 0.0f);
				knobs.waveScale = std::max(desc.waveScale, 0.001f);
				knobs.refractStrength = std::max(desc.refractionStrength, 0.0f);
				knobs.refractEnabled = composeRefract ? 1.0f : 0.0f;
				gReflectiveWaterKnobs[name] = knobs;
				// track the grab lifecycle when composing (keeps the grab alive as
				// long as a composed reflective surface needs it)
				if(composeRefract)
				{
					gRefractiveWaterMaterials.insert(name);
				}
				else
				{
					gRefractiveWaterMaterials.erase(name);
					gRefractiveWaterKnobs.erase(name);
				}
				gWaterScrollSpeeds[name] = desc.waveSpeed;
				pinWaterTechniqueToViewportScheme(material);
				return material;
			}
			catch(Ogre::Exception const & e)
			{
				oDebugError("engine", 0, "RenderSystem::createWaterMaterial('"
					<< name << "'): planar reflection setup failed: "
					<< e.getDescription() << " - falling back to the plain surface");
				outComplete = false;
				// fall through to the standard water path below
			}
		}
		// not (or no longer) a reflective water material: drop it from the set so
		// the mirror target hides only the surfaces that actually reflect
		gReflectiveWaterMaterials.erase(name);
		gReflectiveWaterKnobs.erase(name);
		// --- opt-in screen-space refraction (grab-pass) ---
		// When refraction is requested AND supported, the surface renders through
		// a programmable pass that samples the scene GRABBED behind it (water
		// hidden) at a normal-perturbed screen UV - basic distortion, so what sits
		// under the water bends/wobbles. This REPLACES the RTSS-lit water below
		// (basic distortion, not depth-graded transmission - @see RenderWaterDesc).
		// Off/unsupported -> falls through to the byte-stable Stage-1 look.
		if(desc.screenSpaceRefraction &&
			RenderBackend::screenSpaceRefractionSupported() &&
			!desc.normalTexture.empty())
		{
			try
			{
				Ogre::TexturePtr normal = Ogre::TextureManager::getSingleton()
					.load(RenderBackend::resolveTextureResourceName(
						desc.normalTexture),
						Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
				ensureRefractionPrograms();
				ensureSceneGrabTexture();
				// a plain opaque programmable pass: we composite the scene
				// ourselves, so no alpha blend and depth writes on (a solid
				// surface over the lakebed)
				pass->setLightingEnabled(false);
				pass->setSceneBlending(Ogre::SBT_REPLACE);
				pass->setDepthWriteEnabled(true);
				pass->setDepthCheckEnabled(true);
				pass->setVertexProgram("Orkige/WaterRefract_vs");
				pass->setFragmentProgram("Orkige/WaterRefract_fs");
				// TU 0 = the scene grab (screen-space), TU 1 = the ripple normal,
				// TU 2 = the live IBL environment cubemap for the fresnel sky
				// reflection (a 1x1 black fallback cube while image lighting is
				// off - the per-frame update rebinds when the live state moves)
				Ogre::TextureUnitState* sceneUnit = pass->createTextureUnitState();
				sceneUnit->setTextureName(kSceneGrabTexture);
				sceneUnit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);
				sceneUnit->setTextureFiltering(Ogre::TFO_BILINEAR);
				Ogre::TextureUnitState* normalUnit = pass->createTextureUnitState();
				normalUnit->setTexture(normal);
				normalUnit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_WRAP);
				normalUnit->setTextureFiltering(Ogre::TFO_BILINEAR);
				Ogre::TextureUnitState* skyUnit = pass->createTextureUnitState();
				skyUnit->setTexture(ensureWaterSkyFallbackCube());
				skyUnit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);
				skyUnit->setTextureFiltering(Ogre::TFO_TRILINEAR);
				pass->getFragmentProgramParameters()->setNamedConstant(
					"sceneMap", 0);
				pass->getFragmentProgramParameters()->setNamedConstant(
					"normalMap", 1);
				pass->getFragmentProgramParameters()->setNamedConstant(
					"skyMap", 2);
				applyRefractionParams(pass, desc, 0.0f, 0.0f);
				gRefractiveWaterMaterials.insert(name);
				RefractKnobs refractKnobs;
				refractKnobs.strength = std::max(desc.refractionStrength, 0.0f);
				refractKnobs.waveScale = std::max(desc.waveScale, 0.001f);
				// the same F0 formula as the reflect path (base water F0 from
				// fresnelPower + the reflectionStrength boost)
				refractKnobs.skyF0 = std::clamp(
					std::clamp(0.02f * std::max(desc.fresnelPower, 0.0f),
						0.0f, 0.2f) +
					std::clamp(desc.reflectionStrength, 0.0f, 1.0f) * 0.12f,
					0.02f, 0.3f);
				refractKnobs.waveHeight = std::max(desc.waveHeight, 0.0f);
				gRefractiveWaterKnobs[name] = refractKnobs;
				gWaterScrollSpeeds[name] = desc.waveSpeed;
				pinWaterTechniqueToViewportScheme(material);
				return material;
			}
			catch(Ogre::Exception const & e)
			{
				oDebugError("engine", 0, "RenderSystem::createWaterMaterial('"
					<< name << "'): refraction setup failed: "
					<< e.getDescription() << " - falling back to the plain surface");
				outComplete = false;
				// fall through to the standard water path below
			}
		}
		// not (or no longer) a refractive water material: drop it from the set so
		// the grab target hides only the surfaces that actually refract
		gRefractiveWaterMaterials.erase(name);
		gRefractiveWaterKnobs.erase(name);
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
			// Fresnel term brightens the grazing-angle reflection. Water keeps its
			// own tuned flat ambient - it opts OUT of the hemisphere fill.
			configureSurfaceShaderState(generator, material, normalTuIndex,
				/*hemisphereAmbient*/ false);
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
		// a REFLECTIVE water surface scrolls the ripple through its program's
		// scroll constants (checked first: a composed reflect+refract material is
		// in BOTH sets, and the combined program owns both param vectors) AND keeps
		// its mirror (and, when composing, its grab) target sized to the window
		if(gReflectiveWaterMaterials.find(name) != gReflectiveWaterMaterials.end())
		{
			ensureReflectionTexture(gReflectionPlaneSet ? gReflectionPlaneY : 0.0f);
			std::unordered_map<String, ReflectKnobs>::const_iterator knobs =
				gReflectiveWaterKnobs.find(name);
			const ReflectKnobs k = knobs != gReflectiveWaterKnobs.end()
				? knobs->second : ReflectKnobs();
			if(k.refractEnabled > 0.5f)
			{
				ensureSceneGrabTexture();	// pick up a window resize
			}
			Ogre::GpuProgramParametersSharedPtr params =
				pass->getFragmentProgramParameters();
			params->setIgnoreMissingParams(true);
			// re-push the FULL refract vector (x=strength y=waveScale z/w=scroll)
			params->setNamedConstant("refractParams", Ogre::Vector4(
				k.refractStrength, k.waveScale, travel, travel * 0.6f));
			// reflectParams keeps its build-time F0 + refract-enabled flag +
			// body-dim scale
			params->setNamedConstant("reflectParams", Ogre::Vector4(
				k.reflectStrength, k.refractEnabled, k.baseScale, 0.0f));
			// the geometric swell (VS): amplitude + the shared world-space
			// frequency, phased by the shared clock rate (the same formula
			// and constants the next flavor's water vertex stage runs)
			pass->getVertexProgramParameters()->setNamedConstant("waveParams",
				Ogre::Vector4(k.waveHeight, kSwellWorldFrequency,
					seconds * kSwellPhaseRate, 0.0f));
			// the sun's direction/colour for the specular streak, pushed
			// manually (the pass is lighting-disabled, so the auto light
			// constants would not bind); w gates the streak off when no
			// directional sun exists
			if(Ogre::Light* sun = RenderBackend::firstDirectionalLight())
			{
				const Ogre::Vector3 towards = -sun->getDerivedDirection();
				const Ogre::ColourValue colour = sun->getDiffuseColour();
				params->setNamedConstant("sunTowards", Ogre::Vector4(
					towards.x, towards.y, towards.z, 1.0f));
				params->setNamedConstant("sunColour", Ogre::Vector4(
					colour.r, colour.g, colour.b, 1.0f));
			}
			else
			{
				params->setNamedConstant("sunTowards",
					Ogre::Vector4(0.0f, 1.0f, 0.0f, 0.0f));
			}
			return;
		}
		// a refractive water surface scrolls the ripple through its program's
		// scroll constants (the normal is sampled in the shader, not a texture-
		// unit scroll) AND keeps its grab target sized to the window
		if(gRefractiveWaterMaterials.find(name) != gRefractiveWaterMaterials.end())
		{
			ensureSceneGrabTexture();	// pick up a window resize
			std::unordered_map<String, RefractKnobs>::const_iterator
				knobsIt = gRefractiveWaterKnobs.find(name);
			const RefractKnobs k = knobsIt != gRefractiveWaterKnobs.end()
				? knobsIt->second : RefractKnobs();
			Ogre::GpuProgramParametersSharedPtr params =
				pass->getFragmentProgramParameters();
			params->setIgnoreMissingParams(true);
			// re-push the FULL 4-vector: x=strength y=waveScale z/w=scroll
			params->setNamedConstant("refractParams",
				Ogre::Vector4(k.strength, k.waveScale, travel, travel * 0.6f));
			// the geometric swell (VS): the same push as the reflective branch
			pass->getVertexProgramParameters()->setNamedConstant("waveParams",
				Ogre::Vector4(k.waveHeight, kSwellWorldFrequency,
					seconds * kSwellPhaseRate, 0.0f));
			// the fresnel sky reflection tracks the LIVE image-lighting state
			// (the director may toggle it after the water built): gate + level
			// per frame, and rebind the sky unit when the environment cubemap
			// (re)appears - while off, the black fallback cube stays bound
			const bool skyLive = gIbl.active && !gIbl.envTexture.empty();
			params->setNamedConstant("skyParams", Ogre::Vector4(
				k.skyF0, skyLive ? 1.0f : 0.0f,
				std::max(gIbl.luminance, 0.0f), 0.0f));
			if(pass->getNumTextureUnitStates() > 2)
			{
				Ogre::TextureUnitState* skyUnit = pass->getTextureUnitState(2);
				const String wanted = skyLive
					? gIbl.envTexture : String("Orkige/WaterSkyFallback");
				if(skyUnit->getTextureName() != wanted)
				{
					// autodetect: the chain lives in DEFAULT, an authored
					// skybox cubemap in its project's group
					if(Ogre::TexturePtr texture =
						Ogre::TextureManager::getSingleton().getByName(wanted,
							Ogre::ResourceGroupManager::
								AUTODETECT_RESOURCE_GROUP_NAME))
					{
						skyUnit->setTexture(texture);
					}
				}
			}
			// the sun's direction/colour for the specular streak (the same
			// manual push as the reflective branch - the pass is
			// lighting-disabled, so auto light constants would not bind)
			if(Ogre::Light* sun = RenderBackend::firstDirectionalLight())
			{
				const Ogre::Vector3 towards = -sun->getDerivedDirection();
				const Ogre::ColourValue colour = sun->getDiffuseColour();
				params->setNamedConstant("sunTowards", Ogre::Vector4(
					towards.x, towards.y, towards.z, 1.0f));
				params->setNamedConstant("sunColour", Ogre::Vector4(
					colour.r, colour.g, colour.b, 1.0f));
			}
			else
			{
				params->setNamedConstant("sunTowards",
					Ogre::Vector4(0.0f, 1.0f, 0.0f, 0.0f));
			}
			pushWaterAmbient(params);
			return;
		}
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
		if(type == LT_ZIP)
		{
			// a whole-zip mount (no sub-tree): the SAME MiniZip-backed pak path
			// as mountPak, so LT_ZIP behaves identically on both flavors (the
			// Ogre-Next build ships no stock Zip archive - Docs/filesystem.md)
			PakMount::mount(path, "", groupName);
			return;
		}
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			path, "FileSystem",
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
	void RenderSystem::mountPak(String const & pakPath,
		String const & mountPoint, String const & groupName)
	{
		// the mount plumbing lives in the sanctioned engine_filesystem zone and
		// is IDENTICAL on both flavors (it wraps the stock Zip archive)
		PakMount::mount(pakPath, mountPoint, groupName);
	}
	//---------------------------------------------------------
	void RenderSystem::unmountPak(String const & pakPath,
		String const & mountPoint, String const & groupName)
	{
		PakMount::unmount(pakPath, mountPoint, groupName);
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
	//--- image-based lighting (skybox-sourced) - IBL block ----------------
	namespace
	{
		//! the honest one-line degrade: an image-lighting opt-in that cannot
		//! render right now says WHY, once per distinct reason
		void warnImageLightingOnce(String const & reason)
		{
			if(gIblWarnedReason != reason)
			{
				gIblWarnedReason = reason;
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige classic backend: image-based lighting " + reason);
			}
		}

#ifdef USE_RTSHADER_SYSTEM
		//! @brief resolve the environment chain texture name for @p source
		//! under @p quality ("" on failure). The chain is the skybox
		//! cubemap's own mip chain (prefiltered offline); a tier cap below
		//! the source edge blits the tail mips into the derived
		//! kIblChainTexture cubemap, otherwise the loaded skybox texture
		//! binds directly by name.
		String ensureIblChainTexture(String const & source,
			IblPreset::Quality quality)
		{
			Ogre::TextureManager & textureManager =
				Ogre::TextureManager::getSingleton();
			try
			{
				Ogre::TexturePtr skybox = textureManager.load(source,
					Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
					Ogre::TEX_TYPE_CUBE_MAP);
				unsigned int skip = IblPreset::mipSkipForSource(
					static_cast<unsigned int>(skybox->getWidth()),
					IblPreset::forQuality(quality));
				if(skip == 0u)
				{
					return source;	// within the tier cap - bind it directly
				}
				// re-decode the cubemap CPU-side and blit the tail mips into
				// the derived tier-capped copy (replace-by-recreate)
				if(Ogre::TexturePtr stale = textureManager.getByName(
					kIblChainTexture,
					Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
				{
					textureManager.remove(stale);
				}
				const String group = Ogre::ResourceGroupManager::getSingleton()
					.findGroupContainingResource(source);
				Ogre::Image image;
				image.load(source, group);
				// classic mip counts EXCLUDE the base level
				const unsigned int sourceExtraMips = image.getNumMipmaps();
				if(skip > sourceExtraMips)
				{
					skip = sourceExtraMips;	// keep at least the smallest mip
				}
				const unsigned int chainEdge = std::max(1u,
					static_cast<unsigned int>(image.getWidth()) >> skip);
				const unsigned int chainExtraMips = sourceExtraMips - skip;
				Ogre::TexturePtr chain = textureManager.createManual(
					kIblChainTexture,
					Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
					Ogre::TEX_TYPE_CUBE_MAP, chainEdge, chainEdge,
					static_cast<int>(chainExtraMips), image.getFormat(),
					Ogre::TU_DEFAULT);
				for(size_t face = 0; face < 6; ++face)
				{
					for(unsigned int mip = 0; mip <= chainExtraMips; ++mip)
					{
						chain->getBuffer(face, mip)->blitFromMemory(
							image.getPixelBox(face, mip + skip));
					}
				}
				return kIblChainTexture;
			}
			catch(Ogre::Exception const & e)
			{
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige classic backend: image-lighting chain for '" +
					source + "' failed to build: " + e.getDescription());
				return String();
			}
		}

		//! @brief synthesize the procedural-sky environment cubemap into the
		//! derived kIblChainTexture (the RTSS image-based-lighting stage samples
		//! it by name, like the skybox chain) - the runtime SECOND source of the
		//! ONE IBL path. Its RGBA8 mip chain is built on the CPU from the
		//! atmosphere + sun (@see core_util/SkyEnvMap - the SAME sky model the
		//! visible classic gradient dome draws, so the reflections match the
		//! sky). "" on failure.
		String ensureProceduralIblChainTexture(AtmosphereDesc const & desc,
			Ogre::Vector3 const & toSun, IblPreset::Quality quality)
		{
			const unsigned int edge =
				IblPreset::forQuality(quality).chainResolution;
			if(edge == 0u)
			{
				return String();
			}
			std::vector<unsigned char> chain;
			unsigned int mips = 0u;
			SkyEnvMap::buildCubemapChainRgba8(edge, desc,
				static_cast<float>(toSun.x), static_cast<float>(toSun.y),
				static_cast<float>(toSun.z), chain, mips);
			Ogre::TextureManager & textureManager =
				Ogre::TextureManager::getSingleton();
			try
			{
				if(Ogre::TexturePtr stale = textureManager.getByName(
					kIblChainTexture,
					Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
				{
					textureManager.remove(stale);
				}
				// classic mip counts EXCLUDE the base level
				const unsigned int extraMips = mips - 1u;
				Ogre::TexturePtr cube = textureManager.createManual(
					kIblChainTexture,
					Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
					Ogre::TEX_TYPE_CUBE_MAP, edge, edge,
					static_cast<int>(extraMips), Ogre::PF_BYTE_RGBA,
					Ogre::TU_DEFAULT);
				for(size_t face = 0; face < 6; ++face)
				{
					for(unsigned int mip = 0; mip <= extraMips; ++mip)
					{
						const unsigned int e = std::max(1u, edge >> mip);
						Ogre::PixelBox box(e, e, 1u, Ogre::PF_BYTE_RGBA,
							chain.data() + SkyEnvMap::faceMipOffset(edge, mip,
								static_cast<unsigned int>(face)));
						cube->getBuffer(face, mip)->blitFromMemory(box);
					}
				}
				return kIblChainTexture;
			}
			catch(Ogre::Exception const & e)
			{
				Ogre::LogManager::getSingleton().logMessage(
					"Orkige classic backend: procedural-sky environment "
					"capture failed: " + e.getDescription());
				return String();
			}
		}
#endif // USE_RTSHADER_SYSTEM
	}
	//---------------------------------------------------------
	bool RenderBackend::imageBasedLightingSupported()
	{
#ifdef USE_RTSHADER_SYSTEM
		Ogre::RTShader::ShaderGenerator* generator =
			Ogre::RTShader::ShaderGenerator::getSingletonPtr();
		if(!generator)
		{
			return false;	// no shader generator - no generated IBL stage
		}
		// the generated shader indexes the cubemap's mip chain per fragment
		// (roughness -> lod), which a bare GLES2/WebGL1 context cannot do -
		// the stage needs GLSL ES 3.0 there (desktop GLSL always qualifies)
		if(generator->getTargetLanguage() == "glsles" &&
			!Ogre::GpuProgramManager::getSingleton().isSyntaxSupported(
				"glsl300es"))
		{
			return false;
		}
		return true;
#else
		return false;
#endif
	}
	//---------------------------------------------------------
	void RenderBackend::applyImageLighting()
	{
#ifdef USE_RTSHADER_SYSTEM
		RenderSystem* system = RenderBackend::system();
		if(!system || !system->getWorld())
		{
			return;
		}
		RenderWorld::Impl* world = system->getWorld()->mImpl;
		bool want = world->iblEnabled &&
			world->iblQuality != IblPreset::IQ_OFF;
		if(want && !RenderBackend::imageBasedLightingSupported())
		{
			// the honest per-device refusal - said ONCE, then the opt-in
			// keeps round-tripping (the marker line the selfcheck greps)
			warnImageLightingOnce("is not supported on this render backend - "
				"the opt-in is recorded but no image lighting renders on "
				"this flavor");
			want = false;
		}
		// SOURCE selection - one IBL stage, two sources: an authored skybox
		// cubemap (the offline-baked prefiltered chain) OR, when the procedural
		// gradient sky is showing with no skybox, a runtime capture of it
		// (@see ensureProceduralIblChainTexture). Colour skies / a disabled
		// atmosphere still have no meaningful environment.
		String source = RenderBackend::activeSkyboxTexture();
		bool procedural = false;
		if(want && source.empty())
		{
			if(world->atmosphere.enabled &&
				world->atmosphere.skyType == AtmosphereSky::ST_PROCEDURAL &&
				world->skyDome && world->skyDome->getVisible())
			{
				procedural = true;
				source = kProceduralSource;
			}
			else
			{
				warnImageLightingOnce("is enabled without a skybox cubemap or "
					"a procedural sky (needs an enabled atmosphere showing a "
					"skybox or procedural sky) - rendering unchanged");
				want = false;
			}
		}
		if(want)
		{
			// the split-sum lookup table the generated stage samples (ships
			// with the shader-library media); load once, refuse honestly
			// when the media set predates it
			try
			{
				Ogre::TextureManager::getSingleton().load(
					"dfgLUTmultiscatter.dds", Ogre::RGN_INTERNAL);
			}
			catch(Ogre::Exception const &)
			{
				warnImageLightingOnce("found no DFG lookup table in the "
					"shader-library media - rendering unchanged");
				want = false;
			}
		}
		IblState desired;
		desired.active = want;
		desired.luminance = world->iblIntensity;
		if(want && procedural)
		{
			// the sun the sky is lit by (first directional light, toward-sun)
			Ogre::Vector3 toSun(0.3f, 0.9f, 0.2f);
			if(Ogre::Light* sun = RenderBackend::firstDirectionalLight())
			{
				toSun = -sun->getDerivedDirection();
			}
			toSun.normalise();
			const SkyEnvMap::CaptureKey nowKey = SkyEnvMap::keyFor(
				world->atmosphere, static_cast<float>(toSun.x),
				static_cast<float>(toSun.y), static_cast<float>(toSun.z));
			// recapture on a source/tier switch, a first capture, or a material
			// sky move (sun swing / colour change) - never per frame otherwise
			const bool rebuild = gIblChainSource != source ||
				gIblChainQuality != world->iblQuality || !gIbl.active ||
				!gProceduralIblHasKey ||
				SkyEnvMap::materiallyDiffers(gProceduralIblKey, nowKey,
					kSunMoveCosThreshold);
			if(rebuild)
			{
				desired.envTexture = ensureProceduralIblChainTexture(
					world->atmosphere, toSun, world->iblQuality);
				if(desired.envTexture.empty())
				{
					desired.active = false;
					desired.luminance = 1.0f;
				}
				else
				{
					gIblChainSource = source;
					gIblChainQuality = world->iblQuality;
					gProceduralIblKey = nowKey;
					gProceduralIblHasKey = true;
					// the observable recapture marker (one line per capture)
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige classic backend: procedural-sky image-lighting "
						"capture");
				}
			}
			else
			{
				desired.envTexture = gIbl.envTexture;	// unchanged capture
			}
		}
		else if(want)
		{
			gProceduralIblHasKey = false;	// not a procedural capture now
			if(gIblChainSource != source ||
				gIblChainQuality != world->iblQuality || !gIbl.active)
			{
				desired.envTexture = ensureIblChainTexture(source,
					world->iblQuality);
				if(desired.envTexture.empty())
				{
					desired.active = false;
					desired.luminance = 1.0f;
				}
				else
				{
					gIblChainSource = source;
					gIblChainQuality = world->iblQuality;
				}
			}
			else
			{
				desired.envTexture = gIbl.envTexture;	// unchanged chain
			}
		}
		if(!desired.active)
		{
			desired.envTexture.clear();
			desired.luminance = 1.0f;
			gIblChainSource.clear();
			gIblChainQuality = IblPreset::IQ_OFF;
			gProceduralIblHasKey = false;
		}
		else
		{
			gIblWarnedReason.clear();	// active - a future refusal logs anew
		}
		if(desired.active == gIbl.active &&
			desired.envTexture == gIbl.envTexture &&
			desired.luminance == gIbl.luminance)
		{
			return;	// nothing changes - no shader re-derive
		}
		gIbl = desired;
		Ogre::RTShader::ShaderGenerator* generator =
			Ogre::RTShader::ShaderGenerator::getSingletonPtr();
		if(!generator)
		{
			return;	// fixed-function flavor: nothing was generated to re-derive
		}
		// re-derive every generated Cook-Torrance material with the new
		// state (the stage appears/disappears/retunes); a dead entry (its
		// material dropped on a project switch) prunes itself
		Ogre::MaterialManager & materialManager =
			Ogre::MaterialManager::getSingleton();
		for(std::map<String, int>::iterator each = gSurfaceMaterials.begin();
			each != gSurfaceMaterials.end();)
		{
			Ogre::MaterialPtr material = materialManager.getByName(each->first,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
			if(!material)
			{
				gHemisphereMaterials.erase(each->first);
				each = gSurfaceMaterials.erase(each);
				continue;
			}
			configureSurfaceShaderState(generator, material, each->second,
				gHemisphereMaterials.find(each->first) != gHemisphereMaterials.end());
			++each;
		}
#endif // USE_RTSHADER_SYSTEM
	}
	//---------------------------------------------------------
	void RenderBackend::imageLightingTeardown()
	{
		gIbl = IblState();
		gSurfaceMaterials.clear();
		gHemisphereMaterials.clear();
		gIblChainSource.clear();
		gIblChainQuality = IblPreset::IQ_OFF;
		gIblWarnedReason.clear();
		gProceduralIblHasKey = false;
		if(Ogre::TextureManager* textureManager =
			Ogre::TextureManager::getSingletonPtr())
		{
			if(Ogre::TexturePtr chain = textureManager->getByName(
				kIblChainTexture,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
			{
				textureManager->remove(chain);
			}
		}
	}
	//--- end IBL block ----------------------------------------------------
}
