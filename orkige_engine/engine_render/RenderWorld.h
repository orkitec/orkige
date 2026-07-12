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
#include "engine_render/VectorMesh.h"
#include <core_util/ShadowPreset.h>
#include <core_util/AtmosphereDesc.h>
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
		//! refilled from a CPU vertex array each frame (@see SpriteBatch)
		//! map: classic=ManualObject + shared "Sprite/<tex>"/"SpriteAdd/<tex>" material | next=v2 ManualObject + shared HlmsUnlit datablock | filament=dynamic VB/IB + unlit filamat instance
		optr<SpriteBatch> createSpriteBatch(String const & textureName,
			SpriteBatch::BlendMode blendMode = SpriteBatch::BLEND_ALPHA);
		//! @brief create a world-space untextured vertex-coloured triangle mesh
		//! - the flat-colour vector-shape building block (fills + alpha-feather
		//! edge), refilled from a CPU vertex+index array (@see VectorMesh)
		//! map: classic=ManualObject + shared "VectorFill" material | next=v2
		//! ManualObject + shared HlmsUnlit vertex-colour datablock | filament=
		//! dynamic VB/IB + unlit filamat instance
		optr<VectorMesh> createVectorMesh();
		//! @brief create a camera (attach it to a node to place it)
		//! map: classic/next=SceneManager::createCamera | filament=Engine::createCamera(entity)
		optr<RenderCamera> createCamera(String const & name = "");
		//! @brief create a light (attach it to a node to place it)
		//! map: classic/next=SceneManager::createLight | filament=LightManager::Builder
		optr<RenderLight> createLight();

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
		//! @brief does this backend render dynamic shadow maps at all?
		//! (capability probe, @see RenderTexture::canOwnLayers precedent)
		//! map: classic=false (no dynamic shadows on the compatibility flavor -
		//! honest "no", see setShadowQuality) | next=true | filament=true
		static bool shadowsSupported();
		//! @brief the coarse shadow quality knob (budgets per step in
		//! core_util/ShadowPreset.h; live-tunable via the `r.shadowQuality`
		//! cvar the app host registers). Shadow maps render only while the
		//! knob is not SQ_OFF AND at least one light asked to cast
		//! (RenderLight::setCastShadows(true) - LightComponent.castsShadows);
		//! until then the scene pays neither memory nor per-frame cost.
		//! v1 scope: DIRECTIONAL casters (cascaded/PSSM maps); point/spot
		//! lights accept the cast flag but throw no maps yet.
		//! map: classic=accepted + ONE "not supported" log line, renders
		//! nothing (the knob still round-trips) | next=compositor shadow node
		//! (PSSM + PCF) in the window and RTT workspaces | filament=per-light
		//! LightManager::setShadowCaster + shadow options
		void setShadowQuality(ShadowPreset::Quality quality);
		//! the knob position last set (default SQ_MEDIUM - the phone budget)
		ShadowPreset::Quality getShadowQuality() const;

		//--- sky / fog / atmosphere ---
		//! @brief does this backend render a real sky dome + atmospheric fog?
		//! (capability probe, mirrors shadowsSupported)
		//! map: classic=false (flat clear-colour sky + fixed-function fog only -
		//! the honest subset, @see setAtmosphere) | next=true (AtmosphereNpr) |
		//! filament=true
		static bool skyDomeSupported();
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
		//! map: next=Ogre::AtmosphereNpr (sky dome + HlmsPbs-integrated object
		//! fog + linked directional sun; sky media from the ogre-next port) |
		//! classic=fixed-function scene fog + flat window clear colour, NO sky
		//! dome (logged once - the honest subset) | filament=Skybox + fog
		void setAtmosphere(AtmosphereDesc const & desc);
		//! the atmosphere description last set (default: disabled)
		AtmosphereDesc const & getAtmosphere() const;

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
