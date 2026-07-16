/**************************************************************
	created:	2026/07/16 at 12:00
	filename: 	WebSocket.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __WebSocket_h__16_7_2026__12_00_00__
#define __WebSocket_h__16_7_2026__12_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <cstddef>

namespace Orkige
{
	struct HttpRequest;
	struct HttpResponse;

	//! @brief the server-side WebSocket vocabulary (RFC 6455) behind the
	//! browser debug transport: pure handshake + frame codec functions, no
	//! sockets, no state - so every piece is headlessly unit-testable.
	//! @remarks a page cannot listen on and cannot speak raw TCP, so the
	//! browser player DIALS OUT and its POSIX-socket byte stream arrives
	//! WebSocket-framed (binary subprotocol, client-masked). These helpers
	//! let the editor's HttpServer answer the upgrade and let the line
	//! transport de-frame the SAME '\n'-framed DebugMessage stream the raw
	//! TCP link carries - one protocol, two carriers.
	namespace WebSocketUtil
	{
		//--- frame opcodes (RFC 6455 section 5.2) ---
		const int OP_CONTINUATION = 0x0;	//!< continuation of a fragmented message
		const int OP_TEXT = 0x1;			//!< text data frame
		const int OP_BINARY = 0x2;			//!< binary data frame
		const int OP_CLOSE = 0x8;			//!< connection close control frame
		const int OP_PING = 0x9;			//!< ping control frame (answer with pong)
		const int OP_PONG = 0xA;			//!< pong control frame

		//! @brief hard cap on a single frame's payload: bigger frames are a
		//! protocol error here (the debug link batches at most a send
		//! buffer's worth of protocol lines into one frame - a peer claiming
		//! more is broken or hostile, and a 64-bit length must never size a
		//! buffer allocation unchecked)
		extern ORKIGE_CORE_DLL const std::size_t MAX_FRAME_PAYLOAD;

		//! one decoded frame
		struct ORKIGE_CORE_DLL Frame
		{
			bool	fin = true;		//!< final fragment of its message
			int		opcode = 0;		//!< one of the OP_* values
			String	payload;		//!< unmasked payload bytes
		};

		//! decodeFrame outcome
		enum class DecodeResult
		{
			NeedMore,	//!< buffer holds no complete frame yet
			Ok,			//!< outFrame + outConsumed are valid
			Error		//!< malformed/oversized frame - drop the connection
		};

		//! @brief is this parsed HTTP request a WebSocket upgrade
		//! (Connection: Upgrade + Upgrade: websocket + a key, per RFC 6455)
		bool ORKIGE_CORE_DLL isUpgradeRequest(HttpRequest const & request);

		//! @brief the Sec-WebSocket-Accept value for a client's
		//! Sec-WebSocket-Key: base64(SHA-1(key + RFC 6455 GUID))
		String ORKIGE_CORE_DLL computeAcceptKey(String const & secWebSocketKey);

		//! @brief the 101 Switching Protocols response for an accepted
		//! upgrade request (accept key computed, the client's first offered
		//! subprotocol echoed - the browser player offers "binary"). The
		//! caller marks it as a takeover response so the socket leaves HTTP
		//! framing once the head is flushed.
		HttpResponse ORKIGE_CORE_DLL buildHandshakeResponse(
			HttpRequest const & request);

		//! @brief decode the first complete frame in buffer. On Ok,
		//! outConsumed is the frame's full wire size (erase that many bytes)
		//! and outFrame holds the unmasked payload. NeedMore leaves both
		//! untouched; Error means the peer violated the framing (oversized
		//! payload included) and the connection should drop.
		DecodeResult ORKIGE_CORE_DLL decodeFrame(String const & buffer,
			std::size_t & outConsumed, Frame & outFrame);

		//! @brief one unmasked final frame (the server-to-client direction;
		//! RFC 6455 forbids server masking)
		String ORKIGE_CORE_DLL encodeFrame(int opcode, String const & payload);

		//! @brief one masked final frame (the client-to-server direction -
		//! used by the unit tests' synthetic browser peer; a real browser
		//! masks on its own)
		String ORKIGE_CORE_DLL encodeMaskedFrame(int opcode,
			String const & payload, unsigned int maskKey);
	}
}

#endif //__WebSocket_h__16_7_2026__12_00_00__
