/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	RenderWorld.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __RenderWorld_h__8_7_2026__12_00_00__
#define __RenderWorld_h__8_7_2026__12_00_00__

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include "engine_render/SpriteBatch.h"
#include "engine_render/SpriteQuad.h"
#include "engine_render/VectorMesh.h"
#include "engine_render/LineMesh.h"
#include <core_util/ShadowPreset.h>
#include <core_util/IblPreset.h>
#include <core_util/BloomPreset.h>
#include <core_util/GradeDesc.h>
#include <core_util/AtmosphereDesc.h>
#include <core_util/MeshBuilder.h>
#include <core_util/String.h>
#include <vector>

namespace Orkige
{
	//! @brief one renderable scene: node hierarchy, scene content and queries
	//! @remarks Facade over the backend scene manager. Owned by RenderSystem
	//! (one world for now - the door to several stays open). All create*
	//! calls return optr handles; the handle owns the backend object and
	//! destroying it removes the object from the scene (RAII, no destroy*
	//! methods needed).
	//!
	//! Backend mapping (whole class): classic = Ogre::SceneManager;
	//! next = Ogre::SceneManager (v2, created with worker-thread count);
	//! filament = filament::Scene + the manager singletons (EntityManager,
	//! TransformManager, LightManager, RenderableManager).
	class ORKIGE_ENGINE_DLL RenderWorld
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief one hit of a scene ray query, sorted by distance
		//! @remarks plain data (scripting-friendly, mirrors
		//! PhysicsWorld::RayHit). Bounding-box accurate - triangle-accurate
		//! picking goes through PhysicsWorld::castRay against collision
		//! shapes instead (the CollisionTools successor).
		struct ORKIGE_ENGINE_DLL RayQueryHit
		{
			Real	distance;		//!< distance along the ray to the AABB entry
			optr<RenderNode> node;	//!< the node the hit content is attached to (may be NULL for non-facade content)
			void*	userPointer;	//!< first user pointer found walking node->parents (@see RenderNode::setUserPointer) or NULL
			RayQueryHit();			// defined by the backend TU
		};
	protected:
		//! backend state - defined only inside the selected backend
		struct Impl;
		//! the selected backend's plumbing (@see RenderPrerequisites.h)
		friend struct RenderBackend;
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Impl*	mImpl;	//!< backend scene guts
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor - drops the backend scene (all handles must be gone)
		~RenderWorld();

		//--- node hierarchy ---
		//! @brief the world root node (owned by the world, do not re-parent)
		//! map: classic/next=SceneManager::getRootSceneNode | filament=implicit (parentless transforms)
		optr<RenderNode> getRootNode() const;
		//! @brief create a node under the root (empty name = generated)
		//! map: classic/next=getRootSceneNode()->createChildSceneNode | filament=EntityManager::create+TransformManager::create
		optr<RenderNode> createNode(String const & name = "");

