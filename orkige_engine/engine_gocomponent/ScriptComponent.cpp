/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	ScriptComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/ScriptComponent.h"
#include "engine_gocomponent/ScriptComponentRegistry.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_gocomponent/SpriteComponent.h"
#include "engine_gocomponent/ParticleComponent.h"
#include "engine_gocomponent/VectorShapeComponent.h"
#include "engine_gocomponent/VectorAnimationComponent.h"
#include "engine_gocomponent/DecalComponent.h"
#include "engine_gocomponent/SoundComponent.h"
#include "engine_gocomponent/CameraComponent.h"
#include "engine_sound/SoundManager.h"
#include "engine_graphic/ScreenFade.h"
#include "engine_graphic/ScreenShake.h"
#include "engine_input/HapticManager.h"
#include "engine_gui/GuiManager.h"
#include "engine_base/EngineLog.h"
#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>
#include <core_game/LevelComponent.h>
#include <core_game/LevelManager.h>
#include <core_game/SaveStore.h>
#include <core_game/TimeControl.h>
#include <core_game/GameState.h>
#include <core_debug/CVarManager.h>
#include <core_debug/BenchmarkRecorder.h>
#include <core_project/AssetDatabase.h>
#include <core_script/ScriptRuntime.h>
#include <core_script/ScriptEventBus.h>
#include <core_util/StringTable.h>
#include <core_tween/TweenManager.h>
#include <core_tween/TimerManager.h>
#include <core_util/MusicFade.h>
#include <core_game/PropertyTween.h>

namespace Orkige
{
	namespace
	{
		//! @brief script errors must be VISIBLE in every build configuration:
		//! the engine log reaches the log file, stderr and - in editor play
		//! mode - the editor Console via the player's log forwarder; headless
		//! runs without an engine log fall back to stderr (EngineLogCapture
		//! hides the logging backend - engine_base/EngineLog.h)
		void logScriptError(String const & message)
		{
			EngineLogCapture::logError(message);
		}
	}
	namespace
	{
		//! fetch a live GameObject by id (NULL/nil when it does not exist) -
		//! shared by the world API functions below
		GameObject* worldGetGameObject(String const & id)
		{
			if (GameObjectManager::getSingletonPtr() == NULL)
			{
				return NULL;
			}
			optr<GameObject> gameObject =
				GameObjectManager::getSingleton().getGameObject(id).lock();
			return gameObject.get();
		}
		//! fetch a component of a live GameObject (NULL/nil when missing);
		//! quiet - scripts probe with this, a miss is not an error
		template<typename ComponentType>
		ComponentType* worldGetComponent(String const & id)
		{
			GameObject* gameObject = worldGetGameObject(id);
			if (!gameObject || !gameObject->hasComponent<ComponentType>())
			{
				return NULL;
			}
			return gameObject->getComponentPtr<ComponentType>();
		}
		//--- weak-woptr resolvers for the Lua world API. Each returns a backend-
		//--- neutral woptr; ScriptRuntime::registerHandle*Accessor wraps it into a
		//--- WEAK handle Lua holds (locks per call, honest error once gone), so
		//--- ScriptComponent never names a scripting-backend detail. The raw
		//--- helpers above stay for the C++-internal callers (tween plumbing). ----
		//! world.get(id) -> the GameObject as a woptr (empty when the id is unknown)
		inline woptr<GameObject> worldGameObjectWeak(String const & id)
		{
			if (GameObjectManager::getSingletonPtr() == NULL)
			{
				return woptr<GameObject>();
			}
			return GameObjectManager::getSingleton().getGameObject(id);
		}
		//! world.get<Component>(id) -> the component as a woptr (empty when the
		//! object or the component is absent - the quiet-probe contract)
		template<typename ComponentType>
		inline woptr<ComponentType> worldComponentWeak(String const & id)
		{
			GameObject* gameObject = worldGetGameObject(id);
			if (!gameObject)
			{
				return woptr<ComponentType>();
			}
			return gameObject->getComponent<ComponentType>();
		}
		//! world.findByTag(tag) -> the tagged GameObjects as woptrs (the manager
		//! tag index drives the set)
		std::vector<woptr<GameObject>> worldFindByTagWeak(String const & tag)
		{
			std::vector<woptr<GameObject>> result;
			if (GameObjectManager::getSingletonPtr() == NULL)
			{
				return result;
			}
			GameObjectManager & manager = GameObjectManager::getSingleton();
			StringVector const ids = manager.findByTag(tag);
			result.reserve(ids.size());
			foreach(String const & id, ids)
			{
				result.push_back(manager.getGameObject(id));
			}
			return result;
		}
	}
	namespace
	{
		//--- tween binding plumbing (the Lua `tween` table registered in
		//--- ensureScriptApi below) -----------------------------------------

