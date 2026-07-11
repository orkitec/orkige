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
//! STATE (Docs/render-abstraction.md): the
//! whole facade is implemented - boot (Root + Metal RS + SDL-hosted
//! window + CompositorManager2 workspace), nodes/cameras, mesh
//! instances (assimp import -> v1::ManualObject -> Mesh::importV1 ->
//! Item; .mesh via the v1 serializer + importV1), HLMS PBS/Unlit
//! datablock generation (the backend's whole material surface),
//! sprite quads (v2 ManualObject + shared per-texture HlmsUnlit
//! datablock), lights, RTT (TextureGpu + workspace-per-target), AABB
//! ray queries, frame stats (RenderingMetrics) and the cube-mesh
//! service. render_facade_selfcheck passes on this backend (enabled
//! in ctest; since the default flip this flavor's suite is the
//! default desktop preset). Remaining honest gaps, each logged
//! once via notImplementedOnce: LT_ZIP/LT_BIGZIP resource locations
//! (waits for real content work + the zziplib port feature) and
//! skeletal animation IMPORT through the assimp path (the animation
//! control surface is implemented over v2 SkeletonInstance, but the
//! importer bakes node transforms - static meshes only).
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
#include "engine_render/SpriteBatch.h"
#include "engine_render/VectorMesh.h"
#include "engine_render/RenderCamera.h"
#include "engine_render/RenderLight.h"
#include "engine_render/RenderTexture.h"
#include "engine_render/DrawLayer2D.h"

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
	class Image2;
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
		bool				uiOnlyWindow = false;	//!< showUIOnlyWindow mode: the window workspace clears + draws ONLY the 2D layer queue (getWindowCamera answers NULL)
		//--- frame timing (FrameStats on a backend without per-target stats)
		std::chrono::steady_clock::time_point	lastFrameTime;
		bool				haveLastFrameTime = false;
		float				lastFPS = 0.0f;
		float				avgFPS = 0.0f;
		float				bestFPS = 0.0f;		//!< max since resetFrameStats
		float				worstFPS = 999999.0f;	//!< min since resetFrameStats (classic's sentinel)
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
		Ogre::TextureGpu*	texture = NULL;			//!< the loaded texture (per-sampler datablock rebinds need it)
		String				datablockName;			//!< the per-(texture,sampler) "Sprite/<tex>#..." HlmsUnlit datablock
		float				texelWidth = 0.0f;		//!< texture size in texels (aspect derivation)
		float				texelHeight = 0.0f;
		float				width = 0.0f;			//!< configured size; <= 0 derives from the texture aspect
		float				height = 0.0f;
		float				u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
		Ogre::ColourValue	tint = Ogre::ColourValue::White;
		bool				flipX = false;
		bool				flipY = false;
		int					zOrder = 0;
		//! the v1 sprite sampler (bilinear+clamp = the historic look)
		SpriteQuad::FilterMode	filter = SpriteQuad::FILTER_BILINEAR;
		SpriteQuad::AddressMode	addressing = SpriteQuad::ADDRESS_CLAMP;
		optr<RenderNode>	attachedTo;

		//! rebuild the quad vertex data from the state above (same honest
		//! sprite rules as the classic backend: tint/flips in vertex data)
		void rebuild();
	};

	struct SpriteBatch::Impl
	{
		Ogre::ManualObject*	batch = NULL;			//!< v2 manual object (VaoManager-backed)
		Ogre::SceneManager*	creator = NULL;
		String				textureName;
		Ogre::TextureGpu*	texture = NULL;			//!< the loaded texture (texel-size queries)
		String				datablockName;			//!< the shared per-(texture,blend) HlmsUnlit datablock
		SpriteBatch::BlendMode	blendMode = SpriteBatch::BLEND_ALPHA;
		float				texelWidth = 0.0f;		//!< texture size in texels (atlas UV derivation)
		float				texelHeight = 0.0f;
		int					zOrder = 0;
		std::size_t			quadCount = 0;			//!< quads in the batch right now
		optr<RenderNode>	attachedTo;

		//! (re)build the v2 manual object from a CPU vertex array (4 verts/quad,
		//! TL/TR/BR/BL); an empty array leaves the object with no geometry
		void rebuild(SpriteBatch::Vertex const * vertices, std::size_t quadCount);
	};

	struct VectorMesh::Impl
	{
		Ogre::ManualObject*	mesh = NULL;			//!< v2 manual object (VaoManager-backed)
		Ogre::SceneManager*	creator = NULL;
		int					zOrder = 0;
		std::size_t			triangleCount = 0;		//!< triangles in the mesh right now
		std::size_t			vertexCount = 0;		//!< vertices in the built section (dynamic-update guard)
		std::vector<Ogre::uint32>	indices;		//!< cached topology, re-emitted by beginUpdate on a dynamic update
		optr<RenderNode>	attachedTo;

		//! (re)build the v2 manual object from an arbitrary CPU vertex + index
		//! array (untextured, vertex colour); empty arrays leave it geometry-free
		void rebuild(VectorMesh::Vertex const * vertices, std::size_t vertexCount,
			unsigned int const * indices, std::size_t indexCount);
		//! rewrite ONLY the vertex positions/colours of the built section via
		//! beginUpdate (topology reused from the cached indices); a count
		//! mismatch or an un-built mesh is ignored
		void updateVertices(VectorMesh::Vertex const * vertices,
			std::size_t vertexCount);
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

	struct DrawLayer2D::Impl
	{
		//! one submitted batch: texture + a flat, already scissor-clipped
		//! triangle list in pixel space (@see DrawLayer2DClip.h), rendered
		//! as one v2 ManualObject on its own node - the node's camera
		//! depth realizes the painter order (transparents sort back to
		//! front in the UI render queue)
		struct Batch
		{
			String								textureName;
			//! offscreen-target binding (the addTriangles RenderTexture
			//! overload); the batch keeps the target alive until clear()
			optr<RenderTexture>					renderTexture;
			std::vector<DrawLayer2D::Vertex2D>	triangles;
			Ogre::ManualObject*					object = NULL;
			Ogre::SceneNode*					node = NULL;
		};

		bool				visible = true;
		int					zOrder = 0;
		std::vector<Batch>	batches;
		//! the surface this layer composites onto: NULL = the main window;
		//! set = an offscreen RenderTexture (RenderTexture::createLayer). The
		//! target is kept alive by the layer while the layer references it.
		optr<RenderTexture>	target;
		//! the visibility flag every batch object of this layer carries, so
		//! only the matching UI pass (window or the target's) draws it
		//! (RenderBackend::UI_WINDOW_VISIBILITY == bit 0 for a window layer;
		//! RenderBackend is defined below, so the literal is spelled here)
		unsigned int		visibilityFlags = 0x00000001u;

		//! (re)build one batch's manual object from its triangle list
		void buildBatchObject(Batch & batch);
		//! destroy a batch's backend objects again
		void destroyBatchObject(Batch & batch);
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

		//--- offscreen 2D composition (RenderTexture::createLayer) ---
		//! this target owns 2D layers: its workspace grows a UI pass drawing
		//! the 2D queue through the per-target pixel-space UI camera, masked
		//! to uiVisibilityFlag (the GUI Preview surface)
		bool				hostsLayers = false;
		//! the per-target 2D visibility bit (0 until the first createLayer),
		//! shared by every layer this target owns and its UI pass mask
		unsigned int		uiVisibilityFlag = 0;
		//! per-target pixel-space ortho UI camera (owned by the scene manager;
		//! sized to width/height - the simulated device resolution)
		Ogre::Camera*		uiCamera = NULL;

		//! (re)create texture + workspace from the state above
		//! (resize-by-recreate, same contract as the classic backend)
		void recreate();
		//! drop workspace + texture (dtor and the recreate path)
		void destroyTarget();
		//! the per-target UI camera's backend name (unique per target)
		String uiCameraName() const;
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
		//! NULL + a log line when the texture cannot be loaded
		static optr<SpriteBatch> createSpriteBatch(
			Ogre::SceneManager* sceneManager, String const & textureName,
			SpriteBatch::BlendMode blendMode);
		//! create a world-space untextured vertex-coloured triangle mesh
		//! (@see VectorMesh; the shared "VectorFill" HlmsUnlit datablock)
		static optr<VectorMesh> createVectorMesh(Ogre::SceneManager* sceneManager);
		static optr<RenderLight> createLight(Ogre::SceneManager* sceneManager);
		static optr<RenderTexture> createRenderTexture(String const & name,
			unsigned int width, unsigned int height);
		//! create a 2D overlay layer (@see DrawLayer2DNext.cpp: v2 manual
		//! objects in the UI render queue, drawn by the window workspace's
		//! second scene pass through the pixel-space UI camera)
		static optr<DrawLayer2D> createDrawLayer2D(int zOrder);
		//! create a 2D layer that composites into an OFFSCREEN target instead
		//! of the window (RenderTexture::createLayer; the GUI Preview stage).
		//! Its batches carry the target's visibility flag so only the target's
		//! own UI pass draws them (@see RenderTextureNext.cpp)
		static optr<DrawLayer2D> createTargetDrawLayer2D(
			optr<RenderTexture> const & target, unsigned int visibilityFlag,
			int zOrder);
		//! drop a dying layer from the registry again (facade dtor)
		static void unregisterDrawLayer2D(DrawLayer2D* layer);
		//! layer content/order changed - painter depths get reassigned
		//! before the next frame
		static void markDrawLayer2DOrderDirty();
		//! per-frame 2D upkeep (called from renderOneFrame): follow window
		//! resizes with the UI camera and reassign batch depths when dirty
		static void updateDrawLayer2DFrame();
		//! teardown hook (destroyRenderSystem): drop the UI-camera/registry
		//! statics - the backend objects die with the root
		static void resetDrawLayer2DState();
		//! the UI render queue id (v2 FAST by default; the window
		//! workspace's scene pass ends BELOW it, the UI pass draws only it).
		//! ALL 2D batches (window and offscreen targets) live in this one
		//! queue; per-surface separation is by visibility flag, not by queue.
		static unsigned char const DRAWLAYER2D_RENDER_QUEUE = 200;
		//! visibility flag of the WINDOW's 2D batches: the window UI pass
		//! masks to exactly this bit, so an offscreen target's UI pass (a
		//! different bit) never draws window batches and vice versa. Bit 0 of
		//! the user-usable RESERVED_VISIBILITY_FLAGS range; offscreen targets
		//! draw their bits from allocateUiVisibilityFlag (bits 1..N)
		static unsigned int const UI_WINDOW_VISIBILITY = 0x00000001u;
		//! @brief hand out a fresh per-target 2D visibility bit (bits 1..N of
		//! the user range; bit 0 is the window). Returns 0 when the pool is
		//! exhausted (logged once) - the caller falls back to the window bit.
		static unsigned int allocateUiVisibilityFlag();
		//! @brief return a target bit to the pool (RenderTexture teardown)
		static void freeUiVisibilityFlag(unsigned int flag);
		//! name of the pixel-space ortho camera the (window) UI pass renders with
		static char const * drawLayer2DCameraName();
		//! lazily create the UI camera (recreateWindowWorkspace needs it
		//! before it can reference it by name in the pass definition)
		static Ogre::Camera* ensureDrawLayer2DCamera();
		//! @brief shape a pixel-space ortho UI camera to a surface size
		//! (top-left origin, +y down via the negated-rect convention). Shared
		//! by the window UI camera and every per-target preview UI camera.
		static void shapeUICamera(Ogre::Camera* camera,
			unsigned int width, unsigned int height);

		//--- mesh import (MeshLoaderNext.cpp) ------------------------
		//! @brief make sure a v2 mesh named meshName exists (idempotent)
		//! @remarks THE Next mesh path: an existing v2 mesh
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
		//! the backend line-list mesh service (RenderWorld::createLineListMesh:
		//! v1 ManualObject OT_LINE_LIST -> importV1, shared "VertexColour"
		//! unlit datablock - the cube-service recipe on line primitives)
		static void createVertexColourLineListMesh(
			Ogre::SceneManager* sceneManager, String const & meshName,
			Vec3 const * points, Color const * colours, size_t pointCount);

		//--- texture / datablock services (the material surface) -----
		//! load a 2D texture through the resource system (any group);
		//! metadata (texel size) is ready on return; NULL when missing
		static Ogre::TextureGpu* loadTexture2D(String const & textureName);
		//! create a 2D texture from an in-memory encoded image (png/jpg
		//! bytes - glb-embedded textures); idempotent per name
		static Ogre::TextureGpu* createTexture2DFromMemory(String const & name,
			void const * bytes, size_t sizeBytes, String const & formatHint);
		//! create/replace a 2D texture from RAW RGBA8 pixels (the facade
		//! RenderSystem::createTexture2D - ImGui font atlas service);
		//! replacing under an existing name re-uploads (2D-layer datablocks
		//! bound to the name are re-pointed at the new incarnation)
		static Ogre::TextureGpu* createTexture2DFromPixels(String const & name,
			unsigned char const * rgbaPixels,
			unsigned int width, unsigned int height);
		//! destroy a created 2D texture again (idempotent; detaches it from
		//! the generated 2D-layer datablock first - the replace path and
		//! RenderSystem::destroyTexture2D share this)
		static void destroyTexture2DByName(String const & name);
		//! @brief the per-(texture,sampler) "Sprite/<tex>#..." HlmsUnlit
		//! datablock (unlit, alpha-blended, depth-checked/not-written,
		//! two-sided; an HlmsSamplerblock on slot 0 carries the requested
		//! filter/addressing). Idempotent per SpriteQuad::samplerName key -
		//! the sampler is baked in so distinct sampling of one texture never
		//! shares (stomps) a datablock.
		static Ogre::HlmsDatablock* getOrCreateSpriteDatablock(
			String const & textureName, Ogre::TextureGpu* texture,
			SpriteQuad::FilterMode filter, SpriteQuad::AddressMode addressing);
		//! @brief the shared per-(texture,blend) sprite-batch datablock: the
		//! alpha variant IS the SpriteQuad "Sprite/<tex>#bilinear-clamp"
		//! datablock (reused wholesale); the additive variant a distinct
		//! "SpriteAdd/<tex>#bilinear-clamp" HlmsUnlit datablock (source alpha /
		//! one - order-independent glow). Idempotent per name.
		static Ogre::HlmsDatablock* getOrCreateSpriteBatchDatablock(
			String const & textureName, Ogre::TextureGpu* texture,
			SpriteBatch::BlendMode blendMode);
		//! @brief the ONE shared untextured vertex-colour "VectorFill" HlmsUnlit
		//! datablock: unlit, alpha-blended, depth-checked/not-written, two-sided;
		//! colour flows from VES_DIFFUSE (the DrawLayer2D empty-texture recipe).
		//! Idempotent - every vector shape renders through this one datablock.
		static Ogre::HlmsDatablock* getOrCreateVectorFillDatablock();
		//! an unlit datablock named datablockName that renders vertex
		//! colours (times the optional texture); idempotent per name -
		//! backs setVertexColourUnlit and the cube-mesh service
		static Ogre::HlmsDatablock* getOrCreateVertexColourUnlitDatablock(
			String const & datablockName, Ogre::TextureGpu* texture);
		//! the "DrawLayer2D/<tex>" HlmsUnlit datablock of a 2D batch
		//! texture (unlit, alpha-blended, depth-IGNORED, two-sided,
		//! clamped point sampling - the facade's 2D render contract);
		//! empty textureName = the shared untextured vertex-colour block
		//! @return NULL + one log line when the texture cannot be loaded
		static Ogre::HlmsDatablock* getOrCreateDrawLayer2DDatablock(
			String const & textureName);
		//! the per-target "DrawLayer2D/RTT/<name>" datablock of an
		//! offscreen-target 2D batch (same render contract), re-pointed at
		//! the target's CURRENT texture on every call (resize safe)
		static Ogre::HlmsDatablock* getOrCreateDrawLayer2DRTTDatablock(
			optr<RenderTexture> const & renderTexture);
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
		//! the target's CURRENT backend texture (changes across resizes) -
		//! the 2D layer binds render-texture batches through this
		static Ogre::TextureGpu* renderTextureGpu(
			optr<RenderTexture> const & texture);
		//! the target's stable facade name (per-target datablock identity)
		static String renderTextureName(optr<RenderTexture> const & texture);
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
		//! force a readback image's alpha channel opaque before saving:
		//! render targets carry alpha as a rendering byproduct (content
		//! legally writes 0), classic screenshots are opaque - parity
		static void makeImageAlphaOpaque(Ogre::Image2 & image);
		//! honest-gap discipline: log the missing feature ONCE, stay
		//! silent afterwards - callers then return their safe default
		//! (residual: LT_ZIP/LT_BIGZIP locations, skeletal import)
		static void notImplementedOnce(char const * feature);
	};
}

#endif //__NextBackend_h__8_7_2026__20_00_00__
