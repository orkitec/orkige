// hello_orkige - feature demo.
// SDL3 owns the window and event loop; Orkige::Engine boots the renderer into
// it via the externalWindowHandle path, everything AFTER boot goes through the
// engine_render facade (A1, Docs/render-abstraction.md). Scene: a spinning
// vertex-colored cube (the shared procedural cube mesh), which exercises the
// whole RTSS shader pipeline without needing any asset files.
#include <SDL3/SDL.h>
#include <engine_graphic/Engine.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderWorld.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/MeshInstance.h>
#include <engine_util/PlatformWindow.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/LightComponent.h>
#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/SpriteAnimationComponent.h>
#include <engine_gocomponent/ParticleComponent.h>
#include <engine_gocomponent/VectorShapeComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_physic/PhysicsWorld.h>
// gui is flavor-neutral - the
// ORKIGE_DEMO_GUI smoke test below runs on both render flavors
#include <engine_gui/GuiManager.h>
#include <engine_gui/GuiFactory.h>
#include <engine_gui/GuiToggleGroup.h>
#include <engine_gui/GuiSlider.h>
#include <engine_gui/GuiProgressBar.h>
#include <engine_gui/GuiTextEntry.h>
#include <engine_gui/GuiScrollView.h>
#include <engine_gui/GuiTextbox.h>
#include <engine_gui/GuiTextEdit.h>
#include <engine_gui/GuiLabel.h>
#include <engine_gui/GuiButton.h>
#include <engine_gui/UiAtlas.h>
#include <core_util/StringTable.h>
#include <engine_gocomponent/ScriptComponent.h>
#include <core_tween/TweenManager.h>
#include <core_util/SafeArea.h>
#include <engine_input/InputManager.h>
#include <engine_runtime/AppHost.h>
#include <engine_sound/SoundManager.h>
#include <engine_util/StringUtil.h>
#include <core_game/GameObjectManager.h>
#include <core_util/StringUtil.h>
#include <core_script/ScriptRuntime.h>
#include <core_script/ScriptEventBus.h>

#include <exception>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using Orkige::optr;
using Orkige::woptr;

// C++-side receiver for the event the Lua smoke-test script triggers -
// proves the Lua -> GlobalEventManager -> C++ listener path end-to-end
struct LuaEventProbe
{
	bool received = false;
	bool onLuaEvent(Orkige::Event const& event)
	{
		received = event.getData() &&
			event.getData()->getObjectID() == "lua_payload";
		return false;
	}
};

