/**************************************************************
	created:	2026/07/11 at 13:00
	filename: 	HapticTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit coverage for the haptic pattern mapping: the pure
	name -> Pattern -> (duration, amplitude) functions HapticManager and the
	Android backend share. The device buzz itself is not headlessly testable
	(the desktop no-op + API shape is covered by the player selfcheck).
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "engine_input/HapticManager.h"

using namespace Orkige;
using Pattern = HapticManager::Pattern;

TEST_CASE("haptic pattern names map to their patterns", "[unit][haptic]")
{
	CHECK(HapticManager::patternFromName("light") == Pattern::Light);
	CHECK(HapticManager::patternFromName("medium") == Pattern::Medium);
	CHECK(HapticManager::patternFromName("heavy") == Pattern::Heavy);
	CHECK(HapticManager::patternFromName("success") == Pattern::Success);
	CHECK(HapticManager::patternFromName("warning") == Pattern::Warning);
	CHECK(HapticManager::patternFromName("error") == Pattern::Error);
	CHECK(HapticManager::patternFromName("selection") == Pattern::Selection);
}

TEST_CASE("an unknown haptic pattern name defaults to Medium", "[unit][haptic]")
{
	CHECK(HapticManager::patternFromName("") == Pattern::Medium);
	CHECK(HapticManager::patternFromName("rumble") == Pattern::Medium);
	CHECK(HapticManager::patternFromName("LIGHT") == Pattern::Medium);	// case-sensitive
}

TEST_CASE("every haptic pattern yields stable, in-range drive params",
	"[unit][haptic]")
{
	const Pattern all[] = { Pattern::Light, Pattern::Medium, Pattern::Heavy,
		Pattern::Success, Pattern::Warning, Pattern::Error, Pattern::Selection };
	for (Pattern pattern : all)
	{
		const HapticManager::PatternParams params =
			HapticManager::paramsForPattern(pattern);
		CHECK(params.durationMs > 0);
		CHECK(params.amplitude > 0.0f);
		CHECK(params.amplitude <= 1.0f);
	}
	// intensity ordering the sensation implies: Light < Medium < Heavy
	CHECK(HapticManager::paramsForPattern(Pattern::Light).amplitude <
		HapticManager::paramsForPattern(Pattern::Medium).amplitude);
	CHECK(HapticManager::paramsForPattern(Pattern::Medium).amplitude <
		HapticManager::paramsForPattern(Pattern::Heavy).amplitude);
}