		//--- scene content factories ---
		//! @brief create a mesh instance from a mesh resource name
		//! (resolves through the resource system incl. project assets)
		//! map: classic=SceneManager::createEntity | next=createItem (v2 mesh; v1 meshes need Mesh::importV1) | filament=gltfio/filamesh loader + RenderableManager
		optr<MeshInstance> createMeshInstance(String const & meshName);
		//! @brief create a textured alpha-blended sprite quad (2D building block)
		//! map: classic=ManualObject + generated "Sprite/<tex>" material | next=ManualObject v2 + HlmsUnlit datablock | filament=quad VB/IB + unlit filamat instance
		optr<SpriteQuad> createSpriteQuad(String const & textureName);
		//! @brief create a world-space, single-texture N-quad batch (the 2D
		//! particle-system building block) - one draw call per system,
		//! refilled from a CPU vertex array each frame (@see SpriteBatch).
		//! The alpha variant may pick the sprite SAMPLER (filter/addressing,
		//! SpriteQuad::samplerName keying) so a batched sprite run renders
		//! through the SAME per-(texture,sampler) material its individual
		//! quads use; the additive glow variant always samples
		//! bilinear+clamp (the burst recipe - sampler arguments ignored).
		//! map: classic=ManualObject + shared "Sprite/<tex>#<sampler>"/"SpriteAdd/<tex>" material | next=v2 ManualObject + shared HlmsUnlit datablock | filament=dynamic VB/IB + unlit filamat instance
		optr<SpriteBatch> createSpriteBatch(String const & textureName,
			SpriteBatch::BlendMode blendMode = SpriteBatch::BLEND_ALPHA,
			SpriteQuad::FilterMode filter = SpriteQuad::FILTER_BILINEAR,
			SpriteQuad::AddressMode addressing = SpriteQuad::ADDRESS_CLAMP);
		//! @brief create a world-space untextured vertex-coloured triangle mesh
		//! - the flat-colour vector-shape building block (fills + alpha-feather
		//! edge), refilled from a CPU vertex+index array (@see VectorMesh)
		//! map: classic=ManualObject + shared "VectorFill" material | next=v2
		//! ManualObject + shared HlmsUnlit vertex-colour datablock | filament=
		//! dynamic VB/IB + unlit filamat instance
		optr<VectorMesh> createVectorMesh();
		//! @brief create a world/local-space vertex-coloured 3D LINE renderable
		//! - a polyline (strip) or independent-segment list, refilled from a CPU
		//! vertex array (static once, dynamically per change) and depth-tested
		//! against the scene or drawn as an overlay (@see LineMesh). Line width
		//! is hairline-only (@see LineMesh - no thickness knob yet).
		//! map: classic=ManualObject OT_LINE_STRIP/OT_LINE_LIST + the shared
		//! unlit vertex-colour material | next=v2 ManualObject (SCENE_DYNAMIC) +
		//! the HlmsUnlit vertex-colour datablock | filament=dynamic VB + LINES
		optr<LineMesh> createLineMesh();
		//! @brief create a camera (attach it to a node to place it)
		//! map: classic/next=SceneManager::createCamera | filament=Engine::createCamera(entity)
		optr<RenderCamera> createCamera(String const & name = "");
		//! @brief create a light (attach it to a node to place it)
		//! map: classic/next=SceneManager::createLight | filament=LightManager::Builder
		optr<RenderLight> createLight();
		//! @brief create a projected surface decal (attach it to a node to place
		//! it; it projects down the node's local -Y onto the surface). @see
		//! RenderDecal for the per-flavor tolerance (next = a true projected
		//! Ogre-Next Decal, classic = a surface-aligned quad). The new decal joins
		//! the world's visible-decal budget (@see setMaxDecals) - if it pushes the
		//! count past the cap the oldest decal is hidden.
		//! map: classic=ManualObject aligned quad + "Decal/<tex>" material | next=SceneManager::createDecal (forward-clustered) | filament=n/a (future)
		optr<RenderDecal> createDecal();

