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
//! default desktop preset). Skinned glTF imports whole (skeleton +
//! clips + weights via the v1 skeleton -> importV1 route, see
//! MeshLoaderNext.cpp) and plays over v2 SkeletonInstance
//! (MeshInstanceNext.cpp). Pak mounting (LT_ZIP / mountPak) now works
//! here too, backed by the shared MiniZip reader since Ogre-Next ships
//! no stock zip archive (engine_filesystem, Docs/filesystem.md).
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
#include "engine_render/RenderDecal.h"
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
	class Decal;
	class TextureGpu;
	class HlmsDatablock;
	class Image2;
	class AtmosphereNpr;
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
		unsigned int		caps = 0;			//!< RenderCaps bitset (bit i = supports RenderCaps(i)), filled at boot
		unsigned int		lightBudget = 0;	//!< sane concurrent dynamic-light ceiling (@see RenderSystem::lightBudget), filled at boot
	};

	struct RenderWorld::Impl
	{
		Ogre::SceneManager*	sceneManager = NULL;	//!< owned by the backend (root teardown)
		optr<RenderNode>	rootNode;				//!< stable facade handle of the root node (owned=false)
		Ogre::ColourValue	ambient = Ogre::ColourValue(0.2f, 0.2f, 0.2f, 1.0f);	//!< upper-hemisphere/flat ambient facade cache
		Ogre::ColourValue	ambientLower = Ogre::ColourValue(0.2f, 0.2f, 0.2f, 1.0f);	//!< lower-hemisphere ambient facade cache
		//! the shadow quality knob; maps render only while != SQ_OFF AND a
		//! light casts (@see RenderBackend::activeShadowNodeName)
		ShadowPreset::Quality	shadowQuality = ShadowPreset::SQ_MEDIUM;
		//! the sky/fog atmosphere last set (@see RenderWorld::setAtmosphere); the
		//! live Ogre::AtmosphereNpr is a backend static (NextBackend.cpp)
		AtmosphereDesc			atmosphere;
		//! the image-lighting opt-in (@see RenderWorld::setImageLighting);
		//! renders only with the quality knob on AND a loaded skybox cubemap
		bool					iblEnabled = false;
		//! scales the added cubemap contribution (the scene envmapScale here)
		float					iblIntensity = 1.0f;
		//! the IBL quality knob (chain budgets in core_util/IblPreset.h)
		IblPreset::Quality		iblQuality = IblPreset::IQ_MEDIUM;
		//! the LDR bloom knob + tunables (@see RenderWorld::setBloom); the pass
		//! renders only while quality != BQ_OFF AND bloom.enabled (@see
		//! RenderBackend::bloomActive). Default MEDIUM tier, bloom OFF.
		BloomPreset::Quality	bloomQuality = BloomPreset::BQ_MEDIUM;
		BloomDesc				bloom;
		//! the output grade (@see RenderWorld::setOutputGrade); the grade quad is
		//! inserted after the scene pass only while grade.enabled (@see
		//! RenderBackend::gradeActive). Default OFF (identity).
		GradeDesc				grade;
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
		//! the mobility flag (@see RenderNode::setStatic): the node lives in
		//! the SCENE_STATIC memory managers while set
		bool				isStatic = false;
		//! one mobility-contract warning per node (@see RenderNode::setStatic)
		bool				staticMoveWarned = false;

		//! the mobility-contract repair path: a transform mutation reached a
		//! static node - warn once, then notifyStaticDirty so the frozen
		//! transforms re-derive on the next frame (correct but costly)
		void noteStaticMutation(char const * operation);
	};

	struct MeshInstance::Impl
	{
		Ogre::Item*			item = NULL;			//!< the v2 entity equivalent
		Ogre::SceneManager*	creator = NULL;
		String				meshName;
		optr<RenderNode>	attachedTo;				//!< keeps the node alive while content hangs off it
		//! per-sub-item ORIGINAL datablocks, held while the instance is
		//! switched to no-receive variants (@see MeshInstance::setReceiveShadows);
		//! empty while the instance receives shadows normally
		std::vector<Ogre::HlmsDatablock*>	receiveRestore;
		//! runtime accents (@see MeshInstance::setTint/setEmissiveBoost):
		//! the current tint (white = none) and additive emissive boost
		//! (black = none), plus - while accented - the per-sub-item ORIGINAL
		//! datablocks and the per-instance accent VARIANT clones rendering in
		//! their place (destroyed on restore/teardown; NULL where a sub-item
		//! carried a non-PBS datablock the accent skips)
		Color	accentTint = Color(1.0f, 1.0f, 1.0f, 1.0f);
		Color	accentBoost = Color(0.0f, 0.0f, 0.0f, 1.0f);
		std::vector<Ogre::HlmsDatablock*>	accentRestore;
		std::vector<Ogre::HlmsDatablock*>	accentVariants;
		//! @see MeshInstance::setAnimatedBounds: while armed (and the item
		//! carries a skeleton), getLocalBounds derives the box from the LIVE
		//! bone poses instead of the fixed bind-pose Aabb
		bool				animatedBounds = false;
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
		std::size_t			allocatedQuads = 0;		//!< quads the live buffers were built for (in-place refresh guard)
		optr<RenderNode>	attachedTo;

		//! (re)build the v2 manual object from a CPU vertex array (4 verts/quad,
		//! TL/TR/BR/BL); an empty array leaves the object with no geometry.
		//! A refresh at the SAME quad count updates the live buffers in place
		//! (beginUpdate - stable VAO identity, no buffer churn); a count change
		//! reallocates. Owners upload at most once per frame (@see SpriteBatch)
		void rebuild(SpriteBatch::Vertex const * vertices, std::size_t quadCount);
	};

	struct VectorMesh::Impl
	{
		//! one built v2-ManualObject section: its resolved datablock, cached
		//! topology (re-emitted by beginUpdate on a dynamic update) and
		//! whether the vertex format carries a UV stream
		struct BuiltSection
		{
			String						datablock;	//!< "VectorFill" or the per-texture sprite datablock
			bool						textured = false;	//!< emit UVs (a texture is bound)
			std::size_t					vertexCount = 0;	//!< dynamic-update guard (0 = section built nothing)
			std::size_t					ogreSection = 0;	//!< ManualObject section ordinal (degenerate entries create none)
			std::vector<Ogre::uint32>	indices;	//!< cached section topology
		};
		Ogre::ManualObject*	mesh = NULL;			//!< v2 manual object (VaoManager-backed)
		Ogre::SceneManager*	creator = NULL;
		int					zOrder = 0;
		std::size_t			triangleCount = 0;		//!< triangles in the mesh right now
		std::vector<BuiltSection>	sections;		//!< built sections, paint order
		optr<RenderNode>	attachedTo;

		//! (re)build the v2 manual object from a section list (one section
		//! per entry: flat = "VectorFill", textured = the sprite datablock +
		//! a UV stream); an empty list leaves it geometry-free
		void rebuild(VectorMesh::Section const * list, std::size_t count);
		//! rewrite ONLY one section's vertex data via beginUpdate (topology
		//! reused from the cached indices); a count mismatch or an un-built
		//! section is ignored
		void updateSection(std::size_t index,
			VectorMesh::Vertex const * vertices, std::size_t vertexCount);
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
		//! is THIS light counted in the backend's shadow-caster tally
		//! (@see RenderBackend::shadowCasterCountChanged)
		bool				castingShadows = false;
	};

	struct RenderDecal::Impl
	{
		Ogre::Decal*		decal = NULL;			//!< the v2 Decal movable
		Ogre::SceneManager*	creator = NULL;
		optr<RenderNode>	attachedTo;
		String				textureName;			//!< the diffuse (mark) texture, "" until set
		Ogre::TextureGpu*	texture = NULL;			//!< the pooled decal-diffuse texture (auto slice), or NULL
		//! the two visibility gates ANDed onto the movable: mUserVisible = the
		//! owner/component (setVisible), budgetVisible = the world decal budget
		//! (RenderBackend enforces it). The Decal renders only when both hold.
		bool				userVisible = true;
		bool				budgetVisible = true;
		//! next has no per-decal alpha uniform, so opacity is a threshold: <= 0
		//! hides the mark (@see RenderDecal::setOpacity remarks). Kept so the
		//! effective visibility recomputes from all three flags.
		bool				opacityVisible = true;

		//! push (userVisible && budgetVisible && opacityVisible) onto the movable
		void applyVisibility();
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
		//! facade cache: the Next flavor compiles no overlay component, so
		//! "off" already holds structurally - kept for the facade contract
		bool				overlaysEnabled = true;
		//! per-target shadow opt-out: while true (the default) this target's
		//! scene pass carries the world's shadow node whenever shadows are
		//! active (@see RenderBackend::activeShadowNodeName); false keeps the
		//! target shadow-free regardless of the world knob (the parity RTT)
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
		//! the UI camera's backend name, made UNIQUE PER INCARNATION via
		//! generateName (set once in createLayer). The facade name is stable
		//! across incarnations, but a preview device switch builds a fresh
		//! surface while the dying incarnation's camera still lives (2D batches
		//! hold the old target an extra frame) - a stable camera name would
		//! then collide on createCamera and abort. Same identity-per-incarnation
		//! rule as the RTT datablock.
		String				uiCamName;

		//! (re)create texture + workspace from the state above
		//! (resize-by-recreate, same contract as the classic backend)
		void recreate();
		//! drop workspace + texture (dtor and the recreate path)
		void destroyTarget();
		//! the per-incarnation UI camera backend name (@see uiCamName)
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

		//--- clustered forward light grid (boot config) ------------
		//! @brief the clustered-forward light grid this backend configures at
		//! boot (setForwardClustered). lightsPerCell is the per-cluster
		//! light-list bound - the worst-case (all lights overlapping one cell)
		//! concurrent dynamic-light count the shader evaluates, which
		//! RenderSystem::defaultLightBudget reports as this flavor's sane
		//! many-lights ceiling. Named here so the boot call and the budget
		//! derivation share ONE source and cannot drift.
		static unsigned int const FORWARD_CLUSTERED_WIDTH = 16u;
		static unsigned int const FORWARD_CLUSTERED_HEIGHT = 8u;
		static unsigned int const FORWARD_CLUSTERED_SLICES = 24u;
		static unsigned int const FORWARD_CLUSTERED_LIGHTS_PER_CELL = 96u;
		//! @brief the per-cluster decal-list bound the boot passes to
		//! setForwardClustered (its decalsPerCell arg) - a 0 there disables
		//! decals entirely, so it MUST be > 0 for RenderDecal to render. The
		//! worst-case overlapping decals a cell shades; 8 is generous for the
		//! surface-mark budget (@see RenderWorld::setMaxDecals, the r.maxDecals
		//! ceiling).
		static unsigned int const FORWARD_CLUSTERED_DECALS_PER_CELL = 8u;

		//--- decal diffuse texture pool (RenderDecal) --------------
		//! @brief the fixed decal texture resolution. Ogre-Next decals sample ONE
		//! diffuse ARRAY, and AutomaticBatching auto-pools textures by resolution,
		//! so every decal texture must be this size to share the one pool whose
		//! master feeds SceneManager::setDecalsDiffuse; a different-sized texture
		//! would land in another pool and never render (rejected + logged). The
		//! engine decal textures and project decal textures match it
		//! (Docs/materials.md).
		static unsigned int const DECAL_TEXTURE_SIZE = 256u;

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
			SpriteBatch::BlendMode blendMode, SpriteQuad::FilterMode filter,
			SpriteQuad::AddressMode addressing);
		//! create a world-space untextured vertex-coloured triangle mesh
		//! (@see VectorMesh; the shared "VectorFill" HlmsUnlit datablock)
		static optr<VectorMesh> createVectorMesh(Ogre::SceneManager* sceneManager);
		static optr<RenderLight> createLight(Ogre::SceneManager* sceneManager);
		//! create a projected decal (@see RenderDecalNext.cpp); joins the world
		//! visible-decal budget on creation
		static optr<RenderDecal> createDecal(Ogre::SceneManager* sceneManager);
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
		//! reassign every batch node's painter depth NOW (global zOrder/
		//! creation/submission walk); batch (re)builds and zOrder changes
		//! call it directly so a mid-frame rebuild draws the same frame
		static void assignDrawLayer2DBatchDepths();
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
		//! colour/texture, incl. glb-embedded textures). A static source
		//! gets its node transforms baked (aiProcess_PreTransformVertices);
		//! a skinned/animated one imports skeleton + clips + weights whole
		//! (the v1 skeleton route, see MeshLoaderNext.cpp remarks).
		//! @return false + a log line when the resource is missing/broken
		static bool ensureV2Mesh(Ogre::SceneManager* sceneManager,
			String const & meshName);
		//! @brief record a skinned import's facts for the instance surface
		//! (@see MeshInstance::setAnimatedBounds / getAnimationNames):
		//! boneRadius = max skinned-vertex distance from its bone at bind
		//! (the animated-bounds padding), clipNames = the clip names in
		//! import order (exact strings in every build config - the v2
		//! IdString only keeps readable names in debug builds)
		static void registerSkinnedMesh(String const & meshName,
			Real boneRadius, StringVector const & clipNames);
		//! the registered bone radius, 0 when meshName never imported skinned
		static Real skinnedMeshBoneRadius(String const & meshName);
		//! the registered clip names, NULL when meshName never imported skinned
		static StringVector const * skinnedMeshClipNames(String const & meshName);
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
		//! @brief resolve a texture resource name against the cooked-payload
		//! rename: an exported payload block-compresses textures, replacing
		//! foo.png with foo.dds/foo.oitd - when the plain name is missing
		//! from every resource group, the cooked siblings are tried (id-less
		//! bare-name references: .omat maps, gui atlases, script-set sprite
		//! names; id-carrying references resolve through the renamed sidecar
		//! before they reach the backend). Dev trees always hit the raw name.
		static String resolveTextureResourceName(String const & textureName);
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
		//! alpha variant IS the SpriteQuad "Sprite/<tex>#<sampler>" datablock
		//! (reused wholesale, so a batched sprite run renders through the
		//! same material its individual quads use); the additive variant a
		//! distinct "SpriteAdd/<tex>#bilinear-clamp" HlmsUnlit datablock
		//! (source alpha / one - order-independent glow, always
		//! bilinear+clamp). Idempotent per name.
		static Ogre::HlmsDatablock* getOrCreateSpriteBatchDatablock(
			String const & textureName, Ogre::TextureGpu* texture,
			SpriteBatch::BlendMode blendMode, SpriteQuad::FilterMode filter,
			SpriteQuad::AddressMode addressing);
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
		//! @brief create OR UPDATE the named HLMS PBS datablock from a facade
		//! surface description (RenderSystem::createMaterial). Metallic
		//! workflow - every RenderMaterialDesc field is native on this
		//! backend. Albedo/emissive maps load through loadTexture2D (raw
		//! texels - the gamma-space colour-parity rule), the normal map
		//! through the PBS slot's own suggested filters (normal-map
		//! preparation). A missing texture is skipped + logged and clears
		//! outComplete; NULL when the name belongs to a datablock of a
		//! different family (a generated sprite/unlit block).
		static Ogre::HlmsDatablock* createOrUpdatePbsDatablock(
			String const & name, RenderMaterialDesc const & desc,
			bool & outComplete);
		//! @brief create OR UPDATE the named HLMS PBS WATER datablock from a
		//! water surface description (RenderSystem::createWaterMaterial):
		//! realistic transparency preserving the fresnel edge, the deep colour
		//! as the water-body albedo, a subtle shallow-colour scatter, and TWO
		//! detail normal maps (the ripple animation, scrolled by
		//! setWaterDatablockTime). Registers the wave tunables so the per-frame
		//! scroll can recompute the detail offsets. A missing normal map is
		//! skipped + logged and clears outComplete; NULL when the name belongs
		//! to a datablock of a different family.
		static Ogre::HlmsDatablock* createOrUpdateWaterDatablock(
			String const & name, RenderWaterDesc const & desc,
			bool & outComplete);
		//! @brief scroll a water datablock's two detail normal maps to @p
		//! seconds (RenderSystem::setWaterTime) - a name with no registered
		//! water datablock is a silent no-op (the dormancy rule)
		static void setWaterDatablockTime(String const & name, float seconds);
		//! the "DrawLayer2D/<tex>" HlmsUnlit datablock of a 2D batch
		//! texture (unlit, alpha-blended, depth-IGNORED, two-sided,
		//! clamped point sampling - the facade's 2D render contract);
		//! empty textureName = the shared untextured vertex-colour block
		//! @return NULL + one log line when the texture cannot be loaded
		static Ogre::HlmsDatablock* getOrCreateDrawLayer2DDatablock(
			String const & textureName);
		//! the per-INCARNATION "DrawLayer2D/RTT/<backendTexName>" datablock of
		//! an offscreen-target 2D batch (same render contract). Keyed by the
		//! target's current backend texture name (unique per resize-by-
		//! recreate), NOT the stable facade name: the texture-gpu manager
		//! reuses the TextureGpu POINTER across a recreate, so a datablock kept
		//! across incarnations would re-bind a descriptor set the driver still
		//! caches against that pointer - the destroyed incarnation's freed
		//! image view. A fresh incarnation gets a fresh datablock; the retired
		//! one is dropped via retireRTTDatablock once its batch is gone.
		static Ogre::HlmsDatablock* getOrCreateDrawLayer2DRTTDatablock(
			optr<RenderTexture> const & renderTexture);
		//! the per-incarnation RTT datablock name for a backend texture, or ""
		//! when it is NULL ("DrawLayer2D/RTT/" + the texture's backend name)
		static String rttDatablockName(Ogre::TextureGpu* texture);
		//! mark a dying incarnation's RTT datablock for destruction. It cannot
		//! be destroyed immediately: a still-linked datablock trips the
		//! ~HlmsDatablock renderable assert, and the batch that draws it is not
		//! cleared until the layer next rebuilds. flushRetiredRTTDatablocks
		//! destroys it once no renderable links it.
		static void retireRTTDatablock(String const & name);
		//! destroy every retired RTT datablock that no renderable links any
		//! more (freeing its pooled descriptor set, so a later incarnation on
		//! the reused texture pointer never binds the dead image view); called
		//! once per frame from updateDrawLayer2DFrame, before the render
		static void flushRetiredRTTDatablocks();
		//! the diffuse texture of a backend datablock or NULL (PBS
		//! diffuse slot / Unlit slot 0 - the subMeshHasTexture probe)
		static Ogre::TextureGpu* datablockDiffuseTexture(
			Ogre::HlmsDatablock* datablock);
		//! @brief every datablock the backend generates registers here
		//! @remarks backs the global wireframe toggle; datablocks live
		//! until teardown (they are tiny and shared by name)
		static void registerContentDatablock(Ogre::HlmsDatablock* datablock);
		//! restore a mesh instance's pre-accent datablocks and retire its
		//! accent variant clones (@see MeshInstance::setTint; no-op unaccented)
		static void resetMeshAccents(MeshInstance::Impl* impl);
		//! realize a mesh instance's accent state (@see MeshInstance::setTint)
		static void applyMeshAccents(MeshInstance::Impl* impl);
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
		//! the node's mobility flag (@see RenderNode::setStatic) - content
		//! classes align their movable's static mode with it on attach
		static bool nodeIsStatic(optr<RenderNode> const & node);
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

		//--- dynamic shadows (PSSM shadow node) -----------------------
		//! @brief the shadow node definition the scene passes reference, or
		//! "" while shadows are inactive. Shadows are ACTIVE while the world
		//! knob is not SQ_OFF AND at least one light asked to cast - until
		//! then no shadow node exists in any workspace and no atlas memory is
		//! allocated (2D-only games pay nothing). The definition is built on
		//! first use per quality step (ShadowNodeHelper, PSSM technique,
		//! DIRECTIONAL casters only in v1 - budgets from
		//! core_util/ShadowPreset.h) and reused; each workspace referencing it
		//! instantiates its own node (own atlas texture).
		static String activeShadowNodeName();
		//! @brief a RenderLight turned its cast flag on (+1) or off (-1); a
		//! 0 <-> >0 transition of the tally (while the knob is on) attaches/
		//! detaches the shadow node by rebuilding every workspace
		static void shadowCasterCountChanged(int delta);
		//! @brief re-apply the shadow configuration: HlmsPbs PCF filter +
		//! scene shadow distances from the current preset, then rebuild the
		//! window workspace and every live render-target workspace so their
		//! scene passes pick the shadow node up / drop it
		static void applyShadowConfig();
		//! @brief one-line description of the live shadow infrastructure
		//! (the active shadow node + the caster tally) - the render_facade
		//! selfcheck asserts arm/disarm restores it EXACTLY
		static String shadowStateDescription();
		//! render-target registry (applyShadowConfig rebuilds them all)
		static void registerRenderTarget(RenderTexture* target);
		static void unregisterRenderTarget(RenderTexture* target);
		//! @brief put every live offscreen target into the SAMPLEABLE resource
		//! layout (the barrier the compositor cannot derive, because the batch
		//! that samples a RenderTexture lives in another workspace); called
		//! from the window workspace's per-pass listener, before the pass opens
		//! its render pass. A target already sampleable costs nothing.
		static void transitionRenderTargetsForSampling();

		//--- sky / fog atmosphere (AtmosphereNpr) --------------------
		//! @brief create/update/tear down the one AtmosphereNpr from a facade
		//! desc (RenderWorld::setAtmosphere). Links the first directional light
		//! as the sun, reads its direction, drives its colour/power; a boot
		//! without the atmosphere sky media degrades to an honest no-op (logged
		//! once) - the flat window clear colour still follows the sky tint.
		static void applyAtmosphere(AtmosphereDesc const & desc);

		//--- image-based lighting (skybox-sourced) -------------------
		//! @brief realize the facade image-lighting state (RenderWorld::
		//! setImageLighting/setIblQuality): while active - enabled, knob on,
		//! a loaded skybox cubemap showing - every generated PBS datablock
		//! binds the tier-capped environment chain as its reflection map and
		//! the scene envmapScale carries the intensity; inactive unbinds them
		//! all and drops the derived chain texture. Also re-run at the end of
		//! every applyAtmosphere so the chain follows a skybox change; cheap
		//! no-op while the state is off. Enabling without a skybox source
		//! logs one honest line and renders unchanged.
		static void applyImageLighting();
		//! bind the active environment chain to one freshly generated PBS
		//! datablock (no-op while image lighting is inactive or for unlit
		//! datablocks) - keeps late-created materials in the lit set
		static void applyImageLightingToDatablock(Ogre::HlmsDatablock* datablock);
		//! the scene envmapScale image lighting wants right now (1 while
		//! inactive) - every sceneManager->setAmbientLight caller passes it
		//! so an ambient write never resets the intensity
		static float imageLightingEnvmapScale();

		//! @brief a RenderLight became (isDirectional=true) or stopped being
		//! (false) directional / is being destroyed. Maintains the directional
		//! registry firstDirectionalLight reads and, while the atmosphere is
		//! live, re-links it to the new first sun (never a dangling pointer).
		static void noteDirectionalLight(Ogre::Light* light, bool isDirectional);
		//! @brief a RenderLight setter authored a colour. While the atmosphere
		//! DRIVES @p light (it is the linked sun), the authored value lands in
		//! the restore snapshot instead of the live light - the atmosphere owns
		//! the visible colour, and disabling it restores the LATEST authored
		//! one. Returns true when consumed that way (the caller then skips the
		//! live write).
		static bool noteAuthoredSunColour(Ogre::Light* light,
			Ogre::ColourValue const & colour, bool specular);
		//! the first (creation order) directional light, or NULL - the sun the
		//! atmosphere links to (@see RenderWorld::setAtmosphere)
		static Ogre::Light* firstDirectionalLight();

		//--- LDR bloom (CompositorManager2 quad passes) -------------
		//! visibility bit carried by the 2D tier (sprite quads/batches + vector
		//! meshes) so the bloom-on scene split can render the 3D scene into the
		//! bright-pass source WITHOUT them and composite them un-bloomed on top.
		//! A bare user bit (setVisibilityFlags preserves the reserved layer bits,
		//! @see DrawLayer2DNext); disjoint from UI_WINDOW_VISIBILITY (bit 0). The
		//! scene default clears it (setSceneDefaultVisibility), so 3D content
		//! never carries it and the two bloom-on passes are mutually exclusive.
		static unsigned int const SCENE_2D_VISIBILITY = 0x00000002u;
		//! @brief tag a 2D-tier movable (sprite/batch/vector mesh) with
		//! SCENE_2D_VISIBILITY so bloom can split it out of the 3D scene. Byte-
		//! stable while bloom is off (the single scene pass masks nothing).
		static void tagScene2D(Ogre::MovableObject* movable);
		//! @brief clear SCENE_2D_VISIBILITY from the process-wide default movable
		//! visibility flags at boot, so all 3D content is created without the 2D
		//! bit (only tagScene2D sets it). Reserved layer bits are preserved.
		static void setSceneDefaultVisibility();
		//! @brief whether the bloom pass is currently ACTIVE: the world quality
		//! knob is not BQ_OFF, a scene enabled bloom (BloomDesc::enabled), and the
		//! backend supports it. While inactive the window/RTT workspaces build
		//! their unchanged (byte-identical) pass structure.
		static bool bloomActive();
		//! whether this backend can render the bloom post-process at all (the
		//! RenderCaps::Bloom fill; always true on this desktop-capable flavor)
		static bool bloomSupported();
		//! @brief re-apply the bloom configuration (RenderWorld::setBloom /
		//! setBloomQuality): push the threshold/intensity onto the bloom
		//! materials, then rebuild the window workspace so its passes pick the
		//! bloom chain up / drop it. Idempotent; a no-op change still rebuilds
		//! cheaply (the shadow-config precedent).
		static void applyBloomConfig();
		//! @brief register the bloom shader media location + build the four bloom
		//! materials on first use (bright-pass/blur-h/blur-v/combine). Idempotent;
		//! the media dir is the ORKIGE_*_BLOOM_DIR the host passes at boot.
		static void ensureBloomMaterials();

		//--- output grade (the shared authored look; CompositorManager2 quad) ---
		//! whether this backend can render the grade quad at all (the
		//! RenderCaps::OutputGrade fill; always true on this desktop/mobile-capable
		//! flavor - the render targets carry the off-screen scene texture the grade
		//! quad samples)
		static bool gradeSupported();
		//! @brief whether the grade quad is currently ACTIVE: a scene enabled the
		//! grade (GradeDesc::enabled) AND the grade material resolved. While
		//! inactive the window workspace builds its unchanged (byte-identical) pass
		//! structure - grade is OFF by default.
		static bool gradeActive();
		//! @brief re-apply the grade configuration (RenderWorld::setOutputGrade):
		//! push the contrast/saturation onto the grade material, then rebuild the
		//! window workspace so its passes insert/drop the grade quad. Idempotent;
		//! a no-op change still rebuilds cheaply (the bloom-config precedent).
		static void applyGradeConfig();
		//! @brief make sure the grade material resolved (Orkige/Grade/Apply,
		//! shipped in the media/grade dir the host registers). Idempotent; a
		//! media-less/headless boot has none and grade degrades to no pass
		//! (byte-identical), logged once.
		static void ensureGradeMaterials();

		//--- screen-space water refraction (HlmsPbs Refractive) ------------
		//! whether this backend can render screen-space water refraction at all
		//! (the RenderCaps::ScreenSpaceRefraction fill; always true on this
		//! desktop-capable flavor - the Metal/Vulkan render targets carry the
		//! scene-colour+depth split the HlmsPbs Refractive mode reads)
		static bool screenSpaceRefractionSupported();
		//! @brief whether the refraction workspace is currently ACTIVE: at least
		//! one LIVE refractive water material exists AND the copy media resolved.
		//! While inactive the window workspace builds its unchanged (byte-identical)
		//! pass structure - default water is Transparent, so this is off by default.
		static bool refractionActive();
		//! @brief make sure the refraction copy material resolved (Orkige/Refraction/
		//! Copy, shipped in the same media dir as the bloom chain). Idempotent; a
		//! media-less/headless boot has none and refraction degrades to the
		//! byte-stable Transparent look (createOrUpdateWaterDatablock keeps
		//! Transparent), logged once.
		static void ensureRefractionMaterials();
		//! @brief record whether the named water material renders REFRACTIVE
		//! (createOrUpdateWaterDatablock): tracks the live refractive-water set and,
		//! on an empty<->non-empty transition, rebuilds the window workspace so the
		//! refraction split appears / disappears. @see recreateWindowWorkspace
		static void noteWaterMaterialRefractive(String const & name,
			bool refractive);
		//! @brief whether the named material is a live refractive water material -
		//! MeshInstance::setMaterial reads this to put the surface in the dedicated
		//! refraction render queue (WATER_REFRACTION_RENDER_QUEUE) the second scene
		//! pass renders, or restore it to the default Item queue when not
		static bool isRefractiveWaterMaterial(String const & name);
		//! the dedicated render queue the refractive water surface renders in - its
		//! OWN scene pass (rendered after the opaque scene is captured). Inside the
		//! v2-FAST 0..99 band (an Item queue) and above all opaque 3D/sprite content
		//! (Items 10, sprites 10..90), so the opaque pass ends BELOW it.
		static unsigned char const WATER_REFRACTION_RENDER_QUEUE = 99;
		//! the Ogre-Next default Item render queue (@see OgreRenderQueue.h) - a
		//! water surface restores to it when it stops being refractive
		static unsigned char const DEFAULT_ITEM_RENDER_QUEUE = 10;

		//--- planar water reflection (Ogre::PlanarReflections) -------------
		//! @brief whether the named material is a live PLANAR-REFLECTIVE water
		//! material (createOrUpdateWaterDatablock recorded it). MeshInstance::
		//! setMaterial reads this - a reflective surface renders in the water
		//! render queue (WATER_REFRACTION_RENDER_QUEUE) so the reflection scene
		//! render excludes it (a surface must not appear in its own mirror), and
		//! its Item is registered with the reflection subsystem
		static bool isPlanarReflectiveWaterMaterial(String const & name);
		//! @brief record whether the named water material renders PLANAR-REFLECTIVE
		//! and, when it does, the world-space plane the reflection mirrors across
		//! (@p planeHeightY, normal +Y) and the surface half-extents. Stands the
		//! Ogre::PlanarReflections subsystem up on the first reflective material
		//! and tears it down when the last one goes away; updates the reflection
		//! actor plane. @see createOrUpdateWaterDatablock
		static void noteWaterMaterialPlanarReflective(String const & name,
			bool reflective, float planeHeightY, float halfSizeX, float halfSizeZ);
		//! @brief register (or drop) a water Item's sub-renderables with the
		//! reflection subsystem so the HlmsPbs datablock samples the reflection RTT.
		//! Called from MeshInstance::setMaterial: @p reflective true tracks the
		//! Item's SubItems (idempotent), false drops a previously-tracked Item.
		//! No-op while the subsystem is absent. @see registerPlanarReflectionItem
		static void registerPlanarReflectionItem(Ogre::Item* item, bool reflective);
		//! @brief per-frame reflection drive: begins the reflection frame and
		//! updates it against the window camera (which renders the mirror RTTs),
		//! before the window workspace renders. No-op while the subsystem is
		//! inactive. Called before every Root::renderOneFrame (@see
		//! RenderSystem::renderOneFrame / saveWindowContents).
		static void updatePlanarReflections();
		//! drop the reflection subsystem (unset it on HlmsPbs, destroy its
		//! cameras/RTTs/workspaces) BEFORE the root tears the scene manager down
		static void destroyPlanarReflections();
		//! hand-build (once) + return the compositor workspace DEFINITION name the
		//! reflection subsystem renders each active actor's mirror through
		static String ensurePlanarReflectionWorkspaceDef();
		//! stand the reflection subsystem up (idempotent): construct it, allocate
		//! its ONE reflection slot + RTT, and hand it to HlmsPbs
		static void ensurePlanarReflectionsSubsystem();

		//--- projected decals (RenderDecal) --------------------------
		//! @brief load @p textureName into the shared decal-diffuse pool (a
		//! fixed-size array so every decal texture shares ONE master the scene
		//! samples) and, on first use, hand that master to the SceneManager
		//! (setDecalsDiffuse). Returns the pooled texture (auto slice), or NULL +
		//! one log line when the resource is missing OR its resolution does not
		//! match DECAL_TEXTURE_SIZE (a mismatch would land in another pool). The
		//! pool is reserved lazily on the first call.
		static Ogre::TextureGpu* loadDecalDiffuseTexture(String const & textureName);
		//! @brief the world's visible-decal budget (@see RenderWorld::setMaxDecals):
		//! a create registers here in order; register/setMaxDecals both re-enforce
		//! the cap by hiding the OLDEST decals over it (budgetVisible=false) and
		//! showing the newest up to it. A cap of 0 hides every decal.
		static void registerDecal(RenderDecal* decal);
		static void unregisterDecal(RenderDecal* decal);
		//! re-apply the cap over the whole registry (a RenderBackend member so it
		//! may touch RenderDecal::Impl - the friendship is to the hub)
		static void enforceDecalBudget();
		static void setMaxDecals(unsigned int maxDecals);
		static unsigned int maxDecals();
		//! decals currently visible under the budget (budgetVisible && userVisible
		//! && opacityVisible) - the eviction / toggle-identity probe
		static unsigned int visibleDecalCount();
		//! drop the decal registry + pool statics (destroyRenderSystem teardown -
		//! the pool texture dies with the texture manager)
		static void resetDecalState();

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
		static void notImplementedOnce(char const * feature);
	};
}

#endif //__NextBackend_h__8_7_2026__20_00_00__
