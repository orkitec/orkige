// hello_orkige - Phase 1 milestone demo.
// SDL3 owns the window and event loop; Orkige::Engine renders into it via the
// externalWindowHandle path. Scene: a spinning vertex-colored cube, which
// exercises the whole RTSS shader pipeline without needing any asset files.
#include <SDL3/SDL.h>
#include <engine_graphic/Engine.h>
#include <engine_input/InputManager.h>
#include <engine_sound/SoundManager.h>
#include <engine_util/StringUtil.h>
#include <core_util/StringUtil.h>
#include <core_util/Timer.h>
#include <core_event/GlobalEventManager.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

extern "C" void* orkige_native_window_handle(SDL_Window* window);

// quit-on-ESC through the engine input pipeline (SDL event -> InputManager ->
// GlobalEventManager -> listener) instead of a raw SDL keycode check
struct QuitOnEscape
{
	bool quitRequested = false;
	bool onKeyPressed(Orkige::Event const& event)
	{
		if (event.getDataPtr<Orkige::KeyEventData>()->key ==
			Orkige::KeyEventData::KC_ESCAPE)
		{
			quitRequested = true;
		}
		return false;
	}
};

int main(int, char**)
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	SDL_Window* window = SDL_CreateWindow("hello orkige", 1280, 720, 0);
	if (!window)
	{
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		return 1;
	}

	{
		// engine singletons normally created by Orkige::Application; the demo
		// boots just the ones Engine::setup depends on
		Orkige::Timer::initialise();
		Orkige::GlobalEventManager eventManager;

		// no resources.cfg / plugins.cfg / ogre.cfg: the demo wires its single
		// resource location manually and lets Engine::configure pick defaults.
		Orkige::Engine engine(Ogre::SMT_DEFAULT,
			Orkige::StringUtil::BLANK, Orkige::StringUtil::BLANK,
			Orkige::StringUtil::BLANK, "hello_orkige.log");
		engine.setCustomWindowParam("width", "1280");
		engine.setCustomWindowParam("height", "720");

		// The RTSS needs its shader library (and OgreUnifiedShader.h from
		// Media/Main) in the OgreInternal group before Engine::setup runs -
		// same locations OgreBites::ApplicationContext registers.
		// ORKIGE_DEMO_MEDIA_DIR is a demo-only compile definition pointing into
		// the vcpkg-installed OGRE media (see CMakeLists.txt).
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_DEMO_MEDIA_DIR "/Main", "FileSystem",
			Ogre::RGN_INTERNAL);
		Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
			ORKIGE_DEMO_MEDIA_DIR "/RTShaderLib", "FileSystem",
			Ogre::RGN_INTERNAL);

		if (!engine.setup("hello orkige", Orkige::Engine::SHOW_NEVER,
			Orkige::StringUtil::Converter::toString(
				reinterpret_cast<size_t>(orkige_native_window_handle(window)))))
		{
			SDL_Log("Engine::setup failed");
			return 1;
		}
		engine.createDefaultCameraAndViewport();
		Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();

		// input pipeline: the poll loop below feeds every SDL event into the
		// InputManager, which triggers Orkige input events globally
		Orkige::InputManager inputManager;
		QuitOnEscape quitOnEscape;
		optr<Orkige::EventListener> escapeListener =
			Orkige::GlobalEventManager::getSingleton().bind(
				Orkige::InputManager::KeyPressedEvent,
				&QuitOnEscape::onKeyPressed, &quitOnEscape);

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
		}

		Ogre::SceneManager* sceneManager = engine.getSceneManager();
		sceneManager->setAmbientLight(Ogre::ColourValue(0.2f, 0.2f, 0.2f));

		// unlit material that takes its colour from the vertices (the RTSS only
		// reads vertex colours when the pass tracks them)
		Ogre::MaterialPtr cubeMaterial = Ogre::MaterialManager::getSingleton().create(
			"VertexColour", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		Ogre::Pass* cubePass = cubeMaterial->getTechnique(0)->getPass(0);
		cubePass->setLightingEnabled(false);
		cubePass->setVertexColourTracking(Ogre::TVC_DIFFUSE);

		Ogre::ManualObject* cube = sceneManager->createManualObject("cube");
		cube->begin("VertexColour", Ogre::RenderOperation::OT_TRIANGLE_LIST);
		const float s = 1.0f;
		const Ogre::Vector3 corners[8] = {
			{-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s},
			{-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s},
		};
		const Ogre::ColourValue colors[8] = {
			Ogre::ColourValue(1, 0, 0), Ogre::ColourValue(0, 1, 0),
			Ogre::ColourValue(0, 0, 1), Ogre::ColourValue(1, 1, 0),
			Ogre::ColourValue(1, 0, 1), Ogre::ColourValue(0, 1, 1),
			Ogre::ColourValue(1, 1, 1), Ogre::ColourValue(0.5f, 0.5f, 0.5f),
		};
		for (int i = 0; i < 8; ++i)
		{
			cube->position(corners[i]);
			cube->colour(colors[i]);
		}
		const int quads[6][4] = {
			{0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0},
		};
		for (const int* q : quads)
		{
			cube->quad(q[0], q[1], q[2], q[3]);
		}
		cube->end();

		Ogre::SceneNode* cubeNode =
			sceneManager->getRootSceneNode()->createChildSceneNode();
		cubeNode->attachObject(cube);

		engine.getCamera()->getParentSceneNode()->setPosition(0.0f, 2.0f, 6.0f);
		engine.getCamera()->getParentSceneNode()->lookAt(
			Ogre::Vector3::ZERO, Ogre::Node::TS_WORLD);

		// ORKIGE_DEMO_FRAMES: frame-limit escape for automated runs
		// (0/unset = run until the window is closed).
		unsigned long frameLimit = 0;
		if (const char* demoFrames = std::getenv("ORKIGE_DEMO_FRAMES"))
		{
			frameLimit = std::strtoul(demoFrames, nullptr, 10);
		}

		bool running = true;
		unsigned long frameCount = 0;
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
			cubeNode->yaw(Ogre::Degree(0.4f));
			cubeNode->pitch(Ogre::Degree(0.13f));
			if (!engine.renderOneFrame())
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
					engine.getRenderWindow()->writeContentsToFile(shotPath);
				}
			}
			// ORKIGE_DEMO_SYNTH_ESC: push a synthetic ESC key press through the
			// SDL event queue after 60 frames to prove the quit path (SDL event
			// -> InputManager::injectEvent -> KeyPressedEvent -> listener) in
			// automated runs; OS-level synthetic key events would need macOS
			// accessibility permissions, this stays inside SDL instead.
			if (frameCount == 60 && std::getenv("ORKIGE_DEMO_SYNTH_ESC"))
			{
				SDL_Event escEvent{};
				escEvent.type = SDL_EVENT_KEY_DOWN;
				escEvent.key.scancode = SDL_SCANCODE_ESCAPE;
				escEvent.key.key = SDLK_ESCAPE;
				escEvent.key.down = true;
				SDL_PushEvent(&escEvent);
			}
			if (frameCount == 10)
			{
				// verification that the cube actually got drawn, not just a black window
				const size_t triangleCount =
					engine.getRenderWindow()->getStatistics().triangleCount;
				Ogre::LogManager::getSingleton().logMessage(
					"hello_orkige: triangle count after 10 frames: " +
					Ogre::StringConverter::toString(triangleCount));
				SDL_Log("hello_orkige: triangle count after 10 frames: %zu",
					triangleCount);
				if (triangleCount == 0)
				{
					SDL_Log("hello_orkige: FAILED - nothing was rendered");
					return 1;
				}
			}
			if (frameLimit != 0 && frameCount >= frameLimit)
			{
				running = false;
			}
		}

		soundManager.deinit();
	}

	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