		//--- procedural built-in meshes ---
		//! the editor's "Create Cube" mesh resource name (the facade home of
		//! PrimitiveUtil::CUBE_MESH_NAME - flavor-neutral callers spell THIS)
		static constexpr char const * CUBE_MESH_NAME = "EditorCube.mesh";
		//! @brief ensure the shared vertex-coloured cube MESH RESOURCE exists
		//! (idempotent) - the editor's "Create Cube" content. Scenes reference
		//! it by name through ModelComponent, so every app that loads such
		//! scenes calls this before the scene load; afterwards the cube loads
		//! like any mesh via createMeshInstance. Defaults mirror the editor's
		//! PrimitiveUtil constants (CUBE_MESH_NAME / CUBE_MESH_HALF_EXTENT).
		//! Creates the shared unlit "VertexColour" vertex-colour-tracking
		//! material along the way (@see MeshInstance::setVertexColourUnlit).
		//! map: classic=ManualObject::convertToMesh (engine_util/PrimitiveUtil recipe, backend-private) | next=v2 mesh built from the same vertex data + HlmsUnlit | filament=prebuilt vertex/index buffers + unlit filamat
		void createVertexColourCubeMesh(String const & meshName = CUBE_MESH_NAME,
			Real halfExtent = Real(0.8));
		//! @brief ensure a vertex-coloured LINE-LIST mesh resource exists
		//! (idempotent per name) - editor helper geometry (the reference
		//! grid). Consecutive point PAIRS form one world-space segment
		//! (pointCount must be even); colours are per point and render
		//! through the same shared unlit "VertexColour" look as the cube
		//! service. Instantiate it like any mesh via createMeshInstance.
		//! map: classic=ManualObject OT_LINE_LIST -> convertToMesh | next=v1 ManualObject line list -> createByImportingV1 (the cube-service recipe) | filament=RenderableManager PRIMITIVE_TYPE LINES + unlit filamat
		void createLineListMesh(String const & meshName,
			Vec3 const * points, Color const * colours, size_t pointCount);
		//! @brief create a LIT mesh RESOURCE from CPU vertex data - the entry a
		//! procedural/parametric mesh takes into the renderer, and the reason a
		//! generated mesh behaves EXACTLY like a loaded `.glb`: this makes a
		//! named resource, and everything afterwards
		//! (createMeshInstance, PBS `.omat` materials via
		//! MeshInstance::setMaterial, shadows, the static mobility flag,
		//! visibility flags, native instancing) is the ordinary loaded-mesh
		//! path with no procedural special case anywhere above the facade.
		//! It is the LIT sibling of createVectorMesh (which is the unlit
		//! vertex-colour 2D primitive) and of createLineListMesh/
		//! createVertexColourCubeMesh (which are fixed editor recipes rather
		//! than an open data entry).
		//!
		//! @p data carries positions, unit normals, one UV set and tangents
		//! (@see core_util/MeshBuilder.h - a normal-mapping material is refused
		//! on a tangent-less mesh and a textured one on a UV-less mesh, so all
		//! four are required) plus the per-material SECTIONS, each of which
		//! becomes one SUB-MESH: a section's `material` is a LIVE renderer
		//! material name (@see RenderSystem::createMaterial) or empty for the
		//! backend default. Per-sub-mesh materials are exactly how a multi-
		//! material import behaves, so MeshInstance::setMaterial still overrides
		//! the WHOLE instance the way it does for a `.glb`.
		//!
		//! IDEMPOTENT per name: a name that already exists is left alone and the
		//! call succeeds (the cube/line-list service contract), so callers may
		//! ensure-then-instantiate. To REPLACE the geometry under a live name
		//! (a hot-reload), destroyGeneratedMesh it first - with no instance
		//! still holding it.
		//! @return false + a log line on empty/invalid data (@see
		//! MeshBuilder::validate) or a backend build failure; nothing is
		//! registered then.
		//! map: classic=Ogre::ManualObject section per Section (position/normal/
		//! texcoord) -> convertToMesh -> buildTangentVectors | next=v1::Manual-
		//! Object section per Section -> convertToMesh -> buildTangentVectors ->
		//! Mesh::importV1, sub-mesh material names written after the import (the
		//! MeshLoaderNext recipe) | filament=VertexBuffer/IndexBuffer per section
		bool createMeshFromData(String const & meshName,
			MeshBuilder::Mesh const & data);
		//! @brief make sure the mesh resource behind a `.omesh` ASSET name
		//! exists, parsing and building it on first use - the road that lets
		//! `ModelComponent.mesh` (and every other createMeshInstance caller)
		//! name a `.omesh` exactly like a `.glb`. Both backends call it from
		//! createMeshInstance, so no consumer needs a procedural branch.
		//! A name that is not a `.omesh`, or a resource that already exists, is
		//! a no-op returning true. The `.omesh` text is read through the
		//! resource system (so it resolves out of a pak/APK like any asset), its
		//! `extrude`/`revolve` shape references resolve the same way, and each
		//! section's authored material reference is turned into a live material
		//! through the SAME `Omat/<file>` convention ModelComponent uses - so a
		//! `.omesh` and a `ModelComponent.material` name the same materials.
		//! @return false + one honest log line on a missing file, a parse error
		//! (with its line) or a build failure. Flavor-NEUTRAL: implemented once
		//! over this facade, not per backend.
		bool ensureMeshAsset(String const & meshName);
		//! @brief destroy a mesh resource this facade GENERATED (@see
		//! createLineListMesh, createMeshFromData) - the honest sibling: a
		//! caller that regenerates geometry under a fresh name every rebuild,
		//! or replaces it under the same name, must drop the old mesh or leak
		//! GPU meshes.
		//! @warning ONLY call with NO live MeshInstance still using the mesh -
		//! destroy the instance first. A no-op when the name does not exist.
		//! map: classic=MeshManager::remove (the .manual object was destroyed at
		//! create time) | next=MeshManager::remove for the v2 mesh AND v1::Mesh-
		//! Manager::remove for the "meshName/v1import" intermediate the import
		//! recipe keeps alive | filament=destroy the VertexBuffer/IndexBuffer
		void destroyGeneratedMesh(String const & meshName);
		//! @brief does a mesh resource of this name exist (generated or loaded)
		//! - the create/destroy round-trip probe helper geometry callers assert
		//! on, and the idempotence gate of the generated-mesh services.
		//! map: classic=MeshManager::resourceExists | next=MeshManager::getByName (the v2 mesh) | filament=facade registry lookup
		bool generatedMeshExists(String const & meshName) const;

