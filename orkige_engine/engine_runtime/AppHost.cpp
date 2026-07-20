/**************************************************************
	created:	2026/07/12 at 12:00
	filename: 	AppHost.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "engine_runtime/AppHost.h"
#include "engine_runtime/RenderResourceReader.h"

#include "engine_graphic/Engine.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderCamera.h"
#include "engine_render/RenderNode.h"
#include "engine_render/MeshInstance.h"
#include "engine_gocomponent/ModelComponent.h"
#include "engine_input/KeyEventData.h"
#include "engine_util/PlatformWindow.h"
#include "engine_util/StringUtil.h"
#include "core_debug/CVarManager.h"
#include "core_debug/LogManager.h"
#include "core_game/GameObjectManager.h"
#include "core_util/ShadowPreset.h"
#include "core_util/Timer.h"
#include "core_event/GlobalEventManager.h"
#include "core_script/ScriptRuntime.h"
#include "core_filesystem/ResourceReader.h"

#include <algorithm>
#include <cstdlib>

//! the platform window handle Engine::setup embeds into
//! (engine_util/SDLNativeWindow*.{mm,cpp})
extern "C" void* orkige_native_window_handle(SDL_Window* window);

namespace Orkige
{
	//---------------------------------------------------------
	AppHost::AppHost()
	{
	}
	//---------------------------------------------------------
	AppHost::~AppHost()
	{
		// Teardown order. Scripts are the TOPMOST layer: their userdata reach
		// DOWN into engine objects (widgets, render nodes, the camera), so the
		// Lua state must finalize while those systems are still alive - else an
		// owning handle a script still holds runs its widget/node destructor
		// into an already-dead UiLayer/renderer (the shutdown UAF). So:
		//   1. the game world first - its ScriptComponents get their `shutdown`
		//      (needs the ScriptRuntime AND a live renderer), facade objects
		//      release into a live renderer;
		//   2. THEN reset the ScriptRuntime - lua_close runs the GC finalizers
		//      that destroy any widget/node handle a script still owns, while
		//      the render facade + GUI system below are still up;
		//   3. only then the render facade + engine + event singletons, window
		//      last. This deliberately breaks the strict reverse-of-bring-up
		//      (ScriptRuntime came up before the Engine) because Lua holds
		//      references INTO the Engine's systems and must go down first.
		// FIRST de-register the archive reader so no late script load routes a
		// read into a renderer that is about to be torn down below (it wraps
		// RenderSystem::get(), still alive here) - then drop the impl object.
		ResourceAccess::setReader(nullptr);
		this->mResourceReader.reset();
		this->mGameObjectManager.reset();
		this->mScriptRuntime.reset();
		this->mCameraNode.reset();
		this->mWindowCamera.reset();
		this->mRenderWorld = nullptr;
		this->mRenderSystem = nullptr;
		this->mEngine.reset();
		this->mEventManager.reset();
		if (this->mWindow)
		{
			SDL_DestroyWindow(this->mWindow);
			this->mWindow = nullptr;
		}
		if (this->mSdlInitialised)
		{
			SDL_Quit();
		}
	}
	//---------------------------------------------------------
	bool AppHost::initialise(AppHostConfig const & config)
	{
		this->mConfig = config;
		if (!SDL_Init(SDL_INIT_VIDEO))
		{
			oDebugError("engine", 0, "AppHost: SDL_Init failed: " << SDL_GetError());
			return false;
		}
		this->mSdlInitialised = true;
#if defined(ORKIGE_IPHONE) || defined(__ANDROID__)
		// mobile: fullscreen native window; SDL sizes it to the screen/surface
		// regardless of the requested size (it renders at native scale via the
		// view's own content scale, so the window stays in points). RESIZABLE
		// is the rotation opt-in: only a resizable window may follow the
		// device orientation - a fixed one gets pinned to its boot aspect
		SDL_WindowFlags mobileFlags = SDL_WINDOW_FULLSCREEN;
		if (config.resizableWindow)
		{
			mobileFlags |= SDL_WINDOW_RESIZABLE;
		}
		this->mWindow = SDL_CreateWindow(config.windowTitle.c_str(),
			config.windowWidth, config.windowHeight, mobileFlags);
#else
		// desktop: HIGH_PIXEL_DENSITY so the render surface tracks the OS
		// backing scale - both render flavors then derive the same drawable
		// from the same window request (the engine window policy - see the
		// render_backend_parity gate)
		SDL_WindowFlags windowFlags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
		if (config.resizableWindow)
		{
			windowFlags |= SDL_WINDOW_RESIZABLE;
		}
		this->mWindow = SDL_CreateWindow(config.windowTitle.c_str(),
			config.windowWidth, config.windowHeight, windowFlags);
#endif
		if (!this->mWindow)
		{
			oDebugError("engine", 0, "AppHost: SDL_CreateWindow failed: " << SDL_GetError());
			return false;
		}
		// register the window so the Engine can read its content scale and
		// safe-area insets (SDL_GetWindowDisplayScale / SDL_GetWindowSafeArea)
		PlatformWindow::setActiveWindow(this->mWindow);

		// the engine singletons, in the order Engine::setup depends on; the
		// scripting seam must exist before the module init functions run so
		// OrkigeMetaExport reaches the real backend state (otherwise it
		// targets the throwaway fallback state)
		Timer::initialise();
		this->mEventManager = uptr<GlobalEventManager>(new GlobalEventManager());
		this->mScriptRuntime = uptr<ScriptRuntime>(new ScriptRuntime());
		init_module_orkige_core();

		// ORKIGE_SANCTIONED_OGRE_BEGIN(classic-boot) - lint gate, see Util/ogre_containment.json
		// --- per-flavor boot block (Docs/render-abstraction.md "App boot"):
		// on classic, Engine construction/config, the ORKIGE_RENDERSYSTEM
		// pick and the RTSS-internal media registration stay classic
		// plumbing; on the next flavor the Engine sibling
		// (engine_graphic/EngineNext.h) carries the same parameters into
		// RenderBackend::createRenderSystem. After Engine::setup every host
		// talks to the engine_render facade on BOTH flavors.
#ifdef ORKIGE_RENDER_CLASSIC
		this->mEngine = uptr<Engine>(new Engine(Ogre::SMT_DEFAULT,
			StringUtil::BLANK, StringUtil::BLANK, StringUtil::BLANK,
			config.engineLogFile));
#else
		this->mEngine = uptr<Engine>(new Engine(config.engineLogFile));
#endif
		// SDL may have clamped the request to the usable display bounds
		int actualWidth = config.windowWidth;
		int actualHeight = config.windowHeight;
		SDL_GetWindowSize(this->mWindow, &actualWidth, &actualHeight);
		this->mEngine->setCustomWindowParam("width",
			StringUtil::Converter::toString(actualWidth));
		this->mEngine->setCustomWindowParam("height",
			StringUtil::Converter::toString(actualHeight));
		if (!config.automatedRun)
		{
			this->mEngine->setCustomWindowParam("vsync", "true");
		}
#ifdef ORKIGE_RENDER_CLASSIC
		// ORKIGE_RENDERSYSTEM: explicit render system choice ("Vulkan",
		// "Metal", "GL3Plus", "GL" - see Engine::matchRenderSystemName);
		// unset keeps the default (first available, i.e. GL3Plus). Vulkan
		// (MoltenVK on macOS) has full RTSS support; OGRE 14.5's Metal RS
		// does not (no MSL backend - built-in default shaders only). The
		// next flavor boots its platform render system unconditionally.
		if (const char* renderSystemEnv = std::getenv("ORKIGE_RENDERSYSTEM"))
		{
			this->mEngine->setPreferredRenderSystem(renderSystemEnv);
		}
		// RTSS shader library + OgreUnifiedShader.h - backend-internal media,
		// needed before setup: classic bootstrap business, not a facade call
		if (!config.classicMediaDir.empty())
		{
			Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
				config.classicMediaDir + "/Main", "FileSystem",
				Ogre::RGN_INTERNAL);
			Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
				config.classicMediaDir + "/RTShaderLib", "FileSystem",
				Ogre::RGN_INTERNAL);
		}
#else
		// the next flavor's Hlms shader templates are a baked default of its
		// Engine sibling; bundled/exported runs point it at their own media
		if (!config.hlmsMediaDir.empty())
		{
			this->mEngine->setHlmsMediaDir(config.hlmsMediaDir);
		}
#endif
		// ORKIGE_SANCTIONED_OGRE_END
		return true;
	}
	//---------------------------------------------------------
	bool AppHost::setupEngine(std::function<void()> const & registerResources)
	{
		// Boot is exception-SAFE end to end: any failure between the window
		// coming up and the first frame - engine setup, resource-group init, a
		// render call a contended/broken driver throws from - unwinds to a clean
		// `false` (a non-zero app exit), never an uncaught throw that terminates
		// the process (segfaulting mid-unwind on some drivers). std::exception
		// covers Ogre's exceptions too (they derive from it), so the render
		// backend stays uncoupled from this flavor-neutral host.
		try
		{
			return this->setupEngineBody(registerResources);
		}
		catch (std::exception const & e)
		{
			oDebugError("engine", 0, "AppHost: engine setup failed with an "
				"exception - exiting cleanly instead of crashing: " << e.what());
			return false;
		}
	}
	//---------------------------------------------------------
	bool AppHost::setupEngineBody(std::function<void()> const & registerResources)
	{
		// the platform bridge may honestly have no native handle to offer
		// (the browser's one canvas IS the window): an empty handle string
		// makes the engine create its own render window instead of parsing
		// a meaningless zero
		void* const nativeWindowHandle =
			orkige_native_window_handle(this->mWindow);
		if (!this->mEngine->setup(this->mConfig.windowTitle,
			Engine::SHOW_NEVER, nativeWindowHandle
				? StringUtil::Converter::toString(
					reinterpret_cast<size_t>(nativeWindowHandle))
				: String()))
		{
			oDebugError("engine", 0, "AppHost: engine setup failed");
			return false;
		}
		// from here on the host talks to the renderer through the
		// engine_render facade exclusively (both flavors)
		this->mRenderSystem = RenderSystem::get();
		this->mRenderWorld = this->mRenderSystem->getWorld();

		// the host's own resource locations, then one group initialisation
		if (registerResources)
		{
			registerResources();
		}
		// TEST SEAM: a post-setup failure (resource-group init throwing on a
		// contended/broken driver, the last step before the first frame) must
		// also unwind to a clean exit, not crash
		if (const char* stage = std::getenv("ORKIGE_TEST_FORCE_BOOT_FAILURE"))
		{
			if (String(stage) == "resources")
			{
				throw std::runtime_error(
					"forced resource-init boot failure (test seam)");
			}
		}
		this->mRenderSystem->initialiseResourceGroups();

		// install the archive-aware content reader NOW - after every mount a
		// host registered (registerResources ran above) is live and the groups
		// are indexed. Core loaders (scripts today; scenes/config/localisation
		// next) resolve a name across loose files AND mounted paks/APKs through
		// it, so pak/APK-resident content loads in place with no fopen. Unset
		// until here means the pre-boot path (and any host that never boots the
		// renderer) falls back to fopen - the honest default.
		this->mResourceReader = uptr<ResourceReader>(new RenderResourceReader());
		ResourceAccess::setReader(this->mResourceReader.get());

		// the historical Engine default ambient
		this->mRenderWorld->setAmbientLight(Color(0.2f, 0.2f, 0.2f));

		// the shadow quality knob as a live cvar: reachable from the console,
		// Lua cvar.set, the debug protocol / MCP set_cvar, and persistable
		// through the project manifest ("cvar.r.shadowQuality"). registerCVar
		// applies a manifest override held before this registration and fires
		// the hook once with the boot value; setShadowQuality no-ops on an
		// unchanged knob, so a plain boot stays silent on every flavor (the
		// classic backend logs its one honest "not supported" line only when
		// the knob actually moves).
		CVarManager::getSingleton().registerCVar("r.shadowQuality",
			CVarType::String,
			ShadowPreset::qualityName(this->mRenderWorld->getShadowQuality()),
			CVAR_PERSIST,
			"dynamic shadow quality: off, low, medium or high (PSSM split "
			"count + shadow map size, see core_util/ShadowPreset.h)",
			[](CVar const & cvar)
			{
				RenderSystem* renderSystem = RenderSystem::get();
				if (!renderSystem)
				{
					return;	// a set after render teardown changes nothing
				}
				ShadowPreset::Quality quality;
				if (ShadowPreset::parseQuality(cvar.value, quality))
				{
					renderSystem->getWorld()->setShadowQuality(quality);
				}
				else
				{
					oDebugWarning(false, "AppHost: r.shadowQuality unknown value '"
						<< cvar.value.c_str()
						<< "' (off/low/medium/high) - keeping the current quality");
				}
			});

		// the image-lighting quality knob as a live cvar, the r.shadowQuality
		// mold: the tier caps the prefiltered environment chain's resolution
		// (core_util/IblPreset.h) and re-arms live; the knob alone renders
		// nothing - image lighting also needs the runtime opt-in
		// (engine:setImageLighting) and a skybox cubemap, so a plain boot
		// stays byte-identical on every flavor.
		CVarManager::getSingleton().registerCVar("r.iblQuality",
			CVarType::String,
			IblPreset::qualityName(this->mRenderWorld->getIblQuality()),
			CVAR_PERSIST,
			"image-based lighting quality: off, low, medium or high (the "
			"prefiltered environment chain's resolution cap, see "
			"core_util/IblPreset.h)",
			[](CVar const & cvar)
			{
				RenderSystem* renderSystem = RenderSystem::get();
				if (!renderSystem)
				{
					return;	// a set after render teardown changes nothing
				}
				IblPreset::Quality quality;
				if (IblPreset::parseQuality(cvar.value, quality))
				{
					renderSystem->getWorld()->setIblQuality(quality);
				}
				else
				{
					oDebugWarning(false, "AppHost: r.iblQuality unknown value '"
						<< cvar.value.c_str()
						<< "' (off/low/medium/high) - keeping the current quality");
				}
			});

		// the LDR bloom quality knob as a live cvar (the r.shadowQuality mold):
		// off/low/medium/high sets the blur budget (downsample + pass count, see
		// core_util/BloomPreset.h). Orthogonal to the per-scene setBloom opt-in -
		// bloom renders only while this knob is on AND a scene enabled it; a live
		// flip re-arms the pass. Persisted through the manifest
		// ("cvar.r.bloomQuality"). setBloomQuality no-ops on an unchanged knob so
		// a plain boot stays silent.
		CVarManager::getSingleton().registerCVar("r.bloomQuality",
			CVarType::String,
			BloomPreset::qualityName(this->mRenderWorld->getBloomQuality()),
			CVAR_PERSIST,
			"LDR bloom quality: off, low, medium or high (bright-pass glow blur "
			"budget, see core_util/BloomPreset.h); a scene opts in via "
			"engine:setBloom",
			[](CVar const & cvar)
			{
				RenderSystem* renderSystem = RenderSystem::get();
				if (!renderSystem)
				{
					return;	// a set after render teardown changes nothing
				}
				BloomPreset::Quality quality;
				if (BloomPreset::parseQuality(cvar.value, quality))
				{
					renderSystem->getWorld()->setBloomQuality(quality);
				}
				else
				{
					oDebugWarning(false, "AppHost: r.bloomQuality unknown value '"
						<< cvar.value.c_str()
						<< "' (off/low/medium/high) - keeping the current quality");
				}
			});

		// the mobility-flag apply gate (@see TransformComponent::setStaticFlag):
		// ON by default - static-flagged objects take the backend's immobile
		// fast path (SCENE_STATIC / StaticGeometry). OFF leaves every object
		// on the default dynamic path: the editor's edit mode boots that way,
		// and the render toggle tests prove the pixels match either way. Read
		// at flag-APPLY time, so a live change affects newly loaded scenes.
		CVarManager::getSingleton().registerCVar("r.staticScene",
			CVarType::Bool, "1", CVAR_PERSIST,
			"apply the static mobility flag to the renderer (off = every "
			"object renders on the dynamic path; flags still round-trip)");
		// sprite-run batching (@see SpriteBatcher): ON by default in every
		// runtime that creates the batcher; OFF is the live escape hatch -
		// runs release and every sprite draws individually again (pixels
		// identical either way, the render-toggle test compares them)
		CVarManager::getSingleton().registerCVar("r.spriteBatching",
			CVarType::Bool, "1", CVAR_PERSIST,
			"merge contiguous same-material sprite runs into one draw each "
			"(off = every sprite costs its own draw call)");
		// the projected-decal budget (@see RenderWorld::setMaxDecals): the hard
		// cap on concurrently VISIBLE surface marks - the mobile-budget knob.
		// When more decals exist the OLDEST are hidden; 0 hides every decal
		// (byte-identical to a scene with none). Live-tunable + persisted. The
		// hook pushes it onto the world so a set reconfigures mid-play.
		CVarManager::getSingleton().registerCVar("r.maxDecals",
			CVarType::Int,
			std::to_string(this->mRenderWorld->getMaxDecals()),
			CVAR_PERSIST,
			"the maximum concurrently visible projected decals (surface marks); "
			"the oldest are hidden past it, 0 hides every decal",
			[](CVar const & cvar)
			{
				RenderSystem* renderSystem = RenderSystem::get();
				if (!renderSystem)
				{
					return;	// a set after render teardown changes nothing
				}
				const int value = cvar.asInt();
				renderSystem->getWorld()->setMaxDecals(
					value > 0 ? static_cast<unsigned int>(value) : 0u);
			});

		if (this->mConfig.createWindowCamera)
		{
			// the window camera on a facade rig (the
			// createDefaultCameraAndViewport successor); the fixed yaw axis
			// keeps per-frame lookAt calls roll-free
			this->mWindowCamera = this->mRenderWorld->createCamera("app.camera");
			this->mCameraNode = this->mRenderWorld->createNode("app.cameraNode");
			this->mCameraNode->setFixedYawAxis(true);
			this->mWindowCamera->attachTo(this->mCameraNode);
			this->mRenderSystem->showCameraOnWindow(this->mWindowCamera);
		}
		if (this->mConfig.createCubeMesh)
		{
			// the shared procedural cube mesh + its unlit "VertexColour"
			// material, so scenes referencing them by name load in every host
			this->mRenderWorld->createVertexColourCubeMesh();
		}

		// GameObject/component bridge: the module init registers the
		// component factories, so it must precede the GameObjectManager
		init_module_orkige_engine();
		this->mGameObjectManager =
			uptr<GameObjectManager>(new GameObjectManager());
		return true;
	}
	//---------------------------------------------------------
	bool AppHost::boot(AppHostConfig const & config,
		std::function<void()> const & registerResources)
	{
		return this->initialise(config) && this->setupEngine(registerResources);
	}
	//---------------------------------------------------------
	float AppHost::clampFrameDelta(float measuredDelta, bool automatedRun)
	{
		return std::clamp(measuredDelta,
			automatedRun ? 1.0f / 60.0f : 0.0001f, 0.1f);
	}
	//---------------------------------------------------------
	bool QuitOnEscape::onKeyPressed(Event const & event)
	{
		if (event.getDataPtr<KeyEventData>()->key == KeyEventData::KC_ESCAPE)
		{
			if (this->intercept && this->intercept())
			{
				return false;	// consumed (e.g. the editor cleared a selection)
			}
			this->quitRequested = true;
		}
		return false;
	}
	//---------------------------------------------------------
	void pushKeyEvent(SDL_Scancode scancode, SDL_Keycode key, bool down)
	{
		SDL_Event event{};
		event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
		event.key.scancode = scancode;
		event.key.key = key;
		event.key.down = down;
		SDL_PushEvent(&event);
	}
	//---------------------------------------------------------
	void pushMouseMove(float x, float y)
	{
		SDL_Event event{};
		event.type = SDL_EVENT_MOUSE_MOTION;
		event.motion.x = x;
		event.motion.y = y;
		SDL_PushEvent(&event);
	}
	//---------------------------------------------------------
	void pushMouseButton(float x, float y, bool down)
	{
		SDL_Event event{};
		event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
			: SDL_EVENT_MOUSE_BUTTON_UP;
		event.button.button = SDL_BUTTON_LEFT;
		event.button.down = down;
		event.button.x = x;
		event.button.y = y;
		SDL_PushEvent(&event);
	}
	//---------------------------------------------------------
	void applyUnlitFixToLoadedModels(GameObjectManager& gameObjectManager)
	{
		for (auto const& [id, gameObject] : gameObjectManager.getGameObjects())
		{
			if (!gameObject->hasComponent<ModelComponent>())
			{
				continue;
			}
			ModelComponent* model =
				gameObject->getComponentPtr<ModelComponent>();
			// ONLY material-less models get the legacy unlit vertex-colour
			// look (the historical flat-shaded sample meshes). A model with a
			// recorded .omat reference is LIT content - swapping its sub-items
			// onto the unlit datablock here would silently discard the applied
			// PBS material (every model in a scene load runs through this).
			if (!model->getMaterialFileName().empty())
			{
				continue;
			}
			optr<MeshInstance> mesh = model->getMeshInstance();
			if (mesh)
			{
				mesh->setVertexColourUnlit();
			}
		}
	}
}
