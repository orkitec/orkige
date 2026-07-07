/**************************************************************
	created:	2026/07/07 at 23:30
	filename: 	DebugClient.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debugnet/DebugClient.h"

#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	include <arpa/inet.h>
#	include <errno.h>
#	include <netinet/in.h>
#	include <poll.h>
#	include <sys/socket.h>
#endif

#include <cstring>

namespace Orkige
{
	namespace
	{
		//! same stall guard as the server side
		const size_t MAX_QUEUED_MESSAGES = 1024;
		//---------------------------------------------------------
		//! @brief detect the TCP "self-connect" trap: connecting to a not
		//! (yet) listening port in the ephemeral range can pick the target
		//! port as the local port and succeed against itself (simultaneous
		//! open). Such a link answers nothing - it must count as failed so
		//! the caller's retry loop keeps going until the real server is up.
		bool isSelfConnected(DebugSocketUtil::SocketHandle handle)
		{
			struct sockaddr_in localAddress;
			struct sockaddr_in peerAddress;
			std::memset(&localAddress, 0, sizeof(localAddress));
			std::memset(&peerAddress, 0, sizeof(peerAddress));
#ifdef _WIN32
			int localLength = sizeof(localAddress);
			int peerLength = sizeof(peerAddress);
#else
			socklen_t localLength = sizeof(localAddress);
			socklen_t peerLength = sizeof(peerAddress);
#endif
			if (::getsockname(handle,
					reinterpret_cast<struct sockaddr*>(&localAddress),
					&localLength) != 0 ||
				::getpeername(handle,
					reinterpret_cast<struct sockaddr*>(&peerAddress),
					&peerLength) != 0)
			{
				return false;
			}
			return localAddress.sin_port == peerAddress.sin_port &&
				localAddress.sin_addr.s_addr == peerAddress.sin_addr.s_addr;
		}
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	DebugClient::DebugClient()
	{
		this->pendingHandle = DebugSocketUtil::INVALID_SOCKET_HANDLE;
		this->state = State::Idle;
		this->malformedLines = 0;
	}
	//---------------------------------------------------------
	DebugClient::~DebugClient()
	{
		this->disconnect();
	}
	//---------------------------------------------------------
	bool DebugClient::connect(String const & host, unsigned short port)
	{
		this->disconnect();
		DebugSocketUtil::SocketHandle handle =
			DebugSocketUtil::createNonBlockingTcpSocket();
		if (handle == DebugSocketUtil::INVALID_SOCKET_HANDLE)
		{
			this->state = State::Failed;
			return false;
		}
		struct sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
		{
			DebugSocketUtil::closeSocket(handle);
			this->state = State::Failed;
			return false;
		}
		const int result = ::connect(handle,
			reinterpret_cast<struct sockaddr*>(&address), sizeof(address));
		if (result == 0)
		{
			// immediate success (possible on loopback)
			if (isSelfConnected(handle))
			{
				DebugSocketUtil::closeSocket(handle);
				this->state = State::Failed;
				return false;
			}
			this->connection.attach(handle);
			this->state = State::Connected;
			return true;
		}
		if (!DebugSocketUtil::lastErrorWouldBlock())
		{
			DebugSocketUtil::closeSocket(handle);
			this->state = State::Failed;
			return false;
		}
		this->pendingHandle = handle;
		this->state = State::Connecting;
		return true;
	}
	//---------------------------------------------------------
	void DebugClient::disconnect()
	{
		DebugSocketUtil::closeSocket(this->pendingHandle);
		this->connection.close();
		this->state = State::Idle;
		this->malformedLines = 0;
		this->received.clear();
	}
	//---------------------------------------------------------
	void DebugClient::update()
	{
		if (this->state == State::Connecting)
		{
			// a non-blocking connect resolves through POLLOUT + SO_ERROR
			struct pollfd descriptor;
			std::memset(&descriptor, 0, sizeof(descriptor));
			descriptor.fd = this->pendingHandle;
			descriptor.events = POLLOUT;
#ifdef _WIN32
			const int ready = WSAPoll(&descriptor, 1, 0);
#else
			const int ready = ::poll(&descriptor, 1, 0);
#endif
			if (ready < 0 && !DebugSocketUtil::lastErrorWouldBlock())
			{
				DebugSocketUtil::closeSocket(this->pendingHandle);
				this->state = State::Failed;
				return;
			}
			if (ready <= 0)
			{
				return; // handshake still in flight
			}
			int socketError = 0;
#ifdef _WIN32
			int errorLength = sizeof(socketError);
			::getsockopt(this->pendingHandle, SOL_SOCKET, SO_ERROR,
				reinterpret_cast<char*>(&socketError), &errorLength);
#else
			socklen_t errorLength = sizeof(socketError);
			::getsockopt(this->pendingHandle, SOL_SOCKET, SO_ERROR,
				&socketError, &errorLength);
#endif
			if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
				socketError != 0 || isSelfConnected(this->pendingHandle))
			{
				DebugSocketUtil::closeSocket(this->pendingHandle);
				this->state = State::Failed;
				return;
			}
			this->connection.attach(this->pendingHandle);
			this->pendingHandle = DebugSocketUtil::INVALID_SOCKET_HANDLE;
			this->state = State::Connected;
		}
		if (this->state == State::Connected)
		{
			this->connection.pump();
			this->drainLines();
			if (!this->connection.isOpen())
			{
				this->connection.close();
				this->state = State::Disconnected;
			}
		}
	}
	//---------------------------------------------------------
	bool DebugClient::receive(DebugMessage & out)
	{
		if (this->received.empty())
		{
			return false;
		}
		out = this->received.front();
		this->received.pop_front();
		return true;
	}
	//---------------------------------------------------------
	bool DebugClient::send(DebugMessage const & message)
	{
		if (this->state != State::Connected)
		{
			return false;
		}
		return this->connection.sendLine(message.encode());
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void DebugClient::drainLines()
	{
		String line;
		while (this->connection.nextLine(line))
		{
			DebugMessage message;
			if (!DebugMessage::decode(line, message))
			{
				++this->malformedLines;
				continue; // malformed input is dropped, never fatal
			}
			if (this->received.size() >= MAX_QUEUED_MESSAGES)
			{
				this->received.pop_front();
			}
			this->received.push_back(message);
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
