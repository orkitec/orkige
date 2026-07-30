/**************************************************************
	created:	2026/07/29 at 14:00
	filename: 	WavWriter.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file WavWriter.cpp
//! @brief the minimal RIFF/WAVE encoder (@see WavWriter.h)

#include "core_util/WavWriter.h"

namespace Orkige
{
	const std::size_t WavWriter::HEADER_BYTES = 44u;

	namespace
	{
		//! append the four ASCII bytes of a RIFF chunk tag
		void putTag(std::vector<unsigned char> & out, char const * tag)
		{
			for(int i = 0; i < 4; ++i)
			{
				out.push_back(static_cast<unsigned char>(tag[i]));
			}
		}
		//! append a 32-bit field, little-endian (the format's byte order)
		void putU32(std::vector<unsigned char> & out, unsigned int value)
		{
			out.push_back(static_cast<unsigned char>(value & 0xFFu));
			out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
			out.push_back(static_cast<unsigned char>((value >> 16) & 0xFFu));
			out.push_back(static_cast<unsigned char>((value >> 24) & 0xFFu));
		}
		//! append a 16-bit field, little-endian
		void putU16(std::vector<unsigned char> & out, unsigned int value)
		{
			out.push_back(static_cast<unsigned char>(value & 0xFFu));
			out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
		}
	}
	//---------------------------------------------------------
	bool WavWriter::encode(std::vector<std::int16_t> const & samples,
		int sampleRate, int channels, std::vector<unsigned char> & out)
	{
		if(samples.empty() || sampleRate <= 0 ||
			(channels != 1 && channels != 2))
		{
			return false;
		}
		if((samples.size() % static_cast<std::size_t>(channels)) != 0u)
		{
			return false;	// a partial frame is a caller bug, not a file
		}

		const unsigned int bitsPerSample = 16u;
		const unsigned int channelCount = static_cast<unsigned int>(channels);
		const unsigned int rate = static_cast<unsigned int>(sampleRate);
		const unsigned int blockAlign = channelCount * (bitsPerSample / 8u);
		const unsigned int byteRate = rate * blockAlign;
		const unsigned int dataBytes = static_cast<unsigned int>(
			samples.size() * sizeof(std::int16_t));

		out.reserve(out.size() + WavWriter::HEADER_BYTES + dataBytes);
		// --- the RIFF container ---
		putTag(out, "RIFF");
		// everything after this field: the 4-byte "WAVE" tag + the two chunks
		putU32(out, 36u + dataBytes);
		putTag(out, "WAVE");
		// --- the format chunk (16 bytes, uncompressed PCM) ---
		putTag(out, "fmt ");
		putU32(out, 16u);
		putU16(out, 1u);				// format 1 = PCM, no compression
		putU16(out, channelCount);
		putU32(out, rate);
		putU32(out, byteRate);
		putU16(out, blockAlign);
		putU16(out, bitsPerSample);
		// --- the sample chunk ---
		putTag(out, "data");
		putU32(out, dataBytes);
		for(std::size_t i = 0; i < samples.size(); ++i)
		{
			// 16-bit samples ride little-endian too, so the stream is
			// byte-identical on a big-endian host
			putU16(out, static_cast<unsigned int>(
				static_cast<unsigned short>(samples[i])));
		}
		return true;
	}
}
