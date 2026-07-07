/**************************************************************
	created:	2026/07/07 at 23:30
	filename: 	DebugSocket.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debugnet/DebugSocket.h"
#include "core_debugnet/DebugProtocol.h"

#ifdef _WIN32
// winsock seam: everything below compiles against winsock2 once the Windows
// port lands (WSAStartup in initialise(), closesocket, ioctlsocket for
// non-blocking mode, WSAGetLastError()-based wouldBlock check)
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	include <errno.h>
#	include <fcntl.h>
#	include <netinet/in.h>
#	include <netinet/tcp.h>
#	include <poll.h>
#	include <signal.h>
#	include <sys/socket.h>
#	include <unistd.h>
#endif

#include <cstring>

namespace Orkige
{
	namespace DebugSocketUtil
	{
#ifdef _WIN32
		const SocketHandle INVALID_SOCKET_HANDLE = ~0ull;
#else
		const SocketHandle INVALID_SOCKET_HANDLE = -1;
#endif
		//---------------------------------------------------------
		bool initialise()
		{
#ifdef _WIN32
			static bool started = false;
			if (!started)
			{
				WSADATA data;
				started = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
			}
			return started;
#else
			// a peer that vanishes mid-send must surface as an EPIPE error
			// code on the send call, not as a process-killing SIGPIPE
			static bool ignoredSigpipe = false;
			if (!ignoredSigpipe)
			{
				::signal(SIGPIPE, SIG_IGN);
				ignoredSigpipe = true;
			}
			return true;
#endif
		}
		//---------------------------------------------------------
		SocketHandle createNonBlockingTcpSocket()
		{
			if (!initialise())
			{
				return INVALID_SOCKET_HANDLE;
			}
#ifdef _WIN32
			SocketHandle handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (handle == INVALID_SOCKET_HANDLE)
			{
				return INVALID_SOCKET_HANDLE;
			}
			u_long nonBlocking = 1;
			if (ioctlsocket(handle, FIONBIO, &nonBlocking) != 0)
			{
				closeSocket(handle);
				return INVALID_SOCKET_HANDLE;
			}
#else
			SocketHandle handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (handle == INVALID_SOCKET_HANDLE)
			{
				return INVALID_SOCKET_HANDLE;
			}
			const int flags = ::fcntl(handle, F_GETFL, 0);
			if (flags < 0 || ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) < 0)
			{
				closeSocket(handle);
				return INVALID_SOCKET_HANDLE;
			}
#ifdef SO_NOSIGPIPE
			// macOS: belt+braces next to the global SIGPIPE ignore above
			int noSigpipe = 1;
			::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe,
				sizeof(noSigpipe));
#endif
#endif
			// localhost debug link: latency beats batching
			int noDelay = 1;
			::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY,
				reinterpret_cast<char*>(&noDelay), sizeof(noDelay));
			return handle;
		}
		//---------------------------------------------------------
		void closeSocket(SocketHandle & handle)
		{
			if (handle == INVALID_SOCKET_HANDLE)
			{
				return;
			}
#ifdef _WIN32
			::closesocket(handle);
#else
			::close(handle);
#endif
			handle = INVALID_SOCKET_HANDLE;
		}
		//---------------------------------------------------------
		bool lastErrorWouldBlock()
		{
#ifdef _WIN32
			const int error = WSAGetLastError();
			return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
			return errno == EWOULDBLOCK || errno == EAGAIN ||
				errno == EINPROGRESS || errno == EINTR;
#endif
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	DebugLineConnection::DebugLineConnection()
	{
		this->handle = DebugSocketUtil::INVALID_SOCKET_HANDLE;
		this->open = false;
		this->discardingLine = false;
		this->partialLength = 0;
		this->droppedLines = 0;
	}
	//---------------------------------------------------------
	DebugLineConnection::~DebugLineConnection()
	{
		this->close();
	}
	//---------------------------------------------------------
	void DebugLineConnection::attach(DebugSocketUtil::SocketHandle socketHandle)
	{
		this->close();
		this->handle = socketHandle;
		this->open = (socketHandle != DebugSocketUtil::INVALID_SOCKET_HANDLE);
	}
	//---------------------------------------------------------
	void DebugLineConnection::close()
	{
		DebugSocketUtil::closeSocket(this->handle);
		this->open = false;
		this->discardingLine = false;
		this->partialLength = 0;
		this->inBuffer.clear();
		this->outBuffer.clear();
	}
	//---------------------------------------------------------
	bool DebugLineConnection::sendLine(String const & line)
	{
		if (!this->open)
		{
			return false;
		}
		if (line.size() + 1 > DebugProtocol::MAX_LINE_LENGTH)
		{
			return false; // refuse oversized lines instead of flooding the peer
		}
		this->outBuffer += line;
		this->outBuffer += '\n';
		this->pump();
		return this->open;
	}
	//---------------------------------------------------------
	void DebugLineConnection::pump()
	{
		if (!this->open)
		{
			return;
		}
		// poll() gates the non-blocking recv/send calls: POLLIN drains all
		// currently readable bytes, POLLOUT flushes as much of the queued
		// output as the socket accepts - nothing here ever blocks
		struct pollfd descriptor;
		std::memset(&descriptor, 0, sizeof(descriptor));
		descriptor.fd = this->handle;
		descriptor.events = POLLIN;
		if (!this->outBuffer.empty())
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
				this->open = false;
			}
			return;
		}
		if (ready == 0)
		{
			return;
		}
		if (descriptor.revents & (POLLERR | POLLNVAL))
		{
			this->open = false;
			return;
		}
		if (descriptor.revents & (POLLIN | POLLHUP))
		{
			char chunk[4096];
			for (;;)
			{
				const long received = static_cast<long>(::recv(this->handle,
					chunk, sizeof(chunk), 0));
				if (received > 0)
				{
					// enforce the line cap while receiving: an oversized line
					// switches to discard mode until its terminator arrives;
					// partialLength tracks only the unterminated tail line, so
					// buffered complete lines are never harmed
					for (long i = 0; i < received; ++i)
					{
						const char c = chunk[i];
						if (this->discardingLine)
						{
							if (c == '\n')
							{
								this->discardingLine = false;
								++this->droppedLines;
							}
							continue;
						}
						this->inBuffer += c;
						if (c == '\n')
						{
							this->partialLength = 0;
						}
						else if (++this->partialLength >=
							DebugProtocol::MAX_LINE_LENGTH)
						{
							this->inBuffer.erase(
								this->inBuffer.size() - this->partialLength);
							this->partialLength = 0;
							this->discardingLine = true;
						}
					}
					continue;
				}
				if (received == 0)
				{
					this->open = false; // orderly peer shutdown
					return;
				}
				if (!DebugSocketUtil::lastErrorWouldBlock())
				{
					this->open = false;
					return;
				}
				break; // drained
			}
		}
		if (!this->outBuffer.empty())
		{
			const long sent = static_cast<long>(::send(this->handle,
				this->outBuffer.data(), this->outBuffer.size(), 0));
			if (sent > 0)
			{
				this->outBuffer.erase(0, static_cast<size_t>(sent));
			}
			else if (sent < 0 && !DebugSocketUtil::lastErrorWouldBlock())
			{
				this->open = false;
			}
		}
	}
	//---------------------------------------------------------
	bool DebugLineConnection::nextLine(String & out)
	{
		const size_t terminator = this->inBuffer.find('\n');
		if (terminator == String::npos)
		{
			return false;
		}
		out = this->inBuffer.substr(0, terminator);
		this->inBuffer.erase(0, terminator + 1);
		// tolerate CRLF peers
		if (!out.empty() && out.back() == '\r')
		{
			out.pop_back();
		}
		return true;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
