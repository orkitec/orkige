/********************************************************************
	created:	Saturday 2026/07/11 at 20:10
	filename: 	ScrollMomentum.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_util/ScrollMomentum.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		//! velocity fraction retained per second of coasting (exponential decay).
		//! Low value = a flick dies quickly; the standard "settles in ~1s" feel.
		const float DECELERATION = 0.0025f;
		//! coast stops once the speed falls below this (offset units / second)
		const float MIN_COAST_SPEED = 8.0f;
		//! fraction of the overscroll closed per second while springing back
		//! (exponential approach to the bound)
		const float SPRINGBACK_RATE = 0.0000002f;
		//! overscroll is considered closed once within this many pixels of the bound
		const float SPRINGBACK_EPSILON = 0.5f;
		//! rubber-band asymptote: overscroll travel approaches this many pixels no
		//! matter how far the finger drags past the edge (diminishing resistance)
		const float RUBBER_LIMIT = 180.0f;

		//! @brief the rubber-band mapping of a raw past-bound distance to the
		//! resisted travel: monotonic, always < the raw distance, and its ratio to
		//! the raw distance shrinks with distance (stiffer the further you pull).
		//! d * L / (d + L) asymptotes to L.
		float rubberband(float rawDistance)
		{
			if(rawDistance <= 0.0f)
			{
				return 0.0f;
			}
			return rawDistance * RUBBER_LIMIT / (rawDistance + RUBBER_LIMIT);
		}
	}
	//---------------------------------------------------------
	ScrollMomentum::ScrollMomentum()
		: mOffset(0.0f), mVelocity(0.0f), mMin(0.0f), mMax(0.0f),
		mDragging(false), mLastDragOffset(0.0f)
	{
	}
	//---------------------------------------------------------
	void ScrollMomentum::setBounds(float minOffset, float maxOffset)
	{
		// an empty range (content fits the viewport) pins to the upper bound
		if(minOffset > maxOffset)
		{
			minOffset = maxOffset;
		}
		this->mMin = minOffset;
		this->mMax = maxOffset;
		// keep the offset legal when not actively overscrolling by drag
		if(!this->mDragging)
		{
			this->mOffset = std::max(this->mMin,
				std::min(this->mMax, this->mOffset));
		}
	}
	//---------------------------------------------------------
	void ScrollMomentum::setOffset(float offset)
	{
		this->mOffset = std::max(this->mMin, std::min(this->mMax, offset));
		this->mVelocity = 0.0f;
		this->mLastDragOffset = this->mOffset;
	}
	//---------------------------------------------------------
	void ScrollMomentum::beginDrag()
	{
		this->mDragging = true;
		this->mVelocity = 0.0f;
		this->mLastDragOffset = this->mOffset;
	}
	//---------------------------------------------------------
	void ScrollMomentum::dragBy(float delta)
	{
		if(!this->mDragging)
		{
			return;
		}
		const float target = this->mOffset + delta;
		if(target > this->mMax)
		{
			this->mOffset = this->mMax + rubberband(target - this->mMax);
		}
		else if(target < this->mMin)
		{
			this->mOffset = this->mMin - rubberband(this->mMin - target);
		}
		else
		{
			this->mOffset = target;
		}
	}
	//---------------------------------------------------------
	void ScrollMomentum::endDrag()
	{
		// the flick velocity was measured over the drag's update ticks; leave it
		// in place so the coast/spring-back picks it up on the next update
		this->mDragging = false;
	}
	//---------------------------------------------------------
	void ScrollMomentum::wheelBy(float delta)
	{
		// discrete input bypasses momentum: snap within bounds, kill inertia
		this->mDragging = false;
		this->setOffset(this->mOffset + delta);
	}
	//---------------------------------------------------------
	float ScrollMomentum::update(float dt)
	{
		if(dt <= 0.0f)
		{
			return this->mOffset;
		}
		if(this->mDragging)
		{
			// measure the flick velocity from the drag travel this tick (a simple
			// blend keeps a single jittery frame from dominating the flick)
			const float measured = (this->mOffset - this->mLastDragOffset) / dt;
			this->mVelocity = this->mVelocity * 0.2f + measured * 0.8f;
			this->mLastDragOffset = this->mOffset;
			return this->mOffset;
		}

		// released past a bound: spring back to it, whatever the velocity
		if(this->mOffset > this->mMax || this->mOffset < this->mMin)
		{
			const float bound = this->mOffset > this->mMax ? this->mMax : this->mMin;
			const float t = 1.0f - std::pow(SPRINGBACK_RATE, dt);
			this->mOffset += (bound - this->mOffset) * t;
			this->mVelocity = 0.0f;
			if(std::fabs(this->mOffset - bound) < SPRINGBACK_EPSILON)
			{
				this->mOffset = bound;
			}
			return this->mOffset;
		}

		// coasting: exponential deceleration, clamp at a bound (which then hands
		// over to spring-back only if a flick carried it past)
		if(std::fabs(this->mVelocity) > MIN_COAST_SPEED)
		{
			this->mOffset += this->mVelocity * dt;
			this->mVelocity *= std::pow(DECELERATION, dt);
			if(this->mOffset > this->mMax)
			{
				this->mOffset = this->mMax;
				this->mVelocity = 0.0f;
			}
			else if(this->mOffset < this->mMin)
			{
				this->mOffset = this->mMin;
				this->mVelocity = 0.0f;
			}
		}
		else
		{
			this->mVelocity = 0.0f;
		}
		return this->mOffset;
	}
	//---------------------------------------------------------
	bool ScrollMomentum::isMoving() const
	{
		if(this->mDragging)
		{
			return false;
		}
		if(this->mOffset > this->mMax || this->mOffset < this->mMin)
		{
			return true;
		}
		return std::fabs(this->mVelocity) > MIN_COAST_SPEED;
	}
}
