/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	TweenTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tween unit tests: the EaseLibrary invariants (f(0)=0, f(1)=1,
	monotonicity where the family promises it, byName resolution) and the
	TweenManager semantics (value-at-t, exact landing, onComplete exactly
	once, cancel, delay, target reaping, clear - including the scene
	teardown hook in GameObjectManager::clear). The rendered end-to-end
	proof is the player_tween_selfcheck integration run
	(tests/projects/tween).
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CoreTestEnvironment.h"

#include <core_tween/EaseLibrary.h>
#include <core_tween/TweenManager.h>
#include <core_game/GameObject.h>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace
{
	struct NamedEase
	{
		char const *			mName;
		Orkige::Ease::Function	mFunction;
		bool					mMonotonic;		//!< promised non-decreasing on [0,1]
		bool					mBounded;		//!< promised to stay in [0,1]
	};
	//! every ease the library ships; back/elastic overshoot by design (not
	//! monotonic, not bounded), bounce stays bounded but oscillates
	std::vector<NamedEase> allEases()
	{
		using namespace Orkige::Ease;
		return {
			{ "linear",			&linear,		true,	true },
			{ "quadIn",			&quadIn,		true,	true },
			{ "quadOut",		&quadOut,		true,	true },
			{ "quadInOut",		&quadInOut,		true,	true },
			{ "cubicIn",		&cubicIn,		true,	true },
			{ "cubicOut",		&cubicOut,		true,	true },
			{ "cubicInOut",		&cubicInOut,	true,	true },
			{ "quartIn",		&quartIn,		true,	true },
			{ "quartOut",		&quartOut,		true,	true },
			{ "quartInOut",		&quartInOut,	true,	true },
			{ "quintIn",		&quintIn,		true,	true },
			{ "quintOut",		&quintOut,		true,	true },
			{ "quintInOut",		&quintInOut,	true,	true },
			{ "sineIn",			&sineIn,		true,	true },
			{ "sineOut",		&sineOut,		true,	true },
			{ "sineInOut",		&sineInOut,		true,	true },
			{ "expoIn",			&expoIn,		true,	true },
			{ "expoOut",		&expoOut,		true,	true },
			{ "expoInOut",		&expoInOut,		true,	true },
			{ "circIn",			&circIn,		true,	true },
			{ "circOut",		&circOut,		true,	true },
			{ "circInOut",		&circInOut,		true,	true },
			{ "backIn",			&backIn,		false,	false },
			{ "backOut",		&backOut,		false,	false },
			{ "backInOut",		&backInOut,		false,	false },
			{ "elasticIn",		&elasticIn,		false,	false },
			{ "elasticOut",		&elasticOut,	false,	false },
			{ "elasticInOut",	&elasticInOut,	false,	false },
			{ "bounceIn",		&bounceIn,		false,	true },
			{ "bounceOut",		&bounceOut,		false,	true },
			{ "bounceInOut",	&bounceInOut,	false,	true },
		};
	}
}

TEST_CASE("EaseLibrary curves hold the f(0)=0 / f(1)=1 invariants", "[tween]")
{
	for (NamedEase const & ease : allEases())
	{
		INFO("ease " << ease.mName);
		CHECK(ease.mFunction(0.0f) == Approx(0.0f).margin(1e-4));
		CHECK(ease.mFunction(1.0f) == Approx(1.0f).margin(1e-4));
	}
}

TEST_CASE("EaseLibrary curves are monotonic/bounded where promised", "[tween]")
{
	const int steps = 200;
	for (NamedEase const & ease : allEases())
	{
		INFO("ease " << ease.mName);
		float previous = ease.mFunction(0.0f);
		for (int i = 1; i <= steps; ++i)
		{
			const float t = static_cast<float>(i) / steps;
			const float value = ease.mFunction(t);
			if (ease.mMonotonic)
			{
				CHECK(value >= previous - 1e-5f);
			}
			if (ease.mBounded)
			{
				CHECK(value >= -1e-4f);
				CHECK(value <= 1.0f + 1e-4f);
			}
			// every curve must at least stay finite and sane
			CHECK(std::isfinite(value));
			previous = value;
		}
	}
}

TEST_CASE("EaseLibrary spot values match the curve families", "[tween]")
{
	using namespace Orkige::Ease;
	CHECK(linear(0.25f) == Approx(0.25f));
	CHECK(quadIn(0.5f) == Approx(0.25f));
	CHECK(quadOut(0.5f) == Approx(0.75f));
	CHECK(cubicIn(0.5f) == Approx(0.125f));
	CHECK(quadInOut(0.5f) == Approx(0.5f));
	CHECK(sineInOut(0.5f) == Approx(0.5f));
	// backIn dips below zero on the way (the overshoot signature)
	CHECK(backIn(0.2f) < 0.0f);
	// elasticOut overshoots above one
	bool overshoots = false;
	for (int i = 1; i < 100; ++i)
	{
		if (elasticOut(i / 100.0f) > 1.0f)
		{
			overshoots = true;
		}
	}
	CHECK(overshoots);
}

