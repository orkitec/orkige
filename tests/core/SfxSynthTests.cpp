/**************************************************************
	created:	2026/07/29 at 10:30
	filename: 	SfxSynthTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the procedural sound-effect synthesizer (the standard
	retro-sfx parameter model): every waveform's shape invariants, the
	envelope's stages and its punch, what each parameter audibly changes, the
	fixed render rate, byte-level determinism from the seed, the
	no-wrap/no-NaN guarantees, the clamp-and-name verdicts for out-of-range
	weights and the archetype generators' sanity. Pure - no audio device, no
	engine.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/SfxSynth.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace Orkige;
using Catch::Approx;

namespace
{
	//! the largest absolute sample in a rendered buffer, in 0..1
	//! (the synthesizer's full scale is 32000, not 32767)
	float peakOf(SfxSynth::Pcm const & pcm)
	{
		int peak = 0;
		for(std::size_t i = 0; i < pcm.samples.size(); ++i)
		{
			peak = std::max(peak, std::abs(static_cast<int>(pcm.samples[i])));
		}
		return static_cast<float>(peak) / 32000.0f;
	}
	//! how many DISTINCT sample values a window of the buffer holds
	std::size_t distinctValues(SfxSynth::Pcm const & pcm, std::size_t from,
		std::size_t to)
	{
		if(pcm.samples.size() < to)
		{
			return 0u;
		}
		std::vector<std::int16_t> window(pcm.samples.begin() + from,
			pcm.samples.begin() + to);
		std::sort(window.begin(), window.end());
		window.erase(std::unique(window.begin(), window.end()), window.end());
		return window.size();
	}
	//! @brief the share of a window's samples sitting close to silence - the
	//! shape probe that survives the standard's always-on tiny high-pass (it
	//! adds a slow DC drift, so a square's two levels are two BANDS, but a
	//! square still spends almost no time near zero while a sine spends much
	//! of its cycle there)
	float shareNearZero(SfxSynth::Pcm const & pcm, std::size_t from,
		std::size_t to)
	{
		if(pcm.samples.size() < to || to <= from)
		{
			return -1.0f;
		}
		int peak = 0;
		for(std::size_t i = from; i < to; ++i)
		{
			peak = std::max(peak, std::abs(static_cast<int>(pcm.samples[i])));
		}
		std::size_t quiet = 0;
		for(std::size_t i = from; i < to; ++i)
		{
			if(std::abs(static_cast<int>(pcm.samples[i])) * 5 < peak * 2)
			{
				++quiet;			// below two fifths of the window's peak
			}
		}
		return static_cast<float>(quiet) / static_cast<float>(to - from);
	}
	//! a long, plain, filter-free tone of one wave - the shape probe
	SfxDesc steadyTone(SfxWave::Type wave)
	{
		SfxDesc desc;
		desc.waveType = wave;
		desc.baseFreq = 0.3f;
		desc.attack = 0.0f;
		desc.sustain = 0.6f;		// a long, flat body to inspect
		desc.decay = 0.1f;
		desc.punch = 0.0f;
		desc.lpfFreq = 1.0f;		// the "off" end: no low-pass
		desc.hpfFreq = 0.0f;
		// deliberately below the standard's own default: at 0.5 a square
		// already reaches full scale (the zero-offset delay tap doubles
		// every sample), which would hide any headroom a probe needs
		desc.soundVolume = 0.2f;
		desc.masterVolume = 1.0f;
		return desc;
	}
}

TEST_CASE("sfx_render_produces_mono16_pcm_at_the_standard_rate", "[unit][sfx]")
{
	SfxDesc desc;						// the standard's own reset state
	const SfxSynth::Pcm pcm = SfxSynth::render(desc);

	CHECK(pcm.channels == 1);			// mono, so a source stays positional
	CHECK(pcm.bitsPerSample == 16);
	// the rate is FIXED: the envelope and period constants are expressed in
	// samples at it, so it is not a parameter
	CHECK(pcm.sampleRate == SfxSynth::SAMPLE_RATE);
	CHECK(pcm.sampleRate == 44100);
	REQUIRE(pcm.samples.size() > 1u);
	CHECK(pcm.byteSize() == pcm.samples.size() * 2u);
	// sustain 0.3 + decay 0.4 -> (0.09 + 0.16) * 100000 samples, plus the one
	// sample each stage change emits at its own time zero
	CHECK(pcm.samples.size() == 25002u);
	CHECK(pcm.durationSec() == Approx(0.567f).margin(0.002f));
	CHECK(SfxSynth::renderedDuration(desc) ==
		Approx(pcm.durationSec()).margin(0.0001f));
	CHECK(peakOf(pcm) > 0.2f);			// and it is audible
}

TEST_CASE("sfx_envelope_stages", "[unit][sfx]")
{
	SECTION("the envelope's three weights set the length in samples")
	{
		SfxDesc desc;
		desc.attack = 0.1f;		// 0.01 * 100000 = 1000 samples
		desc.sustain = 0.2f;	// 0.04 * 100000 = 4000
		desc.decay = 0.3f;		// 0.09 * 100000 = 9000
		const SfxSynth::Pcm pcm = SfxSynth::render(desc);
		CHECK(pcm.samples.size() == 14002u);
	}
	SECTION("an attack ramps up from silence, a decay ends near it")
	{
		SfxDesc desc = steadyTone(SfxWave::SW_SINE);
		desc.attack = 0.3f;			// 9000 samples of ramp
		const SfxSynth::Pcm pcm = SfxSynth::render(desc);
		REQUIRE(pcm.samples.size() > 9000u);
		CHECK(std::abs(static_cast<int>(pcm.samples[0])) < 200);
		// the ramp's amplitude grows: a late window peaks above an early one
		int early = 0;
		int late = 0;
		for(std::size_t i = 100; i < 900; ++i)
		{
			early = std::max(early, std::abs(static_cast<int>(pcm.samples[i])));
		}
		for(std::size_t i = 8100; i < 8900; ++i)
		{
			late = std::max(late, std::abs(static_cast<int>(pcm.samples[i])));
		}
		CHECK(late > early * 4);
		// the decay's last sample is at the bottom of the fall
		CHECK(std::abs(static_cast<int>(pcm.samples.back())) <
			static_cast<int>(0.02f * 32000.0f));
	}
	SECTION("a zero-length segment is skipped, never divided by")
	{
		// every weight at 0 leaves nothing to play - an honest empty render,
		// not a crash and not a NaN
		SfxDesc desc;
		desc.attack = 0.0f;
		desc.sustain = 0.0f;
		desc.decay = 0.0f;
		const SfxSynth::Pcm pcm = SfxSynth::render(desc);
		CHECK(pcm.samples.size() <= 3u);
	}
	SECTION("punch spikes the sustain's start above the body")
	{
		SfxDesc plain = steadyTone(SfxWave::SW_SQUARE);
		SfxDesc punched = plain;
		punched.punch = 0.6f;
		const SfxSynth::Pcm plainPcm = SfxSynth::render(plain);
		const SfxSynth::Pcm punchedPcm = SfxSynth::render(punched);
		REQUIRE(plainPcm.samples.size() == punchedPcm.samples.size());
		// same length (punch is amplitude, not time) but a louder onset
		int plainOnset = 0;
		int punchedOnset = 0;
		for(std::size_t i = 0; i < 500; ++i)
		{
			plainOnset = std::max(plainOnset,
				std::abs(static_cast<int>(plainPcm.samples[i])));
			punchedOnset = std::max(punchedOnset,
				std::abs(static_cast<int>(punchedPcm.samples[i])));
		}
		CHECK(punchedOnset > plainOnset);
	}
}

TEST_CASE("sfx_waveform_shapes", "[unit][sfx]")
{
	SECTION("square sits at its two levels and almost never between them")
	{
		const SfxSynth::Pcm pcm = SfxSynth::render(
			steadyTone(SfxWave::SW_SQUARE));
		REQUIRE(pcm.samples.size() > 9000u);
		CHECK(shareNearZero(pcm, 8000u, 9000u) < 0.02f);
		// and it is symmetric about silence: as much time high as low
		std::size_t high = 0;
		for(std::size_t i = 8000u; i < 9000u; ++i)
		{
			if(pcm.samples[i] > 0)
			{
				++high;
			}
		}
		CHECK(high > 400u);
		CHECK(high < 600u);
	}
	SECTION("sine is smooth and centred")
	{
		const SfxSynth::Pcm pcm = SfxSynth::render(
			steadyTone(SfxWave::SW_SINE));
		REQUIRE(pcm.samples.size() > 9000u);
		CHECK(distinctValues(pcm, 8000u, 9000u) > 100u);	// a continuum
		// unlike a square, a sine passes through the quiet middle constantly
		CHECK(shareNearZero(pcm, 8000u, 9000u) > 0.2f);
		double sum = 0.0;
		int biggestStep = 0;
		for(std::size_t i = 8001; i < 9000; ++i)
		{
			sum += static_cast<double>(pcm.samples[i]);
			biggestStep = std::max(biggestStep,
				std::abs(static_cast<int>(pcm.samples[i]) -
					static_cast<int>(pcm.samples[i - 1])));
		}
		CHECK(std::abs(sum / 999.0) < 1500.0);		// no DC offset
		CHECK(biggestStep < 6000);					// no discontinuity
	}
	SECTION("saw ramps monotonically inside a cycle")
	{
		const SfxSynth::Pcm pcm = SfxSynth::render(
			steadyTone(SfxWave::SW_SAW));
		REQUIRE(pcm.samples.size() > 9000u);
		// the saw falls across a cycle and wraps up once per period; count the
		// direction changes over a window - a handful, not one per sample
		std::size_t flips = 0;
		int previous = 0;
		for(std::size_t i = 8001; i < 8200; ++i)
		{
			const int step = static_cast<int>(pcm.samples[i]) -
				static_cast<int>(pcm.samples[i - 1]);
			if(previous != 0 && step != 0 && ((step < 0) != (previous < 0)))
			{
				++flips;
			}
			if(step != 0)
			{
				previous = step;
			}
		}
		CHECK(flips < 20u);
	}
	SECTION("noise is broadband")
	{
		const SfxSynth::Pcm pcm = SfxSynth::render(
			steadyTone(SfxWave::SW_NOISE));
		REQUIRE(pcm.samples.size() > 9000u);
		CHECK(distinctValues(pcm, 8000u, 9000u) > 300u);
		// noise changes sign constantly, unlike any of the tones
		std::size_t signFlips = 0;
		for(std::size_t i = 8001; i < 9000; ++i)
		{
			if((pcm.samples[i] < 0) != (pcm.samples[i - 1] < 0))
			{
				++signFlips;
			}
		}
		CHECK(signFlips > 50u);
	}
}

TEST_CASE("sfx_render_is_deterministic", "[unit][sfx]")
{
	// THE contract that makes a sound file an ASSET: the same parameters
	// always yield byte-identical samples, the noise oscillator included
	SfxDesc desc = steadyTone(SfxWave::SW_NOISE);
	desc.seed = 4242u;
	const SfxSynth::Pcm first = SfxSynth::render(desc);
	const SfxSynth::Pcm again = SfxSynth::render(desc);
	REQUIRE(first.samples.size() == again.samples.size());
	CHECK(first.samples == again.samples);

	// a different seed is a different noise sequence of the same length
	SfxDesc other = desc;
	other.seed = 4243u;
	const SfxSynth::Pcm shifted = SfxSynth::render(other);
	REQUIRE(shifted.samples.size() == first.samples.size());
	CHECK(shifted.samples != first.samples);

	// a seed change does NOT disturb a tone (only noise reads the generator)
	SfxDesc tone = steadyTone(SfxWave::SW_SAW);
	SfxDesc toneOtherSeed = tone;
	toneOtherSeed.seed = 999u;
	CHECK(SfxSynth::render(tone).samples ==
		SfxSynth::render(toneOtherSeed).samples);
}

TEST_CASE("sfx_render_stays_in_full_scale_and_finite", "[unit][sfx]")
{
	SECTION("volume scales the mix")
	{
		SfxDesc loud = steadyTone(SfxWave::SW_SQUARE);
		SfxDesc quiet = loud;
		quiet.soundVolume = loud.soundVolume * 0.25f;
		CHECK(peakOf(SfxSynth::render(quiet)) <
			peakOf(SfxSynth::render(loud)) * 0.4f);
		CHECK(peakOf(SfxSynth::render(loud)) <= 1.0f);
		// the output gain multiplies on top of it, the same way
		SfxDesc halved = loud;
		halved.masterVolume = 0.5f;
		CHECK(peakOf(SfxSynth::render(halved)) <
			peakOf(SfxSynth::render(loud)) * 0.7f);
	}
	SECTION("a punched, resonant, phasered effect never wraps")
	{
		// every gain-adding stage at once: the clamp is what keeps this inside
		// the sample range
		SfxDesc desc = steadyTone(SfxWave::SW_SQUARE);
		desc.soundVolume = 1.0f;
		desc.masterVolume = 1.0f;
		desc.punch = 1.0f;
		desc.lpfFreq = 0.4f;
		desc.lpfResonance = 1.0f;
		desc.phaserOffset = 0.6f;
		desc.phaserRamp = 0.4f;
		const SfxSynth::Pcm pcm = SfxSynth::render(desc);
		CHECK(peakOf(pcm) <= 1.0f);
		for(std::size_t i = 0; i < pcm.samples.size(); ++i)
		{
			REQUIRE(std::abs(static_cast<int>(pcm.samples[i])) <= 32000);
		}
	}
	SECTION("non-finite parameters fall back instead of poisoning the buffer")
	{
		SfxDesc desc;
		desc.baseFreq = std::nanf("");
		desc.soundVolume = std::numeric_limits<float>::infinity();
		desc.decay = -std::numeric_limits<float>::infinity();
		std::vector<String> notes;
		SfxDesc sane = desc;
		CHECK_FALSE(SfxSynth::sanitize(sane, &notes));
		CHECK(std::isfinite(sane.baseFreq));
		CHECK(std::isfinite(sane.soundVolume));
		CHECK(std::isfinite(sane.decay));
		CHECK(notes.size() >= 3u);
		const SfxSynth::Pcm pcm = SfxSynth::render(desc);
		CHECK(peakOf(pcm) <= 1.0f);
	}
}

TEST_CASE("sfx_sanitize_clamps_and_names_out_of_range_weights", "[unit][sfx]")
{
	SfxDesc desc;
	desc.baseFreq = 4.0f;			// the weights are normalized 0..1
	desc.soundVolume = 12.0f;
	desc.sustain = -3.0f;
	desc.freqRamp = -8.0f;			// the SIGNED weights run -1..1
	desc.arpMod = 5.0f;
	desc.phaserOffset = -2.5f;

	std::vector<String> notes;
	CHECK_FALSE(SfxSynth::sanitize(desc, &notes));
	CHECK(desc.baseFreq == Approx(1.0f));
	CHECK(desc.soundVolume == Approx(1.0f));
	CHECK(desc.sustain == Approx(0.0f));
	CHECK(desc.freqRamp == Approx(-1.0f));
	CHECK(desc.arpMod == Approx(1.0f));
	CHECK(desc.phaserOffset == Approx(-1.0f));
	// every correction is NAMED so the loader can log it
	CHECK(notes.size() >= 6u);
	bool sawBaseFreq = false;
	for(std::size_t i = 0; i < notes.size(); ++i)
	{
		if(notes[i].find("baseFreq") != String::npos)
		{
			sawBaseFreq = true;
		}
	}
	CHECK(sawBaseFreq);

	// a second pass has nothing left to clamp (sanitize is idempotent)
	std::vector<String> again;
	CHECK(SfxSynth::sanitize(desc, &again));
	CHECK(again.empty());
}

TEST_CASE("sfx_sanitize_leaves_the_standard_defaults_alone", "[unit][sfx]")
{
	SfxDesc desc;
	std::vector<String> notes;
	CHECK(SfxSynth::sanitize(desc, &notes));
	CHECK(notes.empty());
}

TEST_CASE("sfx_parameters_change_the_sound", "[unit][sfx]")
{
	const SfxDesc base = steadyTone(SfxWave::SW_SQUARE);
	const std::vector<std::int16_t> reference =
		SfxSynth::render(base).samples;

	// each standard knob must actually reach the mix - a parameter that
	// changed nothing would be a silent wiring bug
	SECTION("frequency slide")
	{
		SfxDesc desc = base;
		desc.freqRamp = 0.4f;
		CHECK(SfxSynth::render(desc).samples != reference);
	}
	SECTION("delta slide")
	{
		SfxDesc desc = base;
		desc.freqRamp = 0.2f;
		desc.freqDeltaRamp = -0.5f;
		SfxDesc slideOnly = base;
		slideOnly.freqRamp = 0.2f;
		CHECK(SfxSynth::render(desc).samples !=
			SfxSynth::render(slideOnly).samples);
	}
	SECTION("frequency limit ends a downward slide early")
	{
		SfxDesc desc = base;
		desc.freqRamp = -0.5f;
		desc.freqLimit = 0.2f;
		SfxDesc unlimited = desc;
		unlimited.freqLimit = 0.0f;
		// the limit stops PLAYBACK, so the buffer is shorter
		CHECK(SfxSynth::render(desc).samples.size() <
			SfxSynth::render(unlimited).samples.size());
	}
	SECTION("square duty and its sweep")
	{
		SfxDesc desc = base;
		desc.duty = 0.6f;
		CHECK(SfxSynth::render(desc).samples != reference);
		SfxDesc swept = desc;
		swept.dutyRamp = 0.8f;
		CHECK(SfxSynth::render(swept).samples !=
			SfxSynth::render(desc).samples);
	}
	SECTION("vibrato")
	{
		SfxDesc desc = base;
		desc.vibratoStrength = 0.5f;
		desc.vibratoSpeed = 0.4f;
		CHECK(SfxSynth::render(desc).samples != reference);
	}
	SECTION("low-pass, its sweep and its resonance")
	{
		SfxDesc filtered = base;
		filtered.lpfFreq = 0.3f;
		const std::vector<std::int16_t> lowpassed =
			SfxSynth::render(filtered).samples;
		CHECK(lowpassed != reference);
		// a filtered square is no longer two levels: it ramps between them
		SfxSynth::Pcm pcm;
		pcm.samples = lowpassed;
		CHECK(distinctValues(pcm, 8000u, 9000u) > 2u);
		SfxDesc resonant = filtered;
		resonant.lpfResonance = 0.8f;
		CHECK(SfxSynth::render(resonant).samples != lowpassed);
		SfxDesc swept = filtered;
		swept.lpfRamp = 0.9f;
		CHECK(SfxSynth::render(swept).samples != lowpassed);
	}
	SECTION("high-pass and its sweep")
	{
		SfxDesc desc = base;
		desc.hpfFreq = 0.4f;
		const std::vector<std::int16_t> highpassed =
			SfxSynth::render(desc).samples;
		CHECK(highpassed != reference);
		SfxDesc swept = desc;
		swept.hpfRamp = -0.8f;
		CHECK(SfxSynth::render(swept).samples != highpassed);
	}
	SECTION("the delay tap's offset and sweep")
	{
		SfxDesc desc = base;
		desc.phaserOffset = 0.5f;
		const std::vector<std::int16_t> phasered =
			SfxSynth::render(desc).samples;
		CHECK(phasered != reference);
		SfxDesc swept = desc;
		swept.phaserRamp = 0.5f;
		CHECK(SfxSynth::render(swept).samples != phasered);
	}
	SECTION("retrigger restarts the oscillator without extending the sound")
	{
		// a retrigger re-arms the PITCH, so it is audible against a slide (on
		// a flat tone there is by construction nothing to restart)
		SfxDesc sliding = base;
		sliding.freqRamp = 0.3f;
		const SfxSynth::Pcm plain = SfxSynth::render(sliding);
		SfxDesc desc = sliding;
		desc.repeatSpeed = 0.5f;
		const SfxSynth::Pcm repeated = SfxSynth::render(desc);
		CHECK(repeated.samples != plain.samples);
		// the envelope owns the length: a retrigger re-arms the oscillator,
		// never the envelope, so a repeating effect is not an endless one
		CHECK(repeated.samples.size() == plain.samples.size());
	}
	SECTION("arpeggio steps the pitch once")
	{
		SfxDesc desc = base;
		desc.arpSpeed = 0.6f;
		desc.arpMod = 0.5f;
		CHECK(SfxSynth::render(desc).samples != reference);
	}
	SECTION("the parameters the format carries but the standard never reads")
	{
		// honesty, not laziness: honoring these would make our output differ
		// from every tool that writes the format (@see SfxDesc.h)
		SfxDesc desc = base;
		desc.vibratoDelay = 0.9f;
		desc.filterOn = true;
		CHECK(SfxSynth::render(desc).samples == reference);
	}
}

TEST_CASE("sfx_archetype_generators_are_sane", "[unit][sfx]")
{
	// every standard generator draws a short, audible, non-wrapping effect
	// that needs no clamping
	for(int i = 0; i < SfxPreset::ARCHETYPE_COUNT; ++i)
	{
		const SfxPreset::Archetype archetype =
			static_cast<SfxPreset::Archetype>(i);
		SfxDesc desc = SfxPreset::forArchetype(archetype);
		const String name = SfxPreset::archetypeName(archetype);
		INFO("archetype " << name);

		std::vector<String> notes;
		SfxDesc sane = desc;
		CHECK(SfxSynth::sanitize(sane, &notes));
		CHECK(notes.empty());

		const SfxSynth::Pcm pcm = SfxSynth::render(desc);
		CHECK(pcm.durationSec() > 0.02f);		// audible ...
		CHECK(pcm.durationSec() < 2.0f);		// ... but not a music bed
		CHECK(peakOf(pcm) > 0.2f);				// loud enough to hear
		CHECK(peakOf(pcm) <= 1.0f);				// and never wrapped

		// the name round-trips through the parse/print pair
		SfxPreset::Archetype reparsed = SfxPreset::SA_JUMP;
		CHECK(SfxPreset::parseArchetype(name, reparsed));
		CHECK(reparsed == archetype);
	}
	// the archetypes are genuinely DIFFERENT sounds, not one recipe
	CHECK(SfxSynth::render(SfxPreset::forArchetype(SfxPreset::SA_PICKUP_COIN))
		.samples !=
		SfxSynth::render(SfxPreset::forArchetype(SfxPreset::SA_LASER_SHOOT))
		.samples);
}

TEST_CASE("sfx_archetype_seed_picks_a_family_member", "[unit][sfx]")
{
	// a generator is a FAMILY of sounds; the seed names one of them, and the
	// same seed always names the same one (what makes a preset an asset)
	const SfxDesc first = SfxPreset::forArchetype(SfxPreset::SA_EXPLOSION, 7u);
	const SfxDesc again = SfxPreset::forArchetype(SfxPreset::SA_EXPLOSION, 7u);
	const SfxDesc other = SfxPreset::forArchetype(SfxPreset::SA_EXPLOSION, 8u);
	CHECK(first.seed == 7u);				// carried into the render seed
	CHECK(SfxSynth::render(first).samples == SfxSynth::render(again).samples);
	CHECK(SfxSynth::render(first).samples != SfxSynth::render(other).samples);
	// an explosion stays an explosion whichever member is drawn
	CHECK(first.waveType == SfxWave::SW_NOISE);
	CHECK(other.waveType == SfxWave::SW_NOISE);
}

TEST_CASE("sfx_wave_and_preset_names_reject_strangers", "[unit][sfx]")
{
	SfxPreset::Archetype archetype = SfxPreset::SA_LASER_SHOOT;
	CHECK_FALSE(SfxPreset::parseArchetype("trombone", archetype));
	CHECK(archetype == SfxPreset::SA_LASER_SHOOT);	// caller's value untouched
	CHECK(SfxPreset::parseArchetype("COIN", archetype));	// case-insensitive
	CHECK(archetype == SfxPreset::SA_PICKUP_COIN);
	CHECK(SfxPreset::parseArchetype("pickup", archetype));	// the standard's
	CHECK(archetype == SfxPreset::SA_PICKUP_COIN);			// other name
	CHECK(SfxPreset::parseArchetype("shoot", archetype));
	CHECK(archetype == SfxPreset::SA_LASER_SHOOT);

	SfxWave::Type wave = SfxWave::SW_SAW;
	CHECK_FALSE(SfxWave::parseType("bagpipe", wave));
	CHECK(wave == SfxWave::SW_SAW);
	CHECK(SfxWave::parseType("Noise", wave));
	CHECK(wave == SfxWave::SW_NOISE);
	CHECK(String(SfxWave::typeName(SfxWave::SW_SQUARE)) == "square");

	// the NUMBERING is part of the file format and must not drift
	CHECK(static_cast<int>(SfxWave::SW_SQUARE) == 0);
	CHECK(static_cast<int>(SfxWave::SW_SAW) == 1);
	CHECK(static_cast<int>(SfxWave::SW_SINE) == 2);
	CHECK(static_cast<int>(SfxWave::SW_NOISE) == 3);
	SfxWave::Type fromFile = SfxWave::SW_SQUARE;
	CHECK(SfxWave::fromNumber(2, fromFile));
	CHECK(fromFile == SfxWave::SW_SINE);
	CHECK_FALSE(SfxWave::fromNumber(4, fromFile));
	CHECK_FALSE(SfxWave::fromNumber(-1, fromFile));
	CHECK(fromFile == SfxWave::SW_SINE);			// untouched on refusal
}
