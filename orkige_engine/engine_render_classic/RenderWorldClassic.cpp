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
#include "core_util/AtmosphereSunDrive.h"

#include <cmath>

namespace Orkige
{
	namespace
	{
		//! the sky dome's material name (generated once, RTSS auto-shaded)
		const char* const kSkyDomeMaterial = "Orkige/SkyDome";
		//! tessellation of the gradient sphere - enough rings/segments that the
		//! vertical gradient and the sun glow read smooth, still trivially cheap
		const unsigned int kSkyRings = 24;		//!< latitude divisions
		const unsigned int kSkySegments = 48;	//!< longitude divisions

		//! the node the sky dome hangs off, followed to the rendering camera by
		//! the listener below (NULL when no dome is up). File-scope like the next
		//! flavor's gAtmosphere - one world per process.
		Ogre::SceneNode* gSkyDomeNode = NULL;

		//! smoothstep 0..1 (Hermite), for the elevation gradient blend
		inline float smoothstep01(float t)
		{
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			return t * t * (3.0f - 2.0f * t);
		}
		//! component-wise lerp of two colours
		inline Ogre::ColourValue mixColour(Ogre::ColourValue const & a,
			Ogre::ColourValue const & b, float t)
		{
			return Ogre::ColourValue(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
				a.b + (b.b - a.b) * t, 1.0f);
		}
		//! saturate a colour to [0;1] per channel (the un-tonemapped sum can
		//! exceed 1 where the sun glow stacks on a bright sky)
		inline Ogre::ColourValue saturateColour(Ogre::ColourValue c)
		{
			c.r = std::min(1.0f, std::max(0.0f, c.r));
			c.g = std::min(1.0f, std::max(0.0f, c.g));
			c.b = std::min(1.0f, std::max(0.0f, c.b));
			c.a = 1.0f;
			return c;
		}
		//! the sky colour for a unit view direction: a zenith -> horizon ->
		//! ground vertical gradient (horizon hazed/brightened by density) plus a
		//! soft sun glow toward @p sunDir. Approximates the next flavor's
		//! atmospheric look - "same sky, softer", not a pixel match.
		Ogre::ColourValue skyDirectionColour(Ogre::Vector3 const & dir,
			AtmosphereDesc const & desc, Ogre::Vector3 const & sunDir)
		{
			const float power = std::max(desc.skyPower, 0.0f);
			const Ogre::ColourValue zenith(desc.skyRed * power,
				desc.skyGreen * power, desc.skyBlue * power, 1.0f);
			// density washes the horizon toward a bright haze (hazier = thicker)
			const float haze = std::min(0.85f, std::max(0.0f, desc.density * 0.6f));
			const float hazeLevel = std::min(1.2f, std::max(power, 0.15f));
			const Ogre::ColourValue horizon = mixColour(zenith,
				Ogre::ColourValue(hazeLevel, hazeLevel, hazeLevel, 1.0f), haze);
			const Ogre::ColourValue ground(zenith.r * 0.35f, zenith.g * 0.35f,
				zenith.b * 0.35f, 1.0f);
			const float elevation = dir.y;	// unit sphere: y is the elevation
			Ogre::ColourValue base = elevation >= 0.0f
				? mixColour(horizon, zenith, smoothstep01(elevation))
				: mixColour(horizon, ground, smoothstep01(-elevation));
			// the sun glow: a tight bright core + a soft wide halo toward the sun
			const float toward = std::max(0.0f, dir.dotProduct(sunDir));
			const float glow = std::pow(toward, 160.0f) * 0.85f
				+ std::pow(toward, 6.0f) * 0.25f;
			const Ogre::ColourValue sunColour(1.0f, 0.92f, 0.78f, 1.0f);
			return saturateColour(Ogre::ColourValue(base.r + sunColour.r * glow,
				base.g + sunColour.g * glow, base.b + sunColour.b * glow, 1.0f));
		}
		//! the sun direction the dome links to: -direction of the FIRST
		//! directional light points FROM a surface TOWARD the sun (same rule as
		//! the next flavor); a default high daytime sun when there is none
		Ogre::Vector3 resolveSunDirection()
		{
			Ogre::Vector3 dir(0.3f, 0.9f, 0.2f);
			if(Ogre::Light* sun = RenderBackend::firstDirectionalLight())
			{
				dir = -sun->getDerivedDirection();
			}
			dir.normalise();
			return dir;
		}

