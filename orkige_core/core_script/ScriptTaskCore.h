/**************************************************************
	created:	2026/08/03 at 18:00
	filename: 	ScriptTaskCore.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptTaskCore_h__3_8_2026__18_00_00__
#define __ScriptTaskCore_h__3_8_2026__18_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */

	//! @brief what a suspended script task is waiting for. One kind per
	//! yield - a task waits for exactly one thing at a time.
	enum class ScriptWaitKind
	{
		Tick = 0,	//!< resume on the next task tick (the bare yield)
		Seconds,	//!< resume once the remaining seconds have elapsed
		Ticks,		//!< resume once the remaining ticks have passed
		Condition	//!< resume once the task's condition function returns true
	};

	//! @brief the wait a suspended task yielded with, plus its own countdown.
	//! A VALUE: the manager keeps one per live task and polls it once per
	//! tick, so the decision below stays a pure function of this state and
	//! the frame delta.
	struct ScriptTaskWait
	{
		ScriptWaitKind	kind = ScriptWaitKind::Tick;
		float			seconds = 0.0f;		//!< Seconds: what is left to wait
		int				ticks = 0;			//!< Ticks: how many are left; Condition: the give-up countdown
		int				limitTicks = 0;		//!< Condition: the DECLARED give-up cap (0 = wait forever)
	};

	//! @brief the decision for one task on one tick
	enum class ScriptTaskVerdict
	{
		Hold = 0,		//!< keep waiting - nothing to do this tick
		Resume,			//!< the wait is over: resume the coroutine
		AskCondition	//!< the answer belongs to the task's condition function
	};

	//! @brief the PURE half of the script-task scheduler: what a wait means,
	//! when it is over, and when a task has outstayed its budget. It knows
	//! nothing about coroutines, scripting backends or the engine - which is
	//! what makes the whole start / wait / resume / give-up / time-out
	//! behaviour exhaustively unit-testable headlessly, in EVERY build
	//! configuration (the decisions are the same with no interpreter compiled
	//! in; there is simply nothing to decide about).
	namespace ScriptTaskCore
	{
		//! a bare yield: resume on the next tick
		ScriptTaskWait waitTick();
		//! @brief wait `seconds` of scaled gameplay time. A non-positive
		//! delay cannot mean "resume immediately" - the task has already
		//! yielded - so it degrades to the next tick.
		ScriptTaskWait waitSeconds(double seconds);
		//! @brief wait `ticks` task ticks. Fewer than one is the next tick
		//! (the earliest a yielded task can possibly come back).
		ScriptTaskWait waitTicks(double ticks);
		//! @brief wait until a condition function answers true, asked once
		//! per tick. @param limitTicks >0 gives up after that many ticks
		//! (@see conditionGaveUp); 0 waits forever.
		ScriptTaskWait waitCondition(double limitTicks);

		//! @brief the once-per-tick decision, advancing the wait's own
		//! countdown by this tick. @param deltaSeconds the SCALED gameplay
		//! delta (a hitstop must not advance a wait).
		ScriptTaskVerdict pollWait(ScriptTaskWait & wait, float deltaSeconds);

		//! @brief after a Condition wait was asked and answered FALSE: has it
		//! now spent its declared give-up budget? Consumes one tick of that
		//! budget; always false for an unlimited wait.
		bool conditionGaveUp(ScriptTaskWait & wait);

		//! the honest refusal a give-up produces
		String conditionGiveUpMessage(int limitTicks);

		//! @brief the default per-test tick budget: a wedged wait must be a
		//! NAMED failure, never a hung run.
		int defaultTestTickLimit();

		//! @brief has a task lived longer than its budget? A limit of 0 (or
		//! less) is unbudgeted - the game-side default, where a task waiting
		//! for a door to open is not a bug.
		bool budgetSpent(int ticksLived, int tickLimit);

		//! the honest refusal an exhausted budget produces
		String budgetSpentMessage(int tickLimit);
	}

	/** @} */
}

#endif //__ScriptTaskCore_h__3_8_2026__18_00_00__