TEST_CASE("EaseLibrary byName resolves every curve and rejects unknowns", "[tween]")
{
	for (NamedEase const & ease : allEases())
	{
		INFO("ease " << ease.mName);
		CHECK(Orkige::Ease::byName(ease.mName) == ease.mFunction);
	}
	CHECK(Orkige::Ease::byName("") == nullptr);
	CHECK(Orkige::Ease::byName("QuadOut") == nullptr);
	CHECK(Orkige::Ease::byName("wobble") == nullptr);
}

namespace
{
	//! a 1-channel recorder callback: collects every applied value
	struct ValueRecorder
	{
		std::vector<float> mValues;
		Orkige::TweenManager::UpdateFunction callback()
		{
			return [this](float const * values, int count) -> bool
			{
				REQUIRE(count >= 1);
				this->mValues.push_back(values[0]);
				return true;
			};
		}
	};
}

TEST_CASE("TweenManager interpolates values through the ease over time", "[tween]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TweenManager manager;

	ValueRecorder recorder;
	const float from = 2.0f;
	const float to = 6.0f;
	int completions = 0;
	manager.startTween(&from, &to, 1, 1.0f, &Orkige::Ease::linear,
		recorder.callback(), [&completions]() { ++completions; });
	CHECK(manager.getActiveCount() == 1);

	manager.update(0.25f);	// t=0.25 -> 3.0
	manager.update(0.25f);	// t=0.5  -> 4.0
	REQUIRE(recorder.mValues.size() == 2);
	CHECK(recorder.mValues[0] == Approx(3.0f));
	CHECK(recorder.mValues[1] == Approx(4.0f));
	CHECK(completions == 0);

	manager.update(10.0f);	// overshoots the end: lands EXACTLY on `to`
	REQUIRE(recorder.mValues.size() == 3);
	CHECK(recorder.mValues[2] == 6.0f);
	CHECK(completions == 1);
	CHECK(manager.getActiveCount() == 0);

	// no further callbacks after completion
	manager.update(1.0f);
	CHECK(recorder.mValues.size() == 3);
	CHECK(completions == 1);
}

TEST_CASE("TweenManager tweens multiple channels (vector/colour shape)", "[tween]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TweenManager manager;

	const float from[3] = { 0.0f, 10.0f, -2.0f };
	const float to[3] = { 1.0f, 20.0f, 2.0f };
	float seen[3] = { 0.0f, 0.0f, 0.0f };
	manager.startTween(from, to, 3, 2.0f, &Orkige::Ease::linear,
		[&seen](float const * values, int count) -> bool
		{
			REQUIRE(count == 3);
			seen[0] = values[0];
			seen[1] = values[1];
			seen[2] = values[2];
			return true;
		});
	manager.update(1.0f);	// t = 0.5
	CHECK(seen[0] == Approx(0.5f));
	CHECK(seen[1] == Approx(15.0f));
	CHECK(seen[2] == Approx(0.0f));
}

TEST_CASE("TweenManager onComplete fires exactly once", "[tween]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TweenManager manager;

	const float from = 0.0f;
	const float to = 1.0f;
	int completions = 0;
	manager.startTween(&from, &to, 1, 0.1f, &Orkige::Ease::quadOut,
		Orkige::TweenManager::UpdateFunction(),
		[&completions]() { ++completions; });
	for (int i = 0; i < 20; ++i)
	{
		manager.update(0.05f);
	}
	CHECK(completions == 1);

	// a zero/negative duration completes on its first tick - once
	completions = 0;
	manager.startTween(&from, &to, 1, 0.0f, nullptr,
		Orkige::TweenManager::UpdateFunction(),
		[&completions]() { ++completions; });
	manager.update(0.016f);
	manager.update(0.016f);
	CHECK(completions == 1);
}

TEST_CASE("TweenManager cancel stops updates and never completes", "[tween]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TweenManager manager;

	ValueRecorder recorder;
	const float from = 0.0f;
	const float to = 1.0f;
	int completions = 0;
	const Orkige::TweenManager::TweenId id = manager.startTween(&from, &to,
		1, 10.0f, &Orkige::Ease::linear, recorder.callback(),
		[&completions]() { ++completions; });

	manager.update(0.5f);
	CHECK(recorder.mValues.size() == 1);
	CHECK(manager.isTweenActive(id));

	CHECK(manager.cancelTween(id));
	CHECK_FALSE(manager.isTweenActive(id));
	CHECK(manager.getActiveCount() == 0);
	CHECK_FALSE(manager.cancelTween(id));	// idempotent - already gone

	manager.update(20.0f);
	CHECK(recorder.mValues.size() == 1);	// no update after cancel
	CHECK(completions == 0);				// and no completion, ever

	// the value-typed handle drives the same path (the Lua-facing surface)
	Orkige::TweenHandle handle;
	handle.mId = manager.startTween(&from, &to, 1, 10.0f, nullptr,
		Orkige::TweenManager::UpdateFunction());
	CHECK(handle.isActive());
	CHECK(handle.cancel());
	CHECK_FALSE(handle.isActive());
	CHECK_FALSE(handle.cancel());
}

