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
//! engine_render_classic/*.cpp TUs, engine_graphic/Engine.cpp (the
//! classic bootstrapper, which creates/destroys the RenderSystem) and the
//! render_facade selfcheck's bootstrap_classic.cpp (the per-backend test
//! TU, mirroring bootstrap_next.cpp's NextBackend.h include) may
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
#include "engine_render/RenderDecal.h"
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
		unsigned int		lightBudget = 0;		//!< sane concurrent dynamic-light ceiling (@see RenderSystem::lightBudget), filled at boot
	};

	struct RenderWorld::Impl
	{
		Ogre::SceneManager*	sceneManager = NULL;	//!< owned by Engine (classic bootstrap), not by the facade
		optr<RenderNode>	rootNode;				//!< stable facade handle of the root node (owned=false)
		//! classic ambient is flat - the two hemisphere colours are cached so the
		//! facade getters read them back honestly (the scene sees their average)
		Ogre::ColourValue	ambientUpper = Ogre::ColourValue(0.2f, 0.2f, 0.2f, 1.0f);
		Ogre::ColourValue	ambientLower = Ogre::ColourValue(0.2f, 0.2f, 0.2f, 1.0f);
		//! the shadow quality knob position (@see RenderWorld::setShadowQuality;
		//! RenderBackend::applyShadowConfig arms/disarms the scene-level RTSS
		//! integrated-PSSM technique from it)
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
		//! the image-lighting opt-in (@see RenderWorld::setImageLighting);
		//! renders only with the quality knob on AND a loaded skybox cubemap
		bool					iblEnabled = false;
		//! scales the added cubemap contribution (the IBL stage's luminance)
		float					iblIntensity = 1.0f;
		//! the IBL quality knob (chain budgets in core_util/IblPreset.h)
		IblPreset::Quality		iblQuality = IblPreset::IQ_MEDIUM;
		//! the LDR bloom knob + tunables (@see RenderWorld::setBloom); the
		//! OrkigeBloom viewport compositor is enabled only while quality != BQ_OFF
		//! AND bloom.enabled (@see RenderBackend::bloomActive). Default MEDIUM
		//! tier, bloom OFF.
		BloomPreset::Quality	bloomQuality = BloomPreset::BQ_MEDIUM;
		BloomDesc				bloom;
	};

	struct RenderNode::Impl
	{
		Ogre::SceneNode*	node = NULL;
		Ogre::SceneManager*	creator = NULL;
		bool				owned = true;			//!< false for the world root (never destroyed by the handle)
		void*				userPointer = NULL;		//!< @see RenderNode::setUserPointer
		woptr<RenderNode>	parent;					//!< facade graph mirror (backend child lists are never walked)
		std::vector<woptr<RenderNode>>	children;	//!< facade graph mirror, pruned lazily
		//! the mobility flag (@see RenderNode::setStatic): attached mesh
		//! content is StaticGeometry-bake eligible while set
		bool				isStatic = false;
		//! one mobility-contract warning per node (@see RenderNode::setStatic)
		bool				staticMoveWarned = false;

		//! the mobility-contract repair path: a transform mutation reached a
		//! static node - warn once, then DEMOTE the subtree's baked entities
		//! out of their regions (one rebuild; they render individually and
		//! follow the node again - correct but costly)
		void noteStaticMutation(char const * operation);
	};

	struct MeshInstance::Impl
	{
		Ogre::Entity*		entity = NULL;
		Ogre::SceneManager*	creator = NULL;
		String				meshName;
		optr<RenderNode>	attachedTo;				//!< keeps the node alive while content hangs off it
		//! per-sub-entity ORIGINAL materials, held while the instance is
		//! switched to no-receive variants (@see MeshInstance::setReceiveShadows);
		//! empty while the instance receives shadows normally
		std::vector<Ogre::MaterialPtr>	receiveRestore;
		//! runtime accents (@see MeshInstance::setTint/setEmissiveBoost):
		//! the current tint (white = none) and additive emissive boost
		//! (black = none), plus - while accented - the per-sub-entity
		//! ORIGINAL materials and the per-instance accent VARIANT clones
		//! that render in their place (destroyed on restore/teardown)
		Color	accentTint = Color(1.0f, 1.0f, 1.0f, 1.0f);
		Color	accentBoost = Color(0.0f, 0.0f, 0.0f, 1.0f);
		std::vector<Ogre::MaterialPtr>	accentRestore;
		std::vector<Ogre::MaterialPtr>	accentVariants;
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
		std::size_t			allocatedQuads = 0;		//!< quads the live buffers were built for (in-place refresh guard)
		optr<RenderNode>	attachedTo;

		//! (re)build the manual object from a CPU vertex array (4 verts/quad,
		//! TL/TR/BR/BL); an empty array leaves the object with no geometry.
		//! A refresh at the SAME quad count updates the live buffers in place
		//! (beginUpdate - no per-frame buffer realloc); a count change rebuilds
		void rebuild(SpriteBatch::Vertex const * vertices, std::size_t quadCount);
	};

	struct VectorMesh::Impl
	{
		//! one built ManualObject section: its resolved material, cached
		//! topology (re-emitted by beginUpdate on a dynamic update) and
		//! whether the vertex format carries a UV stream
		struct BuiltSection
		{
			String						material;	//!< "VectorFill" or the per-texture sprite material
			bool						textured = false;	//!< emit UVs (a texture is bound)
			std::size_t					vertexCount = 0;	//!< dynamic-update guard (0 = section built nothing)
			std::size_t					ogreSection = 0;	//!< ManualObject section ordinal (degenerate entries create none)
			std::vector<Ogre::uint32>	indices;	//!< cached section topology
		};
		Ogre::ManualObject*	mesh = NULL;
		Ogre::SceneManager*	creator = NULL;
		int					zOrder = 0;
		std::size_t			triangleCount = 0;		//!< triangles in the mesh right now
		std::vector<BuiltSection>	sections;		//!< built sections, paint order
		optr<RenderNode>	attachedTo;

		//! (re)build the manual object from a section list (one ManualObject
		//! section per entry: flat = "VectorFill", textured = the sprite
		//! material + a UV stream); an empty list leaves it geometry-free
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
		bool				owned = true;			//!< false for wraps of Engine-owned cameras (never destroyed by the handle)
		optr<RenderNode>	attachedTo;
	};

	struct RenderLight::Impl
	{
		Ogre::Light*		light = NULL;
		Ogre::SceneManager*	creator = NULL;
		optr<RenderNode>	attachedTo;
	};

	struct RenderDecal::Impl
	{
		//! the aligned-quad honest subset: a textured quad in the node's local XZ
		//! plane, depth-biased above the surface (classic has no projective decal)
		Ogre::ManualObject*	quad = NULL;
		Ogre::SceneManager*	creator = NULL;
		optr<RenderNode>	attachedTo;
		String				textureName;			//!< the diffuse (mark) texture, "" until set
		Ogre::TexturePtr	texture;				//!< the loaded texture (material rebinds need it)
		String				materialName;			//!< the per-texture "Decal/<tex>" material ("" until set)
		float				sizeX = 2.0f;			//!< footprint width (local X)
		float				sizeZ = 2.0f;			//!< footprint depth (local Z)
		float				opacity = 1.0f;			//!< smooth alpha (per-vertex on classic)
		//! two visibility gates ANDed onto the quad: the owner/component
		//! (userVisible) and the world decal budget (budgetVisible). Classic dims
		//! opacity smoothly per-vertex, so there is no opacity threshold gate.
		bool				userVisible = true;
		bool				budgetVisible = true;

		//! (re)build the XZ-plane quad from size + opacity (vertex-colour alpha),
		//! rendered with the "Decal/<tex>" material; no-op without a material
		void rebuild();
		//! push (userVisible && budgetVisible) onto the quad's visibility
		void applyVisibility();
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
		//! @brief the classic forward renderer's sane concurrent dynamic-light
		//! ceiling (@see RenderSystem::defaultLightBudget): RTSS generates one
		//! forward material whose Cook-Torrance/FFP stage iterates the active
		//! scene lights per pass, so many dynamic point lights must stay
		//! bounded. 30 is the honest per-pass headroom the lit-content /
		//! shadow / atmosphere work assumed (the night-lights showcase grid),
		//! well within the fixed-function 8-light minimum the RTSS Cook-Torrance
		//! stage lifts to a per-object active-light loop.
		static unsigned int const FORWARD_LIGHT_BUDGET = 30u;

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
			SpriteBatch::BlendMode blendMode, SpriteQuad::FilterMode filter,
			SpriteQuad::AddressMode addressing);
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
		//! create an aligned-quad decal (@see RenderDecalClassic.cpp); joins the
		//! world visible-decal budget on creation
		static optr<RenderDecal> createDecal(Ogre::SceneManager* sceneManager);
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
		//! the cubemap the native sky box currently shows ("" = none) - the
		//! image-lighting source (@see applyImageLighting)
		static String const & activeSkyboxTexture();

		//--- image-based lighting (skybox-sourced) -----------------------
		//! @brief realize the facade image-lighting state (RenderWorld::
		//! setImageLighting/setIblQuality): while active - enabled, knob on, a
		//! loaded skybox cubemap showing, the generated-shader path available -
		//! every generated Cook-Torrance material (surface + water) re-derives
		//! with the image-based-lighting stage appended (DFG LUT + the
		//! tier-capped environment chain, luminance = the intensity); inactive
		//! re-derives them back without it. Also re-run at the end of every
		//! setAtmosphere so the chain follows a skybox change; cheap no-op
		//! while the state is off. Enabling without a skybox source (or on a
		//! render system without the shader path) logs one honest line and
		//! renders unchanged.
		static void applyImageLighting();
		//! @brief can this backend render image-based lighting at all: the
		//! shader generator is active AND the generated shaders can index the
		//! cubemap's mip chain (GLSL ES 3.0 on a GLES context - the
		//! RenderCaps::IblReflections fill; runtime-determined per device)
		static bool imageBasedLightingSupported();
		//! drop the image-lighting bookkeeping + the derived chain texture
		//! (world teardown; the generated materials die with the manager)
		static void imageLightingTeardown();

		//--- dynamic shadows (the scene-level RTSS integrated PSSM) ------
		//! @brief (re)apply the shadow configuration: ARM the scene's
		//! SHADOWTYPE_TEXTURE_ADDITIVE_INTEGRATED technique (depth shadow
		//! textures + a PSSM camera setup sized by ShadowPreset + the RTSS
		//! shadow-mapping receiver sub-render-state injected ONCE into the
		//! generated-material scheme) while the world knob is on AND a
		//! directional light casts AND the atmosphere-driven sun is not
		//! night-dimmed; DISARM restore-exactly otherwise (technique NONE,
		//! shadow maps freed, texture counts restored, receiver removed). A
		//! tier change while armed re-arms with the new budgets. Called from
		//! every seam that can change the answer: the quality knob, a light's
		//! cast flag, the directional-light registry and the sun-exposure
		//! dim gate. A render system without depth-texture render targets
		//! (a bare GLES2/WebGL1 context) refuses with ONE log line.
		static void applyShadowConfig();
		//! @brief the sun-exposure linkage dimmed the driven sun to (near)
		//! black - a shadow pass under a black sun costs frame time and shows
		//! nothing, so night disarms it (and dawn re-arms). Only the
		//! atmosphere drive calls this; a state CHANGE re-applies the config.
		static void noteSunDimmedForShadows(bool dimmed);
		//! @brief can this backend render dynamic shadow maps at all: the
		//! RTSS shader generator is active AND the render system can create
		//! depth-texture render targets (the RenderCaps::DynamicShadows fill;
		//! runtime-determined - a GLES2 context answers per device)
		static bool dynamicShadowsSupported();
		//! @brief one-line description of the live shadow infrastructure
		//! (technique, configured texture counts, receiver injection) - the
		//! render_facade selfcheck asserts arm/disarm restores it EXACTLY
		static String shadowStateDescription();
		//! @brief is the scene-level shadow technique currently armed
		//! @remarks While armed, per-target shadow DISABLE is overridden: an
		//! integrated technique bakes the receiver into the scene's generated
		//! shaders, and classic OGRE only maintains the shadow-projector
		//! state for viewports that prepare shadow textures - rendering those
		//! shaders in a shadows-disabled viewport reads stale projectors (a
		//! crash class, @see RenderTexture::Impl::applyViewportState).
		static bool shadowsArmed();

		//--- LDR bloom (viewport compositor) -------------------------
		//! visibility bit carried by explicitly tagged 3D-tier movables (mesh
		//! instances). The tier split itself keys on SCENE_2D: the bloom scene
		//! target masks to ~SCENE_2D, so untagged movables (sky dome, decals,
		//! static regions) default into the 3D tier (@see createRenderSystem).
		static unsigned int const SCENE_3D_VISIBILITY = 0x00000001u;
		//! visibility bit carried by the 2D tier (sprite quads/batches + vector
		//! meshes) - the bloom compositor's output target masks to this so the
		//! 2D tier draws un-bloomed over the combined 3D result.
		static unsigned int const SCENE_2D_VISIBILITY = 0x00000002u;
		//! @brief tag a 3D-tier movable so the bloom scene split includes it in
		//! the glow source. Byte-stable while bloom is off (every viewport
		//! renders with the default 0xFFFFFFFF mask, matching every bit).
		static void tagScene3D(Ogre::MovableObject* movable);
		//! @brief tag a 2D-tier movable so the bloom scene split keeps it OUT of
		//! the glow source (drawn un-bloomed over the combine instead).
		static void tagScene2D(Ogre::MovableObject* movable);
		//! whether the bloom compositor is currently ACTIVE: the world quality
		//! knob is not BQ_OFF, a scene enabled bloom, and the backend supports it
		static bool bloomActive();
		//! whether this backend can render the bloom compositor at all: the RTSS
		//! shader generator is active AND the render system can create the
		//! off-screen colour render targets (the RenderCaps::Bloom fill,
		//! runtime-determined - a GLES2/WebGL context answers false, gated
		//! pending an on-device/browser proof run of the compositor chain)
		static bool bloomSupported();
		//! @brief re-apply the bloom configuration (RenderWorld::setBloom /
		//! setBloomQuality): build/enable/disable the per-tier bloom compositor
		//! on the window viewport (@see buildBloomCompositor in the .cpp) and
		//! push the live threshold/intensity onto its materials. Idempotent; a
		//! bloom-less context refuses with ONE log line.
		static void applyBloomConfig();
		//! @brief is the given viewport the bloom compositor's OUTPUT viewport
		//! (target_output renders through the window's own viewport, so the 2D
		//! overlay-queue GUI listener composites there). @see DrawLayer2DClassic.
		static bool isBloomOutputViewport(Ogre::Viewport* viewport);
		//! live render-target registry: applyShadowConfig re-applies every
		//! target's viewport state on arm/disarm (the shadow-toggle override
		//! above), mirroring the next backend's registry
		static void registerRenderTarget(RenderTexture* target);
		static void unregisterRenderTarget(RenderTexture* target);
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
		//! the node's mobility flag (@see RenderNode::setStatic) - content
		//! classes register with the static bake through it on attach
		static bool nodeIsStatic(optr<RenderNode> const & node);
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
		//! alpha variant IS the SpriteQuad "Sprite/<tex>#<sampler>" material
		//! (reused wholesale, so a batched sprite run renders through the
		//! same material its individual quads use); the additive variant is a
		//! distinct "SpriteAdd/<tex>#bilinear-clamp" material
		//! (SBF_SOURCE_ALPHA/SBF_ONE - src.rgb*src.a + dst, order-independent
		//! glow, always bilinear+clamp). Idempotent per name.
		static Ogre::MaterialPtr getOrCreateSpriteBatchMaterial(
			Ogre::TexturePtr const & texture, SpriteBatch::BlendMode blendMode,
			SpriteQuad::FilterMode filter, SpriteQuad::AddressMode addressing);
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
		//! @brief can this backend do screen-space water refraction: the RTSS
		//! generator is active AND the target shading language is desktop GLSL
		//! (GL3Plus - the grab-pass water program is authored for it; a
		//! Vulkan/GLES context runtime-gates false, byte-stable). The
		//! RenderCaps::ScreenSpaceRefraction fill. @see createOrUpdateWaterMaterial
		static bool screenSpaceRefractionSupported();
		//! @brief note a mesh entity's freshly-assigned material so the scene-grab
		//! render target hides the refractive water while it captures the opaque
		//! scene the water samples (registers the entity iff the material is a
		//! live refractive water material). @see MeshInstance::setMaterial
		static void noteMeshMaterialForRefraction(Ogre::Entity* entity,
			String const & materialName);
		//! @brief (re)create the shared window-sized scene-grab render target on
		//! the main camera (idempotent - a no-op when it already matches the
		//! window size); NULL-safe on a missing window/viewport
		static void ensureSceneGrabTexture();
		//! @brief destroy the shared scene-grab target (the last refractive water
		//! left, or teardown)
		static void destroySceneGrabTexture();
		//! @brief drop all screen-space refraction state (the grab target + the
		//! entity/material registries) - called at render-system teardown BEFORE
		//! the scene manager dies, so the auto-updated grab target stops
		//! referencing a dying camera
		static void refractionTeardown();
		//! restore a mesh instance's pre-accent materials and retire its
		//! accent variant clones (@see MeshInstance::setTint; no-op unaccented)
		static void resetMeshAccents(MeshInstance::Impl* impl);
		//! realize a mesh instance's accent state (@see MeshInstance::setTint)
		static void applyMeshAccents(MeshInstance::Impl* impl);
		//--- projected decals (RenderDecal aligned-quad subset) ---------
		//! @brief the generated per-texture "Decal/<tex>" material: unlit,
		//! vertex colours tracked (the fade alpha), alpha-blended, depth-CHECKED
		//! but not written, two-sided, with a DEPTH BIAS pulling the quad toward
		//! the camera so it floats above the surface without z-fighting.
		//! Idempotent per texture name. Returns "" when the texture is missing.
		static String getOrCreateDecalMaterial(String const & textureName,
			Ogre::TexturePtr & outTexture);
		//! @brief the world's visible-decal budget (@see RenderWorld::setMaxDecals):
		//! a create registers here in order; register/setMaxDecals both re-enforce
		//! the cap by hiding the OLDEST decals over it. A cap of 0 hides every one.
		static void registerDecal(RenderDecal* decal);
		static void unregisterDecal(RenderDecal* decal);
		//! re-apply the cap over the whole registry (a member so it may touch
		//! RenderDecal::Impl - the friendship is to the hub)
		static void enforceDecalBudget();
		static void setMaxDecals(unsigned int maxDecals);
		static unsigned int maxDecals();
		//! decals currently visible under the budget (budgetVisible && userVisible)
		static unsigned int visibleDecalCount();
		//! drop the decal registry statics (destroyRenderSystem teardown)
		static void resetDecalState();

		//! @brief order a 2D renderable (sprite / vector fill / sprite batch)
		//! by @p zOrder so a higher zOrder paints ON TOP.
		//! @remarks classic OGRE sorts alpha-blended, depth-write-disabled
		//! renderables by camera distance and does NOT honour the render-queue
		//! GROUP id across groups for them (a lower-group full-screen backdrop
		//! paints over a higher-group sprite - the distance sort wins, and a
		//! centred quad is nearest so it always lands on top). Render PRIORITY
		//! *within one group* IS honoured, so all 2D content shares
		//! RENDER_QUEUE_MAIN and paints by zOrder-as-priority (higher zOrder =
		//! higher priority = drawn later = on top), matching the next flavor's
		//! render-queue-id painter order (WYSIWYG). The priority is centred on
		//! the renderable default so co-planar 2D still straddles other MAIN
		//! transparents (e.g. water) by zOrder sign.
		static void applyZOrder(Ogre::MovableObject* object, int zOrder);
		//--- the static bake (the classic mobility fast path) ------------
		//! @brief entities on STATIC nodes bake into shared StaticGeometry
		//! regions - OGRE's own many-immobile-meshes machinery; the backend
		//! owns only the membership bookkeeping (@see StaticBakeClassic.cpp).
		//! Registration is driven from MeshInstance attach/detach against a
		//! static node and from RenderNode::setStatic over already-attached
		//! entities; the bake itself is DEFERRED - membership changes mark it
		//! dirty and renderOneFrame flushes ONE rebuild per frame at most.
		//! register an entity living on a static node (idempotent)
		static void staticBakeRegister(Ogre::Entity* entity,
			Ogre::SceneNode* node);
		//! drop an entity from the bake again (idempotent; restores its
		//! individual rendering on the next flush)
		static void staticBakeUnregister(Ogre::Entity* entity);
		//! is the entity currently registered with the bake
		static bool staticBakeContains(Ogre::Entity* entity);
		//! request a region rebuild at the next frame boundary (membership or
		//! visibility changed)
		static void staticBakeMarkDirty();
		//! rebuild the regions when dirty - called once per renderOneFrame
		static void staticBakeFlush();
		//! entities currently baked into regions (selfcheck introspection)
		static size_t staticBakeBakedCount();
		//! destroy regions + registry (destroyRenderSystem teardown)
		static void staticBakeTeardown();
	};
}

#endif //__ClassicBackend_h__8_7_2026__18_00_00__
