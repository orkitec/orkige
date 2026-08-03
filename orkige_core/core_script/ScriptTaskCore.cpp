/**************************************************************
	created:	2026/08/03 at 18:00
	filename: 	ScriptTaskCore.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptTaskCore.h"

#include <string>

namespace Orkige
{
	namespace ScriptTaskCore
	{
		//---------------------------------------------------------
		ScriptTaskWait waitTick()
		{
			ScriptTaskWait wait;
			wait.kind = ScriptWaitKind::Tick;
			return wait;
		}
		//---------------------------------------------------------
		ScriptTaskWait waitSeconds(double seconds)
		{
			if(!(seconds > 0.0))
			{
				// a task that yielded cannot come back before the next tick,
				// so a zero/negative/NaN delay IS the next tick - never a busy
				// spin inside this one
				return waitTick();
			}
			ScriptTaskWait wait;
			wait.kind = ScriptWaitKind::Seconds;
			wait.seconds = static_cast<float>(seconds);
			return wait;
		}
		//---------------------------------------------------------
		ScriptTaskWait waitTicks(double ticks)
		{
			if(!(ticks > 1.0))
			{
				return waitTick();
			}
			ScriptTaskWait wait;
			wait.kind = ScriptWaitKind::Ticks;
			wait.ticks = static_cast<int>(ticks);
			return wait;
		}
		//---------------------------------------------------------
		ScriptTaskWait waitCondition(double limitTicks)
		{
			ScriptTaskWait wait;
			wait.kind = ScriptWaitKind::Condition;
			wait.limitTicks = (limitTicks > 0.0) ? static_cast<int>(limitTicks) : 0;
			wait.ticks = wait.limitTicks;
			return wait;
		}
		//---------------------------------------------------------
		ScriptTaskVerdict pollWait(ScriptTaskWait & wait, float deltaSeconds)
		{
			switch(wait.kind)
			{
			case ScriptWaitKind::Seconds:
				wait.seconds -= deltaSeconds;
				return (wait.seconds <= 0.0f)
					? ScriptTaskVerdict::Resume : ScriptTaskVerdict::Hold;
			case ScriptWaitKind::Ticks:
				wait.ticks -= 1;
				return (wait.ticks <= 0)
					? ScriptTaskVerdict::Resume : ScriptTaskVerdict::Hold;
			case ScriptWaitKind::Condition:
				// the answer is not ours: only the condition function knows,
				// and it is called by the ONE tick site that owns resumption
				return ScriptTaskVerdict::AskCondition;
			case ScriptWaitKind::Tick:
			default:
				return ScriptTaskVerdict::Resume;
			}
		}
		//---------------------------------------------------------
		bool conditionGaveUp(ScriptTaskWait & wait)
		{
			if(wait.kind != ScriptWaitKind::Condition || wait.limitTicks <= 0)
			{
				return false;	// an unlimited wait never gives up
			}
			wait.ticks -= 1;
			return wait.ticks <= 0;
		}
		//---------------------------------------------------------
		String conditionGiveUpMessage(int limitTicks)
		{
			return "the condition never became true within " +
				std::to_string(limitTicks) + " frames";
		}
		//---------------------------------------------------------
		int defaultTestTickLimit()
		{
			// ~10 seconds at 60fps: long enough for a real playthrough leg,
			// short enough that a wedged wait names itself long before a CI
			// job's budget is at risk
			return 600;
		}
		//---------------------------------------------------------
		bool budgetSpent(int ticksLived, int tickLimit)
		{
			return tickLimit > 0 && ticksLived >= tickLimit;
		}
		//---------------------------------------------------------
		String budgetSpentMessage(int tickLimit)
		{
			return "timed out after " + std::to_string(tickLimit) +
				" frames without finishing";
		}
		//---------------------------------------------------------
	}
}