int main(int, char**)
{
	// automation-hook flags, read before boot: ORKIGE_DEMO_FRAMES caps the
	// run (0/unset = run until the window is closed) and marks it automated
	// (vsync-free, simulated-time frame pacing); the ORKIGE_DEMO_* feature
	// flags below pick which resource locations the run needs
	unsigned long frameLimit = 0;
	if (const char* demoFrames = std::getenv("ORKIGE_DEMO_FRAMES"))
	{
		frameLimit = std::strtoul(demoFrames, nullptr, 10);
	}
	const bool automatedRun = frameLimit != 0;
	// ORKIGE_DEMO_MESH=1: also load the generated glTF test asset
	// (samples/hello_orkige/media/test_mesh.glb, built by
	// Util/make_test_mesh.py) through the statically linked Codec_Assimp
	// plugin. Unconditional runs stay asset-free.
	const bool demoMesh = (std::getenv("ORKIGE_DEMO_MESH") != nullptr);
	// ORKIGE_DEMO_SPRITEANIM=1: the flipbook selfcheck (below) needs a
	// texture for its SpriteComponent - register the committed sample
	// texture dir so loadSprite resolves one (any texture works; the
	// check reads UV rects, not pixels)
	const bool demoSpriteAnim =
		(std::getenv("ORKIGE_DEMO_SPRITEANIM") != nullptr);
	// ORKIGE_DEMO_SPRITEATLAS=1: the sprite-atlas + sampler selfcheck
	// (below) needs the generated atlas (.oatlas + .png) on a resource
	// location so loadSpriteFromAtlas resolves it
	const bool demoSpriteAtlas =
		(std::getenv("ORKIGE_DEMO_SPRITEATLAS") != nullptr);
	// ORKIGE_DEMO_PARTICLES=1: the 2D particle-system selfcheck
	// (below) needs a texture for its ParticleComponent SpriteBatch -
	// register the committed sample texture dir (any texture works; the
	// check reads the frame-stats triangle count, not pixels)
	const bool demoParticles =
		(std::getenv("ORKIGE_DEMO_PARTICLES") != nullptr);
	// ORKIGE_DEMO_MUSIC=1: the streamed-music selfcheck (below) needs the
	// committed loopable OGG on a resource location so playMusic resolves
	// it (music_loop.ogg lives in the sample media dir)
	const bool demoMusic =
		(std::getenv("ORKIGE_DEMO_MUSIC") != nullptr);
	// ORKIGE_DEMO_VECTORSHAPE=1: the flat-colour vector-shape selfcheck
	// (below) loads the committed demo_blob.oshape from the sample media dir
	const bool demoVectorShape =
		(std::getenv("ORKIGE_DEMO_VECTORSHAPE") != nullptr);
	// ORKIGE_DEMO_LIGHT=1: the LightComponent end-to-end selfcheck (below)
	const bool demoLight = (std::getenv("ORKIGE_DEMO_LIGHT") != nullptr);

	// the shared boot spine (engine_runtime/AppHost.h): SDL window, engine
	// singletons, the per-flavor Engine boot, the window-camera rig and the
	// GameObject world - the demo owns only its resource locations and loop.
	// ORKIGE_DEMO_MEDIA_DIR is a demo-only compile definition pointing into
	// the vcpkg-installed OGRE media (see CMakeLists.txt); no resources.cfg /
	// plugins.cfg / ogre.cfg - the demo wires its locations manually.
	Orkige::AppHost host;
	Orkige::AppHostConfig hostConfig;
	hostConfig.windowTitle = "hello orkige";
	hostConfig.automatedRun = automatedRun;
	hostConfig.engineLogFile = "hello_orkige.log";
	hostConfig.classicMediaDir = ORKIGE_DEMO_MEDIA_DIR;
	if (!host.boot(hostConfig, [&]()
		{
			Orkige::RenderSystem* render = host.getRenderSystem();
			if (demoMesh)
			{
				render->addResourceLocation(ORKIGE_DEMO_ASSET_DIR);
			}
			if (demoSpriteAnim)
			{
				render->addResourceLocation(ORKIGE_SPRITE_TEXTURE_DIR);
			}
			if (demoSpriteAtlas)
			{
				render->addResourceLocation(ORKIGE_SPRITE_ATLAS_DIR);
			}
			if (demoParticles)
			{
				render->addResourceLocation(ORKIGE_SPRITE_TEXTURE_DIR);
			}
			if (demoMusic || demoVectorShape)
			{
				render->addResourceLocation(ORKIGE_DEMO_ASSET_DIR);
			}
		}))
	{
		return 1;
	}
	{
		Orkige::RenderSystem* render = host.getRenderSystem();
		Orkige::RenderWorld* world = host.getRenderWorld();
		optr<Orkige::RenderCamera> camera = host.getWindowCamera();
		optr<Orkige::RenderNode> cameraNode = host.getCameraNode();
		Orkige::ScriptRuntime& scriptRuntime = host.getScriptRuntime();
		// the historical Engine default viewport colour
		render->setWindowBackgroundColour(Orkige::Color(0.0f, 0.0f, 1.0f));

		// input pipeline: the poll loop below feeds every SDL event into the
		// InputManager, which triggers Orkige input events globally
		Orkige::InputManager inputManager;
		Orkige::QuitOnEscape quitOnEscape;
		optr<Orkige::EventListener> escapeListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyPressedEvent,
				&Orkige::QuitOnEscape::onKeyPressed, &quitOnEscape);

		// ORKIGE_DEMO_SOUND=1: play a synthesized beep through the engine_sound
		// OpenAL Soft path at demo start; normal runs stay silent (the manager
		// is only initialized when the env var is set).
		Orkige::SoundManager soundManager;
		if (std::getenv("ORKIGE_DEMO_SOUND"))
		{
			if (!soundManager.init())
			{
				SDL_Log("hello_orkige: FAILED - SoundManager::init "
					"(OpenAL device/context) failed");
				return 1;
			}
			// 0.2s of 440Hz 16-bit mono PCM synthesized in code - the raw-PCM
			// path (SoundManager::createSoundFromPCM) needs no asset file
			const int sampleRate = 44100;
			const int sampleCount = sampleRate / 5;
			std::vector<int16_t> samples(sampleCount);
			for (int i = 0; i < sampleCount; ++i)
			{
				const float t = static_cast<float>(i) / sampleRate;
				const float fadeOut =
					1.0f - static_cast<float>(i) / sampleCount;
				samples[i] = static_cast<int16_t>(30000.0f * fadeOut *
					std::sin(2.0f * 3.14159265f * 440.0f * t));
			}
			soundManager.createSoundFromPCM("beep", samples.data(),
				static_cast<int>(samples.size() * sizeof(int16_t)),
				1, 16, sampleRate);
			if (soundManager.playSound("beep"))
			{
				SDL_Log("hello_orkige: beep playing (440Hz via OpenAL Soft)");
			}
			else
			{
				SDL_Log("hello_orkige: FAILED - beep did not start");
				return 1;
			}

			// SFX variation: a source with +/-20% pitch variation lands each
			// play within the range, moves off 1.0 across a few plays, and the
			// varied pitch actually reaches the AL source (queryPitch).
			Orkige::SoundSourcePtr varied = soundManager.createSoundFromPCM(
				"varied", samples.data(),
				static_cast<int>(samples.size() * sizeof(int16_t)),
				1, 16, sampleRate);
			if (varied)
			{
				varied->setPitchVariation(0.2f);
				bool anyOffOne = false;
				for (int i = 0; i < 8; ++i)
				{
					varied->stop();		// clear AL_PLAYING so play() pushes this pitch
					varied->play();
					const float p = varied->getCurrentPitch();
					if (p < 0.8f - 1e-3f || p > 1.2f + 1e-3f)
					{
						SDL_Log("hello_orkige: FAILED - varied pitch %f outside "
							"the +/-20%% range", p);
						return 1;
					}
					if (std::fabs(p - 1.0f) > 1e-3f)
					{
						anyOffOne = true;
					}
					if (varied->isInitialized() &&
						std::fabs(varied->queryPitch() - p) > 1e-3f)
					{
						SDL_Log("hello_orkige: FAILED - varied pitch %f did not "
							"reach the AL source (%f)", p, varied->queryPitch());
						return 1;
					}
				}
				if (!anyOffOne)
				{
					SDL_Log("hello_orkige: FAILED - pitch variation never moved "
						"off 1.0 across 8 plays");
					return 1;
				}
				SDL_Log("hello_orkige: sfx pitch variation reaches the source "
					"(+/-20%%, varied across plays)");
			}
		}

		// ORKIGE_DEMO_MUSIC=1: stream a committed loopable OGG through the
		// queued-buffer MusicStream path. Device-tolerant: when OpenAL opens a
		// device the selfcheck asserts the track plays and its playhead ADVANCES
		// across frames (proving the ring refilled); headless (no device) it
		// asserts the honest no-op path instead (the track registers but stays
		// silent and the queries never crash).
		bool musicAudioUp = false;
		if (demoMusic)
		{
			musicAudioUp = soundManager.init();
			const bool played =
				soundManager.playMusic("bgm", "music_loop.ogg", true);
			if (musicAudioUp)
			{
				if (!played || !soundManager.isMusicPlaying("bgm"))
				{
					SDL_Log("hello_orkige: FAILED - music did not start");
					return 1;
				}
				SDL_Log("hello_orkige: music streaming music_loop.ogg "
					"(OGG Vorbis, queued buffers)");
			}
			else
			{
				// no audio device (headless CI): the honest no-op path
				if (soundManager.isMusicPlaying("bgm"))
				{
					SDL_Log("hello_orkige: FAILED - music reports playing "
						"with no audio device");
					return 1;
				}
				SDL_Log("hello_orkige: no audio device - music honest no-op "
					"path");
			}
		}

		// the demo geometry: a demo-sized sibling of the shared procedural
		// cube (facade service; also creates the unlit "VertexColour" material
		// the RTSS reads vertex colours through). The 12-triangle count per
		// cube feeds the frame-stats self-check below.
		world->createVertexColourCubeMesh("HelloCube.mesh", 1.0f);
		optr<Orkige::RenderNode> cubeNode = world->createNode("cubeNode");
		optr<Orkige::MeshInstance> cube =
			world->createMeshInstance("HelloCube.mesh");
		cube->attachTo(cubeNode);

		// --- GameObject component bridge: a second, smaller cube that is not
		// placed through raw Ogre scene calls but through a GameObject with a
		// TransformComponent (engine_gocomponent), orbiting the main cube.
		// The component factories registered during the host boot;
		// GameObjectManager is the singleton owning the objects.
		Orkige::GameObjectManager& gameObjectManager =
			host.getGameObjectManager();
		optr<Orkige::GameObject> orbiter =
			gameObjectManager.createGameObject("orbiter").lock();
		if (!orbiter || !orbiter->addComponent<Orkige::TransformComponent>())
		{
			SDL_Log("hello_orkige: FAILED - GameObject/TransformComponent "
				"creation failed");
			return 1;
		}
		Orkige::TransformComponent* orbiterTransform =
			orbiter->getComponentPtr<Orkige::TransformComponent>();

		// a smaller cube instance of the same mesh, attached to the
		// TransformComponent's facade node through a scaled child (the
		// demo content sits on facade types - no more reaching the
		// backend node by its deterministic name)
		optr<Orkige::RenderNode> smallCubeNode =
			orbiterTransform->createChildNode("orbiterVisual");
		smallCubeNode->setScale(Orkige::Vec3(0.35f, 0.35f, 0.35f));
		optr<Orkige::MeshInstance> smallCube =
			world->createMeshInstance("HelloCube.mesh");
		smallCube->attachTo(smallCubeNode);

		// --- ORKIGE_DEMO_SPRITEANIM=1: the sprite flipbook selfcheck. A
		// GameObject carrying a SpriteAnimationComponent on a 4x1 grid with one
		// looping 4-frame clip. Adding the flipbook auto-adds its
		// SpriteComponent dependency (which in turn brings a TransformComponent)
		// - the same AnimationComponent<->ModelComponent idiom. The frame loop
		// ticks the GameObjectManager with a fixed dt and records the sibling
		// sprite's UV rect; the frame-120 self-check asserts the rect advances
		// across frames AND wraps on the loop - reading component state, no
		// pixel readback, so it runs identically on both render flavors.
		Orkige::SpriteComponent* animSprite = nullptr;
		Orkige::SpriteAnimationComponent* spriteAnim = nullptr;
		std::vector<float> spriteAnimU0Log;	// per-tick UV u0 (column probe)
		if (demoSpriteAnim)
		{
			optr<Orkige::GameObject> flip =
				gameObjectManager.createGameObject("flipbook").lock();
			if (!flip ||
				!flip->addComponent<Orkige::SpriteAnimationComponent>())
			{
				SDL_Log("hello_orkige: FAILED - SpriteAnimationComponent "
					"creation failed");
				return 1;
			}
			animSprite = flip->getComponentPtr<Orkige::SpriteComponent>();
			spriteAnim =
				flip->getComponentPtr<Orkige::SpriteAnimationComponent>();
			if (!animSprite || !spriteAnim)
			{
				SDL_Log("hello_orkige: FAILED - flipbook siblings missing");
				return 1;
			}
			// any texture works (the check reads UV, not pixels); a real one
			// also exercises the frameToUVRect half-texel inset via getTextureSize
			animSprite->loadSprite("player.png");
			spriteAnim->setGrid(4, 1);
			spriteAnim->addClip("spin", 0, 4, 8.0f, true);	// 4 frames @ 8 fps
			spriteAnim->setDefaultClip("spin");
			spriteAnim->play("spin");						// start on frame 0
			SDL_Log("hello_orkige: flipbook up - 4x1 grid, clip 'spin' playing "
				"(sprite loaded=%d)", static_cast<int>(animSprite->hasSprite()));
		}

		// --- ORKIGE_DEMO_SPRITEATLAS=1: the sprite-atlas + sampler selfcheck.
		// A SpriteComponent loads a named region of the generated
		// demo_sprite_atlas (packed by Util/make_sprite_atlas.py at build
		// time); the check asserts loadSpriteFromAtlas positioned the UV rect
		// EXACTLY where the shared pixelRectToUV primitive puts the region's
		// pixel rect (parsed independently from the .oatlas), that distinct
		// regions land on distinct rects, a bogus region is refused, and the
		// live sampler setters apply without a crash. Reads component state -
		// runs identically on both render flavors.
		if (demoSpriteAtlas)
		{
			optr<Orkige::GameObject> atlasObject =
				gameObjectManager.createGameObject("atlasSprite").lock();
			if (!atlasObject ||
				!atlasObject->addComponent<Orkige::SpriteComponent>())
			{
				SDL_Log("hello_orkige: FAILED - atlas sprite creation failed");
				return 1;
			}
			Orkige::SpriteComponent* atlasSprite =
				atlasObject->getComponentPtr<Orkige::SpriteComponent>();
			if (!atlasSprite ||
				!atlasSprite->loadSpriteFromAtlas("demo_sprite_atlas.oatlas",
					"crate"))
			{
				SDL_Log("hello_orkige: FAILED - loadSpriteFromAtlas('crate')");
				return 1;
			}
			// independently parse the region rect from the .oatlas and confirm
			// the UV rect matches pixelRectToUV over it
			std::string atlasText;
			if (!render->readResourceText("demo_sprite_atlas.oatlas", atlasText))
			{
				SDL_Log("hello_orkige: FAILED - .oatlas not readable");
				return 1;
			}
			float rx = 0.0f, ry = 0.0f, rw = 0.0f, rh = 0.0f;
			bool found = false;
			std::istringstream lineStream(atlasText);
			std::string line;
			while (std::getline(lineStream, line))
			{
				std::istringstream tokens(line);
				std::string key;
				if (!(tokens >> key) || key.empty() || key[0] == '#' ||
					key[0] == '[')
				{
					continue;
				}
				if (key == "crate" && (tokens >> rx >> ry >> rw >> rh))
				{
					found = true;
				}
			}
			float atlasW = 0.0f, atlasH = 0.0f;
			atlasSprite->getTextureSize(atlasW, atlasH);
			float eu0, ev0, eu1, ev1;
			Orkige::SpriteComponent::pixelRectToUV(rx, ry, rw, rh,
				atlasW, atlasH, eu0, ev0, eu1, ev1);
			float u0, v0, u1, v1;
			atlasSprite->getUVRect(u0, v0, u1, v1);
			const bool uvMatches = found &&
				std::abs(u0 - eu0) < 1e-5f && std::abs(v0 - ev0) < 1e-5f &&
				std::abs(u1 - eu1) < 1e-5f && std::abs(v1 - ev1) < 1e-5f;
			// the region must be a strict SUB-rect of the atlas, not the whole
			// texture (proves the atlas > the sprite and the region was applied)
			const bool isSubRect = (u1 - u0) < 0.999f && (v1 - v0) < 0.999f &&
				u0 >= 0.0f && v0 >= 0.0f && u1 <= 1.0f && v1 <= 1.0f;

			// a different region must land on a different rect
			atlasSprite->loadSpriteFromAtlas("demo_sprite_atlas.oatlas",
				"player");
			float pu0, pv0, pu1, pv1;
			atlasSprite->getUVRect(pu0, pv0, pu1, pv1);
			const bool regionsDiffer =
				std::abs(pu0 - u0) > 1e-5f || std::abs(pv0 - v0) > 1e-5f ||
				std::abs(pu1 - u1) > 1e-5f || std::abs(pv1 - v1) > 1e-5f;

			// a bogus region is refused (and leaves the sprite intact)
			const bool bogusRefused = !atlasSprite->loadSpriteFromAtlas(
				"demo_sprite_atlas.oatlas", "no_such_region");

			// the live sampler setters apply without a crash and are readable
			atlasSprite->setFilter(Orkige::SpriteQuad::FILTER_POINT);
			atlasSprite->setAddressing(Orkige::SpriteQuad::ADDRESS_WRAP);
			const bool samplerApplied =
				atlasSprite->getFilter() == Orkige::SpriteQuad::FILTER_POINT &&
				atlasSprite->getAddressing() ==
					Orkige::SpriteQuad::ADDRESS_WRAP;

			SDL_Log("hello_orkige: atlas uvMatches=%d subRect=%d differ=%d "
				"bogusRefused=%d sampler=%d (atlas %.0fx%.0f region "
				"%.0f,%.0f,%.0f,%.0f)", static_cast<int>(uvMatches),
				static_cast<int>(isSubRect), static_cast<int>(regionsDiffer),
				static_cast<int>(bogusRefused), static_cast<int>(samplerApplied),
				atlasW, atlasH, rx, ry, rw, rh);
			if (!(uvMatches && isSubRect && regionsDiffer && bogusRefused &&
				samplerApplied))
			{
				SDL_Log("hello_orkige: FAILED - sprite atlas/sampler selfcheck");
				return 1;
			}
			SDL_Log("hello_orkige: sprite atlas + sampler selfcheck passed");
		}

		// --- ORKIGE_DEMO_PARTICLES=1: the 2D particle-system selfcheck.
		// A GameObject carrying a ParticleComponent (auto-adds its
		// TransformComponent dependency); the emitter is burst-only (additive
		// glow) so the frame-stats triangle count is a clean before/after probe.
		// The loop fires ONE burst at frame 20 and the frame checks assert the
		// triangle count rose by ~maxParticles*2 while the burst is alive and
		// decayed back once every particle passed its lifetime. The batch is a
		// SINGLE draw call; this reads stats, not pixels, so it runs identically
		// on both render flavors.
		Orkige::ParticleComponent* particles = nullptr;
		const int particleMax = 64;
		const float particleLifetime = 0.5f;	// 10 ticks at the fixed 0.05 dt
		std::size_t particleBaselineTriangles = 0;	// stats just before the burst
		if (demoParticles)
		{
			optr<Orkige::GameObject> emitter =
				gameObjectManager.createGameObject("sparks").lock();
			if (!emitter ||
				!emitter->addComponent<Orkige::ParticleComponent>())
			{
				SDL_Log("hello_orkige: FAILED - ParticleComponent creation "
					"failed");
				return 1;
			}
			particles = emitter->getComponentPtr<Orkige::ParticleComponent>();
			if (!particles)
			{
				SDL_Log("hello_orkige: FAILED - ParticleComponent missing");
				return 1;
			}
			Orkige::ParticleSim::EmitterParams p = particles->params();
			p.emissionRate = 0.0f;			// burst-only
			p.burstCount = particleMax;
			p.maxParticles = particleMax;
			p.lifetimeMin = particleLifetime;
			p.lifetimeMax = particleLifetime;
			p.gravity = Orkige::Vec2(0.0f, -1.0f);
			p.startSize = 0.3f;
			p.endSize = 0.0f;
			p.blendMode = Orkige::ParticleSim::BLEND_ADDITIVE;
			p.zOrder = 5;
			particles->params() = p;
			particles->setEmitOnStart(false);	// only the explicit burst emits
			particles->setTexture("player.png");	// creates the batch
			SDL_Log("hello_orkige: particle emitter up (max=%d, additive burst)",
				particleMax);
		}

		// --- ORKIGE_DEMO_VECTORSHAPE=1: the flat-colour vector-shape selfcheck.
		// A GameObject carrying a VectorShapeComponent (auto-adds its
		// TransformComponent dependency) loads demo_blob.oshape - a concave blob
		// silhouette + an accent region - which is tessellated (earcut) with a
		// baked alpha-feather edge into a facade VectorMesh. The check asserts
		// the mesh tessellated (triangle count > 0) at load, then toggles its
		// visibility across frames and reads the frame-stats triangle delta to
		// prove it actually renders. Reads stats, not pixels - runs identically
		// on both render flavors.
		Orkige::VectorShapeComponent* vectorShape = nullptr;
		std::size_t vectorShapeHiddenTriangles = 0;
		if (demoVectorShape)
		{
			optr<Orkige::GameObject> shapeObject =
				gameObjectManager.createGameObject("blob").lock();
			if (!shapeObject ||
				!shapeObject->addComponent<Orkige::VectorShapeComponent>())
			{
				SDL_Log("hello_orkige: FAILED - VectorShapeComponent creation "
					"failed");
				return 1;
			}
			vectorShape =
				shapeObject->getComponentPtr<Orkige::VectorShapeComponent>();
			if (!vectorShape)
			{
				SDL_Log("hello_orkige: FAILED - VectorShapeComponent missing");
				return 1;
			}
			vectorShape->setZOrder(6);
			vectorShape->setTint(1.0f, 1.0f, 1.0f, 1.0f);
			vectorShape->loadShape("demo_blob.oshape");
			if (vectorShape->getTriangleCount() == 0)
			{
				SDL_Log("hello_orkige: FAILED - vector shape did not "
					"tessellate (0 triangles)");
				return 1;
			}
			SDL_Log("hello_orkige: vector shape up (%zu triangles from "
				"demo_blob.oshape)", vectorShape->getTriangleCount());
		}

		// ORKIGE_DEMO_MESH=1: a real mesh asset next to the procedural cubes -
		// createMeshInstance("test_mesh.glb") pulls the .glb through
		// Codec_Assimp (registered in Engine.cpp's static-plugin block). The
		// codec sets vertex-colour tracking on the synthesized material because
		// the mesh carries COLOR_0 vertex colours, but it also generates
		// normals (aiProcess_GenNormals) so lighting stays on; under this
		// scene's ambient-only light the colours would drown. Render it unlit,
		// the same treatment the cubes get.
		optr<Orkige::RenderNode> testMeshNode;
		optr<Orkige::MeshInstance> testMesh;
		if (demoMesh)
		{
			testMesh = world->createMeshInstance("test_mesh.glb");
			testMesh->setVertexColourUnlit();
			testMeshNode = world->createNode("testMeshNode");
			testMeshNode->setPosition(Orkige::Vec3(0.0f, 2.5f, 0.0f));
			testMesh->attachTo(testMeshNode);
			SDL_Log("hello_orkige: test_mesh.glb loaded via Codec_Assimp "
				"(%zu sub-meshes)", testMesh->getNumSubMeshes());
		}

		// --- ORKIGE_DEMO_LIGHT=1: the LightComponent end-to-end selfcheck. A
		// GameObject carries a LightComponent, which owns a child scene node and
		// a live facade RenderLight; the checks assert the component built the
		// light, that its reflected parameters applied, and that the light node
		// FOLLOWS the object's TransformComponent. Reads component/facade state,
		// no pixels - identical on both render flavors.
		if (demoLight)
		{
			optr<Orkige::GameObject> lightObject =
				gameObjectManager.createGameObject("sun").lock();
			if (!lightObject ||
				!lightObject->addComponent<Orkige::LightComponent>())
			{
				SDL_Log("hello_orkige: FAILED - LightComponent creation failed");
				return 1;
			}
			// LightComponent auto-added its TransformComponent dependency
			Orkige::TransformComponent* lightTransform =
				lightObject->getComponentPtr<Orkige::TransformComponent>();
			Orkige::LightComponent* light =
				lightObject->getComponentPtr<Orkige::LightComponent>();
			if (!lightTransform || !light)
			{
				SDL_Log("hello_orkige: FAILED - LightComponent siblings missing");
				return 1;
			}
			// onAdd built a live facade light (a render world is up)
			if (!light->hasLight())
			{
				SDL_Log("hello_orkige: FAILED - LightComponent built no light");
				return 1;
			}
			// reflected parameters apply live onto the facade light
			light->setType(Orkige::LightComponent::LT_SPOT);
			light->setColour(0.9f, 0.8f, 0.2f);
			light->setIntensity(1.5f);
			light->setRange(30.0f);
			light->setInnerAngle(20.0f);
			light->setOuterAngle(45.0f);
			light->setCastsShadows(true);
			const bool stateApplied =
				light->getType() == Orkige::LightComponent::LT_SPOT &&
				std::fabs(light->getColour().g - 0.8f) < 1e-4f &&
				std::fabs(light->getIntensity() - 1.5f) < 1e-4f &&
				std::fabs(light->getRange() - 30.0f) < 1e-4f &&
				std::fabs(light->getOuterAngle() - 45.0f) < 1e-4f &&
				light->getCastsShadows();
			if (!stateApplied)
			{
				SDL_Log("hello_orkige: FAILED - LightComponent params did not apply");
				return 1;
			}
			// the light follows the object's transform (its node is a child of
			// the TransformComponent node)
			lightTransform->setPosition(Orkige::Vec3(4.0f, 5.0f, -2.0f));
			const Orkige::Vec3 lightWorld = light->getNode()->getWorldPosition();
			if ((lightWorld - Orkige::Vec3(4.0f, 5.0f, -2.0f)).length() > 1e-3f)
			{
				SDL_Log("hello_orkige: FAILED - light does not follow the "
					"transform node");
				return 1;
			}
			// a dim world with one bright dynamic light (the scene actually lit
			// by the component, not just the ambient minimum)
			world->setAmbientLight(Orkige::Color(0.05f, 0.05f, 0.05f));
			SDL_Log("hello_orkige: LightComponent up - live facade light, params "
				"applied, follows the transform node");
		}

		cameraNode->setPosition(Orkige::Vec3(0.0f, 2.0f, 6.0f));
		cameraNode->lookAt(Orkige::Vec3::ZERO, Orkige::RenderNode::TS_WORLD);

		// --- ORKIGE_DEMO_PHYSICS=1: Jolt dynamics through the engine_physic /
		// engine_gocomponent bridge. A static floor body, a pile of dynamic
		// cubes dropped from height, and two plane-locked cubes
		// (setPlanarMode: translation locked to X/Y, rotation to Z) shoved
		// sideways with an impulse. The world is stepped from this app loop
		// with the measured frame dt (engine-loop integration via
		// FrameStartedEvent is a TODO); self-checks run at frame 120.
		const bool demoPhysics = (std::getenv("ORKIGE_DEMO_PHYSICS") != nullptr);
		Orkige::PhysicsWorld physicsWorld; // inert until init()
		const float floorTopY = -2.0f;
		const float cubeHalf = 0.5f;
		std::vector<Orkige::TransformComponent*> dropTransforms;
		std::vector<Orkige::RigidBodyComponent*> dropBodies;
		std::vector<float> dropStartY;
		std::vector<Orkige::TransformComponent*> planarTransforms;
		std::vector<Orkige::RigidBodyComponent*> planarBodies;
		std::vector<float> planarStartX;
		// the physics boxes' visuals: cube-mesh instances on scaled child
		// nodes of the components' facade nodes - the app holds the handles
		// (RAII; released before the GameObjectManager by declaration order)
		std::vector<optr<Orkige::RenderNode>> physicsVisualNodes;
		std::vector<optr<Orkige::MeshInstance>> physicsVisuals;
		const float planarStartZ = 0.0f;
		if (demoPhysics)
		{
			physicsWorld.init();

			// GameObject with TransformComponent + RigidBodyComponent and a
			// cube-mesh visual attached (the unit cube scaled per axis to the
			// box's half extents); the rigid body is created at the
			// transform's pose on the first component update
			auto makePhysicsBox = [&](std::string const& name,
				Orkige::Vec3 const& pos, Orkige::Vec3 const& halfExtents,
				Orkige::PhysicsWorld::BodyType bodyType, bool planar)
				-> std::pair<Orkige::TransformComponent*,
					Orkige::RigidBodyComponent*>
			{
				optr<Orkige::GameObject> gameObject =
					gameObjectManager.createGameObject(name).lock();
				if (!gameObject ||
					!gameObject->addComponent<Orkige::TransformComponent>() ||
					!gameObject->addComponent<Orkige::RigidBodyComponent>())
				{
					return {nullptr, nullptr};
				}
				Orkige::TransformComponent* transform =
					gameObject->getComponentPtr<Orkige::TransformComponent>();
				Orkige::RigidBodyComponent* rigidBody =
					gameObject->getComponentPtr<Orkige::RigidBodyComponent>();
				transform->setPosition(pos);
				optr<Orkige::RenderNode> visualNode =
					transform->createChildNode(name + "Visual");
				visualNode->setScale(halfExtents);
				optr<Orkige::MeshInstance> visual =
					world->createMeshInstance("HelloCube.mesh");
				visual->attachTo(visualNode);
				physicsVisualNodes.push_back(visualNode);
				physicsVisuals.push_back(visual);
				rigidBody->setBodyType(bodyType);
				rigidBody->setBoxShape(halfExtents);
				rigidBody->setMass(1.0f);
				rigidBody->setPlanarMode(planar);
				return {transform, rigidBody};
			};

			// static floor, top surface at floorTopY
			if (!makePhysicsBox("physicsFloor",
				Orkige::Vec3(0.0f, floorTopY - 0.5f, 0.0f),
				Orkige::Vec3(12.0f, 0.5f, 12.0f),
				Orkige::PhysicsWorld::BT_STATIC, false).first)
			{
				SDL_Log("hello_orkige: FAILED - physics floor creation");
				return 1;
			}

			// dynamic cubes dropped from height (spread so each lands on the
			// floor instead of on a sibling)
			const Orkige::Vec3 dropPositions[4] = {
				{ 1.2f, 3.0f, -0.6f }, { 2.4f, 4.5f, -0.6f },
				{ 1.2f, 6.0f,  0.6f }, { 2.4f, 7.5f,  0.6f },
			};
			for (int i = 0; i < 4; ++i)
			{
				auto [transform, rigidBody] = makePhysicsBox(
					"physicsCube" + std::to_string(i), dropPositions[i],
					Orkige::Vec3(cubeHalf),
					Orkige::PhysicsWorld::BT_DYNAMIC, false);
				if (!transform)
				{
					SDL_Log("hello_orkige: FAILED - physics cube creation");
					return 1;
				}
				dropTransforms.push_back(transform);
				dropBodies.push_back(rigidBody);
				dropStartY.push_back(dropPositions[i].y);
			}

			// two plane-locked cubes (the 2D mode)
			const Orkige::Vec3 planarPositions[2] = {
				{ -3.5f, 2.0f, planarStartZ }, { -5.0f, 3.0f, planarStartZ },
			};
			for (int i = 0; i < 2; ++i)
			{
				auto [transform, rigidBody] = makePhysicsBox(
					"planarCube" + std::to_string(i), planarPositions[i],
					Orkige::Vec3(cubeHalf),
					Orkige::PhysicsWorld::BT_DYNAMIC, true);
				if (!transform)
				{
					SDL_Log("hello_orkige: FAILED - planar cube creation");
					return 1;
				}
				planarTransforms.push_back(transform);
				planarBodies.push_back(rigidBody);
				planarStartX.push_back(planarPositions[i].x);
			}

			// a zero-dt component update creates all bodies at their initial
			// poses (no simulation step runs) so the impulse has a body to
			// push on: 1 kg * 1.5 m/s sideways along +x
			gameObjectManager.update(0.0f);
			for (Orkige::RigidBodyComponent* rigidBody : planarBodies)
			{
				if (!rigidBody->hasBody())
				{
					SDL_Log("hello_orkige: FAILED - rigid body not created");
					return 1;
				}
				rigidBody->applyImpulse(Orkige::Vec3(1.5f, 0.0f, 0.0f));
			}
			SDL_Log("hello_orkige: physics world up - gravity (%.2f, %.2f, "
				"%.2f), %zu dynamic + %zu planar cubes",
				physicsWorld.getGravity().x, physicsWorld.getGravity().y,
				physicsWorld.getGravity().z, dropTransforms.size(),
				planarTransforms.size());

			// pull the camera back so floor and falling cubes stay in view
			cameraNode->setPosition(Orkige::Vec3(0.0f, 3.0f, 20.0f));
			cameraNode->lookAt(Orkige::Vec3(0.0f, -1.0f, 0.0f),
				Orkige::RenderNode::TS_WORLD);
		}

		// --- Lua scripting smoke test (sol2 meta backend): an inline
		// script pulls the Engine singleton, calls registered methods on it,
		// walks into the exposed engine_render facade types
		// (RenderSystem/RenderWorld/RenderCamera/RenderNode - the classic Ogre
		// usertypes are gone), constructs core objects through their
		// registered factories, triggers an event a C++ listener receives,
		// and sets a global the C++ side reads back. Runs through the
		// neutral ScriptRuntime seam; skipped in no-scripting builds.
		if (Orkige::ScriptRuntime::available())
		{
			LuaEventProbe luaProbe;
			optr<Orkige::EventListener> luaListener =
				Orkige::GlobalEventManager::getSingleton().bind(
					Orkige::EventType("lua_event"),
					&LuaEventProbe::onLuaEvent, &luaProbe);
			const Orkige::ScriptRuntime::Result luaResult =
				scriptRuntime.runString(R"lua(
				local engine = Engine.getSingleton()
				assert(engine ~= nil, 'Engine.getSingleton() returned nil')

				-- call registered Engine methods
				demo_window_handle = engine:getTopLevelWindowHandle()

				-- the render facade surface (RenderSystem/RenderWorld/
				-- RenderNode/RenderCamera usertypes): reach the scene graph
				-- and the window camera rig the demo built in C++
				local render = engine:getRenderSystem()
				assert(render ~= nil, 'engine:getRenderSystem() returned nil')
				local renderWorld = render:getWorld()
				assert(renderWorld ~= nil, 'render:getWorld() returned nil')
				assert(renderWorld:getRootNode():numChildren() > 0,
					'render world root has no facade child nodes')
				local camera = engine:getCamera()
				assert(camera ~= nil, 'engine:getCamera() returned nil')
				local cameraNode = camera:getNode()
				assert(cameraNode ~= nil, 'camera rig node not reachable from Lua')
				local cameraPos = cameraNode:getPosition()
				assert(cameraPos.z > 1.0, 'camera rig node answers a wrong position')
				assert(engine:getWindowWidth() > 0 and engine:getWindowHeight() > 0,
					'window size not reachable from Lua')

				-- construct registered core objects: Object through its factory,
				-- Event through the Lua call syntax (first registered constructor)
				local payload = Object.new1('lua_payload')
				assert(payload:getObjectID() == 'lua_payload')
				local ev = Event('lua_event')
				ev:setData(payload)
				assert(ev:getData():getObjectID() == 'lua_payload')

				-- fire it into the engine event system; a C++ listener verifies it
				GlobalEventManager.getSingleton():trigger(ev)

				demo_lua_ok = 42
			)lua");
			if (!luaResult.success)
			{
				SDL_Log("hello_orkige: FAILED - Lua script error: %s",
					luaResult.error.c_str());
				return 1;
			}
			const int luaOk = static_cast<int>(
				scriptRuntime.getNumber({"demo_lua_ok"}, 0.0));
			const std::string luaHandle =
				scriptRuntime.getString({"demo_window_handle"}, "");
			if (luaOk != 42 || !luaProbe.received)
			{
				SDL_Log("hello_orkige: FAILED - Lua verification (demo_lua_ok=%d, "
					"event received=%d)", luaOk,
					static_cast<int>(luaProbe.received));
				return 1;
			}
			SDL_Log("hello_orkige: Lua scripting OK - demo_lua_ok=%d, C++ listener "
				"got the Lua-triggered event, window handle via Lua='%s'",
				luaOk, luaHandle.c_str());
		}
		else
		{
			SDL_Log("hello_orkige: scripting disabled (backend '%s') - Lua "
				"smoke test skipped", Orkige::ScriptRuntime::backendName());
		}

		// ORKIGE_DEMO_GUI=1: engine_gui runtime smoke test, flavor-
		// neutral (hello ships no .ogui atlas media of its own, so the
		// full HUD render is covered by the jumper/roller selfchecks
		// instead). What this verifies: the module links, an empty
		// UiScreen on its DrawLayer2D survives a rendered frame and
		// tears down cleanly, and GuiManager - which loads its
		// default atlas in the constructor by design - fails that
		// construction with the documented resource error instead of
		// crashing.
		if (std::getenv("ORKIGE_DEMO_GUI"))
		{
			{
				// the UI renderer alone: an empty screen (no atlas file -
				// a bare UiAtlas-less surface is not constructible, so use
				// a minimal in-memory atlas-free draw layer) rendered and
				// torn down without media
				auto uiProbeLayer = render->createDrawLayer2D();
				if (!render->renderOneFrame())
				{
					SDL_Log("hello_orkige: FAILED - frame with an empty UI "
						"draw layer alive did not render");
					return 1;
				}
				uiProbeLayer.reset();
			}
			bool guiRefusedCleanly = false;
			try
			{
				Orkige::GuiManager guiManager(
					Orkige::onew(new Orkige::GuiFactory()));
			}
			catch (std::exception const& e)
			{
				// both Ogre backends surface the missing atlas as a
				// resource exception (derives from std::exception - no
				// renderer types needed to catch it)
				guiRefusedCleanly = true;
				SDL_Log("hello_orkige: GuiManager without atlas failed "
					"cleanly as designed: %s", e.what());
			}
			if (!guiRefusedCleanly)
			{
				SDL_Log("hello_orkige: FAILED - GuiManager constructed "
					"without its default atlas; expected a resource error");
				return 1;
			}
			SDL_Log("hello_orkige: Gui smoke test passed (UI draw-layer "
				"lifecycle + clean no-atlas constructor failure, "
				"engine.hasUISystem()=%d)",
				static_cast<int>(host.getEngine().hasUISystem()));
		}

		// ORKIGE_DEMO_UI_SCALE=1: the DPI + safe-area + settings-widget
		// selfcheck (flavor-neutral). Injects a fake content scale
		// (ORKIGE_FAKE_CONTENT_SCALE, default 3) and a fake top safe-area inset
		// (ORKIGE_FAKE_SAFE_TOP, default 96) through the PlatformWindow override
		// seam, boots a real GuiManager on the committed gui atlas and
		// asserts: UiGlyph::scale snaps to the integer content scale, an
		// authored 160x40 button grows to a tappable size, getSafeAreaInsets
		// reports the inset and a top-anchored label lands below it, and the
		// bound checkbox + select menu change value.
		if (std::getenv("ORKIGE_DEMO_UI_SCALE"))
		{
			const float fakeScale = std::getenv("ORKIGE_FAKE_CONTENT_SCALE")
				? std::strtof(std::getenv("ORKIGE_FAKE_CONTENT_SCALE"), nullptr)
				: 3.0f;
			const unsigned int fakeTop = std::getenv("ORKIGE_FAKE_SAFE_TOP")
				? static_cast<unsigned int>(std::strtoul(
					std::getenv("ORKIGE_FAKE_SAFE_TOP"), nullptr, 10))
				: 96u;
			Orkige::PlatformWindow::setContentScaleOverride(fakeScale);
			Orkige::SafeAreaInsets fakeInsets;
			fakeInsets.mTop = fakeTop;
			Orkige::PlatformWindow::setSafeAreaInsetsOverride(fakeInsets);

			// the committed gui atlas (gui_default.ogui/.png)
			render->addResourceLocation(ORKIGE_DEMO_GUI_ATLAS_DIR);
			render->initialiseResourceGroups();

			bool uiOk = true;
			{
				Orkige::optr<Orkige::GuiFactory> factory =
					Orkige::onew(new Orkige::GuiFactory());
				// the ctor drives UiGlyph::scale from the (overridden) scale
				Orkige::GuiManager gui(factory, "gui_default");

				const float expectedScale =
					std::max(1.0f, std::round(fakeScale));
				if (Orkige::UiGlyph::scale.x != expectedScale ||
					Orkige::UiGlyph::scale.y != expectedScale)
				{
					SDL_Log("hello_orkige: FAILED - UiGlyph::scale is (%.1f,%.1f)"
						", expected (%.1f,%.1f)", Orkige::UiGlyph::scale.x,
						Orkige::UiGlyph::scale.y, expectedScale, expectedScale);
					uiOk = false;
				}

				// an authored 160x40 button must scale to a tappable size
				Orkige::woptr<Orkige::GuiButton> button =
					factory->createButton("uiScaleButton", "button", 9, "OK",
						Orkige::Vec2(100, 100), Orkige::GuiLabel::LA_CENTER,
						Orkige::Vec2(160, 40), "", 5, false,
						Orkige::GuiButtonBlink::BBLINK_NONE);
				if (button.lock())
				{
					const float widthPx = button.lock()->getSize().x;
					if (widthPx < 120.0f)
					{
						SDL_Log("hello_orkige: FAILED - a 160px button scaled to "
							"%.1fpx, below the 120px tappable floor", widthPx);
						uiOk = false;
					}
				}
				else
				{
					SDL_Log("hello_orkige: FAILED - button not created");
					uiOk = false;
				}

				// safe-area insets flow through the Engine, and a top-anchored
				// label lands below the (injected) top inset
				const Orkige::SafeAreaInsets insets =
					host.getEngine().getSafeAreaInsets();
				if (insets.mTop != fakeTop)
				{
					SDL_Log("hello_orkige: FAILED - getSafeAreaInsets top=%u, "
						"expected %u", insets.mTop, fakeTop);
					uiOk = false;
				}
				float anchoredX = 0.0f;
				float anchoredY = 0.0f;
				Orkige::UiAnchor::place(200, 40, 8, 8,
					host.getEngine().getWindowWidth(),
					host.getEngine().getWindowHeight(), insets,
					false, false, anchoredX, anchoredY);
				Orkige::woptr<Orkige::GuiLabel> hudLabel =
					factory->createLabel("uiSafeLabel", 9, "HUD",
						Orkige::Vec2(anchoredX, anchoredY), "", 5, false);
				if (hudLabel.lock() &&
					hudLabel.lock()->getPosition().y <
						static_cast<float>(insets.mTop))
				{
					SDL_Log("hello_orkige: FAILED - top-anchored label y=%.1f "
						"crosses the top inset %u",
						hudLabel.lock()->getPosition().y, insets.mTop);
					uiOk = false;
				}

				// a bound checkbox toggles
				Orkige::woptr<Orkige::GuiCheckBox> checkBox =
					factory->createCheckBox("uiCheck", "checkbox", 9, "SFX",
						Orkige::Vec2(100, 300), Orkige::GuiLabel::LA_CENTER,
						Orkige::Vec2::ZERO, "", 6, false);
				if (checkBox.lock())
				{
					const bool before = checkBox.lock()->isChecked();
					checkBox.lock()->toggle();
					if (checkBox.lock()->isChecked() == before)
					{
						SDL_Log("hello_orkige: FAILED - checkbox toggle did not "
							"change isChecked()");
						uiOk = false;
					}
				}
				else
				{
					SDL_Log("hello_orkige: FAILED - checkbox not created");
					uiOk = false;
				}

				// a bound select menu reports a value change (the slider shares
				// this value API)
				Orkige::woptr<Orkige::GuiSelectMenu> selectMenu =
					factory->createSelectMenu("uiSelect", "uiSelectButton",
						"panel", 9, "Quality", Orkige::Vec2(100, 400),
						Orkige::GuiLabel::LA_CENTER, Orkige::Vec2::ZERO,
						"", 6);
				if (selectMenu.lock())
				{
					Orkige::StringVector items;
					items.push_back("Low");
					items.push_back("Medium");
					items.push_back("High");
					selectMenu.lock()->setItems(items);
					selectMenu.lock()->selectItemIndex(2, false);
					if (selectMenu.lock()->getSelectedItemIndex() != 2)
					{
						SDL_Log("hello_orkige: FAILED - select menu index is "
							"%zu, expected 2",
							selectMenu.lock()->getSelectedItemIndex());
						uiOk = false;
					}
				}
				else
				{
					SDL_Log("hello_orkige: FAILED - select menu not created");
					uiOk = false;
				}

				// a bound slider (shares the select-menu value API + pin/arrow
				// sprites) - and a live setSize resize, previously unimplemented:
				// the resize must not assert and must keep the selected value
				Orkige::woptr<Orkige::GuiSlider> slider =
					factory->createSlider("uiSlider", "uiSliderButton",
						"select_menu_field", 9, "Volume", Orkige::Vec2(100, 470),
						Orkige::GuiLabel::LA_CENTER, Orkige::Vec2(180, 40),
						"", 6);
				if (slider.lock())
				{
					Orkige::StringVector items;
					items.push_back("0");
					items.push_back("50");
					items.push_back("100");
					slider.lock()->setItems(items);
					slider.lock()->selectItemIndex(1, false);
					slider.lock()->setSize(220, 44);	// the implemented path
					if (slider.lock()->getSelectedItemIndex() != 1)
					{
						SDL_Log("hello_orkige: FAILED - slider index is %zu after "
							"resize, expected 1",
							slider.lock()->getSelectedItemIndex());
						uiOk = false;
					}
				}
				else
				{
					SDL_Log("hello_orkige: FAILED - slider not created");
					uiOk = false;
				}

				// the whole UI must compose a frame without crashing
				if (!render->renderOneFrame())
				{
					SDL_Log("hello_orkige: FAILED - UI-scale frame did not "
						"render");
					uiOk = false;
				}
			}
			Orkige::PlatformWindow::setContentScaleOverride(0.0f);
			Orkige::PlatformWindow::clearSafeAreaInsetsOverride();
			if (!uiOk)
			{
				return 1;
			}
			SDL_Log("hello_orkige: UI-scale selfcheck passed (UiGlyph::scale "
				"snapped, button + text scaled, safe-area anchoring, checkbox "
				"toggle + select-menu value)");
		}

		// ORKIGE_DEMO_TTF=1: the runtime TrueType-font + vector-sprite selfcheck
		// (flavor-neutral). Boots a real GuiManager on a runtime-baked atlas
		// (gui_ttf_demo.ogui: Nunito TTF fonts + an SVG star, all rasterised
		// into one GPU page at boot by FontAtlas), then asserts: the TTF fonts +
		// the SVG sprite baked, a Latin label AND a Cyrillic label render (the
		// Cyrillic glyphs are BEYOND the eager Latin-1 page, so they exercise the
		// lazy-paging baker - the loc()/CJK localisation unblocker), and the HUD
		// submits a non-empty batch. Runs identically on classic + next (gui
		// draws through DrawLayer2D), so a runtime font looks the same on both.
		if (std::getenv("ORKIGE_DEMO_TTF"))
		{
			// the runtime atlas .ogui + its committed .svg, and the engine
			// default font the .ogui references by name
			render->addResourceLocation(ORKIGE_DEMO_TTF_ATLAS_DIR);
			render->addResourceLocation(ORKIGE_ENGINE_FONT_DIR);
			render->initialiseResourceGroups();

			bool ttfOk = true;
			{
				Orkige::optr<Orkige::GuiFactory> factory =
					Orkige::onew(new Orkige::GuiFactory());
				// loads gui_ttf_demo.ogui -> FontAtlas bakes the TTF pages +
				// the SVG sprite into one page and uploads it
				Orkige::GuiManager gui(factory, "gui_ttf_demo");

				Orkige::woptr<Orkige::GuiView> view =
					gui.getView("gui_ttf_demo");
				Orkige::UiAtlas const* atlas = view.lock()
					? view.lock()->getScreen()->getAtlas() : nullptr;
				if (atlas == nullptr)
				{
					SDL_Log("hello_orkige: FAILED - runtime TTF atlas view missing");
					ttfOk = false;
				}
				else
				{
					// the TTF fonts baked (metrics from the font tables)
					if (atlas->getFont(9) == nullptr ||
						atlas->getFont(24) == nullptr)
					{
						SDL_Log("hello_orkige: FAILED - runtime TTF fonts not baked");
						ttfOk = false;
					}
					// the SVG rasterised into a normal UiSprite
					Orkige::UiSprite const* star = atlas->getSprite("star");
					if (star == nullptr || star->spriteWidth <= 0.0f)
					{
						SDL_Log("hello_orkige: FAILED - SVG sprite not baked");
						ttfOk = false;
					}

					// a Latin label (eager page) + a Cyrillic label (lazy page)
					factory->createLabel("ttfLatin", 9, "Hello Orkige",
						Orkige::Vec2(80, 80), "", 5, false);
					// UTF-8 for "АБВ" (Cyrillic, U+0410..U+0412) - beyond Latin-1
					factory->createLabel("ttfCyrillic", 9,
						"\xD0\x90\xD0\x91\xD0\x92", Orkige::Vec2(80, 140),
						"", 5, false);
					if (star != nullptr)
					{
						factory->createDecorWidget("ttfStar", "star",
							Orkige::Vec2(80, 200),
							Orkige::Vec2(star->spriteWidth, star->spriteHeight),
							"gui_ttf_demo", 5);
					}

					// compose a frame: the widgets lay out, the screen submits
					// its batch, and any lazily-baked glyph reaches the GPU
					if (!render->renderOneFrame())
					{
						SDL_Log("hello_orkige: FAILED - runtime TTF frame did not "
							"render");
						ttfOk = false;
					}

					// the HUD actually drew (non-empty submitted batch)
					if (view.lock() &&
						view.lock()->getScreen()->getLastVertexCount() == 0)
					{
						SDL_Log("hello_orkige: FAILED - runtime TTF HUD submitted "
							"no vertices");
						ttfOk = false;
					}

					// the Cyrillic codepoints were baked on demand into the
					// sparse page (they were never in the eager Latin-1 range)
					if (atlas->getFont(9) != nullptr &&
						atlas->getFont(9)->getGlyph(0x0410) == nullptr)
					{
						SDL_Log("hello_orkige: FAILED - lazy glyph paging did not "
							"bake the Cyrillic glyph");
						ttfOk = false;
					}
				}
			}
			if (!ttfOk)
			{
				return 1;
			}
			SDL_Log("hello_orkige: runtime TTF + SVG selfcheck passed (TTF fonts "
				"+ SVG sprite baked, Latin + lazy-paged Cyrillic text rendered, "
				"non-empty HUD batch)");
		}

		// ORKIGE_DEMO_LOC=1: the localisation runtime selfcheck (flavor-neutral).
		// Loads a project's loc/ registry (en source + de target + the generated
		// en-XA pseudo-locale) into the StringTable, then loads a committed .oui
		// whose captions are all @keys and reads the RESOLVED widget text back:
		// - the source language (en) resolves the English source ("Hello, Orkige")
		// - locale.set("de") + a screen re-push resolves the German target and it
		//   actually RENDERS (non-empty batch, text differs from the source)
		// - the pseudo locale en-XA resolves an accented variant (its glyphs bake
		//   through lazy paging) that also differs from the source and renders
		// The language switch is driven through the SAME StringTable the Lua
		// `locale` table wraps (set == StringTable::setLanguage on a loaded
		// language); re-pushing the screen is the documented contract (a switch
		// does not retro-edit widgets built at the old language).
		if (std::getenv("ORKIGE_DEMO_LOC"))
		{
			Orkige::PlatformWindow::setContentScaleOverride(1.0f);
			render->addResourceLocation(ORKIGE_DEMO_GUI_ATLAS_DIR);
			render->addResourceLocation(ORKIGE_DEMO_OUI_DIR);
			render->initialiseResourceGroups();

			// load the project's XLIFF localisation directory into the table the
			// gui @key routing (GuiFactory::resolveText) reads
			Orkige::StringTable stringTable;
			bool locOk = stringTable.loadXliffDirectory(ORKIGE_DEMO_LOC_DIR);
			if (!locOk)
			{
				SDL_Log("hello_orkige: FAILED - loc directory '%s' did not load",
					ORKIGE_DEMO_LOC_DIR);
			}

			// after a directory load the active language defaults to the source
			if (locOk && stringTable.getSourceLanguage() != "en")
			{
				SDL_Log("hello_orkige: FAILED - source language is '%s', "
					"expected 'en'", stringTable.getSourceLanguage().c_str());
				locOk = false;
			}
			// the three languages loaded (sorted: de, en, en-XA)
			if (locOk && (!stringTable.hasLanguage("en") ||
				!stringTable.hasLanguage("de") ||
				!stringTable.hasLanguage("en-XA")))
			{
				SDL_Log("hello_orkige: FAILED - not all languages loaded");
				locOk = false;
			}

			// build the localised screen at a given language and read back the
			// resolved text of the @key label + button, and whether it rendered.
			// A fresh GuiFactory/GuiManager each call is the "re-push the screen"
			// contract in miniature.
			auto buildAndRead = [&](Orkige::String const & language,
				Orkige::String & titleText,
				Orkige::String & startText, bool & rendered) -> bool
			{
				stringTable.setLanguage(language);
				Orkige::optr<Orkige::GuiFactory> factory =
					Orkige::onew(new Orkige::GuiFactory());
				Orkige::GuiManager gui(factory, "gui_default");
				factory->loadLayout("loc_demo.oui");
				const bool frameOk = render->renderOneFrame();
				titleText.clear();
				startText.clear();
				rendered = false;
				if (gui.widgetExists("locTitle"))
				{
					Orkige::optr<Orkige::GuiLabel> label =
						gui.getWidgetAs<Orkige::GuiLabel>("locTitle").lock();
					if (label && label->getCaption())
					{
						titleText = label->getCaption()->text();
					}
				}
				if (gui.widgetExists("locStart"))
				{
					Orkige::optr<Orkige::GuiButton> button =
						gui.getWidgetAs<Orkige::GuiButton>("locStart").lock();
					if (button)
					{
						startText = button->getCaption();
					}
				}
				Orkige::woptr<Orkige::GuiView> view = gui.getView("gui_default");
				if (view.lock())
				{
					rendered =
						view.lock()->getScreen()->getLastVertexCount() > 0;
				}
				return frameOk;
			};

			Orkige::String enTitle, enStart, deTitle, deStart, pseudoTitle,
				pseudoStart;
			bool enRendered = false, deRendered = false, pseudoRendered = false;
			if (locOk)
			{
				buildAndRead("en", enTitle, enStart, enRendered);
				// the source language resolves the English source text
				if (enTitle != "Hello, Orkige" || enStart != "Start Game")
				{
					SDL_Log("hello_orkige: FAILED - en text '%s' / '%s' != source",
						enTitle.c_str(), enStart.c_str());
					locOk = false;
				}
				if (!enRendered)
				{
					SDL_Log("hello_orkige: FAILED - en screen did not render");
					locOk = false;
				}
			}
			if (locOk)
			{
				buildAndRead("de", deTitle, deStart, deRendered);
				// switching to German re-skins the whole screen from the target
				if (deTitle != "Hallo, Orkige" || deStart != "Spiel starten")
				{
					SDL_Log("hello_orkige: FAILED - de text '%s' / '%s' != target",
						deTitle.c_str(), deStart.c_str());
					locOk = false;
				}
				if (deTitle == enTitle)
				{
					SDL_Log("hello_orkige: FAILED - de title did not differ from en");
					locOk = false;
				}
				if (!deRendered)
				{
					SDL_Log("hello_orkige: FAILED - de screen did not render");
					locOk = false;
				}
			}
			if (locOk)
			{
				buildAndRead("en-XA", pseudoTitle, pseudoStart, pseudoRendered);
				// the pseudo-locale resolves an accented variant (its glyphs bake
				// on demand) that differs from the source but still renders
				if (pseudoTitle == enTitle || pseudoTitle.empty())
				{
					SDL_Log("hello_orkige: FAILED - pseudo title '%s' did not "
						"differ from source", pseudoTitle.c_str());
					locOk = false;
				}
				if (!pseudoRendered)
				{
					SDL_Log("hello_orkige: FAILED - pseudo screen did not render");
					locOk = false;
				}
			}

			Orkige::PlatformWindow::setContentScaleOverride(0.0f);
			if (!locOk)
			{
				return 1;
			}
			SDL_Log("hello_orkige: localisation selfcheck passed (en source '%s', "
				"de target '%s', pseudo en-XA '%s' - all resolved through @key "
				"routing and rendered; live language switch re-skins the screen)",
				enTitle.c_str(), deTitle.c_str(), pseudoTitle.c_str());
		}

		// ORKIGE_DEMO_LAYOUT=1: the nine-slice + rect-anchor layout selfcheck
		// (flavor-neutral). Boots a real GuiManager on the committed atlas,
		// builds an anchored, nine-sliced panel with a child pinned inside it,
		// and asserts: the panel resolves to the window minus its anchor insets;
		// enabling nine-slice emits MORE geometry (fixed corners + stretched
		// edges) than a plain stretched quad; the child resolves inside the
		// panel's rect; switching the root to the safe area re-lays-out the HUD
		// off the (injected) notch inset with zero script maths; and a design
		// resolution scales the layout geometry. Runs identically on classic +
		// next (the resolver is pure, gui draws through DrawLayer2D).
		if (std::getenv("ORKIGE_DEMO_LAYOUT"))
		{
			// deterministic geometry: force content scale 1 (glyph/corner
			// density) and a known top+left safe inset for the safe-root leg
			Orkige::PlatformWindow::setContentScaleOverride(1.0f);
			Orkige::SafeAreaInsets fakeInsets;
			fakeInsets.mTop = 100;
			fakeInsets.mLeft = 40;
			Orkige::PlatformWindow::setSafeAreaInsetsOverride(fakeInsets);

			render->addResourceLocation(ORKIGE_DEMO_GUI_ATLAS_DIR);
			render->initialiseResourceGroups();

			bool layoutOk = true;
			{
				Orkige::optr<Orkige::GuiFactory> factory =
					Orkige::onew(new Orkige::GuiFactory());
				Orkige::GuiManager gui(factory, "gui_default");

				const float W = static_cast<float>(host.getEngine().getWindowWidth());
				const float H = static_cast<float>(host.getEngine().getWindowHeight());

				// an anchored panel: fill the window minus a 20px inset on each
				// edge (StretchAll + offsets). Its initial size is a placeholder
				// the resolver overwrites.
				Orkige::woptr<Orkige::GuiDecorWidget> panelWeak =
					factory->createDecorWidget("layoutPanel", "panel",
						Orkige::Vec2(0, 0), Orkige::Vec2(64, 64), "", 5);
				Orkige::optr<Orkige::GuiDecorWidget> panel = panelWeak.lock();
				if (!panel)
				{
					SDL_Log("hello_orkige: FAILED - layout panel not created");
					layoutOk = false;
				}
				else
				{
					panel->setAnchorPreset("StretchAll");
					panel->setOffsets(20, 20, -20, -20);

					render->renderOneFrame();	// runs the resolve pass
					const Orkige::Vec2 pos = panel->getPosition();
					const Orkige::Vec2 size = panel->getSize();
					if (std::abs(pos.x - 20.0f) > 1.5f ||
						std::abs(pos.y - 20.0f) > 1.5f ||
						std::abs(size.x - (W - 40.0f)) > 1.5f ||
						std::abs(size.y - (H - 40.0f)) > 1.5f)
					{
						SDL_Log("hello_orkige: FAILED - anchored panel resolved to "
							"(%.1f,%.1f %.1fx%.1f), expected ~(20,20 %.0fx%.0f)",
							pos.x, pos.y, size.x, size.y, W - 40.0f, H - 40.0f);
						layoutOk = false;
					}

					// nine-slice emits more geometry than a plain stretched quad
					Orkige::woptr<Orkige::GuiView> view =
						gui.getView("gui_default");
					panel->setNineSlice(false);
					render->renderOneFrame();
					const size_t vertsPlain = view.lock()
						? view.lock()->getScreen()->getLastVertexCount() : 0;
					panel->setNineSlice(true);
					render->renderOneFrame();
					const size_t vertsNine = view.lock()
						? view.lock()->getScreen()->getLastVertexCount() : 0;
					if (vertsNine <= vertsPlain)
					{
						SDL_Log("hello_orkige: FAILED - nine-slice did not add "
							"geometry (%zu vs plain %zu verts)",
							vertsNine, vertsPlain);
						layoutOk = false;
					}

					// a child pinned top-left INSIDE the panel resolves against
					// the panel's rect (parent-relative), not the window
					Orkige::woptr<Orkige::GuiButton> childWeak =
						factory->createButton("layoutChild", "button", 9, "OK",
							Orkige::Vec2(0, 0), Orkige::GuiLabel::LA_CENTER,
							Orkige::Vec2(120, 40), "", 6, false,
							Orkige::GuiButtonBlink::BBLINK_NONE);
					Orkige::optr<Orkige::GuiButton> child = childWeak.lock();
					if (child)
					{
						child->setParent(panel);
						child->setAnchorPreset("TopLeft");
						child->setSizeDelta(120, 40);
						child->setAnchoredPosition(12, 12);
						render->renderOneFrame();
						const Orkige::Vec2 childPos = child->getPosition();
						const Orkige::Vec2 panelPos = panel->getPosition();
						if (std::abs(childPos.x - (panelPos.x + 12.0f)) > 1.5f ||
							std::abs(childPos.y - (panelPos.y + 12.0f)) > 1.5f)
						{
							SDL_Log("hello_orkige: FAILED - child resolved to "
								"(%.1f,%.1f), expected panel+(12,12)=(%.1f,%.1f)",
								childPos.x, childPos.y, panelPos.x + 12.0f,
								panelPos.y + 12.0f);
							layoutOk = false;
						}
					}
					else
					{
						SDL_Log("hello_orkige: FAILED - layout child not created");
						layoutOk = false;
					}

					// switch the root to the safe area: the panel re-lays-out
					// off the injected top/left insets with no script maths
					gui.setRootSpace("SafeArea");
					render->renderOneFrame();
					const Orkige::Vec2 safePos = panel->getPosition();
					if (std::abs(safePos.x -
							(static_cast<float>(fakeInsets.mLeft) + 20.0f)) > 1.5f ||
						std::abs(safePos.y -
							(static_cast<float>(fakeInsets.mTop) + 20.0f)) > 1.5f)
					{
						SDL_Log("hello_orkige: FAILED - safe-area panel at "
							"(%.1f,%.1f), expected inset+(20,20)=(%.1f,%.1f)",
							safePos.x, safePos.y,
							static_cast<float>(fakeInsets.mLeft) + 20.0f,
							static_cast<float>(fakeInsets.mTop) + 20.0f);
						layoutOk = false;
					}

					// a design resolution scales the layout geometry: half the
					// live window as the design => layoutScale 2 => the 20px
					// inset becomes 40px (in the safe root, from the left inset)
					gui.setDesignResolution(W * 0.5f, H * 0.5f, 0.0f);
					render->renderOneFrame();
					const Orkige::Vec2 scaledPos = panel->getPosition();
					if (std::abs(scaledPos.x -
							(static_cast<float>(fakeInsets.mLeft) + 40.0f)) > 1.5f)
					{
						SDL_Log("hello_orkige: FAILED - design-scaled panel x=%.1f,"
							" expected inset+40=%.1f", scaledPos.x,
							static_cast<float>(fakeInsets.mLeft) + 40.0f);
						layoutOk = false;
					}
				}
			}
			Orkige::PlatformWindow::setContentScaleOverride(0.0f);
			Orkige::PlatformWindow::clearSafeAreaInsetsOverride();
			if (!layoutOk)
			{
				return 1;
			}
			SDL_Log("hello_orkige: layout selfcheck passed (anchored nine-slice "
				"panel, parent-relative child, safe-area root re-layout, design "
				"reference scale)");
		}

		// ORKIGE_DEMO_OUI=1: the declarative-UI + scroll + groups selfcheck
		// (flavor-neutral). Loads a whole settings screen from a committed .oui
		// file (an anchored nine-slice panel, a scroll viewport whose content is
		// a vertical group of labelled rows that overflows) through
		// GuiFactory::loadLayout - the acceptance proof that an agent can
		// author a full screen as one text file. Asserts: the content overflows;
		// scrolling shifts the resolved row rects up; the content layer carries a
		// scissor equal to the viewport; and a checkbox in a scrolled row still
		// hit-tests correctly (a synthetic click at its shifted position toggles
		// it on the real input path). Runs identically on classic + next.
		if (std::getenv("ORKIGE_DEMO_OUI"))
		{
			Orkige::PlatformWindow::setContentScaleOverride(1.0f);
			render->addResourceLocation(ORKIGE_DEMO_GUI_ATLAS_DIR);
			render->addResourceLocation(ORKIGE_DEMO_OUI_DIR);
			render->initialiseResourceGroups();

			bool ouiOk = true;
			{
				Orkige::optr<Orkige::GuiFactory> factory =
					Orkige::onew(new Orkige::GuiFactory());
				Orkige::GuiManager gui(factory, "gui_default");
				gui.enableInputEvents();

				// the whole screen, authored as one text file
				factory->loadLayout("settings_screen.oui");
				render->renderOneFrame();	// runs the two-pass resolve

				Orkige::optr<Orkige::GuiScrollView> scroll;
				Orkige::optr<Orkige::GuiCheckBox> check;
				if (gui.widgetExists("settingsScroll"))
				{
					scroll = gui.getWidgetAs<Orkige::GuiScrollView>(
						"settingsScroll").lock();
				}
				if (gui.widgetExists("rowSound"))
				{
					check = gui.getWidgetAs<Orkige::GuiCheckBox>(
						"rowSound").lock();
				}
				if (!scroll || !check)
				{
					SDL_Log("hello_orkige: FAILED - .oui widgets not created");
					ouiOk = false;
				}
				else if (scroll->getMaxScroll() <= 0.0f)
				{
					SDL_Log("hello_orkige: FAILED - scroll content did not "
						"overflow the viewport (maxScroll %.1f)",
						scroll->getMaxScroll());
					ouiOk = false;
				}
				else
				{
					// the content layer is clipped to the viewport rect exactly
					Orkige::UiLayer* clip = scroll->getLayer();
					const Orkige::Vec2 vpPos = scroll->getPosition();
					const Orkige::Vec2 vpSize = scroll->getSize();
					if (!clip || !clip->hasScissor())
					{
						SDL_Log("hello_orkige: FAILED - no scissor on the scroll "
							"content layer");
						ouiOk = false;
					}
					else
					{
						const Orkige::DrawLayer2D::ScissorRect sc = clip->getScissor();
						if (std::abs(sc.left - vpPos.x) > 1.5f ||
							std::abs(sc.top - vpPos.y) > 1.5f ||
							std::abs(sc.width - vpSize.x) > 1.5f ||
							std::abs(sc.height - vpSize.y) > 1.5f)
						{
							SDL_Log("hello_orkige: FAILED - scissor (%d,%d %dx%d) "
								"!= viewport (%.0f,%.0f %.0fx%.0f)", sc.left, sc.top,
								sc.width, sc.height, vpPos.x, vpPos.y, vpSize.x,
								vpSize.y);
							ouiOk = false;
						}
					}

					// scrolling shifts the resolved content rects UP
					const Orkige::Vec2 checkBefore = check->getPosition();
					const bool checkedBefore = check->isChecked();
					scroll->setScroll(-50.0f);
					render->renderOneFrame();
					const Orkige::Vec2 checkAfter = check->getPosition();
					if (checkAfter.y >= checkBefore.y - 1.0f)
					{
						SDL_Log("hello_orkige: FAILED - scroll did not move the "
							"content (checkbox y %.1f -> %.1f)", checkBefore.y,
							checkAfter.y);
						ouiOk = false;
					}

					// a click at the checkbox's SHIFTED centre toggles it: the hit
					// test accounts for the scroll offset (the rect moved with it)
					const Orkige::Vec2 cPos = check->getPosition();
					const Orkige::Vec2 cSize = check->getSize();
					SDL_Event press{};
					press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
					press.button.button = SDL_BUTTON_LEFT;
					press.button.down = true;
					press.button.x = cPos.x + cSize.x * 0.5f;
					press.button.y = cPos.y + cSize.y * 0.5f;
					inputManager.injectEvent(press);
					if (check->isChecked() == checkedBefore)
					{
						SDL_Log("hello_orkige: FAILED - checkbox in a scrolled row "
							"did not toggle at its shifted position (%.1f,%.1f)",
							press.button.x, press.button.y);
						ouiOk = false;
					}
				}
			}
			Orkige::PlatformWindow::setContentScaleOverride(0.0f);
			if (!ouiOk)
			{
				return 1;
			}
			SDL_Log("hello_orkige: .oui selfcheck passed (declarative settings "
				"screen loaded from one file, vertical group + scroll, scissor "
				"clip == viewport, scrolled checkbox hit-tests correctly)");
		}

		// ORKIGE_DEMO_TEXTENTRY=1: the gui TextEntry selfcheck (flavor-
		// neutral). Boots a real GuiManager on the committed atlas, creates a
		// text field and drives it entirely through synthetic SDL events on the
		// REAL input path (InputManager::injectEvent -> GuiManager routing):
		// a mouse press focuses it, SDL_EVENT_TEXT_INPUT types text, backspace
		// deletes, Home/Right move the caret + mid-string insert lands correctly,
		// and Return submits (wasSubmitted latches once, then blurs).
		if (std::getenv("ORKIGE_DEMO_TEXTENTRY"))
		{
			render->addResourceLocation(ORKIGE_DEMO_GUI_ATLAS_DIR);
			render->initialiseResourceGroups();
			bool entryOk = true;
			{
				Orkige::optr<Orkige::GuiFactory> factory =
					Orkige::onew(new Orkige::GuiFactory());
				Orkige::GuiManager gui(factory, "gui_default");
				gui.enableInputEvents();

				const Orkige::Vec2 fieldPos(100.0f, 100.0f);
				const Orkige::Vec2 fieldSize(320.0f, 50.0f);
				Orkige::woptr<Orkige::GuiTextEntry> entryWeak =
					factory->createTextEntry("uiTextEntry", "none", 9,
						"type here", fieldPos, fieldSize, "", 5, 16);
				Orkige::optr<Orkige::GuiTextEntry> entry = entryWeak.lock();
				if (!entry)
				{
					SDL_Log("hello_orkige: FAILED - text entry not created");
					entryOk = false;
				}

				// synthetic-event helpers on the REAL input path
				auto pressAt = [&](float x, float y)
				{
					SDL_Event e{};
					e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
					e.button.button = SDL_BUTTON_LEFT;
					e.button.down = true;
					e.button.x = x;
					e.button.y = y;
					inputManager.injectEvent(e);
				};
				auto typeText = [&](const char* text)
				{
					SDL_Event e{};
					e.type = SDL_EVENT_TEXT_INPUT;
					e.text.text = text;
					inputManager.injectEvent(e);
				};
				auto pressKey = [&](SDL_Scancode scancode)
				{
					SDL_Event e{};
					e.type = SDL_EVENT_KEY_DOWN;
					e.key.scancode = scancode;
					e.key.down = true;
					inputManager.injectEvent(e);
				};

				if (entryOk)
				{
					// tap to focus, then type
					pressAt(fieldPos.x + 20.0f, fieldPos.y + 20.0f);
					if (!entry->isFocused())
					{
						SDL_Log("hello_orkige: FAILED - tap did not focus the "
							"text entry");
						entryOk = false;
					}
					typeText("Hello");
					if (entryOk && entry->getText() != "Hello")
					{
						SDL_Log("hello_orkige: FAILED - typed 'Hello', got '%s'",
							entry->getText().c_str());
						entryOk = false;
					}
					// backspace removes the last character
					pressKey(SDL_SCANCODE_BACKSPACE);
					if (entryOk && entry->getText() != "Hell")
					{
						SDL_Log("hello_orkige: FAILED - after backspace got '%s'",
							entry->getText().c_str());
						entryOk = false;
					}
					// Home + one Right, then insert -> lands after the first char
					pressKey(SDL_SCANCODE_HOME);
					pressKey(SDL_SCANCODE_RIGHT);
					typeText("X");
					if (entryOk && entry->getText() != "HXell")
					{
						SDL_Log("hello_orkige: FAILED - mid-string insert got '%s'"
							", expected 'HXell'", entry->getText().c_str());
						entryOk = false;
					}
					// max length 16: a long paste is clipped
					typeText("0123456789ABCDEF");
					if (entryOk &&
						Orkige::TextEntryEdit::codepointCount(entry->getText())
							!= 16)
					{
						SDL_Log("hello_orkige: FAILED - max length not enforced "
							"(%zu code points)",
							Orkige::TextEntryEdit::codepointCount(
								entry->getText()));
						entryOk = false;
					}
					// Return submits (latches once) and blurs
					pressKey(SDL_SCANCODE_RETURN);
					if (entryOk && !entry->wasSubmitted())
					{
						SDL_Log("hello_orkige: FAILED - Return did not submit");
						entryOk = false;
					}
					if (entryOk && entry->wasSubmitted())
					{
						SDL_Log("hello_orkige: FAILED - wasSubmitted did not "
							"clear after one poll");
						entryOk = false;
					}
					if (entryOk && entry->isFocused())
					{
						SDL_Log("hello_orkige: FAILED - submit did not blur the "
							"text entry");
						entryOk = false;
					}
				}
				if (entryOk && !render->renderOneFrame())
				{
					SDL_Log("hello_orkige: FAILED - text-entry frame did not "
						"render");
					entryOk = false;
				}
				gui.disableInputEvents();
			}
			if (!entryOk)
			{
				return 1;
			}
			SDL_Log("hello_orkige: text-entry selfcheck passed (focus, typing, "
				"backspace, caret move, max length, submit)");
		}

		// ORKIGE_DEMO_GUI_MODAL=1: the modal / disabled-state / toggle-group /
		// toast / dropdown selfcheck (flavor-neutral, gui on DrawLayer2D). Loads
		// a declarative screen with a [Modal] + a [ToggleGroup] and drives it via
		// synthetic SDL events on the REAL input path (InputManager::injectEvent
		// -> GuiManager routing). Asserts: the consuming scrim blocks a press
		// bound for a button UNDER it; the dialog button on the modal layer still
		// fires; dismissing the modal frees its widgets and re-enables the layers
		// below; a disabled button never fires; a toggle group enforces
		// single-selection; a toast surfaces; a dropdown opens, picks and closes.
		if (std::getenv("ORKIGE_DEMO_GUI_MODAL"))
		{
			Orkige::PlatformWindow::setContentScaleOverride(1.0f);
			render->addResourceLocation(ORKIGE_DEMO_GUI_ATLAS_DIR);
			render->addResourceLocation(ORKIGE_DEMO_OUI_DIR);
			render->initialiseResourceGroups();

			bool modalOk = true;
			{
				Orkige::optr<Orkige::GuiFactory> factory =
					Orkige::onew(new Orkige::GuiFactory());
				Orkige::GuiManager gui(factory, "gui_default");
				gui.enableInputEvents();
				factory->loadLayout("modal_screen.oui");
				render->renderOneFrame();	// resolve + first frame

				auto clickAt = [&](float x, float y)
				{
					SDL_Event down{};
					down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
					down.button.button = SDL_BUTTON_LEFT;
					down.button.down = true;
					down.button.x = x;
					down.button.y = y;
					inputManager.injectEvent(down);
					SDL_Event up{};
					up.type = SDL_EVENT_MOUSE_BUTTON_UP;
					up.button.button = SDL_BUTTON_LEFT;
					up.button.down = false;
					up.button.x = x;
					up.button.y = y;
					inputManager.injectEvent(up);
				};
				auto centerOf = [&](const char* id, float& cx, float& cy) -> bool
				{
					if (!gui.widgetExists(id))
					{
						return false;
					}
					Orkige::optr<Orkige::GuiWidget> w = gui.getWidget(id).lock();
					if (!w)
					{
						return false;
					}
					const Orkige::Vec2 p = w->getPosition();
					const Orkige::Vec2 s = w->getSize();
					cx = p.x + s.x * 0.5f;
					cy = p.y + s.y * 0.5f;
					return true;
				};
				auto button = [&](const char* id)
				{
					return gui.getWidgetAs<Orkige::GuiButton>(id).lock();
				};

				if (!gui.isModalActive())
				{
					SDL_Log("hello_orkige: FAILED - modal not active after load");
					modalOk = false;
				}

				// a press on the background button UNDER the scrim must NOT fire
				float bx = 0.0f, by = 0.0f;
				if (modalOk && centerOf("bgButton", bx, by))
				{
					clickAt(bx, by);
					Orkige::optr<Orkige::GuiButton> bg = button("bgButton");
					if (bg && bg->wasClicked())
					{
						SDL_Log("hello_orkige: FAILED - background button fired "
							"under the modal scrim");
						modalOk = false;
					}
				}
				// the dialog button on the modal's content layer DOES fire
				float dx = 0.0f, dy = 0.0f;
				if (modalOk && centerOf("dlgOk", dx, dy))
				{
					clickAt(dx, dy);
					Orkige::optr<Orkige::GuiButton> ok = button("dlgOk");
					if (!(ok && ok->wasClicked()))
					{
						SDL_Log("hello_orkige: FAILED - dialog button did not fire");
						modalOk = false;
					}
				}
				// dismiss the modal; the frame boundary tears down its widgets
				gui.dismissModal("dlg");
				render->renderOneFrame();
				if (modalOk && gui.isModalActive())
				{
					SDL_Log("hello_orkige: FAILED - modal still active after "
						"dismiss");
					modalOk = false;
				}
				if (modalOk && gui.widgetExists("dlgOk"))
				{
					SDL_Log("hello_orkige: FAILED - dialog widget not torn down");
					modalOk = false;
				}
				// with the modal gone, the background button works again
				if (modalOk && centerOf("bgButton", bx, by))
				{
					clickAt(bx, by);
					Orkige::optr<Orkige::GuiButton> bg = button("bgButton");
					if (!(bg && bg->wasClicked()))
					{
						SDL_Log("hello_orkige: FAILED - background button did not "
							"fire after the modal closed");
						modalOk = false;
					}
				}
				// a disabled button never fires (gated in the dispatch loop)
				float lx = 0.0f, ly = 0.0f;
				if (modalOk && centerOf("lockedButton", lx, ly))
				{
					clickAt(lx, ly);
					Orkige::optr<Orkige::GuiButton> locked = button("lockedButton");
					if (locked && locked->wasClicked())
					{
						SDL_Log("hello_orkige: FAILED - disabled button fired");
						modalOk = false;
					}
				}
				// toggle group: optLow preselected; tapping optMed steals it
				Orkige::optr<Orkige::GuiCheckBox> low =
					gui.getWidgetAs<Orkige::GuiCheckBox>("optLow").lock();
				Orkige::optr<Orkige::GuiCheckBox> med =
					gui.getWidgetAs<Orkige::GuiCheckBox>("optMed").lock();
				if (modalOk && (!low || !med))
				{
					SDL_Log("hello_orkige: FAILED - toggle-group checkboxes "
						"missing");
					modalOk = false;
				}
				if (modalOk && !low->isChecked())
				{
					SDL_Log("hello_orkige: FAILED - toggle group did not "
						"preselect index 0");
					modalOk = false;
				}
				float mx = 0.0f, my = 0.0f;
				if (modalOk && centerOf("optMed", mx, my))
				{
					clickAt(mx, my);
					if (!med->isChecked() || low->isChecked())
					{
						SDL_Log("hello_orkige: FAILED - single-selection not "
							"enforced (low %d, med %d)", low->isChecked(),
							med->isChecked());
						modalOk = false;
					}
				}
				Orkige::optr<Orkige::GuiToggleGroup> group =
					gui.getToggleGroup("quality").lock();
				if (modalOk && (!group || group->getSelected() != 1))
				{
					SDL_Log("hello_orkige: FAILED - group selected index != 1");
					modalOk = false;
				}

				// toast: queue one and tick a frame; it should be on screen
				gui.showToast("Saved", 1.0f);
				render->renderOneFrame();
				if (modalOk && !gui.isToastVisible())
				{
					SDL_Log("hello_orkige: FAILED - toast did not surface");
					modalOk = false;
				}

				// showConfirm: a built dialog (panel + message + Yes/No) resolves
				// via getDialogResult and dismisses itself on a click
				if (modalOk)
				{
					Orkige::String confirmId = gui.showConfirm("Reset?",
						"Erase all settings?", "Yes", "No");
					render->renderOneFrame();
					if (!gui.isModalActive())
					{
						SDL_Log("hello_orkige: FAILED - showConfirm did not raise "
							"a modal");
						modalOk = false;
					}
					// Escape must NOT close a confirm dialog (not light-
					// dismissable) - it has to be answered by a button
					{
						SDL_Event esc{};
						esc.type = SDL_EVENT_KEY_DOWN;
						esc.key.scancode = SDL_SCANCODE_ESCAPE;
						esc.key.down = true;
						inputManager.injectEvent(esc);
						render->renderOneFrame();
						if (modalOk && !gui.isModalActive())
						{
							SDL_Log("hello_orkige: FAILED - Escape closed a "
								"non-light-dismissable confirm dialog");
							modalOk = false;
						}
					}
					float yx = 0.0f, yy = 0.0f;
					if (modalOk && centerOf((confirmId + ".yes").c_str(), yx, yy))
					{
						clickAt(yx, yy);
						render->renderOneFrame();	// poll -> result + dismiss + drain
						if (gui.getDialogResult(confirmId) !=
							Orkige::GuiManager::DR_YES)
						{
							SDL_Log("hello_orkige: FAILED - confirm Yes did not "
								"resolve to DR_YES");
							modalOk = false;
						}
						if (gui.isModalActive())
						{
							SDL_Log("hello_orkige: FAILED - confirm dialog stayed "
								"up after a choice");
							modalOk = false;
						}
					}
				}

				// dropdown: opens a list on a light-dismiss modal, picks, closes
				Orkige::woptr<Orkige::GuiDropDown> dropWeak =
					factory->createDropDown("quality.dropdown", "button", 9,
						"Low", Orkige::Vec2(80.0f, 440.0f), Orkige::GuiLabel::LA_CENTER,
						Orkige::Vec2(220.0f, 44.0f), "", 2);
				Orkige::optr<Orkige::GuiDropDown> drop = dropWeak.lock();
				if (modalOk && drop)
				{
					Orkige::StringVector options;
					options.push_back("Low");
					options.push_back("Med");
					options.push_back("High");
					drop->setItems(options);
					float ddx = 0.0f, ddy = 0.0f;
					centerOf("quality.dropdown", ddx, ddy);
					clickAt(ddx, ddy);			// request open (deferred)
					render->renderOneFrame();	// deferred open + list resolve
					if (!drop->isMenuOpen() || !gui.isModalActive())
					{
						SDL_Log("hello_orkige: FAILED - dropdown did not open");
						modalOk = false;
					}
					else
					{
						// Escape closes the dropdown (it IS light-dismissable),
						// then re-open it for the pick
						SDL_Event esc{};
						esc.type = SDL_EVENT_KEY_DOWN;
						esc.key.scancode = SDL_SCANCODE_ESCAPE;
						esc.key.down = true;
						inputManager.injectEvent(esc);
						render->renderOneFrame();	// dismiss drains
						render->renderOneFrame();	// dropdown reflects closed
						if (drop->isMenuOpen() || gui.isModalActive())
						{
							SDL_Log("hello_orkige: FAILED - Escape did not close "
								"the light-dismissable dropdown");
							modalOk = false;
						}
						clickAt(ddx, ddy);			// re-open
						render->renderOneFrame();
						// pick the third option (index 2)
						float ox = 0.0f, oy = 0.0f;
						if (centerOf("quality.dropdown.menu.opt2", ox, oy))
						{
							clickAt(ox, oy);
							render->renderOneFrame();	// pollOptions picks + closes
							render->renderOneFrame();	// dismissal drains
							if (drop->getSelectedIndex() != 2)
							{
								SDL_Log("hello_orkige: FAILED - dropdown pick did "
									"not select index 2 (got %zu)",
									drop->getSelectedIndex());
								modalOk = false;
							}
							if (drop->isMenuOpen())
							{
								SDL_Log("hello_orkige: FAILED - dropdown did not "
									"close after a pick");
								modalOk = false;
							}
						}
					}
				}
				// subtree disable: disabling a container disables its children.
				// Author a panel with a button parented under it; disabling the
				// panel makes the child input-inert too (effective-enabled walk).
				if (modalOk)
				{
					Orkige::optr<Orkige::GuiDecorWidget> panel =
						factory->createDecorWidget("subPanel", "none",
							Orkige::Vec2(520.0f, 80.0f), Orkige::Vec2(200.0f, 60.0f),
							"", 2).lock();
					Orkige::woptr<Orkige::GuiButton> childWeak =
						factory->createButton("subChild", "button", 9, "Child",
							Orkige::Vec2(10.0f, 10.0f), Orkige::GuiLabel::LA_CENTER,
							Orkige::Vec2(180.0f, 40.0f), "", 2, false,
							Orkige::GuiButtonBlink::BBLINK_NONE);
					Orkige::optr<Orkige::GuiButton> child = childWeak.lock();
					if (panel && child)
					{
						child->setParent(panel);
						render->renderOneFrame();	// resolve the child inside panel
						float cx = 0.0f, cy = 0.0f;
						centerOf("subChild", cx, cy);
						// enabled: the child fires
						clickAt(cx, cy);
						if (!child->wasClicked())
						{
							SDL_Log("hello_orkige: FAILED - parented child did not "
								"fire while enabled");
							modalOk = false;
						}
						// disabling the PANEL makes the child inert (subtree)
						panel->setEnabled(false);
						clickAt(cx, cy);
						if (child->wasClicked())
						{
							SDL_Log("hello_orkige: FAILED - child fired while its "
								"parent panel was disabled (subtree not gated)");
							modalOk = false;
						}
					}
				}

				gui.disableInputEvents();
			}
			Orkige::PlatformWindow::setContentScaleOverride(0.0f);
			if (!modalOk)
			{
				return 1;
			}
			SDL_Log("hello_orkige: gui-modal selfcheck passed (scrim blocks input "
				"below, dialog button fires, Escape spares a confirm but closes a "
				"dropdown, dismiss frees widgets, disabled + subtree inert, toggle "
				"group single-selection, toast, dropdown open-on-press/pick)");
		}

		// ORKIGE_DEMO_GUI_FLOW=1: the screen-stack (screen-flow router) selfcheck.
		// Registers a title + settings screen (.oui) and drives title -> settings
		// -> back through the router, with a synthetic Android-back key on the REAL
		// input path. Asserts: a screen's enter transition fires; a push tears the
		// covered screen's widgets down once its EXIT transition finishes (not
		// instantly); the back key pops the stack (the popped screen's widgets go,
		// the revealed screen is rebuilt from its .oui); and a back at the root
		// screen is NOT consumed. Flavor-neutral (gui on DrawLayer2D) - classic +
		// next must agree.
		if (std::getenv("ORKIGE_DEMO_GUI_FLOW"))
		{
			Orkige::PlatformWindow::setContentScaleOverride(1.0f);
			render->addResourceLocation(ORKIGE_DEMO_GUI_ATLAS_DIR);
			render->addResourceLocation(ORKIGE_DEMO_OUI_DIR);
			render->initialiseResourceGroups();

			bool flowOk = true;
			{
				// a TweenManager makes the enter/exit transitions actually tick,
				// exactly as the player loop does (the editor never creates one, so
				// flows snap there); the router's teardown keys off these tweens.
				Orkige::TweenManager tweenManager;
				Orkige::optr<Orkige::GuiFactory> factory =
					Orkige::onew(new Orkige::GuiFactory());
				Orkige::GuiManager gui(factory, "gui_default");
				gui.enableInputEvents();

				auto fail = [&](const char* msg)
				{
					SDL_Log("hello_orkige: FAILED - gui-flow: %s", msg);
					flowOk = false;
				};
				// one "frame": tick the tweens (player-loop order) then render (the
				// FrameStarted reconcile tears finished-exit screens down)
				auto tick = [&](float dt)
				{
					tweenManager.update(dt);
					render->renderOneFrame();
				};
				// advance frames until a widget is torn down (its exit transition
				// finished), bounded by a cap
				auto pumpUntilGone = [&](const char* id) -> bool
				{
					for (int i = 0; i < 240 && gui.widgetExists(id); ++i)
					{
						tick(0.25f);	// > the 0.2s fade, so a tick or two suffices
					}
					return !gui.widgetExists(id);
				};
				auto backKey = [&]()
				{
					SDL_Event e{};
					e.type = SDL_EVENT_KEY_DOWN;
					e.key.scancode = SDL_SCANCODE_AC_BACK;	// -> KC_WEBBACK (Android back)
					e.key.down = true;
					inputManager.injectEvent(e);
				};

				gui.registerScreen("title", "flow_title.oui");
				gui.registerScreen("settings", "flow_settings.oui");

				// push the title screen: its widgets build and the fade-in enter
				// transition fires (a fade starts the widget at effective alpha ~0)
				gui.pushScreen("title");
				render->renderOneFrame();	// resolve
				if (!gui.widgetExists("titleLabel") ||
					!gui.widgetExists("titleSettings"))
				{
					fail("title screen widgets not built on push");
				}
				if (gui.currentScreen() != "title" || gui.screenDepth() != 1)
				{
					fail("stack state wrong after push(title)");
				}
				if (flowOk)
				{
					Orkige::optr<Orkige::GuiWidget> w =
						gui.getWidget("titleLabel").lock();
					if (!w || w->getEffectiveAlpha() > 0.5f)
					{
						fail("title enter (fade-in) transition did not fire");
					}
				}

				// push settings over the title. The router is sequential: the title
				// plays its exit transition FIRST (still present this frame - proof
				// the exit animates, not an instant teardown), and the settings
				// screen builds only once the title has cleared.
				gui.pushScreen("settings");
				render->renderOneFrame();
				if (gui.currentScreen() != "settings" || gui.screenDepth() != 2)
				{
					fail("stack state wrong after push(settings)");
				}
				if (flowOk && !gui.widgetExists("titleLabel"))
				{
					fail("title torn down instantly - exit transition did not animate");
				}
				// let the title exit finish; the router tears its widgets down, then
				// builds the settings screen (same reconcile once the title clears)
				if (flowOk && !pumpUntilGone("titleLabel"))
				{
					fail("covered title widgets never torn down after its exit");
				}
				if (flowOk && !gui.widgetExists("settingsLabel"))
				{
					fail("settings screen not built after the title's exit");
				}

				// the Android back button pops settings -> back to the title
				backKey();
				if (gui.currentScreen() != "title" || gui.screenDepth() != 1)
				{
					fail("back key did not pop settings off the stack");
				}
				if (flowOk && !pumpUntilGone("settingsLabel"))
				{
					fail("popped settings widgets never torn down");
				}
				if (flowOk && (!gui.widgetExists("titleLabel") ||
					!gui.widgetExists("titleSettings")))
				{
					fail("title screen not rebuilt when revealed by the pop");
				}

				// back at the ROOT screen: the router does NOT consume it (Android
				// backgrounds the app instead); the title stays put
				if (flowOk && gui.handleScreenBack())
				{
					fail("back consumed at the root screen (should fall through)");
				}
				if (flowOk && gui.currentScreen() != "title")
				{
					fail("root screen popped empty on back");
				}

				gui.disableInputEvents();
			}
			Orkige::PlatformWindow::setContentScaleOverride(0.0f);
			if (!flowOk)
			{
				return 1;
			}
			SDL_Log("hello_orkige: gui-flow selfcheck passed (screen stack "
				"push/pop, enter+exit transitions fire, covered/popped screens "
				"torn down, back key pops, root back falls through)");
		}

		// ORKIGE_DEMO_GUI_MATRIX=1: the widget interaction MATRIX selfcheck. Boots
		// a real GuiManager, builds every widget type from a .oui AND imperatively,
		// drives each with synthetic input on the REAL input path, and asserts its
		// state/geometry/response - PLUS the animation layer: a widget property
		// tween, the scale/rotation render transform, cascading group alpha (with
		// the below-threshold hit-test gate), a show/hide transition, button press
		// feedback and scroll-view flick momentum. Flavor-neutral (gui draws on
		// DrawLayer2D), so it runs on classic + next and both must agree.
		if (std::getenv("ORKIGE_DEMO_GUI_MATRIX"))
		{
			Orkige::PlatformWindow::setContentScaleOverride(1.0f);
			render->addResourceLocation(ORKIGE_DEMO_GUI_ATLAS_DIR);
			render->addResourceLocation(ORKIGE_DEMO_OUI_DIR);
			render->initialiseResourceGroups();

			bool ok = true;
			// a TweenManager makes the widget animations tick, exactly as the
			// player loop does; the editor never creates one (gui animation
			// dormant in edit mode for free)
			Orkige::TweenManager tweenManager;
			{
				Orkige::optr<Orkige::GuiFactory> factory =
					Orkige::onew(new Orkige::GuiFactory());
				Orkige::GuiManager gui(factory, "gui_default");
				gui.enableInputEvents();

				auto fail = [&](const char* msg)
				{
					SDL_Log("hello_orkige: FAILED - gui-matrix: %s", msg);
					ok = false;
				};
				auto clickAt = [&](float x, float y)
				{
					SDL_Event down{};
					down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
					down.button.button = SDL_BUTTON_LEFT;
					down.button.down = true;
					down.button.x = x; down.button.y = y;
					inputManager.injectEvent(down);
					SDL_Event up{};
					up.type = SDL_EVENT_MOUSE_BUTTON_UP;
					up.button.button = SDL_BUTTON_LEFT;
					up.button.down = false;
					up.button.x = x; up.button.y = y;
					inputManager.injectEvent(up);
				};
				auto pressAt = [&](float x, float y)		// down only (no release)
				{
					SDL_Event down{};
					down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
					down.button.button = SDL_BUTTON_LEFT;
					down.button.down = true;
					down.button.x = x; down.button.y = y;
					inputManager.injectEvent(down);
				};
				auto releaseAt = [&](float x, float y)
				{
					SDL_Event up{};
					up.type = SDL_EVENT_MOUSE_BUTTON_UP;
					up.button.button = SDL_BUTTON_LEFT;
					up.button.down = false;
					up.button.x = x; up.button.y = y;
					inputManager.injectEvent(up);
				};
				auto moveTo = [&](float x, float y)
				{
					SDL_Event mv{};
					mv.type = SDL_EVENT_MOUSE_MOTION;
					mv.motion.x = x; mv.motion.y = y;
					inputManager.injectEvent(mv);
				};
				auto centerOf = [&](const char* id, float& cx, float& cy) -> bool
				{
					if (!gui.widgetExists(id)) { return false; }
					Orkige::optr<Orkige::GuiWidget> w = gui.getWidget(id).lock();
					if (!w) { return false; }
					const Orkige::Vec2 p = w->getPosition();
					const Orkige::Vec2 s = w->getSize();
					cx = p.x + s.x * 0.5f; cy = p.y + s.y * 0.5f;
					return true;
				};
				// one "frame": tick the tweens (player-loop order) then render
				auto tick = [&](float dt)
				{
					tweenManager.update(dt);
					render->renderOneFrame();
				};

				// === 1. the .oui grammar builds every declarable widget type ===
				factory->loadLayout("matrix_screen.oui");
				render->renderOneFrame();
				const char* ouiIds[] = { "mtxPanel", "mtxLabel", "mtxButton",
					"mtxCheck", "mtxSlider", "mtxSelect", "mtxProgress",
					"mtxEntry", "mtxDrop" };
				for (const char* id : ouiIds)
				{
					if (!gui.widgetExists(id))
					{
						fail(id);
					}
				}
				// every widget resolved to a positive on-screen size
				for (const char* id : ouiIds)
				{
					if (ok && gui.widgetExists(id))
					{
						Orkige::optr<Orkige::GuiWidget> w =
							gui.getWidget(id).lock();
						if (w && (w->getSize().x <= 0.0f || w->getSize().y <= 0.0f))
						{
							fail("a widget resolved to a non-positive size");
						}
					}
				}

				// === 2. per-widget interaction on the real input path ===
				// button click -> wasClicked (single-consumer poll) AND
				// gui.clicked on the MESSAGE BUS (multi-consumer). The two
				// channels coexist: the poll latch and TWO bus subscribers all
				// see ONE real click - the multi-consumer gap the poll
				// convention leaves, closed end to end on a real gui.
				{
					Orkige::ScriptComponent::ensureScriptApi();
					Orkige::ScriptEventBus::getSingleton().clear();
					scriptRuntime.runString(
						"shared.busA = 0; shared.busB = 0; shared.busId = ''\n"
						"events.subscribe('gui.clicked', function(e)\n"
						"  shared.busA = shared.busA + 1; shared.busId = e.id end)\n"
						"events.subscribe('gui.clicked', function(e)\n"
						"  shared.busB = shared.busB + 1 end)\n");

					float x, y; centerOf("mtxButton", x, y); clickAt(x, y);
					Orkige::optr<Orkige::GuiButton> b =
						gui.getWidgetAs<Orkige::GuiButton>("mtxButton").lock();
					if (ok && !(b && b->wasClicked())) { fail("button click"); }

					// the SAME click reached both bus subscribers: the gui queued a
					// gui.clicked onto GlobalEventManager during input dispatch;
					// draining it (the player loop's script-phase tick, driven
					// here since the sample has no player loop) fans it out
					if (Orkige::GlobalEventManager::getSingletonPtr())
					{
						Orkige::GlobalEventManager::getSingleton().tick();
					}
					if (ok && (scriptRuntime.getNumber({ "shared", "busA" }, -1.0) != 1.0 ||
						scriptRuntime.getNumber({ "shared", "busB" }, -1.0) != 1.0 ||
						scriptRuntime.getString({ "shared", "busId" }, "") != "mtxButton"))
					{
						fail("gui.clicked bus mirror (two subscribers)");
					}
					Orkige::ScriptEventBus::getSingleton().clear();
				}
				// checkbox toggles
				{
					Orkige::optr<Orkige::GuiCheckBox> c =
						gui.getWidgetAs<Orkige::GuiCheckBox>("mtxCheck").lock();
					const bool before = c && c->isChecked();
					float x, y; centerOf("mtxCheck", x, y); clickAt(x, y);
					if (ok && !(c && c->isChecked() != before))
					{
						fail("checkbox toggle");
					}
				}
				// slider value math: selectItemIndex round-trips through the getter
				{
					Orkige::optr<Orkige::GuiSlider> s =
						gui.getWidgetAs<Orkige::GuiSlider>("mtxSlider").lock();
					if (s)
					{
						Orkige::StringVector items;
						items.push_back("0"); items.push_back("1");
						items.push_back("2"); items.push_back("3");
						s->setItems(items);
						s->selectItemIndex(2, false);
						if (ok && s->getSelectedItemIndex() != 2)
						{
							fail("slider value index");
						}
					}
				}
				// select-menu cycles through its options and wraps
				{
					Orkige::optr<Orkige::GuiSelectMenu> sm =
						gui.getWidgetAs<Orkige::GuiSelectMenu>("mtxSelect").lock();
					if (sm)
					{
						Orkige::StringVector items;
						items.push_back("A"); items.push_back("B");
						items.push_back("C");
						sm->setItems(items);
						sm->selectItemIndex(2, false);
						if (ok && sm->getSelectedItem() != "C")
						{
							fail("select-menu value");
						}
					}
				}
				// progress bar reflects its set value
				{
					Orkige::optr<Orkige::GuiProgressBar> pb =
						gui.getWidgetAs<Orkige::GuiProgressBar>("mtxProgress").lock();
					if (pb)
					{
						pb->setProgress(50.0f);		// 0..100 scale
						if (ok && std::fabs(pb->getProgress() - 50.0f) > 0.001f)
						{
							fail("progress bar value");
						}
					}
				}
				// text entry: a tap focuses it, typed text lands
				{
					Orkige::optr<Orkige::GuiTextEntry> te =
						gui.getWidgetAs<Orkige::GuiTextEntry>("mtxEntry").lock();
					float x, y; centerOf("mtxEntry", x, y); clickAt(x, y);
					render->renderOneFrame();
					if (te)
					{
						te->onTextInput("Hi");
						if (ok && te->getText() != "Hi")
						{
							fail("text entry typing");
						}
					}
				}
				// dropdown opens a list on a modal and picks
				{
					Orkige::optr<Orkige::GuiDropDown> dd =
						gui.getWidgetAs<Orkige::GuiDropDown>("mtxDrop").lock();
					float x, y; centerOf("mtxDrop", x, y);
					clickAt(x, y);					// request open (deferred)
					render->renderOneFrame();		// deferred open + list resolve
					if (ok && !(dd && dd->isMenuOpen()))
					{
						fail("dropdown open");
					}
					float ox, oy;
					if (ok && centerOf("mtxDrop.menu.opt1", ox, oy))
					{
						clickAt(ox, oy);
						render->renderOneFrame();
						render->renderOneFrame();
						if (dd && dd->getSelectedIndex() != 1)
						{
							fail("dropdown pick");
						}
					}
				}
				// toast surfaces
				{
					gui.showToast("Matrix", 1.0f);
					render->renderOneFrame();
					if (ok && !gui.isToastVisible()) { fail("toast surface"); }
				}

				// === 3. the animation layer ===
				// (a) a widget alpha tween moves the group alpha over time
				{
					float to = 0.25f;
					gui.tweenWidget("mtxButton", Orkige::GuiManager::WTC_Alpha,
						&to, 0.2f, 0);
					for (int f = 0; f < 15; ++f) { tick(1.0f / 60.0f); }
					Orkige::optr<Orkige::GuiWidget> b =
						gui.getWidget("mtxButton").lock();
					if (ok && (!b || std::fabs(b->getGroupAlpha() - 0.25f) > 0.02f))
					{
						fail("alpha tween did not reach the target");
					}
					// retarget-replace: a second alpha tween supersedes the first
					float back = 1.0f;
					gui.tweenWidget("mtxButton", Orkige::GuiManager::WTC_Alpha,
						&back, 0.2f, 0);
					for (int f = 0; f < 15; ++f) { tick(1.0f / 60.0f); }
					if (ok && b && std::fabs(b->getGroupAlpha() - 1.0f) > 0.02f)
					{
						fail("alpha tween retarget");
					}
				}
				// (b) the render transform: scale/rotation take hold
				{
					Orkige::optr<Orkige::GuiWidget> b =
						gui.getWidget("mtxButton").lock();
					if (b)
					{
						b->setRenderScale(1.5f, 1.5f);
						b->setRenderRotation(30.0f);
						render->renderOneFrame();
						if (ok && (std::fabs(b->getRenderScaleX() - 1.5f) > 0.001f ||
							std::fabs(b->getRenderRotation() - 30.0f) > 0.001f))
						{
							fail("render transform not stored");
						}
						b->setRenderScale(1.0f, 1.0f);
						b->setRenderRotation(0.0f);
					}
				}
				// (c) cascading alpha: a panel fade dims its parented child, and
				// once effectively invisible the subtree stops hit-testing
				{
					Orkige::optr<Orkige::GuiWidget> panel =
						gui.getWidget("mtxPanel").lock();
					Orkige::optr<Orkige::GuiWidget> label =
						gui.getWidget("mtxLabel").lock();
					if (panel && label)
					{
						panel->setGroupAlpha(0.3f);
						render->renderOneFrame();		// runs the cascade pass
						if (ok && std::fabs(label->getEffectiveAlpha() - 0.3f) > 0.001f)
						{
							fail("cascade alpha did not reach the child");
						}
						panel->setGroupAlpha(1.0f);
						render->renderOneFrame();
					}
				}
				// hit-test gate: a button under a fully-faded panel is inert
				{
					Orkige::optr<Orkige::GuiDecorWidget> gatePanel =
						factory->createDecorWidget("gatePanel", "none",
							Orkige::Vec2(40.0f, 300.0f), Orkige::Vec2(200.0f, 60.0f),
							"", 3).lock();
					Orkige::woptr<Orkige::GuiButton> gateChildW =
						factory->createButton("gateChild", "button", 9, "Gate",
							Orkige::Vec2(50.0f, 310.0f), Orkige::GuiLabel::LA_CENTER,
							Orkige::Vec2(180.0f, 40.0f), "", 3, false,
							Orkige::GuiButtonBlink::BBLINK_NONE);
					Orkige::optr<Orkige::GuiButton> gateChild = gateChildW.lock();
					if (gatePanel && gateChild)
					{
						gateChild->setParent(gatePanel);
						gatePanel->setGroupAlpha(0.0f);	// fully faded subtree
						render->renderOneFrame();
						float x, y; centerOf("gateChild", x, y);
						clickAt(x, y);
						if (ok && gateChild->wasClicked())
						{
							fail("faded-out subtree still hit-tested");
						}
					}
				}
				// (d) show/hide transition: hide parks the panel invisible, show
				// brings it back (the .oui declared `transition = fade 0.2`)
				{
					Orkige::optr<Orkige::GuiWidget> panel =
						gui.getWidget("mtxPanel").lock();
					if (panel)
					{
						gui.playWidgetTransition("mtxPanel", false);	// hide
						for (int f = 0; f < 18; ++f) { tick(1.0f / 60.0f); }
						if (ok && panel->getEffectiveAlpha() > 0.05f)
						{
							fail("hide transition did not park the panel hidden");
						}
						gui.playWidgetTransition("mtxPanel", true);		// show
						for (int f = 0; f < 18; ++f) { tick(1.0f / 60.0f); }
						if (ok && std::fabs(panel->getEffectiveAlpha() - 1.0f) > 0.05f)
						{
							fail("show transition did not restore the panel");
						}
					}
				}
				// (e) button press feedback: a press scales the button down, a
				// release springs it back toward 1
				{
					Orkige::optr<Orkige::GuiButton> b =
						gui.getWidgetAs<Orkige::GuiButton>("mtxButton").lock();
					if (b)
					{
						b->setPressFeedback(true);
						float x, y; centerOf("mtxButton", x, y);
						pressAt(x, y);
						render->renderOneFrame();
						if (ok && b->getRenderScaleX() >= 1.0f)
						{
							fail("press feedback did not scale down on press");
						}
						releaseAt(x, y);
						for (int f = 0; f < 20; ++f) { tick(1.0f / 60.0f); }
						if (ok && std::fabs(b->getRenderScaleX() - 1.0f) > 0.02f)
						{
							fail("press feedback did not spring back on release");
						}
						b->setPressFeedback(false);
						(void)b->wasClicked();	// clear the click latch
					}
				}
				// (f) scroll-view flick momentum: a tall content subtree scrolls
				// by drag, and the flick keeps coasting after release
				{
					Orkige::woptr<Orkige::GuiScrollView> svW =
						factory->createScrollView("mtxScroll",
							Orkige::Vec2(700.0f, 60.0f), Orkige::Vec2(180.0f, 160.0f),
							"", 4);
					Orkige::optr<Orkige::GuiScrollView> sv = svW.lock();
					// a content panel taller than the viewport, parented under it.
					// The viewport opts into layout as a top-left-anchored root (so
					// the resolver hands it its content extent); the content pins to
					// the viewport top and its preferred height (500) overflows.
					Orkige::optr<Orkige::GuiDecorWidget> content =
						factory->createDecorWidget("mtxScrollContent", "none",
							Orkige::Vec2(0.0f, 0.0f), Orkige::Vec2(180.0f, 500.0f),
							"", 4).lock();
					if (sv && content)
					{
						sv->setAnchorPreset("topleft");
						sv->setOffsets(700.0f, 60.0f, 880.0f, 220.0f);
						content->setParent(sv);
						content->setAnchorPreset("stretchtop");
						content->setOffsets(0.0f, 0.0f, 0.0f, 0.0f);
						content->setContentSizeFit("none", "preferred");
						render->renderOneFrame();		// resolve the extents
						if (ok && sv->getMaxScroll() <= 0.0f)
						{
							fail("scroll view has no scrollable range");
						}
						// drag upward over a few frames to build a flick velocity
						pressAt(790.0f, 200.0f);
						for (int f = 0; f < 5; ++f)
						{
							moveTo(790.0f, 200.0f - 20.0f * (f + 1));
							tick(1.0f / 60.0f);
						}
						const float dragged = sv->getScroll();
						if (ok && dragged >= 0.0f)
						{
							fail("scroll drag did not move the content");
						}
						releaseAt(790.0f, 100.0f);
						const float released = sv->getScroll();
						tick(1.0f / 60.0f);
						if (ok && sv->getScroll() > released)	// still coasting
						{
							fail("scroll flick did not coast after release");
						}
						// it settles within the legal range
						for (int f = 0; f < 240; ++f) { tick(1.0f / 60.0f); }
						if (ok && (sv->getScroll() < -sv->getMaxScroll() - 1.0f ||
							sv->getScroll() > 1.0f))
						{
							fail("scroll momentum settled out of bounds");
						}
					}
				}

				gui.disableInputEvents();
			}

			// === 4. the UI PERFORMANCE CONTRACT (mobile is the target: one draw
			// per screen per atlas, dirty-tracked, zero steady-state allocation -
			// made enforceable). A fresh, controlled screen so the batch counts
			// are deterministic. ===
			{
				tweenManager.clear();	// retire the interaction scope's tweens
				Orkige::optr<Orkige::GuiFactory> factory =
					Orkige::onew(new Orkige::GuiFactory());
				Orkige::GuiManager gui(factory, "gui_default");
				auto perfFail = [&](const char* msg)
				{
					SDL_Log("hello_orkige: FAILED - gui-perf: %s", msg);
					ok = false;
				};
				auto tick = [&](float dt)
				{
					tweenManager.update(dt);
					render->renderOneFrame();
				};

				// a busy screen: every widget type, one atlas
				factory->loadLayout("matrix_screen.oui");
				render->renderOneFrame();	// resolve + first rebuild

				// (1) BATCH COUNT: one atlas, no scissor => exactly ONE draw batch
				if (ok && gui.getLastBatchCount() != 1)
				{
					perfFail("a busy single-atlas screen is not exactly 1 batch");
				}
				// a modal on top (scrim + dialog ride the same atlas, no scissor)
				// => still 1 batch
				gui.showModal("perfModal", false);
				render->renderOneFrame();
				if (ok && gui.getLastBatchCount() != 1)
				{
					perfFail("a modal changed the batch count (should share the atlas)");
				}
				gui.dismissModal("perfModal");
				render->renderOneFrame();
				// an open scroll view adds its ONE scissored segment => 2 batches
				{
					Orkige::optr<Orkige::GuiScrollView> sv =
						factory->createScrollView("perfScroll",
							Orkige::Vec2(700.0f, 60.0f), Orkige::Vec2(160.0f, 140.0f),
							"", 7).lock();
					Orkige::optr<Orkige::GuiDecorWidget> content =
						factory->createDecorWidget("perfScrollContent", "none",
							Orkige::Vec2(0.0f, 0.0f), Orkige::Vec2(160.0f, 400.0f),
							"", 7).lock();
					if (sv && content)
					{
						sv->setAnchorPreset("topleft");
						sv->setOffsets(700.0f, 60.0f, 860.0f, 200.0f);
						content->setParent(sv);
						content->setAnchorPreset("stretchtop");
						content->setOffsets(0.0f, 0.0f, 0.0f, 0.0f);
						content->setContentSizeFit("none", "preferred");
						render->renderOneFrame();
						if (ok && gui.getLastBatchCount() != 2)
						{
							perfFail("an open scroll region is not exactly +1 batch");
						}
						gui.destroyWidget("perfScrollContent");
						gui.destroyWidget("perfScroll");
						render->renderOneFrame();
					}
				}
				// a widget on a SECOND atlas = a second screen = +1 batch (a solid
				// decor needs no font/sprite from that atlas, just the white pixel)
				{
					factory->createDecorWidget("perfSecondAtlas", "none",
						Orkige::Vec2(600.0f, 400.0f), Orkige::Vec2(50.0f, 50.0f),
						"gui_ttf_demo", 2);
					render->renderOneFrame();
					if (ok && gui.getLastBatchCount() != 2)
					{
						perfFail("a second atlas is not exactly +1 batch");
					}
					gui.destroyViewWithWidgets("gui_ttf_demo");
					render->renderOneFrame();
					if (ok && gui.getLastBatchCount() != 1)
					{
						perfFail("removing the second atlas did not return to 1 batch");
					}
				}

				// (2) DIRTY TRACKING (rebuild counter deltas)
				// a fully static screen: ZERO rebuilds over N frames
				{
					// warm up past any settling from the modal/scroll teardown above,
					// then a fully static screen must not rebuild AT ALL
					for (int f = 0; f < 40; ++f) { render->renderOneFrame(); }
					const size_t r0 = gui.getRebuildCount();
					for (int f = 0; f < 40; ++f) { render->renderOneFrame(); }
					if (ok && gui.getRebuildCount() != r0)
					{
						perfFail("a static screen rebuilt with no content change");
					}
					// change ONE label: exactly one rebuild, then static again
					Orkige::optr<Orkige::GuiLabel> label =
						gui.getWidgetAs<Orkige::GuiLabel>("mtxLabel").lock();
					if (label)
					{
						const size_t g0 = gui.getGeometryRebuildCount();
						label->setText("Changed");
						render->renderOneFrame();
						if (ok && gui.getRebuildCount() != r0 + 1)
						{
							perfFail("one label change was not exactly one rebuild");
						}
						// the label's caption actually re-tessellated: a geometry rebuild
						if (ok && gui.getGeometryRebuildCount() <= g0)
						{
							perfFail("a text change did not rebuild any geometry");
						}
						for (int f = 0; f < 12; ++f) { render->renderOneFrame(); }
						if (ok && gui.getRebuildCount() != r0 + 1)
						{
							perfFail("the screen kept rebuilding after a settled change");
						}
					}
					// a RUNNING tween rebuilds each active frame, and STOPS at
					// completion (the regression the animation work could introduce)
					const size_t rBefore = gui.getRebuildCount();
					float to = 0.3f;
					gui.tweenWidget("mtxButton", Orkige::GuiManager::WTC_Alpha,
						&to, 0.15f, 0);
					for (int f = 0; f < 12; ++f) { tick(1.0f / 60.0f); }	// spans 0.15s
					if (ok && gui.getRebuildCount() <= rBefore)
					{
						perfFail("an active tween did not rebuild the screen");
					}
					const size_t rIdle = gui.getRebuildCount();
					for (int f = 0; f < 12; ++f) { tick(1.0f / 60.0f); }
					if (ok && gui.getRebuildCount() != rIdle)
					{
						perfFail("the screen kept rebuilding after the tween completed");
					}
					// THE POST-PASS DESIGN PROOF: a transform-only (rotation)
					// animation RESUBMITS the batch each active frame but rebuilds
					// ZERO geometry (the cached vertices are reused, the transform
					// rides on the emitted copy). Resubmits go up; geometry is flat.
					{
						const size_t resubBefore = gui.getRebuildCount();
						const size_t geomBefore = gui.getGeometryRebuildCount();
						float spin = 45.0f;
						gui.tweenWidget("mtxButton",
							Orkige::GuiManager::WTC_Rotation, &spin, 0.15f, 0);
						for (int f = 0; f < 12; ++f) { tick(1.0f / 60.0f); }
						if (ok && gui.getRebuildCount() <= resubBefore)
						{
							perfFail("a transform animation did not resubmit the batch");
						}
						if (ok && gui.getGeometryRebuildCount() != geomBefore)
						{
							perfFail("a transform-only animation rebuilt geometry "
								"(the post-pass design was violated)");
						}
					}
				}

				// (3) STEADY-STATE ALLOCATION: with a looping animation forcing a
				// rebuild EVERY frame, the retained scratch capacity is stable after
				// warmup (no per-frame reallocation - capacity approximates the
				// allocator contract, an honest note in the report).
				{
					float spin = 360.0f;
					Orkige::TweenManager::TweenId id = gui.tweenWidget("mtxButton",
						Orkige::GuiManager::WTC_Rotation, &spin, 1.0f,
						&Orkige::Ease::linear);
					tweenManager.setTweenLoops(id, -1, false);	// never settles
					for (int f = 0; f < 8; ++f) { tick(1.0f / 60.0f); }	// warmup
					const size_t cap = gui.getScratchCapacity();
					const size_t rebuildsBefore = gui.getRebuildCount();
					for (int f = 0; f < 40; ++f)
					{
						tick(1.0f / 60.0f);
						if (ok && gui.getScratchCapacity() != cap)
						{
							perfFail("the scratch buffer reallocated in steady state");
							break;
						}
					}
					if (ok && gui.getRebuildCount() <= rebuildsBefore + 30)
					{
						perfFail("the looping animation did not rebuild each frame");
					}
					gui.cancelWidgetTweens("mtxButton");
				}

				// (4) PERF BUDGET number: time the full gui update (layout resolve +
				// tween tick + vertex rebuild + submission) over M frames, forcing a
				// rebuild each frame, for a settings-scale screen and a 200-widget
				// stress. No hard threshold (machine-dependent) - the number lands
				// in the log + the report.
				auto timeHeavyGui = [&](const char* label, int frames)
				{
					auto t0 = std::chrono::steady_clock::now();
					for (int f = 0; f < frames; ++f)
					{
						gui.markLayoutDirty();		// force the full pipeline
						gui.profileTick(1.0f / 60.0f);
					}
					auto t1 = std::chrono::steady_clock::now();
					const double us =
						std::chrono::duration<double, std::micro>(t1 - t0).count()
						/ double(frames);
					SDL_Log("hello_orkige: gui-perf: %s full gui update = "
						"%.1f us/frame over %d frames", label, us, frames);
				};
				timeHeavyGui("settings-scale (matrix screen)", 200);
				// 200-widget stress
				for (int i = 0; i < 200; ++i)
				{
					char id[32];
					std::snprintf(id, sizeof(id), "stress%d", i);
					factory->createButton(id, "button", 9, "S",
						Orkige::Vec2(float((i % 20) * 40), float((i / 20) * 30)),
						Orkige::GuiLabel::LA_CENTER, Orkige::Vec2(36.0f, 26.0f), "", 8,
						false, Orkige::GuiButtonBlink::BBLINK_NONE);
				}
				render->renderOneFrame();
				timeHeavyGui("200-widget stress", 200);
			}
			Orkige::PlatformWindow::setContentScaleOverride(0.0f);
			if (!ok)
			{
				return 1;
			}
			SDL_Log("hello_orkige: gui-matrix selfcheck passed (every widget type "
				"built from .oui + imperatively and driven; alpha tween + retarget, "
				"render transform, cascading alpha + hit gate, show/hide transition, "
				"button press feedback, scroll flick momentum; perf contract: 1 batch/"
				"atlas +1 per scroll, dirty-tracked rebuilds, stable scratch, timed)");
		}

		// ORKIGE_DEMO_GUI_LUA=1: the FULLY-SCRIPTABLE contract. The whole widget
		// matrix is authored, driven and asserted IN LUA through the ScriptRuntime
		// surface; the C++ side only injects one synthetic input and reads the
		// probes. A missing/renamed binding raises a Lua error here, so the suite
		// fails BY CONSTRUCTION instead of by review. Flavor-neutral. Skipped in
		// ORKIGE_SCRIPTING=OFF builds (ScriptRuntime::available() is false).
		if (std::getenv("ORKIGE_DEMO_GUI_LUA") && Orkige::ScriptRuntime::available())
		{
			Orkige::PlatformWindow::setContentScaleOverride(1.0f);
			render->addResourceLocation(ORKIGE_DEMO_GUI_ATLAS_DIR);
			render->addResourceLocation(ORKIGE_DEMO_OUI_DIR);
			render->initialiseResourceGroups();
			Orkige::ScriptComponent::ensureScriptApi();	// the tween/guitween tables

			bool luaOk = true;
			// PHASE 1: build + exercise + assert EVERY gui capability from Lua
			const Orkige::ScriptRuntime::Result r1 = scriptRuntime.runString(R"lua(
				local LA = GuiLabel.LabelAlignment

				factory = GuiFactory()
				gui = GuiManager(factory, "gui_default", "General")
				gui:enableInputEvents()

				-- layout policy (design resolution / match / root space)
				gui:setDesignResolution(1280, 720, 0.5)
				gui:setLayoutMatchMode(0)
				gui:setRootSpace("fullwindow")
				assert(gui:getLayoutScale() > 0, "getLayoutScale")

				-- create EVERY widget type imperatively (atlas selection = arg)
				local label = factory:createLabel("luaLabel", 9, "Hi", Vector2(20,20), "", 2, false)
				luaBtn = factory:createButton("luaBtn", "button", 9, "Go", Vector2(20,60), LA.LA_CENTER, Vector2(200,48), "", 2, false, 0)
				local check = factory:createCheckBox("luaCheck", "checkbox", 9, "On", Vector2(20,120), LA.LA_LEFT, Vector2(200,40), "", 2, false)
				local slider = factory:createSlider("luaSlider", "luaSliderBtn", "button", 9, "Vol", Vector2(20,170), LA.LA_CENTER, Vector2(260,44), "", 2)
				local select = factory:createSelectMenu("luaSelect", "luaSelectBtn", "button", 9, "Mode", Vector2(20,220), LA.LA_CENTER, Vector2(260,44), "", 2)
				local prog = factory:createProgressBar("luaProg", "button", 9, "Load", Vector2(20,270), LA.LA_CENTER, Vector2(260,32), "", 2)
				local entry = factory:createTextEntry("luaEntry", "none", 9, "hint", Vector2(20,310), Vector2(260,44), "", 2, 16)
				local scroll = factory:createScrollView("luaScroll", Vector2(320,60), Vector2(180,160), "", 3)
				local drop = factory:createDropDown("luaDrop", "button", 9, "Pick", Vector2(320,240), LA.LA_CENTER, Vector2(220,44), "", 2)
				local panel = factory:createDecorWidget("luaPanel", "none", Vector2(560,60), Vector2(200,200), "", 2)

				-- widget-specific setters/getters (nine-slice / tiled modes too)
				check:setChecked(true, false); assert(check:isChecked(), "checkbox setChecked")
				check:toggle(false); assert(not check:isChecked(), "checkbox toggle")
				slider:setItemsString("0 | 1 | 2 | 3"); slider:selectItemIndex(2, false)
				assert(slider:getSelectedItemIndex() == 2, "slider index")
				select:setItemsString("A | B | C"); select:selectItemIndex(1, false)
				assert(select:getSelectedItem() == "B", "selectmenu value")
				prog:setProgress(50); assert(prog:getProgress() == 50, "progress")
				entry:setText("hi"); entry:setPlaceholder("type"); entry:setMaxLength(20)
				assert(entry:getText() == "hi", "text entry")
				assert(entry:isFocused() == false, "entry focus poll"); entry:wasSubmitted()
				drop:setItemsString("X | Y | Z"); assert(drop:getSelectedIndex() >= 0, "dropdown index")
				panel:setColour(0.2,0.3,0.4,1); panel:setAlpha(0.9)
				panel:setNineSlice(true); panel:setTiled(false); panel:setSprite("none")
				luaBtn:setPressFeedback(true); luaBtn:setNineSlice(false); luaBtn:setTiled(false)
				luaBtn:wasClicked(); luaBtn:getState()
				assert(luaBtn:getCaption() == "Go", "button caption"); luaBtn:setCaption("Go2")

				-- rect-anchor layout: anchors / pivots / offsets / groups / fit / safe area
				panel:setAnchorPreset("center"); panel:setAnchors(0.5,0.5,0.5,0.5)
				panel:setPivot(0.5,0.5); panel:setAnchoredPosition(0,0); panel:setSizeDelta(200,200)
				panel:setOffsets(0,0,0,0); panel:setUseSafeArea(false)
				panel:setLayoutGroup("vertical"); panel:setGroupPadding(8,8,8,8)
				panel:setGroupSpacing(10,0); panel:setChildAlignment("center")
				panel:setChildForceExpand(true); panel:setGridCellSize(100,100)
				panel:setGridConstraint("columns",2); panel:setContentSizeFit("none","preferred")
				-- setParent takes a base GuiWidget handle; findWidget returns exactly
				-- that (a derived userdata is not auto-upcast as a shared_ptr arg)
				label:setParent(gui:findWidget("luaPanel"))
				label:setEnabled(false); assert(not label:isEnabled(), "enabled"); label:setEnabled(true)
				local layer = luaBtn:getLayer(); layer:isVisible(); layer:setVisible(true)

				-- animation surface: transform, cascading alpha, transition
				luaBtn:setRenderScale(1.25,1.25)	-- float-stored: compare with tolerance
				assert(math.abs(luaBtn:getRenderScaleX() - 1.25) < 0.001, "render scale")
				assert(math.abs(luaBtn:getRenderScaleY() - 1.25) < 0.001, "render scale y")
				luaBtn:setRenderScale(1,1)
				luaBtn:setRenderRotation(15)
				assert(math.abs(luaBtn:getRenderRotation() - 15) < 0.001, "render rotation")
				luaBtn:setRenderRotation(0)
				panel:setGroupAlpha(0.5); assert(panel:getGroupAlpha() == 0.5, "group alpha")
				assert(math.abs(label:getEffectiveAlpha() - 0.5) < 0.001, "cascade alpha")
				panel:setGroupAlpha(1.0); luaBtn:setAlphaBlocksInput(true)
				panel:setTransition("fade 0.2")

				-- guitween: every helper returns a handle (:isActive/:cancel/:setLoops)
				local h = guitween.alpha("luaBtn", 0.5, 0.2, "quadOut")
				assert(h ~= nil, "guitween.alpha handle")
				h:isActive(); h:setLoops(2, true); h:cancel()
				guitween.scale("luaBtn", 1.3, 0.2)
				guitween.rotate("luaBtn", 90, 0.2)
				guitween.move("luaBtn", 30, 60, 0.2)
				guitween.size("luaBtn", 200, 48, 0.2)
				guitween.color("luaPanel", 1, 0, 0, 1, 0.2)
				guitween.stop("luaBtn")
				panel:setTransition("pop 0.2")
				assert(guitween.show("luaPanel"), "guitween.show")
				assert(guitween.hide("luaPanel"), "guitween.hide")

				-- modals: showModal + content z + register + dismiss; confirm/alert + result
				local mid = gui:showModal("luaModal", false)
				assert(gui:isModalActive(), "modal active")
				assert(gui:getModalContentZ(mid) > 0, "modal content z")
				-- a modal-owned widget (torn down WITH the modal - do NOT register a
				-- widget the rest of the matrix still needs)
				factory:createDecorWidget("luaModalPanel", "panel", Vector2(200,200), Vector2(300,150), "", gui:getModalContentZ(mid))
				gui:registerModalWidget(mid, "luaModalPanel")
				gui:getTopModalId(); gui:getModalCount()
				gui:dismissModal(mid)
				local cid = gui:showConfirm("T", "M", "Yes", "No")
				assert(gui:getDialogResult(cid) == 0, "dialog pending"); gui:dismissModal(cid)
				local aid = gui:showAlert("A", "B", "OK"); gui:dismissModal(aid)
				gui:dismissAllModals()

				-- toggle groups
				local grp = gui:createToggleGroup("luaGroup")
				grp:addMember(check); grp:setAllowNone(true); grp:setSelected(0)
				grp:getSelected(); grp:getMemberCount(); grp:pollChanged()
				assert(gui:getToggleGroup("luaGroup") ~= nil, "getToggleGroup")

				-- toast
				gui:showToast("Saved", 1.0); gui:isToastVisible()

				-- scroll offsets
				scroll:setScroll(-10); scroll:getScroll(); scroll:getMaxScroll()

				-- declarative authoring + the .oui<->Lua BRIDGE (findWidget by id)
				factory:loadLayout("matrix_screen.oui")
				local found = gui:findWidget("mtxButton")
				assert(found ~= nil, "findWidget could not reach an .oui-authored widget")
				found:setGroupAlpha(0.8)					-- wire behavior onto it
				guitween.alpha("mtxButton", 1.0, 0.2)		-- animate an .oui widget by id
				assert(gui:findWidget("nope#missing") == nil, "findWidget of a missing id must be nil")

				return true
			)lua");
			if (!r1.success)
			{
				SDL_Log("hello_orkige: FAILED - gui-lua build/drive: %s",
					r1.error.c_str());
				luaOk = false;
			}
			render->renderOneFrame();	// resolve the Lua-authored screen

			// C++ injects ONE synthetic press on the Lua-created button (the only
			// C++ role: input + probes), then Lua asserts it reached the widget.
			if (luaOk && Orkige::GuiManager::getSingletonPtr() &&
				Orkige::GuiManager::getSingleton().widgetExists("luaBtn"))
			{
				Orkige::optr<Orkige::GuiWidget> b =
					Orkige::GuiManager::getSingleton().getWidget("luaBtn").lock();
				if (b)
				{
					const Orkige::Vec2 p = b->getPosition();
					const Orkige::Vec2 s = b->getSize();
					const float cx = p.x + s.x * 0.5f;
					const float cy = p.y + s.y * 0.5f;
					SDL_Event down{};
					down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
					down.button.button = SDL_BUTTON_LEFT;
					down.button.down = true;
					down.button.x = cx; down.button.y = cy;
					inputManager.injectEvent(down);
					SDL_Event up{};
					up.type = SDL_EVENT_MOUSE_BUTTON_UP;
					up.button.button = SDL_BUTTON_LEFT;
					up.button.down = false;
					up.button.x = cx; up.button.y = cy;
					inputManager.injectEvent(up);
				}
			}
			render->renderOneFrame();

			// PHASE 2: Lua asserts the injected input reached its widget, and the
			// C++ side reads a probe on the Lua-built screen
			if (luaOk)
			{
				const Orkige::ScriptRuntime::Result r2 = scriptRuntime.runString(
					"assert(luaBtn:wasClicked(), 'synthetic click did not reach the "
					"Lua-created button')\nreturn true");
				if (!r2.success)
				{
					SDL_Log("hello_orkige: FAILED - gui-lua input: %s",
						r2.error.c_str());
					luaOk = false;
				}
			}
			if (luaOk && Orkige::GuiManager::getSingletonPtr() &&
				Orkige::GuiManager::getSingleton().getLastBatchCount() < 1)
			{
				SDL_Log("hello_orkige: FAILED - gui-lua: the Lua screen drew no batch");
				luaOk = false;
			}

			// tear the Lua-owned gui down before the trailing free-run loop
			scriptRuntime.runString("if gui then gui:destroyAllWidgets() end\n"
				"gui = nil factory = nil luaBtn = nil collectgarbage('collect')");
			Orkige::PlatformWindow::setContentScaleOverride(0.0f);
			if (!luaOk)
			{
				return 1;
			}
			SDL_Log("hello_orkige: gui-lua matrix passed (every widget type + every "
				"setter/getter/mode/modal/toggle/dropdown/toast/scroll + tweens/"
				"transforms/transitions/show-hide authored & asserted in Lua; .oui "
				"widgets reached by id via findWidget; synthetic input verified)");
		}

		bool running = true;
		unsigned long frameCount = 0;
		// music selfcheck: the playhead sampled early, compared late to prove it
		// advanced (device present) or stayed put at 0 (headless no-op)
		float musicPosStart = 0.0f;
		std::chrono::steady_clock::time_point lastFrameTime =
			std::chrono::steady_clock::now();
		while (running)
		{
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				if (event.type == SDL_EVENT_QUIT)
				{
					running = false;
				}
				inputManager.injectEvent(event);
			}
			if (quitOnEscape.quitRequested)
			{
				running = false;
			}
			if (demoPhysics)
			{
				// measured frame dt for the physics step through the shared
				// clamp policy: automated runs keep the 1/60 floor so headless
				// frames (rendering far faster than 60 fps) accumulate enough
				// simulated time for the frame-based self-checks below
				const std::chrono::steady_clock::time_point frameTime =
					std::chrono::steady_clock::now();
				float deltaTime = std::chrono::duration<float>(
					frameTime - lastFrameTime).count();
				lastFrameTime = frameTime;
				deltaTime = Orkige::AppHost::clampFrameDelta(deltaTime,
					automatedRun);
				physicsWorld.update(deltaTime);
				// component updates create the bodies and sync poses
				// (simulation -> TransformComponent for dynamic bodies)
				gameObjectManager.update(deltaTime);
			}
			if (demoSpriteAnim)
			{
				// fixed tick: deterministic frame progression regardless of the
				// (headless-fast) render rate
				gameObjectManager.update(0.05f);
				float au0, av0, au1, av1;
				animSprite->getUVRect(au0, av0, au1, av1);
				spriteAnimU0Log.push_back(au0);
			}
			if (demoParticles)
			{
				// fixed tick: the emitter ages its particles deterministically
				// (burst-only, so nothing spawns until frame 20)
				gameObjectManager.update(0.05f);
			}
			if (demoMusic)
			{
				// refill the ring on the main thread; the small real-time delay
				// lets OpenAL actually advance the playhead between the sampled
				// frames (audio plays wall-clock, not per rendered frame)
				soundManager.update(0.016f);
				SDL_Delay(4);
			}
			cubeNode->yaw(Orkige::Degree(0.4f));
			cubeNode->pitch(Orkige::Degree(0.13f));
			// orbit the small cube around the main cube purely through the
			// TransformComponent API - proves the component bridge end-to-end
			const float orbitAngle = static_cast<float>(frameCount) * 0.02f;
			orbiterTransform->setPosition(Orkige::Vec3(
				3.0f * std::cos(orbitAngle), 0.8f, 3.0f * std::sin(orbitAngle)));
			orbiterTransform->setOrientation(Orkige::Quat(
				Orkige::Radian(-orbitAngle), Orkige::Vec3::UNIT_Y));
			if (!render->renderOneFrame())
			{
				running = false;
			}
			++frameCount;
			if (frameCount == 60)
			{
				// ORKIGE_DEMO_SCREENSHOT: dump the framebuffer for automated
				// visual verification
				if (const char* shotPath = std::getenv("ORKIGE_DEMO_SCREENSHOT"))
				{
					render->saveWindowContents(shotPath);
				}
			}
			// ORKIGE_DEMO_SYNTH_ESC: push a synthetic ESC key press through the
			// SDL event queue after 60 frames to prove the quit path (SDL event
			// -> InputManager::injectEvent -> KeyPressedEvent -> listener) in
			// automated runs; OS-level synthetic key events would need macOS
			// accessibility permissions, this stays inside SDL instead.
			if (frameCount == 60 && std::getenv("ORKIGE_DEMO_SYNTH_ESC"))
			{
				Orkige::pushKeyEvent(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, true);
			}
			if (frameCount == 10)
			{
				// verification that both cubes actually got drawn (12 triangles
				// each), not just a black window; with ORKIGE_DEMO_MESH the
				// glTF octahedron adds 8 more - read through the facade stats
				const size_t expectedTriangles = demoMesh ? 32 : 24;
				const size_t triangleCount =
					render->getFrameStats().triangleCount;
				SDL_Log("hello_orkige: triangle count after 10 frames: %zu",
					triangleCount);
				if (triangleCount < expectedTriangles)
				{
					SDL_Log("hello_orkige: FAILED - expected %s "
						"(>= %zu triangles)", demoMesh ?
						"both cubes and the test mesh" : "both cubes",
						expectedTriangles);
					return 1;
				}
			}
			if (demoPhysics && frameCount == 110)
			{
				// physics screenshot (cubes resting on the floor) through the
				// existing ORKIGE_DEMO_SCREENSHOT hook, later than the
				// frame-60 shot so the pile has settled
				if (const char* shotPath = std::getenv("ORKIGE_DEMO_SCREENSHOT"))
				{
					render->saveWindowContents(shotPath);
				}
			}
			if (demoPhysics && frameCount == 120)
			{
				// physics self-checks: (a) the dropped cubes fell and rest on
				// the floor (not below it), (b) the plane-locked cubes moved
				// in x but kept their z (DOF locks work)
				bool physicsOk = true;
				const float restY = floorTopY + cubeHalf;
				for (size_t i = 0; i < dropTransforms.size(); ++i)
				{
					const Orkige::Vec3 pos = dropTransforms[i]->getPosition();
					const float speed = dropBodies[i]->getLinearVelocity().length();
					const bool fell = (dropStartY[i] - pos.y) > 2.0f;
					const bool atRest = std::abs(pos.y - restY) < 0.3f &&
						pos.y > floorTopY && speed < 0.5f;
					SDL_Log("hello_orkige: physics cube %zu y=%.3f (start "
						"%.1f, rest %.1f) |v|=%.3f fell=%d atRest=%d",
						i, pos.y, dropStartY[i], restY, speed,
						static_cast<int>(fell), static_cast<int>(atRest));
					physicsOk = physicsOk && fell && atRest;
				}
				for (size_t i = 0; i < planarTransforms.size(); ++i)
				{
					const Orkige::Vec3 pos = planarTransforms[i]->getPosition();
					const bool zLocked =
						std::abs(pos.z - planarStartZ) < 1e-3f;
					const bool xMoved =
						std::abs(pos.x - planarStartX[i]) > 0.5f;
					SDL_Log("hello_orkige: planar cube %zu x=%.3f (start "
						"%.1f) z=%.6f (start %.1f) zLocked=%d xMoved=%d",
						i, pos.x, planarStartX[i], pos.z, planarStartZ,
						static_cast<int>(zLocked), static_cast<int>(xMoved));
					physicsOk = physicsOk && zLocked && xMoved;
				}
				if (!physicsOk)
				{
					SDL_Log("hello_orkige: FAILED - physics self-checks");
					return 1;
				}
				SDL_Log("hello_orkige: physics self-checks passed (fall + "
					"rest on floor, planar DOF locks)");
			}
			if (demoSpriteAnim && frameCount == 120)
			{
				// count distinct grid columns visited (u0 quantized to the 4-cell
				// grid) and confirm the clip advanced AND wrapped to frame 0
				std::set<int> columns;
				bool advanced = false;
				bool wrapped = false;
				const float firstU0 =
					spriteAnimU0Log.empty() ? 0.0f : spriteAnimU0Log.front();
				for (size_t i = 0; i < spriteAnimU0Log.size(); ++i)
				{
					const float au0 = spriteAnimU0Log[i];
					columns.insert(static_cast<int>(std::lround(au0 * 4.0f)));
					if (i > 0 &&
						std::abs(au0 - spriteAnimU0Log[i - 1]) > 1e-4f)
					{
						advanced = true;
					}
					// frame 0 seen again after leaving it = a completed loop
					if (i > 2 && advanced &&
						std::abs(au0 - firstU0) < 1e-4f)
					{
						wrapped = true;
					}
				}
				SDL_Log("hello_orkige: flipbook ticks=%zu distinctColumns=%zu "
					"advanced=%d wrapped=%d", spriteAnimU0Log.size(),
					columns.size(), static_cast<int>(advanced),
					static_cast<int>(wrapped));
				if (columns.size() != 4 || !advanced || !wrapped)
				{
					SDL_Log("hello_orkige: FAILED - flipbook self-checks "
						"(expected 4 distinct frames advancing and wrapping)");
					return 1;
				}
				SDL_Log("hello_orkige: flipbook self-checks passed (4-frame clip "
					"advanced across frames and wrapped on loop)");
			}
			if (demoParticles && frameCount == 20)
			{
				// baseline (no live particles yet), then fire the one burst
				particleBaselineTriangles =
					render->getFrameStats().triangleCount;
				const int spawned = particles->burst(particleMax);
				SDL_Log("hello_orkige: particle burst spawned=%d live=%d "
					"baselineTris=%zu", spawned, particles->getLiveCount(),
					particleBaselineTriangles);
				if (spawned != particleMax ||
					particles->getLiveCount() != particleMax)
				{
					SDL_Log("hello_orkige: FAILED - burst did not spawn %d "
						"particles", particleMax);
					return 1;
				}
			}
			if (demoParticles && frameCount == 25)
			{
				// the burst is still alive (age ~0.25 < 0.5): the single-draw
				// batch must have raised the triangle count by ~maxParticles*2
				const std::size_t burstTriangles =
					render->getFrameStats().triangleCount;
				const std::size_t risen =
					(burstTriangles > particleBaselineTriangles)
					? burstTriangles - particleBaselineTriangles : 0;
				SDL_Log("hello_orkige: particle burst triangles=%zu (baseline "
					"%zu, +%zu) live=%d", burstTriangles,
					particleBaselineTriangles, risen, particles->getLiveCount());
				if (risen < static_cast<std::size_t>(particleMax * 2 - 8) ||
					particles->getLiveCount() == 0)
				{
					SDL_Log("hello_orkige: FAILED - burst did not raise the "
						"triangle count (expected ~%d more)", particleMax * 2);
					return 1;
				}
			}
			if (demoParticles && frameCount == 45)
			{
				// lifetime 0.5s at the fixed 0.05 dt = 10 ticks: every particle
				// is long dead, so the count decayed back to the baseline
				const std::size_t decayTriangles =
					render->getFrameStats().triangleCount;
				const int live = particles->getLiveCount();
				SDL_Log("hello_orkige: particle decay triangles=%zu (baseline "
					"%zu) live=%d", decayTriangles, particleBaselineTriangles,
					live);
				if (live != 0 ||
					decayTriangles > particleBaselineTriangles + 8)
				{
					SDL_Log("hello_orkige: FAILED - particles did not decay "
						"back down (live=%d)", live);
					return 1;
				}
				SDL_Log("hello_orkige: particle selfcheck passed (burst raised "
					"then decayed, single-draw batch)");
			}
			if (demoVectorShape && frameCount == 20)
			{
				// hide the shape, then sample the frame's triangle count with it
				// gone at frame 25 - the baseline the shown count must exceed
				vectorShape->setShapeVisible(false);
			}
			if (demoVectorShape && frameCount == 25)
			{
				vectorShapeHiddenTriangles =
					render->getFrameStats().triangleCount;
				vectorShape->setShapeVisible(true);
			}
			if (demoVectorShape && frameCount == 35)
			{
				// with the shape shown again the frame carries its triangles: the
				// count must have risen by ~the shape's own triangle count
				const std::size_t shownTriangles =
					render->getFrameStats().triangleCount;
				const std::size_t risen =
					(shownTriangles > vectorShapeHiddenTriangles)
					? shownTriangles - vectorShapeHiddenTriangles : 0;
				SDL_Log("hello_orkige: vector shape triangles shown=%zu "
					"(hidden %zu, +%zu, mesh %zu)", shownTriangles,
					vectorShapeHiddenTriangles, risen,
					vectorShape->getTriangleCount());
				if (risen == 0)
				{
					SDL_Log("hello_orkige: FAILED - showing the vector shape did "
						"not raise the triangle count (it did not render)");
					return 1;
				}
				SDL_Log("hello_orkige: vector-shape selfcheck passed (tessellated "
					"+ rendered on this flavor)");
			}
			if (demoMusic && frameCount == 20)
			{
				// sample the playhead early (after the ring primed and a little
				// audio has played)
				Orkige::MusicStreamPtr track = soundManager.getMusic("bgm");
				musicPosStart = track ? track->getPlayPosition() : 0.0f;
			}
			if (demoMusic && frameCount == 110)
			{
				Orkige::MusicStreamPtr track = soundManager.getMusic("bgm");
				const float musicPosEnd =
					track ? track->getPlayPosition() : 0.0f;
				const bool playing = soundManager.isMusicPlaying("bgm");
				SDL_Log("hello_orkige: music posStart=%.4f posEnd=%.4f "
					"playing=%d audioUp=%d", musicPosStart, musicPosEnd,
					static_cast<int>(playing), static_cast<int>(musicAudioUp));
				if (musicAudioUp)
				{
					// device present: the track must be playing AND its playhead
					// must have advanced (proves the queued-buffer ring refilled)
					if (!playing || !(musicPosEnd > musicPosStart))
					{
						SDL_Log("hello_orkige: FAILED - music playhead did not "
							"advance (ring did not refill)");
						return 1;
					}
					SDL_Log("hello_orkige: music selfcheck passed (streamed OGG "
						"playhead advanced across frames)");
				}
				else
				{
					// headless: the honest no-op path - nothing plays, nothing
					// advances, and no query crashed to get here
					if (playing || musicPosEnd != 0.0f)
					{
						SDL_Log("hello_orkige: FAILED - music no-op path "
							"reported playback with no device");
						return 1;
					}
					SDL_Log("hello_orkige: music selfcheck passed (honest no-op "
						"without an audio device)");
				}
			}
			if (frameLimit != 0 && frameCount >= frameLimit)
			{
				running = false;
			}
		}

		soundManager.deinit();
	}

	// AppHost's destructor mirrors the boot: world, engine, singletons,
	// then the SDL window
	return 0;
}