		//--- sun-exposure linkage (the classic subset of the next flavor's
		//--- atmosphere-driven sun) --------------------------------------
		//! restore-exactly bookkeeping: while the atmosphere is enabled it
		//! OWNS the linked sun's colour (like the next flavor's native
		//! linkage), so the light's authored diffuse/specular are snapshotted
		//! the moment the atmosphere takes it and written back EXACTLY when
		//! it lets go (disable, sun-set change, world teardown) - the
		//! recover-then-reapply rule (@see ScreenShake). The editor never
		//! enables an atmosphere, so editing stays untouched.
		Ogre::Light* gLinkedSun = NULL;
		Ogre::ColourValue gLinkedSunDiffuse;
		Ogre::ColourValue gLinkedSunSpecular;
		//! true while the atmosphere drives the scene ambient (so disabling
		//! restores the authored hemisphere average exactly ONCE, and a
		//! never-enabled atmosphere never touches the scene ambient)
		bool gAtmosphereDrivesAmbient = false;

		//! give the linked sun its authored colours back (no-op when the
		//! atmosphere holds no light)
		void restoreLinkedSun()
		{
			if(gLinkedSun)
			{
				gLinkedSun->setDiffuseColour(gLinkedSunDiffuse);
				gLinkedSun->setSpecularColour(gLinkedSunSpecular);
				gLinkedSun = NULL;
			}
		}

