/**************************************************************
	created:	2026/07/08 at 12:00
	filename: 	PlayerRuntime.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The player side of the editor's play-mode debug protocol, extracted
	from tools/player/main.cpp so native game modules (see
	cmake/OrkigeGameModule.cmake) speak the identical protocol without
	duplicating it.
***************************************************************/

#include "engine_runtime/PlayerRuntime.h"

#include "core_base/TypeManager.h"
#include "core_base/PropertySchema.h"
#include "core_base/PropertyValue.h"
#include "core_debug/CVarManager.h"
#include "core_debug/MemoryManager.h"
#include "core_debug/MemorySampler.h"
#include "core_game/GameObjectComponent.h"
#include "core_game/GameObjectManager.h"
#include "core_game/GameState.h"
#include "core_debugnet/TraceWriter.h"
#include "core_script/ScriptEventBus.h"
#include "engine_base/EngineLog.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_gocomponent/ScriptComponent.h"
#include "engine_gocomponent/VectorAnimationComponent.h"
#include "core_util/VectorAnimAsset.h"
#include "core_util/VectorAnimEval.h"
#include "engine_render/RenderMath.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderCamera.h"
#include "engine_util/PlatformWindow.h"
#include "engine_gui/GuiManager.h"
#include "engine_input/InputManager.h"
#include <SDL3/SDL_events.h>
#include "engine_sound/SoundManager.h"

// SDL_GetBasePath (PlayerBundle) - safe to call before SDL_Init
#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <cstring>
#include <filesystem>
#include <utility>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

namespace Orkige
{
	namespace Protocol = DebugProtocol;

	const unsigned long PlayerDebugLink::HIERARCHY_CHECK_INTERVAL = 15;
	const int PlayerDebugLink::OBJECT_STATE_INTERVAL_MS = 66;
	const int PlayerDebugLink::DIAL_RETRY_INTERVAL_MS = 500;
	const int PlayerDebugLink::DIAL_GIVE_UP_SECONDS = 20;
	//---------------------------------------------------------
	namespace
	{
		//! @brief the widget hint for a reflected property crossing the wire:
		//! Enum -> its "label=value,label=value,..." option table (looked up once
		//! per enum-type and cached), AssetRef/ObjectRef -> the asset-kind /
		//! object-type hint, everything else -> "". Lets the editor pick a typed
		//! widget (combo/asset picker) without a local schema.
		String propertyHint(PropertyDesc const & desc, TypeManager & typeManager,
			std::map<String, String> & enumHintCache)
		{
			if (desc.kind == PropertyKind::Enum)
			{
				std::map<String, String>::const_iterator cached =
					enumHintCache.find(desc.enumTypeName);
				if (cached != enumHintCache.end())
				{
					return cached->second;
				}
				String options;
				if (EnumInfo const * enumInfo =
					typeManager.findEnum(desc.enumTypeName))
				{
					for (auto const & labelled : enumInfo->values())
					{
						if (!options.empty())
						{
							options += ",";
						}
						options += labelled.first + "=" +
							std::to_string(labelled.second);
					}
				}
				enumHintCache[desc.enumTypeName] = options;
				return options;
			}
			if (desc.kind == PropertyKind::AssetRef ||
				desc.kind == PropertyKind::ObjectRef)
			{
				return desc.referenceHint;
			}
			return String();
		}

		//! @brief all GameObject ids plus their parent ids ("" = root) and
		//! activeSelf flags ("1"/"0") as three parallel lists (the manager map
		//! is sorted, order is stable); the parent/active lists are the
		//! additive protocol-v1 hierarchy extension
		void collectHierarchy(GameObjectManager & gameObjectManager,
			StringVector & ids, StringVector & parents, StringVector & actives)
		{
			const std::size_t objectCount =
				gameObjectManager.getGameObjects().size();
			ids.clear();
			ids.reserve(objectCount);
			parents.clear();
			parents.reserve(objectCount);
			actives.clear();
			actives.reserve(objectCount);
			for (auto const & [id, gameObject] :
				gameObjectManager.getGameObjects())
			{
				ids.push_back(id);
				parents.push_back(gameObject->getParentId());
				actives.push_back(gameObject->isActiveSelf() ? "1" : "0");
			}
		}

		//! @brief object_state: GENERIC per-component property snapshot driven off
		//! the reflection registry. For every component of the
		//! object, iterate its declared PropertySchema and stream each reflected
		//! property as "<Component>.<name>" -> PropertyValue::toString() (the
		//! stringly-typed wire form), skipping HIDDEN properties and any without a
		//! getter. Four parallel lists (keys/kinds/hints/read-only flags) carry
		//! the per-property metadata so the editor picks a typed widget without a
		//! local schema. New components and script exports appear here with ZERO
		//! protocol code - the hand-written per-component allowlist is retired.
		//! @remarks streams every frame for the selected object, so the schema
		//! pointer is cached per component TYPE (one TypeManager lookup per type,
		//! not per property per frame). Components that declared no properties
		//! still appear in the "components" list.
		DebugMessage buildObjectState(optr<GameObject> const & gameObject)
		{
			DebugMessage state(Protocol::MSG_OBJECT_STATE);
			state.set(Protocol::FIELD_ID, gameObject->getObjectID());

			// the enum value<->label hint cache stays per-type (enum metadata is
			// static); the SCHEMA is now resolved per component through the
			// static-UNION-dynamic union, so a ScriptComponent's
			// exported script properties stream too - the union is a handful of
			// descriptors, cheap enough for the selected object per frame.
			static std::map<String, String> enumHintCache;
			TypeManager & typeManager = TypeManager::getSingleton();

			StringVector componentNames;
			StringVector propKeys;
			StringVector propKinds;
			StringVector propHints;
			StringVector propFlags;
			for (auto const & [componentType, component] :
				gameObject->getComponents())
			{
				String const & componentName = componentType.getName();
				componentNames.push_back(componentName);

				const PropertySchema schema = getComponentSchema(*component);
				if (schema.empty())
				{
					continue; // a component that declared no reflected properties
				}
				Object const * instance = component.get();
				for (PropertyDesc const & desc : schema.properties())
				{
					if (desc.hasFlag(PROP_HIDDEN) || !desc.get)
					{
						continue; // never stream a hidden / getter-less property
					}
					const String key = componentName + "." + desc.name;
					state.set(key, desc.get(instance).toString());
					propKeys.push_back(key);
					propKinds.push_back(
						std::to_string(static_cast<int>(desc.kind)));
					propHints.push_back(
						propertyHint(desc, typeManager, enumHintCache));
					propFlags.push_back(desc.isReadOnly() ? "1" : "0");
				}
			}
			state.setList(Protocol::LIST_COMPONENTS, componentNames);
			state.setList(Protocol::LIST_PROP_KEYS, propKeys);
			state.setList(Protocol::LIST_PROP_KINDS, propKinds);
			state.setList(Protocol::LIST_PROP_HINTS, propHints);
			state.setList(Protocol::LIST_PROP_FLAGS, propFlags);
			return state;
		}
	}
	//---------------------------------------------------------
	PlayerArguments PlayerArguments::parse(int argc, char ** argv)
	{
		PlayerArguments arguments;
		for (int argIndex = 1; argIndex < argc; ++argIndex)
		{
			if (std::strcmp(argv[argIndex], "--debug-port") == 0 &&
				argIndex + 1 < argc)
			{
				arguments.debugRequested = true;
				arguments.debugPort = static_cast<unsigned short>(
					std::strtoul(argv[++argIndex], nullptr, 10));
			}
			else if (std::strcmp(argv[argIndex], "--project") == 0 &&
				argIndex + 1 < argc)
			{
				arguments.projectPath = argv[++argIndex];
			}
			else if (argv[argIndex][0] != '-' && arguments.scenePath.empty())
			{
				arguments.scenePath = argv[argIndex];
			}
			else
			{
				arguments.valid = false;
				arguments.unknownArgument = argv[argIndex];
				return arguments;
			}
		}
		return arguments;
	}
	//--- PlayerBundle (exported-app project/media discovery) --
	namespace PlayerBundle
	{
		const String PROJECT_MARKER_FILE_NAME = "orkige_project.txt";

