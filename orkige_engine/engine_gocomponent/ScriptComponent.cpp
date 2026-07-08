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
#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_script/ScriptRuntime.h>

#include <OgreLogManager.h>

#include <cstdio>

namespace Orkige
{
	namespace
	{
		//! @brief script errors must be VISIBLE in every build configuration:
		//! the Ogre log reaches the engine log file, stderr and - in editor
		//! play mode - the editor Console via the player's log forwarder.
		//! Headless runs without an Ogre::LogManager fall back to stderr.
		void logScriptError(String const & message)
		{
			if (Ogre::LogManager::getSingletonPtr())
			{
				Ogre::LogManager::getSingleton().logError(message);
			}
			else
			{
				fprintf(stderr, "%s\n", message.c_str());
			}
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
		// only the authoring state round-trips: the script path and the
		// enabled flag; runtime state (loaded/failed, the Lua environment)
		// is rebuilt from the script file on the next play run
		ar << this->mScriptFile << this->mScriptEnabled;
	}
	//---------------------------------------------------------
	void ScriptComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		this->resetScriptState();
		ar >> this->mScriptFile >> this->mScriptEnabled;
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
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		this->mInstance->setSelfValue("id", componentOwner->getObjectID());
		this->mInstance->setSelfValue("gameObject", componentOwner);
		this->mInstance->setSelfValue("script", this);
		if (componentOwner->hasComponent<TransformComponent>())
		{
			this->mInstance->setSelfValue("transform",
				componentOwner->getComponentPtr<TransformComponent>());
		}
		if (componentOwner->hasComponent<RigidBodyComponent>())
		{
			this->mInstance->setSelfValue("rigidbody",
				componentOwner->getComponentPtr<RigidBodyComponent>());
		}
		if (componentOwner->hasComponent<ModelComponent>())
		{
			this->mInstance->setSelfValue("model",
				componentOwner->getComponentPtr<ModelComponent>());
		}
		if (componentOwner->hasComponent<SpriteComponent>())
		{
			this->mInstance->setSelfValue("sprite",
				componentOwner->getComponentPtr<SpriteComponent>());
		}

		this->mStarted = true;
		if (!this->mInstance->callInit(&error))
		{
			this->failScript(String("init: ") + error);
			return false;
		}
		oDebugMsg("script",0,"ScriptComponent: '" << this->mScriptFile
			<< "' loaded for '" << componentOwner->getObjectID() << "'");
		return true;
	}
	//---------------------------------------------------------
	void ScriptComponent::ensureScriptApi()
	{
		ScriptRuntime & runtime = ScriptRuntime::getSingleton();
		// `shared`: the documented cross-instance state escape hatch (per-
		// instance environments make sharing opt-in instead of accidental)
		runtime.ensureGlobalTable("shared");
		// `world`: reach OTHER GameObjects by id. Raw component pointers -
		// valid while the object lives; scripts re-fetch when in doubt.
		if (runtime.hasGlobalTable("world"))
		{
			return;
		}
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
		runtime.registerFunction("world", "getScript",
			&worldGetComponent<ScriptComponent>);
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
	OOBJECT_END
}
