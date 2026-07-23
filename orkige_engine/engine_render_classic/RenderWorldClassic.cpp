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
#include "engine_render_classic/AtmosphereFogSrs.h"
#include "engine_render_classic/HemisphereAmbientSrs.h"
#include "engine_util/PrimitiveUtil.h"
#include "core_util/AtmosphereSunDrive.h"
#include "core_util/SkyEnvMap.h"

#include <cmath>

namespace Orkige
{
	namespace
	{
		//! the sky dome's vertex-colour material name (generated once, RTSS
		//! auto-shaded) - the GLES2/WebGL1/Vulkan fallback look
		const char* const kSkyDomeMaterial = "Orkige/SkyDome";
		//! the per-pixel dome material: explicit programs evaluating the shared
		//! sky model per fragment (the default on the programmable GLSL targets)
		const char* const kSkyDomePixelMaterial = "Orkige/SkyDomePixel";
		//! tessellation of the gradient sphere - enough rings/segments that the
		//! vertical gradient and the sun glow read smooth, still trivially cheap
		const unsigned int kSkyRings = 24;		//!< latitude divisions
		const unsigned int kSkySegments = 48;	//!< longitude divisions
		//! the material the current dome geometry was emitted with ("" = not
		//! built): the per-pixel dome's geometry is sun-independent, so it is
		//! emitted once and only its uniforms track the atmosphere, while the
		//! vertex-colour fallback re-emits per change. File-scope like
		//! gSkyDomeNode - one world per process.
		String gSkyDomeBuiltMaterial;

		//! the node the sky dome hangs off, followed to the rendering camera by
		//! the listener below (NULL when no dome is up). File-scope like the next
		//! flavor's gAtmosphere - one world per process.
		Ogre::SceneNode* gSkyDomeNode = NULL;

