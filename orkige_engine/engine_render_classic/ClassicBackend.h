/********************************************************************
	created:	Wednesday 2026/07/08 at 18:00
	filename: 	ClassicBackend.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ClassicBackend_h__8_7_2026__18_00_00__
#define __ClassicBackend_h__8_7_2026__18_00_00__

//! @file ClassicBackend.h
//! @brief PRIVATE plumbing of the classic-OGRE engine_render backend
//! @remarks This header is the one place that pairs the backend-free
//! facade headers (engine_render/) with classic OGRE 14 types. Only the
//! engine_render_classic/*.cpp TUs and engine_graphic/Engine.cpp (the
//! classic bootstrapper, which creates/destroys the RenderSystem) may
//! include it - application code and everything above engine_graphic
//! talk to the facade headers exclusively. ONE sanctioned exception
//! (a deliberate design decision, see Docs/render-abstraction.md): the root-motion
//! backdoor in engine_gocomponent/AnimationComponent.cpp includes this
//! inside its documented #if ORKIGE_RENDER_CLASSIC block to reach
//! RenderBackend::ogreEntity. A planned
//! engine_render_next/ClassicBackend counterpart mirrors this file
//! against Ogre-Next (Docs/render-abstraction.md, "Directory layout").

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
#include "engine_module/EnginePrerequisitesClassic.h"

#include <vector>

#ifndef ORKIGE_RENDER_CLASSIC
#	error "engine_render_classic compiled without ORKIGE_RENDER_CLASSIC - the build flavor wiring regressed (see ORKIGE_RENDER_BACKEND in CMake)"
#endif

namespace Orkige
{
	class Engine;

	//--- backend state of the facade classes (pimpl bodies) -----------
	// Out-of-line definitions of the protected nested Impl structs; each
	// facade class's methods (its own TU) see its own Impl directly,
	// cross-class access goes through the RenderBackend statics below.

	struct RenderSystem::Impl
	{
		Engine*				engine = NULL;			//!< the classic bootstrapper (Engine singleton, not owned)
		RenderWorld*		world = NULL;			//!< the one scene world (owned, deleted in ~RenderSystem)
		optr<RenderCamera>	windowCamera;			//!< camera currently shown full-window (keeps it alive)
		optr<RenderCamera>	engineWindowCamera;		//!< lazy non-owning wrap of Engine's default camera (getWindowCamera on the Engine path)
		Ogre::ColourValue	windowBackground = Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f);
		bool				uiOnlyWindow = false;	//!< showUIOnlyWindow mode: the window shows only 2D layers (getWindowCamera answers NULL)
		Ogre::Camera*		uiOnlyCamera = NULL;	//!< internal camera feeding the UI-only viewport (owned by the scene manager)
		unsigned int		caps = 0;				//!< RenderCaps bitset (bit i = supports RenderCaps(i)), filled at boot
	};

	struct RenderWorld::Impl
	{
		Ogre::SceneManager*	sceneManager = NULL;	//!< owned by Engine (classic bootstrap), not by the facade
		optr<RenderNode>	rootNode;				//!< stable facade handle of the root node (owned=false)
		//! classic ambient is flat - the two hemisphere colours are cached so the
		//! facade getters read them back honestly (the scene sees their average)
		Ogre::ColourValue	ambientUpper = Ogre::ColourValue(0.2f, 0.2f, 0.2f, 1.0f);
		Ogre::ColourValue	ambientLower = Ogre::ColourValue(0.2f, 0.2f, 0.2f, 1.0f);
		//! the shadow quality knob position - RECORDED only on this flavor
		//! (no dynamic shadows on classic, @see RenderWorld::setShadowQuality)
		ShadowPreset::Quality	shadowQuality = ShadowPreset::SQ_MEDIUM;
		//! the sky/fog atmosphere last set (@see RenderWorld::setAtmosphere);
		//! classic renders the fog subset + a vertex-colour gradient sky dome
		AtmosphereDesc			atmosphere;
		//! the gradient sky dome (built lazily when the atmosphere is enabled;
		//! a camera-following inward sphere in the sky render queue). Its vertex
		//! colours recompute from the atmosphere + the sun (first directional
		//! light); NULL until the first enabled setAtmosphere.
		Ogre::ManualObject*		skyDome = NULL;
		Ogre::SceneNode*		skyNode = NULL;
	};

	struct RenderNode::Impl
	{
		Ogre::SceneNode*	node = NULL;
		Ogre::SceneManager*	creator = NULL;
		bool				owned = true;			//!< false for the world root (never destroyed by the handle)
		void*				userPointer = NULL;		//!< @see RenderNode::setUserPointer
		woptr<RenderNode>	parent;					//!< facade graph mirror (backend child lists are never walked)
		std::vector<woptr<RenderNode>>	children;	//!< facade graph mirror, pruned lazily
	};

	struct MeshInstance::Impl
	{
		Ogre::Entity*		entity = NULL;
		Ogre::SceneManager*	creator = NULL;
		String				meshName;
		optr<RenderNode>	attachedTo;				//!< keeps the node alive while content hangs off it
	};

	struct SpriteQuad::Impl
	{
		Ogre::ManualObject*	quad = NULL;
		Ogre::SceneManager*	creator = NULL;
		String				textureName;
		Ogre::TexturePtr	texture;				//!< the loaded texture (per-sampler material rebinds need it)
		String				materialName;			//!< the per-(texture,sampler) material the quad renders with
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
		//! v1 sprite rules SpriteComponent renders with today)
		void rebuild();
	};

	struct SpriteBatch::Impl
	{
		Ogre::ManualObject*	batch = NULL;
		Ogre::SceneManager*	creator = NULL;
		String				textureName;
		Ogre::TexturePtr	texture;				//!< the loaded texture (texel-size queries)
		String				materialName;			//!< the shared per-(texture,blend) material the batch renders with
		SpriteBatch::BlendMode	blendMode = SpriteBatch::BLEND_ALPHA;
		float				texelWidth = 0.0f;		//!< texture size in texels (atlas UV derivation)
		float				texelHeight = 0.0f;
		int					zOrder = 0;
		std::size_t			quadCount = 0;			//!< quads in the batch right now
		optr<RenderNode>	attachedTo;

		//! (re)build the manual object from a CPU vertex array (4 verts/quad,
		//! TL/TR/BR/BL); an empty array leaves the object with no geometry
		void rebuild(SpriteBatch::Vertex const * vertices, std::size_t quadCount);
	};

	struct VectorMesh::Impl
	{
		Ogre::ManualObject*	mesh = NULL;
		Ogre::SceneManager*	creator = NULL;
		int					zOrder = 0;
		std::size_t			triangleCount = 0;		//!< triangles in the mesh right now
		std::size_t			vertexCount = 0;		//!< vertices in the built section (dynamic-update guard)
		std::vector<Ogre::uint32>	indices;		//!< cached topology, re-emitted by beginUpdate on a dynamic update
		optr<RenderNode>	attachedTo;

		//! (re)build the manual object from an arbitrary CPU vertex + index
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
		bool				owned = true;			//!< false for wraps of Engine-owned cameras (never destroyed by the handle)
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
		//! triangle list in pixel space (@see DrawLayer2DClip.h)
		struct Batch
		{
			String								textureName;
			//! offscreen-target binding (the addTriangles RenderTexture
			//! overload); the batch keeps the target alive until clear()
			optr<RenderTexture>					renderTexture;
			std::vector<DrawLayer2D::Vertex2D>	triangles;
		};

		bool				visible = true;
		int					zOrder = 0;
		std::vector<Batch>	batches;
		bool				dirty = true;			//!< vertex buffer refill needed

		//--- render plumbing (one dynamic vertex buffer per layer, the
		//--- Gorilla machinery generalized; filled lazily at render time)
		Ogre::RenderOperation	renderOp;			//!< its VertexData is owned here
		Ogre::HardwareVertexBufferSharedPtr	vertexBuffer;
		size_t				vertexBufferCapacity = 0;
		//! per batch: [start, count) vertex range inside the buffer
		std::vector<std::pair<size_t, size_t>>	batchRanges;
		float				filledWidth = 0.0f;		//!< window size the buffer was
		float				filledHeight = 0.0f;	//!< transformed to NDC against

		//! (re)fill the vertex buffer from the batches (pixel -> NDC)
		void fillVertexBuffer(float windowWidth, float windowHeight);
		//! drop the vertex data again (dtor)
		void destroyVertexBuffer();
	};

	struct RenderTexture::Impl
	{
		Ogre::TexturePtr	texture;
		String				name;
		unsigned int		width = 0;
		unsigned int		height = 0;
		optr<RenderCamera>	camera;					//!< keeps the fed camera alive
		Ogre::ColourValue	background = Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f);
		bool				overlaysEnabled = true;
		bool				shadowsEnabled = true;

		//! (re)create the backend texture + viewport from the state above
		//! (resize-by-recreate, the editor's proven RTT pattern)
		void recreate();
		//! push background/overlays/shadows onto the current viewport
		void applyViewportState();
		//! drop viewport + texture (dtor and the recreate path)
		void destroyTexture();
	};

	//--- the backend hub -----------------------------------------------
	//! @brief the classic backend's cross-class door (befriended by every
	//! facade class, @see engine_render/RenderPrerequisites.h)
	//! @remarks Static-only. Holds the process-wide backend state: the
	//! RenderSystem singleton pointer behind RenderSystem::get() and the
	//! SceneNode -> facade-handle registry that back-maps ray query hits,
	//! getParent navigation and the user-pointer walks. One window, one
	//! world, one registry - multi-anything stays frozen per the decided
	//! design questions.
	struct RenderBackend
	{
		//--- lifecycle (called from the classic bootstrapper Engine) ---
		//! create the facade RenderSystem over a set-up Engine (scene
		//! manager + render window must exist); wires RenderSystem::get
		static RenderSystem* createRenderSystem(Engine* engine);
		//! tear the facade down again (idempotent; called from ~Engine
		//! while the Ogre root is still alive)
		static void destroyRenderSystem();
		//! the live RenderSystem or NULL (backs RenderSystem::get)
		static RenderSystem* system();

		//! @brief resolve a texture resource name against the cooked-payload
		//! rename: an exported payload block-compresses textures, replacing
		//! foo.png with foo.dds/foo.ktx - when the plain name is missing
		//! from every resource group, the cooked siblings are tried (id-less
		//! bare-name references: .omat maps, gui atlases, script-set sprite
		//! names; id-carrying references resolve through the renamed sidecar
		//! before they reach the backend). Dev trees always hit the raw name.
		static String resolveTextureResourceName(String const & textureName);

		//--- handle factories (protected facade ctors) -----------------
		//! wrap an existing backend node into an owning facade handle and
		//! register it; parent is the facade-graph parent (NULL for root)
		static optr<RenderNode> wrapNode(Ogre::SceneNode* node, bool owned,
			optr<RenderNode> const & parent);
		static optr<MeshInstance> createMeshInstance(
			Ogre::SceneManager* sceneManager, String const & meshName);
		static optr<SpriteQuad> createSpriteQuad(
			Ogre::SceneManager* sceneManager, String const & textureName);
		static optr<SpriteBatch> createSpriteBatch(
			Ogre::SceneManager* sceneManager, String const & textureName,
			SpriteBatch::BlendMode blendMode);
		//! create a world-space untextured vertex-coloured triangle mesh
		//! (@see VectorMesh; the shared "VectorFill" material)
		static optr<VectorMesh> createVectorMesh(Ogre::SceneManager* sceneManager);
		static optr<RenderCamera> createCamera(
			Ogre::SceneManager* sceneManager, String const & name);
		//! wrap an EXISTING backend camera into a facade handle (owned=false
		//! leaves destruction with the creator - the Engine-default-camera
		//! bridge behind RenderSystem::getWindowCamera)
		static optr<RenderCamera> wrapCamera(Ogre::Camera* camera, bool owned);
		static optr<RenderLight> createLight(Ogre::SceneManager* sceneManager);
		//! the SUN the sky dome links to: the FIRST directional light in
		//! creation order (same semantic as the next flavor), or NULL. The
		//! registry below keeps this in step as lights change kind or die.
		static Ogre::Light* firstDirectionalLight();
		//! keep the directional-light registry in step with @p light's kind
		//! (RenderLight::setType / ~RenderLight). A membership change re-resolves
		//! a live sky dome's sun so orienting/retyping the sun updates the sky.
		static void noteDirectionalLight(Ogre::Light* light, bool isDirectional);
		//! @brief a RenderLight setter authored a colour. While the atmosphere
		//! DRIVES @p light (it is the linked sun of the sun-exposure linkage),
		//! the authored value lands in the restore snapshot instead of the live
		//! light - the atmosphere owns the visible colour, and disabling it
		//! restores the LATEST authored one. Returns true when consumed that
		//! way (the caller then skips the live write).
		static bool noteAuthoredSunColour(Ogre::Light* light,
			Ogre::ColourValue const & colour, bool specular);
		//! recompute the live sky dome's vertex colours from the cached
		//! atmosphere + the current sun (a no-op when no dome is up) AND
		//! re-resolve the sun-exposure linkage. Called when the sun registry
		//! changes (@see noteDirectionalLight).
		static void refreshSkyDome();
		static optr<RenderTexture> createRenderTexture(String const & name,
			unsigned int width, unsigned int height);
		//! create a 2D overlay layer (registers it with the render hook -
		//! a RenderQueueListener compositing after RENDER_QUEUE_OVERLAY on
		//! the main window's viewport only; @see DrawLayer2DClassic.cpp)
		static optr<DrawLayer2D> createDrawLayer2D(int zOrder);
		//! drop a dying layer from the render hook again (facade dtor);
		//! the last layer detaches the hook from the scene manager
		static void unregisterDrawLayer2D(DrawLayer2D* layer);
		//! the render hook's worker (RenderBackend member so it reaches
		//! the facade Impl state): composite all layers into the CURRENT
		//! viewport IF it is the main window's (@see DrawLayer2DClassic.cpp)
		static void renderDrawLayers2D(Ogre::SceneManager* sceneManager);
		//! drop the cached "DrawLayer2D/<texture>" material again - called
		//! when RenderSystem::createTexture2D REPLACES a texture under an
		//! existing name (the material would keep the dead incarnation)
		static void invalidateDrawLayer2DTexture(String const & textureName);

		//--- guts accessors (NULL-safe) ---------------------------------
		static Ogre::SceneNode* sceneNode(optr<RenderNode> const & node);
		static Ogre::Camera* ogreCamera(optr<RenderCamera> const & camera);
		//! the target's CURRENT backend texture (changes across resizes) -
		//! the 2D layer binds render-texture batches through this per draw
		static Ogre::TexturePtr ogreTexture(optr<RenderTexture> const & texture);
		//! the wrapped entity - ONLY for AnimationComponent's root-motion
		//! backdoor (see the file remarks above)
		static Ogre::Entity* ogreEntity(optr<MeshInstance> const & mesh);

		//--- node registry (Ogre::SceneNode* -> facade handle) ----------
		static void registerNode(Ogre::SceneNode* node,
			optr<RenderNode> const & handle);
		static void unregisterNode(Ogre::SceneNode* node);
		//! the facade handle wrapping the node, or NULL for non-facade nodes
		static optr<RenderNode> findNode(Ogre::SceneNode* node);
		//! first non-NULL user pointer from node towards the root
		static void* findUserPointerUpwards(Ogre::SceneNode* node);

		//--- shared services --------------------------------------------
		//! unique name for backend-created content ("prefix.<n>")
		static String generateName(String const & prefix);
		//! the RTSS material scheme wiring every viewport needs on the
		//! shader-only render systems (same as Engine/editor apply today)
		static void applyRTSSScheme(Ogre::Viewport* viewport);
		//! @brief the generated per-(texture,sampler) sprite material,
		//! idempotent per SpriteQuad::samplerName key: unlit, alpha-blended,
		//! depth-checked/not-written, two-sided, with the requested
		//! filter/addressing on its texture unit. The sampler is baked into
		//! the name so distinct sampling of one texture never shares (stomps)
		//! a material.
		static Ogre::MaterialPtr getOrCreateSpriteMaterial(
			Ogre::TexturePtr const & texture, SpriteQuad::FilterMode filter,
			SpriteQuad::AddressMode addressing);
		//! @brief the shared per-(texture,blend) sprite-batch material: the
		//! alpha variant IS the SpriteQuad "Sprite/<tex>#bilinear-clamp"
		//! material (reused wholesale); the additive variant is a distinct
		//! "SpriteAdd/<tex>#bilinear-clamp" material (SBF_SOURCE_ALPHA/SBF_ONE -
		//! src.rgb*src.a + dst, order-independent glow). Idempotent per name.
		static Ogre::MaterialPtr getOrCreateSpriteBatchMaterial(
			Ogre::TexturePtr const & texture, SpriteBatch::BlendMode blendMode);
		//! @brief the ONE shared untextured vertex-colour "VectorFill" material:
		//! unlit, vertex colours tracked (TVC_DIFFUSE), alpha-blended,
		//! depth-checked/not-written, two-sided, NO texture unit. Idempotent -
		//! every vector shape renders through this one material.
		static Ogre::MaterialPtr getOrCreateVectorFillMaterial();
		//! @brief create OR UPDATE the named lit scene-content material from a
		//! facade surface description (RenderSystem::createMaterial). When the
		//! RTSS shader generator is active (the shader-only render systems this
		//! flavor targets - GL3Plus/Vulkan/GLES2) the material renders through a
		//! metal-rough Cook-Torrance lighting stage: albedo -> diffuse colour +
		//! texture unit, metalness/roughness -> the specular.xy the lighting
		//! stage reads, emissive colour -> self-illumination, the tangent-space
		//! normal map -> an RTSS normal-map stage that perturbs the lit normal,
		//! and the emissive map -> an additive self-illumination pass. Without a
		//! shader generator it degrades to the fixed-function Blinn-Phong subset
		//! (derived specular, maps ignored + logged once). A missing texture is
		//! skipped + logged and clears outComplete.
		static Ogre::MaterialPtr createOrUpdateSurfaceMaterial(
			String const & name, RenderMaterialDesc const & desc,
			bool & outComplete);
		//! @brief true when the named surface material binds a normal map, so a
		//! mesh it is applied to must carry tangents (MeshInstance::setMaterial
		//! builds them on demand). Populated by createOrUpdateSurfaceMaterial;
		//! an unknown name answers false.
		static bool materialUsesNormalMap(String const & name);
		//! @brief create OR UPDATE the named transparent WATER material
		//! (RenderSystem::createWaterMaterial). This flavor renders the honest
		//! subset: the deep/shallow colours blend into ONE flat water tint, a
		//! glossy specular highlight, alpha transparency, and the normal map (if
		//! any) bound as a scrolling shimmer overlay (an illusion - true
		//! tangent-space normal-mapped fresnel water is next-only, logged once).
		//! Registers the scroll speed so setWaterMaterialTime can drive the
		//! shimmer UV. A missing normal map is skipped + logged and clears
		//! outComplete.
		static Ogre::MaterialPtr createOrUpdateWaterMaterial(
			String const & name, RenderWaterDesc const & desc,
			bool & outComplete);
		//! @brief scroll a water material's shimmer overlay to @p seconds
		//! (RenderSystem::setWaterTime) - a name with no registered water
		//! material is a silent no-op (the dormancy rule)
		static void setWaterMaterialTime(String const & name, float seconds);
		//! zOrder -> render queue id (painter's sorting around MAIN)
		static Ogre::uint8 renderQueueForZOrder(int zOrder);
	};
}

#endif //__ClassicBackend_h__8_7_2026__18_00_00__