		//! drive the linked sun's colour and the flat scene ambient from the
		//! shared day/night curve (core_util/AtmosphereSunDrive.h - the SAME
		//! model the next flavor's atmosphere evaluates natively, with the
		//! classic exposure calibration documented there). The sun's
		//! DIRECTION stays transform-authored on both flavors; only colour
		//! and fill are driven.
		void driveSunExposure(Ogre::SceneManager* sceneManager,
			AtmosphereDesc const & desc)
		{
			Ogre::Light* sun = RenderBackend::firstDirectionalLight();
			if(gLinkedSun != sun)
			{
				// the previous sun returns to its authored colours (it is
				// still alive here - the registry is updated before a dying
				// light is destroyed), the new one is snapshotted first
				restoreLinkedSun();
				if(sun)
				{
					gLinkedSun = sun;
					gLinkedSunDiffuse = sun->getDiffuseColour();
					gLinkedSunSpecular = sun->getSpecularColour();
				}
			}
			const Ogre::Vector3 toSun = resolveSunDirection();
			const AtmosphereSunDrive::Drive drive =
				AtmosphereSunDrive::compute(desc, toSun.x, toSun.y, toSun.z);
			if(sun)
			{
				const Ogre::ColourValue driven(
					drive.sunRed * drive.classicSunScale,
					drive.sunGreen * drive.classicSunScale,
					drive.sunBlue * drive.classicSunScale, 1.0f);
				sun->setDiffuseColour(driven);
				sun->setSpecularColour(driven);
				// a night-dark sun throws no visible shadow - skip the whole
				// shadow pass below the intensity floor and re-arm at dawn
				// (@see RenderBackend::noteSunDimmedForShadows)
				const float sunPeak = std::max(driven.r,
					std::max(driven.g, driven.b));
				RenderBackend::noteSunDimmedForShadows(sunPeak < 0.05f);
			}
			// the atmosphere's hemisphere fill, averaged flat (the classic
			// setAmbientHemisphere subset) - written straight to the scene so
			// the AUTHORED hemisphere cache stays the restore source
			sceneManager->setAmbientLight(Ogre::ColourValue(
				drive.classicAmbientRed, drive.classicAmbientGreen,
				drive.classicAmbientBlue, 1.0f));
			gAtmosphereDrivesAmbient = true;
		}
		//! create the sky material once: unlit, vertex-colour, no depth test/
		//! write, two-sided - drawn first (sky queue) as the backdrop. RTSS
		//! auto-shades it (transform + vertex colour, no lighting).
		void ensureSkyDomeMaterial()
		{
			Ogre::MaterialManager & materialManager =
				Ogre::MaterialManager::getSingleton();
			if(materialManager.resourceExists(kSkyDomeMaterial,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
			{
				return;
			}
			Ogre::MaterialPtr material = materialManager.create(kSkyDomeMaterial,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
			// the sky is a backdrop - never a shadow receiver (or caster: the
			// dome's ManualObject turns its caster flag off at build time)
			material->setReceiveShadows(false);
			Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
			pass->setLightingEnabled(false);
			pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
			pass->setDepthWriteEnabled(false);
			pass->setDepthCheckEnabled(false);	// always behind everything drawn after
			pass->setCullingMode(Ogre::CULL_NONE);
		}
		//! (re)emit the gradient sphere geometry into @p dome from @p desc + the
		//! current sun. Rebuilt (clear + begin/end) on each atmosphere change -
		//! infrequent, so a fresh section is simpler than a dynamic update.
		void buildSkyDomeGeometry(Ogre::ManualObject* dome,
			AtmosphereDesc const & desc)
		{
			const Ogre::Vector3 sunDir = resolveSunDirection();
			dome->clear();
			dome->begin(kSkyDomeMaterial, Ogre::RenderOperation::OT_TRIANGLE_LIST);
			for(unsigned int ring = 0; ring <= kSkyRings; ++ring)
			{
				const float theta =
					Ogre::Math::PI * float(ring) / float(kSkyRings);
				const float y = std::cos(theta);
				const float r = std::sin(theta);
				for(unsigned int seg = 0; seg <= kSkySegments; ++seg)
				{
					const float phi =
						2.0f * Ogre::Math::PI * float(seg) / float(kSkySegments);
					const Ogre::Vector3 dir(r * std::cos(phi), y,
						r * std::sin(phi));
					dome->position(dir);
					dome->colour(skyDirectionColour(dir, desc, sunDir));
				}
			}
			const unsigned int stride = kSkySegments + 1;
			for(unsigned int ring = 0; ring < kSkyRings; ++ring)
			{
				for(unsigned int seg = 0; seg < kSkySegments; ++seg)
				{
					const unsigned int a = ring * stride + seg;
					const unsigned int b = a + 1;
					const unsigned int c = a + stride;
					const unsigned int d = c + 1;
					// two-sided material, so winding is irrelevant
					dome->triangle(a, c, b);
					dome->triangle(b, c, d);
				}
			}
			dome->end();
			// never frustum-cull the sky (it wraps whatever camera it follows)
			dome->setBoundingBox(Ogre::AxisAlignedBox::BOX_INFINITE);
		}

		//! keeps the sky dome centred on whatever camera is about to render
		//! (window, editor RTT, ...). preFindVisibleObjects fires per viewport
		//! BEFORE culling, so the reposition takes effect this frame - one
		//! listener covers every camera (the DrawLayer2D RenderQueueListener is
		//! the sibling precedent).
		class SkyDomeCameraFollower : public Ogre::SceneManager::Listener
		{
		public:
			void preFindVisibleObjects(Ogre::SceneManager*,
				Ogre::SceneManager::IlluminationRenderStage stage,
				Ogre::Viewport* viewport) override
			{
				// never follow a shadow-texture camera: the dome casts
				// nothing, and re-centring it on the caster rig would leave
				// it misplaced for the scene pass that follows
				if(stage == Ogre::SceneManager::IRS_RENDER_TO_TEXTURE)
				{
					return;
				}
				if(!gSkyDomeNode || !viewport)
				{
					return;
				}
				Ogre::Camera* camera = viewport->getCamera();
				if(!camera)
				{
					return;
				}
				// centre on the camera; a radius just past the near plane can
				// never be sliced by it and stays well inside the far plane
				gSkyDomeNode->setPosition(camera->getDerivedPosition());
				const float radius = camera->getNearClipDistance() * 4.0f;
				gSkyDomeNode->setScale(radius, radius, radius);
			}
		};
		SkyDomeCameraFollower gSkyFollower;
		bool gSkyFollowerRegistered = false;

		//! build the dome on first use (ManualObject + node + material + the
		//! camera-follow listener) then (re)emit its gradient from the cached
		//! atmosphere; makes it visible. Idempotent. Takes the world's fields by
		//! reference (the RenderWorld::Impl type is not free-function-accessible).
		void rebuildSkyDome(Ogre::SceneManager* sceneManager,
			Ogre::ManualObject*& skyDome, Ogre::SceneNode*& skyNode,
			AtmosphereDesc const & atmosphere)
		{
			ensureSkyDomeMaterial();
			if(!skyDome)
			{
				skyDome = sceneManager->createManualObject(
					RenderBackend::generateName("Orkige/SkyDome"));
				skyDome->setRenderQueueGroup(Ogre::RENDER_QUEUE_SKIES_EARLY);
				skyDome->setCastShadows(false);
				skyNode = sceneManager->getRootSceneNode()->createChildSceneNode();
				skyNode->attachObject(skyDome);
				gSkyDomeNode = skyNode;
				if(!gSkyFollowerRegistered)
				{
					sceneManager->addListener(&gSkyFollower);
					gSkyFollowerRegistered = true;
				}
			}
			buildSkyDomeGeometry(skyDome, atmosphere);
			skyDome->setVisible(true);
		}
		//--- cubemap sky box (AtmosphereSky::ST_SKYBOX) -------------------
		//! the cubemap the native sky box currently shows ("" = no sky box),
		//! so per-frame atmosphere re-applies with the same cubemap skip the
		//! rebuild. File-scope like gSkyDomeNode - one world per process.
		String gSkyBoxTexture;
		//! the cubemap name last warned about (missing/unloadable), so the
		//! honest degrade logs ONCE per name instead of per apply
		String gSkyBoxWarnedTexture;

		//! show/hide the native camera-bound sky box: @p textureName is a
		//! single cubemap image (a cubemap .dds - what Util/make_sky_assets.py
		//! bakes), "" disables. The per-cubemap material is generated (script
		//! ban): unlit, depth-write off, first texture unit TEX_TYPE_CUBE_MAP -
		//! the shape SceneManager::setSkyBox requires. A missing/unloadable
		//! cubemap degrades honestly to the flat sky tint with one log line.
		void applySkyBox(Ogre::SceneManager* sceneManager,
			String const & requestedName)
		{
			// a cooked cubemap ships block-compressed: BCn keeps the .dds name,
			// but an ASTC/ETC2 export renamed it to .ktx - resolve a missing
			// .dds to its cooked sibling (the fallback the sprite paths use)
			const String textureName =
				RenderBackend::resolveTextureResourceName(requestedName);
			if(textureName == gSkyBoxTexture)
			{
				return;	// already showing this cubemap (or already disabled)
			}
			if(textureName.empty())
			{
				sceneManager->setSkyBox(false, "");
				gSkyBoxTexture.clear();
				return;
			}
			Ogre::TexturePtr cubemap;
			try
			{
				cubemap = Ogre::TextureManager::getSingleton().load(
					textureName,
					Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
					Ogre::TEX_TYPE_CUBE_MAP);
			}
			catch(Ogre::Exception const & e)
			{
				if(gSkyBoxWarnedTexture != textureName)
				{
					gSkyBoxWarnedTexture = textureName;
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige classic backend: skybox cubemap '" +
						textureName + "' failed to load - rendering the flat "
						"sky colour instead: " + e.getDescription());
				}
				sceneManager->setSkyBox(false, "");
				gSkyBoxTexture.clear();
				return;
			}
			const String materialName = "Orkige/SkyBox/" + textureName;
			Ogre::MaterialManager & materialManager =
				Ogre::MaterialManager::getSingleton();
			if(!materialManager.resourceExists(materialName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
			{
				Ogre::MaterialPtr material = materialManager.create(
					materialName,
					Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
				// a backdrop like the gradient dome: never lit, never in the
				// shadow pass (setSkyBox itself forces depth-write off and the
				// sky mesh casts nothing)
				material->setReceiveShadows(false);
				Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
				pass->setLightingEnabled(false);
				pass->setDepthWriteEnabled(false);
				pass->setCullingMode(Ogre::CULL_NONE);
				Ogre::TextureUnitState* textureUnit =
					pass->createTextureUnitState();
				textureUnit->setTexture(cubemap);
				textureUnit->setTextureAddressingMode(
					Ogre::TextureUnitState::TAM_CLAMP);
				textureUnit->setTextureFiltering(Ogre::FO_LINEAR,
					Ogre::FO_LINEAR, Ogre::FO_LINEAR);
			}
			// a fixed camera-bound cube well inside every practical near/far
			// pair (near < 50 < far); drawn first in the sky queue, so depth
			// ordering never cuts it
			sceneManager->setSkyBox(true, materialName, 50.0f, true,
				Ogre::Quaternion::IDENTITY,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
			gSkyBoxTexture = textureName;
		}

		//! drop the dome + its listener (world teardown)
		void teardownSkyDome(Ogre::SceneManager* sceneManager,
			Ogre::ManualObject*& skyDome, Ogre::SceneNode*& skyNode)
		{
			if(gSkyFollowerRegistered && sceneManager)
			{
				sceneManager->removeListener(&gSkyFollower);
				gSkyFollowerRegistered = false;
			}
			gSkyDomeNode = NULL;
			if(skyDome && sceneManager)
			{
				if(skyNode)
				{
					skyNode->detachAllObjects();
					sceneManager->destroySceneNode(skyNode);
					skyNode = NULL;
				}
				sceneManager->destroyManualObject(skyDome);
				skyDome = NULL;
			}
		}
	}

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
		// the linked sun dies with the scene manager - drop the handle, no
		// restore (and the ambient flag resets for a future world)
		gLinkedSun = NULL;
		gAtmosphereDrivesAmbient = false;
		// the scene manager outlives this world (Engine owns it): give it its
		// sky box back off and reset the cubemap bookkeeping for a future world
		if(this->mImpl->sceneManager)
		{
			applySkyBox(this->mImpl->sceneManager, String());
		}
		gSkyBoxWarnedTexture.clear();
		// image lighting dies with the world (its source cubemap just left)
		RenderBackend::imageLightingTeardown();
		// drop the sky dome (its ManualObject/node + the camera-follow listener)
		// before the scene manager tears down
		teardownSkyDome(this->mImpl->sceneManager, this->mImpl->skyDome,
			this->mImpl->skyNode);
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
		return RenderBackend::createCamera(this->mImpl->sceneManager, name);
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
	void RenderWorld::setImageLighting(bool enabled, Real intensity)
	{
		this->mImpl->iblEnabled = enabled;
		this->mImpl->iblIntensity = static_cast<float>(intensity);
		RenderBackend::applyImageLighting();
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
	void RenderWorld::setShadowQuality(ShadowPreset::Quality quality)
	{
		if(quality == this->mImpl->shadowQuality)
		{
			return;
		}
		// the knob feeds the scene-level RTSS integrated-PSSM technique: a
		// tier change while shadows render re-arms with the new budgets, OFF
		// disarms restore-exactly (@see RenderBackend::applyShadowConfig - it
		// also owns the honest one-log-line refusal of a render system
		// without depth-texture targets)
		this->mImpl->shadowQuality = quality;
		RenderBackend::applyShadowConfig();
	}
	//---------------------------------------------------------
	ShadowPreset::Quality RenderWorld::getShadowQuality() const
	{
		return this->mImpl->shadowQuality;
	}
	//---------------------------------------------------------
	void RenderWorld::setAtmosphere(AtmosphereDesc const & desc)
	{
		this->mImpl->atmosphere = desc;

		// the window clear colour tracks the sky tint so the window edges / a
		// disabled atmosphere still read as sky (the dome covers the rest)
		if(RenderSystem* system = RenderSystem::get())
		{
			system->setWindowBackgroundColour(
				Color(desc.skyRed, desc.skyGreen, desc.skyBlue));
		}

		// fixed-function exponential distance fog (the honest fog subset -
		// colours will not match the next flavor's atmospheric fog)
		if(desc.enabled && desc.fogDensity > 0.0f)
		{
			this->mImpl->sceneManager->setFog(Ogre::FOG_EXP2,
				Ogre::ColourValue(desc.fogRed, desc.fogGreen, desc.fogBlue),
				desc.fogDensity);
		}
		else
		{
			this->mImpl->sceneManager->setFog(Ogre::FOG_NONE);
		}

		// the sky VISUAL per type (AtmosphereDesc::skyType); fog above and the
		// sun-exposure drive below are sky-type-independent - the desc's
		// contract, so a skybox/colour scene keeps the same day/night arc
		if(desc.enabled)
		{
			if(desc.skyType == AtmosphereSky::ST_PROCEDURAL)
			{
				applySkyBox(this->mImpl->sceneManager, String());
				// the gradient sky dome: build/refresh it while enabled (kept
				// built but hidden for a cheap re-enable on the other types)
				rebuildSkyDome(this->mImpl->sceneManager, this->mImpl->skyDome,
					this->mImpl->skyNode, this->mImpl->atmosphere);
			}
			else
			{
				if(this->mImpl->skyDome)
				{
					this->mImpl->skyDome->setVisible(false);
				}
				if(desc.skyType == AtmosphereSky::ST_SKYBOX &&
					desc.skyboxTexture.empty() &&
					gSkyBoxWarnedTexture != "<empty>")
				{
					// skybox mode without a cubemap: the honest flat-tint
					// degrade, said once
					gSkyBoxWarnedTexture = "<empty>";
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige classic backend: skybox sky type without a "
						"cubemap texture - rendering the flat sky colour");
				}
				applySkyBox(this->mImpl->sceneManager,
					desc.skyType == AtmosphereSky::ST_SKYBOX
						? desc.skyboxTexture : String());
			}
			// sun-exposure linkage: drive the first directional light's
			// colour + the flat ambient fill through the shared day/night
			// curve (the next flavor gets the same drive natively from its
			// atmosphere - AtmosphereDesc::sunPower/ambientPower now act on
			// BOTH flavors)
			driveSunExposure(this->mImpl->sceneManager, this->mImpl->atmosphere);
		}
		else
		{
			applySkyBox(this->mImpl->sceneManager, String());
			if(this->mImpl->skyDome)
			{
				this->mImpl->skyDome->setVisible(false);
			}
			// restore-exactly: the linked sun returns to its authored
			// colours, the scene ambient to the authored hemisphere average
			// (only if the atmosphere was actually driving them); an
			// atmosphere-imposed night dim lifts with the atmosphere
			RenderBackend::noteSunDimmedForShadows(false);
			restoreLinkedSun();
			if(gAtmosphereDrivesAmbient)
			{
				gAtmosphereDrivesAmbient = false;
				this->mImpl->sceneManager->setAmbientLight(
					(this->mImpl->ambientUpper + this->mImpl->ambientLower)
						* 0.5f);
			}
		}
		// the environment chain follows the skybox shown above (activates,
		// deactivates or rebuilds; a cheap no-op while the opt-in is off)
		RenderBackend::applyImageLighting();
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
	bool RenderBackend::noteAuthoredSunColour(Ogre::Light* light,
		Ogre::ColourValue const & colour, bool specular)
	{
		if(!light || light != gLinkedSun)
		{
			return false;	// not driven - the caller writes the live light
		}
		// the atmosphere owns the live colour; record the authored value so
		// disabling restores the LATEST one (restore-exactly)
		if(specular)
		{
			gLinkedSunSpecular = colour;
		}
		else
		{
			gLinkedSunDiffuse = colour;
		}
		return true;
	}
	//---------------------------------------------------------
	String const & RenderBackend::activeSkyboxTexture()
	{
		return gSkyBoxTexture;
	}
	//---------------------------------------------------------
	void RenderBackend::refreshSkyDome()
	{
		RenderSystem* system = RenderBackend::system();
		if(!system || !system->getWorld())
		{
			return;
		}
		RenderWorld::Impl* impl = system->getWorld()->mImpl;
		if(impl->atmosphere.enabled)
		{
			if(impl->skyDome &&
				impl->atmosphere.skyType == AtmosphereSky::ST_PROCEDURAL)
			{
				// the sun set changed under a live dome: re-emit its gradient
				// (the sun glow tracks the new first directional light; a
				// skybox/colour sky has no sun-linked pixels to refresh)
				buildSkyDomeGeometry(impl->skyDome, impl->atmosphere);
			}
			// re-resolve the sun-exposure linkage to the new first
			// directional light (restores a leaving sun, takes the new one)
			driveSunExposure(impl->sceneManager, impl->atmosphere);
		}
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
