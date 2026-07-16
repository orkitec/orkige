/**************************************************************
	created:	2026/07/16 at 12:00
	filename: 	WebSocket.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debugnet/WebSocket.h"
#include "core_debugnet/HttpServer.h"
#include "core_util/Sha1.h"

#include <cstdint>
#include <cstring>

namespace Orkige
{
	namespace
	{
		//! the fixed handshake GUID from RFC 6455 section 1.3
		const char * const HANDSHAKE_GUID =
			"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
		//---------------------------------------------------------
		String base64Encode(unsigned char const * data, std::size_t length)
		{
			static const char ALPHABET[] =
				"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
				"0123456789+/";
			String out;
			out.reserve(((length + 2) / 3) * 4);
			for (std::size_t i = 0; i < length; i += 3)
			{
				const unsigned int byte0 = data[i];
				const unsigned int byte1 = i + 1 < length ? data[i + 1] : 0;
				const unsigned int byte2 = i + 2 < length ? data[i + 2] : 0;
				const unsigned int triple =
					(byte0 << 16) | (byte1 << 8) | byte2;
				out += ALPHABET[(triple >> 18) & 0x3F];
				out += ALPHABET[(triple >> 12) & 0x3F];
				out += i + 1 < length ? ALPHABET[(triple >> 6) & 0x3F] : '=';
				out += i + 2 < length ? ALPHABET[triple & 0x3F] : '=';
			}
			return out;
		}
		//---------------------------------------------------------
		//! ASCII lower-case (header values compare case-insensitively)
		String toLowerAscii(String const & value)
		{
			String out(value);
			for (char & c : out)
			{
				if (c >= 'A' && c <= 'Z')
				{
					c = static_cast<char>(c - 'A' + 'a');
				}
			}
			return out;
		}
	}

	namespace WebSocketUtil
	{
		// generous ceiling: the debug link's biggest legitimate frame is a
		// flushed send buffer of 64K-capped protocol lines; anything in the
		// megabytes says broken peer, and a 64-bit wire length must never
		// drive an allocation unchecked
		const std::size_t MAX_FRAME_PAYLOAD = 4 * 1024 * 1024;
		//---------------------------------------------------------
		bool isUpgradeRequest(HttpRequest const & request)
		{
			// Connection may be a list ("keep-alive, Upgrade"); values are
			// case-insensitive per the HTTP spec
			const String connection =
				toLowerAscii(request.header("connection"));
			const String upgrade = toLowerAscii(request.header("upgrade"));
			return request.method == "GET" &&
				connection.find("upgrade") != String::npos &&
				upgrade == "websocket" &&
				!request.header("sec-websocket-key").empty();
		}
		//---------------------------------------------------------
		String computeAcceptKey(String const & secWebSocketKey)
		{
			// SHA-1 lives in core_util/Sha1 (the handshake needs exactly one
			// digest of a ~60-byte string; @see that file's remarks)
			const String salted = secWebSocketKey + HANDSHAKE_GUID;
			unsigned char digest[20];
			Sha1::digest(reinterpret_cast<unsigned char const *>(salted.data()),
				salted.size(), digest);
			return base64Encode(digest, sizeof(digest));
		}
		//---------------------------------------------------------
		HttpResponse buildHandshakeResponse(HttpRequest const & request)
		{
			HttpResponse response;
			response.status = 101;
			response.reason = "Switching Protocols";
			response.contentType.clear();	// a 1xx response carries no body
			response.extraHeaders.push_back({ "Upgrade", "websocket" });
			response.extraHeaders.push_back({ "Connection", "Upgrade" });
			response.extraHeaders.push_back({ "Sec-WebSocket-Accept",
				computeAcceptKey(request.header("sec-websocket-key")) });
			// echo the client's first offered subprotocol (the browser
			// player offers "binary"); a client that offered none gets none
			const String offered = request.header("sec-websocket-protocol");
			if (!offered.empty())
			{
				const std::size_t comma = offered.find(',');
				String first = offered.substr(0, comma);
				while (!first.empty() && (first.front() == ' ' ||
					first.front() == '\t'))
				{
					first.erase(first.begin());
				}
				while (!first.empty() && (first.back() == ' ' ||
					first.back() == '\t'))
				{
					first.pop_back();
				}
				response.extraHeaders.push_back(
					{ "Sec-WebSocket-Protocol", first });
			}
			response.takeover = true;
			return response;
		}
		//---------------------------------------------------------
		DecodeResult decodeFrame(String const & buffer,
			std::size_t & outConsumed, Frame & outFrame)
		{
			if (buffer.size() < 2)
			{
				return DecodeResult::NeedMore;
			}
			const unsigned char byte0 =
				static_cast<unsigned char>(buffer[0]);
			const unsigned char byte1 =
				static_cast<unsigned char>(buffer[1]);
			if ((byte0 & 0x70u) != 0)
			{
				return DecodeResult::Error;	// RSV bits: no extension agreed
			}
			const bool masked = (byte1 & 0x80u) != 0;
			std::uint64_t payloadLength = byte1 & 0x7Fu;
			std::size_t cursor = 2;
			if (payloadLength == 126)
			{
				if (buffer.size() < cursor + 2)
				{
					return DecodeResult::NeedMore;
				}
				payloadLength =
					(static_cast<std::uint64_t>(
						static_cast<unsigned char>(buffer[cursor])) << 8) |
					static_cast<unsigned char>(buffer[cursor + 1]);
				cursor += 2;
			}
			else if (payloadLength == 127)
			{
				if (buffer.size() < cursor + 8)
				{
					return DecodeResult::NeedMore;
				}
				payloadLength = 0;
				for (int i = 0; i < 8; ++i)
				{
					payloadLength = (payloadLength << 8) |
						static_cast<unsigned char>(buffer[cursor + i]);
				}
				cursor += 8;
			}
			if (payloadLength > MAX_FRAME_PAYLOAD)
			{
				return DecodeResult::Error;
			}
			unsigned char maskKey[4] = { 0, 0, 0, 0 };
			if (masked)
			{
				if (buffer.size() < cursor + 4)
				{
					return DecodeResult::NeedMore;
				}
				std::memcpy(maskKey, buffer.data() + cursor, 4);
				cursor += 4;
			}
			const std::size_t length =
				static_cast<std::size_t>(payloadLength);
			if (buffer.size() < cursor + length)
			{
				return DecodeResult::NeedMore;
			}
			outFrame.fin = (byte0 & 0x80u) != 0;
			outFrame.opcode = byte0 & 0x0Fu;
			outFrame.payload.assign(buffer, cursor, length);
			if (masked)
			{
				for (std::size_t i = 0; i < length; ++i)
				{
					outFrame.payload[i] = static_cast<char>(
						static_cast<unsigned char>(outFrame.payload[i]) ^
						maskKey[i % 4]);
				}
			}
			outConsumed = cursor + length;
			return DecodeResult::Ok;
		}
		//---------------------------------------------------------
		//! the shared header builder behind both encoders
		static String encodeHeader(int opcode, std::size_t payloadLength,
			bool masked)
		{
			String head;
			head += static_cast<char>(0x80u |
				(static_cast<unsigned int>(opcode) & 0x0Fu));	// FIN + opcode
			const unsigned char maskBit = masked ? 0x80u : 0x00u;
			if (payloadLength < 126)
			{
				head += static_cast<char>(maskBit |
					static_cast<unsigned char>(payloadLength));
			}
			else if (payloadLength <= 0xFFFFu)
			{
				head += static_cast<char>(maskBit | 126u);
				head += static_cast<char>((payloadLength >> 8) & 0xFFu);
				head += static_cast<char>(payloadLength & 0xFFu);
			}
			else
			{
				head += static_cast<char>(maskBit | 127u);
				for (int i = 7; i >= 0; --i)
				{
					head += static_cast<char>(
						(static_cast<std::uint64_t>(payloadLength) >>
							(i * 8)) & 0xFFu);
				}
			}
			return head;
		}
		//---------------------------------------------------------
		String encodeFrame(int opcode, String const & payload)
		{
			String frame = encodeHeader(opcode, payload.size(), false);
			frame += payload;
			return frame;
		}
		//---------------------------------------------------------
		String encodeMaskedFrame(int opcode, String const & payload,
			unsigned int maskKey)
		{
			String frame = encodeHeader(opcode, payload.size(), true);
			unsigned char key[4];
			key[0] = static_cast<unsigned char>((maskKey >> 24) & 0xFFu);
			key[1] = static_cast<unsigned char>((maskKey >> 16) & 0xFFu);
			key[2] = static_cast<unsigned char>((maskKey >> 8) & 0xFFu);
			key[3] = static_cast<unsigned char>(maskKey & 0xFFu);
			frame.append(reinterpret_cast<char const *>(key), 4);
			for (std::size_t i = 0; i < payload.size(); ++i)
			{
				frame += static_cast<char>(
					static_cast<unsigned char>(payload[i]) ^ key[i % 4]);
			}
			return frame;
		}
	}
}
