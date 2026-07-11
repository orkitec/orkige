/********************************************************************
	created:	Saturday 2026/07/11 at 18:00
	filename: 	ToastQueue.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ToastQueue_h__11_7_2026__18_00_00__
#define __ToastQueue_h__11_7_2026__18_00_00__

//! @file ToastQueue.h
//! @brief backend-neutral timed-notification queue. Toasts are shown one at a
//! time: the front toast is active for its lifetime, then it is popped and the
//! next one surfaces. Pure timing math (no renderer, no tween) - the gui toast
//! builder drives the visible label + fade from this, and a unit test shares
//! the exact schedule.

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <deque>

namespace Orkige
{
	//! @brief a FIFO of timed toasts, one active at a time. update() advances
	//! the active toast and pops it when its lifetime elapses; a caller reads
	//! hasActive()/activeText()/activeAlpha() each frame to drive the widget.
	class ORKIGE_CORE_DLL ToastQueue
	{
	public:
		//! @brief queue a toast for `lifetime` seconds. `fade` is the fade-in
		//! and fade-out ramp length (clamped so the two ramps never overlap the
		//! hold). Both default sensibly for a notification.
		void enqueue(String const & text, float lifetime = 2.5f, float fade = 0.3f);

		//! @brief advance the active toast by `dt` seconds, popping expired ones
		//! (a large dt can retire several). Carries the remainder into the next
		//! toast so a long frame does not lose queued time.
		void update(float dt);

		//! @brief drop every toast immediately (scene teardown)
		void clear();

		bool hasActive() const { return !this->mItems.empty(); }
		//! the active toast's text, or "" when none is showing
		String activeText() const;
		//! @brief the active toast's opacity 0..1: ramps up over `fade`, holds at
		//! 1, ramps down over the last `fade`. 0 when nothing is active.
		float activeAlpha() const;
		//! how many toasts are waiting behind the active one
		std::size_t pending() const;
		//! total queued toasts (active + waiting)
		std::size_t size() const { return this->mItems.size(); }
	private:
		struct Item
		{
			String	text;
			float	lifetime = 0.0f;
			float	fade = 0.0f;
			float	elapsed = 0.0f;
		};
		std::deque<Item> mItems;	//!< front = active
	};
}

#endif //__ToastQueue_h__11_7_2026__18_00_00__
