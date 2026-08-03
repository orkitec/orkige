/**************************************************************
	created:	2026/08/03 at 18:00
	filename: 	ScriptTaskManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptTaskManager.h"
#include "core_debug/Profile.h"

#include <algorithm>
#include <functional>

namespace Orkige
{
	IMPL_OSINGLETON(ScriptTaskManager)

	namespace
	{
		//! the process-wide backend record: written once by the scripting
		//! runtime at boot (before any manager exists), read by every manager
		//! that is created afterwards
		ScriptTaskManager::Backend & registeredBackend()
		{
			static ScriptTaskManager::Backend backend;
			return backend;
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ScriptTaskManager::ScriptTaskManager() : mNextId(1), mUpdating(false)
	{
		this->mBackend = registeredBackend();
	}
	//---------------------------------------------------------
	ScriptTaskManager::~ScriptTaskManager()
	{
		this->clear();
	}
	//---------------------------------------------------------
	void ScriptTaskManager::registerBackend(Backend backend)
	{
		registeredBackend() = backend;
	}
	//---------------------------------------------------------
	void ScriptTaskManager::setBackend(Backend backend)
	{
		this->mBackend = backend;
	}
	//---------------------------------------------------------
	bool ScriptTaskManager::hasBackend() const
	{
		return this->mBackend.resume != 0;
	}
	//---------------------------------------------------------
	ScriptTaskManager::TaskId ScriptTaskManager::start(int slot,
		void const * owner, int tickLimit)
	{
		if(!this->hasBackend())
		{
			return 0;
		}
		Task task;
		task.mId = this->mNextId++;
		task.mSlot = slot;
		task.mOwner = owner;
		task.mTickLimit = tickLimit;
		task.mTicksLived = 0;
		// a fresh task is "waiting for the next tick": its FIRST resume
		// happens at the one resume site, never inside the call that started
		// it - so starting a task from a script never re-enters the script
		task.mWait = ScriptTaskCore::waitTick();
		task.mStatus = ScriptTaskStatus::Running;
		// safe while update() iterates: it walks by index over the size it
		// read at entry, so a task started from a resumed body takes its
		// first step on the NEXT tick
		this->mTasks.push_back(task);
		return task.mId;
	}
	//---------------------------------------------------------
	int ScriptTaskManager::findTask(TaskId id) const
	{
		if(id == 0)
		{
			return -1;
		}
		for(std::size_t i = 0; i < this->mTasks.size(); ++i)
		{
			if(this->mTasks[i].mId == id)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}
	//---------------------------------------------------------
	void ScriptTaskManager::landTask(Task & task, ScriptTaskStatus status,
		String const & error)
	{
		if(task.mStatus != ScriptTaskStatus::Running)
		{
			return;
		}
		task.mStatus = status;
		task.mError = error;
		if(this->mBackend.release)
		{
			this->mBackend.release(task.mSlot);
		}
	}
	//---------------------------------------------------------
	bool ScriptTaskManager::cancel(TaskId id)
	{
		const int index = this->findTask(id);
		if(index < 0 || this->mTasks[index].mStatus != ScriptTaskStatus::Running)
		{
			return false;
		}
		this->landTask(this->mTasks[index], ScriptTaskStatus::Cancelled,
			String());
		return true;
	}
	//---------------------------------------------------------
	bool ScriptTaskManager::isActive(TaskId id) const
	{
		const int index = this->findTask(id);
		return index >= 0 &&
			this->mTasks[index].mStatus == ScriptTaskStatus::Running;
	}
	//---------------------------------------------------------
	ScriptTaskStatus ScriptTaskManager::statusOf(TaskId id) const
	{
		const int index = this->findTask(id);
		return index >= 0
			? this->mTasks[index].mStatus : ScriptTaskStatus::Unknown;
	}
	//---------------------------------------------------------
	String ScriptTaskManager::errorOf(TaskId id) const
	{
		const int index = this->findTask(id);
		return index >= 0 ? this->mTasks[index].mError : String();
	}
	//---------------------------------------------------------
	int ScriptTaskManager::cancelOwner(void const * owner)
	{
		if(owner == 0)
		{
			return 0;	// an unowned task is never bulk-cancelled
		}
		int cancelled = 0;
		for(std::size_t i = 0; i < this->mTasks.size(); ++i)
		{
			if(this->mTasks[i].mOwner == owner &&
				this->mTasks[i].mStatus == ScriptTaskStatus::Running)
			{
				this->landTask(this->mTasks[i], ScriptTaskStatus::Cancelled,
					String());
				++cancelled;
			}
		}
		return cancelled;
	}
	//---------------------------------------------------------
	void ScriptTaskManager::update(float delta)
	{
		OPROFILE("tasks.update");
		// SWEEP FIRST, resume after: a task that landed during the PREVIOUS
		// tick stays readable for the whole frame it landed on (statusOf /
		// errorOf), which is what lets whoever started it read the verdict
		// without racing the sweep
		this->mTasks.erase(std::remove_if(this->mTasks.begin(),
			this->mTasks.end(), [](Task const & task)
		{
			return task.mStatus != ScriptTaskStatus::Running;
		}), this->mTasks.end());

		this->mUpdating = true;
		// by index over the size read at entry: a body that starts another
		// task appends, and the newcomer takes its first step next tick
		const std::size_t taskCount = this->mTasks.size();
		for(std::size_t i = 0; i < taskCount && i < this->mTasks.size(); ++i)
		{
			if(this->mTasks[i].mStatus != ScriptTaskStatus::Running)
			{
				continue;
			}
			// THE BUDGET, checked before anything else can consume the tick:
			// a wedged wait must be a NAMED failure, never a run that hangs
			this->mTasks[i].mTicksLived += 1;
			if(ScriptTaskCore::budgetSpent(this->mTasks[i].mTicksLived,
				this->mTasks[i].mTickLimit))
			{
				const String message = ScriptTaskCore::budgetSpentMessage(
					this->mTasks[i].mTickLimit);
				this->landTask(this->mTasks[i], ScriptTaskStatus::TimedOut,
					message);
				continue;
			}
			ScriptTaskVerdict verdict =
				ScriptTaskCore::pollWait(this->mTasks[i].mWait, delta);
			if(verdict == ScriptTaskVerdict::AskCondition)
			{
				bool ready = false;
				String error;
				const int slot = this->mTasks[i].mSlot;
				void const * const owner = this->mTasks[i].mOwner;
				if(!this->mBackend.condition ||
					!this->mBackend.condition(slot, owner, ready, error))
				{
					if(error.empty())
					{
						error = "the condition could not be evaluated";
					}
					this->landTask(this->mTasks[i], ScriptTaskStatus::Failed,
						error);
					if(this->mErrorSink)
					{
						this->mErrorSink(error);
					}
					continue;
				}
				if(ready)
				{
					verdict = ScriptTaskVerdict::Resume;
				}
				else if(ScriptTaskCore::conditionGaveUp(this->mTasks[i].mWait))
				{
					const String message =
						ScriptTaskCore::conditionGiveUpMessage(
							this->mTasks[i].mWait.limitTicks);
					this->landTask(this->mTasks[i], ScriptTaskStatus::Failed,
						message);
					if(this->mErrorSink)
					{
						this->mErrorSink(message);
					}
					continue;
				}
				else
				{
					verdict = ScriptTaskVerdict::Hold;
				}
			}
			if(verdict != ScriptTaskVerdict::Resume)
			{
				continue;
			}
			ScriptTaskWait nextWait;
			String error;
			const int resumeSlot = this->mTasks[i].mSlot;
			void const * const resumeOwner = this->mTasks[i].mOwner;
			const ScriptTaskStep step =
				this->mBackend.resume(resumeSlot, resumeOwner, nextWait, error);
			// the resume may have cancelled this very task (a body that
			// retires its own sandbox / clears the scene): never overwrite a
			// landed task with its own resume's outcome
			if(this->mTasks[i].mStatus != ScriptTaskStatus::Running)
			{
				continue;
			}
			if(step == ScriptTaskStep::Yielded)
			{
				this->mTasks[i].mWait = nextWait;
			}
			else if(step == ScriptTaskStep::Finished)
			{
				this->landTask(this->mTasks[i], ScriptTaskStatus::Completed,
					String());
			}
			else
			{
				this->landTask(this->mTasks[i], ScriptTaskStatus::Failed,
					error);
				if(this->mErrorSink)
				{
					this->mErrorSink(error);
				}
			}
		}
		this->mUpdating = false;
	}
	//---------------------------------------------------------
	void ScriptTaskManager::clear()
	{
		for(std::size_t i = 0; i < this->mTasks.size(); ++i)
		{
			this->landTask(this->mTasks[i], ScriptTaskStatus::Cancelled,
				String());
		}
		if(this->mUpdating)
		{
			// a scene teardown from inside a resumed body: the tasks are
			// cancelled above and the loop skips them; the vector itself is
			// swept at the start of the next update
			return;
		}
		this->mTasks.clear();
	}
	//---------------------------------------------------------
	std::size_t ScriptTaskManager::getActiveCount() const
	{
		std::size_t count = 0;
		for(Task const & task : this->mTasks)
		{
			if(task.mStatus == ScriptTaskStatus::Running)
			{
				++count;
			}
		}
		return count;
	}
	//---------------------------------------------------------
	void ScriptTaskManager::setErrorSink(std::function<void(String const &)> sink)
	{
		this->mErrorSink = sink;
	}
	//---------------------------------------------------------
	//--- ScriptTaskHandle ------------------------------------
	//---------------------------------------------------------
	bool ScriptTaskHandle::cancel()
	{
		ScriptTaskManager* manager = ScriptTaskManager::getSingletonPtr();
		return manager != 0 && this->mId != 0 && manager->cancel(this->mId);
	}
	//---------------------------------------------------------
	bool ScriptTaskHandle::isActive() const
	{
		ScriptTaskManager* manager = ScriptTaskManager::getSingletonPtr();
		return manager != 0 && this->mId != 0 && manager->isActive(this->mId);
	}
}
