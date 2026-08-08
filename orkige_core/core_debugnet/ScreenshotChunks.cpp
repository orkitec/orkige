/********************************************************************
	created:	Saturday 2026/08/08 at 10:20
	filename: 	ScreenshotChunks.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_debugnet/ScreenshotChunks.h"
#include "core_util/Base64.h"

#include <string>

namespace Orkige
{
	namespace
	{
		//! base64 characters per line. MAX_LINE_LENGTH is 64 KiB and the rest
		//! of the line is the JSON envelope plus the echoed capture path, so a
		//! 32 KiB payload leaves half the budget for them - a margin no path
		//! length a filesystem accepts can eat through.
		const unsigned int CHUNK_PAYLOAD_CHARS = 32u * 1024u;
		//! the decoded-image ceiling: a window capture at any device size this
		//! engine renders fits far inside it, and a peer claiming more is
		//! refused before anything is buffered
		const std::size_t MAX_IMAGE_BYTES = 32u * 1024u * 1024u;
		//! chunk count ceiling, derived from the two constants above so the two
		//! can never disagree (a base64 quantum carries 3 bytes per 4 chars)
		unsigned int maxChunkCount()
		{
			const std::size_t bytesPerChunk =
				(static_cast<std::size_t>(CHUNK_PAYLOAD_CHARS) / 4u) * 3u;
			return static_cast<unsigned int>(
				(MAX_IMAGE_BYTES + bytesPerChunk - 1u) / bytesPerChunk);
		}
	}
	//---------------------------------------------------------
	unsigned int screenshotChunkPayloadChars()
	{
		return CHUNK_PAYLOAD_CHARS;
	}
	//---------------------------------------------------------
	std::size_t screenshotMaxImageBytes()
	{
		return MAX_IMAGE_BYTES;
	}
	//---------------------------------------------------------
	StringVector splitScreenshotChunks(unsigned char const * data,
		std::size_t length)
	{
		StringVector chunks;
		if (data == NULL || length == 0)
		{
			return chunks;
		}
		// slice the RAW bytes on a base64 quantum boundary (3 bytes -> 4
		// characters), so every chunk but the last encodes without padding and
		// the concatenation of the payloads is itself valid base64
		const std::size_t bytesPerChunk =
			(static_cast<std::size_t>(CHUNK_PAYLOAD_CHARS) / 4u) * 3u;
		for (std::size_t offset = 0; offset < length; offset += bytesPerChunk)
		{
			const std::size_t take = length - offset < bytesPerChunk
				? length - offset : bytesPerChunk;
			chunks.push_back(Base64::encode(data + offset, take));
		}
		return chunks;
	}
	//---------------------------------------------------------
	void ScreenshotChunkAssembler::reset()
	{
		this->mPath.clear();
		this->mTotal = 0;
		this->mNext = 0;
		this->mActive = false;
		this->mBytes.clear();
	}
	//---------------------------------------------------------
	ScreenshotChunkAssembler::Result ScreenshotChunkAssembler::addChunk(
		String const & path, unsigned int seq, unsigned int total,
		String const & data, String & outError)
	{
		outError.clear();
		auto fail = [this, &outError](String const & reason)
		{
			this->reset();
			outError = reason;
			return Result::Failed;
		};
		if (total == 0)
		{
			return fail("screenshot chunk claims a zero-length sequence");
		}
		if (total > maxChunkCount())
		{
			return fail("screenshot chunk sequence is too long (" +
				std::to_string(total) + " chunks)");
		}
		if (seq >= total)
		{
			return fail("screenshot chunk " + std::to_string(seq) +
				" is outside its sequence of " + std::to_string(total));
		}
		if (!this->mActive)
		{
			// only chunk 0 opens a transfer: a stray tail from a sequence that
			// already failed must not look like the start of a new image
			if (seq != 0)
			{
				return fail("screenshot chunk " + std::to_string(seq) +
					" arrived with no sequence in progress");
			}
			this->mPath = path;
			this->mTotal = total;
			this->mNext = 0;
			this->mBytes.clear();
			this->mActive = true;
		}
		else
		{
			if (total != this->mTotal)
			{
				return fail("screenshot chunk sequence length changed mid "
					"transfer (" + std::to_string(this->mTotal) + " -> " +
					std::to_string(total) + ")");
			}
			if (path != this->mPath)
			{
				return fail("screenshot chunk path changed mid transfer ('" +
					this->mPath + "' -> '" + path + "')");
			}
			if (seq != this->mNext)
			{
				// one rule for gaps, reordering and duplicates alike
				return fail("screenshot chunk out of order (expected " +
					std::to_string(this->mNext) + ", got " +
					std::to_string(seq) + ")");
			}
		}
		std::vector<unsigned char> decoded;
		if (!Base64::decode(data, decoded))
		{
			return fail("screenshot chunk " + std::to_string(seq) +
				" is not valid base64");
		}
		if (this->mBytes.size() + decoded.size() > MAX_IMAGE_BYTES)
		{
			return fail("screenshot image exceeds the " +
				std::to_string(MAX_IMAGE_BYTES) + " byte transfer limit");
		}
		this->mBytes.insert(this->mBytes.end(), decoded.begin(), decoded.end());
		this->mNext = seq + 1;
		if (this->mNext < this->mTotal)
		{
			return Result::NeedMore;
		}
		// the last chunk landed: close the transfer but KEEP the result, so
		// the caller reads bytes()/path() before it resets
		this->mActive = false;
		if (this->mBytes.empty())
		{
			return fail("screenshot sequence carried no image bytes");
		}
		return Result::Complete;
	}
	//---------------------------------------------------------
}
