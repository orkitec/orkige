/**************************************************************
	created:	2026/08/03 at 18:00
	filename: 	ScriptTaskManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptTaskManager_h__3_8_2026__18_00_00__
#define __ScriptTaskManager_h__3_8_2026__18_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"
#include "core_script/ScriptTaskCore.h"

#include <functional>
#include <vector>

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */

	//! @brief how far a suspended task got. A finished task keeps its outcome
	//! readable for exactly one more tick (the sweep runs at the START of the
	//! next update), so whoever started it can read the verdict on the frame
	//! it landed.
	enum class ScriptTaskStatus
	{
		Unknown = 0,	//!< no task with that id (finished and swept, or never started)
		Running,		//!< live: running or suspended in a wait
		Completed,		//!< the body returned
		Failed,			//!< the body raised, or a condition function did
		Cancelled,		//!< cancelled by handle, by owner retire or by scene teardown
		TimedOut		//!< the tick budget ran out before the body finished
	};

	//! @brief what one resume of a task's coroutine did
	enum class ScriptTaskStep
	{
		Yielded = 0,	//!< suspended again on a new wait
		Finished,		//!< the body returned
		Failed			//!< the body raised
	};

	//! @brief the ONE scheduler for script TASKS: a piece of game code that
	//! runs across frames, suspends itself on a wait and continues where it
	//! left off (the `script.async` surface, @see Docs/lua-api.md).
	//!
	//! THE RULE THAT MAKES IT SAFE: a task is resumed ONLY here, and this is
	//! ticked ONLY in the SCRIPT PHASE of the canonical game-loop tick order
	//! (@see Orkige::advanceGameWorld). A task can therefore never continue
	//! inside a physics contact callback, an event dispatch or a render pass -
	//! the reentrancy hazard is gone by construction, not by discipline. Do
	//! not add a second resume site.
	//!
	//! LIFETIME RULES (the same ones timers and event subscriptions carry):
	//!  * a task is SANDBOX-SCOPED: it names the script sandbox that started
	//!    it, and cancelOwner() drops every task of a retired sandbox without
	//!    resuming it - so removing the component, tearing the scene down or
	//!    hot-reloading the script cancels its tasks. A suspended task can
	//!    never continue into a dead sandbox.
	//!  * clear() drops every task without resuming it; the ONE authoritative
	//!    call site is the scene teardown hook GameObjectManager::clear,
	//!    beside the TweenManager/TimerManager clears.
	//!  * a task is never serialized: it is live control flow, not state. A
	//!    save file records what the game DECIDED, never where a coroutine
	//!    was suspended.
	//!
	//! The COROUTINE itself belongs to the scripting backend, which hands its
	//! three operations over as `Backend` (registered before any instance
	//! exists, like the scriptable-component registry). This class owns only
	//! the neutral half - the task list, the ownership rules and the pure
	//! wait/budget decisions - so it compiles and behaves identically with no
	//! interpreter in the build: without a backend nothing can ever start.
	//!
	//! The editor never creates one, so tasks are dormant in edit mode for
	//! free - exactly like tweens and timers.
	class ORKIGE_CORE_DLL ScriptTaskManager : public Singleton<ScriptTaskManager>
	{
		DECL_OSINGLETON(ScriptTaskManager)
		//--- Types -------------------------------------------
	public:
		typedef unsigned long long TaskId;	//!< task identity (0 = invalid)

		//! @brief the scripting backend's coroutine operations, keyed by the
		//! opaque SLOT the backend handed out when it created the coroutine.
		//! Neutral by construction: no backend type crosses this boundary.
		struct Backend
		{
			//! @brief resume the slot's coroutine, filling the new wait when
			//! it yields. @p owner is the sandbox the task belongs to, so the
			//! backend can tag whatever the body subscribes to / schedules
			//! with it - a task runs AS its sandbox.
			std::function<ScriptTaskStep(int slot, void const * owner,
				ScriptTaskWait & outWait, String & outError)> resume;
			//! ask the slot's condition function; false + outError on a raise
			std::function<bool(int slot, void const * owner, bool & outReady,
				String & outError)>	condition;
			//! drop everything the slot holds (the coroutine dies unresumed)
			std::function<void(int slot)> release;
		};
	protected:
		//! one live (or just-finished) task
		struct Task
		{
			TaskId				mId;			//!< identity
			int					mSlot;			//!< the backend's coroutine slot
			void const *		mOwner;			//!< the sandbox that started it (NULL = unowned)
			int					mTickLimit;		//!< budget in ticks (0 = unbudgeted)
			int					mTicksLived;	//!< ticks this task has been alive
			ScriptTaskWait		mWait;			//!< what it is waiting for
			ScriptTaskStatus	mStatus;		//!< Running until it lands
			String				mError;			//!< the failure text when it did not complete
		};
		//--- Variables ---------------------------------------
	protected:
		std::vector<Task>	mTasks;		//!< live + just-finished tasks
		TaskId				mNextId;	//!< next task identity
		bool				mUpdating;	//!< inside update() - guards a reentrant clear()
		Backend				mBackend;	//!< the scripting backend's coroutine operations
		std::function<void(String const &)>	mErrorSink;	//!< where a task failure is reported
		//--- Methods -----------------------------------------
	public:
		//! constructor - picks up the backend the scripting runtime registered
		ScriptTaskManager();
		//! destructor - releases every task's coroutine
		virtual ~ScriptTaskManager();

		//! @brief declare the scripting backend's coroutine operations. Called
		//! by the scripting runtime at boot, so it must be safe BEFORE any
		//! ScriptTaskManager exists (it is a process-wide record every later
		//! instance picks up).
		static void registerBackend(Backend backend);
		//! @brief override this instance's backend (the seam a headless test
		//! drives the whole scheduler through with no interpreter at all)
		void setBackend(Backend backend);
		//! can a task be started at all (a backend is present)
		bool hasBackend() const;

		//! @brief adopt a coroutine the backend created: it becomes a live
		//! task, first resumed on the NEXT tick (never inside the call that
		//! started it - the one resume site owns that).
		//! @param slot the backend's coroutine slot
		//! @param owner the script sandbox the task belongs to (@see cancelOwner)
		//! @param tickLimit ticks before the task is given up on as TimedOut
		//! (0 = unbudgeted, the game-side default)
		//! @return the task's id, or 0 without a backend
		TaskId start(int slot, void const * owner, int tickLimit);

		//! @brief cancel a task - it is never resumed again.
		//! @return true when it was still running
		bool cancel(TaskId id);
		//! is the task still live
		bool isActive(TaskId id) const;
		//! how far the task got (@see ScriptTaskStatus)
		ScriptTaskStatus statusOf(TaskId id) const;
		//! the failure text of a task that did not complete ("" otherwise)
		String errorOf(TaskId id) const;

		//! @brief cancel EVERY task owned by `owner` (a retired sandbox); a
		//! NULL owner matches nothing.
		//! @return how many were cancelled
		int cancelOwner(void const * owner);

		//! @brief advance every live task by one tick: sweep the landed ones,
		//! then poll each wait and resume the tasks whose wait is over.
		//! @param delta the SCALED gameplay delta - a hitstop must not
		//! advance a wait
		//! @remarks THE ONE RESUME SITE. Call it from the script phase of the
		//! canonical tick order and nowhere else.
		void update(float delta);

		//! @brief drop every task WITHOUT resuming it. Called from THE scene
		//! teardown hook (GameObjectManager::clear).
		void clear();

		//! number of live tasks
		std::size_t getActiveCount() const;

		//! @brief where a task failure is reported (the engine log). Without
		//! a sink a failure is still recorded on the task itself.
		void setErrorSink(std::function<void(String const &)> sink);
	protected:
		//! index of the task with that id, or -1
		int findTask(TaskId id) const;
		//! release the backend slot and record how the task landed
		void landTask(Task & task, ScriptTaskStatus status,
			String const & error);
	private:
	};
	//---------------------------------------------------------------
	//! @brief the script-facing handle to a started task - a tiny value type
	//! (safe to copy into Lua) that talks to the ScriptTaskManager singleton
	//! by id; every operation is a harmless no-op after the task landed or
	//! when no ScriptTaskManager exists (the editor).
	struct ORKIGE_CORE_DLL ScriptTaskHandle
	{
		ScriptTaskManager::TaskId	mId = 0;	//!< the task's id (0 = invalid)

		//! cancel the task - it is never resumed again
		//! @return true when it was still running
		bool cancel();
		//! is the task still running
		bool isActive() const;
	};
	/** @} */
}

#endif //__ScriptTaskManager_h__3_8_2026__18_00_00__
