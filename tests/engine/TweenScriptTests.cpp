/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	TweenScriptTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests of the Lua tween/sound surface registered by
	ScriptComponent::ensureScriptApi (the `tween`, `sound` and extended
	`world` tables): a script starts tweens, a manually ticked
	TweenManager drives them - no renderer, no OpenAL device. The
	transform-typed helpers (tween.move/scale/rotate/fade) need live
	scene components and are covered by the player_tween_selfcheck
	integration run instead.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/ScriptComponent.h>
#include <engine_sound/SoundManager.h>
#include <core_game/GameObject.h>
#include <core_tween/TweenManager.h>

#include <filesystem>
#include <fstream>

using Catch::Approx;

namespace
{
	//! a throwaway script dir (mirrors ScriptComponentTests' helper)
	struct TempTweenScriptDir
	{
		std::filesystem::path root;
		explicit TempTweenScriptDir(std::string const & name)
			: root(std::filesystem::temp_directory_path() / name)
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
			std::filesystem::create_directories(this->root / "scripts");
		}
		~TempTweenScriptDir()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
		}
		std::string write(std::string const & name, std::string const & source)
		{
			const std::filesystem::path path = this->root / "scripts" / name;
			std::ofstream file(path);
			file << source;
			return path.string();
		}
	};
	//! read shared.tw.<key> as double (-999 when missing)
	double twNumber(char const * key)
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "tw", key}, -999.0);
	}
	//! skip helper (same convention as ScriptComponentTests)
	bool scriptingAvailable()
	{
		if (Orkige::ScriptRuntime::available())
		{
			return true;
		}
		SUCCEED("scripting disabled - tween script test skipped");
		return false;
	}
}