		//--- global lighting ---
		//! @brief the ambient light minimum every app sets today
		//! map: classic/next=SceneManager::setAmbientLight (next takes hemisphere upper/lower - impl passes colour twice) | filament=IndirectLight intensity or ambient term in material
		void setAmbientLight(Color const & colour);
		//! map: classic/next=SceneManager::getAmbientLight | filament=cached facade value
		Color const & getAmbientLight() const;
		//! @brief two-colour sky/ground ambient: @p upperHemisphere lights
		//! surfaces facing +Y (the sky term), @p lowerHemisphere those facing -Y
		//! (the ground bounce) - the mobile-cheap image-based-lighting stand-in
		//! @remarks setAmbientLight(colour) is the flat special case
		//! (setAmbientHemisphere(colour, colour)).
		//! map: next=SceneManager::setAmbientLight(upper, lower, UNIT_Y) - the
		//! native hemisphere term | classic=SceneManager::setAmbientLight with the
		//! averaged colour (the flavor has flat ambient only - an honest subset) |
		//! filament=IndirectLight (SH ambient)
		void setAmbientHemisphere(Color const & upperHemisphere,
			Color const & lowerHemisphere);
		//! the upper-hemisphere (sky) ambient colour last set (@see setAmbientHemisphere)
		Color const & getAmbientHemisphereUpper() const;
		//! the lower-hemisphere (ground) ambient colour last set (@see setAmbientHemisphere)
		Color const & getAmbientHemisphereLower() const;

		//--- dynamic shadows ---
		//! whether this backend renders dynamic shadow maps at all is the
		//! `RenderCaps::DynamicShadows` capability (`RenderSystem::supports`).
		//! @brief the coarse shadow quality knob (budgets per step in
		//! core_util/ShadowPreset.h; live-tunable via the `r.shadowQuality`
		//! cvar the app host registers). Shadow maps render only while the
		//! knob is not SQ_OFF AND at least one light asked to cast
		//! (RenderLight::setCastShadows(true) - LightComponent.castsShadows);
		//! until then the scene pays neither memory nor per-frame cost.
		//! v1 scope: DIRECTIONAL casters (cascaded/PSSM maps); point/spot
		//! lights accept the cast flag but throw no maps yet.
		//! A knob change while armed re-arms the technique with the new tier
		//! (disarm + arm - the restore-exactly discipline makes it cheap), so
		//! a live `r.shadowQuality` cvar flip reconfigures mid-play.
		//! map: classic=scene-level RTSS integrated PSSM: depth shadow
		//! textures + PSSMShadowCameraSetup + the shadow-mapping receiver
		//! sub-render-state injected into the ONE generated-material scheme;
		//! armed only while a directional light casts, disarmed
		//! restore-exactly (technique NONE, maps freed, receiver removed). A
		//! GLES2 context without depth-texture render targets refuses with
		//! ONE log line (the runtime capability gate behind the
		//! RenderCaps::DynamicShadows bit) | next=compositor shadow node
		//! (PSSM + PCF; the 1-split low tier is a single focused map) in the
		//! window and RTT workspaces | filament=per-light
		//! LightManager::setShadowCaster + shadow options
		void setShadowQuality(ShadowPreset::Quality quality);
		//! the knob position last set (default SQ_MEDIUM - the phone budget)
		ShadowPreset::Quality getShadowQuality() const;

		//--- projected decals (surface marks) ---
		//! whether this backend renders TRUE projected decals (vs the classic
		//! surface-aligned quad subset) is the `RenderCaps::ProjectedDecals`
		//! capability (`RenderSystem::supports`).
		//! @brief the hard cap on concurrently VISIBLE decals - the mobile-budget
		//! discipline (live-tunable via the `r.maxDecals` cvar the app host
		//! registers). When more decals exist than the cap allows, the OLDEST are
		//! hidden (not destroyed - their handles still live); a cap of 0 hides
		//! every decal, byte-identical to a scene with none (the escape hatch and
		//! the toggle-identity gate). Default is a generous desktop ceiling; a
		//! phone project lowers it. Applied immediately: lowering the cap live
		//! hides the excess, raising it re-shows the newest up to the new cap.
		//! map: classic/next=facade-side visible-decal registry (creation order)
		void setMaxDecals(unsigned int maxDecals);
		//! the decal cap last set (@see setMaxDecals)
		unsigned int getMaxDecals() const;
		//! the number of decals currently VISIBLE under the budget (<= the cap;
		//! selfcheck/introspection - the eviction and toggle-identity probe)
		unsigned int getVisibleDecalCount() const;

