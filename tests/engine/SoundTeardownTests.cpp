/********************************************************************
	created:	Wednesday 2026/07/15
	filename: 	SoundTeardownTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

// Headless SoundManager / MusicStream TEARDOWN regression tests. Unlike the
// gain-model tests (which keep the manager uninitialised, no AL objects), these
// force the OpenAL Soft "null" backend (ALSOFT_DRIVERS=null) so a real
// ALCdevice + ALCcontext and real AL sources/buffers exist to tear down - with
// no audio hardware, deterministic on a dev box (real audio present) AND on CI/
// the sanitizer container (no device). The contract under test: every teardown
// path frees its AL objects and its device/context cleanly (no leak, no
// use-after-free) - run under the container ASan/LSan for the memory verdict.
//
// SoundManager is a Singleton, so only ONE instance may live at a time - each
// case scopes its manager so it is destroyed before the next is built.

#include <catch2/catch_test_macros.hpp>

#include "EngineTestEnvironment.h"

#include <engine_sound/SoundManager.h>
#include <engine_sound/SoundSource.h>
#include <engine_sound/MusicStream.h>

#include <cstdlib>
#include <fstream>
#include <vector>

namespace
{
	//! force the null AL backend before the FIRST alcOpenDevice in this process
	//! (OpenAL Soft reads ALSOFT_DRIVERS at device open; no other unit test opens
	//! AL, so setting it per test-start is enough and order-safe).
	void forceNullAlBackend()
	{
#ifdef _WIN32
		_putenv_s("ALSOFT_DRIVERS", "null");
#else
		setenv("ALSOFT_DRIVERS", "null", 1);
#endif
	}

	//! the committed tiny OGG fixture (tests/assets/blip.ogg, path from CMake)
	std::vector<unsigned char> readOggFixture()
	{
		std::ifstream file(ORKIGE_MUSIC_TEST_OGG, std::ios::binary);
		return std::vector<unsigned char>(
			std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>());
	}

	//! a short silent 16-bit mono PCM buffer - createSoundFromPCM needs no file
	//! (headless has no resource system), so it is how a test gets a REAL AL
	//! source onto the manager
	std::vector<short> silentPcm(int frames)
	{
		return std::vector<short>(static_cast<size_t>(frames), 0);
	}

	Orkige::SoundSourcePtr addPcmSource(Orkige::SoundManager & manager,
		std::vector<short> const & pcm, char const * id)
	{
		return manager.createSoundFromPCM(id, pcm.data(),
			static_cast<int>(pcm.size() * sizeof(short)), 1, 16, 44100);
	}
}

// smoke: the null backend opens a device/context headless and deinit() releases
// it. If this ever fails to init, the file's teardown coverage is moot - so it
// REQUIREs (the null backend ships with OpenAL Soft on every platform).
TEST_CASE("SoundNullBackendInitDeinit", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	forceNullAlBackend();

	{
		Orkige::SoundManager manager;
		REQUIRE(manager.init());		// opens the null ALCdevice + context
		CHECK(manager.isinitialised());
		CHECK(manager.deinit());		// destroys the context, closes the device
	}
	// a SECOND manager inits cleanly after the first tore its context down -
	// proves deinit() left no context current the next open would fight
	{
		Orkige::SoundManager second;
		REQUIRE(second.init());
		CHECK(second.deinit());
	}
}

// (scope 1+2) the DESTRUCTOR must tear the AL objects down on its own: the
// player owns the SoundManager as a stack local and NEVER calls deinit(), so
// ~SoundManager is the only teardown at shutdown. Sources/streams self-free in
// their destructors and the AL runtime reclaims the device/context at exit -
// a leak on this path is the sanitizer's to catch; this asserts the path runs
// clean and a fresh manager still inits afterwards.
TEST_CASE("SoundManagerDestructorTearsDownAL", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	forceNullAlBackend();

	{
		Orkige::SoundManager manager;
		REQUIRE(manager.init());
		std::vector<short> pcm = silentPcm(2048);
		Orkige::SoundSourcePtr snd = addPcmSource(manager, pcm, "beep");
		REQUIRE(snd);
		CHECK(snd->isInitialized());	// a real AL source exists to leak/free
		manager.playSound("beep");
		manager.update(0.016f);
		// NO deinit() - fall off the scope; ~SoundManager must free everything
	}
	{
		Orkige::SoundManager after;
		REQUIRE(after.init());
		CHECK(after.deinit());
	}
}

// (scope 3) the mobile suspend->resume cycle: onInterruptBegin tears the AL
// context down (sources release their AL objects, music rings released),
// onInterruptEnd rebuilds it (sources re-init, music re-primed). A full cycle
// must leave the source functional again.
TEST_CASE("SoundInterruptSuspendResumeCycle", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	forceNullAlBackend();

	Orkige::SoundManager manager;
	REQUIRE(manager.init());

	std::vector<short> pcm = silentPcm(44100);	// ~1s
	Orkige::SoundSourcePtr snd = addPcmSource(manager, pcm, "beep");
	REQUIRE(snd);
	REQUIRE(snd->isInitialized());
	manager.playSound("beep");

	// interruption begin: the source releases its AL objects, the context dies
	manager.onInterruptBegin();
	CHECK_FALSE(snd->isInitialized());

	// interruption end: a fresh context + the source's AL objects come back
	manager.onInterruptEnd();
	CHECK(snd->isInitialized());

	// playback is functional again after the cycle
	CHECK(manager.playSound("beep"));
	manager.update(0.016f);

	CHECK(manager.deinit());
}

// (scope 3) shutdown DURING suspension: destroy the manager after
// onInterruptBegin (context already torn down) and never call onInterruptEnd -
// teardown must not double-free the destroyed context.
TEST_CASE("SoundShutdownDuringSuspension", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	forceNullAlBackend();

	{
		Orkige::SoundManager manager;
		REQUIRE(manager.init());
		std::vector<short> pcm = silentPcm(2048);
		addPcmSource(manager, pcm, "beep");
		manager.playSound("beep");
		manager.onInterruptBegin();		// context torn down, still "initialised"
		// destroy while suspended - must be clean
	}
	{
		Orkige::SoundManager after;
		REQUIRE(after.init());
		CHECK(after.deinit());
	}
}

// (scope 2+4) a MusicStream's AL ring dies cleanly, including MID-REFILL. The
// registry path (playMusic) needs the resource system, absent in the renderer-
// less unit env, so the stream is primed directly - the SAME primeAudio/
// teardownAudio the registry destruction runs. restore() is the public
// open->primed transition (the interrupt path uses it); update() cycles the
// ring so teardown hits a source with queued/decoded buffers.
TEST_CASE("MusicStreamTeardownMidRefill", "[sound]")
{
	Orkige::EngineTestEnvironment::get();
	forceNullAlBackend();

	Orkige::SoundManager manager;	// owns the current AL context the stream uses
	REQUIRE(manager.init());

	std::vector<unsigned char> ogg = readOggFixture();
	REQUIRE(ogg.size() > 4);

	{
		Orkige::MusicStream stream("bgm", "blip.ogg", true /*loop*/);
		REQUIRE(stream.openFromMemory(ogg));
		CHECK(stream.isOpen());
		CHECK_FALSE(stream.isPrimed());

		// prime the AL ring from the opened decoder (restore() is the public
		// not-primed+open -> primed path; it runs the same primeAudio the
		// resource open() does)
		stream.restore();
		CHECK(stream.isPrimed());
		CHECK(stream.play());

		// cycle the ring a few times (mid-refill): unqueue processed buffers,
		// requeue freshly decoded ones
		for (int i = 0; i < 8; ++i)
		{
			stream.update();
		}
		// destroy mid-refill (scope exit) -> teardownAudio stops the source,
		// unqueues and deletes its buffers + source, closes the decoder
	}

	// suspend()/restore() also free + rebuild the ring - exercise that teardown
	{
		Orkige::MusicStream stream("bgm2", "blip.ogg", false /*non-loop*/);
		REQUIRE(stream.openFromMemory(ogg));
		stream.restore();
		REQUIRE(stream.isPrimed());
		stream.suspend();				// releases the AL ring
		CHECK_FALSE(stream.isPrimed());
		stream.restore();				// rebuilds it
		CHECK(stream.isPrimed());
	}

	CHECK(manager.deinit());
}
