/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	ScriptComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/ScriptComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/RigidBodyComponent.h"
#include "engine_gocomponent/SpriteComponent.h"
#include "engine_gocomponent/ParticleComponent.h"
#include "engine_gocomponent/SoundComponent.h"
#include "engine_sound/SoundManager.h"
#include "engine_base/EngineLog.h"
#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_game/SceneSerializer.h>
#include <core_game/LevelComponent.h>
#include <core_game/LevelManager.h>
#include <core_debug/CVarManager.h>
#include <core_project/AssetDatabase.h>
#include <core_script/ScriptRuntime.h>
#include <core_tween/TweenManager.h>
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
		//! every live GameObject carrying the given tag (the manager tag index),
		//! as raw pointers - the Lua array world.findByTag returns
		std::vector<GameObject*> worldFindByTag(String const & tag)
		{
			std::vector<GameObject*> result;
			if (GameObjectManager::getSingletonPtr() == NULL)
			{
				return result;
			}
			GameObjectManager & manager = GameObjectManager::getSingleton();
			StringVector const ids = manager.findByTag(tag);
			result.reserve(ids.size());
			foreach(String const & id, ids)
			{
				if (GameObject* gameObject = manager.getGameObject(id).lock().get())
				{
					result.push_back(gameObject);
				}
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
		if (!this->mInstance->callFunction(hook, &error, other))
		{
			// an error inside the hook disables the instance like any other
			// script error (logged once through failScript)
			this->failScript(String(hook) + ": " + error);
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void ScriptComponent::onAdd()
	{
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
		instance->setSelfValue("id", componentOwner->getObjectID());
		instance->setSelfValue("gameObject", componentOwner);
		instance->setSelfValue("script", this);
		if (componentOwner->hasComponent<TransformComponent>())
		{
			instance->setSelfValue("transform",
				componentOwner->getComponentPtr<TransformComponent>());
		}
		if (componentOwner->hasComponent<RigidBodyComponent>())
		{
			instance->setSelfValue("rigidbody",
				componentOwner->getComponentPtr<RigidBodyComponent>());
		}
		if (componentOwner->hasComponent<ModelComponent>())
		{
			instance->setSelfValue("model",
				componentOwner->getComponentPtr<ModelComponent>());
		}
		if (componentOwner->hasComponent<SpriteComponent>())
		{
			instance->setSelfValue("sprite",
				componentOwner->getComponentPtr<SpriteComponent>());
		}
		if (componentOwner->hasComponent<ParticleComponent>())
		{
			instance->setSelfValue("particles",
				componentOwner->getComponentPtr<ParticleComponent>());
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
		runtime.registerFunction("world", "get", &worldGetGameObject);
		runtime.registerFunction("world", "getTransform",
			&worldGetComponent<TransformComponent>);
		runtime.registerFunction("world", "getRigidBody",
			&worldGetComponent<RigidBodyComponent>);
		runtime.registerFunction("world", "getModel",
			&worldGetComponent<ModelComponent>);
		runtime.registerFunction("world", "getSprite",
			&worldGetComponent<SpriteComponent>);
		runtime.registerFunction("world", "getParticles",
			&worldGetComponent<ParticleComponent>);
		runtime.registerFunction("world", "getScript",
			&worldGetComponent<ScriptComponent>);
		runtime.registerFunction("world", "getSound",
			&worldGetComponent<SoundComponent>);
		// world.findByTag(tag) -> array of the GameObjects carrying that tag
		// (empty table when none); tags are set in the editor Inspector or via
		// GameObject:addTag, indexed by the GameObjectManager
		runtime.registerFunction("world", "getLevel",
			&worldGetComponent<LevelComponent>);
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
		runtime.registerFunction("world", "findByTag", &worldFindByTag);

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
	OOBJECT_END
}
