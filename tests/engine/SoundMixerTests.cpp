/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	SoundMixerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless audio mixer unit tests: the effective-gain model
	(base x group, master, recompute across sources), the manifest
	Settings parsing (audio.master / audio.group.<name>) and the
	project round-trip. No OpenAL device is opened - the mixer STATE
	is fully queryable without one (SoundManager registers sources
	uninitialized when audio is down); the audible proof is the
	demo_sound integration run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_sound/SoundManager.h>
#include <core_project/Project.h>

#include <filesystem>

using Catch::Approx;

TEST_CASE("SoundSource effective gain is base times group volume", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	Orkige::SoundManager soundManager;	// deliberately NOT initialized

	Orkige::SoundSourcePtr sound =
		soundManager.createSound("gain.model", "no_such_file.wav");
	REQUIRE(sound);

	// defaults: full volume in the default "sfx" group
	CHECK(sound->getBaseGain() == Approx(1.0f));
	CHECK(sound->getGroup() == "sfx");
	CHECK(sound->getEffectiveGain() == Approx(1.0f));

	sound->setBaseGain(0.8f);
	CHECK(sound->getEffectiveGain() == Approx(0.8f));

	soundManager.setGroupVolume("sfx", 0.5f);
	CHECK(sound->getEffectiveGain() == Approx(0.4f));

	// volumes stay in 0..1 (AL_MAX_GAIN is pinned to 1 - larger values
	// would clamp silently, so the API clamps honestly)
	sound->setBaseGain(2.0f);
	CHECK(sound->getBaseGain() == Approx(1.0f));
	sound->setBaseGain(-1.0f);
	CHECK(sound->getBaseGain() == Approx(0.0f));

	soundManager.deinit();
}

TEST_CASE("SoundManager group volume recomputes every source of the group", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	Orkige::SoundManager soundManager;

	Orkige::SoundSourcePtr effectA =
		soundManager.createSound("mix.effectA", "a.wav");
	Orkige::SoundSourcePtr effectB =
		soundManager.createSound("mix.effectB", "b.wav");
	Orkige::SoundSourcePtr music =
		soundManager.createSound("mix.music", "m.wav");
	REQUIRE((effectA && effectB && music));

	// group moves are manager-mediated: the tag AND the group's current
	// volume arrive together
	soundManager.setGroupVolume("music", 0.25f);
	soundManager.setSoundGroup(music, "music");
	CHECK(music->getGroup() == "music");
	CHECK(music->getEffectiveGain() == Approx(0.25f));

	effectA->setBaseGain(0.5f);
	soundManager.setGroupVolume("sfx", 0.5f);
	CHECK(effectA->getEffectiveGain() == Approx(0.25f));	// 0.5 * 0.5
	CHECK(effectB->getEffectiveGain() == Approx(0.5f));		// 1.0 * 0.5
	CHECK(music->getEffectiveGain() == Approx(0.25f));		// untouched

	// a group nobody ever set reads as full volume
	CHECK(soundManager.getGroupVolume("narrator") == Approx(1.0f));
	// a source created AFTER the group volume was set picks it up
	Orkige::SoundSourcePtr lateEffect =
		soundManager.createSound("mix.late", "late.wav");
	CHECK(lateEffect->getEffectiveGain() == Approx(0.5f));

	// master is one knob on top (the AL listener), clamped like the rest
	soundManager.setMasterVolume(0.7f);
	CHECK(soundManager.getMasterVolume() == Approx(0.7f));
	soundManager.setMasterVolume(3.0f);
	CHECK(soundManager.getMasterVolume() == Approx(1.0f));

	soundManager.deinit();
}

TEST_CASE("SoundManager applies the audio.* manifest settings", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	Orkige::SoundManager soundManager;

	std::map<Orkige::String, Orkige::String> settings;
	settings["audio.master"] = "0.8";
	settings["audio.group.music"] = "0.5";
	settings["audio.group.sfx"] = "0.9";
	settings["audio.group."] = "0.1";			// empty group name - ignored
	settings["export.macos.bundleId"] = "com.example";	// unrelated - ignored
	settings["audio.group.loud"] = "7.5";		// clamped to 1

	soundManager.applySettings(settings);
	CHECK(soundManager.getMasterVolume() == Approx(0.8f));
	CHECK(soundManager.getGroupVolume("music") == Approx(0.5f));
	CHECK(soundManager.getGroupVolume("sfx") == Approx(0.9f));
	CHECK(soundManager.getGroupVolume("loud") == Approx(1.0f));

	soundManager.deinit();
}

TEST_CASE("Audio mixer settings round-trip through the project manifest", "[sound]")
{
	Orkige::EngineTestEnvironment::get();

	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "orkige_audio_manifest_test";
	std::error_code ignored;
	std::filesystem::remove_all(root, ignored);

	// write: the editor-side flow (setSetting + save)
	{
		Orkige::Project project;
		Orkige::String error;
		REQUIRE(Orkige::Project::create(root.string(), "AudioFixture",
			project, &error));
		project.setSetting("audio.master", "0.75");
		project.setSetting("audio.group.music", "0.25");
		REQUIRE(project.save(&error));
	}

	// read: the runtime-side flow (load + applySettings) - what the player
	// does on project load
	{
		Orkige::Project project;
		Orkige::String error;
		REQUIRE(project.load(root.string(), &error));
		Orkige::SoundManager soundManager;
		soundManager.applySettings(project.getSettings());
		CHECK(soundManager.getMasterVolume() == Approx(0.75f));
		CHECK(soundManager.getGroupVolume("music") == Approx(0.25f));
		soundManager.deinit();
	}

	std::filesystem::remove_all(root, ignored);
}
