/********************************************************************
	created:	Tuesday 2026/08/04 at 12:00
	filename: 	AudioBackendTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

// The audio backend seam (engine_sound/AudioBackend.h) on the SILENT device.
// The silent device consumes samples in real time, so everything here is the
// REAL behaviour a speaker would give, just inaudibly - which is what makes
// the streaming contract testable headlessly on a dev box AND on CI.
//
// What these cases lock down:
//   * the device opens, closes and reopens cleanly, and voices only exist
//     while it is open;
//   * a voice reports back what the MIXER reads behind it (frames, rate) -
//     the accepted-samples proof SoundSource::queryBufferBytes rides on;
//   * "3D for mono, 2D for stereo" - the class contract SoundSource states
//     but nothing used to check;
//   * the streaming ring: room accounting, main-thread writes, the frames the
//     mixer really consumed, the drained -> END transition that stops a
//     non-looping track, and the reset that un-drains it.

#include <catch2/catch_test_macros.hpp>

#include "EngineTestEnvironment.h"

#include <engine_sound/AudioBackend.h>
#include <engine_sound/MusicStream.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <vector>

namespace
{
	using namespace Orkige;

	//! open the silent device for one case (the tests own the graph directly -
	//! no SoundManager, no environment poking, so nothing else can be the
	//! reason a case passes)
	bool openSilentDevice()
	{
		return AudioBackend::engineInit(true /*silent*/);
	}

	AudioBackend::PcmFormat pcmFormat(int channels)
	{
		AudioBackend::PcmFormat format;
		format.channels = channels;
		format.bitsPerSample = 16;
		format.sampleRate = 44100;
		return format;
	}

	//! a silent 16-bit block of the given frame count
	std::vector<short> silentPcm(int frames, int channels)
	{
		return std::vector<short>(
			static_cast<std::size_t>(frames) * channels, 0);
	}

	//! @brief poll until the predicate holds or the budget runs out.
	//! @remarks the device thread runs at WALL CLOCK, so a stream test must be
	//! condition-driven; a fixed sleep would be a flake waiting to happen.
	template <typename Predicate>
	bool waitUntil(Predicate ready, int budgetMs = 5000)
	{
		for (int waited = 0; waited < budgetMs; waited += 10)
		{
			if (ready())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return ready();
	}

	//! the committed tiny OGG fixture (tests/assets/blip.ogg, path from CMake)
	std::vector<unsigned char> readOggFixture()
	{
		std::ifstream file(ORKIGE_MUSIC_TEST_OGG, std::ios::binary);
		return std::vector<unsigned char>(
			std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>());
	}
}

TEST_CASE("AudioBackendDeviceLifecycle", "[sound]")
{
	Orkige::EngineTestEnvironment::get();

	CHECK_FALSE(AudioBackend::engineReady());
	REQUIRE(openSilentDevice());
	CHECK(AudioBackend::engineReady());

	// a second open replaces the first rather than stacking a device on it
	REQUIRE(openSilentDevice());
	CHECK(AudioBackend::engineReady());

	AudioBackend::engineUninit();
	CHECK_FALSE(AudioBackend::engineReady());
	// closing what is already closed is a no-op, never a double free
	AudioBackend::engineUninit();
	CHECK_FALSE(AudioBackend::engineReady());

	// no device, no voice - the honest NULL every caller checks for
	std::vector<short> pcm = silentPcm(1024, 1);
	CHECK(AudioBackend::voiceCreate(pcm.data(),
		static_cast<int>(pcm.size() * sizeof(short)), pcmFormat(1), false)
		== NULL);
	CHECK(AudioBackend::streamCreate(1, 44100, 4096) == NULL);
}