		//--- image-based lighting (skybox-sourced, opt-in) ---
		//! whether this backend renders cubemap-sourced image-based lighting
		//! at all is the `RenderCaps::IblReflections` capability
		//! (`RenderSystem::supports` - classic answers per device: the
		//! generated-shader path needs GLSL ES 3.0 on a GLES context).
		//! @brief opt into image-based lighting sourced from the scene's
		//! skybox cubemap (AtmosphereDesc::skyboxTexture): PBS-lit facade
		//! materials (surface + water) gain cubemap specular reflections and
		//! a cubemap diffuse fill, ADDED on top of the analytic lights and
		//! ambient - never replacing them. @p intensity scales the added
		//! contribution (1 = the cubemap's own brightness).
		//! Renders only while enabled AND the quality knob is not IQ_OFF AND
		//! an enabled skybox-type atmosphere shows a loaded cubemap; enabling
		//! without a skybox source logs one honest line and renders unchanged
		//! (procedural/colour skies have no cubemap to sample - v1 scope).
		//! Default OFF: content that never opts in renders byte-identically.
		//! map: next=HlmsPbs reflection map (PBSM_REFLECTION) on the generated
		//! PBS datablocks + the scene envmapScale/diffuse-GI env features |
		//! classic=the shader-generator image-based-lighting stage appended to
		//! the generated Cook-Torrance materials (DFG LUT + the same cubemap;
		//! needs GLSL ES 3.0 on GLES - refused honestly with one log line) |
		//! filament=IndirectLight from the cubemap
		void setImageLighting(bool enabled, Real intensity = Real(1));
		//! the opt-in last set (default false)
		bool getImageLightingEnabled() const;
		//! the intensity last set (default 1)
		Real getImageLightingIntensity() const;
		//! @brief the coarse IBL quality knob (budgets per step in
		//! core_util/IblPreset.h; live-tunable via the `r.iblQuality` cvar the
		//! app host registers). A knob change while image lighting renders
		//! re-arms the environment chain at the new tier (drop leading mips
		//! of the skybox cubemap to the tier cap - cheap re-upload), so a
		//! live cvar flip reconfigures mid-play; IQ_OFF disarms exactly.
		void setIblQuality(IblPreset::Quality quality);
		//! the knob position last set (default IQ_MEDIUM - the phone budget)
		IblPreset::Quality getIblQuality() const;

		//--- sky / fog / atmosphere ---
		//! whether this backend renders a real atmospheric sky dome (vs a flat
		//! clear colour) is the `RenderCaps::SkyDome` capability
		//! (`RenderSystem::supports`).
		//! @brief set the scene's sky/fog atmosphere (@see AtmosphereDesc).
		//! Idempotent - call again to change the look or animate time of day.
		//!
		//! SUN LINKAGE: while enabled the atmosphere links to the FIRST
		//! directional light in the world (creation order) and reads that
		//! light's current direction as the sun, then drives the light's
		//! colour/power/direction from the atmospheric model so sun and sky
		//! stay consistent. Orient that light (via its node) to place the sun /
		//! sweep a day-night arc, then re-call to resolve it; with no
		//! directional light the sky renders with a default overhead sun. A
		//! second-sun / explicit-light setter can come later if a game needs it.
		//!
		//! SKY TYPE (AtmosphereDesc::skyType): what fills the sky pixels -
		//! the procedural dome (default), a cubemap skybox
		//! (AtmosphereDesc::skyboxTexture, a single cubemap image both
		//! flavors load natively) or the flat sky-tint clear. Fog and the sun
		//! drive are sky-type-independent; the AMBIENT fill is the sky model's
		//! own light, so only the procedural type drives it - a skybox or
		//! colour sky fills from the ambient the scene authored through
		//! setAmbientHemisphere (@see AtmosphereDesc).
		//!
		//! map: next=Ogre::AtmosphereNpr (sky dome + HlmsPbs-integrated object
		//! fog + linked directional sun; sky media from the ogre-next port;
		//! skybox = the native SceneManager cubemap sky quad, same media set) |
		//! classic=vertex-colour gradient sky dome + fixed-function scene fog
		//! + flat window clear colour (skybox = the native camera-bound
		//! SceneManager sky box over a generated cubemap material) |
		//! filament=Skybox + fog
		void setAtmosphere(AtmosphereDesc const & desc);
		//! the atmosphere description last set (default: disabled)
		AtmosphereDesc const & getAtmosphere() const;