TEST_CASE("Lua tween.to drives values, completes once and cancels", "[tween][script]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	Orkige::TweenManager tweenManager;
	TempTweenScriptDir dir("orkige_tween_script_test");
	dir.write("tw.lua", R"lua(
		local cancelHandle = nil
		function init(self)
			shared.tw = { value = -1, completes = 0, cancelUpdates = 0,
				cancelled = 0 }
			tween.to(0.0, 10.0, 1.0, "linear",
				function(v) shared.tw.value = v end,
				function()
					shared.tw.completes = shared.tw.completes + 1
				end)
			cancelHandle = tween.to(0.0, 1.0, 100.0, "linear",
				function(v)
					shared.tw.cancelUpdates = shared.tw.cancelUpdates + 1
				end,
				function() shared.tw.completes = shared.tw.completes + 100 end)
			-- an unknown ease must not break the tween (logged, runs linear)
			tween.to(0.0, 1.0, 1.0, "no_such_ease",
				function(v) shared.tw.typoValue = v end)
		end
		function update(self, dt)
			if cancelHandle ~= nil and shared.tw.cancelUpdates > 0 then
				if cancelHandle:isActive() and cancelHandle:cancel() then
					shared.tw.cancelled = shared.tw.cancelled + 1
				end
				cancelHandle = nil
			end
		end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	optr<Orkige::GameObject> host =
		env.gameObjectManager.createGameObject("TweenHost").lock();
	REQUIRE(host);
	REQUIRE(host->addComponent<Orkige::ScriptComponent>());
	host->getComponentPtr<Orkige::ScriptComponent>()
		->setScriptFile("scripts/tw.lua");

	// the canonical order: scripts first, then tweens
	env.gameObjectManager.update(0.016f);	// init - tweens registered
	Orkige::ScriptComponent* script =
		host->getComponentPtr<Orkige::ScriptComponent>();
	REQUIRE_FALSE(script->hasScriptError());
	CHECK(tweenManager.getActiveCount() == 3);

	tweenManager.update(0.5f);				// t=0.5 -> value 5
	CHECK(twNumber("value") == Approx(5.0));
	CHECK(twNumber("cancelUpdates") == Approx(1.0));
	CHECK(twNumber("typoValue") == Approx(0.5));	// typo'd ease ran linear

	env.gameObjectManager.update(0.016f);	// the script cancels its handle
	CHECK(twNumber("cancelled") == Approx(1.0));

	tweenManager.update(0.6f);				// finishes the 1s tween exactly
	CHECK(twNumber("value") == Approx(10.0));
	CHECK(twNumber("completes") == Approx(1.0));

	tweenManager.update(1.0f);				// nothing left to fire
	CHECK(twNumber("completes") == Approx(1.0));
	CHECK(twNumber("cancelUpdates") == Approx(1.0));
	CHECK(tweenManager.getActiveCount() == 0);

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("Lua tween.volume and the sound table drive the mixer", "[tween][script][sound]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	Orkige::TweenManager tweenManager;
	Orkige::SoundManager soundManager;	// headless - never initialized
	TempTweenScriptDir dir("orkige_tween_volume_test");
	dir.write("duck.lua", R"lua(
		function init(self)
			shared.tw = {}
			sound.setMasterVolume(0.9)
			shared.tw.master = sound.getMasterVolume()
			sound.setGroupVolume("music", 1.0)
			-- THE DUCKING RECIPE (the documented two-liner): duck now,
			-- restore later - both are plain group-volume tweens
			tween.volume("music", 0.2, 0.5)
		end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	optr<Orkige::GameObject> host =
		env.gameObjectManager.createGameObject("Ducker").lock();
	REQUIRE(host);
	REQUIRE(host->addComponent<Orkige::ScriptComponent>());
	host->getComponentPtr<Orkige::ScriptComponent>()
		->setScriptFile("scripts/duck.lua");

	env.gameObjectManager.update(0.016f);
	REQUIRE_FALSE(host->getComponentPtr<Orkige::ScriptComponent>()
		->hasScriptError());
	CHECK(twNumber("master") == Approx(0.9));
	CHECK(soundManager.getMasterVolume() == Approx(0.9f));

	tweenManager.update(0.25f);	// halfway: 1.0 -> 0.6
	CHECK(soundManager.getGroupVolume("music") == Approx(0.6f));
	tweenManager.update(0.25f);	// landed: exactly 0.2
	CHECK(soundManager.getGroupVolume("music") == Approx(0.2f));
	CHECK(tweenManager.getActiveCount() == 0);

	env.gameObjectManager.clear();
	env.scriptRuntime.setScriptSearchRoot("");
	soundManager.deinit();
}

TEST_CASE("scene clear reaps script-started tweens (the teardown hook)", "[tween][script]")
{
	Orkige::EngineTestEnvironment & env = Orkige::EngineTestEnvironment::get();
	if (!scriptingAvailable())
	{
		return;
	}
	Orkige::TweenManager tweenManager;
	TempTweenScriptDir dir("orkige_tween_clear_test");
	dir.write("endless.lua", R"lua(
		function init(self)
			shared.tw = { updates = 0 }
			tween.to(0.0, 1.0, 1000.0, "linear",
				function(v) shared.tw.updates = shared.tw.updates + 1 end)
		end
	)lua");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.gameObjectManager.clear();

	optr<Orkige::GameObject> host =
		env.gameObjectManager.createGameObject("Endless").lock();
	REQUIRE(host);
	REQUIRE(host->addComponent<Orkige::ScriptComponent>());
	host->getComponentPtr<Orkige::ScriptComponent>()
		->setScriptFile("scripts/endless.lua");

	env.gameObjectManager.update(0.016f);
	tweenManager.update(0.016f);
	CHECK(twNumber("updates") == Approx(1.0));
	CHECK(tweenManager.getActiveCount() == 1);

	// the scene goes away (scene switch/stop): the hook clears the tween -
	// its closure can never fire against the dead world
	env.gameObjectManager.clear();
	CHECK(tweenManager.getActiveCount() == 0);
	tweenManager.update(1.0f);
	CHECK(twNumber("updates") == Approx(1.0));

	env.scriptRuntime.setScriptSearchRoot("");
}
