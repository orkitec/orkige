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

#include "core_debug/CVarManager.h"
#include "core_game/GameObjectManager.h"
#include "engine_base/EngineLog.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_gocomponent/ScriptComponent.h"
#include "engine_render/RenderMath.h"

// SDL_GetBasePath (PlayerBundle) - safe to call before SDL_Init
#include <SDL3/SDL_filesystem.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>

namespace Orkige
{
	namespace Protocol = DebugProtocol;

	const unsigned long PlayerDebugLink::HIERARCHY_CHECK_INTERVAL = 15;
	const int PlayerDebugLink::OBJECT_STATE_INTERVAL_MS = 66;
	//---------------------------------------------------------
	namespace
	{
		//! print a float with round-trip precision (matches
		//! DebugMessage::setFloat)
		String formatFloat(float value)
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.9g", value);
			return buffer;
		}

		String formatVector3(Vec3 const & v)
		{
			return formatFloat(v.x) + " " + formatFloat(v.y) + " " +
				formatFloat(v.z);
		}

		String formatQuaternion(Quat const & q)
		{
			return formatFloat(q.w) + " " + formatFloat(q.x) + " " +
				formatFloat(q.y) + " " + formatFloat(q.z);
		}

		//! parse exactly count whitespace-separated floats; false on junk
		bool parseFloats(String const & text, float * out, int count)
		{
			std::istringstream stream(text);
			for (int i = 0; i < count; ++i)
			{
				if (!(stream >> out[i]))
				{
					return false;
				}
			}
			String trailing;
			return !(stream >> trailing); // no trailing tokens allowed
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

		//! @brief object_state v1: per-component property snapshot via the
		//! known component getters. Exposed: TransformComponent position/
		//! orientation/scale, ModelComponent mesh, RigidBodyComponent
		//! body_type/has_body/linear_velocity/angular_velocity,
		//! ScriptComponent script/enabled/started/error; other components
		//! appear in the "components" list without properties.
		DebugMessage buildObjectState(optr<GameObject> const & gameObject)
		{
			DebugMessage state(Protocol::MSG_OBJECT_STATE);
			state.set(Protocol::FIELD_ID, gameObject->getObjectID());
			StringVector componentNames;
			for (auto const & [componentType, component] :
				gameObject->getComponents())
			{
				componentNames.push_back(componentType.getName());
			}
			state.setList(Protocol::LIST_COMPONENTS, componentNames);
			if (gameObject->hasComponent<TransformComponent>())
			{
				TransformComponent * transform =
					gameObject->getComponentPtr<TransformComponent>();
				state.set("TransformComponent.position",
					formatVector3(transform->getPosition()));
				state.set("TransformComponent.orientation",
					formatQuaternion(transform->getOrientation()));
				state.set("TransformComponent.scale",
					formatVector3(transform->getScale()));
			}
			if (gameObject->hasComponent<ModelComponent>())
			{
				state.set("ModelComponent.mesh",
					gameObject->getComponentPtr<ModelComponent>()
						->getCurrentModelFileName());
			}
			if (gameObject->hasComponent<RigidBodyComponent>())
			{
				RigidBodyComponent * rigidBody =
					gameObject->getComponentPtr<RigidBodyComponent>();
				const char * bodyTypeNames[] =
					{ "static", "kinematic", "dynamic" };
				const int bodyType = static_cast<int>(rigidBody->getBodyType());
				state.set("RigidBodyComponent.body_type",
					(bodyType >= 0 && bodyType <= 2)
						? bodyTypeNames[bodyType] : "?");
				state.set("RigidBodyComponent.has_body",
					rigidBody->hasBody() ? "1" : "0");
				state.set("RigidBodyComponent.linear_velocity",
					formatVector3(rigidBody->getLinearVelocity()));
				state.set("RigidBodyComponent.angular_velocity",
					formatVector3(rigidBody->getAngularVelocity()));
			}
			if (gameObject->hasComponent<ScriptComponent>())
			{
				// the live script state - this is what feeds the editor's
				// remote Inspector, including the "(script error)" indicator
				ScriptComponent * script =
					gameObject->getComponentPtr<ScriptComponent>();
				state.set("ScriptComponent.script", script->getScriptFile());
				state.set("ScriptComponent.enabled",
					script->isScriptEnabled() ? "1" : "0");
				state.set("ScriptComponent.started",
					script->isScriptStarted() ? "1" : "0");
				state.set("ScriptComponent.error", script->getScriptError());
			}
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
				if (std::filesystem::is_directory(bundledMediaDir + "/Main",
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
	void PlayerDebugLink::update(GameObjectManager & gameObjectManager,
		String const & scenePath)
	{
		if (!mActive)
		{
			return;
		}
		mServer.update();
		if (mServer.consumeClientConnected())
		{
			DebugMessage hello(Protocol::MSG_HELLO);
			hello.set(Protocol::FIELD_SCENE, scenePath);
			mServer.send(hello);
			sendHierarchyIfChanged(gameObjectManager, true);
			// goes through the log capture - guarantees the editor Console
			// receives at least one [remote] line per session
			EngineLogCapture::logMessage(
				"orkige runtime: debug link up - forwarding the runtime log "
				"to the editor");
		}
		if (mServer.consumeClientDisconnected())
		{
			// a vanished editor must not leave the game frozen
			mPaused = false;
			mPendingSteps = 0;
			mSelectedObjectId.clear();
			mHierarchySent = false;
			// a NEW editor session must learn about existing failures again
			mReportedScriptErrors.clear();
		}
		processMessages(gameObjectManager);
	}
	//---------------------------------------------------------
	void PlayerDebugLink::stream(GameObjectManager & gameObjectManager,
		unsigned long frameCount)
	{
		if (!mActive || !mServer.hasClient())
		{
			return;
		}
		if (frameCount % HIERARCHY_CHECK_INTERVAL == 0)
		{
			sendHierarchyIfChanged(gameObjectManager, false);
			sendNewScriptErrors(gameObjectManager);
		}
		streamObjectState(gameObjectManager);
		// forward the engine-log lines captured since the last frame
		for (EngineLogCapture::Line const & line : mLogCapture->drain())
		{
			DebugMessage log(Protocol::MSG_LOG);
			log.set(Protocol::FIELD_MESSAGE, line.text);
			log.set(Protocol::FIELD_LEVEL, line.level);
			mServer.send(log);
		}
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
		if (mServer.hasClient())
		{
			if (!mQuitRequested)
			{
				mServer.send(DebugMessage(Protocol::MSG_BYE));
			}
			for (int flush = 0; flush < 10; ++flush)
			{
				mServer.update();
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		}
		mServer.stop();
		mActive = false;
	}
	//---------------------------------------------------------
	void PlayerDebugLink::sendError(String const & text)
	{
		DebugMessage error(Protocol::MSG_ERROR);
		error.set(Protocol::FIELD_MESSAGE, text);
		mServer.send(error);
	}
	//---------------------------------------------------------
	void PlayerDebugLink::sendHierarchyIfChanged(
		GameObjectManager & gameObjectManager, bool force)
	{
		if (!mServer.hasClient())
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
		mServer.send(hierarchy);
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
			if (!gameObject->hasComponent<ScriptComponent>())
			{
				continue;
			}
			ScriptComponent * script =
				gameObject->getComponentPtr<ScriptComponent>();
			const bool broken =
				script->hasScriptError() || script->hasReloadError();
			if (!broken || mReportedScriptErrors.count(id) != 0)
			{
				continue;
			}
			// a hot-reload failure is the fresher, more actionable message when
			// present (the old instance is still alive); otherwise the fatal one
			DebugMessage error(Protocol::MSG_SCRIPT_ERROR);
			error.set(Protocol::FIELD_ID, id);
			error.set(Protocol::FIELD_MESSAGE, script->hasReloadError()
				? script->getLastReloadError() : script->getScriptError());
			mServer.send(error);
			mReportedScriptErrors.insert(id);
		}
	}
	//---------------------------------------------------------
	//! set_property v1: TransformComponent position ("x y z"), orientation
	//! ("w x y z", normalized here), scale ("x y z"); RigidBodyComponent
	//! linear_velocity/angular_velocity ("x y z", needs the created body).
	//! Unknown objects/components/properties and malformed values answer
	//! with an error message - never crash.
	void PlayerDebugLink::handleSetProperty(
		GameObjectManager & gameObjectManager, DebugMessage const & message)
	{
		const String id = message.get(Protocol::FIELD_ID);
		const String component = message.get(Protocol::FIELD_COMPONENT);
		const String property = message.get(Protocol::FIELD_PROPERTY);
		const String value = message.get(Protocol::FIELD_VALUE);
		optr<GameObject> gameObject =
			gameObjectManager.getGameObject(id).lock();
		if (!gameObject)
		{
			sendError("set_property: no GameObject '" + id + "'");
			return;
		}
		if (component == "TransformComponent" &&
			gameObject->hasComponent<TransformComponent>())
		{
			TransformComponent * transform =
				gameObject->getComponentPtr<TransformComponent>();
			float floats[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if (property == "position" && parseFloats(value, floats, 3))
			{
				transform->setPosition(
					Vec3(floats[0], floats[1], floats[2]));
				return;
			}
			if (property == "orientation" && parseFloats(value, floats, 4))
			{
				Quat orientation(floats[0], floats[1], floats[2],
					floats[3]);
				orientation.normalise();
				transform->setOrientation(orientation);
				return;
			}
			if (property == "scale" && parseFloats(value, floats, 3))
			{
				transform->setScale(
					Vec3(floats[0], floats[1], floats[2]));
				return;
			}
		}
		else if (component == "RigidBodyComponent" &&
			gameObject->hasComponent<RigidBodyComponent>())
		{
			RigidBodyComponent * rigidBody =
				gameObject->getComponentPtr<RigidBodyComponent>();
			float floats[3] = { 0.0f, 0.0f, 0.0f };
			if ((property == "linear_velocity" ||
				property == "angular_velocity") &&
				parseFloats(value, floats, 3))
			{
				if (!rigidBody->hasBody())
				{
					sendError("set_property: '" + id +
						"' has no created rigid body yet");
					return;
				}
				const Vec3 velocity(floats[0], floats[1], floats[2]);
				if (property == "linear_velocity")
				{
					rigidBody->setLinearVelocity(velocity);
				}
				else
				{
					rigidBody->setAngularVelocity(velocity);
				}
				return;
			}
		}
		sendError("set_property: unsupported or malformed " + component +
			"." + property + " = '" + value + "' on '" + id + "'");
	}
	//---------------------------------------------------------
	//! @brief reload_script (WP #77 hot-reload): recompile-and-swap the
	//! ScriptComponents of a target GameObject (FIELD_ID) or ALL of them
	//! (FIELD_ID absent, the v1 reload-ALL). Player-directed: the editor's
	//! file watcher only sends this; the swap (compile-before-swap failure
	//! containment) happens HERE, on the running player. Each reloaded id is
	//! cleared from mReportedScriptErrors so a re-break re-reports and a healed
	//! reload lets the error clear; the fresh error state is pushed right away.
	void PlayerDebugLink::handleReloadScript(
		GameObjectManager & gameObjectManager, DebugMessage const & message)
	{
		const String targetId = message.get(Protocol::FIELD_ID); // "" = all
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
			if (!gameObject->hasComponent<ScriptComponent>())
			{
				continue;
			}
			gameObject->getComponentPtr<ScriptComponent>()->hotReload();
			// re-arm reporting: a still-/newly-broken instance must re-report,
			// a successful reload lets the error clear on the next stream tick
			mReportedScriptErrors.erase(id);
			++reloaded;
		}
		// surface any reload that just FAILED immediately (its old instance is
		// still ticking, but the editor must see the broken edit at once)
		sendNewScriptErrors(gameObjectManager);
		EngineLogCapture::logMessage("orkige runtime: hot-reloaded " +
			std::to_string(reloaded) + " script(s)" +
			(targetId.empty() ? String() : " on '" + targetId + "'"));
	}
	//---------------------------------------------------------
	//! @brief set_cvar (WP #83): change a console variable on the RUNNING
	//! player live. CVarManager::setString parses+validates the value per the
	//! cvar's registered type and fires its onChange (the live re-apply seam),
	//! so a `set` in the editor Console tunes the running game at once. An
	//! unknown name or a value the type rejects answers with an error - never
	//! crashes (parallel to handleSetProperty). The registry is a core
	//! singleton, so this handler needs no GameObjectManager.
	void PlayerDebugLink::handleSetCvar(DebugMessage const & message)
	{
		const String name = message.get(Protocol::FIELD_CVAR_NAME);
		const String value = message.get(Protocol::FIELD_VALUE);
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
		while (mServer.receive(message))
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
				mServer.send(DebugMessage(Protocol::MSG_BYE));
			}
			else if (message.type == Protocol::MSG_SELECT)
			{
				const String id = message.get(Protocol::FIELD_ID);
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
				const String id = message.get(Protocol::FIELD_ID);
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
				// WP #77 Lua hot-reload (editor-driven, compile-before-swap)
				handleReloadScript(gameObjectManager, message);
			}
			else if (message.type == Protocol::MSG_SET_CVAR)
			{
				// WP #83 cvars: tune a console variable on the running player
				handleSetCvar(message);
			}
			// --- protocol-extension slot -------------------------------------
			// Additive editor->runtime messages ride THIS one debug protocol as
			// new else-if branches (old players hit the unknown-else below and
			// answer honestly - never crash). Keep the chain flat and each
			// branch a thin dispatch to a handle*() method. #77 (reload) and #83
			// (cvars) are wired above; the remaining slot is:
			//   #80 MCP   -> the editor-side MCP server TRANSLATES its play-
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
		if (!mServer.hasClient() || mSelectedObjectId.empty())
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
		mServer.send(buildObjectState(gameObject));
		mLastStateSend = now;
	}
	//---------------------------------------------------------
}