		//--- lighting suppression (the editor's flat "unlit" inspection view) ---
		//! @brief render the WHOLE world flat (albedo + a bright flat ambient, every
		//! analytic light's contribution removed) - the editor Scene view's "unlit"
		//! inspection look. GLOBAL, not per-target: the previous-attempt per-target
		//! route is impossible (Ogre-Next gathers directional lights from the global
		//! light list, unfiltered by the per-pass mask - @see RenderCaps::
		//! SceneUnlitView; classic has no per-viewport lighting override), so this is
		//! a per-frame GLOBAL state. The editor drives it from panel visibility (it
		//! is only armed when the Scene view is the visible tab AND the Game Preview
		//! is not, so the frame the flat look applies to is a Scene-only frame - see
		//! Docs). RECOVER-THEN-REAPPLY (the ScreenShake precedent): arming snapshots
		//! the ambient + every light's visibility and applies the flat look;
		//! releasing restores them EXACTLY (a suppressed frame followed by a restored
		//! frame leaves the light/ambient state byte-identical). Idempotent: a no-op
		//! unless the flag changes, so the editor calls it every frame and it flips
		//! only on the visibility decision change. Flips on tab switches, not per
		//! frame, so the zero-light Hlms shader variant caches after first use.
		//! map: next=snapshot+hide every Ogre::Light + flat-white SceneManager
		//! ambient, restore on release | classic=the same (Ogre lights + ambient) |
		//! filament=disable LightManager instances + IndirectLight, restore
		void setLightingSuppressed(bool suppressed);
		//! @see RenderWorld::setLightingSuppressed (default false)
		bool getLightingSuppressed() const;

		//--- scene wireframe (the editor's Scene-view WIREFRAME inspection) ---
		//! @brief render the 3D SCENE geometry in wireframe (line-fill) polygon
		//! mode - the editor Scene view's wireframe look. The seam is ASYMMETRIC
		//! by construction (@see RenderCaps::SceneWireframeView):
		//!  - classic honors a PER-TARGET wireframe (@see RenderTexture::
		//!    setViewMode) - the Scene RTT's own camera flips to PM_WIREFRAME,
		//!    leak-free, and this GLOBAL route is a no-op there;
		//!  - next has no per-pass/per-target polygon override (Ogre-Next bakes
		//!    polygon mode into the pipeline state object), so this GLOBAL route
		//!    is the road: it flips the macroblock polygon mode of the 3D-SCENE
		//!    datablock set ONLY (PBS mesh/material/water) and NEVER the 2D/UI set
		//!    (sprites, vector shapes, dynamic lines, the editor's own ImGui
		//!    chrome + gui), so the scene wireframes while the editor UI stays
		//!    solid. Being global, the editor arms it ONLY on a frame where the
		//!    Scene view is the ONE game view rendering (the one-game-view render
		//!    invariant guarantees the Game Preview / camera inset / Play never
		//!    share that frame, so wireframe cannot leak into them) - exactly the
		//!    dock-tab discipline setLightingSuppressed uses, and the two compose
		//!    (wireframe + lighting-off = flat unlit wireframe).
		//! RECOVER-THEN-REAPPLY: releasing restores every scene datablock's
		//! polygon mode to PM_SOLID (the created state), byte-exact; only the
		//! polygon-mode lane is touched, so a concurrent macroblock change
		//! (shadows, alpha-test cull) composes. Idempotent - flips only on the
		//! decision change (tab switch), never per frame, so the wireframe PSO
		//! variant caches after first use.
		//! map: next=flip the DT_SCENE datablock set's macroblock polygon mode
		//! (RenderBackend::setGlobalWireframe) | classic=no-op (per-target via
		//! RenderTexture::setViewMode) | filament=n/a
		void setSceneWireframe(bool enabled);
		//! @see RenderWorld::setSceneWireframe (default false)
		bool getSceneWireframe() const;

