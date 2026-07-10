/**************************************************************
	created:	2026/07/11 at 13:00
	filename: 	HapticManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_input/HapticManager.h"

#include <algorithm>

#if defined(__ANDROID__)
#	include <SDL3/SDL.h>
#	include <SDL3/SDL_system.h>
#	include <jni.h>
#endif

namespace Orkige
{
	IMPL_OSINGLETON(HapticManager);

#if defined(ORKIGE_IPHONE)
	// implemented in HapticBridgeApple.mm (the taptic engine via
	// UIFeedbackGenerator); the manager stays plain C++
	extern "C" void orkige_haptic_play_ios(float strength, int durationMs);
	extern "C" void orkige_haptic_pattern_ios(int pattern);
#endif

#if defined(__ANDROID__)
	namespace
	{
		//! drive android.os.Vibrator once. Resolves the service + method through
		//! JNI on every call (cheap relative to the buzz itself) and clears any
		//! pending JNI exception so a failure never propagates into the game.
		void androidVibrate(int durationMs, float amplitude)
		{
			JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
			jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
			if (!env || !activity)
			{
				return;
			}
			jclass activityClass = env->GetObjectClass(activity);
			jmethodID getSystemService = env->GetMethodID(activityClass,
				"getSystemService",
				"(Ljava/lang/String;)Ljava/lang/Object;");
			jstring serviceName = env->NewStringUTF("vibrator");
			jobject vibrator = env->CallObjectMethod(activity, getSystemService,
				serviceName);
			if (vibrator)
			{
				jclass vibratorClass = env->GetObjectClass(vibrator);
				const int sdk = SDL_GetAndroidSDKVersion();
				const jlong ms = static_cast<jlong>(durationMs);
				if (sdk >= 26)
				{
					// VibrationEffect.createOneShot(ms, amplitude 1..255)
					const int amplitude255 = std::clamp(
						static_cast<int>(amplitude * 255.0f + 0.5f), 1, 255);
					jclass effectClass =
						env->FindClass("android/os/VibrationEffect");
					jmethodID createOneShot = effectClass
						? env->GetStaticMethodID(effectClass, "createOneShot",
							"(JI)Landroid/os/VibrationEffect;")
						: nullptr;
					jmethodID vibrate = env->GetMethodID(vibratorClass, "vibrate",
						"(Landroid/os/VibrationEffect;)V");
					if (effectClass && createOneShot && vibrate)
					{
						jobject effect = env->CallStaticObjectMethod(effectClass,
							createOneShot, ms,
							static_cast<jint>(amplitude255));
						env->CallVoidMethod(vibrator, vibrate, effect);
						env->DeleteLocalRef(effect);
					}
					if (effectClass)
					{
						env->DeleteLocalRef(effectClass);
					}
				}
				else
				{
					// legacy Vibrator.vibrate(ms)
					jmethodID vibrate = env->GetMethodID(vibratorClass, "vibrate",
						"(J)V");
					if (vibrate)
					{
						env->CallVoidMethod(vibrator, vibrate, ms);
					}
				}
				env->DeleteLocalRef(vibratorClass);
				env->DeleteLocalRef(vibrator);
			}
			env->DeleteLocalRef(serviceName);
			env->DeleteLocalRef(activityClass);
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
			}
		}
	}
#endif

	//---------------------------------------------------------
	HapticManager::HapticManager()
		: mEnabled(true)
	{
	}
	//---------------------------------------------------------
	HapticManager::~HapticManager()
	{
	}
	//---------------------------------------------------------
	HapticManager::Pattern HapticManager::patternFromName(String const & name)
	{
		if (name == "light")		return Pattern::Light;
		if (name == "medium")		return Pattern::Medium;
		if (name == "heavy")		return Pattern::Heavy;
		if (name == "success")		return Pattern::Success;
		if (name == "warning")		return Pattern::Warning;
		if (name == "error")		return Pattern::Error;
		if (name == "selection")	return Pattern::Selection;
		return Pattern::Medium;		// unknown -> a sensible default
	}
	//---------------------------------------------------------
	HapticManager::PatternParams HapticManager::paramsForPattern(Pattern pattern)
	{
		switch (pattern)
		{
		case Pattern::Light:		return { 10, 0.3f };
		case Pattern::Medium:		return { 20, 0.6f };
		case Pattern::Heavy:		return { 30, 1.0f };
		case Pattern::Success:		return { 40, 0.7f };
		case Pattern::Warning:		return { 60, 0.8f };
		case Pattern::Error:		return { 80, 1.0f };
		case Pattern::Selection:	return { 8, 0.4f };
		}
		return { 20, 0.6f };
	}
	//---------------------------------------------------------
	void HapticManager::play(float strength, int durationMs)
	{
		if (!this->mEnabled || durationMs <= 0)
		{
			return;
		}
		strength = std::clamp(strength, 0.0f, 1.0f);
#if defined(ORKIGE_IPHONE)
		orkige_haptic_play_ios(strength, durationMs);
#elif defined(__ANDROID__)
		androidVibrate(durationMs, strength);
#else
		// desktop: honest no-op (no phone vibrator)
		(void)strength;
		(void)durationMs;
#endif
	}
	//---------------------------------------------------------
	void HapticManager::playPattern(Pattern pattern)
	{
		if (!this->mEnabled)
		{
			return;
		}
#if defined(ORKIGE_IPHONE)
		// iOS taptics ARE named generators - hand the pattern straight over
		orkige_haptic_pattern_ios(static_cast<int>(pattern));
#elif defined(__ANDROID__)
		const PatternParams params = paramsForPattern(pattern);
		androidVibrate(params.durationMs, params.amplitude);
#else
		(void)pattern;
#endif
	}
	//---------------------------------------------------------
	void HapticManager::playPatternByName(String const & name)
	{
		this->playPattern(patternFromName(name));
	}
	//---------------------------------------------------------
	bool HapticManager::isAvailable() const
	{
#if defined(ORKIGE_IPHONE) || defined(__ANDROID__)
		return true;	// a device build has a real vibrator/taptic path
#else
		return false;	// desktop: no phone vibrator
#endif
	}
}
