/**************************************************************
	created:	2026/07/29 at 10:40
	filename: 	SfxAssetTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the two carriers of the ONE sound parameter model: the
	standard BINARY parameter file (`.sfs`, all three format versions, laid out
	byte by byte here so the reader is checked against the documented layout
	rather than against itself) and the `.osfx` TEXT twin (every directive
	round-trips, `preset` seeds and explicit directives override it REGARDLESS
	of line order, and every malformation fails with a line-numbered error while
	leaving the caller's description untouched). Pure - no audio device.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/SfxAsset.h"
#include "core_util/SfxSynth.h"

#include <cstring>
#include <vector>

using namespace Orkige;
using Catch::Approx;

namespace
{
	//! @brief a little-endian byte builder - the test lays the binary
	//! parameter file out ITSELF, field by field, from the documented layout
	struct ByteBuilder
	{
		std::vector<unsigned char> bytes;

		void i32(int value)
		{
			const unsigned int bits = static_cast<unsigned int>(value);
			this->bytes.push_back(static_cast<unsigned char>(bits & 0xFFu));
			this->bytes.push_back(static_cast<unsigned char>((bits >> 8) & 0xFFu));
			this->bytes.push_back(static_cast<unsigned char>((bits >> 16) & 0xFFu));
			this->bytes.push_back(static_cast<unsigned char>((bits >> 24) & 0xFFu));
		}
		void f32(float value)
		{
			unsigned int bits = 0u;
			std::memcpy(&bits, &value, sizeof(bits));
			this->i32(static_cast<int>(bits));
		}
		void byte(unsigned char value)
		{
			this->bytes.push_back(value);
		}
	};

	//! @brief lay out one complete parameter file of @p version.
	//! The field ORDER is the format's: version, wave, [volume], the three
	//! frequency fields, [delta slide], duty pair, vibrato triple, envelope
	//! quad, the one-byte filter flag, five filter fields, delay-tap pair,
	//! retrigger, [arpeggio pair]. Values are distinct decimals so a
	//! mis-mapped field is impossible to miss.
	std::vector<unsigned char> buildParameterFile(int version)
	{
		ByteBuilder out;
		out.i32(version);
		out.i32(1);						// wave type 1 = saw
		if(version >= 102)
		{
			out.f32(0.55f);				// sound volume
		}
		out.f32(0.61f);					// base frequency
		out.f32(0.12f);					// frequency limit
		out.f32(0.23f);					// slide
		if(version >= 101)
		{
			out.f32(-0.34f);			// delta slide
		}
		out.f32(0.45f);					// duty
		out.f32(-0.56f);				// duty sweep
		out.f32(0.67f);					// vibrato strength
		out.f32(0.78f);					// vibrato speed
		out.f32(0.89f);					// vibrato delay
		out.f32(0.11f);					// attack
		out.f32(0.22f);					// sustain
		out.f32(0.33f);					// decay
		out.f32(0.44f);					// punch
		out.byte(1u);					// the filter flag: ONE byte, no padding
		out.f32(0.13f);					// low-pass resonance
		out.f32(0.24f);					// low-pass cutoff
		out.f32(-0.35f);				// low-pass sweep
		out.f32(0.46f);					// high-pass cutoff
		out.f32(-0.57f);				// high-pass sweep
		out.f32(0.68f);					// delay-tap offset
		out.f32(-0.79f);				// delay-tap sweep
		out.f32(0.15f);					// retrigger
		if(version >= 101)
		{
			out.f32(0.26f);				// arpeggio speed
			out.f32(-0.37f);			// arpeggio amount
		}
		return out.bytes;
	}
}

TEST_CASE("sfx_binary_reads_the_standard_layout", "[unit][sfx]")
{
	const std::vector<unsigned char> file = buildParameterFile(102);
	// version 102: two ints + 24 floats + the one-byte flag, no padding
	CHECK(file.size() == 8u + 24u * 4u + 1u);

	SfxDesc desc;
	String error;
	REQUIRE(SfxAsset::parseBinary(file.data(), file.size(), desc, &error));
	CHECK(error.empty());
	CHECK(desc.waveType == SfxWave::SW_SAW);
	CHECK(desc.soundVolume == Approx(0.55f));
	CHECK(desc.baseFreq == Approx(0.61f));
	CHECK(desc.freqLimit == Approx(0.12f));
	CHECK(desc.freqRamp == Approx(0.23f));
	CHECK(desc.freqDeltaRamp == Approx(-0.34f));
	CHECK(desc.duty == Approx(0.45f));
	CHECK(desc.dutyRamp == Approx(-0.56f));
	CHECK(desc.vibratoStrength == Approx(0.67f));
	CHECK(desc.vibratoSpeed == Approx(0.78f));
	CHECK(desc.vibratoDelay == Approx(0.89f));
	CHECK(desc.attack == Approx(0.11f));
	CHECK(desc.sustain == Approx(0.22f));
	CHECK(desc.decay == Approx(0.33f));
	CHECK(desc.punch == Approx(0.44f));
	CHECK(desc.filterOn);
	CHECK(desc.lpfResonance == Approx(0.13f));
	CHECK(desc.lpfFreq == Approx(0.24f));
	CHECK(desc.lpfRamp == Approx(-0.35f));
	CHECK(desc.hpfFreq == Approx(0.46f));
	CHECK(desc.hpfRamp == Approx(-0.57f));
	CHECK(desc.phaserOffset == Approx(0.68f));
	CHECK(desc.phaserRamp == Approx(-0.79f));
	CHECK(desc.repeatSpeed == Approx(0.15f));
	CHECK(desc.arpSpeed == Approx(0.26f));
	CHECK(desc.arpMod == Approx(-0.37f));
	// fields the format does not carry keep their defaults, so an imported
	// sound plays at a usable volume and renders reproducibly
	CHECK(desc.masterVolume == Approx(SfxDesc().masterVolume));
	CHECK(desc.seed == SfxDesc().seed);

	// ... and it renders (the whole point of reading the file)
	CHECK(SfxSynth::render(desc).samples.size() > 1u);
}

TEST_CASE("sfx_binary_reads_the_older_format_versions", "[unit][sfx]")
{
	SECTION("version 101 carries no per-sound volume")
	{
		const std::vector<unsigned char> file = buildParameterFile(101);
		CHECK(file.size() == 8u + 23u * 4u + 1u);
		SfxDesc desc;
		REQUIRE(SfxAsset::parseBinary(file.data(), file.size(), desc, nullptr));
		CHECK(desc.soundVolume == Approx(SfxDesc().soundVolume));	// default
		CHECK(desc.baseFreq == Approx(0.61f));		// everything else lands
		CHECK(desc.freqDeltaRamp == Approx(-0.34f));
		CHECK(desc.arpSpeed == Approx(0.26f));
		CHECK(desc.arpMod == Approx(-0.37f));
	}
	SECTION("version 100 carries no delta slide and no arpeggio either")
	{
		const std::vector<unsigned char> file = buildParameterFile(100);
		CHECK(file.size() == 8u + 20u * 4u + 1u);
		SfxDesc desc;
		REQUIRE(SfxAsset::parseBinary(file.data(), file.size(), desc, nullptr));
		CHECK(desc.freqDeltaRamp == Approx(0.0f));
		CHECK(desc.arpSpeed == Approx(0.0f));
		CHECK(desc.arpMod == Approx(0.0f));
		// the fields it DOES carry still map correctly (the version gates sit
		// in the middle of the layout, so a wrong gate shifts everything)
		CHECK(desc.duty == Approx(0.45f));
		CHECK(desc.punch == Approx(0.44f));
		CHECK(desc.repeatSpeed == Approx(0.15f));
	}
}

TEST_CASE("sfx_binary_malformed_verdicts", "[unit][sfx]")
{
	SfxDesc untouched;
	untouched.baseFreq = 0.123f;
	String error;

	SECTION("an unsupported version")
	{
		std::vector<unsigned char> file = buildParameterFile(102);
		file[0] = 99u;					// version 99
		SfxDesc desc = untouched;
		CHECK_FALSE(SfxAsset::parseBinary(file.data(), file.size(), desc,
			&error));
		CHECK(error.find("version") != String::npos);
		CHECK(desc.baseFreq == Approx(untouched.baseFreq));
	}
	SECTION("a wave number the format does not define")
	{
		std::vector<unsigned char> file = buildParameterFile(102);
		file[4] = 9u;					// wave type 9
		SfxDesc desc = untouched;
		CHECK_FALSE(SfxAsset::parseBinary(file.data(), file.size(), desc,
			&error));
		CHECK(error.find("wave") != String::npos);
		CHECK(desc.baseFreq == Approx(untouched.baseFreq));
	}
	SECTION("a truncated file")
	{
		std::vector<unsigned char> file = buildParameterFile(102);
		file.resize(file.size() - 6u);
		SfxDesc desc = untouched;
		CHECK_FALSE(SfxAsset::parseBinary(file.data(), file.size(), desc,
			&error));
		CHECK(error.find("truncated") != String::npos);
		CHECK(desc.baseFreq == Approx(untouched.baseFreq));
	}
	SECTION("no bytes at all")
	{
		SfxDesc desc = untouched;
		CHECK_FALSE(SfxAsset::parseBinary(nullptr, 0u, desc, &error));
		CHECK_FALSE(error.empty());
		const unsigned char stub[4] = { 0u, 0u, 0u, 0u };
		CHECK_FALSE(SfxAsset::parseBinary(stub, sizeof(stub), desc, &error));
		CHECK(desc.baseFreq == Approx(untouched.baseFreq));
	}
	SECTION("a WAV file is not a parameter file")
	{
		// the honest refusal for the mistake a designer WILL make
		const unsigned char wavHeader[16] = {
			'R', 'I', 'F', 'F', 0x24, 0x08, 0x00, 0x00,
			'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' };
		SfxDesc desc = untouched;
		CHECK_FALSE(SfxAsset::parseBinary(wavHeader, sizeof(wavHeader), desc,
			&error));
		CHECK_FALSE(error.empty());
		CHECK(desc.baseFreq == Approx(untouched.baseFreq));
	}
}

TEST_CASE("sfx_text_parse_every_directive", "[unit][sfx]")
{
	const String text =
		"# every standard parameter, one per line\n"
		"version 1\n"
		"wave saw\n"
		"soundVolume 0.55\n"
		"masterVolume 0.9\n"
		"baseFreq 0.61        # a trailing comment\n"
		"freqLimit 0.12\n"
		"freqRamp 0.23\n"
		"freqDeltaRamp -0.34\n"
		"duty 0.45\n"
		"dutyRamp -0.56\n"
		"vibratoStrength 0.67\n"
		"vibratoSpeed 0.78\n"
		"vibratoDelay 0.89\n"
		"attack 0.11\n"
		"sustain 0.22\n"
		"decay 0.33\n"
		"punch 0.44\n"
		"filter 1\n"
		"lpfFreq 0.24\n"
		"lpfRamp -0.35\n"
		"lpfResonance 0.13\n"
		"hpfFreq 0.46\n"
		"hpfRamp -0.57\n"
		"phaserOffset 0.68\n"
		"phaserRamp -0.79\n"
		"repeatSpeed 0.15\n"
		"arpSpeed 0.26\n"
		"arpMod -0.37\n"
		"seed 4242\n";

	SfxDesc desc;
	String error;
	REQUIRE(SfxAsset::parse(text, desc, &error));
	CHECK(error.empty());
	CHECK(desc.waveType == SfxWave::SW_SAW);
	CHECK(desc.soundVolume == Approx(0.55f));
	CHECK(desc.masterVolume == Approx(0.9f));
	CHECK(desc.baseFreq == Approx(0.61f));
	CHECK(desc.freqLimit == Approx(0.12f));
	CHECK(desc.freqRamp == Approx(0.23f));
	CHECK(desc.freqDeltaRamp == Approx(-0.34f));
	CHECK(desc.duty == Approx(0.45f));
	CHECK(desc.dutyRamp == Approx(-0.56f));
	CHECK(desc.vibratoStrength == Approx(0.67f));
	CHECK(desc.vibratoSpeed == Approx(0.78f));
	CHECK(desc.vibratoDelay == Approx(0.89f));
	CHECK(desc.attack == Approx(0.11f));
	CHECK(desc.sustain == Approx(0.22f));
	CHECK(desc.decay == Approx(0.33f));
	CHECK(desc.punch == Approx(0.44f));
	CHECK(desc.filterOn);
	CHECK(desc.lpfFreq == Approx(0.24f));
	CHECK(desc.lpfRamp == Approx(-0.35f));
	CHECK(desc.lpfResonance == Approx(0.13f));
	CHECK(desc.hpfFreq == Approx(0.46f));
	CHECK(desc.hpfRamp == Approx(-0.57f));
	CHECK(desc.phaserOffset == Approx(0.68f));
	CHECK(desc.phaserRamp == Approx(-0.79f));
	CHECK(desc.repeatSpeed == Approx(0.15f));
	CHECK(desc.arpSpeed == Approx(0.26f));
	CHECK(desc.arpMod == Approx(-0.37f));
	CHECK(desc.seed == 4242u);
}

TEST_CASE("sfx_text_carries_the_same_model_as_the_binary", "[unit][sfx]")
{
	// the acid test of "ONE model, two codecs": a binary file, serialized to
	// text and read back, is the SAME sound
	const std::vector<unsigned char> file = buildParameterFile(102);
	SfxDesc fromBinary;
	REQUIRE(SfxAsset::parseBinary(file.data(), file.size(), fromBinary,
		nullptr));
	const String text = SfxAsset::serialize(fromBinary);
	SfxDesc fromText;
	String error;
	REQUIRE(SfxAsset::parse(text, fromText, &error));
	CHECK(error.empty());
	CHECK(SfxSynth::render(fromText).samples ==
		SfxSynth::render(fromBinary).samples);
}

TEST_CASE("sfx_text_minimal_and_defaults", "[unit][sfx]")
{
	SfxDesc desc;
	String error;
	REQUIRE(SfxAsset::parse("baseFreq 0.8\n", desc, &error));
	CHECK(desc.baseFreq == Approx(0.8f));
	CHECK(desc.waveType == SfxWave::SW_SQUARE);		// the standard's default
	CHECK(desc.sustain == Approx(0.3f));
	CHECK(desc.decay == Approx(0.4f));

	// even a bare version line is a sound (an audible default beats silence)
	SfxDesc bare;
	REQUIRE(SfxAsset::parse("version 1\n", bare, nullptr));
	CHECK(bare.baseFreq == Approx(0.3f));
	CHECK(SfxSynth::render(bare).samples.size() > 1u);
}

TEST_CASE("sfx_preset_seeds_and_explicit_keys_override", "[unit][sfx]")
{
	const SfxDesc coin = SfxPreset::forArchetype(SfxPreset::SA_PICKUP_COIN);

	SECTION("preset alone seeds every parameter")
	{
		SfxDesc desc;
		REQUIRE(SfxAsset::parse("preset coin\n", desc, nullptr));
		CHECK(SfxSynth::render(desc).samples ==
			SfxSynth::render(coin).samples);
	}
	SECTION("an explicit directive wins over the seed")
	{
		SfxDesc desc;
		REQUIRE(SfxAsset::parse("preset coin\nsoundVolume 0.2\nwave noise\n",
			desc, nullptr));
		CHECK(desc.soundVolume == Approx(0.2f));		// overridden
		CHECK(desc.waveType == SfxWave::SW_NOISE);		// overridden
		CHECK(desc.baseFreq == Approx(coin.baseFreq));	// still seeded
		CHECK(desc.punch == Approx(coin.punch));		// still seeded
	}
	SECTION("precedence is STRUCTURAL, not textual: order cannot change it")
	{
		// the `preset` line LAST must behave exactly like `preset` first -
		// otherwise a file's meaning would depend on how its lines are sorted
		SfxDesc presetFirst;
		SfxDesc presetLast;
		REQUIRE(SfxAsset::parse("preset coin\nsoundVolume 0.2\n", presetFirst,
			nullptr));
		REQUIRE(SfxAsset::parse("soundVolume 0.2\npreset coin\n", presetLast,
			nullptr));
		CHECK(presetLast.soundVolume == Approx(0.2f));
		CHECK(SfxSynth::render(presetFirst).samples ==
			SfxSynth::render(presetLast).samples);
	}
	SECTION("a seed line reaches the GENERATOR, wherever it sits")
	{
		// the seed picks WHICH member of the archetype's family the file names,
		// so it must be resolved before the preset is drawn - from either side
		SfxDesc seedFirst;
		SfxDesc seedLast;
		REQUIRE(SfxAsset::parse("seed 9\npreset explosion\n", seedFirst,
			nullptr));
		REQUIRE(SfxAsset::parse("preset explosion\nseed 9\n", seedLast,
			nullptr));
		const SfxDesc expected =
			SfxPreset::forArchetype(SfxPreset::SA_EXPLOSION, 9u);
		CHECK(SfxSynth::render(seedFirst).samples ==
			SfxSynth::render(expected).samples);
		CHECK(SfxSynth::render(seedLast).samples ==
			SfxSynth::render(expected).samples);
		// ... and a different seed is a different member
		SfxDesc otherMember;
		REQUIRE(SfxAsset::parse("preset explosion\nseed 10\n", otherMember,
			nullptr));
		CHECK(SfxSynth::render(otherMember).samples !=
			SfxSynth::render(expected).samples);
	}
	SECTION("the standard's alternative generator names resolve the same")
	{
		SfxDesc named;
		SfxDesc synonym;
		REQUIRE(SfxAsset::parse("preset coin\n", named, nullptr));
		REQUIRE(SfxAsset::parse("preset pickup\n", synonym, nullptr));
		CHECK(SfxSynth::render(named).samples ==
			SfxSynth::render(synonym).samples);
	}
}

TEST_CASE("sfx_text_malformed_verdicts", "[unit][sfx]")
{
	// a marker description the parser must NOT touch on failure - a failed
	// hot-reload keeps the sound that is already loaded
	SfxDesc untouched;
	untouched.baseFreq = 0.123f;
	untouched.soundVolume = 0.33f;
	String error;

	const auto refuses = [&](String const & text, String const & because)
	{
		SfxDesc desc = untouched;
		error.clear();
		INFO(because << " <- " << text);
		CHECK_FALSE(SfxAsset::parse(text, desc, &error));
		CHECK(error.find("line ") == 0u);			// always line-numbered
		CHECK(desc.baseFreq == Approx(untouched.baseFreq));
		CHECK(desc.soundVolume == Approx(untouched.soundVolume));
	};

	refuses("", "empty text");
	refuses("# nothing but a comment\n\n", "comment-only text");
	refuses("wobble 3\n", "unknown directive");
	refuses("bitcrush 4\n", "a parameter the standard model does not have");
	refuses("baseFreq 0.5\nbaseFreq 0.6\n", "duplicate directive");
	refuses("wave bagpipe\n", "unknown wave name");
	refuses("wave\n", "wave without a value");
	refuses("wave saw sine\n", "wave with two values");
	refuses("preset trombone\n", "unknown preset name");
	refuses("preset\n", "preset without a value");
	refuses("baseFreq\n", "a directive without its value");
	refuses("baseFreq soon\n", "a non-numeric value");
	refuses("baseFreq 0.5 0.6\n", "two values for one parameter");
	refuses("filter 2\n", "a filter flag that is neither 0 nor 1");
	refuses("filter yes\n", "a non-numeric filter flag");
	refuses("seed -1\n", "a negative seed");
	refuses("seed 1.5\n", "a fractional seed");
	refuses("version 2\n", "an unsupported version");
	refuses("version one\n", "a non-numeric version");

	// the error names the offending LINE, not just the problem
	SfxDesc desc;
	CHECK_FALSE(SfxAsset::parse("version 1\nbaseFreq 0.5\nwobble 1\n", desc,
		&error));
	CHECK(error.find("line 3") == 0u);
	CHECK(error.find("wobble") != String::npos);
}

TEST_CASE("sfx_out_of_range_values_parse_then_clamp", "[unit][sfx]")
{
	// THE division of labour: the grammar accepts a number, the synthesizer
	// clamps it into the standard's range and names what it clamped
	SfxDesc desc;
	String error;
	REQUIRE(SfxAsset::parse("baseFreq 9\nsoundVolume 4\nfreqRamp -7\n", desc,
		&error));
	CHECK(error.empty());
	CHECK(desc.baseFreq == Approx(9.0f));		// the parser passes it through
	CHECK(desc.soundVolume == Approx(4.0f));
	CHECK(desc.freqRamp == Approx(-7.0f));

	std::vector<String> notes;
	CHECK_FALSE(SfxSynth::sanitize(desc, &notes));
	CHECK(desc.baseFreq == Approx(1.0f));
	CHECK(desc.soundVolume == Approx(1.0f));
	CHECK(desc.freqRamp == Approx(-1.0f));
	CHECK(notes.size() >= 3u);
}

TEST_CASE("sfx_text_serialize_roundtrips", "[unit][sfx]")
{
	// every archetype survives serialize -> parse as the SAME sound (the
	// editor's Apply path, and what an imported binary file becomes as text)
	for(int i = 0; i < SfxPreset::ARCHETYPE_COUNT; ++i)
	{
		const SfxPreset::Archetype archetype =
			static_cast<SfxPreset::Archetype>(i);
		const SfxDesc original = SfxPreset::forArchetype(archetype, 3u);
		const String text = SfxAsset::serialize(original);
		INFO("archetype " << SfxPreset::archetypeName(archetype)
			<< " serialized to:\n" << text);
		CHECK(text.find("version 1") != String::npos);
		// a serialized effect carries RESOLVED numbers, never a preset line
		CHECK(text.find("preset ") == String::npos);

		SfxDesc reparsed;
		String error;
		REQUIRE(SfxAsset::parse(text, reparsed, &error));
		CHECK(SfxSynth::render(reparsed).samples ==
			SfxSynth::render(original).samples);
	}
}

TEST_CASE("sfx_text_serialize_defaults_are_minimal", "[unit][sfx]")
{
	const SfxDesc defaults;
	const String text = SfxAsset::serialize(defaults);
	CHECK(text.find("wave") == String::npos);
	CHECK(text.find("baseFreq") == String::npos);
	CHECK(text.find("soundVolume") == String::npos);
	CHECK(text.find("seed") == String::npos);

	SfxDesc reparsed;
	REQUIRE(SfxAsset::parse(text, reparsed, nullptr));
	CHECK(reparsed.baseFreq == Approx(defaults.baseFreq));
	CHECK(reparsed.sustain == Approx(defaults.sustain));
}

TEST_CASE("sfx_template_is_a_tunable_starting_point", "[unit][sfx]")
{
	// what the editor's Create > Sound writes: a commented file with the
	// archetype's own numbers spelled out, which parses to that archetype
	for(int i = 0; i < SfxPreset::ARCHETYPE_COUNT; ++i)
	{
		const SfxPreset::Archetype archetype =
			static_cast<SfxPreset::Archetype>(i);
		const String text = SfxAsset::templateFor(archetype);
		INFO("template for " << SfxPreset::archetypeName(archetype)
			<< ":\n" << text);
		CHECK(text.find("# ") == 0u);					// starts with a comment
		CHECK(text.find("preset ") != String::npos);	// names its archetype
		// EVERY parameter is spelled out - a value a designer cannot see is a
		// value they cannot tune
		CHECK(text.find("phaserOffset ") != String::npos);
		CHECK(text.find("arpMod ") != String::npos);

		SfxDesc desc;
		String error;
		REQUIRE(SfxAsset::parse(text, desc, &error));
		CHECK(error.empty());
		const SfxDesc expected = SfxPreset::forArchetype(archetype);
		CHECK(SfxSynth::render(desc).samples ==
			SfxSynth::render(expected).samples);
	}
	// a seeded template names that family member explicitly
	const String seeded = SfxAsset::templateFor(SfxPreset::SA_JUMP, 12u);
	CHECK(seeded.find("seed 12") != String::npos);
	SfxDesc desc;
	REQUIRE(SfxAsset::parse(seeded, desc, nullptr));
	CHECK(SfxSynth::render(desc).samples ==
		SfxSynth::render(SfxPreset::forArchetype(SfxPreset::SA_JUMP, 12u))
			.samples);
}

TEST_CASE("sfx_extension_probes", "[unit][sfx]")
{
	CHECK(SfxAsset::isSfxName("assets/coin.osfx"));
	CHECK(SfxAsset::isSfxName("assets/coin.sfs"));
	CHECK(SfxAsset::isSfxName("COIN.OSFX"));		// case-insensitive
	CHECK(SfxAsset::isSfxName("COIN.SFS"));
	CHECK(SfxAsset::isTextName("coin.osfx"));
	CHECK_FALSE(SfxAsset::isTextName("coin.sfs"));
	CHECK(SfxAsset::isBinaryName("coin.sfs"));
	CHECK_FALSE(SfxAsset::isBinaryName("coin.osfx"));
	CHECK_FALSE(SfxAsset::isSfxName("assets/coin.wav"));
	CHECK_FALSE(SfxAsset::isSfxName("osfx"));
	CHECK_FALSE(SfxAsset::isSfxName(""));
}
