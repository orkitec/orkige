/**************************************************************
	created:	2026/07/11 at 12:00
	filename: 	TimeControl.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __TimeControl_h__11_7_2026__12_00_00__
#define __TimeControl_h__11_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"

#include <algorithm>

namespace Orkige
{
	//! @brief the game time scale the Lua `world.setTimeScale` face reads: a
	//! single multiplier the player loop applies to the delta it feeds the
	//! GAMEPLAY tick (scripts, tweens, physics), leaving the debug protocol,
	//! rendering and the editor on real time. 1.0 = normal, 0.5 = slow motion,
	//! 2.0 = fast, 0.0 = hitstop (the world freezes but keeps RENDERING).
	//! @remarks Held here rather than on a subsystem because it scales SEVERAL
	//! (scripts + tweens + physics) at once, at the loop's clock. ONLY a runtime
	//! that reads it applies it - the player creates one (tools/player/main.cpp),
	//! the editor never does, so `world.setTimeScale` is an honest no-op in edit
	//! mode (like LevelManager / TweenManager) and the editor stays real-time.
	//! Never persisted, never serialized - a scale is a transient gameplay knob
	//! that a fresh scene/level starts at 1.0.
	class ORKIGE_CORE_DLL TimeControl : public Singleton<TimeControl>
	{
		DECL_OSINGLETON(TimeControl);
		//--- Variables ---------------------------------------
	private:
		float mTimeScale;	//!< the gameplay-delta multiplier (>= 0)
		//--- Methods -----------------------------------------
	public:
		TimeControl() : mTimeScale(1.0f) {}
		virtual ~TimeControl() {}

		//! @brief the current gameplay time scale (>= 0)
		float getTimeScale() const { return mTimeScale; }
		//! @brief set the gameplay time scale; clamped to >= 0 (a negative scale
		//! would run gameplay backwards, which the loop cannot honour)
		void setTimeScale(float scale) { mTimeScale = std::max(scale, 0.0f); }
	protected:
	private:
	};
}

#endif //__TimeControl_h__11_7_2026__12_00_00__
