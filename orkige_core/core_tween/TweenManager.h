/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	TweenManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __TweenManager_h__9_7_2026__10_00_00__
#define __TweenManager_h__9_7_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"
#include "core_tween/EaseLibrary.h"

#include <functional>
#include <vector>

namespace Orkige
{
	/** \addtogroup Tween
	*  @{ */
	//! @brief the tween system: interpolates 1..4 float channels from a start
	//! to an end value over a duration through an easing curve
	//! (core_tween/EaseLibrary.h) and hands every step to a caller-provided
	//! apply callback (no property reflection exists in the engine - values
	//! are ALWAYS applied through typed setters in the callback).
	//! @remarks Renderer-independent by construction: vector/colour tweens are
	//! just multi-channel float tweens, the engine-layer binding splits and
	//! recombines the typed values. The manager is ticked EXPLICITLY by the
	//! runtime that owns live behavior (the PLAYER LOOP TICK ORDER block in
	//! tools/player/main.cpp: scripts -> tweens -> physics); the editor never
	//! creates/ticks one, so tweens are dormant in edit mode for free.
	//!
	//! LIFETIME RULES (the cross-cutting risk of the tween design):
	//!  * a tween MAY name a target GameObject id - it is then reaped
	//!    silently (no callbacks) as soon as that object no longer exists.
	//!  * TweenManager::clear() drops every tween without callbacks; the ONE
	//!    authoritative call site is the scene teardown hook in
	//!    GameObjectManager::clear() (core_game/GameObjectManager.cpp).
	//!  * callbacks should RE-FETCH their objects (world API / manager
	//!    lookup by id) instead of capturing component pointers - captured
	//!    raw pointers dangle when the object dies between frames. The
	//!    target-id reap makes the typed helpers safe; hand-written closures
	//!    follow the re-fetch style.
	class ORKIGE_CORE_DLL TweenManager : public Singleton<TweenManager>
	{
		DECL_OSINGLETON(TweenManager)
		//--- Types -------------------------------------------
	public:
		typedef unsigned long long TweenId;			//!< tween identity (0 = invalid)

		static const int MAX_CHANNELS = 4;			//!< float channels per tween (Vec2/Vec3/Color fit)

		//! @brief per-step apply callback: the eased channel values for this
		//! frame (count = the channelCount the tween was started with)
		//! @return true to keep running, false to cancel the tween (no
		//! onComplete fires then - the error-stop channel of script bindings)
		typedef std::function<bool(float const * values, int count)> UpdateFunction;
		//! fires exactly once when the tween reached its end (never on
		//! cancel/reap/clear)
		typedef std::function<void()> CompleteFunction;
	protected:
		//! one running tween (internal)
		struct Tween
		{
			TweenId				mId;						//!< identity
			String				mTargetId;					//!< reap key ("" = untargeted)
			float				mFrom[MAX_CHANNELS];		//!< start values
			float				mTo[MAX_CHANNELS];			//!< end values
			int					mChannelCount;				//!< used channels (1..MAX_CHANNELS)
			float				mDuration;					//!< seconds (<= 0 completes on the first tick)
			float				mDelay;						//!< seconds left before the first step
			float				mElapsed;					//!< seconds into the interpolation
			Ease::Function		mEase;						//!< easing curve (NULL = linear)
			UpdateFunction		mOnUpdate;					//!< apply callback
			CompleteFunction	mOnComplete;				//!< completion callback
			bool				mDone;						//!< finished/cancelled - swept after update
		};
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		std::vector<Tween>	mTweens;			//!< running tweens (mDone ones swept on update)
		TweenId				mNextId;			//!< next tween identity
		bool				mUpdating;			//!< inside update() - guards reentrant clear()
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		TweenManager();
		//! destructor
		virtual ~TweenManager();

		//! @brief start a tween over channelCount float channels
		//! @param fromValues start values (channelCount entries)
		//! @param toValues end values (channelCount entries)
		//! @param channelCount 1..MAX_CHANNELS
		//! @param duration seconds; <= 0 applies the end values and completes
		//! on the next update tick
		//! @param ease easing curve (NULL = linear)
		//! @param onUpdate the apply callback (@see UpdateFunction)
		//! @param onComplete optional completion callback (fires exactly once)
		//! @param delay optional seconds to wait before the first step
		//! @param targetId optional GameObject id this tween drives - the
		//! tween is reaped silently when that object no longer exists
		//! @return the tween's id (for cancelTween/isTweenActive)
		TweenId startTween(float const * fromValues, float const * toValues,
			int channelCount, float duration, Ease::Function ease,
			UpdateFunction const & onUpdate,
			CompleteFunction const & onComplete = CompleteFunction(),
			float delay = 0.0f, String const & targetId = String());

		//! @brief cancel a running tween - neither onUpdate nor onComplete
		//! fire afterwards
		//! @return true when the tween was running
		bool cancelTween(TweenId id);
		//! is the tween with the given id still running
		bool isTweenActive(TweenId id) const;

		//! @brief advance every tween by delta seconds and sweep the finished
		//! ones; callbacks may start or cancel tweens (tweens started from a
		//! callback take their first step on the NEXT update)
		void update(float delta);

		//! @brief drop every tween WITHOUT firing callbacks. Called from THE
		//! scene teardown hook (GameObjectManager::clear) - never invent a
		//! second teardown path, extend that hook instead.
		void clear();

		//! number of running tweens (cancelled ones drop out immediately)
		std::size_t getActiveCount() const;
	protected:
	private:
	};
	//---------------------------------------------------------------
	//! @brief the script-facing handle to a started tween - a tiny value type
	//! (safe to copy into Lua) that talks to the TweenManager singleton by id;
	//! all operations are harmless no-ops after the tween ended or when no
	//! TweenManager exists (the editor)
	struct ORKIGE_CORE_DLL TweenHandle
	{
		TweenManager::TweenId	mId = 0;	//!< the tween's id (0 = invalid)

		//! cancel the tween (@see TweenManager::cancelTween)
		//! @return true when it was still running
		bool cancel();
		//! is the tween still running
		bool isActive() const;
	};
	/** @} */
}

#endif //__TweenManager_h__9_7_2026__10_00_00__
