/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	HttpServer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debugnet/HttpServer.h"

#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	include <arpa/inet.h>
#	include <fcntl.h>
#	include <netinet/in.h>
#	include <poll.h>
#	include <sys/socket.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Orkige
{
	namespace
	{
		//! hard cap on a single buffered request (headers + body): a peer that
		//! never completes a request cannot grow our memory without bound
		const size_t MAX_REQUEST_BYTES = 4 * 1024 * 1024;
		//! sane header-section cap before the terminating CRLFCRLF must appear
		const size_t MAX_HEADER_BYTES = 64 * 1024;
		//! most concurrent client connections (an MCP client pools a few)
		const size_t MAX_CONNECTIONS = 8;
		//---------------------------------------------------------
		String toLowerAscii(String const & value)
		{
			String out(value);
			for (char & c : out)
			{
				c = static_cast<char>(std::tolower(
					static_cast<unsigned char>(c)));
			}
			return out;
		}
		//---------------------------------------------------------
		String trim(String const & value)
		{
			size_t begin = 0;
			size_t end = value.size();
			while (begin < end && (value[begin] == ' ' || value[begin] == '\t'))
			{
				++begin;
			}
			while (end > begin && (value[end - 1] == ' ' ||
				value[end - 1] == '\t' || value[end - 1] == '\r'))
			{
				--end;
			}
			return value.substr(begin, end - begin);
		}
		//---------------------------------------------------------
		//! set the accepted socket non-blocking (accept does not inherit it
		//! everywhere) - mirrors DebugServer::acceptPending
		void makeNonBlocking(DebugSocketUtil::SocketHandle handle)
		{
#ifdef _WIN32
			u_long nonBlocking = 1;
			ioctlsocket(handle, FIONBIO, &nonBlocking);
#else
			const int flags = ::fcntl(handle, F_GETFL, 0);
			if (flags >= 0)
			{
				::fcntl(handle, F_SETFL, flags | O_NONBLOCK);
			}
#endif
		}
	}
	//---------------------------------------------------------
	//--- HttpRequest / HttpResponse --------------------------
	//---------------------------------------------------------
	String HttpRequest::header(String const & lowerName) const
	{
		std::map<String, String>::const_iterator it =
			this->headers.find(lowerName);
		return it == this->headers.end() ? String() : it->second;
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	HttpServer::HttpServer()
	{
		this->listenHandle = DebugSocketUtil::INVALID_SOCKET_HANDLE;
		this->port = 0;
	}
	//---------------------------------------------------------
	HttpServer::~HttpServer()
	{
		this->stop();
	}
	//---------------------------------------------------------
	bool HttpServer::start(unsigned short listenPort)
	{
		this->stop();
		DebugSocketUtil::SocketHandle handle =
			DebugSocketUtil::createNonBlockingTcpSocket();
		if (handle == DebugSocketUtil::INVALID_SOCKET_HANDLE)
		{
			return false;
		}
		struct sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);	// 127.0.0.1 only
		address.sin_port = htons(listenPort);
		if (::bind(handle, reinterpret_cast<struct sockaddr*>(&address),
			sizeof(address)) != 0)
		{
			DebugSocketUtil::closeSocket(handle);
			return false;
		}
		if (::listen(handle, 4) != 0)
		{
			DebugSocketUtil::closeSocket(handle);
			return false;
		}
		struct sockaddr_in boundAddress;
		std::memset(&boundAddress, 0, sizeof(boundAddress));
#ifdef _WIN32
		int addressLength = sizeof(boundAddress);
#else
		socklen_t addressLength = sizeof(boundAddress);
#endif
		if (::getsockname(handle,
			reinterpret_cast<struct sockaddr*>(&boundAddress),
			&addressLength) != 0)
		{
			DebugSocketUtil::closeSocket(handle);
			return false;
		}
		this->listenHandle = handle;
		this->port = ntohs(boundAddress.sin_port);
		return true;
	}
	//---------------------------------------------------------
	void HttpServer::stop()
	{
		DebugSocketUtil::closeSocket(this->listenHandle);
		for (Connection & connection : this->connections)
		{
			DebugSocketUtil::closeSocket(connection.handle);
		}
		this->connections.clear();
		this->port = 0;
	}
	//---------------------------------------------------------
	void HttpServer::update(Handler const & handler)
	{
		if (this->listenHandle == DebugSocketUtil::INVALID_SOCKET_HANDLE)
		{
			return;
		}
		this->acceptPending();
		for (Connection & connection : this->connections)
		{
			this->pump(connection);
			if (connection.open)
			{
				this->serviceConnection(connection, handler);
				// flush whatever the service produced
				this->pump(connection);
			}
			// a fully flushed half-close completes here
			if (connection.closeAfterFlush && connection.outBuffer.empty())
			{
				connection.open = false;
			}
		}
		// reap closed connections
		for (size_t i = 0; i < this->connections.size();)
		{
			if (!this->connections[i].open &&
				this->connections[i].outBuffer.empty())
			{
				DebugSocketUtil::closeSocket(this->connections[i].handle);
				this->connections.erase(this->connections.begin() + i);
			}
			else
			{
				++i;
			}
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void HttpServer::acceptPending()
	{
		for (;;)
		{
			const DebugSocketUtil::SocketHandle accepted =
				static_cast<DebugSocketUtil::SocketHandle>(
					::accept(this->listenHandle, NULL, NULL));
			if (accepted == DebugSocketUtil::INVALID_SOCKET_HANDLE)
			{
				return; // nothing pending (or transient error - retry next frame)
			}
			if (this->connections.size() >= MAX_CONNECTIONS)
			{
				// too many open connections: refuse the newcomer
				DebugSocketUtil::SocketHandle refused = accepted;
				DebugSocketUtil::closeSocket(refused);
				continue;
			}
			makeNonBlocking(accepted);
			Connection connection;
			connection.handle = accepted;
			connection.open = true;
			connection.closeAfterFlush = false;
			this->connections.push_back(std::move(connection));
		}
	}
	//---------------------------------------------------------
	void HttpServer::pump(Connection & connection)
	{
		if (!connection.open)
		{
			return;
		}
		struct pollfd descriptor;
		std::memset(&descriptor, 0, sizeof(descriptor));
		descriptor.fd = connection.handle;
		descriptor.events = POLLIN;
		if (!connection.outBuffer.empty())
		{
			descriptor.events |= POLLOUT;
		}
#ifdef _WIN32
		const int ready = WSAPoll(&descriptor, 1, 0);
#else
		const int ready = ::poll(&descriptor, 1, 0);
#endif
		if (ready < 0)
		{
			if (!DebugSocketUtil::lastErrorWouldBlock())
			{
				connection.open = false;
			}
			return;
		}
		if (ready == 0)
		{
			return;
		}
		if (descriptor.revents & (POLLERR | POLLNVAL))
		{
			connection.open = false;
			return;
		}
		if (descriptor.revents & (POLLIN | POLLHUP))
		{
			char chunk[8192];
			for (;;)
			{
				const long received = static_cast<long>(::recv(
					connection.handle, chunk, sizeof(chunk), 0));
				if (received > 0)
				{
					connection.inBuffer.append(chunk,
						static_cast<size_t>(received));
					if (connection.inBuffer.size() > MAX_REQUEST_BYTES)
					{
						// oversized request: drop the peer rather than buffer it
						connection.open = false;
						return;
					}
					continue;
				}
				if (received == 0)
				{
					connection.open = false; // orderly peer shutdown
					return;
				}
				if (!DebugSocketUtil::lastErrorWouldBlock())
				{
					connection.open = false;
					return;
				}
				break; // drained
			}
		}
		if (!connection.outBuffer.empty())
		{
			const long sent = static_cast<long>(::send(connection.handle,
				connection.outBuffer.data(), connection.outBuffer.size(), 0));
			if (sent > 0)
			{
				connection.outBuffer.erase(0, static_cast<size_t>(sent));
			}
			else if (sent < 0 && !DebugSocketUtil::lastErrorWouldBlock())
			{
				connection.open = false;
			}
		}
	}
	//---------------------------------------------------------
	void HttpServer::serviceConnection(Connection & connection,
		Handler const & handler)
	{
		// parse as many complete pipelined requests as the buffer holds
		for (;;)
		{
			const size_t headerEnd = connection.inBuffer.find("\r\n\r\n");
			if (headerEnd == String::npos)
			{
				// no complete header section yet; guard against a peer that
				// floods headers without ever terminating them
				if (connection.inBuffer.size() > MAX_HEADER_BYTES)
				{
					connection.open = false;
				}
				return;
			}
			// --- request line ---
			const size_t firstLineEnd = connection.inBuffer.find("\r\n");
			const String requestLine =
				connection.inBuffer.substr(0, firstLineEnd);
			HttpRequest request;
			{
				const size_t methodEnd = requestLine.find(' ');
				const size_t targetEnd = methodEnd == String::npos
					? String::npos : requestLine.find(' ', methodEnd + 1);
				if (methodEnd == String::npos || targetEnd == String::npos)
				{
					// malformed request line: 400 and close
					HttpResponse bad;
					bad.status = 400;
					bad.reason = "Bad Request";
					bad.contentType = "text/plain";
					bad.body = "malformed request line";
					bad.closeConnection = true;
					this->queueResponse(connection, bad, false);
					connection.inBuffer.clear();
					connection.closeAfterFlush = true;
					return;
				}
				request.method = requestLine.substr(0, methodEnd);
				request.target = requestLine.substr(methodEnd + 1,
					targetEnd - methodEnd - 1);
			}
			// --- headers ---
			size_t cursor = firstLineEnd + 2;
			while (cursor < headerEnd)
			{
				size_t lineEnd = connection.inBuffer.find("\r\n", cursor);
				if (lineEnd == String::npos || lineEnd > headerEnd)
				{
					lineEnd = headerEnd;
				}
				const String line =
					connection.inBuffer.substr(cursor, lineEnd - cursor);
				const size_t colon = line.find(':');
				if (colon != String::npos)
				{
					const String name = toLowerAscii(trim(line.substr(0, colon)));
					const String value = trim(line.substr(colon + 1));
					request.headers[name] = value;
				}
				cursor = lineEnd + 2;
			}
			// --- body (Content-Length framed) ---
			size_t contentLength = 0;
			{
				const String lengthValue = request.header("content-length");
				if (!lengthValue.empty())
				{
					contentLength = static_cast<size_t>(
						std::strtoul(lengthValue.c_str(), NULL, 10));
				}
			}
			const size_t bodyStart = headerEnd + 4;
			if (connection.inBuffer.size() < bodyStart + contentLength)
			{
				return; // body not fully arrived yet - wait for more bytes
			}
			request.body = connection.inBuffer.substr(bodyStart, contentLength);
			// consume this request from the buffer
			connection.inBuffer.erase(0, bodyStart + contentLength);

			// keep-alive unless the peer asked to close (HTTP/1.1 default)
			const String connectionHeader =
				toLowerAscii(request.header("connection"));
			const bool keepAlive = connectionHeader.find("close") == String::npos;

			HttpResponse response = handler(request);
			this->queueResponse(connection, response, keepAlive);
			if (!keepAlive || response.closeConnection)
			{
				connection.closeAfterFlush = true;
				connection.inBuffer.clear();
				return;
			}
		}
	}
	//---------------------------------------------------------
	void HttpServer::queueResponse(Connection & connection,
		HttpResponse const & response, bool keepAlive)
	{
		String head;
		char statusLine[64];
		std::snprintf(statusLine, sizeof(statusLine), "HTTP/1.1 %d ",
			response.status);
		head += statusLine;
		head += response.reason;
		head += "\r\n";
		if (!response.contentType.empty())
		{
			head += "Content-Type: ";
			head += response.contentType;
			head += "\r\n";
		}
		char lengthLine[64];
		std::snprintf(lengthLine, sizeof(lengthLine),
			"Content-Length: %zu\r\n", response.body.size());
		head += lengthLine;
		head += (keepAlive && !response.closeConnection)
			? "Connection: keep-alive\r\n" : "Connection: close\r\n";
		for (std::pair<String, String> const & extra : response.extraHeaders)
		{
			head += extra.first;
			head += ": ";
			head += extra.second;
			head += "\r\n";
		}
		head += "\r\n";
		connection.outBuffer += head;
		connection.outBuffer += response.body;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
