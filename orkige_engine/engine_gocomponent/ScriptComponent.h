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
#include "core_util/String.h"
#include "core_util/optr.h"

namespace Orkige
{
	class ScriptInstance;	//core_script/ScriptRuntime.h - only the .cpp needs it

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
		optr<ScriptInstance>	mInstance;	//!< the loaded script instance (NULL until the lazy load)
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
		//! clear the error state - the file re-loads on the next update
		void reloadScript();
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
		//! resolve, load and init the script; false (+failScript) on any error
		bool loadScriptNow();
		//! register the global `world` and `shared` tables once per runtime
		static void ensureScriptApi();
		//! log the error once and disable the instance
		void failScript(String const & message);
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
}

#endif //__ScriptComponent_h__7_7_2026__12_00_00__
