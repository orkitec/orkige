/**************************************************************
	created:	2026/07/29 at 10:10
	filename: 	SfxSynth.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file SfxSynth.cpp
//! @brief the procedural sound-effect synthesizer and the archetype
//! generators, both implementing the standard retro-sfx specification
//! (@see SfxSynth.h, SfxDesc.h, Docs/sound.md on provenance)

#include "core_util/SfxSynth.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Orkige
{
	const int			SfxSynth::SAMPLE_RATE = 44100;
	const int			SfxSynth::SUPERSAMPLES = 8;
	const std::size_t	SfxSynth::MAX_SAMPLES = 44100u * 12u;

	namespace
	{
		const float SFX_TWO_PI = 6.28318530718f;
		//! the delay tap's fixed ring length (part of the standard)
		const int SFX_PHASER_LENGTH = 1024;
		//! the noise oscillator's fixed table length (part of the standard)
		const int SFX_NOISE_LENGTH = 32;

		//! @brief the deterministic random source shared by the noise
		//! oscillator and the archetype generators: a plain xorshift32, whose
		//! sequence is fixed by the standard's integer arithmetic alone (a
		//! std:: distribution's sequence is implementation-defined, so one is
		//! deliberately NOT used here - @see SfxSynth.h on determinism)
		struct Rng
		{
			unsigned int state;

			explicit Rng(unsigned int seed)
				// a zero state would stick at zero forever
				: state(seed != 0u ? seed : 0x9E3779B9u)
			{
			}
			//! the next unsigned int
			unsigned int next()
			{
				this->state ^= this->state << 13;
				this->state ^= this->state >> 17;
				this->state ^= this->state << 5;
				return this->state;
			}
			//! a float in 0..1 (24 bits of mantissa, exactly representable)
			float unit()
			{
				return static_cast<float>(this->next() >> 8) / 16777216.0f;
			}
			//! a float in 0..range - the generators' own primitive
			float frnd(float range)
			{
				return this->unit() * range;
			}
			//! an integer in 0..n inclusive - the generators' own primitive
			int rnd(int n)
			{
				return static_cast<int>(this->unit() *
					static_cast<float>(n + 1)) % (n + 1);
			}
			//! a coin flip (the generators' `if(rnd(1))`)
			bool flip()
			{
				return this->rnd(1) != 0;
			}
		};

		//! square of a float, spelled out where the standard squares a weight
		float sq(float value)
		{
			return value * value;
		}
		//! cube of a float, spelled out where the standard cubes a weight
		float cube(float value)
		{
			return value * value * value;
		}

		//! @brief append one clamp note ("field was clamped to is")
		void note(std::vector<String> * outNotes, String const & field,
			float was, float is)
		{
			if(!outNotes)
			{
				return;
			}
			std::ostringstream line;
			line.setf(std::ios::fixed);
			line.precision(4);
			line << field << " " << was << " clamped to " << is;
			outNotes->push_back(line.str());
		}
		//! @brief clamp a float field into [lo,hi], replacing a non-finite
		//! value with @p fallback; records a note and reports whether it moved
		bool clampField(float & value, float lo, float hi, float fallback,
			String const & field, std::vector<String> * outNotes)
		{
			if(!std::isfinite(value))
			{
				note(outNotes, field, value, fallback);
				value = fallback;
				return true;
			}
			const float clamped = std::min(hi, std::max(lo, value));
			if(clamped != value)
			{
				note(outNotes, field, value, clamped);
				value = clamped;
				return true;
			}
			return false;
		}

		//! @brief the envelope's three segment lengths in samples, the way the
		//! standard derives them (a squared weight scaled to 100000 samples)
		void envelopeLengths(SfxDesc const & desc, int lengths[3])
		{
			lengths[0] = static_cast<int>(sq(desc.attack) * 100000.0f);
			lengths[1] = static_cast<int>(sq(desc.sustain) * 100000.0f);
			lengths[2] = static_cast<int>(sq(desc.decay) * 100000.0f);
		}

		//! @brief the whole running state of one synthesis pass
		//! @remarks Split out so the retrigger can re-arm exactly the subset
		//! the standard re-arms: a RESTART re-derives the oscillator period,
		//! duty, arpeggio and the delay/filter/noise state but deliberately
		//! leaves the ENVELOPE running - which is why a repeating effect is not
		//! an endless one.
		struct SynthState
		{
			// oscillator
			int		period = 8;
			float	fperiod = 8.0f;
			float	fmaxperiod = 0.0f;
			float	fslide = 0.0f;
			float	fdslide = 0.0f;
			int		phase = 0;
			float	squareDuty = 0.5f;
			float	squareSlide = 0.0f;
			// arpeggio
			float	arpMod = 0.0f;
			int		arpTime = 0;
			int		arpLimit = 0;
			// envelope
			int		envStage = 0;
			int		envTime = 0;
			int		envLength[3] = { 0, 0, 0 };
			float	envVolume = 0.0f;
			// filters
			float	fltp = 0.0f;
			float	fltdp = 0.0f;
			float	fltw = 0.0f;
			float	fltwDelta = 0.0f;
			float	fltDamp = 0.0f;
			float	fltphp = 0.0f;
			float	flthp = 0.0f;
			float	flthpDelta = 0.0f;
			// vibrato
			float	vibPhase = 0.0f;
			float	vibSpeed = 0.0f;
			float	vibAmp = 0.0f;
			// the swept delay tap
			float	fphase = 0.0f;
			float	fdphase = 0.0f;
			int		iphase = 0;
			int		ipp = 0;
			float	phaserBuffer[SFX_PHASER_LENGTH];
			// noise
			float	noiseBuffer[SFX_NOISE_LENGTH];
			// retrigger
			int		repTime = 0;
			int		repLimit = 0;
			// playback
			bool	playing = true;

			SynthState()
			{
				for(int i = 0; i < SFX_PHASER_LENGTH; ++i)
				{
					this->phaserBuffer[i] = 0.0f;
				}
				for(int i = 0; i < SFX_NOISE_LENGTH; ++i)
				{
					this->noiseBuffer[i] = 0.0f;
				}
			}
		};

		//! @brief arm (or, with @p restart, re-arm) the synthesis state from
		//! the parameters - the standard's reset step
		void resetState(SynthState & state, SfxDesc const & desc, Rng & rng,
			bool restart)
		{
			if(!restart)
			{
				state.phase = 0;
			}
			state.fperiod = 100.0f / (sq(desc.baseFreq) + 0.001f);
			state.period = static_cast<int>(state.fperiod);
			state.fmaxperiod = 100.0f / (sq(desc.freqLimit) + 0.001f);
			state.fslide = 1.0f - cube(desc.freqRamp) * 0.01f;
			state.fdslide = -cube(desc.freqDeltaRamp) * 0.000001f;
			state.squareDuty = 0.5f - desc.duty * 0.5f;
			state.squareSlide = -desc.dutyRamp * 0.00005f;
			if(desc.arpMod >= 0.0f)
			{
				state.arpMod = 1.0f - sq(desc.arpMod) * 0.9f;
			}
			else
			{
				state.arpMod = 1.0f + sq(desc.arpMod) * 10.0f;
			}
			state.arpTime = 0;
			state.arpLimit = static_cast<int>(sq(1.0f - desc.arpSpeed) *
				20000.0f + 32.0f);
			if(desc.arpSpeed == 1.0f)
			{
				state.arpLimit = 0;		// the "off" end of the knob
			}
			if(restart)
			{
				return;
			}

			// --- the once-per-sound state a retrigger does NOT re-arm ---
			state.fltp = 0.0f;
			state.fltdp = 0.0f;
			state.fltw = cube(desc.lpfFreq) * 0.1f;
			state.fltwDelta = 1.0f + desc.lpfRamp * 0.0001f;
			state.fltDamp = 5.0f / (1.0f + sq(desc.lpfResonance) * 20.0f) *
				(0.01f + state.fltw);
			state.fltDamp = std::min(0.8f, state.fltDamp);
			state.fltphp = 0.0f;
			state.flthp = sq(desc.hpfFreq) * 0.1f;
			state.flthpDelta = 1.0f + desc.hpfRamp * 0.0003f;

			state.vibPhase = 0.0f;
			state.vibSpeed = sq(desc.vibratoSpeed) * 0.01f;
			state.vibAmp = desc.vibratoStrength * 0.5f;

			state.envVolume = 0.0f;
			state.envStage = 0;
			state.envTime = 0;
			envelopeLengths(desc, state.envLength);

			state.fphase = sq(desc.phaserOffset) * 1020.0f;
			if(desc.phaserOffset < 0.0f)
			{
				state.fphase = -state.fphase;
			}
			state.fdphase = sq(desc.phaserRamp) * 1.0f;
			if(desc.phaserRamp < 0.0f)
			{
				state.fdphase = -state.fdphase;
			}
			state.iphase = std::abs(static_cast<int>(state.fphase));
			state.ipp = 0;
			for(int i = 0; i < SFX_PHASER_LENGTH; ++i)
			{
				state.phaserBuffer[i] = 0.0f;
			}
			for(int i = 0; i < SFX_NOISE_LENGTH; ++i)
			{
				state.noiseBuffer[i] = rng.frnd(2.0f) - 1.0f;
			}

			state.repTime = 0;
			state.repLimit = static_cast<int>(sq(1.0f - desc.repeatSpeed) *
				20000.0f + 32.0f);
			if(desc.repeatSpeed == 0.0f)
			{
				state.repLimit = 0;		// the "off" end of the knob
			}
		}
	}
	//---------------------------------------------------------
	bool SfxSynth::sanitize(SfxDesc & desc, std::vector<String> * outNotes)
	{
		const SfxDesc defaults;
		bool moved = false;

		// the UNSIGNED weights (0..1)
		moved |= clampField(desc.soundVolume, 0.0f, 1.0f,
			defaults.soundVolume, "soundVolume", outNotes);
		moved |= clampField(desc.masterVolume, 0.0f, 1.0f,
			defaults.masterVolume, "masterVolume", outNotes);
		moved |= clampField(desc.baseFreq, 0.0f, 1.0f, defaults.baseFreq,
			"baseFreq", outNotes);
		moved |= clampField(desc.freqLimit, 0.0f, 1.0f, defaults.freqLimit,
			"freqLimit", outNotes);
		moved |= clampField(desc.duty, 0.0f, 1.0f, defaults.duty, "duty",
			outNotes);
		moved |= clampField(desc.vibratoStrength, 0.0f, 1.0f,
			defaults.vibratoStrength, "vibratoStrength", outNotes);
		moved |= clampField(desc.vibratoSpeed, 0.0f, 1.0f,
			defaults.vibratoSpeed, "vibratoSpeed", outNotes);
		moved |= clampField(desc.vibratoDelay, 0.0f, 1.0f,
			defaults.vibratoDelay, "vibratoDelay", outNotes);
		moved |= clampField(desc.attack, 0.0f, 1.0f, defaults.attack, "attack",
			outNotes);
		moved |= clampField(desc.sustain, 0.0f, 1.0f, defaults.sustain,
			"sustain", outNotes);
		moved |= clampField(desc.decay, 0.0f, 1.0f, defaults.decay, "decay",
			outNotes);
		moved |= clampField(desc.punch, 0.0f, 1.0f, defaults.punch, "punch",
			outNotes);
		moved |= clampField(desc.lpfResonance, 0.0f, 1.0f,
			defaults.lpfResonance, "lpfResonance", outNotes);
		moved |= clampField(desc.lpfFreq, 0.0f, 1.0f, defaults.lpfFreq,
			"lpfFreq", outNotes);
		moved |= clampField(desc.hpfFreq, 0.0f, 1.0f, defaults.hpfFreq,
			"hpfFreq", outNotes);
		moved |= clampField(desc.repeatSpeed, 0.0f, 1.0f,
			defaults.repeatSpeed, "repeatSpeed", outNotes);
		moved |= clampField(desc.arpSpeed, 0.0f, 1.0f, defaults.arpSpeed,
			"arpSpeed", outNotes);

		// the SIGNED weights (-1..1): slides, sweeps, arpeggio, the delay tap
		moved |= clampField(desc.freqRamp, -1.0f, 1.0f, defaults.freqRamp,
			"freqRamp", outNotes);
		moved |= clampField(desc.freqDeltaRamp, -1.0f, 1.0f,
			defaults.freqDeltaRamp, "freqDeltaRamp", outNotes);
		moved |= clampField(desc.dutyRamp, -1.0f, 1.0f, defaults.dutyRamp,
			"dutyRamp", outNotes);
		moved |= clampField(desc.lpfRamp, -1.0f, 1.0f, defaults.lpfRamp,
			"lpfRamp", outNotes);
		moved |= clampField(desc.hpfRamp, -1.0f, 1.0f, defaults.hpfRamp,
			"hpfRamp", outNotes);
		moved |= clampField(desc.phaserOffset, -1.0f, 1.0f,
			defaults.phaserOffset, "phaserOffset", outNotes);
		moved |= clampField(desc.phaserRamp, -1.0f, 1.0f, defaults.phaserRamp,
			"phaserRamp", outNotes);
		moved |= clampField(desc.arpMod, -1.0f, 1.0f, defaults.arpMod,
			"arpMod", outNotes);

		return !moved;
	}
	//---------------------------------------------------------
	float SfxSynth::renderedDuration(SfxDesc const & desc)
	{
		SfxDesc sane = desc;
		SfxSynth::sanitize(sane);
		int lengths[3] = { 0, 0, 0 };
		envelopeLengths(sane, lengths);
		// the envelope walks its three segments one sample at a time; each of
		// the two STAGE CHANGES emits its first sample at time zero, so the
		// sound is two samples longer than the segments' sum
		const std::size_t total = static_cast<std::size_t>(lengths[0]) +
			static_cast<std::size_t>(lengths[1]) +
			static_cast<std::size_t>(lengths[2]) + 2u;
		return static_cast<float>(std::min(total, SfxSynth::MAX_SAMPLES)) /
			static_cast<float>(SfxSynth::SAMPLE_RATE);
	}
	//---------------------------------------------------------
	SfxSynth::Pcm SfxSynth::render(SfxDesc const & desc)
	{
		// the caller's desc is never mutated - a render is a pure read
		SfxDesc sane = desc;
		SfxSynth::sanitize(sane);

		Pcm pcm;
		pcm.sampleRate = SfxSynth::SAMPLE_RATE;

		Rng rng(sane.seed);
		SynthState state;
		resetState(state, sane, rng, false);

		std::vector<float> mix;
		mix.reserve(static_cast<std::size_t>(
			SfxSynth::renderedDuration(sane) *
			static_cast<float>(SfxSynth::SAMPLE_RATE)) + 16u);

		while(state.playing && mix.size() < SfxSynth::MAX_SAMPLES)
		{
			// --- retrigger: restart the oscillator, keep the envelope -----
			++state.repTime;
			if(state.repLimit != 0 && state.repTime >= state.repLimit)
			{
				state.repTime = 0;
				resetState(state, sane, rng, true);
			}

			// --- arpeggio: one period step, once -------------------------
			++state.arpTime;
			if(state.arpLimit != 0 && state.arpTime >= state.arpLimit)
			{
				state.arpLimit = 0;
				state.fperiod *= state.arpMod;
			}

			// --- frequency slide (and the slide's own slide) -------------
			state.fslide += state.fdslide;
			state.fperiod *= state.fslide;
			if(state.fperiod > state.fmaxperiod)
			{
				state.fperiod = state.fmaxperiod;
				if(sane.freqLimit > 0.0f)
				{
					state.playing = false;	// the limit ends the sound
				}
			}
			float renderPeriod = state.fperiod;
			if(state.vibAmp > 0.0f)
			{
				state.vibPhase += state.vibSpeed;
				renderPeriod = state.fperiod *
					(1.0f + std::sin(state.vibPhase) * state.vibAmp);
			}
			state.period = static_cast<int>(renderPeriod);
			state.period = std::max(8, state.period);
			state.squareDuty += state.squareSlide;
			state.squareDuty = std::min(0.5f, std::max(0.0f,
				state.squareDuty));

			// --- envelope: advance BEFORE reading the stage (a zero-length
			// --- segment is skipped, never divided by) -------------------
			++state.envTime;
			if(state.envTime > state.envLength[state.envStage])
			{
				state.envTime = 0;
				++state.envStage;
				if(state.envStage == 3)
				{
					state.playing = false;
				}
			}
			if(state.playing)
			{
				const float segment = static_cast<float>(
					std::max(1, state.envLength[state.envStage]));
				const float through = static_cast<float>(state.envTime) /
					segment;
				if(state.envStage == 0)
				{
					state.envVolume = through;
				}
				else if(state.envStage == 1)
				{
					// the PUNCH: the sustain starts above full amplitude and
					// falls to it (the standard's percussive accent)
					state.envVolume = 1.0f +
						(1.0f - through) * 2.0f * sane.punch;
				}
				else
				{
					state.envVolume = 1.0f - through;
				}
			}
			if(!state.playing)
			{
				break;
			}

			// --- the delay tap's offset sweep ---------------------------
			state.fphase += state.fdphase;
			state.iphase = std::min(SFX_PHASER_LENGTH - 1,
				std::abs(static_cast<int>(state.fphase)));

			// --- high-pass cutoff sweep ---------------------------------
			if(state.flthpDelta != 0.0f)
			{
				state.flthp *= state.flthpDelta;
				state.flthp = std::min(0.1f, std::max(0.00001f, state.flthp));
			}

			// --- the supersampled mix -----------------------------------
			float superSum = 0.0f;
			for(int step = 0; step < SfxSynth::SUPERSAMPLES; ++step)
			{
				++state.phase;
				if(state.phase >= state.period)
				{
					state.phase %= state.period;
					if(sane.waveType == SfxWave::SW_NOISE)
					{
						for(int i = 0; i < SFX_NOISE_LENGTH; ++i)
						{
							state.noiseBuffer[i] = rng.frnd(2.0f) - 1.0f;
						}
					}
				}
				const float through = static_cast<float>(state.phase) /
					static_cast<float>(state.period);
				float sample = 0.0f;
				switch(sane.waveType)
				{
				case SfxWave::SW_SQUARE:
					sample = (through < state.squareDuty) ? 0.5f : -0.5f;
					break;
				case SfxWave::SW_SAW:
					sample = 1.0f - through * 2.0f;
					break;
				case SfxWave::SW_SINE:
					sample = std::sin(through * SFX_TWO_PI);
					break;
				case SfxWave::SW_NOISE:
				default:
					sample = state.noiseBuffer[
						(state.phase * SFX_NOISE_LENGTH) / state.period %
						SFX_NOISE_LENGTH];
					break;
				}

				// resonant low-pass (bypassed at the knob's "off" end)
				const float beforeLowpass = state.fltp;
				state.fltw *= state.fltwDelta;
				state.fltw = std::min(0.1f, std::max(0.0f, state.fltw));
				if(sane.lpfFreq != 1.0f)
				{
					state.fltdp += (sample - state.fltp) * state.fltw;
					state.fltdp -= state.fltdp * state.fltDamp;
				}
				else
				{
					state.fltp = sample;
					state.fltdp = 0.0f;
				}
				state.fltp += state.fltdp;

				// high-pass
				state.fltphp += state.fltp - beforeLowpass;
				state.fltphp -= state.fltphp * state.flthp;
				sample = state.fltphp;

				// the delay tap: mix the ring's delayed sample back in
				state.phaserBuffer[state.ipp & (SFX_PHASER_LENGTH - 1)] =
					sample;
				sample += state.phaserBuffer[
					(state.ipp - state.iphase + SFX_PHASER_LENGTH) &
					(SFX_PHASER_LENGTH - 1)];
				state.ipp = (state.ipp + 1) & (SFX_PHASER_LENGTH - 1);

				superSum += sample * state.envVolume;
			}

			float out = superSum / static_cast<float>(SfxSynth::SUPERSAMPLES) *
				sane.masterVolume * 2.0f * sane.soundVolume;
			// the standard's hard clamp: the punch stage deliberately drives
			// the mix into it (a filter can also ring), and a non-finite value
			// a pathological filter state could produce dies here too
			if(!std::isfinite(out))
			{
				out = 0.0f;
			}
			out = std::min(1.0f, std::max(-1.0f, out));
			mix.push_back(out);
		}

		pcm.samples.resize(mix.size());
		for(std::size_t i = 0; i < mix.size(); ++i)
		{
			pcm.samples[i] = static_cast<std::int16_t>(
				std::lround(mix[i] * 32000.0f));
		}
		return pcm;
	}
	//---------------------------------------------------------
	//--- the archetype generators (@see SfxDesc.h) ------------
	//---------------------------------------------------------
	namespace SfxPreset
	{
		SfxDesc forArchetype(Archetype archetype, unsigned int seed)
		{
			SfxDesc desc;
			desc.seed = seed;
			Rng rng(seed);

			switch(archetype)
			{
			case SA_PICKUP_COIN:
				desc.baseFreq = 0.4f + rng.frnd(0.5f);
				desc.attack = 0.0f;
				desc.sustain = rng.frnd(0.1f);
				desc.decay = 0.1f + rng.frnd(0.4f);
				desc.punch = 0.3f + rng.frnd(0.3f);
				if(rng.flip())
				{
					desc.arpSpeed = 0.5f + rng.frnd(0.2f);
					desc.arpMod = 0.2f + rng.frnd(0.4f);
				}
				break;

			case SA_LASER_SHOOT:
			{
				int wave = rng.rnd(2);
				if(wave == 2 && rng.flip())
				{
					wave = rng.rnd(1);
				}
				SfxWave::fromNumber(wave, desc.waveType);
				desc.baseFreq = 0.5f + rng.frnd(0.5f);
				desc.freqLimit = desc.baseFreq - 0.2f - rng.frnd(0.6f);
				desc.freqLimit = std::max(0.2f, desc.freqLimit);
				desc.freqRamp = -0.15f - rng.frnd(0.2f);
				if(rng.rnd(2) == 0)
				{
					desc.baseFreq = 0.3f + rng.frnd(0.6f);
					desc.freqLimit = rng.frnd(0.1f);
					desc.freqRamp = -0.35f - rng.frnd(0.3f);
				}
				if(rng.flip())
				{
					desc.duty = rng.frnd(0.5f);
					desc.dutyRamp = rng.frnd(0.2f);
				}
				else
				{
					desc.duty = 0.4f + rng.frnd(0.5f);
					desc.dutyRamp = -rng.frnd(0.7f);
				}
				desc.attack = 0.0f;
				desc.sustain = 0.1f + rng.frnd(0.2f);
				desc.decay = rng.frnd(0.4f);
				if(rng.flip())
				{
					desc.punch = rng.frnd(0.3f);
				}
				if(rng.rnd(2) == 0)
				{
					desc.phaserOffset = rng.frnd(0.2f);
					desc.phaserRamp = -rng.frnd(0.2f);
				}
				if(rng.flip())
				{
					desc.hpfFreq = rng.frnd(0.3f);
				}
				break;
			}

			case SA_EXPLOSION:
				desc.waveType = SfxWave::SW_NOISE;
				if(rng.flip())
				{
					desc.baseFreq = 0.1f + rng.frnd(0.4f);
					desc.freqRamp = -0.1f + rng.frnd(0.4f);
				}
				else
				{
					desc.baseFreq = 0.2f + rng.frnd(0.7f);
					desc.freqRamp = -0.2f - rng.frnd(0.2f);
				}
				desc.baseFreq *= desc.baseFreq;
				if(rng.rnd(4) == 0)
				{
					desc.freqRamp = 0.0f;
				}
				if(rng.rnd(2) == 0)
				{
					desc.repeatSpeed = 0.3f + rng.frnd(0.5f);
				}
				desc.attack = 0.0f;
				desc.sustain = 0.1f + rng.frnd(0.3f);
				desc.decay = rng.frnd(0.5f);
				if(rng.flip())
				{
					desc.phaserOffset = -0.3f + rng.frnd(0.9f);
					desc.phaserRamp = -rng.frnd(0.3f);
				}
				desc.punch = 0.2f + rng.frnd(0.6f);
				if(rng.flip())
				{
					desc.vibratoStrength = rng.frnd(0.7f);
					desc.vibratoSpeed = rng.frnd(0.6f);
				}
				if(rng.rnd(2) == 0)
				{
					desc.arpSpeed = 0.6f + rng.frnd(0.3f);
					desc.arpMod = 0.8f - rng.frnd(1.6f);
				}
				break;

			case SA_POWERUP:
				if(rng.flip())
				{
					desc.waveType = SfxWave::SW_SAW;
				}
				else
				{
					desc.duty = rng.frnd(0.6f);
				}
				if(rng.flip())
				{
					desc.baseFreq = 0.2f + rng.frnd(0.3f);
					desc.freqRamp = 0.1f + rng.frnd(0.4f);
					desc.repeatSpeed = 0.4f + rng.frnd(0.4f);
				}
				else
				{
					desc.baseFreq = 0.2f + rng.frnd(0.3f);
					desc.freqRamp = 0.05f + rng.frnd(0.2f);
					if(rng.flip())
					{
						desc.vibratoStrength = rng.frnd(0.7f);
						desc.vibratoSpeed = rng.frnd(0.6f);
					}
				}
				desc.attack = 0.0f;
				desc.sustain = rng.frnd(0.4f);
				desc.decay = 0.1f + rng.frnd(0.4f);
				break;

			case SA_HIT_HURT:
			{
				int wave = rng.rnd(2);
				if(wave == 2)
				{
					wave = 3;			// sine is not a hit; noise is
				}
				SfxWave::fromNumber(wave, desc.waveType);
				if(desc.waveType == SfxWave::SW_SQUARE)
				{
					desc.duty = rng.frnd(0.6f);
				}
				desc.baseFreq = 0.2f + rng.frnd(0.6f);
				desc.freqRamp = -0.3f - rng.frnd(0.4f);
				desc.attack = 0.0f;
				desc.sustain = rng.frnd(0.1f);
				desc.decay = 0.1f + rng.frnd(0.2f);
				if(rng.flip())
				{
					desc.hpfFreq = rng.frnd(0.3f);
				}
				break;
			}

			case SA_JUMP:
				desc.waveType = SfxWave::SW_SQUARE;
				desc.duty = rng.frnd(0.6f);
				desc.baseFreq = 0.3f + rng.frnd(0.3f);
				desc.freqRamp = 0.1f + rng.frnd(0.2f);
				desc.attack = 0.0f;
				desc.sustain = 0.1f + rng.frnd(0.3f);
				desc.decay = 0.1f + rng.frnd(0.2f);
				if(rng.flip())
				{
					desc.hpfFreq = rng.frnd(0.3f);
				}
				if(rng.flip())
				{
					desc.lpfFreq = 1.0f - rng.frnd(0.6f);
				}
				break;

			case SA_BLIP_SELECT:
			{
				const int wave = rng.rnd(1);
				SfxWave::fromNumber(wave, desc.waveType);
				if(desc.waveType == SfxWave::SW_SQUARE)
				{
					desc.duty = rng.frnd(0.6f);
				}
				desc.baseFreq = 0.2f + rng.frnd(0.4f);
				desc.attack = 0.0f;
				desc.sustain = 0.1f + rng.frnd(0.1f);
				desc.decay = rng.frnd(0.2f);
				desc.hpfFreq = 0.1f;
				break;
			}
			}

			// a generator can draw a value at the very edge of a range (and
			// the laser's freqLimit is a DIFFERENCE) - hand back something the
			// synthesizer accepts without a clamp note
			SfxSynth::sanitize(desc);
			// round every drawn value to the four decimals the TEXT form
			// writes, so a preset and its written-out form are the same sound
			// (a drawn value with more digits than the file can carry would
			// make the editor's Apply subtly change what it saved)
			float * const drawn[] = {
				&desc.soundVolume, &desc.masterVolume, &desc.baseFreq,
				&desc.freqLimit, &desc.freqRamp, &desc.freqDeltaRamp,
				&desc.duty, &desc.dutyRamp, &desc.vibratoStrength,
				&desc.vibratoSpeed, &desc.vibratoDelay, &desc.attack,
				&desc.sustain, &desc.decay, &desc.punch, &desc.lpfResonance,
				&desc.lpfFreq, &desc.lpfRamp, &desc.hpfFreq, &desc.hpfRamp,
				&desc.phaserOffset, &desc.phaserRamp, &desc.repeatSpeed,
				&desc.arpSpeed, &desc.arpMod };
			for(std::size_t i = 0; i < sizeof(drawn) / sizeof(drawn[0]); ++i)
			{
				*drawn[i] = std::round(*drawn[i] * 10000.0f) / 10000.0f;
			}
			return desc;
		}
	}
}
