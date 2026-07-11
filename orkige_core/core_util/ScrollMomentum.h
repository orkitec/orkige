/********************************************************************
	created:	Saturday 2026/07/11 at 20:10
	filename: 	ScrollMomentum.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ScrollMomentum_h__11_7_2026__20_10_00__
#define __ScrollMomentum_h__11_7_2026__20_10_00__

//! @file ScrollMomentum.h
//! @brief the pure 1D flick-scroll physics behind a scroll viewport: inertial
//! deceleration after a flick, rubber-band overscroll while dragging past the
//! bounds, and a spring-back once released beyond them. Plain floats, no
//! renderer, no clock (the caller feeds delta seconds) so a unit test pins the
//! feel headlessly - the same contract the gui scroll widget drives.
//!
//! The offset is a signed scalar the caller clamps to [minOffset, maxOffset].
//! In the gui viewport the offset is the content shift (<= 0, so minOffset =
//! -maxScroll, maxOffset = 0), but the machine is agnostic to the sign meaning.

#include "core_module/OrkigePrerequisites.h"

namespace Orkige
{
	//! @brief a 1D scroll offset with flick inertia + rubber-band overscroll.
	//! Drive it: beginDrag / dragBy (deltas) / endDrag while a finger is down,
	//! wheelBy for a wheel notch (bypasses momentum), update(dt) every frame.
	class ORKIGE_CORE_DLL ScrollMomentum
	{
	public:
		ScrollMomentum();

		//! @brief the valid offset range (content stays pinned inside it). An
		//! empty range (min >= max, i.e. content fits) pins the offset to max.
		void setBounds(float minOffset, float maxOffset);
		//! @brief hard-set the offset (clamped to the bounds) and stop all motion
		void setOffset(float offset);
		inline float offset() const { return this->mOffset; }
		inline float velocity() const { return this->mVelocity; }

		//! @brief begin a drag: stops any inertia so the finger owns the content
		void beginDrag();
		//! @brief move the content by a raw pixel delta while dragging. Travel
		//! beyond a bound is rubber-banded (diminishing returns), so the content
		//! resists but follows the finger past the edge. A no-op unless dragging.
		void dragBy(float delta);
		//! @brief end the drag: the velocity measured over the last update ticks
		//! becomes the flick inertia (or a spring-back begins if released past a
		//! bound).
		void endDrag();
		inline bool isDragging() const { return this->mDragging; }

		//! @brief a wheel notch (raw offset delta). The wheel bypasses momentum:
		//! it hard-clamps to the bounds and kills any inertia (the platform
		//! convention - a wheel is discrete, not a flick).
		void wheelBy(float delta);

		//! @brief advance the physics by @p dt seconds and return the new offset.
		//! While dragging it just measures the flick velocity; released, it coasts
		//! with exponential deceleration and springs back from any overscroll.
		float update(float dt);

		//! @brief is the content still moving on its own (coasting or springing
		//! back)? False while at rest or being dragged.
		bool isMoving() const;
	private:
		float	mOffset;		//!< current offset
		float	mVelocity;		//!< current velocity (offset units / second)
		float	mMin;			//!< lower bound
		float	mMax;			//!< upper bound
		bool	mDragging;		//!< a finger owns the content
		float	mLastDragOffset;//!< offset at the previous update (velocity source)
	};
}

#endif //__ScrollMomentum_h__11_7_2026__20_10_00__
