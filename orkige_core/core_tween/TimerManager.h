/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	TimerManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __TimerManager_h__12_7_2026__16_00_00__
#define __TimerManager_h__12_7_2026__16_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"

#include <functional>
#include <vector>

namespace Orkige
{
	/** \addtogroup Tween
	*  @{ */
	//! @brief the deferred-callback scheduler: run a function ONCE after a
	//! delay (after) or REPEATEDLY on a fixed period (every), cancellable by
	//! handle. A timer is the degenerate case of a tween (a zero-channel
	//! interpolation whose only observable is its completion callback), so it
	//! lives beside TweenManager and RIDES THE SAME PLAYER-LOOP TICK SLOT: the
	//! runtime that owns live behavior ticks it in the tween phase (the player
	//! loop's [3] block, right after TweenManager::update - never a new fence
	//! entry). The editor never creates one, so timers are dormant in edit mode
	//! for free, exactly like tweens.
	//!
	//! LIFETIME RULES (mirrors TweenManager + the ScriptEventBus owner model):
	//!  * a timer MAY name an OWNER token (the script sandbox that scheduled
	//!    it). cancelOwner() drops every timer of a retired sandbox WITHOUT
	//!    firing it - the SAME auto-cancel-on-retire the event-bus
	//!    subscriptions get, wired from ScriptInstance's destructor with the
	//!    same `this` token. So a script's timers die when its component is
	//!    removed / the scene is torn down / a hot-reload swaps the instance -
	//!    a stale timer can never fire into a dead sandbox.
	//!  * clear() drops every timer without firing; the ONE authoritative call
	//!    site is the scene teardown hook GameObjectManager::clear (beside the
	//!    TweenManager::clear call).
	//!  * callbacks should RE-FETCH their objects by id (like tween closures) -
	//!    a captured component pointer dangles when the object dies between
	//!    frames.
	class ORKIGE_CORE_DLL TimerManager : public Singleton<TimerManager>
	{
		DECL_OSINGLETON(TimerManager)
		//--- Types -------------------------------------------
	public:
		typedef unsigned long long TimerId;		//!< timer identity (0 = invalid)

		//! the deferred callback (fires with no arguments, like a tween's
		//! onComplete)
		typedef std::function<void()> TimerFunction;
	protected:
		//! one scheduled timer (internal)
		struct Timer
		{
			TimerId			mId;			//!< identity
			float			mInterval;		//!< seconds per fire (also the initial delay)
			float			mRemaining;		//!< seconds left until the next fire
			bool			mRepeat;		//!< every() (true) vs after() (false)
			void const *	mOwner;			//!< scheduling sandbox (NULL = unowned)
			TimerFunction	mFn;			//!< the callback
			bool			mDone;			//!< finished/cancelled - swept after update
		};
		//! catch-up cap: the most times ONE repeating timer may fire in a
		//! single update (a huge delta / tiny interval must not spin forever)
		static const int	MAX_CATCHUP = 100;
		//--- Variables ---------------------------------------
	protected:
		std::vector<Timer>	mTimers;		//!< scheduled timers (mDone swept on update)
		TimerId				mNextId;		//!< next timer identity
		bool				mUpdating;		//!< inside update() - guards reentrant clear()
		//--- Methods -----------------------------------------
	public:
		//! constructor
		TimerManager();
		//! destructor
		virtual ~TimerManager();

		//! @brief run fn ONCE after `seconds` (<= 0 fires on the next update).
		//! @param owner optional sandbox token the timer belongs to (@see
		//! cancelOwner); NULL leaves it unowned (cancelled only by handle/clear)
		//! @return the timer's id (for cancel/isActive)
		TimerId after(float seconds, TimerFunction const & fn,
			void const * owner = 0);
		//! @brief run fn REPEATEDLY every `seconds` until cancelled. A
		//! non-positive period cannot repeat, so it degrades to a single fire.
		//! @param owner @see after
		//! @return the timer's id
		TimerId every(float seconds, TimerFunction const & fn,
			void const * owner = 0);

		//! @brief cancel a scheduled timer - it never fires again.
		//! @return true when the timer was still scheduled
		bool cancel(TimerId id);
		//! is the timer with the given id still scheduled
		bool isActive(TimerId id) const;
		//! @brief cancel EVERY timer owned by `owner` (a retired sandbox); a
		//! NULL owner matches nothing (unowned timers are never bulk-cancelled).
		//! @return how many were cancelled.
		int cancelOwner(void const * owner);

		//! @brief advance every timer by delta seconds, firing the due ones and
		//! sweeping the finished ones; callbacks may schedule or cancel timers
		//! (a timer scheduled from a callback takes its first step on the NEXT
		//! update, like a tween).
		void update(float delta);

		//! @brief drop every timer WITHOUT firing. Called from THE scene
		//! teardown hook (GameObjectManager::clear) beside TweenManager::clear.
		void clear();

		//! number of scheduled timers (cancelled ones drop out on the next update)
		std::size_t getActiveCount() const;
	};
	//---------------------------------------------------------------
	//! @brief the script-facing handle to a scheduled timer - a tiny value type
	//! (safe to copy into Lua) that talks to the TimerManager singleton by id;
	//! all operations are harmless no-ops after the timer fired/was cancelled or
	//! when no TimerManager exists (the editor)
	struct ORKIGE_CORE_DLL TimerHandle
	{
		TimerManager::TimerId	mId = 0;	//!< the timer's id (0 = invalid)

		//! cancel the timer (@see TimerManager::cancel)
		//! @return true when it was still scheduled
		bool cancel();
		//! is the timer still scheduled
		bool isActive() const;
	};
	/** @} */
}

#endif //__TimerManager_h__12_7_2026__16_00_00__