		//! sRGB-encode one linear channel: the next flavor renders the sky model
		//! into an sRGB swapchain (hardware-encoded), while this classic pipeline
		//! displays values RAW - so every sky-model colour must be encoded here
		//! for the two flavors to display the same picture (the standard
		//! piecewise sRGB transfer, matching the hardware encode)
		inline float toDisplayGamma(float v)
		{
			v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
			return v <= 0.0031308f
				? v * 12.92f
				: 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
		}
		//! the sky colour for a unit view direction: the ONE shared sky model
		//! (SkyEnvMap::skyColour - the CPU port of the next flavor's native sky
		//! pixel formula), sRGB-encoded for this gamma-naive pipeline, so the
		//! visible classic dome and the next flavor's dome DISPLAY the same
		//! picture. The residual gap is vertex-gradient resolution vs per-pixel
		//! shading (tolerance parity).
		Ogre::ColourValue skyDirectionColour(Ogre::Vector3 const & dir,
			AtmosphereDesc const & desc, Ogre::Vector3 const & sunDir)
		{
			const SkyEnvMap::Colour c = SkyEnvMap::skyColour(
				dir.x, dir.y, dir.z, desc, sunDir.x, sunDir.y, sunDir.z);
			return Ogre::ColourValue(toDisplayGamma(c.r), toDisplayGamma(c.g),
				toDisplayGamma(c.b), 1.0f);
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
				// (@see RenderBackend::noteSunDimmedForShadows). The driven
				// colour is LINEAR; the floor is a DISPLAY level of 0.05
				// mapped through the sqrt display transfer (0.05^2)
				const float sunPeak = std::max(driven.r,
					std::max(driven.g, driven.b));
				RenderBackend::noteSunDimmedForShadows(sunPeak < 0.0025f);
			}
			// generated surface materials evaluate the two-colour hemisphere PER
			// PIXEL (@see HemisphereAmbientSrs.h): hand the sub-render-state the
			// SAME raw-linear sky/ground colours the next flavor's HlmsPbs
			// receives (drive.nextUpper/nextLower), so both flavors fill ambient
			// from one sky/ground split - the softened averaged-flat classic
			// value below is now ONLY for consumers off the generated scheme.
			// The blend AXIS mirrors the native linkage's tilt: up plus the
			// toward-the-sun vector half-turned about up, so a horizon-facing
			// surface fills from the warm horizon band on both flavors.
#ifdef USE_RTSHADER_SYSTEM
			Ogre::Vector3 hemiAxis = Ogre::Vector3::UNIT_Y +
				Ogre::Vector3(-toSun.x, toSun.y, -toSun.z);
			hemiAxis.normalise();
			noteHemisphereAmbientColours(
				Ogre::ColourValue(drive.nextUpperRed, drive.nextUpperGreen,
					drive.nextUpperBlue, 1.0f),
				Ogre::ColourValue(drive.nextLowerRed, drive.nextLowerGreen,
					drive.nextLowerBlue, 1.0f),
				hemiAxis);
#endif
			// the atmosphere's hemisphere fill, averaged flat (the softened
			// classic subset) - written straight to the scene for fixed-function
			// fallback materials and imported meshes keeping their own materials;
			// it also keeps the AUTHORED hemisphere cache as the restore source
			sceneManager->setAmbientLight(Ogre::ColourValue(
				drive.classicAmbientRed, drive.classicAmbientGreen,
				drive.classicAmbientBlue, 1.0f));
			gAtmosphereDrivesAmbient = true;
			// the atmospheric fog stage on the generated materials reads the
			// SAME view-independent sky-model terms the other backend uploads
			// for its object fog, under the SAME input conditioning its
			// linkage applies (elevation floor 0.02, the dusk fade folded
			// into the sky power - @see AtmosphereSunDrive::compute)
#ifdef USE_RTSHADER_SYSTEM
			{
				using namespace AtmosphereSunDrive::Detail;
				const float sunHeight = clampf(toSun.y, 0.02f, 1.0f);
				const float duskFade = clampf((toSun.y + 0.12f) / 0.12f,
					0.0f, 1.0f);
				AtmosphereDesc faded = desc;
				faded.skyPower = desc.skyPower * duskFade;
				const SkyTerms terms = skyTerms(faded,
					{ toSun.x, toSun.y, toSun.z }, sunHeight);
				AtmosphereFogState fogState;
				fogState.enabled = desc.fogDensity > 0.0f;
				fogState.fogDensity = desc.fogDensity;
				fogState.sunDir = toSun;
				fogState.sunHeight = sunHeight;
				fogState.skyColour = Ogre::Vector3(terms.skyColour.x,
					terms.skyColour.y, terms.skyColour.z);
				fogState.density = terms.density;
				fogState.lightDensity = terms.lightDensity;
				fogState.finalMultiplier = terms.finalMultiplier;
				fogState.antiMie = terms.antiMie;
				fogState.sunAbsorption = Ogre::Vector3(terms.sunAbsorption.x,
					terms.sunAbsorption.y, terms.sunAbsorption.z);
				fogState.mieAbsorption = Ogre::Vector3(terms.mieAbsorption.x,
					terms.mieAbsorption.y, terms.mieAbsorption.z);
				fogState.skyLightAbsorption = Ogre::Vector3(
					terms.skyLightAbsorption.x, terms.skyLightAbsorption.y,
					terms.skyLightAbsorption.z);
				noteAtmosphereFog(fogState);
			}
#endif
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
		//--- per-pixel sky dome (the programmable-GLSL targets) -----------
		//! can this context run the per-pixel dome programs: same two-variant
		//! GLSL family (desktop core / ES 3.0) and gate as the advanced water
		//! (@see RenderBackend::glslProfile); GLES2/WebGL1 and Vulkan keep the
		//! vertex-colour gradient dome (the byte-stable fallback)
		bool perPixelSkyDomeSupported()
		{
			return RenderBackend::screenSpaceRefractionSupported();
		}
		//! create the per-pixel dome programs once: the vertex program forwards
		//! the object-space position (the dome is a camera-centred unit sphere,
		//! so it IS the view direction), the fragment program evaluates the ONE
		//! shared sky model along it. The fragment body is a transliteration of
		//! the view-dependent half of AtmosphereSunDrive::Detail::atmosphereAt
		//! (LOCKSTEP - the view-independent terms arrive as uniforms from
		//! AtmosphereSunDrive::skyModelTerms, so shader pixels and every CPU
		//! sample of the model read identical formulas), with the CPU path's
		//! display encode (toDisplayGamma) folded onto the end. The Detail
		//! constants are inlined: kDensityDiffusion 2.0, kHorizonLimit 0.025,
		//! and the dome's neutral kSkySunDiskPower 1.0.
		void ensureSkyDomePixelPrograms()
		{
			Ogre::HighLevelGpuProgramManager & programs =
				Ogre::HighLevelGpuProgramManager::getSingleton();
			if(programs.getByName("Orkige/SkyDome_vs", Ogre::RGN_INTERNAL))
			{
				return;
			}
			const RenderBackend::GlslProfile profile =
				RenderBackend::glslProfile();
			Ogre::HighLevelGpuProgramPtr vs = programs.createProgram(
				"Orkige/SkyDome_vs", Ogre::RGN_INTERNAL,
				profile.language, Ogre::GPT_VERTEX_PROGRAM);
			vs->setSource(profile.vsPreamble + std::string(
				"uniform mat4 worldViewProj;\n"
				"in vec4 vertex;\n"
				"out vec3 vDir;\n"
				"void main()\n"
				"{\n"
				"    gl_Position = worldViewProj * vertex;\n"
				"    vDir = vertex.xyz;\n"
				"}\n"));
			vs->load();
			vs->getDefaultParameters()->setNamedAutoConstant("worldViewProj",
				Ogre::GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX);
			Ogre::HighLevelGpuProgramPtr fs = programs.createProgram(
				"Orkige/SkyDome_fs", Ogre::RGN_INTERNAL,
				profile.language, Ogre::GPT_FRAGMENT_PROGRAM);
			fs->setSource(profile.fsPreamble + std::string(
				// uniform packing (@see updateSkyDomePixelParams):
				//   sunDirHeight            = toward-the-sun xyz, sunHeight w
				//   skyColourDensity        = Rayleigh tint xyz, raw density w
				//   sunAbsorptionAntiMie    = sunAbsorption xyz, antiMie w
				//   mieAbsorptionFinalMul   = mieAbsorption xyz, finalMultiplier w
				//   skyLightAbsorptionLightDensity = skyLightAbsorption xyz,
				//                                    lightDensity w
				"uniform vec4 sunDirHeight;\n"
				"uniform vec4 skyColourDensity;\n"
				"uniform vec4 sunAbsorptionAntiMie;\n"
				"uniform vec4 mieAbsorptionFinalMul;\n"
				"uniform vec4 skyLightAbsorptionLightDensity;\n"
				"in vec3 vDir;\n"
				"out vec4 fragColour;\n"
				"void main()\n"
				"{\n"
				"    vec3 dir = normalize(vDir);\n"
				"    vec3 toSun = sunDirHeight.xyz;\n"
				"    float sunHeight = sunDirHeight.w;\n"
				"    float ldotv = max(dot(dir, toSun), 0.0);\n"
				// the horizon bend + floor (densityDiffusion 2.0, limit 0.025)
				"    dir.y += 2.0 * 0.075 * (1.0 - dir.y) * (1.0 - dir.y);\n"
				"    dir = normalize(dir);\n"
				"    dir.y = max(dir.y, 0.025);\n"
				"    dir = normalize(dir);\n"
				"    float ldotv360 = dot(dir, toSun) * 0.5 + 0.5;\n"
				"    vec3 skyColour = skyColourDensity.xyz;\n"
				"    float ptDensity = skyColourDensity.w\n"
				"        / pow(max(dir.y / max(1.0 - sunHeight, 1e-4), 0.0035),\n"
				"              mix(0.10, 2.0, pow(dir.y, 0.3)));\n"
				"    float sunDisk =\n"
				"        pow(ldotv, mix(4.0, 8500.0, sunHeight) * 0.25);\n"
				"    vec3 skyAbsorption = exp2(skyColour * -ptDensity) * 2.0;\n"
				"    vec3 gradient =\n"
				"        pow(exp2(-dir.y / max(skyColour, vec3(1e-3))), vec3(1.5));\n"
				"    vec3 sharedTerms = gradient * skyAbsorption;\n"
				"    vec3 colour = sharedTerms * sunAbsorptionAntiMie.xyz\n"
				"        * sunAbsorptionAntiMie.w;\n"
				"    colour += sharedTerms * skyLightAbsorptionLightDensity.xyz\n"
				"        * (ldotv360 * ptDensity\n"
				"           * skyLightAbsorptionLightDensity.w);\n"
				"    colour += mieAbsorptionFinalMul.xyz * ldotv360;\n"
				"    colour *= skyLightAbsorptionLightDensity.w;\n"
				"    colour *= mieAbsorptionFinalMul.w;\n"
				"    colour += skyLightAbsorptionLightDensity.xyz * sunDisk;\n"
				// the CPU path's display encode (toDisplayGamma - the standard
				// piecewise sRGB transfer), so per-pixel and vertex-fallback
				// domes and the window clear colour stay on one encode
				"    vec3 v = clamp(colour, 0.0, 1.0);\n"
				"    vec3 lo = v * 12.92;\n"
				"    vec3 hi = 1.055 * pow(max(v, vec3(0.0031308)),\n"
				"        vec3(1.0 / 2.4)) - 0.055;\n"
				"    fragColour =\n"
				"        vec4(mix(hi, lo, step(v, vec3(0.0031308))), 1.0);\n"
				"}\n"));
			fs->load();
		}
		//! create the per-pixel dome material once: the explicit program pair
		//! over the same backdrop pass state as the vertex-colour dome. The
		//! technique is pinned to the viewport's RTSS scheme so the per-frame
		//! uniform pushes reach the drawn pass (the water-material precedent:
		//! an unpinned explicit-program technique gets cloned by the RTSS
		//! scheme-not-found handler and the pushes miss the clone).
		void ensureSkyDomePixelMaterial()
		{
			Ogre::MaterialManager & materialManager =
				Ogre::MaterialManager::getSingleton();
			if(materialManager.resourceExists(kSkyDomePixelMaterial,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
			{
				return;
			}
			ensureSkyDomePixelPrograms();
			Ogre::MaterialPtr material = materialManager.create(
				kSkyDomePixelMaterial,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
			material->setReceiveShadows(false);
			Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
			pass->setLightingEnabled(false);
			pass->setDepthWriteEnabled(false);
			pass->setDepthCheckEnabled(false);	// always behind everything drawn after
			pass->setCullingMode(Ogre::CULL_NONE);
			pass->setVertexProgram("Orkige/SkyDome_vs");
			pass->setFragmentProgram("Orkige/SkyDome_fs");
#ifdef USE_RTSHADER_SYSTEM
			if(Ogre::RTShader::ShaderGenerator::getSingletonPtr())
			{
				material->getTechnique(0)->setSchemeName(
					Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
			}
#endif
		}
		//! push the view-independent sky-model terms for @p desc + the current
		//! sun onto the per-pixel dome's fragment program (the shared numbers
		//! from AtmosphereSunDrive::skyModelTerms - the uniform half of the
		//! LOCKSTEP contract with the fragment body above)
		void updateSkyDomePixelParams(AtmosphereDesc const & desc)
		{
			Ogre::MaterialPtr material =
				Ogre::MaterialManager::getSingleton().getByName(
					kSkyDomePixelMaterial,
					Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
			if(!material)
			{
				return;
			}
			const Ogre::Vector3 toSun = resolveSunDirection();
			const AtmosphereSunDrive::Detail::SkyTerms terms =
				AtmosphereSunDrive::skyModelTerms(desc,
					toSun.x, toSun.y, toSun.z);
			Ogre::GpuProgramParametersSharedPtr params =
				material->getTechnique(0)->getPass(0)
					->getFragmentProgramParameters();
			params->setIgnoreMissingParams(true);
			params->setNamedConstant("sunDirHeight", Ogre::Vector4(
				terms.toSun.x, terms.toSun.y, terms.toSun.z, terms.sunHeight));
			params->setNamedConstant("skyColourDensity", Ogre::Vector4(
				terms.skyColour.x, terms.skyColour.y, terms.skyColour.z,
				terms.density));
			params->setNamedConstant("sunAbsorptionAntiMie", Ogre::Vector4(
				terms.sunAbsorption.x, terms.sunAbsorption.y,
				terms.sunAbsorption.z, terms.antiMie));
			params->setNamedConstant("mieAbsorptionFinalMul", Ogre::Vector4(
				terms.mieAbsorption.x, terms.mieAbsorption.y,
				terms.mieAbsorption.z, terms.finalMultiplier));
			params->setNamedConstant("skyLightAbsorptionLightDensity",
				Ogre::Vector4(terms.skyLightAbsorption.x,
					terms.skyLightAbsorption.y, terms.skyLightAbsorption.z,
					terms.lightDensity));
		}

		//! (re)emit the dome sphere geometry into @p dome with @p materialName;
		//! @p withColours bakes the vertex-gradient look (the fallback), the
		//! per-pixel dome emits positions only (its look lives in uniforms)
		void emitSkyDomeSphere(Ogre::ManualObject* dome,
			char const * materialName, bool withColours,
			AtmosphereDesc const & desc)
		{
			const Ogre::Vector3 sunDir = resolveSunDirection();
			dome->clear();
			dome->begin(materialName, Ogre::RenderOperation::OT_TRIANGLE_LIST);
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
					if(withColours)
					{
						dome->colour(skyDirectionColour(dir, desc, sunDir));
					}
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
			gSkyDomeBuiltMaterial = materialName;
		}

		//! refresh the dome's LOOK from @p desc + the current sun: the
		//! per-pixel dome (where the GLSL profile supports it) keeps its
		//! geometry and tracks the atmosphere through uniforms - the shared
		//! model evaluated per FRAGMENT, so the narrow sunset horizon band
		//! renders instead of falling between vertices; the vertex-colour
		//! fallback re-emits its baked gradient (the GLES2/WebGL1/Vulkan
		//! floor, resolution-limited by design).
		void buildSkyDomeGeometry(Ogre::ManualObject* dome,
			AtmosphereDesc const & desc)
		{
			if(perPixelSkyDomeSupported())
			{
				ensureSkyDomePixelMaterial();
				if(gSkyDomeBuiltMaterial != kSkyDomePixelMaterial)
				{
					emitSkyDomeSphere(dome, kSkyDomePixelMaterial,
						false, desc);
				}
				updateSkyDomePixelParams(desc);
			}
			else
			{
				ensureSkyDomeMaterial();
				emitSkyDomeSphere(dome, kSkyDomeMaterial, true, desc);
			}
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
				// it misplaced for the scene pass that follows. The SCENE-
				// CAPTURE cameras are the exception: the water mirror shows
				// the sky above the water (its under-water half is cut by the
				// mirror's near-clip plane) and the refraction grab is the
				// scene the water refracts - each pass needs the dome wrapped
				// around ITS camera
				if(stage == Ogre::SceneManager::IRS_RENDER_TO_TEXTURE &&
					!RenderBackend::isSceneCaptureCamera(
						viewport ? viewport->getCamera() : NULL))
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
			gSkyDomeBuiltMaterial.clear();
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
		// cache both hemisphere colours for the getters
		this->mImpl->ambientUpper = upperHemisphere;
		this->mImpl->ambientLower = lowerHemisphere;
		// generated surface materials evaluate the two-colour hemisphere PER
		// PIXEL through the hemisphere-ambient sub-render-state (@see
		// HemisphereAmbientSrs.h); hand it the live colours (the atmosphere drive
		// re-pushes ambient every frame, so this stays current)
#ifdef USE_RTSHADER_SYSTEM
		noteHemisphereAmbientColours(upperHemisphere, lowerHemisphere);
#endif
		// the scene manager still carries the flat average for any consumer NOT
		// on the generated scheme (fixed-function fallback materials, imported
		// meshes keeping their own materials) and for getAmbientLight()
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

		// the window clear colour tracks the SKY MODEL's horizon look (not the
		// raw Rayleigh tint, which is an absorption spectrum - the raw blue
		// reads alien against a warm evaluated sky) so window edges and any
		// pixel the dome misses still read as sky, matching the next flavor's
		// below-horizon floor. A disabled atmosphere keeps the plain tint.
		if(RenderSystem* system = RenderSystem::get())
		{
			if(desc.enabled)
			{
				const Ogre::Vector3 toSun = resolveSunDirection();
				const SkyEnvMap::Colour horizon = SkyEnvMap::skyColour(
					0.0f, 0.0f, 1.0f, desc, toSun.x, toSun.y, toSun.z);
				system->setWindowBackgroundColour(
					Color(toDisplayGamma(horizon.r), toDisplayGamma(horizon.g),
						toDisplayGamma(horizon.b)));
			}
			else
			{
				system->setWindowBackgroundColour(
					Color(desc.skyRed, desc.skyGreen, desc.skyBlue));
			}
		}

		// object fog rides TWO paths on this flavor. Generated surface
		// materials and the water programs carry the ATMOSPHERIC fog - the
		// exact next-flavor formulas (haze colour from the shared sky model +
		// exp2 transmittance + luminance breakthrough), fed from
		// driveSunExposure below (@see AtmosphereFogSrs.h). This SCENE fog is
		// the fixed-function fallback for materials outside the generated set
		// (imported meshes keeping their own materials): flat authored colour
		// (LINEAR - the shaders mix fog pre-display-transfer), but with the
		// TRANSMITTANCE curve matched to the next flavor's object fog
		// EXACTLY: its weight is exp2(-distance * fogDensity)
		// = exp(-distance * fogDensity * ln2), which is Ogre's FOG_EXP factor
		// exp(-distance * density) at density = fogDensity * ln2 (the earlier
		// FOG_EXP2 gaussian exp(-(d*density)^2) under-fogged the whole 5-100
		// unit range at these small densities).
		if(desc.enabled && desc.fogDensity > 0.0f)
		{
			const float ln2 = 0.6931472f;
			this->mImpl->sceneManager->setFog(Ogre::FOG_EXP,
				Ogre::ColourValue(desc.fogRed, desc.fogGreen, desc.fogBlue),
				desc.fogDensity * ln2);
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
			// the atmospheric fog stage goes neutral with the atmosphere
#ifdef USE_RTSHADER_SYSTEM
			noteAtmosphereFog(AtmosphereFogState());
#endif
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
	void RenderWorld::setOutputGrade(GradeDesc const & desc)
	{
		this->mImpl->grade = desc.sanitised();
		RenderBackend::applyGradeConfig();
	}
	//---------------------------------------------------------
	GradeDesc const & RenderWorld::getOutputGrade() const
	{
		return this->mImpl->grade;
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