		//--- scene wireframe OVERLAY (the editor's Scene-view SHADED+WIREFRAME) ---
		//! @brief render the shaded scene with a thin wireframe drawn ON TOP of it
		//! (line edges over the lit surfaces) - the editor Scene view's
		//! SHADED+WIREFRAME look (@see RenderCaps::SceneWireframeOverlayView).
		//! THE TRIO of Scene-view polygon looks, three DIFFERENT mechanics:
		//!  - Shaded: the default solid look, nothing armed;
		//!  - Wireframe: a polygon-mode FLIP (@see setSceneWireframe) - the scene
		//!    geometry renders line-fill INSTEAD of solid (classic per-target,
		//!    next the DT_SCENE macroblock flip);
		//!  - ShadedWireframe: OVERLAY ITEMS (this seam) - the shaded pass renders
		//!    UNTOUCHED and a second renderable per scene mesh adds the lines,
		//!    never a mid-frame state flip (impossible to bracket per-target on
		//!    next's baked PSO, unbuilt as a second pass on classic).
		//! The two flips are RADIO-EXCLUSIVE with this overlay: only one is armed
		//! at a time, and Wireframe->ShadedWireframe->Shaded all restore byte-exact.
		//! While armed, every scene-tier MESH INSTANCE gets a SECOND renderable
		//! sharing its mesh + node, drawn with ONE shared unlit near-black
		//! wireframe datablock/material whose macroblock carries polygon-mode
		//! wireframe + a small DEPTH BIAS so the lines sit on the shaded surface
		//! without z-fighting. The overlays: inherit the source node/transform;
		//! carry @p editorVisibilityFlags (the editor-only visibility bit, masked
		//! OFF the Game Preview target - so overlays never leak into preview/Play,
		//! belt-and-suspenders over the one-game-view invariant that already keeps
		//! this armed only on a Scene-view frame); cast no shadows; are never
		//! pickable (query flags 0). Composes with lighting-off (flat unlit + lines
		//! is a legitimate combo). The overlay set REBUILDS when the scene item set
		//! changes (live create/delete/mesh-swap): a source created while armed
		//! gains an overlay, a destroyed one loses it; disarm destroys the whole
		//! set and restores nothing else (nothing else changed). ANIMATED/SKINNED
		//! meshes are EXCLUDED in this version - static scene geometry is the
		//! mode's habitat, a skinned overlay would need its own animation state;
		//! soft-body/vector 2D content is scene-tier-excluded by construction.
		//! Idempotent: the editor calls it every frame with the armed decision.
		//! map: next=a second Ogre::Item per source Item with the shared HlmsUnlit
		//! wireframe datablock (RenderBackend scene-item registry) | classic=a
		//! second Ogre::Entity per source with the shared wireframe Material |
		//! filament=n/a
		void setSceneWireframeOverlay(bool enabled,
			unsigned int editorVisibilityFlags);
		//! @see RenderWorld::setSceneWireframeOverlay (default false)
		bool getSceneWireframeOverlay() const;
		//! @brief how many wireframe companion renderables are live right now
		//! (self-check introspection): 0 while disarmed, else one per eligible
		//! scene-tier mesh instance. The editor selfcheck watches this grow/shrink
		//! as objects are added/removed while armed (@see setSceneWireframeOverlay).
		size_t getSceneWireframeOverlayCount() const;

		//--- LDR bloom (highlight glow post-process) ---
		//! whether this backend renders the bloom post-process at all is the
		//! `RenderCaps::Bloom` capability (`RenderSystem::supports`) - true on
		//! both desktop flavors, runtime-gated to false on a classic
		//! GLES2/WebGL context (unproven there pending an on-device/browser
		//! run of the compositor chain, see Docs/render-abstraction.md), where
		//! an enabled bloom degrades to no pass + one honest log line and the
		//! scene still renders correctly.
		//! @brief set the scene's LDR bloom (@see BloomDesc). Per-scene OPT-IN
		//! and DEFAULT OFF: while @c desc.enabled is false (or the quality knob
		//! is BQ_OFF) NO bloom pass runs and the frame is byte-identical to a
		//! build with no bloom code (the toggle-identity discipline).
		//! Idempotent - call again to change the threshold/intensity or toggle.
		//!
		//! SCOPE: bloom wraps the 3D scene only. The 2D tier (sprites, vector
		//! shapes, gui) is EXCLUDED by contract so UI whites and flat-colour 2D
		//! art stay crisp - the backends sequence the bloom pass BEFORE the
		//! 2D + UI composition (@see the per-flavor mechanism in
		//! Docs/render-abstraction.md). LDR threshold: the bright-pass extracts
		//! the pixels already brighter than @c threshold in the clamped [0;1]
		//! target (near-white highlights) - it cannot bloom over-bright values
		//! above 1.0 (there is no HDR/tonemap here yet - a future next-first
		//! phase). @see BloomDesc for the threshold/intensity semantics.
		//!
		//! map: next=CompositorManager2 quad passes (bright-pass -> separable
		//! blur -> additive combine) inserted between the 3D scene pass and the
		//! 2D/UI pass in the window workspace, gated on enabled + quality |
		//! classic=an Ogre::CompositorManager viewport compositor (the same
		//! bright/blur/combine chain, generated in code) armed on the window
		//! viewport while enabled + quality, disarmed restore-exactly |
		//! filament=View bloom options
		void setBloom(BloomDesc const & desc);
		//! the bloom description last set (default: disabled - BloomDesc())
		BloomDesc const & getBloom() const;
		//! @brief the coarse bloom quality/cost knob (blur budget per step in
		//! core_util/BloomPreset.h; live-tunable via the `r.bloomQuality` cvar
		//! the app host registers). The bloom pass renders only while this knob
		//! is not BQ_OFF AND a scene enabled bloom (@see setBloom). A knob change
		//! while a scene has bloom on re-arms the pass with the new budget
		//! (restore-exactly), so a live `r.bloomQuality` flip reconfigures
		//! mid-play. Default BQ_MEDIUM (the phone budget).
		void setBloomQuality(BloomPreset::Quality quality);
		//! the bloom quality knob last set (default BQ_MEDIUM)
		BloomPreset::Quality getBloomQuality() const;