		//! baseDir or (when empty) SDL's base path, separator-terminated
		static String normalizedBase(String const & baseDir)
		{
			String base = baseDir.empty() ? baseDirectory() : baseDir;
			if (!base.empty() && base.back() != '/' && base.back() != '\\')
			{
				base += '/';
			}
			return base;
		}
		//---------------------------------------------------------
		String baseDirectory()
		{
			// SDL_GetBasePath: safe before SDL_Init, cached by SDL (do not
			// free); NULL e.g. on Android where the caller must pass the
			// extracted-assets root explicitly
			const char * basePath = SDL_GetBasePath();
			return basePath ? String(basePath) : String();
		}
		//---------------------------------------------------------
		String findBundledProject(String const & baseDir)
		{
			const String base = normalizedBase(baseDir);
			if (base.empty())
			{
				return String();
			}
			std::ifstream marker(base + PROJECT_MARKER_FILE_NAME);
			if (!marker.is_open())
			{
				return String(); // not an exported app - the normal dev case
			}
			String line;
			std::getline(marker, line);
			while (!line.empty() && (line.back() == '\r' ||
				line.back() == '\n' || line.back() == ' ' ||
				line.back() == '\t'))
			{
				line.pop_back();
			}
			if (line.empty())
			{
				return String();
			}
			const String projectPath = base + line;
			std::error_code ignored;
			if (!std::filesystem::exists(projectPath, ignored))
			{
				return String(); // honest miss: a marker naming nothing
			}
			return projectPath;
		}
		//---------------------------------------------------------
		String resolveMediaDirectory(String const & fallbackMediaDir,
			String const & baseDir)
		{
			const String base = normalizedBase(baseDir);
			if (!base.empty())
			{
				const String bundledMediaDir = base + "Media";
				std::error_code ignored;
				// an exported app carries its engine media under Media/: the
				// classic RTSS library (Media/Main) or the Ogre-Next Hlms
				// shader templates (Media/Hlms). Either marks a bundled Media.
				if (std::filesystem::is_directory(bundledMediaDir + "/Main",
						ignored) ||
					std::filesystem::is_directory(bundledMediaDir + "/Hlms",
						ignored))
				{
					return bundledMediaDir;
				}
			}
			return fallbackMediaDir;
		}
	}
	//---------------------------------------------------------
	PlayerDebugLink::PlayerDebugLink() = default;
	//---------------------------------------------------------
	PlayerDebugLink::~PlayerDebugLink()
	{
		// belt-and-braces: a still-attached capture would dangle (the
		// capture's own dtor detaches too - shutdown() is the orderly path)
		if (mLogCapture)
		{
			mLogCapture->detach();
		}
	}
	//---------------------------------------------------------
	bool PlayerDebugLink::start(unsigned short port)
	{
		if (mActive || !mServer.start(port))
		{
			return false;
		}
		mActive = true;
		// forward the runtime's engine log to the editor Console ([remote]
		// lines); lines logged from here on queue up and flush per frame
		mLogCapture = std::make_unique<EngineLogCapture>();
		mLogCapture->attach();
		return true;
	}
	//---------------------------------------------------------
	bool PlayerDebugLink::startConnect(String const & endpoint)
	{
		if (mActive)
		{
			return false;
		}
		// "host:port" or a bare "port" (host defaults to loopback)
		String host = "127.0.0.1";
		String portText = endpoint;
		const size_t colon = endpoint.rfind(':');
		if (colon != String::npos)
		{
			host = endpoint.substr(0, colon);
			portText = endpoint.substr(colon + 1);
		}
		const long parsedPort = std::strtol(portText.c_str(), NULL, 10);
		if (host.empty() || parsedPort <= 0 || parsedPort > 65535)
		{
			return false;
		}
		mDialMode = true;
		mDialHost = host;
		mDialPort = static_cast<unsigned short>(parsedPort);
		mDialStart = std::chrono::steady_clock::now();
		mDialLastAttempt = mDialStart;
		if (!mDialer.connect(mDialHost, mDialPort))
		{
			// an immediate socket-setup failure; the retry loop in
			// linkUpdate() still owns the give-up budget
			mDialer.disconnect();
		}
		mActive = true;
		mLogCapture = std::make_unique<EngineLogCapture>();
		mLogCapture->attach();
		return true;
	}
	//---------------------------------------------------------
	bool PlayerDebugLink::consumePendingStep()
	{
		if (!mActive || !mPaused || mPendingSteps <= 0)
		{
			return false;
		}
		--mPendingSteps;
		return true;
	}
	//---------------------------------------------------------
	bool PlayerDebugLink::consumePendingScreenshot(String & outPath)
	{
		if (!mActive || !mHasPendingScreenshot)
		{
			return false;
		}
		outPath = mPendingScreenshotPath;
		mHasPendingScreenshot = false;
		mPendingScreenshotPath.clear();
		return true;
	}
	//---------------------------------------------------------
	void PlayerDebugLink::notifyScreenshotSaved(String const & path, bool ok,
		String const & error)
	{
		if (!mActive || !linkHasClient())
		{
			return;
		}
		DebugMessage saved(Protocol::MSG_SCREENSHOT_SAVED);
		saved.set(Protocol::FIELD_PATH, path);
		saved.set(Protocol::FIELD_VALUE, ok ? "1" : "0");
		if (!ok)
		{
			saved.set(Protocol::FIELD_MESSAGE, error);
		}
		linkSend(saved);
	}
	//---------------------------------------------------------
	void PlayerDebugLink::handleRecordStart(DebugMessage const & message)
	{
		const String & path = message.get(Protocol::FIELD_PATH);
		if (path.empty())
		{
			sendError("record: missing path");
			return;
		}
		// a fresh request supersedes any trace already in flight (the editor
		// drives one recording at a time)
		mRecording = true;
		mRecordPath = path;
		// have the script event bus record each emit for the trace's event
		// stream (folded in alongside the physics contacts); drop any stale
		// buffer from before this recording
		ScriptEventBus::getSingleton().setTraceCapture(true);
		ScriptEventBus::getSingleton().takeFrameEvents();
		// clamp to sane bounds: the trace byte cap is the memory backstop, this
		// keeps a stray request from tying up the player for minutes
		const float seconds = message.getFloat(Protocol::FIELD_SECONDS, 5.0f);
		mRecordMaxSeconds = std::clamp(seconds, 0.1f, 60.0f);
		const float every = message.getFloat(Protocol::FIELD_EVERY, 2.0f);
		mRecordEveryNth = every < 1.0f
			? 1u : static_cast<unsigned int>(every);
		// optional id/name allowlist: comma-separated, whitespace-trimmed
		mRecordFilter.clear();
		String const & filter = message.get(Protocol::FIELD_FILTER);
		size_t start = 0;
		while (start <= filter.size())
		{
			const size_t comma = filter.find(',', start);
			const size_t end = comma == String::npos ? filter.size() : comma;
			size_t a = start, b = end;
			while (a < b && std::isspace(static_cast<unsigned char>(filter[a])))
			{
				++a;
			}
			while (b > a && std::isspace(static_cast<unsigned char>(filter[b - 1])))
			{
				--b;
			}
			if (b > a)
			{
				mRecordFilter.insert(filter.substr(a, b - a));
			}
			if (comma == String::npos)
			{
				break;
			}
			start = comma + 1;
		}
		mRecordElapsed = 0.0f;
		mRecordFrameCounter = 0;
		mRecordLastFrame = 0;
		mRecordShouldFinish = false;
		mTrace = std::make_unique<TraceWriter>();
	}
	//---------------------------------------------------------
	void PlayerDebugLink::traceFrame(GameObjectManager & gameObjectManager,
		unsigned long frameCount, float deltaSeconds)
	{
		if (!mActive || !mRecording || !mTrace)
		{
			return;
		}
		mRecordElapsed += deltaSeconds;
		mRecordLastFrame = frameCount;
		if (mRecordElapsed >= mRecordMaxSeconds)
		{
			mRecordShouldFinish = true;
		}
		const bool sampleFrame = (mRecordFrameCounter % mRecordEveryNth) == 0;
		++mRecordFrameCounter;
		if (!sampleFrame)
		{
			return;
		}
		// the window camera drives the cheap in-view test (projectPoint);
		// absent (UI-only/no camera) -> the visible field is omitted
		optr<RenderCamera> camera;
		if (RenderSystem* renderSystem = RenderSystem::get())
		{
			camera = renderSystem->getWindowCamera();
		}
		std::vector<TraceWriter::ObjectSample> samples;
		for (auto const & [id, gameObject] : gameObjectManager.getGameObjects())
		{
			if (id.empty() || !gameObject)
			{
				continue;	// only NAMED objects
			}
			if (!mRecordFilter.empty() &&
				mRecordFilter.find(id) == mRecordFilter.end())
			{
				continue;	// narrowed by the record filter
			}
			if (!gameObject->hasComponent<TransformComponent>())
			{
				continue;	// no transform -> no position to trace
			}
			TransformComponent* transform =
				gameObject->getComponentPtr<TransformComponent>();
			TraceWriter::ObjectSample sample;
			sample.id = id;
			sample.name = id;	// the object id is its human name in this engine
			const Vec3 position = transform->getWorldPosition();
			sample.pos[0] = position.x;
			sample.pos[1] = position.y;
			sample.pos[2] = position.z;
			if (gameObject->hasComponent<RigidBodyComponent>())
			{
				const Vec3 velocity = gameObject
					->getComponentPtr<RigidBodyComponent>()->getLinearVelocity();
				sample.hasVelocity = true;
				sample.vel[0] = velocity.x;
				sample.vel[1] = velocity.y;
				sample.vel[2] = velocity.z;
			}
			sample.active = gameObject->isActiveInHierarchy();
			if (camera)
			{
				Real nx = 0.0f, ny = 0.0f;
				const bool inView = camera->projectPoint(position, nx, ny) &&
					nx >= 0.0f && nx <= 1.0f && ny >= 0.0f && ny <= 1.0f;
				sample.visible = inView ? 1 : 0;
			}
			samples.push_back(std::move(sample));
		}
		// the process footprint rides every sample line ("mem" bytes) so an
		// agent can assert "no unbounded growth" straight off the trace
		const std::size_t residentBytes = sampleMemory();
		// so do the engine-level allocation count ("alloc") and the CPU
		// profiler's top-level frame phases ("phases") - the per-frame
		// where-does-it-go answer, readable straight off the trace
		const long long allocFrame = MemoryManager::framesSampled() > 0
			? static_cast<long long>(MemoryManager::lastFrameTotal()) : -1;
		std::vector<TraceWriter::PhaseSample> phases;
		if (ProfileManager::isEnabled())
		{
			ProfileManager::snapshot(mProfileScratch);
			for (ProfileManager::SnapshotNode const & node : mProfileScratch)
			{
				if (node.depth == 0 && node.calls > 0)
				{
					phases.push_back({ node.name, node.milliseconds });
				}
			}
		}
		mTrace->addSample(mRecordElapsed, frameCount, deltaSeconds, samples,
			residentBytes > 0 ? static_cast<long long>(residentBytes) : -1,
			allocFrame, phases);
	}
	//---------------------------------------------------------
	void PlayerDebugLink::traceContact(String const & nameA, String const & nameB,
		bool began)
	{
		if (!mActive || !mRecording || !mTrace)
		{
			return;
		}
		// respect the filter: skip a contact unless a named side is in scope
		if (!mRecordFilter.empty() &&
			mRecordFilter.find(nameA) == mRecordFilter.end() &&
			mRecordFilter.find(nameB) == mRecordFilter.end())
		{
			return;
		}
		mTrace->addEvent(mRecordElapsed, mRecordLastFrame,
			began ? "contactBegin" : "contactEnd",
			{ { "a", nameA }, { "b", nameB } });
	}
	//---------------------------------------------------------
	void PlayerDebugLink::traceScriptEvents()
	{
		// harvest the events the bus emitted this frame (script emit / gui /
		// engine mirrors) into the trace's event stream, keeping the buffer from
		// growing. The buffer only fills while trace capture is on, so this is a
		// cheap take when idle. Script events are a separate stream from the
		// object-scoped samples, so the record id-filter does not gate them.
		std::vector<ScriptEventBus::FrameEvent> events =
			ScriptEventBus::getSingleton().takeFrameEvents();
		if (!mActive || !mRecording || !mTrace)
		{
			return;
		}
		for (ScriptEventBus::FrameEvent const & event : events)
		{
			mTrace->addEvent(mRecordElapsed, mRecordLastFrame, event.name,
				event.fields);
		}
	}
	//---------------------------------------------------------
	bool PlayerDebugLink::recordingShouldFinish() const
	{
		return mActive && mRecording && mRecordShouldFinish;
	}
	//---------------------------------------------------------
	void PlayerDebugLink::finishRecording()
	{
		if (!mActive || !mRecording)
		{
			return;
		}
		mRecording = false;
		mRecordShouldFinish = false;
		// stop the bus recording emits and drop the tail buffer
		ScriptEventBus::getSingleton().setTraceCapture(false);
		ScriptEventBus::getSingleton().takeFrameEvents();
		bool ok = false;
		String error;
		if (mTrace && !mTrace->empty())
		{
			ok = mTrace->save(mRecordPath);
			if (!ok)
			{
				error = "trace wrote no file";
			}
		}
		else
		{
			error = "no frames sampled";
		}
		if (linkHasClient())
		{
			DebugMessage saved(Protocol::MSG_RECORD_SAVED);
			saved.set(Protocol::FIELD_PATH, mRecordPath);
			saved.set(Protocol::FIELD_VALUE, ok ? "1" : "0");
			if (!ok)
			{
				saved.set(Protocol::FIELD_MESSAGE, error);
			}
			linkSend(saved);
		}
		mTrace.reset();
		mRecordFilter.clear();
		mRecordPath.clear();
	}
	//---------------------------------------------------------
	//! record an event on the active trace (a no-op when idle) - the internal
	//! hook the scene-reload / script-error / warning observers call
	void PlayerDebugLink::traceEvent(String const & event,
		std::vector<std::pair<String, String>> const & fields)
	{
		if (mActive && mRecording && mTrace)
		{
			mTrace->addEvent(mRecordElapsed, mRecordLastFrame, event, fields);
		}
	}
	//---------------------------------------------------------
	//--- the transport seam (listen vs. dial) -----------------
	//---------------------------------------------------------
	void PlayerDebugLink::linkUpdate()
	{
		if (!mDialMode)
		{
			mServer.update();
			return;
		}
		mDialer.update();
		// synthesize the connect/disconnect edges the listen transport
		// reports natively
		const bool connected = mDialer.isConnected();
		if (connected && !mDialWasConnected)
		{
			mDialConnectedEvent = true;
			mDialEverConnected = true;
		}
		if (!connected && mDialWasConnected)
		{
			mDialDisconnectedEvent = true;
		}
		mDialWasConnected = connected;
		// the retry loop only serves the FIRST attachment; once a session
		// existed its end is final (update() winds the link down on the
		// disconnect edge)
		if (connected || mDialEverConnected || mDialer.isConnecting())
		{
			return;
		}
		const std::chrono::steady_clock::time_point now =
			std::chrono::steady_clock::now();
		if (now - mDialStart >
			std::chrono::seconds(DIAL_GIVE_UP_SECONDS))
		{
			deactivateStandalone("no editor answered the debug dial - "
				"running standalone");
			return;
		}
		if (now - mDialLastAttempt >
			std::chrono::milliseconds(DIAL_RETRY_INTERVAL_MS))
		{
			mDialer.connect(mDialHost, mDialPort);
			mDialLastAttempt = now;
		}
	}
	//---------------------------------------------------------
	bool PlayerDebugLink::linkHasClient() const
	{
		return mDialMode ? mDialer.isConnected() : mServer.hasClient();
	}
	//---------------------------------------------------------
	bool PlayerDebugLink::linkSend(DebugMessage const & message)
	{
		return mDialMode ? mDialer.send(message) : mServer.send(message);
	}
	//---------------------------------------------------------
	bool PlayerDebugLink::linkReceive(DebugMessage & out)
	{
		return mDialMode ? mDialer.receive(out) : mServer.receive(out);
	}
	//---------------------------------------------------------
	bool PlayerDebugLink::linkConsumeConnected()
	{
		if (!mDialMode)
		{
			return mServer.consumeClientConnected();
		}
		const bool edge = mDialConnectedEvent;
		mDialConnectedEvent = false;
		return edge;
	}
	//---------------------------------------------------------
	bool PlayerDebugLink::linkConsumeDisconnected()
	{
		if (!mDialMode)
		{
			return mServer.consumeClientDisconnected();
		}
		const bool edge = mDialDisconnectedEvent;
		mDialDisconnectedEvent = false;
		return edge;
	}
	//---------------------------------------------------------
	void PlayerDebugLink::linkStop()
	{
		if (mDialMode)
		{
			mDialer.disconnect();
		}
		else
		{
			mServer.stop();
		}
	}
	//---------------------------------------------------------
	void PlayerDebugLink::deactivateStandalone(String const & reason)
	{
		EngineLogCapture::logMessage("orkige runtime: " + reason);
		if (mLogCapture)
		{
			mLogCapture->detach();
		}
		linkStop();
		mActive = false;
	}
	//---------------------------------------------------------
	void PlayerDebugLink::update(GameObjectManager & gameObjectManager,
		String const & scenePath)
	{
		if (!mActive)
		{
			return;
		}
		linkUpdate();
		if (linkConsumeConnected())
		{
			DebugMessage hello(Protocol::MSG_HELLO);
			hello.set(Protocol::FIELD_SCENE, scenePath);
			linkSend(hello);
			sendHierarchyIfChanged(gameObjectManager, true);
			// goes through the log capture - guarantees the editor Console
			// receives at least one [remote] line per session
			EngineLogCapture::logMessage(
				"orkige runtime: debug link up - forwarding the runtime log "
				"to the editor");
		}
		if (linkConsumeDisconnected())
		{
			// a vanished editor must not leave the game frozen
			mPaused = false;
			mPendingSteps = 0;
			mSelectedObjectId.clear();
			mHierarchySent = false;
			// a NEW editor session must learn about existing failures again
			mReportedScriptErrors.clear();
			if (mDialMode)
			{
				// the dialed-out session ended (the editor stopped it or the
				// tab's peer went away): there is no listener to re-dial and
				// nothing left to serve - the game continues standalone
				deactivateStandalone("debug link closed - continuing "
					"standalone");
				return;
			}
		}
		processMessages(gameObjectManager);
	}
	//---------------------------------------------------------
	void PlayerDebugLink::stream(GameObjectManager & gameObjectManager,
		unsigned long frameCount)
	{
		if (!mActive || !linkHasClient())
		{
			return;
		}
		if (frameCount % HIERARCHY_CHECK_INTERVAL == 0)
		{
			sendHierarchyIfChanged(gameObjectManager, false);
			sendNewScriptErrors(gameObjectManager);
			// runtime metrics ride the same slow cadence as the hierarchy
			// check (~4 Hz) - the memory footprint moves slowly, so a few
			// samples a second is plenty and keeps the query cost negligible
			streamStats();
			// the gui widget rects ride the same cadence: the safe-area
			// device test reads them to assert the HUD sits inside the notch box
			streamUiLayout();
			// the CPU frame profile too (only streamed while the profiler is
			// enabled - Debug default on, Release off until MSG_PROFILE arms it)
			streamProfile();
		}
		streamObjectState(gameObjectManager);
		// forward the engine-log lines captured since the last frame
		for (EngineLogCapture::Line const & line : mLogCapture->drain())
		{
			DebugMessage log(Protocol::MSG_LOG);
			log.set(Protocol::FIELD_MESSAGE, line.text);
			log.set(Protocol::FIELD_LEVEL, line.level);
			linkSend(log);
			// warning-and-above lines also land in an active trace as events
			if (line.level == "warning" || line.level == "error")
			{
				traceEvent("warning",
					{ { "level", line.level }, { "message", line.text } });
			}
		}
	}
	//---------------------------------------------------------
	void PlayerDebugLink::onSceneReloaded()
	{
		// the selected object belonged to the torn-down world - forget it so
		// the object_state stream does not chase a dangling id
		mSelectedObjectId.clear();
		// the new scene's hierarchy differs; force a full re-send next stream()
		mHierarchySent = false;
		mLastSentHierarchy.clear();
		mLastSentParents.clear();
		mLastSentActives.clear();
		// a deferred level/scene switch happened mid-play - mark it in the trace
		traceEvent("sceneLoad", {});
	}
	//---------------------------------------------------------
	void PlayerDebugLink::shutdown()
	{
		if (!mActive)
		{
			return;
		}
		// the capture may outlive the engine otherwise - detach it from
		// the engine log first
		if (mLogCapture)
		{
			mLogCapture->detach();
		}
		// orderly protocol shutdown: tell the editor we are going down (the
		// quit path already sent bye) and give the socket a moment to flush
		if (linkHasClient())
		{
			if (!mQuitRequested)
			{
				linkSend(DebugMessage(Protocol::MSG_BYE));
			}
			for (int flush = 0; flush < 10; ++flush)
			{
				linkUpdate();
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		}
		linkStop();
		mActive = false;
	}
	//---------------------------------------------------------
	void PlayerDebugLink::sendError(String const & text)
	{
		DebugMessage error(Protocol::MSG_ERROR);
		error.set(Protocol::FIELD_MESSAGE, text);
		linkSend(error);
	}
	//---------------------------------------------------------
	void PlayerDebugLink::sendHierarchyIfChanged(
		GameObjectManager & gameObjectManager, bool force)
	{
		if (!linkHasClient())
		{
			return;
		}
		StringVector ids;
		StringVector parents;
		StringVector actives;
		collectHierarchy(gameObjectManager, ids, parents, actives);
		if (!force && mHierarchySent && ids == mLastSentHierarchy &&
			parents == mLastSentParents && actives == mLastSentActives)
		{
			return;
		}
		DebugMessage hierarchy(Protocol::MSG_HIERARCHY);
		hierarchy.setList(Protocol::LIST_IDS, ids);
		hierarchy.setList(Protocol::LIST_PARENTS, parents);
		hierarchy.setList(Protocol::LIST_ACTIVE, actives);
		linkSend(hierarchy);
		mLastSentHierarchy = std::move(ids);
		mLastSentParents = std::move(parents);
		mLastSentActives = std::move(actives);
		mHierarchySent = true;
	}
	//---------------------------------------------------------
	//! @brief push a script_error message for every GameObject whose
	//! ScriptComponent has failed and was not reported to THIS client yet -
	//! script failures must be loud in the editor even for objects the user
	//! never selects (object_state only streams the selected one). This covers
	//! BOTH the fatal update/load error (hasScriptError, the instance disabled
	//! itself) AND a non-fatal hot-reload failure (hasReloadError, the OLD
	//! instance kept running) - a broken live edit must be just as loud, even
	//! though mFailed stays false; handleReloadScript re-arms the report set so
	//! a re-break re-reports and a healed reload clears.
	void PlayerDebugLink::sendNewScriptErrors(
		GameObjectManager & gameObjectManager)
	{
		for (auto const & [id, gameObject] :
			gameObjectManager.getGameObjects())
		{
			// EVERY script on the object is checked - an object may carry
			// several behavior scripts, and each can break independently
			for (ScriptComponent * script :
				ScriptComponent::collectFrom(*gameObject))
			{
				const bool broken =
					script->hasScriptError() || script->hasReloadError();
				// the report set is keyed per SCRIPT (object + kind name), so
				// two broken scripts on one object both report exactly once
				const String key = id + "\n" + script->getComponentName();
				if (!broken || mReportedScriptErrors.count(key) != 0)
				{
					continue;
				}
				// a hot-reload failure is the fresher, more actionable message
				// when present (the old instance is still alive); else the fatal one
				const String & message = script->hasReloadError()
					? script->getLastReloadError() : script->getScriptError();
				DebugMessage error(Protocol::MSG_SCRIPT_ERROR);
				error.set(Protocol::FIELD_ID, id);
				// name the culprit so the editor Console can point at the right
				// script when an object carries several
				error.set(Protocol::FIELD_COMPONENT, script->getComponentName());
				error.set(Protocol::FIELD_MESSAGE, message);
				linkSend(error);
				mReportedScriptErrors.insert(key);
				// mirror it into an active trace as an event
				traceEvent("scriptError",
					{ { "object", id }, { "component", script->getComponentName() },
					{ "message", message } });
			}
		}
	}
	//---------------------------------------------------------
	//! @brief set_property: GENERIC write driven off the reflection registry
	//! Resolve the (component,property) descriptor in the schema,
	//! parse the wire string into a correctly-typed PropertyValue and call the
	//! reflected setter - which routes to the component's real accessor, so the
	//! effect takes hold live (a Sprite reloads its texture, a RigidBody rebuilds,
	//! a Transform moves). A read-only property, an unknown object/component/
	//! property or a value that fails to parse answers with an error - never
	//! crashes. The hand-written per-component write switch is retired.
	void PlayerDebugLink::handleSetProperty(
		GameObjectManager & gameObjectManager, DebugMessage const & message)
	{
		const String & id = message.get(Protocol::FIELD_ID);
		const String & component = message.get(Protocol::FIELD_COMPONENT);
		const String & property = message.get(Protocol::FIELD_PROPERTY);
		const String & value = message.get(Protocol::FIELD_VALUE);
		optr<GameObject> gameObject =
			gameObjectManager.getGameObject(id).lock();
		if (!gameObject)
		{
			sendError("set_property: no GameObject '" + id + "'");
			return;
		}
		const TypeInfo componentType(component);
		if (!gameObject->hasComponent(componentType))
		{
			sendError("set_property: '" + id + "' has no " + component);
			return;
		}
		GameObjectComponent * instance =
			gameObject->getComponentPtr(componentType);
		if (!instance)
		{
			sendError("set_property: '" + id + "' has no " + component);
			return;
		}
		// the union schema (static per-type + dynamic per-instance) so a
		// ScriptComponent's exported properties are writable too
		const PropertySchema schema = getComponentSchema(*instance);
		PropertyDesc const * desc = schema.find(property);
		if (!desc)
		{
			sendError("set_property: unknown property " + component + "." +
				property);
			return;
		}
		if (desc->isReadOnly())
		{
			sendError("set_property: " + component + "." + property +
				" is read-only");
			return;
		}
		// read the current value to obtain a correctly-typed carrier (kind +
		// enum-type/reference hint), then parse the wire string into it
		PropertyValue reflected = desc->get(instance);
		String parseError;
		if (!reflected.fromString(value, &parseError))
		{
			sendError("set_property: bad value for " + component + "." +
				property + " ('" + value + "'): " + parseError);
			return;
		}
		// keep the historical orientation guarantee: an editor drag can send an
		// unnormalized quaternion, and the scene node does not normalize
		if (desc->kind == PropertyKind::Quat)
		{
			const PropQuat raw = reflected.asQuat();
			Quat quat(raw.w, raw.x, raw.y, raw.z);
			quat.normalise();
			PropQuat normalized;
			normalized.w = quat.w;
			normalized.x = quat.x;
			normalized.y = quat.y;
			normalized.z = quat.z;
			reflected = PropertyValue::makeQuat(normalized);
		}
		desc->set(instance, reflected);
	}
	//---------------------------------------------------------
	//! @brief reload_script (hot-reload): recompile-and-swap the
	//! ScriptComponents of a target GameObject (FIELD_ID) or ALL of them
	//! (FIELD_ID absent, the v1 reload-ALL). Player-directed: the editor's
	//! file watcher only sends this; the swap (compile-before-swap failure
	//! containment) happens HERE, on the running player. Each reloaded id is
	//! cleared from mReportedScriptErrors so a re-break re-reports and a healed
	//! reload lets the error clear; the fresh error state is pushed right away.
	void PlayerDebugLink::handleReloadScript(
		GameObjectManager & gameObjectManager, DebugMessage const & message)
	{
		const String & targetId = message.get(Protocol::FIELD_ID); // "" = all
		if (!targetId.empty() && !gameObjectManager.objectExists(targetId))
		{
			sendError("reload_script: no GameObject '" + targetId + "'");
			return;
		}
		int reloaded = 0;
		for (auto const & [id, gameObject] :
			gameObjectManager.getGameObjects())
		{
			if (!targetId.empty() && id != targetId)
			{
				continue;
			}
			// hot-reload EVERY script on the object - an object may carry several
			// behavior scripts; each hotReload is compile-before-swap and
			// preserves engine-side + shared state, so re-arming siblings is safe
			for (ScriptComponent * script :
				ScriptComponent::collectFrom(*gameObject))
			{
				script->hotReload();
				// re-arm reporting per script: a still-/newly-broken instance
				// must re-report, a healed one clears on the next stream tick
				mReportedScriptErrors.erase(id + "\n" + script->getComponentName());
				++reloaded;
			}
		}
		// surface any reload that just FAILED immediately (its old instance is
		// still ticking, but the editor must see the broken edit at once)
		sendNewScriptErrors(gameObjectManager);
		EngineLogCapture::logMessage("orkige runtime: hot-reloaded " +
			std::to_string(reloaded) + " script(s)" +
			(targetId.empty() ? String() : " on '" + targetId + "'"));
	}
	//---------------------------------------------------------
	//! @brief reload_ui (.oui hot-reload): destroy-and-rebuild one declarative
	//! gui screen from its fresh file. FIELD_PATH is the .oui name the game
	//! passed to GuiFactory::loadLayout. Player-directed like reload_script: the
	//! editor's .oui watcher only sends this; the swap happens here, at the
	//! message-drain point. Clean cutover - GuiManager::reloadLayout parses the
	//! fresh file FIRST, so a broken edit keeps the OLD screen and answers with
	//! an error; a successful rebuild emits the `ui.reloaded` script event so a
	//! script re-acquires its now-stale widget handles.
	void PlayerDebugLink::handleReloadUi(DebugMessage const & message)
	{
		const String & file = message.get(Protocol::FIELD_PATH);
		if (file.empty())
		{
			sendError("reload_ui: missing path");
			return;
		}
		GuiManager* gui = GuiManager::getSingletonPtr();
		if (gui == NULL)
		{
			sendError("reload_ui: no UI system on the running game");
			return;
		}
		String error;
		if (!gui->reloadLayout(file, error))
		{
			// the old screen stays up; surface the parse/read error to the editor
			sendError("reload_ui '" + file + "': " + error);
			return;
		}
		// mirror onto the script event bus so a script re-acquires its handles
		// to the just-rebuilt widgets (the old ones were destroyed)
		ScriptEventPayload payload;
		payload.setString("file", file);
		ScriptEventBus::getSingleton().emit("ui.reloaded", payload);
		EngineLogCapture::logMessage("orkige runtime: hot-reloaded ui '" +
			file + "'");
	}
	//---------------------------------------------------------
	//! @brief reload_anim (vector-animation hot-reload): re-read one `.oanim`
	//! rig and rebuild every VectorAnimationComponent playing it. FIELD_PATH is
	//! the rig's resource name (the value a component's `animation` reference
	//! holds). Player-directed like reload_ui: the editor's animation watcher
	//! (which re-cooks a changed Lottie source first) and the MCP verb only
	//! send this; the swap happens here, at the message-drain point. The fresh
	//! file is parsed AND rig-built FIRST - VectorAnimationComponent's own
	//! loadAnimation tears the old mesh down before parsing, so the guard must
	//! sit in front of it: a broken edit keeps every OLD rig playing and
	//! answers with an error carrying the parse line and reason. The resource
	//! read serves fresh bytes by construction (the archive caches the file
	//! INDEX, not contents, and a hot-reload overwrites an existing name - the
	//! same guarantee reload_ui already relies on). A successful rebuild emits
	//! the `animation.reloaded` script event so scripts re-drive their clips.
	void PlayerDebugLink::handleReloadAnim(
		GameObjectManager & gameObjectManager, DebugMessage const & message)
	{
		const String & file = message.get(Protocol::FIELD_PATH);
		if (file.empty())
		{
			sendError("reload_anim: missing path");
			return;
		}
		// parse-and-build-before-swap: only a file that makes a valid rig may
		// touch any component
		String text;
		if (RenderSystem::get() == NULL ||
			!RenderSystem::get()->readResourceText(file, text))
		{
			sendError("reload_anim '" + file + "': not found");
			return;
		}
		VectorAnimAsset::Document document;
		VectorAnimAsset::ParseError parseError;
		if (!VectorAnimAsset::parse(text, document, &parseError))
		{
			sendError("reload_anim '" + file + "': line " +
				std::to_string(parseError.line) + ": " + parseError.message);
			return;
		}
		{
			VectorAnimEval probe;
			if (!probe.build(document))
			{
				sendError("reload_anim '" + file + "': not a valid rig");
				return;
			}
		}
		// clean cutover: rebuild every component playing this rig (loadAnimation
		// re-reads the same fresh bytes and restarts at the reflected clip)
		int reloaded = 0;
		for (auto const & [id, gameObject] :
			gameObjectManager.getGameObjects())
		{
			for (auto const & entry : gameObject->getComponents())
			{
				VectorAnimationComponent * animation =
					dynamic_cast<VectorAnimationComponent*>(entry.second.get());
				if (animation && animation->getAnimationName() == file)
				{
					animation->loadAnimation(file);
					++reloaded;
				}
			}
		}
		// mirror onto the script event bus so a script re-drives the fresh rig
		// (a renamed clip falls back with its own warning, like any load)
		ScriptEventPayload payload;
		payload.setString("file", file);
		ScriptEventBus::getSingleton().emit("animation.reloaded", payload);
		EngineLogCapture::logMessage("orkige runtime: hot-reloaded animation '" +
			file + "' on " + std::to_string(reloaded) + " component(s)");
	}
	//---------------------------------------------------------
	//! @brief set_cvar: change a console variable on the RUNNING
	//! player live. CVarManager::setString parses+validates the value per the
	//! cvar's registered type and fires its onChange (the live re-apply seam),
	//! so a `set` in the editor Console tunes the running game at once. An
	//! unknown name or a value the type rejects answers with an error - never
	//! crashes (parallel to handleSetProperty). The registry is a core
	//! singleton, so this handler needs no GameObjectManager.
	void PlayerDebugLink::handleSetCvar(DebugMessage const & message)
	{
		const String & name = message.get(Protocol::FIELD_CVAR_NAME);
		const String & value = message.get(Protocol::FIELD_VALUE);
		String error;
		if (!CVarManager::getSingleton().setString(name, value, &error))
		{
			sendError("set_cvar: " + error);
			return;
		}
		EngineLogCapture::logMessage("orkige runtime: cvar '" + name +
			"' = " + value);
	}
	//---------------------------------------------------------
	//! drain and act on every queued editor command
	void PlayerDebugLink::processMessages(
		GameObjectManager & gameObjectManager)
	{
		DebugMessage message;
		while (linkReceive(message))
		{
			if (message.type == Protocol::MSG_PAUSE)
			{
				mPaused = true;
				mPendingSteps = 0;
			}
			else if (message.type == Protocol::MSG_RESUME)
			{
				mPaused = false;
				mPendingSteps = 0;
			}
			else if (message.type == Protocol::MSG_STEP)
			{
				if (mPaused)
				{
					++mPendingSteps;
				}
				else
				{
					sendError("step: not paused");
				}
			}
			else if (message.type == Protocol::MSG_QUIT)
			{
				mQuitRequested = true;
				linkSend(DebugMessage(Protocol::MSG_BYE));
			}
			else if (message.type == Protocol::MSG_SELECT)
			{
				const String & id = message.get(Protocol::FIELD_ID);
				if (id.empty() || gameObjectManager.objectExists(id))
				{
					mSelectedObjectId = id;
					// stream the first state message right away
					mLastStateSend = std::chrono::steady_clock::time_point();
				}
				else
				{
					sendError("select: no GameObject '" + id + "'");
				}
			}
			else if (message.type == Protocol::MSG_SET_PROPERTY)
			{
				handleSetProperty(gameObjectManager, message);
			}
			else if (message.type == Protocol::MSG_SET_ACTIVE)
			{
				const String & id = message.get(Protocol::FIELD_ID);
				optr<GameObject> gameObject =
					gameObjectManager.getGameObject(id).lock();
				if (gameObject)
				{
					gameObject->setActive(
						message.get(Protocol::FIELD_VALUE) == "1");
					// the editor tree mirrors the change immediately
					sendHierarchyIfChanged(gameObjectManager, false);
				}
				else
				{
					sendError("set_active: no GameObject '" + id + "'");
				}
			}
			else if (message.type == Protocol::MSG_REQUEST_HIERARCHY)
			{
				sendHierarchyIfChanged(gameObjectManager, true);
			}
			else if (message.type == Protocol::MSG_RELOAD_SCRIPT)
			{
				// Lua hot-reload (editor-driven, compile-before-swap)
				handleReloadScript(gameObjectManager, message);
			}
			else if (message.type == Protocol::MSG_RELOAD_UI)
			{
				// .oui hot-reload (editor-driven): destroy-and-rebuild the named
				// screen at this message-drain point, never mid-frame
				handleReloadUi(message);
			}
			else if (message.type == Protocol::MSG_RELOAD_ANIM)
			{
				// .oanim hot-reload (editor-driven): parse-before-swap, then
				// rebuild the rig's components at this message-drain point
				handleReloadAnim(gameObjectManager, message);
			}
			else if (message.type == Protocol::MSG_SET_CVAR)
			{
				// cvars: tune a console variable on the running player
				handleSetCvar(message);
			}
			else if (message.type == Protocol::MSG_SCREENSHOT)
			{
				// screenshot the running game: record the target path; the
				// main loop captures the window AFTER rendering (renderer
				// containment) and reports back with notifyScreenshotSaved
				const String & path = message.get(Protocol::FIELD_PATH);
				if (path.empty())
				{
					sendError("screenshot: missing path");
				}
				else
				{
					mHasPendingScreenshot = true;
					mPendingScreenshotPath = path;
				}
			}
			else if (message.type == Protocol::MSG_RECORD_START)
			{
				// arm a trace of the running game: the main loop samples the
				// world every Nth frame and answers with MSG_RECORD_SAVED
				handleRecordStart(message);
			}
			else if (message.type == Protocol::MSG_RECORD_STOP)
			{
				// end an in-flight trace early; the main loop wraps it up
				// (write + ack) on its next recording check
				if (mRecording)
				{
					mRecordShouldFinish = true;
				}
				else
				{
					sendError("record: not recording");
				}
			}
			else if (message.type == Protocol::MSG_PROFILE)
			{
				// arm/disarm the runtime CPU profiler; once enabled the
				// profile stream follows on the stats cadence
				ProfileManager::setEnabled(
					message.get(Protocol::FIELD_VALUE) != "0");
			}
			else if (message.type == Protocol::MSG_GUI_PRESS)
			{
				// synthesize a press+release on a gui widget at its centre,
				// routed through the REAL input path so modal/disabled semantics
				// apply (an agent driving a dialog button, or asserting a button
				// under a scrim stays inert)
				const String & id = message.get(Protocol::FIELD_ID);
				GuiManager* gui = GuiManager::getSingletonPtr();
				if (gui == NULL || !gui->widgetExists(id))
				{
					sendError("gui_press: unknown widget '" + id + "'");
				}
				else if (InputManager::getSingletonPtr() != NULL)
				{
					optr<GuiWidget> widget = gui->getWidget(id).lock();
					if (widget)
					{
						const auto pos = widget->getPosition();
						const auto size = widget->getSize();
						const float cx = pos.x + size.x * 0.5f;
						const float cy = pos.y + size.y * 0.5f;
						SDL_Event down{};
						down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
						down.button.button = SDL_BUTTON_LEFT;
						down.button.down = true;
						down.button.x = cx;
						down.button.y = cy;
						InputManager::getSingleton().injectEvent(down);
						SDL_Event up{};
						up.type = SDL_EVENT_MOUSE_BUTTON_UP;
						up.button.button = SDL_BUTTON_LEFT;
						up.button.down = false;
						up.button.x = cx;
						up.button.y = cy;
						InputManager::getSingleton().injectEvent(up);
					}
				}
			}
			else if (message.type == Protocol::MSG_GUI_DISMISS_MODAL)
			{
				// close a modal by id, or the topmost one when no id is given
				GuiManager* gui = GuiManager::getSingletonPtr();
				if (gui != NULL)
				{
					const String & id = message.get(Protocol::FIELD_ID);
					if (id.empty())
					{
						gui->dismissTopModal();
					}
					else
					{
						gui->dismissModal(id);
					}
				}
			}
			// --- protocol-extension slot -------------------------------------
			// Additive editor->runtime messages ride THIS one debug protocol as
			// new else-if branches (old players hit the unknown-else below and
			// answer honestly - never crash). Keep the chain flat and each
			// branch a thin dispatch to a handle*() method. reload and cvars
			// are wired above; the remaining slot is:
			//   MCP   -> the editor-side MCP server TRANSLATES its play-
			//                control verbs into these same messages (no second
			//                player port) - add its verbs here the same way.
			else
			{
				sendError("unknown command '" + message.type + "'");
			}
		}
	}
	//---------------------------------------------------------
	//! stream the selected object's state at ~15Hz
	void PlayerDebugLink::streamObjectState(
		GameObjectManager & gameObjectManager)
	{
		if (!linkHasClient() || mSelectedObjectId.empty())
		{
			return;
		}
		const std::chrono::steady_clock::time_point now =
			std::chrono::steady_clock::now();
		if (now - mLastStateSend <
			std::chrono::milliseconds(OBJECT_STATE_INTERVAL_MS))
		{
			return;
		}
		optr<GameObject> gameObject =
			gameObjectManager.getGameObject(mSelectedObjectId).lock();
		if (!gameObject)
		{
			sendError("selected GameObject '" + mSelectedObjectId +
				"' no longer exists");
			mSelectedObjectId.clear();
			return;
		}
		linkSend(buildObjectState(gameObject));
		mLastStateSend = now;
	}
	//---------------------------------------------------------
	std::size_t PlayerDebugLink::sampleMemory()
	{
		const std::size_t residentBytes = MemorySampler::residentBytes();
		if (residentBytes > mPeakResidentBytes)
		{
			mPeakResidentBytes = residentBytes;
		}
		return residentBytes;
	}
	//---------------------------------------------------------
	void PlayerDebugLink::streamStats()
	{
		if (!linkHasClient())
		{
			return;
		}
		DebugMessage stats(Protocol::MSG_STATS);
		bool anyField = false;
		const std::size_t residentBytes = sampleMemory();
		if (residentBytes != 0)	// omit the metric where the platform has none
		{
			stats.set(Protocol::FIELD_MEM_RSS, std::to_string(residentBytes));
			stats.set(Protocol::FIELD_MEM_RSS_PEAK,
				std::to_string(mPeakResidentBytes));
			anyField = true;
		}
		// engine-level allocation counters (tracked seams, per frame - see
		// core_debug/MemoryManager.h) - present once the player loop has
		// completed a frame boundary
		if (MemoryManager::framesSampled() > 0)
		{
			stats.set(Protocol::FIELD_ALLOC_PER_FRAME,
				std::to_string(MemoryManager::lastFrameTotal()));
			stats.set(Protocol::FIELD_ALLOC_PEAK,
				std::to_string(MemoryManager::peakFrameTotal()));
			StringVector allocTags;
			StringVector allocCounts;
			for (int tag = 0; tag < MemoryManager::TAG_COUNT; ++tag)
			{
				const MemoryManager::AllocTag allocTag =
					static_cast<MemoryManager::AllocTag>(tag);
				allocTags.push_back(MemoryManager::tagName(allocTag));
				allocCounts.push_back(
					std::to_string(MemoryManager::lastFrameCount(allocTag)));
			}
			stats.setList(Protocol::LIST_ALLOC_TAGS, allocTags);
			stats.setList(Protocol::LIST_ALLOC_COUNTS, allocCounts);
			anyField = true;
		}
		// the frame's wall time (measured at the frame boundary even when
		// scope timing is off; needs two boundaries for a duration)
		if (ProfileManager::framesSampled() > 1)
		{
			std::ostringstream frameMs;
			frameMs << std::fixed << std::setprecision(3)
				<< ProfileManager::lastFrameMilliseconds();
			stats.set(Protocol::FIELD_FRAME_MS, frameMs.str());
			anyField = true;
		}
		// the game's current named state (core_game/GameState, Lua game.setState)
		// so the editor Stats panel / MCP get_state can read what state the
		// running game is in. Omitted while unset (empty).
		if (GameState::getSingletonPtr() != NULL &&
			!GameState::getSingleton().get().empty())
		{
			stats.set(Protocol::FIELD_GAME_STATE,
				GameState::getSingleton().get());
			anyField = true;
		}
		// window size + safe-area insets (pixels): the notch-aware readback an
		// agent asserts the HUD against. Pulled from the platform window
		// (no Ogre spelling here - renderer containment). Absent without a
		// render system (headless).
		if (RenderSystem::get() != NULL)
		{
			unsigned int windowWidth = 0;
			unsigned int windowHeight = 0;
			RenderSystem::get()->getWindowSize(windowWidth, windowHeight);
			const SafeAreaInsets insets = PlatformWindow::getSafeAreaInsets(
				windowWidth, windowHeight);
			stats.set(Protocol::FIELD_WINDOW_W, std::to_string(windowWidth));
			stats.set(Protocol::FIELD_WINDOW_H, std::to_string(windowHeight));
			stats.set(Protocol::FIELD_SAFE_LEFT, std::to_string(insets.mLeft));
			stats.set(Protocol::FIELD_SAFE_TOP, std::to_string(insets.mTop));
			stats.set(Protocol::FIELD_SAFE_RIGHT, std::to_string(insets.mRight));
			stats.set(Protocol::FIELD_SAFE_BOTTOM,
				std::to_string(insets.mBottom));
			anyField = true;
		}
		// streamed-music snapshot: one entry per track (id, file, and a flat
		// numeric info string), so the editor Stats panel and the MCP get_state
		// surface can answer "what is playing, where, and how loud". Rides
		// MSG_STATS as three parallel lists (no nested objects). Omitted when
		// no track is registered or there is no SoundManager (headless).
		if (SoundManager::getSingletonPtr() != NULL)
		{
			std::vector<SoundManager::MusicTrackInfo> tracks =
				SoundManager::getSingleton().snapshotMusic();
			if (!tracks.empty())
			{
				StringVector ids;
				StringVector files;
				StringVector infos;
				for (SoundManager::MusicTrackInfo const & track : tracks)
				{
					ids.push_back(track.id);
					files.push_back(track.file);
					// "<playing> <positionSec> <durationSec> <baseGain>
					// <groupVolume> <effectiveGain> <loop>"
					std::ostringstream info;
					info << (track.playing ? 1 : 0) << ' '
						<< track.positionSec << ' '
						<< track.durationSec << ' '
						<< track.baseGain << ' '
						<< track.groupVolume << ' '
						<< track.effectiveGain << ' '
						<< (track.loop ? 1 : 0);
					infos.push_back(info.str());
				}
				stats.setList(Protocol::LIST_MUSIC_IDS, ids);
				stats.setList(Protocol::LIST_MUSIC_FILES, files);
				stats.setList(Protocol::LIST_MUSIC_INFO, infos);
				anyField = true;
			}
		}
		if (anyField)
		{
			linkSend(stats);
		}
	}
	//---------------------------------------------------------
	void PlayerDebugLink::streamUiLayout()
	{
		if (!linkHasClient())
		{
			return;
		}
		GuiManager* manager = GuiManager::getSingletonPtr();
		if (manager == NULL)
		{
			return;	// scriptless / HUD-less game: nothing to report
		}
		StringVector ids;
		StringVector rects;
		for (GuiManager::WidgetLayout const & layout :
			manager->getWidgetLayouts())
		{
			ids.push_back(layout.id);
			// one flat "left top width height visible" string per widget keeps
			// the message a pair of parallel lists (no nested objects)
			std::ostringstream rect;
			rect << (long)std::lround(layout.left) << ' '
				<< (long)std::lround(layout.top) << ' '
				<< (long)std::lround(layout.width) << ' '
				<< (long)std::lround(layout.height) << ' '
				<< (layout.visible ? 1 : 0) << ' '
				<< (layout.enabled ? 1 : 0) << ' '
				<< (layout.modal ? 1 : 0);
			rects.push_back(rect.str());
		}
		DebugMessage message(Protocol::MSG_UI_LAYOUT);
		message.setList(Protocol::LIST_UI_IDS, ids);
		message.setList(Protocol::LIST_UI_RECTS, rects);
		// the screen router's state, so an agent can observe the navigation path
		// (current top + the bottom-to-top stack) alongside the widget rects
		message.set(Protocol::FIELD_UI_SCREEN, manager->currentScreen());
		String screenPath;
		for (String const & name : manager->screenPath())
		{
			if (!screenPath.empty())
			{
				screenPath += ' ';
			}
			screenPath += name;
		}
		message.set(Protocol::FIELD_UI_SCREEN_STACK, screenPath);
		linkSend(message);
	}
	//---------------------------------------------------------
	void PlayerDebugLink::streamProfile()
	{
		if (!linkHasClient() || !ProfileManager::isEnabled())
		{
			return;
		}
		ProfileManager::snapshot(mProfileScratch);
		if (mProfileScratch.empty())
		{
			return;	// no completed frame with scopes yet
		}
		DebugMessage profile(Protocol::MSG_PROFILE_DATA);
		StringVector names;
		StringVector infos;
		names.reserve(mProfileScratch.size());
		infos.reserve(mProfileScratch.size());
		for (ProfileManager::SnapshotNode const & node : mProfileScratch)
		{
			names.push_back(node.name);
			std::ostringstream info;
			info << node.depth << ' ' << node.calls << ' '
				<< std::fixed << std::setprecision(3) << node.milliseconds
				<< ' ' << node.maxMilliseconds;
			infos.push_back(info.str());
		}
		profile.setList(Protocol::LIST_PROFILE_NAMES, names);
		profile.setList(Protocol::LIST_PROFILE_INFO, infos);
		std::ostringstream frameMs;
		frameMs << std::fixed << std::setprecision(3)
			<< ProfileManager::lastFrameMilliseconds();
		profile.set(Protocol::FIELD_FRAME_MS, frameMs.str());
		linkSend(profile);
	}
	//---------------------------------------------------------
}