TEST_CASE("TweenManager delay defers the first step, duration untouched", "[tween]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TweenManager manager;

	ValueRecorder recorder;
	const float from = 0.0f;
	const float to = 1.0f;
	int completions = 0;
	manager.startTween(&from, &to, 1, 1.0f, &Orkige::Ease::linear,
		recorder.callback(), [&completions]() { ++completions; },
		/*delay*/ 0.5f);

	manager.update(0.25f);	// still delayed
	CHECK(recorder.mValues.empty());
	manager.update(0.5f);	// consumes the remaining 0.25 delay + 0.25 step
	REQUIRE(recorder.mValues.size() == 1);
	CHECK(recorder.mValues[0] == Approx(0.25f));
	manager.update(0.75f);	// finishes exactly (0.25 + 0.75 = 1.0)
	REQUIRE(recorder.mValues.size() == 2);
	CHECK(recorder.mValues[1] == 1.0f);
	CHECK(completions == 1);
}

TEST_CASE("TweenManager onUpdate returning false cancels without completing", "[tween]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TweenManager manager;

	const float from = 0.0f;
	const float to = 1.0f;
	int updates = 0;
	int completions = 0;
	manager.startTween(&from, &to, 1, 1.0f, &Orkige::Ease::linear,
		[&updates](float const *, int) -> bool
		{
			++updates;
			return updates < 3;		// stop on the third step
		},
		[&completions]() { ++completions; });
	for (int i = 0; i < 10; ++i)
	{
		manager.update(0.05f);
	}
	CHECK(updates == 3);
	CHECK(completions == 0);
	CHECK(manager.getActiveCount() == 0);
}

TEST_CASE("TweenManager reaps tweens whose target GameObject died", "[tween]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	env.gameObjectManager.clear();
	Orkige::TweenManager manager;

	REQUIRE(env.gameObjectManager.createGameObject("Victim").lock());

	ValueRecorder recorder;
	const float from = 0.0f;
	const float to = 1.0f;
	int completions = 0;
	manager.startTween(&from, &to, 1, 10.0f, &Orkige::Ease::linear,
		recorder.callback(), [&completions]() { ++completions; },
		0.0f, /*targetId*/ "Victim");

	manager.update(0.1f);
	CHECK(recorder.mValues.size() == 1);

	// the target dies between frames: the tween is reaped SILENTLY (no
	// update against a dead object, no onComplete)
	// note: delGameObject bypasses clear(), so this exercises the per-tick
	// reap rather than the teardown hook
	REQUIRE(env.gameObjectManager.delGameObject("Victim"));
	manager.update(0.1f);
	CHECK(recorder.mValues.size() == 1);
	CHECK(completions == 0);
	CHECK(manager.getActiveCount() == 0);
}

TEST_CASE("GameObjectManager::clear is the tween teardown hook", "[tween]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	env.gameObjectManager.clear();
	Orkige::TweenManager manager;

	const float from = 0.0f;
	const float to = 1.0f;
	int completions = 0;
	manager.startTween(&from, &to, 1, 10.0f, &Orkige::Ease::linear,
		Orkige::TweenManager::UpdateFunction(),
		[&completions]() { ++completions; });
	manager.startTween(&from, &to, 1, 10.0f, &Orkige::Ease::linear,
		Orkige::TweenManager::UpdateFunction(),
		Orkige::TweenManager::CompleteFunction(), 0.0f, "SomeTarget");
	CHECK(manager.getActiveCount() == 2);

	// the scene teardown point (SceneSerializer::loadScene, the editor's
	// document paths and the tests all funnel through clear()) drops every
	// running tween without firing callbacks
	env.gameObjectManager.clear();
	CHECK(manager.getActiveCount() == 0);
	CHECK(completions == 0);

	manager.update(1.0f);
	CHECK(completions == 0);
}

TEST_CASE("TweenManager callbacks may start tweens (chaining recipe)", "[tween]")
{
	Orkige::CoreTestEnvironment::get();
	Orkige::TweenManager manager;

	const float from = 0.0f;
	const float to = 1.0f;
	int secondCompleted = 0;
	manager.startTween(&from, &to, 1, 0.1f, &Orkige::Ease::linear,
		Orkige::TweenManager::UpdateFunction(),
		[&manager, &secondCompleted]()
		{
			// v1 sequencing: chain by starting the follow-up tween from
			// onComplete (a dedicated chaining API is a v2 item)
			const float f = 0.0f;
			const float t = 1.0f;
			manager.startTween(&f, &t, 1, 0.1f, &Orkige::Ease::linear,
				Orkige::TweenManager::UpdateFunction(),
				[&secondCompleted]() { ++secondCompleted; });
		});
	for (int i = 0; i < 10; ++i)
	{
		manager.update(0.05f);
	}
	CHECK(secondCompleted == 1);
	CHECK(manager.getActiveCount() == 0);
}