		//--- output grade (the shared authored look) ---
		//! whether this backend renders the output grade at all is the
		//! `RenderCaps::OutputGrade` capability (`RenderSystem::supports`) - true
		//! on both desktop flavors, runtime-gated to false on a classic
		//! GLES2/WebGL1 context (needs GLSL ES 3.0 on a GLES target, like bloom),
		//! where an enabled grade degrades to no pass + one honest log line and
		//! the scene still renders correctly.
		//! @brief set the scene's output GRADE (@see GradeDesc): the ONE authored
		//! look stage (contrast S-curve + saturation) both flavors run
		//! IDENTICALLY (the shared curve is core_util/GradeMath), so whatever the
		//! content dials stays matched across flavors by construction. Per-scene
		//! OPT-IN and DEFAULT OFF: while @c desc.enabled is false NO grade pass
		//! runs and the frame is byte-identical to a build with no grade code (the
		//! toggle-identity discipline). Idempotent - call again to change the
		//! contrast/saturation or toggle.
		//!
		//! SCOPE: the grade wraps the 3D scene only. The 2D tier (sprites, vector
		//! shapes, gui) is EXCLUDED by contract so UI whites and flat-colour 2D art
		//! stay crisp and WYSIWYG - the backends sequence the grade pass AFTER the
		//! 3D scene (and after bloom when both are on: the grade is the LAST thing
		//! before the 2D + UI composition) but BEFORE the 2D/UI passes. The curve
		//! operates in display space (@see GradeMath for the colour-space contract
		//! and the cross-flavor parity guarantee).
		//!
		//! map: next=a CompositorManager2 grade quad pass appended after the 3D
		//! scene pass (and after the bloom combine when both are on) in the window
		//! workspace, gated on enabled | classic=an Ogre::CompositorManager
		//! viewport compositor (the grade quad over the generated-material scheme,
		//! generated in code) armed on the window viewport while enabled, disarmed
		//! restore-exactly | filament=View colour-grading options
		void setOutputGrade(GradeDesc const & desc);
		//! the grade description last set (default: disabled - GradeDesc())
		GradeDesc const & getOutputGrade() const;

		//--- queries (editor picking) ---
		//! @brief all scene content whose bounds the ray hits, nearest first
		//! @param queryMask only content with overlapping query flags is
		//! returned (facade content defaults to QUERYFLAG_DEFAULT; the editor
		//! grid opts out with 0)
		//! map: classic=SceneManager::createRayQuery/execute/destroyQuery | next=same (v2 RaySceneQuery) | filament=no scene query - impl-side AABB walk (or View::pick GPU picking)
		std::vector<RayQueryHit> queryRay(Ray3 const & ray,
			unsigned int queryMask = 0xFFFFFFFF) const;

		//--- default query flags for facade-created content ---
		static const unsigned int QUERYFLAG_DEFAULT;	//!< query flags new content starts with (1)
	protected:
		//! worlds are created by RenderSystem only
		RenderWorld();
	private:
		RenderWorld(RenderWorld const &);				// non-copyable
		RenderWorld & operator=(RenderWorld const &);	// non-copyable
	};
}

#endif //__RenderWorld_h__8_7_2026__12_00_00__
