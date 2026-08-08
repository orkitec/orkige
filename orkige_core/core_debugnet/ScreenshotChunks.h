/********************************************************************
	created:	Saturday 2026/08/08 at 10:20
	filename: 	ScreenshotChunks.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ScreenshotChunks_h__8_8_2026__10_20_00__
#define __ScreenshotChunks_h__8_8_2026__10_20_00__

//! @file ScreenshotChunks.h
//! @brief the pure both-ends logic of the chunked screenshot-DATA road on the
//! debug protocol (@see DebugProtocol::MSG_SCREENSHOT_DATA).
//! @remarks A runtime whose filesystem the editor cannot reach - the browser
//! player, whose captures land in the page's in-memory filesystem - answers a
//! screenshot request with the IMAGE BYTES instead of a path. The transport
//! carries one flat JSON object per line under a hard line cap, so the PNG
//! travels base64-encoded in a sequence of numbered chunks. Splitting and
//! reassembly are decided here, free of sockets and files, so both halves are
//! headless-unit-testable and speak one definition of the wire shape.
//! The assembler FAILS CLOSED: any gap, reordering, duplicate, size overrun or
//! undecodable slice ends the transfer with a named error rather than handing
//! a caller a truncated PNG.

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <cstddef>
#include <vector>

namespace Orkige
{
	//! @brief split image bytes into the base64 payload slices of a
	//! MSG_SCREENSHOT_DATA sequence (chunk i is element i, `total` is the
	//! returned count). An empty input yields no chunks - a runtime with no
	//! bytes to send answers the failure road instead.
	StringVector ORKIGE_CORE_DLL splitScreenshotChunks(
		unsigned char const * data, std::size_t length);

	//! @brief the base64 characters one MSG_SCREENSHOT_DATA line carries.
	//! Sized so the encoded line - the payload plus the message's JSON
	//! envelope and the echoed path - stays well inside
	//! DebugProtocol::MAX_LINE_LENGTH.
	unsigned int ORKIGE_CORE_DLL screenshotChunkPayloadChars();

	//! @brief the largest image a chunk sequence may carry, in decoded bytes.
	//! A peer that claims more is refused before a single byte is buffered.
	std::size_t ORKIGE_CORE_DLL screenshotMaxImageBytes();

	//! @brief reassembles one MSG_SCREENSHOT_DATA sequence into image bytes.
	//! @remarks Strictly sequential by design: chunk 0 opens a transfer and
	//! every following chunk must be the immediate successor of the last one
	//! accepted, carrying the same path and the same total. That single rule
	//! covers reordering, duplication and gaps at once, and the transport
	//! underneath is an ordered stream - anything else means the sequence was
	//! damaged, so the honest answer is a refusal, never a partial image.
	class ORKIGE_CORE_DLL ScreenshotChunkAssembler
	{
		//--- Types -------------------------------------------
	public:
		//! outcome of feeding one chunk
		enum class Result
		{
			NeedMore,	//!< accepted; more chunks are expected
			Complete,	//!< accepted and last - bytes() holds the image
			Failed		//!< the sequence is damaged (see the error text)
		};
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		String						mPath;		//!< the echoed capture path
		unsigned int				mTotal = 0;	//!< chunks the sequence claims
		unsigned int				mNext = 0;	//!< the sequence number expected next
		bool						mActive = false;	//!< a transfer is open
		std::vector<unsigned char>	mBytes;		//!< the decoded image so far
		//--- Methods -----------------------------------------
	public:
		//! @brief feed one chunk. On Failed, outError names what was wrong and
		//! the assembler is reset (the next chunk 0 starts a fresh transfer).
		//! On Complete, bytes() and path() hold the result until reset().
		Result addChunk(String const & path, unsigned int seq,
			unsigned int total, String const & data, String & outError);
		//! drop any transfer in progress (and any completed result)
		void reset();
		//! is a transfer open (at least one chunk in, not finished)
		bool inProgress() const { return this->mActive; }
		//! the reassembled image bytes (valid after a Complete)
		std::vector<unsigned char> const & bytes() const { return this->mBytes; }
		//! the capture path the sequence echoed
		String const & path() const { return this->mPath; }
	protected:
	private:
	};
	//---------------------------------------------------------
}

#endif //__ScreenshotChunks_h__8_8_2026__10_20_00__
