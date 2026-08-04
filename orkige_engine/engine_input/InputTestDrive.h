/**************************************************************
	created:	2026/08/04 at 09:30
	filename: 	InputTestDrive.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __InputTestDrive_h__4_8_2026__09_30_00__
#define __InputTestDrive_h__4_8_2026__09_30_00__

#include "engine_module/EnginePrerequisites.h"
#include "engine_input/InputAction.h"
#include "engine_input/KeyEventData.h"

#include <vector>

namespace Orkige
{
	//! @brief the pure half of TEST-DRIVEN INPUT: what a target NAME means,
	//! and which keys a driver currently holds.
	//! @remarks A test presses INTENT - the same vocabulary the game reads
	//! back ("jump", "move+x") - so a test keeps meaning what it meant when a
	//! binding is re-authored. Both decisions here are pure (no singleton, no
	//! SDL, no window), so the grammar and the ledger are unit-tested
	//! headlessly and the impure half below is only three injectKey calls.
	//! @par The target grammar
	//! @code
	//!   jump        a DIGITAL action - its first key binding is pressed
	//!   move+x      one DIRECTION of an axis action: the keys that push
	//!   move-y      component x (or y) positive (or negative)
	//!   SPACE       a raw KEY, spelled the KeyCodeNames way, for anything
	//!               no action covers
	//! @endcode
	//! An action bound only to tilt or a controller axis has no key to press
	//! and is REFUSED by name - a silent no-press would make a test that
	//! proves nothing look like it passed.
	namespace InputTestDrive
	{
		//! what a target name resolved to: the key to press, or the ONE
		//! human-readable reason it could not be resolved
		struct Target
		{
			KeyEventData::KeyCode	key = KeyEventData::KC_UNASSIGNED;
			String					error;

			bool ok() const { return this->error.empty(); }
		};

		//! @brief pure: split a target into its action name and its direction
		//! suffix ("move+x" -> "move" + "+x"; "SPACE" -> "SPACE" + ""). Only
		//! the four component directions (+x, -x, +y, -y) are suffixes; every
		//! other spelling stays part of the name, so a key or action whose own
		//! name ends oddly is never silently cut in half.
		ORKIGE_ENGINE_DLL void splitTarget(String const & target,
			String & outName, String & outDirection);

		//! @brief pure: the key a target names.
		//! @param action the action definition @p name refers to, or NULL when
		//! the name is not an action (then @p name is read as a KEY name)
		//! @param direction "" or one of "+x" / "-x" / "+y" / "-y"
		//! @remarks With no direction a Key binding is what "press this action"
		//! means (the digital button shape). With one, the KeyAxis binding on
		//! that component supplies the positive or negative keys, and a plain
		//! Key binding on the component answers the positive direction (a Key
		//! binding contributes +1). Every other outcome is a refusal that says
		//! what the action is actually bound to.
		ORKIGE_ENGINE_DLL Target resolveKeys(InputAction const * action,
			String const & name, String const & direction);

		//! @brief pure: the ledger of keys a driver holds right now.
		//! @remarks It exists so a press is EXACTLY one down edge and a
		//! release exactly one up edge: two targets can name the same key
		//! ("jump" and "SPACE"), and an action map's edge snapshot would show
		//! neither a second press nor a stray release for the duplicate. It is
		//! also what lets a run release everything a finished test still held,
		//! so one test can never press a key into the next one.
		class ORKIGE_ENGINE_DLL HeldKeys
		{
			//--- Variables ---------------------------------------
		private:
			std::vector<KeyEventData::KeyCode> mKeys;
			//--- Methods -----------------------------------------
		public:
			//! @brief note @p key as held. @returns true when this ADDS the
			//! hold (so the caller injects a down edge), false when it was
			//! already held (nothing to inject).
			bool hold(KeyEventData::KeyCode key);
			//! @brief forget @p key. @returns true when it WAS held (so the
			//! caller injects an up edge), false when it was not.
			bool letGo(KeyEventData::KeyCode key);
			//! is @p key held right now
			bool holds(KeyEventData::KeyCode key) const;
			//! @brief every held key, in the order they were taken, and the
			//! ledger emptied - the "release what this test still holds" step
			std::vector<KeyEventData::KeyCode> takeAll();
			std::size_t size() const { return this->mKeys.size(); }
			bool empty() const { return this->mKeys.empty(); }
		};
	}

	//! @brief the TEST tier's input driver: press / release / release-all,
	//! resolved by the grammar above and injected through the ONE synthesis
	//! path (InputManager::injectKey), so a driven key is indistinguishable
	//! from a key the platform delivered - the action map, isKeyDown and every
	//! KeyPressed listener see exactly what ships.
	//! @remarks WHERE a press happens decides whether it is seen at all.
	//! InputActionMap takes ONE edge snapshot per frame in the tick order's
	//! input slot (pressed = down && !down-last-frame), so a key pressed from
	//! a test body - which the runner resumes in the SCRIPT phase, after that
	//! slot - registers as pressed in the very next frame's input slot, before
	//! the game scripts of that frame run. That is the guarantee a test relies
	//! on: `t.press("jump")` followed by one wait IS a press the game saw.
	//! Pressing any earlier in the same frame would be a lie about when the
	//! key went down; any later would make the edge invisible for a frame.
	//! @remarks This is deliberately NOT on the game-facing `input` table. A
	//! game script that can fake input is a real capability with real
	//! consequences - isKeyDown answering true for something nobody pressed
	//! muddies the input model for every reader. A game that wants it
	//! (replays, demos, attract modes) should get its own deliberate surface;
	//! until then the capability is installed only for a test run and reachable
	//! only from a test file's own sandbox.
	class ORKIGE_ENGINE_DLL InputTestDriver
	{
		//--- Variables ---------------------------------------
	private:
		InputTestDrive::HeldKeys	mHeld;
		//--- Methods -----------------------------------------
	public:
		//! @brief press @p target and HOLD it until released. False with
		//! @p outError when the target names nothing pressable or the input
		//! system is not up; pressing something already held is a no-op that
		//! still succeeds.
		bool press(String const & target, String & outError);
		//! @brief release @p target. False with @p outError on an unresolvable
		//! target; releasing something not held is a no-op that succeeds.
		bool release(String const & target, String & outError);
		//! @brief release everything still held - the boundary a test run puts
		//! between one test and the next
		void releaseAll();
		//! is anything held right now (diagnostics / tests)
		bool anyHeld() const { return !this->mHeld.empty(); }
	protected:
	private:
		//! resolve a target against the live action map (NULL-safe: without an
		//! action map every target is read as a key name)
		InputTestDrive::Target resolve(String const & target) const;
	};
	//---------------------------------------------------------
}

#endif //__InputTestDrive_h__4_8_2026__09_30_00__
