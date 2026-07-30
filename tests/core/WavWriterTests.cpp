/**************************************************************
	created:	2026/07/29 at 14:10
	filename: 	WavWriterTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the RIFF/WAVE encoder: the header's every field is read
	back out of the produced bytes, the payload length matches the sample count,
	stereo doubles the block alignment, the samples survive byte for byte, and
	every bad argument is refused without touching the caller's buffer. Also
	encodes a REAL synthesized effect, which is what the editor's Export WAV
	writes. Pure - no audio device, no file needed.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/SfxSynth.h"
#include "core_util/WavWriter.h"

#include <vector>

using namespace Orkige;

namespace
{
	//! read a little-endian 32-bit field out of the encoded stream
	unsigned int u32At(std::vector<unsigned char> const & bytes,
		std::size_t at)
	{
		return static_cast<unsigned int>(bytes[at]) |
			(static_cast<unsigned int>(bytes[at + 1]) << 8) |
			(static_cast<unsigned int>(bytes[at + 2]) << 16) |
			(static_cast<unsigned int>(bytes[at + 3]) << 24);
	}
	//! read a little-endian 16-bit field out of the encoded stream
	unsigned int u16At(std::vector<unsigned char> const & bytes,
		std::size_t at)
	{
		return static_cast<unsigned int>(bytes[at]) |
			(static_cast<unsigned int>(bytes[at + 1]) << 8);
	}
	//! is the four-byte tag at @p at the expected one?
	bool tagAt(std::vector<unsigned char> const & bytes, std::size_t at,
		char const * tag)
	{
		for(int i = 0; i < 4; ++i)
		{
			if(bytes[at + i] != static_cast<unsigned char>(tag[i]))
			{
				return false;
			}
		}
		return true;
	}
}

TEST_CASE("wav_encode_mono_header_and_payload", "[unit][sfx]")
{
	std::vector<std::int16_t> samples;
	samples.push_back(0);
	samples.push_back(1000);
	samples.push_back(-1000);
	samples.push_back(32000);
	samples.push_back(-32000);

	std::vector<unsigned char> wav;
	REQUIRE(WavWriter::encode(samples, 22050, 1, wav));

	// the header is a fixed prologue in front of the samples
	REQUIRE(wav.size() == WavWriter::HEADER_BYTES + samples.size() * 2u);
	CHECK(WavWriter::HEADER_BYTES == 44u);
	CHECK(tagAt(wav, 0, "RIFF"));
	// the RIFF size counts everything after that field
	CHECK(u32At(wav, 4) == wav.size() - 8u);
	CHECK(tagAt(wav, 8, "WAVE"));
	CHECK(tagAt(wav, 12, "fmt "));
	CHECK(u32At(wav, 16) == 16u);		// an uncompressed-PCM format chunk
	CHECK(u16At(wav, 20) == 1u);		// format 1 = PCM
	CHECK(u16At(wav, 22) == 1u);		// mono
	CHECK(u32At(wav, 24) == 22050u);	// the rate the caller passed
	CHECK(u32At(wav, 28) == 22050u * 2u);	// byte rate = rate * blockAlign
	CHECK(u16At(wav, 32) == 2u);		// blockAlign = channels * 2 bytes
	CHECK(u16At(wav, 34) == 16u);		// 16 bits per sample
	CHECK(tagAt(wav, 36, "data"));
	CHECK(u32At(wav, 40) == samples.size() * 2u);

	// the samples themselves survive, little-endian, in order
	for(std::size_t i = 0; i < samples.size(); ++i)
	{
		const unsigned int stored = u16At(wav,
			WavWriter::HEADER_BYTES + i * 2u);
		CHECK(static_cast<std::int16_t>(
			static_cast<unsigned short>(stored)) == samples[i]);
	}
}

TEST_CASE("wav_encode_stereo_alignment", "[unit][sfx]")
{
	// four interleaved values = two frames
	std::vector<std::int16_t> samples;
	samples.push_back(100);
	samples.push_back(-100);
	samples.push_back(200);
	samples.push_back(-200);

	std::vector<unsigned char> wav;
	REQUIRE(WavWriter::encode(samples, 44100, 2, wav));
	CHECK(u16At(wav, 22) == 2u);			// stereo
	CHECK(u16At(wav, 32) == 4u);			// blockAlign doubles
	CHECK(u32At(wav, 28) == 44100u * 4u);	// so does the byte rate
	CHECK(u32At(wav, 40) == 8u);			// two frames of four bytes
	CHECK(wav.size() == WavWriter::HEADER_BYTES + 8u);
}

TEST_CASE("wav_encode_refuses_bad_arguments", "[unit][sfx]")
{
	std::vector<std::int16_t> samples;
	samples.push_back(1);
	samples.push_back(2);

	// a refusal leaves the caller's buffer exactly as it was
	std::vector<unsigned char> out;
	out.push_back(0xAAu);
	CHECK_FALSE(WavWriter::encode(std::vector<std::int16_t>(), 44100, 1, out));
	CHECK_FALSE(WavWriter::encode(samples, 0, 1, out));
	CHECK_FALSE(WavWriter::encode(samples, -44100, 1, out));
	CHECK_FALSE(WavWriter::encode(samples, 44100, 0, out));
	CHECK_FALSE(WavWriter::encode(samples, 44100, 3, out));
	CHECK(out.size() == 1u);
	CHECK(out[0] == 0xAAu);

	// an odd sample count is not a whole number of stereo frames
	std::vector<std::int16_t> odd = samples;
	odd.push_back(3);
	CHECK_FALSE(WavWriter::encode(odd, 44100, 2, out));
	CHECK(out.size() == 1u);
}

TEST_CASE("wav_encode_a_synthesized_effect", "[unit][sfx]")
{
	// what the editor's Export WAV writes: the synthesizer's own samples at
	// the synthesizer's own rate
	const SfxSynth::Pcm pcm =
		SfxSynth::render(SfxPreset::forArchetype(SfxPreset::SA_PICKUP_COIN));
	REQUIRE(pcm.samples.size() > 1u);

	std::vector<unsigned char> wav;
	REQUIRE(WavWriter::encode(pcm.samples, pcm.sampleRate, pcm.channels, wav));
	CHECK(wav.size() == WavWriter::HEADER_BYTES + pcm.byteSize());
	CHECK(u32At(wav, 24) == static_cast<unsigned int>(pcm.sampleRate));
	CHECK(u32At(wav, 24) == 44100u);
	CHECK(u16At(wav, 22) == 1u);
	CHECK(u32At(wav, 40) == static_cast<unsigned int>(pcm.byteSize()));
}
