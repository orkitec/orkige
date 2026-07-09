/********************************************************************
	created:	Tuesday 2026/07/07 at 12:00
	filename: 	ScriptComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ScriptComponent_h__7_7_2026__12_00_00__
#define __ScriptComponent_h__7_7_2026__12_00_00__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "core_base/PropertyValue.h"
#include "core_base/PropertySchema.h"
#include "core_util/String.h"
#include "core_util/optr.h"

#include <map>

namespace Orkige
{
	class ScriptInstance;	//core_script/ScriptRuntime.h - only the .cpp needs it
	class GameObject;		//the OTHER object of a contact event (dispatchContact)

	//! @brief attaches a Lua behavior script to a GameObject - game logic in
	//! project scripts instead of C++ (the successor of the dead luabind-era
	//! PythonScriptComponent).
	//!
	//! @remarks The script file path is project-relative (e.g.
	//! "scripts/player.lua") and resolved through
	//! ScriptRuntime::resolveScriptPath (project root first, then the working
	//! directory). Loading happens lazily on the FIRST component update, so a
	//! runtime that never ticks GameObjects - the editor in edit mode - keeps
	//! scripts completely dormant by construction; only a playing runtime
	//! (orkige_player, a game app) runs them.
	//!
	//! Script contract (all functions optional):
	//! @code
	//! function init(self)          -- once, right after the script loaded
	//! function update(self, dt)    -- every frame while enabled
	//! function shutdown(self)      -- when the component is removed/reloaded
	//! @endcode
	//! `self` is a per-instance table: self.id (the GameObject id),
	//! self.gameObject, self.script (this component) plus the sibling
	//! components attached at load time - self.transform, self.rigidbody,
	//! self.model (nil when not attached). The world API (the global `world`
	//! table: world.get/getTransform/getRigidBody/getScript/exists by
	//! GameObject id) reaches other objects; returned component pointers are
	//! only valid while the object lives - re-fetch instead of caching across
	//! frames when objects may be deleted.
	//!
	//! Each instance runs in its OWN sol::environment (globals of one script
	//! never leak into another instance, even of the same file). Deliberate
	//! cross-script state goes through the documented global table `shared`
	//! (created on first script load) - or _G for the truly global.
	//!
	//! Errors (load/init/update/shutdown) are logged ONCE with the Lua
	//! file:line, then the instance disables itself (mFailed) - a broken
	//! script never crashes or spams the log; the editor Inspector surfaces
	//! getScriptError(). reloadScript() clears the state for a fresh load on
	//! the next update (live-reload hook).
	//!
	//! In ORKIGE_SCRIPTING=OFF builds the component still exists and
	//! serializes (scenes keep round-tripping); a component WITH a script
	//! file that gets updated reports the honest "scripting disabled" error
	//! once (through the same failScript path as any other load failure).
	class ORKIGE_ENGINE_DLL ScriptComponent : public GameObjectComponent
	{
		OOBJECT(ScriptComponent,GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		String	mScriptFile;		//!< project-relative script path ("" = none)
		String	mScriptAssetId;	//!< stable asset id of the script ("" = none)
		bool	mScriptEnabled;	//!< update(self, dt) only runs while enabled
		bool	mStarted;			//!< script loaded and init(self) ran
		bool	mFailed;			//!< a script error disabled this instance
		String	mErrorMessage;		//!< the first error ("" while healthy)
		String	mReloadError;		//!< last hotReload() failure ("" = last reload was clean); mFailed stays false - the OLD instance keeps running
		optr<ScriptInstance>	mInstance;	//!< the loaded script instance (NULL until the lazy load)
		//! @brief the DYNAMIC per-instance schema built from the attached
		//! script's `properties` table; empty until a script with
		//! exports is attached and in ORKIGE_SCRIPTING=OFF builds. Its
		//! descriptors' get/set close over THIS component and read/write
		//! mExportValues, so the exports reflect through the ONE registry exactly
		//! like a static C++ property. Rebuilt on every export discovery.
		PropertySchema	mExportSchema;
		//! @brief the current per-instance value of each exported property, keyed
		//! by name (defaults from the declaration, overridable per instance in
		//! the inspector / over the wire). Serialized alongside the script path
		//! through the reflection-driven named path (@see save/load); injected
		//! into the script's `self` before init so the script reads it as a
		//! tunable. Reconciled BY NAME on re-discovery (kept values survive an
		//! export-set change, removed ones drop, new ones take their default).
		std::map<String, PropertyValue>	mExportValues;
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		ScriptComponent();
		//! destructor
		virtual ~ScriptComponent();
		//! @brief set the script file (project-relative path); a running
		//! script is shut down first, the new one loads on the next update
		void setScriptFile(String const & scriptFile);
		//! @see ScriptComponent::mScriptFile
		inline String const & getScriptFile() const;
		//! @see ScriptComponent::mScriptAssetId
		inline String const & getScriptAssetId() const;
		//! enable/disable the per-frame update calls (init state is kept)
		void setScriptEnabled(bool enabled);
		//! @see ScriptComponent::mScriptEnabled
		inline bool isScriptEnabled() const;
		//! did a script error disable this instance
		inline bool hasScriptError() const;
		//! the first script error message ("" while healthy)
		inline String const & getScriptError() const;
		//! has the script been loaded and init(self) run
		inline bool isScriptStarted() const;
		//! @brief drop the running script (shutdown(self) is called) and
		//! clear the error state - the file re-loads on the next update. This
		//! is the HARD reset used by setScriptFile/load: it tears the old
		//! instance down FIRST, so a broken file would leave the object dead.
		void reloadScript();
		//! @brief LIVE hot-reload: recompile the script file and swap
		//! it in with COMPILE-BEFORE-SWAP failure containment. The new file is
		//! loaded + init'd into a LOCAL temp instance FIRST; only when that
		//! succeeds does the old instance get shut down and replaced. A parse
		//! or init error DISCARDS the temp, keeps the OLD instance running and
		//! records getLastReloadError() WITHOUT setting the fatal mFailed flag
		//! (the object stays alive - a broken edit must never kill a running
		//! game). v1 is a FULL RE-INIT: the `shared`/`world` global tables and
		//! all engine-side state (positions/bodies/sprites, re-fetched from the
		//! live siblings) survive; env-locals and `self` reset. (v2 motivator:
		//! roller's tile-slot state lives only in game.lua env-locals and would
		//! desync on a full re-init - self-state preservation is out of scope
		//! here.) Dormant unless a runtime ticks GameObjects, exactly like the
		//! lazy first load; the editor is play-directed and only sends the
		//! reload message - the player calls this.
		void hotReload();
		//! did the last hotReload() fail (the old instance kept running)
		inline bool hasReloadError() const;
		//! the last hotReload() error message ("" when the last reload was
		//! clean); reported to the editor WITHOUT the fatal mFailed flag
		inline String const & getLastReloadError() const;
		//! @brief deliver a physics contact to the script: calls the
		//! OPTIONAL onContactBegin(self, other) / onContactEnd(self, other) hook
		//! with the OTHER GameObject. Called on the MAIN thread from the contact
		//! drain (RigidBodyComponent::dispatchContacts) - never from a Jolt
		//! worker callback. A no-op unless a script is loaded and healthy; a
		//! script the file does not define the hook for is a silent no-op. A
		//! script error inside the hook disables the instance (like update).
		//! Mutating the world here (spawn/destroy) is safe: it goes through the
		//! GameObjectManager delete queue, never mid-drain.
		void dispatchContact(GameObject* other, bool began);
		//! @brief the DYNAMIC schema of the attached script's exported properties
		//! the per-instance half of the reflection union. Empty
		//! until a script declaring a `properties` table is attached (and always
		//! empty in ORKIGE_SCRIPTING=OFF). @see getComponentSchema.
		virtual PropertySchema getInstancePropertySchema() const;
		//! read an exported property's current value (Int(0) when the name is not
		//! an export) - the reflected getter the dynamic descriptors close over
		PropertyValue getExportValue(String const & name) const;
		//! @brief write an exported property's value (the reflected setter the
		//! dynamic descriptors close over). Stores it in the per-instance bag AND,
		//! when a script is running, re-injects it onto `self` so a live set (over
		//! the debug protocol / MCP) reaches the script immediately.
		void setExportValue(String const & name, PropertyValue const & value);
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! lazy first load + init(self), then update(self, dt) per frame
		virtual void onUpdateComponent(float deltaTime);
		//--- SERIALIZATION ---
		//! save the script file path (plus its stable asset id as the
		//! assetId attribute) and the enabled flag to Archive
		virtual void save(optr<IArchive> const & ar);
		//! @brief load the script file path and the enabled flag from
		//! Archive; a script asset id that resolves in the open project's
		//! AssetDatabase wins over a stale path (rename survival)
		virtual void load(optr<IArchive> const & ar);
	private:
		//! @brief (re)discover the attached script's exported properties:
		//! read the `properties` table through the ScriptRuntime seam,
		//! reconcile the per-instance values BY NAME (keep a value whose name +
		//! kind survive, drop a removed export, add a new one at its declared
		//! default) and rebuild mExportSchema. Called on attach (setScriptFile),
		//! load and hotReload; a no-op without a runtime / a resolvable file /
		//! scripting (the schema then simply stays empty).
		void discoverExports();
		//! resolve, load and init the script; false (+failScript) on any error
		bool loadScriptNow();
		//! @brief populate the given instance's `self` table (owner id +
		//! convenience accessors for the sibling components attached NOW) -
		//! shared by loadScriptNow and hotReload so both build an identical
		//! `self`; the siblings are re-fetched from the live GameObject, so a
		//! hot-reloaded script sees the CURRENT engine-side state
		void populateSelfTable(optr<ScriptInstance> const & instance);
		//! register the global `world` and `shared` tables once per runtime
		static void ensureScriptApi();
		//! log the error once and disable the instance
		void failScript(String const & message);
		//! @brief record a hotReload() failure: store it in mReloadError and
		//! log it, but leave the instance ALIVE (mFailed untouched) - the old
		//! script keeps running and the editor surfaces the error non-fatally
		void reportReloadError(String const & message);
		//! shutdown a running script and reset to the not-started state
		void resetScriptState();
	};
	//---------------------------------------------------------
	inline String const & ScriptComponent::getScriptFile() const
	{
		return this->mScriptFile;
	}
	//---------------------------------------------------------
	inline String const & ScriptComponent::getScriptAssetId() const
	{
		return this->mScriptAssetId;
	}
	//---------------------------------------------------------
	inline bool ScriptComponent::isScriptEnabled() const
	{
		return this->mScriptEnabled;
	}
	//---------------------------------------------------------
	inline bool ScriptComponent::hasScriptError() const
	{
		return this->mFailed;
	}
	//---------------------------------------------------------
	inline String const & ScriptComponent::getScriptError() const
	{
		return this->mErrorMessage;
	}
	//---------------------------------------------------------
	inline bool ScriptComponent::isScriptStarted() const
	{
		return this->mStarted;
	}
	//---------------------------------------------------------
	inline bool ScriptComponent::hasReloadError() const
	{
		return !this->mReloadError.empty();
	}
	//---------------------------------------------------------
	inline String const & ScriptComponent::getLastReloadError() const
	{
		return this->mReloadError;
	}
	//---------------------------------------------------------
}

#endif //__ScriptComponent_h__7_7_2026__12_00_00__
