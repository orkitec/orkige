/**************************************************************
	created:	2026/07/10 at 21:30
	filename: 	MusicStreamTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless streamed-music unit tests: the OGG Vorbis decode seam, the
	ring-refill decode bookkeeping (loop wrap vs. non-loop end of stream)
	and the effective-gain model (base x "music" group). No OpenAL device
	is opened and no AL objects are created - the decoder and the volume
	math are fully exercised without one (SoundManager registers a track
	uninitialized when audio is down, exactly like a SoundSource); the
	audible AL path is proven by the demo_music integration run.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_sound/MusicStream.h>
#include <engine_sound/SoundManager.h>

#include <fstream>
#include <vector>

using Catch::Approx;

namespace
{
	// the committed tiny OGG fixture (tests/assets/blip.ogg), path injected by
	// CMake - read straight off disk so the decode seam is exercised without the
	// resource system or an audio device
	std::vector<unsigned char> readFixture()
	{
		std::ifstream file(ORKIGE_MUSIC_TEST_OGG, std::ios::binary);
		return std::vector<unsigned char>(
			std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>());
	}
}

TEST_CASE("MusicStreamDecodeTest", "[sound]")
{
	Orkige::EngineTestEnvironment::get();

	std::vector<unsigned char> bytes = readFixture();
	REQUIRE(bytes.size() > 4);
	// the OggS container magic - a sanity check that the fixture is a real
	// Ogg stream, not a placeholder
	CHECK((bytes[0] == 'O' && bytes[1] == 'g' && bytes[2] == 'g' &&
		bytes[3] == 'S'));

	Orkige::MusicDecode::Info info = {0, 0, 0.0f};
	void* decoder = Orkige::MusicDecode::open(
		&bytes[0], static_cast<int>(bytes.size()), &info);
	REQUIRE(decoder != nullptr);

	// the fixture is a short mono 44.1kHz tone
	CHECK(info.channels == 1);
	CHECK(info.sampleRate == 44100);
	CHECK(info.durationSeconds > 0.0f);
	CHECK(Orkige::MusicDecode::length(decoder) == Approx(info.durationSeconds));

	// N decode calls advance the playhead and end of stream is reported (0)
	const int chunkFrames = 4096;
	std::vector<short> pcm(chunkFrames * info.channels);
	long long total = 0;
	bool sawEnd = false;
	for (int i = 0; i < 64; ++i)
	{
		const int got = Orkige::MusicDecode::read(
			decoder, &pcm[0], chunkFrames, info.channels);
		if (got <= 0)
		{
			sawEnd = true;
			break;
		}
		total += got;
	}
	CHECK(sawEnd);
	// ~0.3s at 44.1kHz
	CHECK(total > 10000);
	CHECK(total < 20000);

	// seek-to-start rewinds: a fresh read after seeking yields data again
	Orkige::MusicDecode::seekStart(decoder);
	const int afterSeek = Orkige::MusicDecode::read(
		decoder, &pcm[0], chunkFrames, info.channels);
	CHECK(afterSeek > 0);

	Orkige::MusicDecode::close(decoder);
}

TEST_CASE("MusicRingRefillTest", "[sound]")
{
	Orkige::EngineTestEnvironment::get();

	std::vector<unsigned char> bytes = readFixture();
	REQUIRE(bytes.size() > 4);

	// a LOOPING stream: decodeChunk always fills the whole buffer because it
	// seeks to the start on end of stream (the fixture is shorter than one
	// ring buffer, so a single fill already has to wrap)
	{
		Orkige::MusicStream stream("loop.track", "blip.ogg", true);
		REQUIRE(stream.openFromMemory(bytes));
		REQUIRE(stream.isOpen());
		const int frames = stream.bufferFrames();
		REQUIRE(frames > 0);
		std::vector<short> pcm(frames * 2);	// generous (mono fixture)
		for (int i = 0; i < 4; ++i)
		{
			const int got = stream.decodeChunk(&pcm[0], frames);
			// a looping stream keeps the buffer full and never marks the end
			CHECK(got == frames);
			CHECK_FALSE(stream.reachedEnd());
		}
	}

	// a NON-looping stream drains: the last fill is short and the stream marks
	// its end; the next fill yields nothing
	{
		Orkige::MusicStream stream("once.track", "blip.ogg", false);
		REQUIRE(stream.openFromMemory(bytes));
		const int frames = stream.bufferFrames();
		std::vector<short> pcm(frames * 2);
		const int first = stream.decodeChunk(&pcm[0], frames);
		// the fixture is shorter than one ring buffer, so the very first fill
		// already reaches the end
		CHECK(first > 0);
		CHECK(first < frames);
		CHECK(stream.reachedEnd());
		const int second = stream.decodeChunk(&pcm[0], frames);
		CHECK(second == 0);
	}
}

TEST_CASE("MusicGroupVolumeTest", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	Orkige::SoundManager soundManager;	// deliberately NOT initialized

	// playMusic on an uninitialized manager REGISTERS the track (silent, no AL
	// objects, no decode) so the gain model stays queryable - just like
	// createSound does for a SoundSource
	REQUIRE_FALSE(soundManager.playMusic("bgm", "blip.ogg", true));
	Orkige::MusicStreamPtr track = soundManager.getMusic("bgm");
	REQUIRE(track);

	// defaults: full volume in the "music" group
	CHECK(track->getGroup() == "music");
	CHECK(track->getBaseGain() == Approx(1.0f));
	CHECK(track->getEffectiveGain() == Approx(1.0f));

	// the per-track own volume multiplies the group volume
	soundManager.setMusicVolume("bgm", 0.8f);
	CHECK(track->getBaseGain() == Approx(0.8f));
	CHECK(track->getEffectiveGain() == Approx(0.8f));

	// the "music" group volume reaches the stream (so sound.setGroupVolume /
	// tween.volume on "music" controls streamed volume too)
	soundManager.setGroupVolume("music", 0.5f);
	CHECK(track->getEffectiveGain() == Approx(0.4f));	// 0.8 * 0.5

	// volumes clamp to 0..1 like the rest of the mixer
	soundManager.setMusicVolume("bgm", 2.0f);
	CHECK(track->getBaseGain() == Approx(1.0f));

	// the runtime-state snapshot mirrors the track
	std::vector<Orkige::SoundManager::MusicTrackInfo> snapshot =
		soundManager.snapshotMusic();
	REQUIRE(snapshot.size() == 1);
	CHECK(snapshot[0].id == "bgm");
	CHECK(snapshot[0].file == "blip.ogg");
	CHECK(snapshot[0].loop);
	CHECK(snapshot[0].groupVolume == Approx(0.5f));
	CHECK(snapshot[0].effectiveGain == Approx(0.5f));	// base clamped to 1

	// stopMusic frees the track
	CHECK(soundManager.stopMusic("bgm"));
	CHECK_FALSE(soundManager.getMusic("bgm"));
	CHECK(soundManager.snapshotMusic().empty());

	soundManager.deinit();
}