TEST_CASE("AudioBackendVoiceReportsWhatTheMixerReads", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	REQUIRE(openSilentDevice());

	const int frames = 8820;					// 0.2s at 44.1kHz
	std::vector<short> pcm = silentPcm(frames, 1);
	AudioBackend::Voice voice = AudioBackend::voiceCreate(pcm.data(),
		static_cast<int>(pcm.size() * sizeof(short)), pcmFormat(1),
		false /*loop*/);
	REQUIRE(voice != NULL);

	// read back through the data-source layer the mixer itself reads: this is
	// the proof the samples were accepted, not a copy of what we passed in
	CHECK(AudioBackend::voiceFrameCount(voice) == frames);
	CHECK(AudioBackend::voiceSampleRate(voice) == 44100);

	// the samples were COPIED: the caller's block is free to go
	pcm.clear();
	pcm.shrink_to_fit();

	AudioBackend::voiceSetPitch(voice, 1.25f);
	CHECK(AudioBackend::voiceGetPitch(voice) > 1.24f);
	CHECK(AudioBackend::voiceGetPitch(voice) < 1.26f);

	CHECK_FALSE(AudioBackend::voiceIsPlaying(voice));
	CHECK(AudioBackend::voiceStart(voice));
	CHECK(AudioBackend::voiceIsPlaying(voice));

	// the playhead advances on the device thread, and a non-looping voice
	// finishes by itself once it runs off the end of its samples
	CHECK(waitUntil([&]() { return !AudioBackend::voiceIsPlaying(voice); }));

	// a stop rewinds, so the next start plays from the first sample again
	AudioBackend::voiceStop(voice);
	CHECK(AudioBackend::voiceGetCursorSeconds(voice) == 0.f);
	CHECK(AudioBackend::voiceStart(voice));
	CHECK(AudioBackend::voiceIsPlaying(voice));

	AudioBackend::voiceDestroy(voice);
	AudioBackend::engineUninit();
}

TEST_CASE("AudioBackendMonoIsPlacedStereoIsNot", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	REQUIRE(openSilentDevice());

	std::vector<short> mono = silentPcm(1024, 1);
	AudioBackend::Voice monoVoice = AudioBackend::voiceCreate(mono.data(),
		static_cast<int>(mono.size() * sizeof(short)), pcmFormat(1), false);
	REQUIRE(monoVoice != NULL);

	std::vector<short> stereo = silentPcm(1024, 2);
	AudioBackend::Voice stereoVoice = AudioBackend::voiceCreate(stereo.data(),
		static_cast<int>(stereo.size() * sizeof(short)), pcmFormat(2), false);
	REQUIRE(stereoVoice != NULL);

	// the class contract: a mono clip is a point in the world, a stereo one is
	// a finished mix and plays as authored
	CHECK(AudioBackend::voiceIsSpatialized(monoVoice));
	CHECK_FALSE(AudioBackend::voiceIsSpatialized(stereoVoice));
	CHECK(AudioBackend::voiceFrameCount(stereoVoice) == 1024);

	AudioBackend::voiceDestroy(monoVoice);
	AudioBackend::voiceDestroy(stereoVoice);
	AudioBackend::engineUninit();
}

TEST_CASE("AudioBackendStreamRingAccounting", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	REQUIRE(openSilentDevice());

	const int ringFrames = 8820;				// 0.2s at 44.1kHz
	AudioBackend::Stream stream =
		AudioBackend::streamCreate(1, 44100, ringFrames);
	REQUIRE(stream != NULL);

	// a fresh ring is empty and has taken nothing
	CHECK(AudioBackend::streamWritableFrames(stream) == ringFrames);
	CHECK(AudioBackend::streamConsumedFrames(stream) == 0);

	// the main thread fills it; a write is capped by the room, never by luck
	std::vector<short> chunk = silentPcm(ringFrames, 1);
	CHECK(AudioBackend::streamWrite(stream, chunk.data(), ringFrames)
		== ringFrames);
	CHECK(AudioBackend::streamWritableFrames(stream) == 0);
	CHECK(AudioBackend::streamWrite(stream, chunk.data(), 64) == 0);

	// the mixer drains it, and ONLY frames it really consumed move the playhead
	CHECK(AudioBackend::streamStart(stream));
	CHECK(waitUntil([&]()
	{
		return AudioBackend::streamConsumedFrames(stream) > 0;
	}));
	CHECK(waitUntil([&]()
	{
		return AudioBackend::streamWritableFrames(stream) > 0;
	}));
	// an underrun is a gap, never the end: nothing is refilling this ring and
	// the stream was never marked drained, so it keeps playing silence
	CHECK(AudioBackend::streamIsPlaying(stream));

	AudioBackend::streamDestroy(stream);
	AudioBackend::engineUninit();
}

