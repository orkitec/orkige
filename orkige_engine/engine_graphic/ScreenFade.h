/********************************************************************
	created:	Friday 2026/07/11 at 12:00
	filename: 	ScreenFade.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ScreenFade_h__11_7_2026__12_00_00__
#define __ScreenFade_h__11_7_2026__12_00_00__

#include "engine_module/EnginePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/optr.h"

#include <functional>

namespace Orkige
{
	class DrawLayer2D;

	//! @brief the full-screen fade transition: a solid, animated-alpha overlay
	//! rectangle composited over the finished frame (and the HUD) on a reserved
	//! high z-layer, driven through the engine_render facade so it lives on BOTH
	//! render flavors. The canonical "fade out -> switch the scene while the
	//! screen is opaque -> fade in" wipe games use to hide a level teardown.
	//! @remarks Facade-only (a full-window DrawLayer2D of untextured vertex
	//! colours) - no renderer types leak in, so it renders identically on both
	//! flavors (the parity pixel test covers the overlay). Owned by the runtime
	//! that ticks it (the player), like LevelManager/TweenManager: the editor
	//! never constructs one, so the Lua `screen` table is an honest no-op in
	//! edit mode. The overlay is engine-owned, NOT a scene object, so it survives
	//! GameObjectManager::clear during a mid-fade scene switch. The alpha ramp is
	//! self-contained (an internal time accumulator + easing, NOT the game's
	//! TweenManager, which is cleared on scene teardown and would wipe a
	//! mid-transition fade).
	class ORKIGE_ENGINE_DLL ScreenFade : public Singleton<ScreenFade>
	{
		DECL_OSINGLETON(ScreenFade);
		//--- Types -------------------------------------------
	public:
		//! the fade state machine
		enum class Phase
		{
			Idle,	//!< no overlay drawn (alpha 0)
			Out,	//!< ramping alpha 0 -> 1 (screen going opaque)
			Hold,	//!< held opaque (alpha 1) - the safe point for a scene switch
			In		//!< ramping alpha 1 -> 0 (screen clearing)
		};
		//! the reserved z-layer: well above HUD/ImGui so the fade covers all
		static const int Z_FADE;
		//! frames the overlay stays fully opaque before an auto fade-in, so a
		//! transition's deferred scene load applies (at the loop's load pump,
		//! before the next fade tick) while the screen is covered
		static const int HOLD_FRAMES;
		//--- Methods -----------------------------------------
	public:
		ScreenFade();
		virtual ~ScreenFade();

		//! ramp the screen to opaque over `seconds`, then hold opaque (a later
		//! fadeIn clears it). Zero/negative is instantaneous.
		void fadeOut(float seconds);
		//! ramp the screen back to clear over `seconds`, then stop drawing
		void fadeIn(float seconds);
		//! @brief the combined wipe: fade out, run `atOpaque` at full opacity
		//! (the safe point to request a deferred scene switch), then auto fade
		//! in after a short opaque hold (so the switch applies while covered)
		void transition(float outSeconds, float inSeconds,
			std::function<void()> atOpaque);
		//! set the fade colour (straight RGB 0..1; default black)
		void setFadeColor(float r, float g, float b);
		//! games gate input while a fade runs
		bool isFading() const { return this->mPhase != Phase::Idle; }
		//! the current overlay alpha (0 clear .. 1 opaque)
		float getAlpha() const { return this->mAlpha; }
		Phase getPhase() const { return this->mPhase; }

		//! @brief tick the fade one frame - rebuilds the overlay quad at the
		//! current alpha (or hides it when idle). Ticked LAST in the player loop
		//! (a presentation overlay), after the deferred-load pump.
		void update(float deltaTime);

		//! @brief the pure alpha ramp (phase, elapsed, duration) -> alpha, factored
		//! out for the unit test: Out ramps 0->1, In ramps 1->0 (eased), Hold is
		//! 1, Idle is 0; a zero/negative duration is instantaneous.
		static float alphaAt(Phase phase, float elapsed, float duration);
	protected:
	private:
		//! create the overlay layer on first use (no-op without a render system)
		void ensureLayer();
		//! rebuild the full-window quad at the current alpha (or clear it)
		void rebuildQuad();

		optr<DrawLayer2D>		mLayer;			//!< the overlay layer (lazy)
		Phase					mPhase;			//!< current state
		float					mAlpha;			//!< current overlay alpha
		float					mElapsed;		//!< seconds into the current phase
		float					mOutSeconds;	//!< fade-out duration
		float					mInSeconds;		//!< fade-in duration
		int						mHoldRemaining;	//!< frames left in Hold before auto fade-in
		bool					mAutoIn;		//!< a transition auto-fades in after Hold
		float					mColorR;		//!< fade colour (straight RGB)
		float					mColorG;
		float					mColorB;
		std::function<void()>	mAtOpaque;		//!< fired once when Out reaches opacity
	};
}

#endif //__ScreenFade_h__11_7_2026__12_00_00__