		//! resolve an ease name for scripts: "" = linear, an unknown name is
		//! a logged error but the tween still runs (linear) - juice must
		//! never crash a game over a typo
		Ease::Function easeForScripts(String const & name)
		{
			if (name.empty())
			{
				return &Ease::linear;
			}
			Ease::Function ease = Ease::byName(name);
			if (!ease)
			{
				EngineLogCapture::logError(
					"tween: unknown ease '" + name + "' - using linear");
				return &Ease::linear;
			}
			return ease;
		}
		//! wrap a script onUpdate closure as the core apply callback: a
		//! script error cancels the tween (logged once, no per-frame spam);
		//! an explicit `return false` from the script cancels too
		TweenManager::UpdateFunction wrapScriptUpdate(ScriptCallback const & callback)
		{
			if (!callback.valid())
			{
				return TweenManager::UpdateFunction();
			}
			return [callback](float const * values, int count) -> bool
			{
				bool requestedStop = false;
				String error;
				if (!callback.invokeNumbers(values, count, &requestedStop, &error))
				{
					EngineLogCapture::logError(
						"tween onUpdate: SCRIPT ERROR - " + error +
						" (tween cancelled)");
					return false;
				}
				return !requestedStop;
			};
		}
		//! wrap a script onComplete closure (fires exactly once)
		TweenManager::CompleteFunction wrapScriptComplete(ScriptCallback const & callback)
		{
			if (!callback.valid())
			{
				return TweenManager::CompleteFunction();
			}
			return [callback]()
			{
				String error;
				if (!callback.invoke(&error))
				{
					EngineLogCapture::logError(
						"tween onComplete: SCRIPT ERROR - " + error);
				}
			};
		}
		//! wrap a script timer closure (timer.after/every): a script error is
		//! logged with the timer context; it does not stop the timer (a
		//! repeating timer is cancelled explicitly, not by throwing)
		TimerManager::TimerFunction wrapScriptTimer(ScriptCallback const & callback)
		{
			if (!callback.valid())
			{
				return TimerManager::TimerFunction();
			}
			return [callback]()
			{
				String error;
				if (!callback.invoke(&error))
				{
					EngineLogCapture::logError(
						"timer callback: SCRIPT ERROR - " + error);
				}
			};
		}
		//! start a tween against the TweenManager singleton (honest no-op
		//! handle when no runtime ticks tweens - the editor never creates one)
		TweenHandle tweenStart(float const * fromValues, float const * toValues,
			int channelCount, float duration, String const & easeName,
			TweenManager::UpdateFunction const & onUpdate,
			TweenManager::CompleteFunction const & onComplete,
			float delay, String const & targetId)
		{
			TweenHandle handle;
			TweenManager* manager = TweenManager::getSingletonPtr();
			if (!manager)
			{
				EngineLogCapture::logError("tween: no TweenManager - this "
					"runtime does not tick tweens (editor edit mode?)");
				return handle;
			}
			handle.mId = manager->startTween(fromValues, toValues,
				channelCount, duration, easeForScripts(easeName), onUpdate,
				onComplete, delay, targetId);
			return handle;
		}
		//! shared shape of the typed one-channel helpers (fade/volume/rotate):
		//! current -> target over duration, optional trailing [ease, delay]
		TweenHandle tweenChannel1(float fromValue, float toValue,
			float duration, ScriptArgs const & extra,
			TweenManager::UpdateFunction const & applyFunction,
			String const & targetId)
		{
			const String easeName = ScriptRuntime::stringArg(extra, 0, "");
			const float delay = static_cast<float>(
				ScriptRuntime::numberArg(extra, 1, 0.0));
			return tweenStart(&fromValue, &toValue, 1, duration, easeName,
				applyFunction, TweenManager::CompleteFunction(), delay,
				targetId);
		}
		//! @brief start a gui-widget property tween through GuiManager (retarget-
		//! replace + auto-kill live there). The trailing script args are uniform
		//! across the guitween.* helpers: [0] ease name, [1] delay, [2] onComplete.
		//! @return the tween handle (:isActive/:cancel/:setLoops), empty if no gui.
		TweenHandle guiTweenStart(String const & widgetId, int channel,
			float const * toValues, float duration, ScriptArgs const & extra)
		{
			TweenHandle handle;
			if (!GuiManager::getSingletonPtr())
			{
				EngineLogCapture::logError(
					"guitween: no GuiManager - the game booted no UI");
				return handle;
			}
			const String easeName = ScriptRuntime::stringArg(extra, 0, "");
			const float delay = static_cast<float>(
				ScriptRuntime::numberArg(extra, 1, 0.0));
			const ScriptCallback onComplete = ScriptCallback::fromArgs(extra, 2);
			handle.mId = GuiManager::getSingleton().tweenWidget(widgetId, channel,
				toValues, duration, easeForScripts(easeName), delay,
				wrapScriptComplete(onComplete));
			return handle;
		}
		//! shared shape of the typed three-channel helpers (move/scale)
		TweenHandle tweenChannel3(float const * fromValues,
			float const * toValues, float duration, ScriptArgs const & extra,
			TweenManager::UpdateFunction const & applyFunction,
			String const & targetId)
		{
			const String easeName = ScriptRuntime::stringArg(extra, 0, "");
			const float delay = static_cast<float>(
				ScriptRuntime::numberArg(extra, 1, 0.0));
			return tweenStart(fromValues, toValues, 3, duration, easeName,
				applyFunction, TweenManager::CompleteFunction(), delay,
				targetId);
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ScriptComponent::ScriptComponent()
		: mScriptEnabled(true), mStarted(false), mFailed(false)
	{
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	ScriptComponent::~ScriptComponent()
	{
	}
	//---------------------------------------------------------
	void ScriptComponent::setScriptFile(String const & scriptFile)
	{
		if (scriptFile == this->mScriptFile)
		{
			return;
		}
		this->resetScriptState();
		this->mScriptFile = scriptFile;
		// the asset id tracks the script file: the open project's database
		// knows it ("" without a project - honest either way)
		this->mScriptAssetId = AssetDatabase::referenceIdForValue(
			scriptFile, "", AssetDatabase::REF_PROJECT_PATH);
		// (re)discover the new script's exported properties: the dynamic schema
		// is known only once a specific .lua is attached. Values
		// reconcile BY NAME, so switching to a script sharing an export name
		// keeps that value; everything else takes the new file's defaults.
		this->discoverExports();
	}
	//---------------------------------------------------------
	void ScriptComponent::setScriptEnabled(bool enabled)
	{
		this->mScriptEnabled = enabled;
	}
	//---------------------------------------------------------
	void ScriptComponent::reloadScript()
	{
		this->resetScriptState();
	}
	//---------------------------------------------------------
	void ScriptComponent::hotReload()
	{
		// each attempt starts from a clean reload-error slate
		this->mReloadError = "";
		if (this->mScriptFile.empty())
		{
			return;	// nothing attached - nothing to reload
		}
		ScriptRuntime* runtime = ScriptRuntime::getSingletonPtr();
		if (!runtime)
		{
			this->mReloadError = "no ScriptRuntime - scripting was not booted";
			logScriptError(String("ScriptComponent hot-reload of '") +
				this->mScriptFile + "': " + this->mReloadError);
			return;
		}
		ScriptComponent::ensureScriptApi();

		// (1) COMPILE-BEFORE-SWAP: load into a LOCAL temp instance. This is a
		// pure factory - a parse error returns NULL WITHOUT touching the live
		// instance, so a broken edit leaves the running script untouched.
		String error;
		optr<ScriptInstance> candidate =
			runtime->loadScriptInstance(this->mScriptFile, &error);
		if (!candidate)
		{
			// parse error / file-not-found / ORKIGE_SCRIPTING=OFF: keep the OLD
			// instance running, surface the error WITHOUT the fatal mFailed
			this->reportReloadError(error);
			return;
		}

		// re-read the edited file's export set and reconcile BY NAME:
		// a designer-set value survives an edit that keeps the export, a
		// removed export drops, a new one takes its default - before the value
		// is injected into the fresh instance's `self` below
		this->discoverExports();

		// (2) build the temp instance's `self` (from the LIVE siblings) and run
		// init on it - still without disturbing the running instance. An init
		// error DISCARDS the candidate and keeps the old one alive.
		this->populateSelfTable(candidate);
		if (!candidate->callInit(&error))
		{
			this->reportReloadError(String("init: ") + error);
			candidate.reset();	// its dtor GCs the aborted environment
			return;
		}

		// (3) SUCCESS: only now is it safe to retire the old instance. Shut it
		// down (shutdown(self) fires), then swap the new one in and clear the
		// fatal error state - a hot reload heals a previously failed instance.
		if (this->mInstance && this->mStarted)
		{
			String shutdownError;
			if (!this->mInstance->callShutdown(&shutdownError))
			{
				// the outgoing instance is going down either way - log, do not
				// fail the reload over the old script's shutdown hiccup
				logScriptError(String("ScriptComponent['") + this->mScriptFile +
					"']: hot-reload shutdown of the old instance: " +
					shutdownError);
			}
		}
		this->mInstance = candidate;
		this->mStarted = true;
		this->mFailed = false;
		this->mErrorMessage = "";
		GameObject* componentOwner = this->getComponentOwner();
		oDebugMsg("script",0,"ScriptComponent: '" << this->mScriptFile
			<< "' hot-reloaded for '"
			<< (componentOwner ? componentOwner->getObjectID() : String("?"))
			<< "'");
	}
	//---------------------------------------------------------
	void ScriptComponent::dispatchContact(GameObject* other, bool began)
	{
		// only a loaded, healthy, running instance receives events (mirrors the
		// gate in onUpdateComponent); in ORKIGE_SCRIPTING=OFF builds mInstance is
		// never set, so this is a no-op there too
		if (!this->mScriptEnabled || this->mFailed || !this->mStarted ||
			!this->mInstance)
		{
			return;
		}
		const char* const hook = began ? "onContactBegin" : "onContactEnd";
		String error;
		// the OTHER object arrives as a WEAK GameObjectHandle (never a raw
		// pointer): a hook that stashes it for later touches it safely - a stale
		// touch raises an honest script error instead of reading freed state
		woptr<GameObject> otherWeak;
		if (GameObjectManager::getSingletonPtr() != NULL && other)
		{
			otherWeak = GameObjectManager::getSingleton().getGameObject(
				other->getObjectID());
		}
		if (!this->mInstance->callHookWithObject(hook, &error, otherWeak))
		{
			// an error inside the hook disables the instance like any other
			// script error (logged once through failScript)
			this->failScript(String(hook) + ": " + error);
		}
	}
	//---------------------------------------------------------
	void ScriptComponent::dispatchAppLifecycle(bool paused)
	{
		// same gate as dispatchContact: only a loaded, healthy, running instance
		// receives the hook (a no-op in ORKIGE_SCRIPTING=OFF, where mInstance is
		// never set)
		if (!this->mScriptEnabled || this->mFailed || !this->mStarted ||
			!this->mInstance)
		{
			return;
		}
		const char* const hook = paused ? "onAppPause" : "onAppResume";
		String error;
		if (!this->mInstance->callFunction(hook, &error))
		{
			this->failScript(String(hook) + ": " + error);
		}
	}
	//---------------------------------------------------------
	void ScriptComponent::dispatchAppLifecycle(
		GameObjectManager & gameObjectManager, bool paused)
	{
		for (auto const& [id, gameObject] : gameObjectManager.getGameObjects())
		{
			(void)id;
			// EVERY script on the object hears it, not just a lone
			// "ScriptComponent" - an object may carry several behavior scripts
			for (ScriptComponent* script : ScriptComponent::collectFrom(*gameObject))
			{
				script->dispatchAppLifecycle(paused);
			}
		}
		// mirror the lifecycle onto the message bus (additive - the bespoke
		// onAppPause/onAppResume hooks above still fire): a payload-less
		// app.pause / app.resume any script can subscribe to. Queued like any
		// event, so it is delivered at the next script-phase drain.
		ScriptEventBus::getSingleton().emit(paused ? "app.pause" : "app.resume",
			ScriptEventPayload());
	}
	//---------------------------------------------------------
	std::vector<ScriptComponent*> ScriptComponent::collectFrom(
		GameObject & gameObject)
	{
		// walk the container and dynamic_cast: a name-aliased script kind is
		// stored under its script name, so the type-keyed getComponentPtr<>()
		// would miss it - this reaches EVERY script on the object regardless of
		// key (and, incidentally, the low-level "ScriptComponent" kind too)
		std::vector<ScriptComponent*> scripts;
		for (auto const & entry : gameObject.getComponents())
		{
			if (ScriptComponent* script =
				dynamic_cast<ScriptComponent*>(entry.second.get()))
			{
				scripts.push_back(script);
			}
		}
		return scripts;
	}
	//---------------------------------------------------------
	String ScriptComponent::getComponentName() const
	{
		return this->getComponentKey().getName();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void ScriptComponent::onAdd()
	{
		// bind this instance to its script KIND: at onAdd the component is
		// already in the owner's map, so getComponentKey() resolves the container
		// key. The low-level kind carries its file in the serialized `script`
		// property - there is nothing to resolve from the key.
		const String key = this->getComponentKey().getName();
		if (key == ScriptComponent::getClassTypeInfo().getName())
		{
			return;
		}
		// a NAME-ALIASED script kind: the key IS the script name - resolve its
		// file through the registry and bind it, so an Add Component / a scene
		// load / an MCP add_component gets a runnable script with no explicit
		// file. Missing registry / kind (a raw unit test, a stale scene) leaves
		// the file empty; a serialized `script` record then still binds it.
		if (ScriptComponentRegistry::getSingletonPtr())
		{
			const String file = ScriptComponentRegistry::getSingleton()
				.scriptFileForComponent(key);
			if (!file.empty())
			{
				this->setScriptFile(file);
			}
		}
	}
	//---------------------------------------------------------
	void ScriptComponent::onRemove()
	{
		this->resetScriptState();
	}
	//---------------------------------------------------------
	void ScriptComponent::onUpdateComponent(float deltaTime)
	{
		if (!this->mScriptEnabled || this->mFailed || this->mScriptFile.empty())
		{
			return;
		}
		if (!this->mStarted)
		{
			if (!this->loadScriptNow())
			{
				return;	// failScript already disabled the instance
			}
		}
		String error;
		if (!this->mInstance->callUpdate(deltaTime, &error))
		{
			this->failScript(String("update: ") + error);
		}
	}
	//---------------------------------------------------------
	void ScriptComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: only the authoring
		// state round-trips - the script path (an AssetRef, its stable asset id in
		// the record for rename survival) and the enabled flag; runtime state
		// (loaded/failed, the Lua environment) is rebuilt on the next play run.
		// The per-instance script EXPORT property VALUES ride the AttributeHolder
		// bag (OParent::save) alongside the reflected export values.
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void ScriptComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		this->resetScriptState();
		// the script AssetRef is resolved against the active database (a resolving
		// id wins over a stale path - rename survival) then set through
		// setScriptFile; the enabled flag follows
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	bool ScriptComponent::loadScriptNow()
	{
		ScriptRuntime* runtime = ScriptRuntime::getSingletonPtr();
		if (!runtime)
		{
			this->failScript("no ScriptRuntime - scripting was not booted");
			return false;
		}
		ScriptComponent::ensureScriptApi();

		String error;
		this->mInstance = runtime->loadScriptInstance(this->mScriptFile, &error);
		if (!this->mInstance)
		{
			// covers file-not-found, script errors and the honest
			// "scripting disabled" of ORKIGE_SCRIPTING=OFF builds
			this->failScript(error);
			return false;
		}

		// the `self` table: owner + convenience accessors for the sibling
		// components attached NOW (components added later: use the world API)
		this->populateSelfTable(this->mInstance);

		this->mStarted = true;
		if (!this->mInstance->callInit(&error))
		{
			this->failScript(String("init: ") + error);
			return false;
		}
		oDebugMsg("script",0,"ScriptComponent: '" << this->mScriptFile
			<< "' loaded for '" << this->getComponentOwner()->getObjectID()
			<< "'");
		return true;
	}
	//---------------------------------------------------------
	void ScriptComponent::populateSelfTable(
		optr<ScriptInstance> const & instance)
	{
		// re-fetch the siblings from the LIVE GameObject every time: on a hot
		// reload the new instance's `self` must point at the CURRENT components
		// (the transform/body/sprite the game has been mutating), never at a
		// stale snapshot - engine-side state survives the swap this way
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		// self.gameObject / self.script / self.<component> are WEAK handles (the
		// same currency as the world API): each locks per method call and raises
		// an honest, pcall-catchable error naming the owner once the object is
		// gone, so a cached `self` field can never touch freed engine state. id
		// stays a plain string. makeHandle over an absent component yields nil, so
		// the has-component guards below are what set a field at all.
		instance->setSelfValue("id", componentOwner->getObjectID());
		instance->setSelfHandle("gameObject",
			GameObjectManager::getSingleton().getGameObject(
				componentOwner->getObjectID()));
		// self.script is a handle to THIS script component (keyed by its own kind,
		// since script kinds live under their name key, not "ScriptComponent")
		instance->setSelfHandle("script",
			componentOwner->getComponent<ScriptComponent>(
				this->getComponentKey()));
		if (componentOwner->hasComponent<TransformComponent>())
		{
			instance->setSelfHandle("transform",
				componentOwner->getComponent<TransformComponent>());
		}
		if (componentOwner->hasComponent<RigidBodyComponent>())
		{
			instance->setSelfHandle("rigidbody",
				componentOwner->getComponent<RigidBodyComponent>());
		}
		if (componentOwner->hasComponent<ModelComponent>())
		{
			instance->setSelfHandle("model",
				componentOwner->getComponent<ModelComponent>());
		}
		if (componentOwner->hasComponent<SpriteComponent>())
		{
			instance->setSelfHandle("sprite",
				componentOwner->getComponent<SpriteComponent>());
		}
		if (componentOwner->hasComponent<ParticleComponent>())
		{
			instance->setSelfHandle("particles",
				componentOwner->getComponent<ParticleComponent>());
		}
		if (componentOwner->hasComponent<VectorShapeComponent>())
		{
			// self.shape:impulse(...) / :playMorph(...) drive the soft-body deform
			instance->setSelfHandle("shape",
				componentOwner->getComponent<VectorShapeComponent>());
		}
		if (componentOwner->hasComponent<VectorAnimationComponent>())
		{
			// self.anim:play(...) / :setClip(...) / :crossFade(...) drive the
			// vector-animation rig's clip playback and blending
			instance->setSelfHandle("anim",
				componentOwner->getComponent<VectorAnimationComponent>());
		}
		if (componentOwner->hasComponent<DecalComponent>())
		{
			// self.decal:place(pos, normal) / :fade(sec) stamp + fade the
			// projected surface mark
			instance->setSelfHandle("decal",
				componentOwner->getComponent<DecalComponent>());
		}
		// inject the EXPORTED property values as their natural
		// Lua types BEFORE init runs, so the script reads them as tunables
		// (self.moveSpeed, self.tint, ...) - script-declared properties
		// auto-exposed in the inspector. A hot reload re-injects the CURRENT values (reconciled
		// above), so designer edits survive the swap.
		for (std::map<String, PropertyValue>::const_iterator it =
			this->mExportValues.begin(); it != this->mExportValues.end(); ++it)
		{
			instance->setSelfProperty(it->first.c_str(), it->second);
		}
	}
	//---------------------------------------------------------
	void ScriptComponent::discoverExports()
	{
		PropertySchema newSchema;
		std::map<String, PropertyValue> newValues;
		ScriptRuntime* runtime = ScriptRuntime::getSingletonPtr();
		if (runtime && !this->mScriptFile.empty())
		{
			const std::vector<ScriptExportProperty> declared =
				runtime->readExportedProperties(this->mScriptFile);
			for (ScriptExportProperty const & ex : declared)
			{
				// reconcile BY NAME: keep the current value when the name AND a
				// compatible kind survive; otherwise take the declared default
				std::map<String, PropertyValue>::const_iterator existing =
					this->mExportValues.find(ex.name);
				PropertyValue value = (existing != this->mExportValues.end() &&
					existing->second.kind() == ex.kind)
					? existing->second : ex.defaultValue;
				newValues[ex.name] = value;

				// the descriptor closes over the property NAME and reads/writes
				// this component's per-instance store - reusing PropertyDesc/
				// PropertyValue so a consumer can't tell dynamic from static
				const String name = ex.name;
				PropertyGetter getter =
					[name](Object const * obj) -> PropertyValue
				{
					return static_cast<ScriptComponent const *>(obj)
						->getExportValue(name);
				};
				PropertySetter setter =
					[name](Object * obj, PropertyValue const & propertyValue)
				{
					static_cast<ScriptComponent *>(obj)->setExportValue(name,
						propertyValue);
				};
				PropertyDesc desc;
				if (ex.kind == PropertyKind::AssetRef ||
					ex.kind == PropertyKind::ObjectRef)
				{
					desc = PropertyDesc::makeReference(ex.name, ex.kind,
						ex.referenceHint, PROP_NONE, getter, setter);
				}
				else
				{
					desc = PropertyDesc(ex.name, ex.kind, PROP_NONE, getter,
						setter);
				}
				if (ex.hasRange)
				{
					desc.meta.hasRange = true;
					desc.meta.minValue = ex.minValue;
					desc.meta.maxValue = ex.maxValue;
				}
				newSchema.add(desc);
			}
		}
		this->mExportSchema = newSchema;
		this->mExportValues.swap(newValues);
	}
	//---------------------------------------------------------
	PropertySchema ScriptComponent::getInstancePropertySchema() const
	{
		return this->mExportSchema;
	}
	//---------------------------------------------------------
	PropertyValue ScriptComponent::getExportValue(String const & name) const
	{
		std::map<String, PropertyValue>::const_iterator it =
			this->mExportValues.find(name);
		return it != this->mExportValues.end() ? it->second : PropertyValue();
	}
	//---------------------------------------------------------
	void ScriptComponent::setExportValue(String const & name,
		PropertyValue const & value)
	{
		this->mExportValues[name] = value;
		// live effect: a set that arrives while the script is running (an
		// inspector edit / an MCP set over the debug protocol) re-injects onto
		// `self`, so self.<name> updates without a reload
		if (this->mInstance)
		{
			this->mInstance->setSelfProperty(name.c_str(), value);
		}
	}
	//---------------------------------------------------------
	void ScriptComponent::ensureScriptApi()
	{
		ScriptRuntime & runtime = ScriptRuntime::getSingleton();
		// `shared`: the documented cross-instance state escape hatch (per-
		// instance environments make sharing opt-in instead of accidental)
		runtime.ensureGlobalTable("shared");
		if (runtime.hasGlobalTable("world"))
		{
			return;
		}

		// ============= THE `world` TABLE (accessor convention) =============
		// Reach OTHER GameObjects by id. Raw component pointers - valid while
		// the object lives; scripts RE-FETCH when in doubt (the recommended
		// closure style, see the tween block below).
		// APPEND-ONLY convention: one world.get<Component>(id) accessor per
		// component type, plus world.exists/get - later packages append THEIR
		// accessor here (getParticles, findByTag, loadLevel)
		// instead of inventing a second lookup vocabulary.
		runtime.registerFunction("world", "exists", [](String const & id)
		{
			return worldGetGameObject(id) != NULL;
		});
		// every world.get* below hands Lua a WEAK handle (never a raw pointer):
		// it locks per method call and raises an honest, pcall-catchable error
		// naming the kind + id once the object is gone. registerHandleAccessor
		// wraps the woptr resolver so this file names no scripting-backend detail.
		runtime.registerHandleAccessor("world", "get", &worldGameObjectWeak);
		runtime.registerHandleAccessor("world", "getTransform",
			&worldComponentWeak<TransformComponent>);
		runtime.registerHandleAccessor("world", "getRigidBody",
			&worldComponentWeak<RigidBodyComponent>);
		runtime.registerHandleAccessor("world", "getModel",
			&worldComponentWeak<ModelComponent>);
		runtime.registerHandleAccessor("world", "getSprite",
			&worldComponentWeak<SpriteComponent>);
		runtime.registerHandleAccessor("world", "getParticles",
			&worldComponentWeak<ParticleComponent>);
		// world.getScript(id): a script component on the object. Script kinds are
		// keyed by script name, not "ScriptComponent", so reach them by scan and
		// return the FIRST as a weak handle (an object with several scripts is
		// better reached by world.get(id) + the shared table; this stays the
		// convenience accessor)
		runtime.registerHandleAccessor("world", "getScript",
			[](String const & id) -> woptr<ScriptComponent>
		{
			GameObject* gameObject = worldGetGameObject(id);
			if (!gameObject)
			{
				return woptr<ScriptComponent>();
			}
			std::vector<ScriptComponent*> scripts =
				ScriptComponent::collectFrom(*gameObject);
			if (scripts.empty())
			{
				return woptr<ScriptComponent>();
			}
			// the woptr keyed by the found script's kind (script components
			// live under their name key, not "ScriptComponent")
			return gameObject->getComponent<ScriptComponent>(
				scripts.front()->getComponentKey());
		});
		runtime.registerHandleAccessor("world", "getSound",
			&worldComponentWeak<SoundComponent>);
		// world.getCamera(id) -> the object's CameraComponent (nil when absent);
		// the accessor a script uses to drive smooth follow, e.g.
		//   world.getCamera("Camera"):follow("Hero", 0.2)
		// follow COMPOSES with the ortho fit policy (fit sizes the projection,
		// follow moves the camera position). Reflected props (followTarget /
		// followDamping / followOffset) reach get/set_component over MCP too.
		runtime.registerHandleAccessor("world", "getCamera",
			&worldComponentWeak<CameraComponent>);
		// world.findByTag(tag) -> array of the GameObjects carrying that tag
		// (empty table when none); tags are set in the editor Inspector or via
		// GameObject:addTag, indexed by the GameObjectManager
		runtime.registerHandleAccessor("world", "getLevel",
			&worldComponentWeak<LevelComponent>);
		// world.loadScene(path): request a DEFERRED, re-entrant scene
		// switch - the pending request is applied by the runtime at the frame
		// boundary after physics (never mid-update), tearing the old world down
		// through the GameObjectManager::clear hook. Honest no-op when no
		// LevelManager exists (the editor never switches scenes); the richer
		// index-based level/progression API lives on the LevelManager singleton.
		runtime.registerFunction("world", "loadScene",
			[](String const & path)
		{
			if(LevelManager::getSingletonPtr())
			{
				LevelManager::getSingleton().loadScenePath(path);
			}
			else
			{
				EngineLogCapture::logError("world.loadScene: no LevelManager "
					"- this runtime does not switch scenes (editor edit mode?)");
			}
		});
		runtime.registerHandleListAccessor("world", "findByTag",
			&worldFindByTagWeak);
		// world.setTimeScale(s) / getTimeScale(): the gameplay time scale the
		// player loop applies to the delta it feeds scripts, tweens and physics
		// (1 = normal, 0.5 = slow motion, 0 = hitstop - the world freezes but
		// keeps rendering; the debug protocol and editor stay real-time). Honest
		// no-op / 1.0 without a TimeControl (the editor never ticks gameplay).
		runtime.registerFunction("world", "setTimeScale", [](double scale)
		{
			if(TimeControl::getSingletonPtr())
			{
				TimeControl::getSingleton().setTimeScale(
					static_cast<float>(scale));
			}
		});
		runtime.registerFunction("world", "getTimeScale", []() -> double
		{
			return TimeControl::getSingletonPtr()
				? TimeControl::getSingleton().getTimeScale() : 1.0;
		});

		// ================= THE `screen` TABLE (fade wipes) =================
		// Full-screen fade transitions over the finished frame + HUD (a
		// reserved high draw layer). screen.fadeOut(secs)/fadeIn(secs) ramp
		// the overlay; screen.loadScene(path, outSecs, inSecs) is the wipe
		// games want - fade out, switch the scene while the screen is opaque
		// (the deferred load applies at the frame boundary), then fade in.
		// setFadeColor(r,g,b) picks the colour (default black); isFading()
		// gates input. Only-a-runtime-ticks: no ScreenFade (editor) -> honest
		// no-op; screen.loadScene without a LevelManager falls through to a
		// plain deferred load (no wipe), like world.loadScene.
		runtime.registerFunction("screen", "fadeOut", [](float seconds)
		{
			if(ScreenFade::getSingletonPtr())
			{
				ScreenFade::getSingleton().fadeOut(seconds);
			}
		});
		runtime.registerFunction("screen", "fadeIn", [](float seconds)
		{
			if(ScreenFade::getSingletonPtr())
			{
				ScreenFade::getSingleton().fadeIn(seconds);
			}
		});
		runtime.registerFunction("screen", "setFadeColor",
			[](float r, float g, float b)
		{
			if(ScreenFade::getSingletonPtr())
			{
				ScreenFade::getSingleton().setFadeColor(r, g, b);
			}
		});
		runtime.registerFunction("screen", "isFading", []() -> bool
		{
			return ScreenFade::getSingletonPtr() &&
				ScreenFade::getSingleton().isFading();
		});
		runtime.registerFunction("screen", "loadScene",
			[](String const & path, float outSeconds, float inSeconds)
		{
			// coordinate the fade with the DEFERRED scene switch: request the
			// load at full opacity so the teardown/reload is hidden. The
			// coordination lives HERE (not in ScreenFade, which stays free of
			// LevelManager) via the at-opaque callback.
			if(ScreenFade::getSingletonPtr() && LevelManager::getSingletonPtr())
			{
				ScreenFade::getSingleton().transition(outSeconds, inSeconds,
					[path]()
				{
					LevelManager::getSingleton().loadScenePath(path);
				});
			}
			else if(LevelManager::getSingletonPtr())
			{
				// no fade overlay - fall through to a plain deferred load
				LevelManager::getSingleton().loadScenePath(path);
			}
			else
			{
				EngineLogCapture::logError("screen.loadScene: no LevelManager "
					"- this runtime does not switch scenes (editor edit mode?)");
			}
		});
		// screen.shake(amplitude, duration [, frequency]): a decaying camera-space
		// wobble applied to the active camera each frame and restored exactly when
		// it ends (works in 2D and 3D). `amplitude` is world units of peak offset,
		// `duration` seconds, optional `frequency` in Hz (defaults sensibly). A
		// fresh call while shaking stacks (stronger amplitude, longer remaining).
		// screen.stopShake() ends it immediately; screen.isShaking() gates polish.
		// Honest no-op without a ScreenShake (editor edit mode).
		runtime.registerFunction("screen", "shake",
			[](float amplitude, float duration, ScriptArgs args)
		{
			if(ScreenShake::getSingletonPtr())
			{
				const float frequency = static_cast<float>(
					ScriptRuntime::numberArg(args, 0, 0.0));
				ScreenShake::getSingleton().shake(amplitude, duration, frequency);
			}
		});
		runtime.registerFunction("screen", "stopShake", []()
		{
			if(ScreenShake::getSingletonPtr())
			{
				ScreenShake::getSingleton().stop();
			}
		});
		runtime.registerFunction("screen", "isShaking", []() -> bool
		{
			return ScreenShake::getSingletonPtr() &&
				ScreenShake::getSingleton().isShaking();
		});

		// ================= THE `haptics` TABLE (rumble) ===================
		// Phone-body vibration for mobile games. Named patterns are preferred
		// (iOS taptics ARE named generators; Android maps them to duration/
		// amplitude): haptics.pattern("light".."selection"). haptics.play(strength,
		// ms) is the generic escape hatch. haptics.isAvailable() is true only on a
		// device with a vibrator; haptics.setEnabled(false) honours an in-game
		// "vibration off" setting. Honest no-op without a HapticManager (editor)
		// or on desktop (no vibrator).
		runtime.registerFunction("haptics", "play",
			[](float strength, int durationMs)
		{
			if(HapticManager::getSingletonPtr())
			{
				HapticManager::getSingleton().play(strength, durationMs);
			}
		});
		runtime.registerFunction("haptics", "pattern",
			[](String const & name)
		{
			if(HapticManager::getSingletonPtr())
			{
				HapticManager::getSingleton().playPatternByName(name);
			}
		});
		runtime.registerFunction("haptics", "isAvailable", []() -> bool
		{
			return HapticManager::getSingletonPtr() &&
				HapticManager::getSingleton().isAvailable();
		});
		runtime.registerFunction("haptics", "setEnabled", [](bool enabled)
		{
			if(HapticManager::getSingletonPtr())
			{
				HapticManager::getSingleton().setEnabled(enabled);
			}
		});

		// ================= THE `loc` GLOBAL (localisation) =================
		// loc("key") -> the active-language string for the key (the key itself
		// when untranslated, so UI stays readable). loc("key", a, b, ...) -> the
		// same with %%0%%, %%1%% ... substituted by the trailing string args.
		// Backed by core_util/StringTable (the player loads the project's
		// localisation file per the config-asset convention); honest without a
		// table - returns the key.
		runtime.registerGlobalFunction("loc",
			[](String const & key, ScriptArgs args) -> String
		{
			if (!StringTable::getSingletonPtr())
			{
				return key;
			}
			// sentinel default distinguishes an absent positional arg from a
			// legitimately empty one, so the loop stops at the real end
			static const String kAbsent = String("\x01") + "__orkige_loc_absent__";
			StringVector formatArgs;
			for (int index = 0; index < 16; ++index)
			{
				const String value =
					ScriptRuntime::stringArg(args, index, kAbsent);
				if (value == kAbsent)
				{
					break;
				}
				formatArgs.push_back(value);
			}
			if (formatArgs.empty())
			{
				return StringTable::getSingleton().get(key);
			}
			return StringTable::getSingleton().format(key, formatArgs);
		});

		// ================= THE `locale` TABLE (language switch) ============
		// A settings-menu language control on top of the same StringTable the
		// loc() accessor reads. Switching the active language does NOT retro-edit
		// widgets already created (their text resolved at creation) - the game
		// re-pushes its screen(s) after a switch, which the screen stack makes a
		// one-liner. Honest without a table (editor / an unlocalised game): get
		// and getSource return "", list is empty, set returns false.
		//   locale.set("de")  -> true if "de" is a loaded language (and switches
		//                        to it), false otherwise (no change)
		//   locale.get()      -> the active language code
		//   locale.list()     -> the loaded language codes, sorted (source incl.)
		//   locale.getSource()-> the source language code
		runtime.registerFunction("locale", "set",
			[](String const & language) -> bool
		{
			if (!StringTable::getSingletonPtr() ||
				!StringTable::getSingleton().hasLanguage(language))
			{
				return false;
			}
			StringTable::getSingleton().setLanguage(language);
			return true;
		});
		runtime.registerFunction("locale", "get", []() -> String
		{
			if (!StringTable::getSingletonPtr())
			{
				return String();
			}
			return StringTable::getSingleton().getLanguage();
		});
		runtime.registerFunction("locale", "list", []() -> StringVector
		{
			if (!StringTable::getSingletonPtr())
			{
				return StringVector();
			}
			return StringTable::getSingleton().getLanguages();
		});
		runtime.registerFunction("locale", "getSource", []() -> String
		{
			if (!StringTable::getSingletonPtr())
			{
				return String();
			}
			return StringTable::getSingleton().getSourceLanguage();
		});

		// ================= THE `sound` TABLE (the mixer) ===================
		// Global mixer controls (per-sound volume/group live on the
		// SoundComponent reached via world.getSound). Effective per-source
		// gain = sound volume * group volume, master on top - all 0..1
		// (values above 1 clamp silently at OpenAL's pinned AL_MAX_GAIN).
		// Group volumes persist per project as the manifest Settings
		// "audio.master" / "audio.group.<name>", applied on project load.
		// Honest without audio: no SoundManager (or a failed OpenAL init)
		// reads back the defaults and setters no-op.
		runtime.registerFunction("sound", "setGroupVolume",
			[](String const & group, float volume)
		{
			if (SoundManager::getSingletonPtr())
			{
				SoundManager::getSingleton().setGroupVolume(group, volume);
			}
		});
		runtime.registerFunction("sound", "getGroupVolume",
			[](String const & group) -> float
		{
			return SoundManager::getSingletonPtr()
				? SoundManager::getSingleton().getGroupVolume(group) : 1.0f;
		});
		runtime.registerFunction("sound", "setMasterVolume", [](float volume)
		{
			if (SoundManager::getSingletonPtr())
			{
				SoundManager::getSingleton().setMasterVolume(volume);
			}
		});
		runtime.registerFunction("sound", "getMasterVolume", []() -> float
		{
			return SoundManager::getSingletonPtr()
				? SoundManager::getSingleton().getMasterVolume() : 1.0f;
		});

		// ================= THE `music` TABLE (streamed tracks) =============
		// Streamed background music, keyed by id, living on the SoundManager so
		// a track survives scene switches (unlike a component, which the scene
		// teardown destroys). Files resolve through the resource system like any
		// sound, so `file` is the project-relative asset name (e.g.
		// "music/level1.ogg"). Tracks sit in the "music" mixer group; group and
		// master volume stay on the `sound` table (sound.setGroupVolume("music",
		// v) / tween.volume("music", ...) reach streams transparently) - this
		// table carries only the per-track own volume. Honest without audio: no
		// SoundManager (or a failed OpenAL init) makes every call a no-op and the
		// readbacks return their defaults.
		runtime.registerFunction("music", "play",
			[](String const & id, String const & file, ScriptArgs args) -> bool
		{
			if (!SoundManager::getSingletonPtr())
			{
				return false;
			}
			// loop defaults to true (background music); an explicit 0/false stops
			// at the end of the track
			const bool loop = ScriptRuntime::numberArg(args, 0, 1.0) != 0.0;
			return SoundManager::getSingleton().playMusic(id, file, loop);
		});
		runtime.registerFunction("music", "stop", [](String const & id) -> bool
		{
			return SoundManager::getSingletonPtr()
				? SoundManager::getSingleton().stopMusic(id) : false;
		});
		runtime.registerFunction("music", "stopAll", []()
		{
			if (SoundManager::getSingletonPtr())
			{
				SoundManager::getSingleton().stopAllMusic();
			}
		});
		runtime.registerFunction("music", "isPlaying",
			[](String const & id) -> bool
		{
			return SoundManager::getSingletonPtr()
				? SoundManager::getSingleton().isMusicPlaying(id) : false;
		});
		runtime.registerFunction("music", "setVolume",
			[](String const & id, float volume)
		{
			if (SoundManager::getSingletonPtr())
			{
				SoundManager::getSingleton().setMusicVolume(id, volume);
			}
		});
		runtime.registerFunction("music", "getPosition",
			[](String const & id) -> float
		{
			if (!SoundManager::getSingletonPtr())
			{
				return 0.0f;
			}
			MusicStreamPtr track = SoundManager::getSingleton().getMusic(id);
			return track ? track->getPlayPosition() : 0.0f;
		});
		// music.crossFade(id, file, seconds): start (id, file) at silence and
		// smoothly swap it in while every OTHER registered track fades out,
		// over `seconds` (the incoming track BECOMES the music, the rest
		// retire). Rides ONE core_tween tween (0->1, ticked by the
		// player loop) whose per-step callback drives both track OWN volumes via
		// the pure equal-power MusicFade curve (constant perceived loudness); the
		// outgoing tracks are stopped when the fade completes. seconds <= 0 (or
		// no TweenManager) swaps instantly. Honest no-op without a SoundManager.
		runtime.registerFunction("music", "crossFade",
			[](String const & id, String const & file, double seconds) -> bool
		{
			if (!SoundManager::getSingletonPtr())
			{
				return false;
			}
			SoundManager & sound = SoundManager::getSingleton();
			// the tracks to fade OUT: every OTHER registered track (crossFade
			// makes the incoming track THE music and retires the rest);
			// re-using the incoming id just re-swaps it in
			std::vector<String> outgoing;
			foreach (SoundManager::MusicTrackInfo const & track,
				sound.snapshotMusic())
			{
				if (track.id != id)
				{
					outgoing.push_back(track.id);
				}
			}
			// bring the incoming track up from silence (loops like music.play)
			sound.playMusic(id, file, true);
			sound.setMusicVolume(id, 0.0f);
			const float duration = static_cast<float>(seconds);
			if (duration <= 0.0f || !TweenManager::getSingletonPtr())
			{
				// instant swap: incoming full, outgoing stopped
				sound.setMusicVolume(id, 1.0f);
				foreach (String const & out, outgoing)
				{
					sound.stopMusic(out);
				}
				return true;
			}
			const float from = 0.0f;
			const float to = 1.0f;
			const String incoming = id;
			const std::vector<String> fadeOut = outgoing;
			TweenManager::getSingleton().startTween(&from, &to, 1, duration,
				&Ease::linear,
				[incoming, fadeOut](float const * values, int) -> bool
				{
					SoundManager* soundPtr = SoundManager::getSingletonPtr();
					if (!soundPtr)
					{
						return false;	// audio gone - stop the tween
					}
					float outGain = 0.0f;
					float inGain = 1.0f;
					MusicFade::crossfadeGains(values[0], outGain, inGain);
					soundPtr->setMusicVolume(incoming, inGain);
					foreach (String const & out, fadeOut)
					{
						soundPtr->setMusicVolume(out, outGain);
					}
					return true;
				},
				[fadeOut]()
				{
					SoundManager* soundPtr = SoundManager::getSingletonPtr();
					if (!soundPtr)
					{
						return;
					}
					foreach (String const & out, fadeOut)
					{
						soundPtr->stopMusic(out);
					}
				},
				0.0f, StringUtil::BLANK);
			return true;
		});

		// ================= THE `tween` TABLE (juice) =======================
		// Callback-based tweening on core_tween/TweenManager, ticked by the
		// player loop (scripts -> tweens -> physics). Ease names come from
		// core_tween/EaseLibrary.h ("linear", "quadOut", "backInOut", ...).
		// Every function returns a handle: handle:cancel(), handle:isActive().
		//
		//   tween.to(from, to, duration, ease [, onUpdate [, onComplete [, delay]]])
		//     the generic form: onUpdate(value) applies the value through a
		//     typed setter. RECOMMENDED CLOSURE STYLE - re-fetch by id, never
		//     capture components (captured pointers dangle when objects die):
		//       tween.to(0, 3, 0.5, "quadOut", function(x)
		//           local t = world.getTransform("Door")
		//           if t then t:setPosition(Vector3(x, 0, 0)) end
		//       end)
		//     `return false` from onUpdate cancels the tween.
		//
		//   typed helpers, keyed to their target and REAPED when it dies
		//   (optional trailing arguments: ease name, delay seconds):
		//     tween.move(id, x, y, z, duration [, ease [, delay]])   - local position
		//     tween.scale(id, x, y, z, duration [, ease [, delay]])  - local scale
		//     tween.rotate(id, degrees, duration [, ease [, delay]]) - Z rotation (2D convention)
		//     tween.fade(id, alpha, duration [, ease [, delay]])     - sprite tint alpha
		//     tween.volume(group, volume, duration [, ease [, delay]]) - mixer group volume
		//
		//   declarative property-path tween - animates ANY numeric
		//   reflected property BY NAME through the registry (no bespoke helper):
		//     tween.property(id, componentType, propertyName, toValue, duration
		//                    [, ease [, onComplete [, delay]]])
		//   toValue is the target's canonical string ("3.5" for a Float, "1 2 0"
		//   for a Vec3, "1 0 0 1" for a Color); only Float/Int/Vec3/Color tween,
		//   any other kind is a logged error. Reaped by id like the typed helpers.
		//     tween.property("Hero", "CameraComponent", "orthoSize", "5", 1.0)
		//     tween.property("Coin", "SpriteComponent", "tint", "1 1 0 1", 0.4, "quadOut")
		//
		//   ducking is deliberately NOT a feature - it is a two-line recipe:
		//     tween.volume("music", 0.3, 0.2)                -- duck under a stinger
		//     tween.volume("music", 1.0, 0.8, "quadOut", 1.5) -- and restore after it
		runtime.registerFunction("tween", "to",
			[](double from, double to, double duration, String const & ease,
				ScriptArgs extra) -> TweenHandle
		{
			const float fromValue = static_cast<float>(from);
			const float toValue = static_cast<float>(to);
			const ScriptCallback onUpdate = ScriptCallback::fromArgs(extra, 0);
			const ScriptCallback onComplete = ScriptCallback::fromArgs(extra, 1);
			const float delay = static_cast<float>(
				ScriptRuntime::numberArg(extra, 2, 0.0));
			return tweenStart(&fromValue, &toValue, 1,
				static_cast<float>(duration), ease,
				wrapScriptUpdate(onUpdate), wrapScriptComplete(onComplete),
				delay, StringUtil::BLANK);
		});
		runtime.registerFunction("tween", "move",
			[](String const & id, double x, double y, double z,
				double duration, ScriptArgs extra) -> TweenHandle
		{
			TransformComponent* transform =
				worldGetComponent<TransformComponent>(id);
			if (!transform)
			{
				EngineLogCapture::logError(
					"tween.move: no TransformComponent on '" + id + "'");
				return TweenHandle();
			}
			const Vec3 from = transform->getPosition();
			const float fromValues[3] = { from.x, from.y, from.z };
			const float toValues[3] = { static_cast<float>(x),
				static_cast<float>(y), static_cast<float>(z) };
			return tweenChannel3(fromValues, toValues,
				static_cast<float>(duration), extra,
				[id](float const * values, int) -> bool
				{
					// re-fetched every step; the id-reap already retires the
					// tween when the object is gone
					TransformComponent* target =
						worldGetComponent<TransformComponent>(id);
					if (target)
					{
						target->setPosition(
							Vec3(values[0], values[1], values[2]));
					}
					return true;
				}, id);
		});
		runtime.registerFunction("tween", "scale",
			[](String const & id, double x, double y, double z,
				double duration, ScriptArgs extra) -> TweenHandle
		{
			TransformComponent* transform =
				worldGetComponent<TransformComponent>(id);
			if (!transform)
			{
				EngineLogCapture::logError(
					"tween.scale: no TransformComponent on '" + id + "'");
				return TweenHandle();
			}
			const Vec3 from = transform->getScale();
			const float fromValues[3] = { from.x, from.y, from.z };
			const float toValues[3] = { static_cast<float>(x),
				static_cast<float>(y), static_cast<float>(z) };
			return tweenChannel3(fromValues, toValues,
				static_cast<float>(duration), extra,
				[id](float const * values, int) -> bool
				{
					TransformComponent* target =
						worldGetComponent<TransformComponent>(id);
					if (target)
					{
						target->setScale(
							Vec3(values[0], values[1], values[2]));
					}
					return true;
				}, id);
		});
		runtime.registerFunction("tween", "rotate",
			[](String const & id, double degrees, double duration,
				ScriptArgs extra) -> TweenHandle
		{
			TransformComponent* transform =
				worldGetComponent<TransformComponent>(id);
			if (!transform)
			{
				EngineLogCapture::logError(
					"tween.rotate: no TransformComponent on '" + id + "'");
				return TweenHandle();
			}
			// Z rotation - the 2D tier's sprite spin (sprites live in the
			// XY plane); the current angle is the orientation's roll
			const float fromDegrees = static_cast<float>(
				transform->getOrientation().getRoll().valueDegrees());
			return tweenChannel1(fromDegrees, static_cast<float>(degrees),
				static_cast<float>(duration), extra,
				[id](float const * values, int) -> bool
				{
					TransformComponent* target =
						worldGetComponent<TransformComponent>(id);
					if (target)
					{
						target->setOrientation(
							Quat(Degree(values[0]), Vec3::UNIT_Z));
					}
					return true;
				}, id);
		});
		runtime.registerFunction("tween", "fade",
			[](String const & id, double alpha, double duration,
				ScriptArgs extra) -> TweenHandle
		{
			SpriteComponent* sprite = worldGetComponent<SpriteComponent>(id);
			if (!sprite)
			{
				EngineLogCapture::logError(
					"tween.fade: no SpriteComponent on '" + id + "'");
				return TweenHandle();
			}
			const float fromAlpha = sprite->getTint().a;
			return tweenChannel1(fromAlpha, static_cast<float>(alpha),
				static_cast<float>(duration), extra,
				[id](float const * values, int) -> bool
				{
					SpriteComponent* target =
						worldGetComponent<SpriteComponent>(id);
					if (target)
					{
						Color const & tint = target->getTint();
						target->setTint(tint.r, tint.g, tint.b, values[0]);
					}
					return true;
				}, id);
		});
		runtime.registerFunction("tween", "volume",
			[](String const & group, double volume, double duration,
				ScriptArgs extra) -> TweenHandle
		{
			// the ducking recipe rides on this (see the table comment); no
			// target object - cleared with the scene like every tween
			const float fromVolume = SoundManager::getSingletonPtr()
				? SoundManager::getSingleton().getGroupVolume(group) : 1.0f;
			return tweenChannel1(fromVolume, static_cast<float>(volume),
				static_cast<float>(duration), extra,
				[group](float const * values, int) -> bool
				{
					if (SoundManager::getSingletonPtr())
					{
						SoundManager::getSingleton().setGroupVolume(group,
							values[0]);
					}
					return true;
				}, StringUtil::BLANK);
		});
		runtime.registerFunction("tween", "property",
			[](String const & id, String const & componentType,
				String const & propertyName, String const & toValue,
				double duration, ScriptArgs extra) -> TweenHandle
		{
			// the DECLARATIVE variant: resolve + interpolate a reflected property
			// by path through the ONE registry (core_game/PropertyTween). Optional
			// trailing args mirror the tween.* style: ease name, onComplete, delay.
			const String easeName = ScriptRuntime::stringArg(extra, 0, "");
			const ScriptCallback onComplete = ScriptCallback::fromArgs(extra, 1);
			const float delay = static_cast<float>(
				ScriptRuntime::numberArg(extra, 2, 0.0));
			TweenHandle handle;
			String error;
			handle.mId = PropertyTween::start(id, componentType, propertyName,
				toValue, static_cast<float>(duration), easeForScripts(easeName),
				wrapScriptComplete(onComplete), delay, &error);
			if (handle.mId == 0 && !error.empty())
			{
				EngineLogCapture::logError("tween.property: " + error);
			}
			return handle;
		});

		// ============== THE `guitween` TABLE (gui widget juice) =============
		// Animate a gui widget (by id) through the SAME TweenManager the game
		// tweens ride. Every helper RETARGET-REPLACES a running tween on the same
		// channel of that widget (last-wins) and AUTO-KILLS when the widget is
		// destroyed - both live in GuiManager. Layout-driven widgets tween their
		// LAYOUT INPUTS (anchoredPosition / sizeDelta) so animation composes with
		// the resolver. Every helper returns a handle: :cancel(), :isActive()
		// (the completion poll) and :setLoops(count, pingpong) (the chained loop
		// modifier). Trailing optional args are uniform: ease name, delay seconds,
		// onComplete callback.
		//   guitween.alpha(id, alpha, duration [, ease [, delay [, onComplete]]])
		//       group alpha 0..1 (cascades to the widget's layout subtree)
		//   guitween.scale(id, scale, duration [, ...])   uniform scale about the centre
		//   guitween.rotate(id, degrees, duration [, ...]) Z rotation about the centre
		//   guitween.move(id, x, y, duration [, ...])     anchoredPosition / position
		//   guitween.size(id, w, h, duration [, ...])     sizeDelta / size
		//   guitween.color(id, r, g, b, a, duration [, ...]) decor tint (decor widgets)
		//   guitween.show(id) / guitween.hide(id)   play the widget's enter/exit
		//       transition (set via .oui `transition` or widget:setTransition)
		//   guitween.stop(id)   cancel every running tween on the widget
		//   endless spinner:  guitween.rotate("spinner", 360, 1.0, "linear"):setLoops(-1, false)
		//   pulse:            guitween.scale("badge", 1.2, 0.3, "quadInOut"):setLoops(-1, true)
		runtime.registerFunction("guitween", "alpha",
			[](String const & id, double alpha, double duration,
				ScriptArgs extra) -> TweenHandle
		{
			const float to = static_cast<float>(alpha);
			return guiTweenStart(id, GuiManager::WTC_Alpha, &to,
				static_cast<float>(duration), extra);
		});
		runtime.registerFunction("guitween", "scale",
			[](String const & id, double scale, double duration,
				ScriptArgs extra) -> TweenHandle
		{
			const float to[2] = { static_cast<float>(scale),
				static_cast<float>(scale) };
			return guiTweenStart(id, GuiManager::WTC_Scale, to,
				static_cast<float>(duration), extra);
		});
		runtime.registerFunction("guitween", "rotate",
			[](String const & id, double degrees, double duration,
				ScriptArgs extra) -> TweenHandle
		{
			const float to = static_cast<float>(degrees);
			return guiTweenStart(id, GuiManager::WTC_Rotation, &to,
				static_cast<float>(duration), extra);
		});
		runtime.registerFunction("guitween", "move",
			[](String const & id, double x, double y, double duration,
				ScriptArgs extra) -> TweenHandle
		{
			const float to[2] = { static_cast<float>(x), static_cast<float>(y) };
			return guiTweenStart(id, GuiManager::WTC_Position, to,
				static_cast<float>(duration), extra);
		});
		runtime.registerFunction("guitween", "size",
			[](String const & id, double w, double h, double duration,
				ScriptArgs extra) -> TweenHandle
		{
			const float to[2] = { static_cast<float>(w), static_cast<float>(h) };
			return guiTweenStart(id, GuiManager::WTC_Size, to,
				static_cast<float>(duration), extra);
		});
		runtime.registerFunction("guitween", "color",
			[](String const & id, double r, double g, double b, double a,
				double duration, ScriptArgs extra) -> TweenHandle
		{
			const float to[4] = { static_cast<float>(r), static_cast<float>(g),
				static_cast<float>(b), static_cast<float>(a) };
			return guiTweenStart(id, GuiManager::WTC_Color, to,
				static_cast<float>(duration), extra);
		});
		runtime.registerFunction("guitween", "show",
			[](String const & id) -> bool
		{
			return GuiManager::getSingletonPtr() &&
				GuiManager::getSingleton().playWidgetTransition(id, true);
		});
		runtime.registerFunction("guitween", "hide",
			[](String const & id) -> bool
		{
			return GuiManager::getSingletonPtr() &&
				GuiManager::getSingleton().playWidgetTransition(id, false);
		});
		runtime.registerFunction("guitween", "stop",
			[](String const & id)
		{
			if (GuiManager::getSingletonPtr())
			{
				GuiManager::getSingleton().cancelWidgetTweens(id);
			}
		});

		// ============== THE `screens` TABLE (screen-flow router) ============
		// A whole-screen navigation stack on top of the gui. A "screen" is one
		// .oui layout (screens.define) or a Lua builder function
		// (screens.defineBuilder) registered under a name; push/replace/pop drive
		// which is current. Each push tears the old screen's widgets down (its
		// .oui/builder is the source of truth - transient state is not preserved)
		// and plays enter/exit transitions from the widgets' own `transition`
		// specs. The Android back button / Escape pop the stack by default; a
		// screen owns back by installing screens.setBackHandler.
		//   screens.define("title", "screens/title.oui")   register a .oui screen
		//   screens.defineBuilder("hud", function() ... end) register a code screen
		//   screens.push("title") / screens.replace("settings") / screens.pop()
		//   screens.current() -> name (""), screens.depth() -> count
		//   screens.setBackHandler(function() ... end)   own the back gesture
		//       (present handler = back is consumed; it pops/confirms itself)
		runtime.registerFunction("screens", "define",
			[](String const & name, String const & ouiPath)
		{
			if (!GuiManager::getSingletonPtr())
			{
				EngineLogCapture::logError("screens: no GuiManager - no UI booted");
				return;
			}
			GuiManager::getSingleton().registerScreen(name, ouiPath);
		});
		runtime.registerFunction("screens", "defineBuilder",
			[](String const & name, ScriptArgs extra)
		{
			if (!GuiManager::getSingletonPtr())
			{
				EngineLogCapture::logError("screens: no GuiManager - no UI booted");
				return;
			}
			const ScriptCallback builder = ScriptCallback::fromArgs(extra, 0);
			GuiManager::getSingleton().registerScreenBuilder(name, [builder]()
			{
				String error;
				if (!builder.invoke(&error))
				{
					EngineLogCapture::logError("screen builder '" + error + "'");
				}
			});
		});
		runtime.registerFunction("screens", "push", [](String const & name)
		{
			if (GuiManager::getSingletonPtr())
			{
				GuiManager::getSingleton().pushScreen(name);
			}
		});
		runtime.registerFunction("screens", "replace", [](String const & name)
		{
			if (GuiManager::getSingletonPtr())
			{
				GuiManager::getSingleton().replaceScreen(name);
			}
		});
		runtime.registerFunction("screens", "pop", []() -> String
		{
			return GuiManager::getSingletonPtr()
				? GuiManager::getSingleton().popScreen() : String("");
		});
		runtime.registerFunction("screens", "current", []() -> String
		{
			return GuiManager::getSingletonPtr()
				? GuiManager::getSingleton().currentScreen() : String("");
		});
		runtime.registerFunction("screens", "depth", []() -> int
		{
			return GuiManager::getSingletonPtr()
				? GuiManager::getSingleton().screenDepth() : 0;
		});
		runtime.registerFunction("screens", "clear", []()
		{
			if (GuiManager::getSingletonPtr())
			{
				GuiManager::getSingleton().clearScreens();
			}
		});
		runtime.registerFunction("screens", "setBackHandler",
			[](ScriptArgs extra)
		{
			if (!GuiManager::getSingletonPtr())
			{
				return;
			}
			const ScriptCallback handler = ScriptCallback::fromArgs(extra, 0);
			if (!handler.valid())
			{
				// setBackHandler(nil): drop back to the default pop behavior
				GuiManager::getSingleton().setScreenBackInterceptor(
					GuiManager::ScreenBackInterceptor());
				return;
			}
			GuiManager::getSingleton().setScreenBackInterceptor([handler]() -> bool
			{
				String error;
				if (!handler.invoke(&error))
				{
					EngineLogCapture::logError("screen back handler '" + error + "'");
				}
				// a handler is installed -> the screen owns the back gesture; it
				// pops / shows a confirm itself, so we always consume it here
				return true;
			});
		});

		// ================= THE `cvar` TABLE (live tuning) ==================
		// A thin Lua face on core_debug/CVarManager - the SAME registry the
		// console command grammar and the MSG_SET_CVAR protocol message drive,
		// so a cvar changed here, from the editor Console or over the debug
		// link all land in one place. The registry itself is pure core (works
		// with scripting OFF); this table is just the scripting-gated accessor.
		//   cvar.registerNumber(name, default)  register a Float cvar (idempotent -
		//                        a manifest/protocol override applied earlier survives)
		//   cvar.getNumber(name [, fallback])   read a cvar as a number
		//   cvar.getBool(name [, fallback])     read a cvar as a bool
		//   cvar.get(name)                      read a cvar's canonical string ("" if unset)
		//   cvar.set(name, value)               set from a string (validated per type);
		//                        returns true, or false + logs on a rejected value
		//   cvar.exists(name)                   is the cvar registered
		// Numbers are the common case (tuning constants), so registerNumber /
		// getNumber are first-class; game code that needs bool/string cvars
		// registers them from C++ (OCVAR_*) and reads them the typed way.
		runtime.registerFunction("cvar", "registerNumber",
			[](String const & name, double defaultValue)
		{
			CVarManager::getSingleton().registerCVar(name, CVarType::Float,
				cvarToString(static_cast<float>(defaultValue)));
		});
		runtime.registerFunction("cvar", "getNumber",
			[](String const & name, ScriptArgs extra) -> double
		{
			const double fallback = ScriptRuntime::numberArg(extra, 0, 0.0);
			return CVarManager::getSingleton().getFloat(name,
				static_cast<float>(fallback));
		});
		runtime.registerFunction("cvar", "getBool",
			[](String const & name, ScriptArgs extra) -> bool
		{
			const bool fallback = ScriptRuntime::numberArg(extra, 0, 0.0) != 0.0;
			return CVarManager::getSingleton().getBool(name, fallback);
		});
		runtime.registerFunction("cvar", "get",
			[](String const & name) -> String
		{
			return CVarManager::getSingleton().getString(name);
		});
		runtime.registerFunction("cvar", "exists", [](String const & name)
		{
			return CVarManager::getSingleton().exists(name);
		});
		runtime.registerFunction("cvar", "set",
			[](String const & name, String const & value) -> bool
		{
			String error;
			if (!CVarManager::getSingleton().setString(name, value, &error))
			{
				EngineLogCapture::logError("cvar.set: " + error);
				return false;
			}
			return true;
		});

		// ================= THE `save` TABLE (persistence) ==================
		// A general, per-project typed key -> value store (Number / Bool /
		// String; values are FLAT - namespace keys with dots like "hero.coins"
		// instead of nesting). Backed by core_game/SaveStore, written atomically
		// (temp file + rename) to the writable app directory; the player loads it
		// at boot and flushes it on a clean shutdown. Honest no-op / defaults
		// without a SaveStore (the editor never creates one).
		//   save.set(key, value)              store number/bool/string by its type
		//   save.getNumber(key [, fallback])  read a number
		//   save.getBool(key [, fallback])    read a bool
		//   save.getString(key [, fallback])  read a string (get is its alias)
		//   save.has(key)                     is the key present
		//   save.remove(key)                  drop the key
		//   save.flush()                      autosave point - write to disk NOW
		// Crash semantics: only a flush (or the clean-shutdown autosave) reaches
		// disk; a set marks the store dirty but does not write, so changes since
		// the last flush are lost on a hard crash (the crash breadcrumb trail is
		// the recovery aid for that window).
		runtime.registerFunction("save", "set",
			[](String const & key, ScriptArgs args)
		{
			if (!SaveStore::getSingletonPtr())
			{
				return;
			}
			SaveStore & store = SaveStore::getSingleton();
			switch (ScriptRuntime::argType(args, 0))
			{
			case ScriptRuntime::AT_BOOL:
				store.setBool(key, ScriptRuntime::boolArg(args, 0, false));
				break;
			case ScriptRuntime::AT_NUMBER:
				store.setNumber(key, ScriptRuntime::numberArg(args, 0, 0.0));
				break;
			case ScriptRuntime::AT_STRING:
				store.setString(key, ScriptRuntime::stringArg(args, 0, ""));
				break;
			default:
				EngineLogCapture::logError("save.set: '" + key + "' - value must "
					"be a number, bool or string");
				break;
			}
		});
		runtime.registerFunction("save", "getNumber",
			[](String const & key, ScriptArgs args) -> double
		{
			const double fallback = ScriptRuntime::numberArg(args, 0, 0.0);
			return SaveStore::getSingletonPtr()
				? SaveStore::getSingleton().getNumber(key, fallback) : fallback;
		});
		runtime.registerFunction("save", "getBool",
			[](String const & key, ScriptArgs args) -> bool
		{
			const bool fallback = ScriptRuntime::boolArg(args, 0, false);
			return SaveStore::getSingletonPtr()
				? SaveStore::getSingleton().getBool(key, fallback) : fallback;
		});
		runtime.registerFunction("save", "getString",
			[](String const & key, ScriptArgs args) -> String
		{
			const String fallback = ScriptRuntime::stringArg(args, 0, "");
			return SaveStore::getSingletonPtr()
				? SaveStore::getSingleton().getString(key, fallback) : fallback;
		});
		// save.get is the string getter alias (the untyped "read whatever is
		// there" accessor - a Number/Bool value comes back as its canonical text)
		runtime.registerFunction("save", "get",
			[](String const & key, ScriptArgs args) -> String
		{
			const String fallback = ScriptRuntime::stringArg(args, 0, "");
			return SaveStore::getSingletonPtr()
				? SaveStore::getSingleton().getString(key, fallback) : fallback;
		});
		runtime.registerFunction("save", "has", [](String const & key) -> bool
		{
			return SaveStore::getSingletonPtr() &&
				SaveStore::getSingleton().has(key);
		});
		runtime.registerFunction("save", "remove", [](String const & key)
		{
			if (SaveStore::getSingletonPtr())
			{
				SaveStore::getSingleton().remove(key);
			}
		});
		runtime.registerFunction("save", "flush", []() -> bool
		{
			return SaveStore::getSingletonPtr() &&
				SaveStore::getSingleton().flush();
		});

		// ================= THE `benchmark` TABLE (perf capture) ===========
		// Mark scene boundaries for the per-scene performance recorder
		// (core_debug/BenchmarkRecorder). OPT-IN and dormant: the recorder
		// only writes an artifact once the player armed it from an env; the
		// editor never arms it, so every call is an honest no-op there.
		//   benchmark.begin(name)  start (or restart/rename) a scene aggregation
		//   benchmark.endScene()   close the current scene, write its record
		//                          ('end' is a Lua keyword, so the method is
		//                           spelled endScene)
		//   benchmark.isArmed()    is a run being recorded (env-armed player)
		// A level switch is ALSO a scene boundary (the player calls beginScene
		// on each deferred load), so begin composes: an explicit begin renames/
		// restarts the current aggregation for a director-authored benchmark.
		runtime.registerFunction("benchmark", "begin", [](String const & name)
		{
			if (BenchmarkRecorder::getSingletonPtr())
			{
				BenchmarkRecorder::getSingleton().beginScene(name);
			}
		});
		runtime.registerFunction("benchmark", "endScene", []()
		{
			if (BenchmarkRecorder::getSingletonPtr())
			{
				BenchmarkRecorder::getSingleton().endScene();
			}
		});
		runtime.registerFunction("benchmark", "isArmed", []() -> bool
		{
			return BenchmarkRecorder::getSingletonPtr() &&
				BenchmarkRecorder::getSingleton().isArmed();
		});

		// ================= THE `events` TABLE (the message bus) ============
		// The Lua face of the ONE engine event bus (core_event/GlobalEventManager)
		// via core_script/ScriptEventBus - NOT a parallel system. The
		// multi-consumer, sandbox-scoped complement to the single-consumer gui
		// poll idiom (which stays valid): SEVERAL scripts react to one signal, and
		// C++ and Lua share the bus.
		//   events.subscribe(name, fn) -> handle   fn(payload) runs in the script
		//       tick phase; handle:cancel() / handle:isActive()
		//   events.emit(name [, payload])           queue an event; payload is a
		//       plain table of string/number/bool (one nesting level) - anything
		//       else errors AT emit
		// DELIVERY (deterministic): emissions queueEvent onto GlobalEventManager
		// and are drained ONCE per frame in the SCRIPT PHASE (its tick()), in
		// subscription order. Its double-buffered queue is the cascade-safety: an
		// emit from inside a handler lands in the opposing buffer and is delivered
		// NEXT frame (a handler never recurses into the same drain). An emit after
		// the phase (a physics contact mirror) also delivers next frame. gui input
		// is pumped before scripts, so a widget event is seen the SAME frame.
		// LIFETIME: subscriptions belong to the sandbox that made them - SUBSCRIBE
		// IN init(); destroying or hot-reloading the script component cancels them
		// (a fresh init re-subscribes). Engine mirrors: gui.clicked/toggled/
		// submitted/valueChanged/dialogResult/screenPushed/screenPopped/toastShown
		// (widget id / value in the payload), physics.contactBegin/contactEnd
		// ({a, b} ids), app.pause/app.resume.
		//
		// route the bus's handler-error reports to the engine log (editor Console
		// + log file + stderr), like every other script error
		ScriptEventBus::getSingleton().setErrorSink([](String const & message)
		{
			EngineLogCapture::logError(message);
		});
		runtime.registerFunction("events", "subscribe",
			[](String const & name, ScriptArgs args) -> EventSubscription
		{
			EventSubscription handle;
			const ScriptCallback callback = ScriptCallback::fromArgs(args, 0);
			if (!callback.valid())
			{
				EngineLogCapture::logError("events.subscribe('" + name +
					"'): the second argument must be a function");
				return handle;	// an invalid (never-live) handle
			}
			handle.mId = ScriptEventBus::getSingleton().subscribe(name, callback);
			return handle;
		});
		runtime.registerFunction("events", "emit",
			[](String const & name, ScriptArgs args)
		{
			// the seam bounded-converts the payload and raises a Lua error on an
			// out-of-bounds value (the honest failure at emit)
			ScriptRuntime::getSingleton().emitEventFromScript(name, args);
		});

		// ================= THE `timer` TABLE (scheduling) =================
		// Deferred callbacks on core_tween/TimerManager, ticked by the player
		// loop in the TWEEN PHASE (right after tweens - no new fence entry).
		//   timer.after(seconds, fn)  -> handle   run fn() ONCE after a delay
		//   timer.every(seconds, fn)  -> handle   run fn() every `seconds`
		//   timer.cancel(handle)      -> bool     stop it (also handle:cancel())
		// A timer is SANDBOX-SCOPED: it is tagged with the script sandbox that
		// scheduled it (the SAME owner token the `events` subscriptions use), so
		// removing the component / tearing the scene down / hot-reloading the
		// script AUTO-CANCELS it - a stale timer never fires into a dead sandbox.
		// SCHEDULE IN init/update as needed; there is no need to cancel on
		// shutdown. Honest no-op without a TimerManager (the editor never makes
		// one), returning a dead handle.
		runtime.registerFunction("timer", "after",
			[](double seconds, ScriptArgs args) -> TimerHandle
		{
			TimerHandle handle;
			if (!TimerManager::getSingletonPtr())
			{
				return handle;
			}
			const ScriptCallback callback = ScriptCallback::fromArgs(args, 0);
			if (!callback.valid())
			{
				EngineLogCapture::logError(
					"timer.after: the second argument must be a function");
				return handle;
			}
			// the current owner is the sandbox executing this call (set by the
			// ScriptCallScope around every script entry point) - the same token
			// ScriptEventBus tags subscriptions with, so ScriptInstance's
			// destructor cancels both in one retire
			void const * owner = ScriptEventBus::getSingleton().currentOwner();
			handle.mId = TimerManager::getSingleton().after(
				static_cast<float>(seconds), wrapScriptTimer(callback), owner);
			return handle;
		});
		runtime.registerFunction("timer", "every",
			[](double seconds, ScriptArgs args) -> TimerHandle
		{
			TimerHandle handle;
			if (!TimerManager::getSingletonPtr())
			{
				return handle;
			}
			const ScriptCallback callback = ScriptCallback::fromArgs(args, 0);
			if (!callback.valid())
			{
				EngineLogCapture::logError(
					"timer.every: the second argument must be a function");
				return handle;
			}
			void const * owner = ScriptEventBus::getSingleton().currentOwner();
			handle.mId = TimerManager::getSingleton().every(
				static_cast<float>(seconds), wrapScriptTimer(callback), owner);
			return handle;
		});
		runtime.registerFunction("timer", "cancel",
			[](TimerHandle handle) -> bool
		{
			return handle.cancel();
		});

		// ================= THE `game` TABLE (state) =======================
		// A THIN convenience over the event bus (NOT a state machine): the game
		// keeps ONE named state string, and every game.setState(name) fires a
		// `game.stateChanged` event ({ old, new }) so scripts react through the
		// SAME `events` bus. game.getState() reads it back. Honest no-op without a
		// GameState (the editor never makes one) - set stores nothing, get is "".
		runtime.registerFunction("game", "setState", [](String const & name)
		{
			if (GameState::getSingletonPtr())
			{
				GameState::getSingleton().set(name);
			}
		});
		runtime.registerFunction("game", "getState", []() -> String
		{
			return GameState::getSingletonPtr()
				? GameState::getSingleton().get() : String();
		});
	}
	//---------------------------------------------------------
	void ScriptComponent::failScript(String const & message)
	{
		this->mFailed = true;
		this->mErrorMessage = message;
		// logged exactly once (mFailed gates every later script call), in
		// every build configuration - the Lua message carries file:line
		GameObject* componentOwner = this->getComponentOwner();
		logScriptError(String("ScriptComponent[") +
			(componentOwner ? componentOwner->getObjectID() : String("?")) +
			", '" + this->mScriptFile + "']: SCRIPT ERROR - " + message +
			" (instance disabled)");
	}
	//---------------------------------------------------------
	void ScriptComponent::reportReloadError(String const & message)
	{
		this->mReloadError = message;
		// logged (like failScript) so it reaches the log file / stderr / the
		// editor Console - but the instance is NOT disabled: mFailed stays as
		// it was and the old script keeps ticking
		GameObject* componentOwner = this->getComponentOwner();
		logScriptError(String("ScriptComponent[") +
			(componentOwner ? componentOwner->getObjectID() : String("?")) +
			", '" + this->mScriptFile + "']: HOT-RELOAD ERROR - " + message +
			" (kept the running instance)");
	}
	//---------------------------------------------------------
	void ScriptComponent::resetScriptState()
	{
		if (this->mInstance)
		{
			if (this->mStarted)
			{
				String error;
				if (!this->mInstance->callShutdown(&error))
				{
					// already going down - log, do not re-enter failScript
					logScriptError(String("ScriptComponent['") +
						this->mScriptFile + "']: shutdown: " + error);
				}
			}
			this->mInstance.reset();
		}
		this->mStarted = false;
		this->mFailed = false;
		this->mErrorMessage = "";
		this->mReloadError = "";
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(ScriptComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(setScriptFile)
		OFUNCCR(getScriptFile)
		OFUNC(setScriptEnabled)
		OFUNC(isScriptEnabled)
		OFUNC(hasScriptError)
		OFUNCCR(getScriptError)
		OFUNC(isScriptStarted)
		OFUNC(reloadScript)
		OFUNC(hotReload)
		OFUNC(hasReloadError)
		OFUNCCR(getLastReloadError)
		// reflected schema: the script file (AssetRef, project-
		// relative path, asset-kind "script") + the enabled flag
		OPROPERTY_REF("script", Orkige::PropertyKind::AssetRef, "script", getScriptFile, setScriptFile, Orkige::PROP_NONE)
		OPROPERTY("enabled", Orkige::PropertyKind::Bool, isScriptEnabled, setScriptEnabled, Orkige::PROP_NONE)
		// runtime telemetry: reflected but TRANSIENT read-only live state - the
		// started flag and the current error text keep the debug protocol's
		// script readout (the editor's inline "(script error)") flowing off the
		// ONE registry (loud failures still ride the separate MSG_SCRIPT_ERROR).
		OPROPERTY_RO("started", Orkige::PropertyKind::Bool, isScriptStarted, Orkige::PROP_TRANSIENT)
		OPROPERTY_RO("error", Orkige::PropertyKind::String, getScriptError, Orkige::PROP_TRANSIENT)

		// self.script / world.getScript(id) hand Lua a WEAK handle: locks per call,
		// raises an honest error naming the owner once gone. @see TransformComponent.
		OWEAKHANDLE_BEGIN(Orkige::ScriptComponent, "ScriptComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(setScriptFile)
			OWEAKHANDLE_BASEMETHOD(getScriptFile)
			OWEAKHANDLE_BASEMETHOD(setScriptEnabled)
			OWEAKHANDLE_BASEMETHOD(isScriptEnabled)
			OWEAKHANDLE_BASEMETHOD(hasScriptError)
			OWEAKHANDLE_BASEMETHOD(getScriptError)
			OWEAKHANDLE_BASEMETHOD(isScriptStarted)
			OWEAKHANDLE_BASEMETHOD(reloadScript)
			OWEAKHANDLE_BASEMETHOD(hotReload)
			OWEAKHANDLE_BASEMETHOD(hasReloadError)
			OWEAKHANDLE_BASEMETHOD(getLastReloadError)
		OWEAKHANDLE_END
	OOBJECT_END
}
