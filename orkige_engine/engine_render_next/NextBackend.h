/********************************************************************
	created:	Wednesday 2026/07/08 at 20:00
	filename: 	NextBackend.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __NextBackend_h__8_7_2026__20_00_00__
#define __NextBackend_h__8_7_2026__20_00_00__

//! @file NextBackend.h
//! @brief PRIVATE plumbing of the Ogre-Next engine_render backend
//! @remarks The Ogre-Next counterpart of engine_render_classic/
//! ClassicBackend.h: the one place that pairs the backend-free facade
//! headers with Ogre-Next types. Only the engine_render_next/*.cpp TUs,
//! the flavor's app-layer boot bridge (engine_graphic/EngineNext.cpp -
//! this flavor's Engine.cpp counterpart) and the per-backend test
//! bootstraps (tests/render_facade/bootstrap_next.cpp, the
//! render_next_smoke main) may include it.
//!
//! B2 STATE (Docs/render-abstraction.md phase A2/WP-A2.2+A2.3): the
//! whole facade is implemented - boot (Root + Metal RS + SDL-hosted
//! window + CompositorManager2 workspace), nodes/cameras, mesh
//! instances (assimp import -> v1::ManualObject -> Mesh::importV1 ->
//! Item; .mesh via the v1 serializer + importV1), HLMS PBS/Unlit
//! datablock generation (the backend's whole material surface),
//! sprite quads (v2 ManualObject + shared per-texture HlmsUnlit
//! datablock), lights, RTT (TextureGpu + workspace-per-target), AABB
//! ray queries, frame stats (RenderingMetrics) and the cube-mesh
//! service. render_facade_selfcheck passes on this backend (enabled
//! in ctest, preset desktop-next). Remaining honest gaps, each logged
//! once via notImplementedOnce: LT_ZIP/LT_BIGZIP resource locations
//! (waits for real content work + the zziplib port feature) and
//! skeletal animation IMPORT through the assimp path (the animation
//! control surface is implemented over v2 SkeletonInstance, but the
//! B2 importer bakes node transforms - static meshes only).
//!
//! Unlike classic (where Engine bootstraps OGRE and the facade wraps
//! it), the RenderSystem facade IS the boot on Next: RenderBackend::
//! createRenderSystem(NextBootOptions) brings the whole renderer up.

#include "engine_render/RenderPrerequisites.h"
#include "engine_render/RenderMath.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"
#include "engine_render/MeshInstance.h"
#include "engine_render/SpriteQuad.h"
#include "engine_render/RenderCamera.h"
#include "engine_render/RenderLight.h"
#include "engine_render/RenderTexture.h"

#include <core_debug/DebugMacros.h>	// oAssert (classic gets it via the EnginePrerequisites umbrella, which this flavor does not compile)

#include <chrono>
#include <vector>

#ifndef ORKIGE_RENDER_NEXT
#	error "engine_render_next compiled without ORKIGE_RENDER_NEXT - the build flavor wiring regressed (see ORKIGE_RENDER_BACKEND in CMake)"
#endif

namespace Ogre
{
	class Root;
	class Window;
	class SceneManager;
	class SceneNode;
	class Camera;
	class CompositorWorkspace;
	class Item;
	class ManualObject;	// the v2 one (OgreManualObject2.h)
	class Light;
	class TextureGpu;
	class HlmsDatablock;
}

namespace Orkige
{
	//--- backend state of the facade classes (pimpl bodies) -----------

	struct RenderSystem::Impl
	{
		Ogre::Root*			root = NULL;			//!< owned by the backend (destroyRenderSystem)
		Ogre::Window*		window = NULL;			//!< owned by root
		RenderWorld*		world = NULL;			//!< owned, deleted in ~RenderSystem
		optr<RenderCamera>	windowCamera;			//!< camera shown full-window (keeps it alive)
		Ogre::ColourValue	windowBackground = Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f);
		Ogre::CompositorWorkspace*	workspace = NULL;	//!< the window clear/render workspace (owned by CompositorManager2)
		//--- frame timing (FrameStats on a backend without per-target stats)
		std::chrono::steady_clock::time_point	lastFrameTime;
		bool				haveLastFrameTime = false;
		float				lastFPS = 0.0f;
		float				avgFPS = 0.0f;
	};

	struct RenderWorld::Impl
	{
		Ogre::SceneManager*	sceneManager = NULL;	//!< owned by the backend (root teardown)
		optr<RenderNode>	rootNode;				//!< stable facade handle of the root node (owned=false)
		Ogre::ColourValue	ambient = Ogre::ColourValue(0.2f, 0.2f, 0.2f, 1.0f);	//!< facade cache (Next splits ambient into hemispheres)
	};

	struct RenderNode::Impl
	{
		Ogre::SceneNode*	node = NULL;
		Ogre::SceneManager*	creator = NULL;
		bool				owned = true;			//!< false for the world root (never destroyed by the handle)
		void*				userPointer = NULL;		//!< @see RenderNode::setUserPointer
		woptr<RenderNode>	parent;					//!< facade graph mirror (backend child lists are never walked)
		std::vector<woptr<RenderNode>>	children;	//!< facade graph mirror, pruned lazily
		//! v2 nodes store transforms SoA and return them BY VALUE - the
		//! facade getters return const refs, so the last read is cached
		mutable Ogre::Vector3		positionCache = Ogre::Vector3::ZERO;
		mutable Ogre::Quaternion	orientationCache = Ogre::Quaternion::IDENTITY;
		mutable Ogre::Vector3		scaleCache = Ogre::Vector3::UNIT_SCALE;
	};

	struct MeshInstance::Impl
	{
		Ogre::Item*			item = NULL;			//!< the v2 entity equivalent
		Ogre::SceneManager*	creator = NULL;
		String				meshName;
		optr<RenderNode>	attachedTo;				//!< keeps the node alive while content hangs off it
	};

	struct SpriteQuad::Impl
	{
		Ogre::ManualObject*	quad = NULL;			//!< v2 manual object (VaoManager-backed)
		Ogre::SceneManager*	creator = NULL;
		String				textureName;
		String				datablockName;			//!< the shared per-texture "Sprite/<tex>" HlmsUnlit datablock
		float				texelWidth = 0.0f;		//!< texture size in texels (aspect derivation)
		float				texelHeight = 0.0f;
		float				width = 0.0f;			//!< configured size; <= 0 derives from the texture aspect
		float				height = 0.0f;
		float				u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
		Ogre::ColourValue	tint = Ogre::ColourValue::White;
		bool				flipX = false;
		bool				flipY = false;
		int					zOrder = 0;
		optr<RenderNode>	attachedTo;

		//! rebuild the quad vertex data from the state above (same honest
		//! sprite rules as the classic backend: tint/flips in vertex data)
		void rebuild();
	};

	struct RenderCamera::Impl
	{
		Ogre::Camera*		camera = NULL;
		Ogre::SceneManager*	creator = NULL;
		bool				owned = true;
		optr<RenderNode>	attachedTo;
	};

	struct RenderLight::Impl
	{
		Ogre::Light*		light = NULL;
		Ogre::SceneManager*	creator = NULL;
		optr<RenderNode>	attachedTo;
	};

	struct RenderTexture::Impl
	{
		String				name;
		unsigned int		width = 0;
		unsigned int		height = 0;
		optr<RenderCamera>	camera;					//!< keeps the fed camera alive
		Ogre::TextureGpu*			texture = NULL;	//!< RenderToTexture target (owned)
		Ogre::CompositorWorkspace*	workspace = NULL;	//!< renders the camera into the texture
		Ogre::ColourValue	background = Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f);
		//! facade caches: the Next flavor compiles no overlay component and
		//! the basic workspace has no shadow node, so "off" already holds
		//! structurally - the flags are kept for a future workspace upgrade
		bool				overlaysEnabled = true;
		bool				shadowsEnabled = true;

		//! (re)create texture + workspace from the state above
		//! (resize-by-recreate, same contract as the classic backend)
		void recreate();
		//! drop workspace + texture (dtor and the recreate path)
		void destroyTarget();
	};

	//--- the backend hub -----------------------------------------------
	//! @brief the Ogre-Next backend's cross-class door (befriended by
	//! every facade class, @see RenderPrerequisites.h)
	struct RenderBackend
	{
		//! @brief everything the Next boot needs from the host app
		//! @remarks the sanctioned boot block of a Next-flavor app is
		//! exactly: SDL window creation + filling this struct + one
		//! createRenderSystem call (the classic counterpart is the
		//! Engine ctor/setup block, see Docs/render-abstraction.md)
		struct NextBootOptions
		{
			String			windowTitle = "Orkige";
			unsigned int	width = 1280;
			unsigned int	height = 720;
			//! stringified native handle (NSWindow* on macOS - the
			//! engine_util/SDLNativeWindow.mm bridge); empty lets
			//! Ogre-Next create its own window (tests/headless)
			String			nativeWindowHandle;
			String			logFileName = "orkige_next.log";
			//! directory containing Hlms/{Common,Pbs,Unlit} shader
			//! templates (the ogre-next port ships them under
			//! share/ogre-next/Media); empty skips Hlms registration
			//! (enough for clear-only rendering, nothing with materials)
			String			hlmsMediaDir;
		};

		//--- lifecycle ---------------------------------------------
		//! boot Ogre-Next (Root + Metal RS + window + world) and create
		//! the facade RenderSystem; idempotent (returns the live system)
		static RenderSystem* createRenderSystem(NextBootOptions const & options);
		//! tear the facade AND the Ogre-Next root down again (idempotent)
		static void destroyRenderSystem();
		//! the live RenderSystem or NULL (backs RenderSystem::get)
		static RenderSystem* system();

		//--- handle factories (protected facade ctors) --------------
		//! wrap an existing backend node into a facade handle and
		//! register it; parent is the facade-graph parent (NULL for root)
		static optr<RenderNode> wrapNode(Ogre::SceneNode* node, bool owned,
			optr<RenderNode> const & parent);
		static optr<RenderCamera> createCamera(
			Ogre::SceneManager* sceneManager, String const & name);
		//! NULL + a log line when the mesh cannot be resolved/imported
		static optr<MeshInstance> createMeshInstance(
			Ogre::SceneManager* sceneManager, String const & meshName);
		//! NULL + a log line when the texture cannot be loaded
		static optr<SpriteQuad> createSpriteQuad(
			Ogre::SceneManager* sceneManager, String const & textureName);
		static optr<RenderLight> createLight(Ogre::SceneManager* sceneManager);
		static optr<RenderTexture> createRenderTexture(String const & name,
			unsigned int width, unsigned int height);

		//--- mesh import (MeshLoaderNext.cpp) ------------------------
		//! @brief make sure a v2 mesh named meshName exists (idempotent)
		//! @remarks THE Next mesh path decided in B2: an existing v2 mesh
		//! is used as-is (cube-mesh service output); "*.mesh" loads
		//! through the v1 serializer; everything else (glb/gltf/obj/...)
		//! imports through assimp - both v1 roads end in
		//! MeshManager::createByImportingV1. The assimp road also
		//! generates one HLMS datablock per sub-mesh (PBS: diffuse
		//! colour/texture, incl. glb-embedded textures). Node transforms
		//! are baked (aiProcess_PreTransformVertices): static meshes
		//! only until a skeletal need appears (logged once).
		//! @return false + a log line when the resource is missing/broken
		static bool ensureV2Mesh(Ogre::SceneManager* sceneManager,
			String const & meshName);
		//! the backend cube-mesh service (v2 mesh + shared vertex-colour
		//! unlit datablock; same recipe/palette as classic PrimitiveUtil)
		static void createVertexColourCubeMesh(Ogre::SceneManager* sceneManager,
			String const & meshName, Real halfExtent);

		//--- texture / datablock services (the material surface) -----
		//! load a 2D texture through the resource system (any group);
		//! metadata (texel size) is ready on return; NULL when missing
		static Ogre::TextureGpu* loadTexture2D(String const & textureName);
		//! create a 2D texture from an in-memory encoded image (png/jpg
		//! bytes - glb-embedded textures); idempotent per name
		static Ogre::TextureGpu* createTexture2DFromMemory(String const & name,
			void const * bytes, size_t sizeBytes, String const & formatHint);
		//! the shared per-texture "Sprite/<tex>" HlmsUnlit datablock
		//! (unlit, alpha-blended, depth-checked/not-written, two-sided;
		//! idempotent - all sprites of one texture share it)
		static Ogre::HlmsDatablock* getOrCreateSpriteDatablock(
			String const & textureName, Ogre::TextureGpu* texture);
		//! an unlit datablock named datablockName that renders vertex
		//! colours (times the optional texture); idempotent per name -
		//! backs setVertexColourUnlit and the cube-mesh service
		static Ogre::HlmsDatablock* getOrCreateVertexColourUnlitDatablock(
			String const & datablockName, Ogre::TextureGpu* texture);
		//! the diffuse texture of a backend datablock or NULL (PBS
		//! diffuse slot / Unlit slot 0 - the subMeshHasTexture probe)
		static Ogre::TextureGpu* datablockDiffuseTexture(
			Ogre::HlmsDatablock* datablock);
		//! @brief every datablock the backend generates registers here
		//! @remarks backs the global wireframe toggle; datablocks live
		//! until teardown (they are tiny and shared by name)
		static void registerContentDatablock(Ogre::HlmsDatablock* datablock);
		//! @brief RenderCamera::setWireframe on this backend: flip the
		//! macroblock polygon mode of every backend-generated datablock
		//! @remarks recorded deviation (see RenderCamera.h): Next's v2
		//! camera lost the per-camera polygon-mode toggle, so wireframe
		//! is GLOBAL here - fine for the debug-toggle call sites
		static void setGlobalWireframe(bool enabled);
		//! zOrder -> render queue id (painter's sorting, queue 50+z like
		//! classic; v2 objects render from the FAST queues 0..99)
		static unsigned char renderQueueForZOrder(int zOrder);

		//--- guts accessors (NULL-safe) ------------------------------
		static Ogre::SceneNode* sceneNode(optr<RenderNode> const & node);
		static Ogre::Camera* ogreCamera(optr<RenderCamera> const & camera);
		//! the booted Ogre root / the one world's scene manager, or NULL
		//! (nested Impl structs cannot reach OTHER facade classes' mImpl -
		//! cross-class plumbing goes through these)
		static Ogre::Root* ogreRoot();
		static Ogre::SceneManager* worldSceneManager();

		//--- node registry (Ogre::SceneNode* -> facade handle) -------
		static void registerNode(Ogre::SceneNode* node,
			optr<RenderNode> const & handle);
		static void unregisterNode(Ogre::SceneNode* node);
		static optr<RenderNode> findNode(Ogre::SceneNode* node);
		static void* findUserPointerUpwards(Ogre::SceneNode* node);

		//--- shared services -----------------------------------------
		//! unique name for backend-created content ("prefix.<n>")
		static String generateName(String const & prefix);
		//! (re)build the window workspace from the stored camera +
		//! background colour (Next renders NOTHING without a workspace)
		static void recreateWindowWorkspace();
		//! honest-gap discipline: log the missing feature ONCE, stay
		//! silent afterwards - callers then return their safe default
		//! (B2 residual: LT_ZIP/LT_BIGZIP locations, skeletal import)
		static void notImplementedOnce(char const * feature);
	};
}

#endif //__NextBackend_h__8_7_2026__20_00_00__
