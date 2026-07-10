/**************************************************************
	created:	2026/07/11 at 13:00
	filename: 	HapticManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __HapticManager_h__11_7_2026__13_00_00__
#define __HapticManager_h__11_7_2026__13_00_00__

#include "engine_module/EnginePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"

namespace Orkige
{
	//! @brief phone-body vibration ("rumble") for mobile games. SDL3's haptic
	//! subsystem is force-feedback for CONTROLLERS (joysticks/gamepads) only - it
	//! does NOT expose the device's own vibration motor, so the built-in buzz
	//! needs a platform shim, exactly like the engine's other .mm/JNI bridges.
	//! @remarks Backends chosen at COMPILE time by preprocessor (not by SDL):
	//! iOS drives the taptic engine through UIFeedbackGenerator (a thin ObjC
	//! bridge, HapticBridgeApple.mm); Android drives android.os.Vibrator /
	//! VibrationEffect through JNI (env + Activity from SDL). Every desktop is an
	//! honest no-op with isAvailable()==false (a phone with no controller
	//! enumerates zero SDL haptics, so the SDL rumble API is a silent no-op there
	//! too - v1 desktop does not chase an attached gamepad). Owned by the runtime
	//! that constructs it (the player) like the other input managers; the editor
	//! never makes one, so the Lua `haptics` table is an honest no-op in edit
	//! mode. Named patterns are preferred (iOS taptics ARE named generators; on
	//! Android they map to (duration, amplitude) pairs); play(strength, ms) is
	//! the generic escape hatch.
	class ORKIGE_ENGINE_DLL HapticManager : public Singleton<HapticManager>
	{
		DECL_OSINGLETON(HapticManager);
		//--- Types -------------------------------------------
	public:
		//! the named feedback patterns (the sensation, not any product's API):
		//! Light/Medium/Heavy are impacts; Success/Warning/Error are
		//! notifications; Selection is the light tick of a value change
		enum class Pattern
		{
			Light,
			Medium,
			Heavy,
			Success,
			Warning,
			Error,
			Selection
		};
		//! a pattern resolved to concrete drive parameters (the Android path and
		//! the unit test share these; iOS maps the Pattern straight to a generator)
		struct PatternParams
		{
			int		durationMs;		//!< buzz length in milliseconds
			float	amplitude;		//!< 0..1 strength
		};
		//--- Methods -----------------------------------------
	public:
		HapticManager();
		virtual ~HapticManager();

		//! @brief a generic buzz: strength 0..1, duration in milliseconds. No-op
		//! when disabled or on a platform without a vibrator.
		void play(float strength, int durationMs);
		//! play a named feedback pattern
		void playPattern(Pattern pattern);
		//! play a pattern by its lowercase name ("light".."selection"); an
		//! unknown name falls back to the Medium impact
		void playPatternByName(String const & name);

		//! is a real vibrator/taptic path present (true only on device builds)
		bool isAvailable() const;
		//! @brief global mute so a game can honour a "vibration off" setting
		//! (default on); a disabled manager no-ops every play call
		void setEnabled(bool enabled) { this->mEnabled = enabled; }
		bool isEnabled() const { return this->mEnabled; }

		//! @brief pure mapping name -> Pattern (unknown -> Medium); shared with
		//! the unit test and playPatternByName
		static Pattern patternFromName(String const & name);
		//! @brief pure mapping Pattern -> (duration, amplitude); shared with the
		//! Android backend and the unit test
		static PatternParams paramsForPattern(Pattern pattern);
	protected:
	private:
		bool mEnabled;	//!< game-side mute toggle
	};
}

#endif //__HapticManager_h__11_7_2026__13_00_00__
