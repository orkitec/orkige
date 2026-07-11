/********************************************************************
	created:	Friday 2026/07/11 at 12:00
	filename: 	ScreenShake.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ScreenShake_h__11_7_2026__12_00_00__
#define __ScreenShake_h__11_7_2026__12_00_00__

#include "engine_module/EnginePrerequisites.h"
#include "engine_render/RenderMath.h"
#include "core_util/Singleton.h"
#include "core_util/optr.h"

namespace Orkige
{
	class RenderNode;

	//! @brief camera-space screen shake: a decaying positional wobble the Lua
	//! `screen.shake` face drives, applied POST-transform to the active window
	//! camera's rig node each frame and removed again so it never fights the
	//! camera's own placement (a follow rig, a script, a TransformComponent) nor
	//! accumulates error - the node returns EXACTLY to its rest pose when the
	//! shake ends. Works in 2D and 3D (the offset is along the camera's local
	//! right/up axes).
	//! @remarks Ticked once per frame, LAST (a presentation effect, after the
	//! deferred-load pump), like ScreenFade. Each frame it recovers the node's
	//! base pose (the value the camera's own placement left this frame),
	//! re-applies the current wobble for rendering, and remembers what it added
	//! so the next frame can undo it - so whether the base is static (the
	//! player's default camera) or rewritten every frame (a CameraComponent rig)
	//! the shake composes cleanly on top without drift.
	//! @remarks Owned by the runtime that ticks it (the player), like
	//! LevelManager / TweenManager / ScreenFade: the editor never constructs one,
	//! so `screen.shake` is an honest no-op in edit mode.
	class ORKIGE_ENGINE_DLL ScreenShake : public Singleton<ScreenShake>
	{
		DECL_OSINGLETON(ScreenShake);
		//--- Variables ---------------------------------------
	public:
		//! default wobble frequency (Hz) when a caller does not pass one
		static const float DEFAULT_FREQUENCY;
		//--- Methods -----------------------------------------
	public:
		ScreenShake();
		virtual ~ScreenShake();

		//! @brief start (or refresh) a shake: `amplitude` world units of peak
		//! offset, decaying to zero over `duration` seconds, wobbling at
		//! `frequency` Hz. A new call while one runs takes the STRONGER amplitude
		//! and the LONGER remaining time (stacked hits feel additive, never
		//! weaker). Zero/negative amplitude or duration is a no-op.
		void shake(float amplitude, float duration, float frequency);
		//! @brief stop shaking immediately and restore the camera to its rest
		//! pose (used on teardown / an explicit script stop)
		void stop();
		//! is a shake currently running
		bool isShaking() const { return this->mActive; }

		//! @brief tick one frame: recover the camera node's base pose, advance
		//! the decay and re-apply the current wobble (or fully restore + go idle
		//! when the shake has run out). No-op without a render system / window
		//! camera. Ticked LAST in the player loop.
		void update(float deltaTime);

		//! @brief the pure wobble sample (factored out for the unit test): the
		//! local-space (x, y) offset at `elapsed` seconds into a shake of the
		//! given amplitude / duration / frequency. Decays LINEARLY to exactly
		//! (0, 0) at elapsed >= duration; magnitude never exceeds `amplitude`.
		static void sampleOffset(float amplitude, float duration, float frequency,
			float elapsed, float & outX, float & outY);
	protected:
	private:
		//! the window camera's rig node this frame, or NULL when none is showing
		optr<RenderNode> activeCameraNode() const;

		bool				mActive;		//!< a shake is running
		float				mAmplitude;		//!< peak offset (world units)
		float				mDuration;		//!< total shake time (seconds)
		float				mFrequency;		//!< wobble rate (Hz)
		float				mElapsed;		//!< seconds into the current shake
		//--- offset bookkeeping (recover-then-reapply, @see the class remarks) ---
		woptr<RenderNode>	mAppliedNode;	//!< node the last offset was applied to
		Vec3				mAppliedOffset;	//!< the world-space offset we added last frame
		Vec3				mAppliedResult;	//!< the node position we left last frame
		bool				mHasApplied;	//!< an offset is currently on the node
	};
}

#endif //__ScreenShake_h__11_7_2026__12_00_00__