TEST_CASE("AudioBackendStreamEndsOnceDrained", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	REQUIRE(openSilentDevice());

	AudioBackend::Stream stream = AudioBackend::streamCreate(1, 44100, 8820);
	REQUIRE(stream != NULL);

	std::vector<short> chunk = silentPcm(4410, 1);	// 0.1s
	REQUIRE(AudioBackend::streamWrite(stream, chunk.data(), 4410) == 4410);
	// the producer says no more will ever arrive
	AudioBackend::streamMarkDrained(stream);
	CHECK(AudioBackend::streamStart(stream));

	// so the voice ENDS once the ring empties - this is what stops a
	// non-looping music track without anyone polling for it
	CHECK(waitUntil([&]() { return !AudioBackend::streamIsPlaying(stream); }));
	CHECK(AudioBackend::streamConsumedFrames(stream) == 4410);

	// a reset is the rewind: nothing queued, nothing consumed, not drained
	AudioBackend::streamReset(stream);
	CHECK(AudioBackend::streamConsumedFrames(stream) == 0);
	CHECK(AudioBackend::streamWritableFrames(stream) == 8820);
	REQUIRE(AudioBackend::streamWrite(stream, chunk.data(), 4410) == 4410);
	CHECK(AudioBackend::streamStart(stream));
	CHECK(AudioBackend::streamIsPlaying(stream));

	AudioBackend::streamDestroy(stream);
	AudioBackend::engineUninit();
}

TEST_CASE("MusicStreamNonLoopingTrackStopsItself", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	REQUIRE(openSilentDevice());

	std::vector<unsigned char> ogg = readOggFixture();
	REQUIRE(ogg.size() > 4);

	{
		// the fixture is a ~0.3s tone, shorter than one ring, so the first
		// fill already reaches the end of the decoder
		Orkige::MusicStream track("once", "blip.ogg", false /*loop*/);
		REQUIRE(track.openFromMemory(ogg));
		track.restore();					// the open -> primed transition
		REQUIRE(track.isPrimed());
		REQUIRE(track.play());
		CHECK(track.getPlayPosition() >= 0.f);

		// it ends on its own once the mixer has played what was decoded
		CHECK(waitUntil([&]() { return !track.isPlaying(); }));
		CHECK(track.reachedEnd());
		CHECK(track.getPlayPosition() > 0.f);

		// stop rewinds and refills, so the track is playable again
		CHECK(track.stop());
		CHECK(track.getPlayPosition() == 0.f);
		CHECK(track.play());
		CHECK(track.isPlaying());
	}

	{
		// a LOOPING track never reaches an end: the decoder wraps, so the ring
		// is always refillable and the voice keeps going
		Orkige::MusicStream track("loop", "blip.ogg", true /*loop*/);
		REQUIRE(track.openFromMemory(ogg));
		track.restore();
		REQUIRE(track.isPrimed());
		REQUIRE(track.play());
		CHECK(waitUntil([&]()
		{
			return track.getPlayPosition() > 0.f;
		}));
		track.update();						// the main-thread refill
		CHECK(track.isPlaying());
		CHECK_FALSE(track.reachedEnd());
	}

	AudioBackend::engineUninit();
}
