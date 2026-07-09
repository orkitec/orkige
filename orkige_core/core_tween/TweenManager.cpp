/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	TweenManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_tween/TweenManager.h"
#include "core_game/GameObjectManager.h"

#include <algorithm>

namespace Orkige
{
	IMPL_OSINGLETON(TweenManager)
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	TweenManager::TweenManager() : mNextId(1), mUpdating(false)
	{
	}
	//---------------------------------------------------------
	TweenManager::~TweenManager()
	{
	}
	//---------------------------------------------------------
	TweenManager::TweenId TweenManager::startTween(float const * fromValues,
		float const * toValues, int channelCount, float duration,
		Ease::Function ease, UpdateFunction const & onUpdate,
		CompleteFunction const & onComplete, float delay,
		String const & targetId)
	{
		oAssert(fromValues && toValues);
		oAssert(channelCount >= 1 && channelCount <= MAX_CHANNELS);
		channelCount = std::clamp(channelCount, 1, static_cast<int>(MAX_CHANNELS));

		Tween tween;
		tween.mId = this->mNextId++;
		tween.mTargetId = targetId;
		for(int channel = 0; channel < channelCount; ++channel)
		{
			tween.mFrom[channel] = fromValues[channel];
			tween.mTo[channel] = toValues[channel];
		}
		tween.mChannelCount = channelCount;
		tween.mDuration = duration;
		tween.mDelay = std::max(0.0f, delay);
		tween.mElapsed = 0.0f;
		tween.mEase = ease ? ease : &Ease::linear;
		tween.mOnUpdate = onUpdate;
		tween.mOnComplete = onComplete;
		tween.mDone = false;
		// safe while update() iterates: it walks by index over the size
		// captured at entry, so a tween started from a callback takes its
		// first step on the NEXT update
		this->mTweens.push_back(tween);
		return tween.mId;
	}
	//---------------------------------------------------------
	bool TweenManager::cancelTween(TweenId id)
	{
		for(Tween & tween : this->mTweens)
		{
			if(tween.mId == id && !tween.mDone)
			{
				// mark only - erasing here would break a running update()
				// pass; the sweep at the end of update() reclaims it
				tween.mDone = true;
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	bool TweenManager::isTweenActive(TweenId id) const
	{
		for(Tween const & tween : this->mTweens)
		{
			if(tween.mId == id)
			{
				return !tween.mDone;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	void TweenManager::update(float delta)
	{
		this->mUpdating = true;
		// index walk over the entry size: callbacks may push_back (grow +
		// reallocate) or cancel - never hold references across a callback
		const std::size_t tweenCount = this->mTweens.size();
		for(std::size_t i = 0; i < tweenCount; ++i)
		{
			if(this->mTweens[i].mDone)
			{
				continue;
			}
			// THE REAP: a targeted tween dies silently (no callbacks) with
			// its GameObject - closures keyed on the id can never fire
			// against a destroyed object
			if(!this->mTweens[i].mTargetId.empty() &&
				GameObjectManager::getSingletonPtr() != 0 &&
				!GameObjectManager::getSingleton().objectExists(
					this->mTweens[i].mTargetId))
			{
				this->mTweens[i].mDone = true;
				continue;
			}

			// the delay consumes time first; the step itself only sees what
			// is left of this frame's delta
			float step = delta;
			if(this->mTweens[i].mDelay > 0.0f)
			{
				const float consumed = std::min(this->mTweens[i].mDelay, step);
				this->mTweens[i].mDelay -= consumed;
				step -= consumed;
				if(this->mTweens[i].mDelay > 0.0f)
				{
					continue;
				}
			}

			this->mTweens[i].mElapsed += step;
			const float duration = this->mTweens[i].mDuration;
			const float t = duration > 0.0f
				? std::min(this->mTweens[i].mElapsed / duration, 1.0f)
				: 1.0f;
			const bool finished = t >= 1.0f;
			const float eased = this->mTweens[i].mEase(t);

			float values[MAX_CHANNELS];
			const int channelCount = this->mTweens[i].mChannelCount;
			for(int channel = 0; channel < channelCount; ++channel)
			{
				// the final step lands EXACTLY on the end value (float noise
				// and overshooting eases both end at 1 by invariant, but
				// exactness is part of the contract)
				values[channel] = finished ? this->mTweens[i].mTo[channel]
					: this->mTweens[i].mFrom[channel] +
						(this->mTweens[i].mTo[channel] -
							this->mTweens[i].mFrom[channel]) * eased;
			}

			// copies: the callbacks may mutate mTweens (start/cancel)
			const UpdateFunction onUpdate = this->mTweens[i].mOnUpdate;
			if(onUpdate && !onUpdate(values, channelCount))
			{
				// the callback asked to stop (script error etc.) - cancel,
				// no onComplete
				this->mTweens[i].mDone = true;
				continue;
			}
			if(finished && !this->mTweens[i].mDone)
			{
				// exactly once: marked done BEFORE the callback runs, so a
				// reentrant cancel/complete cannot refire it
				const CompleteFunction onComplete = this->mTweens[i].mOnComplete;
				this->mTweens[i].mDone = true;
				if(onComplete)
				{
					onComplete();
				}
			}
		}
		this->mUpdating = false;

		// sweep everything finished/cancelled/reaped this pass
		this->mTweens.erase(std::remove_if(this->mTweens.begin(),
			this->mTweens.end(),
			[](Tween const & tween) { return tween.mDone; }),
			this->mTweens.end());
	}
	//---------------------------------------------------------
	void TweenManager::clear()
	{
		if(this->mUpdating)
		{
			// scene teardown from inside a tween callback (a script switching
			// scenes): mark everything dead; the running update() pass skips
			// and sweeps them - tweens started AFTER this clear (for the new
			// scene) survive
			for(Tween & tween : this->mTweens)
			{
				tween.mDone = true;
			}
			return;
		}
		this->mTweens.clear();
	}
	//---------------------------------------------------------
	std::size_t TweenManager::getActiveCount() const
	{
		std::size_t count = 0;
		for(Tween const & tween : this->mTweens)
		{
			if(!tween.mDone)
			{
				++count;
			}
		}
		return count;
	}
	//---------------------------------------------------------
	//--- TweenHandle -----------------------------------------
	//---------------------------------------------------------
	bool TweenHandle::cancel()
	{
		TweenManager* manager = TweenManager::getSingletonPtr();
		return manager != 0 && this->mId != 0 && manager->cancelTween(this->mId);
	}
	//---------------------------------------------------------
	bool TweenHandle::isActive() const
	{
		TweenManager* manager = TweenManager::getSingletonPtr();
		return manager != 0 && this->mId != 0 && manager->isTweenActive(this->mId);
	}
}
