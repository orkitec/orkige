/**************************************************************
	created:	2026/07/09 at 10:05
	filename: 	InputActionMap.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __InputActionMap_h__9_7_2026__10_05_00__
#define __InputActionMap_h__9_7_2026__10_05_00__

#include "engine_module/EnginePrerequisites.h"
#include "engine_input/InputAction.h"
#include "core_util/Singleton.h"

#include <vector>

namespace Orkige
{
	class Project;
	class InputManager;

	//! @brief the action-mapping layer: named, rebindable actions on top of
	//! the raw InputManager.
	//! @remarks Games ask for INTENT ("jump", "move") instead of hardware
	//! ("is SPACE down"), so the same script runs under keys, tilt or (later)
	//! gamepad/touch bindings. This is a PURELY ADDITIVE layer: it polls
	//! InputManager::isKeyDown / getTilt and never touches the raw input APIs,
	//! the event pub/sub or fastgui - those keep working untouched.
	//!
	//! ONCE-PER-FRAME CONTRACT: update(dt) takes ONE edge snapshot per frame
	//! (pressed = down && !down-last-frame) and every query reads that snapshot
	//! back. It MUST be called exactly once per frame, in the player loop's
	//! input slot BEFORE the scripts that read it (see the canonical tick order
	//! in tools/player/main.cpp). Recomputing edges per query or per script
	//! would make pressed() flicker across readers within one frame.
	//!
	//! CONFIG-ASSET CONVENTION (this is the FIRST project-config asset; the
	//! later cvars-persistence and physics.olayers packages copy this shape):
	//!   * a project-config asset is an XMLArchive file (here *.oactions),
	//!     REFERENCED from the manifest by a Settings key (here "input.actions",
	//!     ACTIONS_SETTING_KEY) holding a project-relative path.
	//!   * it is PROJECT-CONFIG, not content: it lives at a project-relative
	//!     path (conventionally the project root, e.g. "input.oactions"), NOT
	//!     under assets/, and is NOT id-tracked by the AssetDatabase.
	//!   * it is OPTIONAL: without the Settings key the built-in default set
	//!     (loadDefaults) applies, so reference projects run with zero
	//!     authoring. A present file fully REPLACES the defaults.
	//!   * export must bundle it when referenced - Util/orkige_export.py stages
	//!     manifest-referenced config files alongside the scenes/assets/scripts
	//!     payload (see CONFIG_SETTING_KEYS there).
	//! @note NOT an OOBJECT: like FastGuiFactory its Lua face is hand-written
	//! in engine_module/module.cpp (OSIMPLEEXPORT as "InputActions"), which
	//! keeps the game-facing name decoupled from the C++ class name. The class
	//! itself is a plain Singleton and compiles in every scripting config.
	class ORKIGE_ENGINE_DLL InputActionMap : public Singleton<InputActionMap>
	{
		DECL_OSINGLETON(InputActionMap);
		//--- Variables ---------------------------------------
	public:
		//! the manifest Settings key naming a project's action override file
		//! (project-relative path). The config-asset convention key (see the
		//! class docs); later config packages add sibling keys, never reuse it.
		static const String ACTIONS_SETTING_KEY;		//!< "input.actions"
		//! the on-disk extension of an action set (an XMLArchive document)
		static const String ACTIONS_FILE_EXTENSION;	//!< ".oactions"
		//! magic string written as the first archive value (format guard)
		static const String ACTIONS_FILE_MAGIC;		//!< "orkige.oactions"
		//! the format version written into every saved file
		static const int ACTIONS_FORMAT_VERSION;
	private:
		std::vector<InputAction> mActions;
		//--- Methods -----------------------------------------
	public:
		InputActionMap();
		virtual ~InputActionMap();

		//--- authoring / loading ---------------------------
		//! replace the set with the built-in defaults (the jumper + roller
		//! actions - see the .cpp; a superset, games query only what they use)
		void loadDefaults();
		//! @brief load an .oactions file (XMLArchive). On any error the current
		//! set is left UNCHANGED and false is returned (honest, non-destructive).
		bool loadActions(String const & fileName);
		//! @brief write the current set to an .oactions file (round-trip and
		//! the future authoring UI); returns false on a write error.
		bool saveActions(String const & fileName);
		//! @brief the project-load entry point: if the manifest has
		//! ACTIONS_SETTING_KEY, resolve it project-relative and loadActions();
		//! otherwise loadDefaults(). A referenced-but-unloadable file keeps the
		//! defaults (logged), so a typo never leaves the game input-less.
		void loadForProject(Project const & project);

		//--- per-frame update ------------------------------
		//! @brief recompute every action's edge snapshot from the live input.
		//! Call EXACTLY ONCE PER FRAME (player input slot, before scripts).
		void update(float deltaTime);

		//--- queries (read the last update() snapshot) -----
		bool down(String const & name) const;		//!< held this frame
		bool pressed(String const & name) const;		//!< went down this frame
		bool released(String const & name) const;		//!< went up this frame
		float value(String const & name) const;		//!< analog1D value (x component)
		Vec2 value2(String const & name) const;		//!< analog2D value (x, y)

		//--- introspection (tests / future editor) ---------
		bool hasAction(String const & name) const;
		size_t getActionCount() const { return mActions.size(); }
		//! append/replace an action definition (used by loaders and tests)
		void setAction(InputAction const & action);

		//--- pure math (headless-testable; mirrors advanceTiltAngle) --------
		//! @brief -1 / 0 / +1 from a negative/positive key-group state (the
		//! promoted jumper axis() helper). Both or neither held -> 0.
		static float axisFromKeys(bool negativeDown, bool positiveDown);
		//! @brief combine two contributions to one component: the one with the
		//! larger magnitude wins (ties keep the incoming candidate). This is the
		//! multi-binding rule (tilt OR arrows, whichever pushes harder).
		static float combineMaxMagnitude(float current, float candidate);
	protected:
	private:
		InputAction const * findAction(String const & name) const;
		//! evaluate one binding against the live input into its signed value
		static float evaluateBinding(InputActionBinding const & binding,
			InputManager & input);
	};
	//---------------------------------------------------------
}

#endif //__InputActionMap_h__9_7_2026__10_05_00__
