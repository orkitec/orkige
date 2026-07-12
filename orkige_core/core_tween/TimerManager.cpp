/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	TimerManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_tween/TimerManager.h"
#include "core_debug/Profile.h"

#include <algorithm>

namespace Orkige
{
	IMPL_OSINGLETON(TimerManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	TimerManager::TimerManager() : mNextId(1), mUpdating(false)
	{
	}
	//---------------------------------------------------------
	TimerManager::~TimerManager()
	{
	}
	//---------------------------------------------------------
	TimerManager::TimerId TimerManager::after(float seconds,
		TimerFunction const & fn, void const * owner)
	{
		Timer timer;
		timer.mId = this->mNextId++;
		timer.mInterval = seconds;
		timer.mRemaining = seconds;
		timer.mRepeat = false;
		timer.mOwner = owner;
		timer.mFn = fn;
		timer.mDone = false;
		// safe while update() iterates: it walks by index over the size
		// captured at entry, so a timer scheduled from a callback fires on the
		// NEXT update
		this->mTimers.push_back(timer);
		return timer.mId;
	}
	//---------------------------------------------------------
	TimerManager::TimerId TimerManager::every(float seconds,
		TimerFunction const & fn, void const * owner)
	{
		Timer timer;
		timer.mId = this->mNextId++;
		timer.mInterval = seconds;
		timer.mRemaining = seconds;
		timer.mRepeat = true;
		timer.mOwner = owner;
		timer.mFn = fn;
		timer.mDone = false;
		this->mTimers.push_back(timer);
		return timer.mId;
	}
	//---------------------------------------------------------
	bool TimerManager::cancel(TimerId id)
	{
		for(Timer & timer : this->mTimers)
		{
			if(timer.mId == id && !timer.mDone)
			{
				// mark only - erasing here would break a running update() pass;
				// the sweep at the end of update() reclaims it
				timer.mDone = true;
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	bool TimerManager::isActive(TimerId id) const
	{
		for(Timer const & timer : this->mTimers)
		{
			if(timer.mId == id)
			{
				return !timer.mDone;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	int TimerManager::cancelOwner(void const * owner)
	{
		if(owner == 0)
		{
			return 0;	// an unowned timer is never bulk-cancelled
		}
		int cancelled = 0;
		for(Timer & timer : this->mTimers)
		{
			if(timer.mOwner == owner && !timer.mDone)
			{
				timer.mDone = true;
				++cancelled;
			}
		}
		return cancelled;
	}
	//---------------------------------------------------------
	void TimerManager::update(float delta)
	{
		OPROFILE("timers.update");
		this->mUpdating = true;
		// index walk over the entry size: callbacks may push_back (grow +
		// reallocate) or cancel - never hold a reference across a callback
		const std::size_t timerCount = this->mTimers.size();
		for(std::size_t i = 0; i < timerCount; ++i)
		{
			if(this->mTimers[i].mDone)
			{
				continue;
			}
			this->mTimers[i].mRemaining -= delta;
			// fire while due; a repeating timer catches up missed periods
			// (bounded by MAX_CATCHUP so a huge delta cannot spin forever)
			int fired = 0;
			while(!this->mTimers[i].mDone &&
				this->mTimers[i].mRemaining <= 0.0f)
			{
				const bool repeat = this->mTimers[i].mRepeat;
				const float interval = this->mTimers[i].mInterval;
				// a repeating timer re-arms for the next period; a one-shot (or
				// a non-positive period that cannot repeat) is marked done
				// BEFORE the callback so a reentrant cancel/schedule can never
				// refire it
				if(repeat && interval > 0.0f)
				{
					this->mTimers[i].mRemaining += interval;
				}
				else
				{
					this->mTimers[i].mDone = true;
				}
				// copy: the callback may mutate mTimers (schedule/cancel)
				const TimerFunction fn = this->mTimers[i].mFn;
				if(fn)
				{
					fn();
				}
				if(++fired >= MAX_CATCHUP)
				{
					break;
				}
			}
		}
		this->mUpdating = false;

		// sweep everything fired-once/cancelled this pass
		this->mTimers.erase(std::remove_if(this->mTimers.begin(),
			this->mTimers.end(),
			[](Timer const & timer) { return timer.mDone; }),
			this->mTimers.end());
	}
	//---------------------------------------------------------
	void TimerManager::clear()
	{
		if(this->mUpdating)
		{
			// scene teardown from inside a timer callback (a script switching
			// scenes): mark everything dead; the running update() pass skips
			// and sweeps them - timers scheduled AFTER this clear survive
			for(Timer & timer : this->mTimers)
			{
				timer.mDone = true;
			}
			return;
		}
		this->mTimers.clear();
	}
	//---------------------------------------------------------
	std::size_t TimerManager::getActiveCount() const
	{
		std::size_t count = 0;
		for(Timer const & timer : this->mTimers)
		{
			if(!timer.mDone)
			{
				++count;
			}
		}
		return count;
	}
	//---------------------------------------------------------
	//--- TimerHandle -----------------------------------------
	//---------------------------------------------------------
	bool TimerHandle::cancel()
	{
		TimerManager* manager = TimerManager::getSingletonPtr();
		return manager != 0 && this->mId != 0 && manager->cancel(this->mId);
	}
	//---------------------------------------------------------
	bool TimerHandle::isActive() const
	{
		TimerManager* manager = TimerManager::getSingletonPtr();
		return manager != 0 && this->mId != 0 && manager->isActive(this->mId);
	}
}
