/**************************************************************
	created:	2026/07/07 at 23:30
	filename: 	DebugServer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debugnet/DebugServer.h"

#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	include <arpa/inet.h>
#	include <fcntl.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#endif

#include <cstring>

namespace Orkige
{
	namespace
	{
		//! cap on buffered inbound messages: the editor streams commands at
		//! human rates, so anything beyond this means the consumer stalled -
		//! drop the oldest instead of growing without bound
		const size_t MAX_QUEUED_MESSAGES = 1024;
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	DebugServer::DebugServer()
	{
		this->listenHandle = DebugSocketUtil::INVALID_SOCKET_HANDLE;
		this->port = 0;
		this->loopbackOnly = true;
		this->clientAttached = false;
		this->clientConnectedEvent = false;
		this->clientDisconnectedEvent = false;
		this->malformedLines = 0;
	}
	//---------------------------------------------------------
	DebugServer::~DebugServer()
	{
		this->stop();
	}
	//---------------------------------------------------------
	bool DebugServer::start(unsigned short listenPort, bool exposeNonLoopback)
	{
		this->stop();
		this->loopbackOnly = !exposeNonLoopback;
		DebugSocketUtil::SocketHandle handle =
			DebugSocketUtil::createNonBlockingTcpSocket();
		if (handle == DebugSocketUtil::INVALID_SOCKET_HANDLE)
		{
			return false;
		}
		// no SO_REUSEADDR on purpose: the debug link always binds ephemeral
		// or caller-chosen ports and a bind failure should be loud
		struct sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		// 127.0.0.1 by default; INADDR_ANY only on the explicit opt-in
		address.sin_addr.s_addr = htonl(
			exposeNonLoopback ? INADDR_ANY : INADDR_LOOPBACK);
		address.sin_port = htons(listenPort);
		if (::bind(handle, reinterpret_cast<struct sockaddr*>(&address),
			sizeof(address)) != 0)
		{
			DebugSocketUtil::closeSocket(handle);
			return false;
		}
		if (::listen(handle, 1) != 0)
		{
			DebugSocketUtil::closeSocket(handle);
			return false;
		}
		// resolve the actual port (relevant for listenPort == 0)
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
	void DebugServer::stop()
	{
		DebugSocketUtil::closeSocket(this->listenHandle);
		this->connection.close();
		this->port = 0;
		this->loopbackOnly = true;
		this->clientAttached = false;
		this->clientConnectedEvent = false;
		this->clientDisconnectedEvent = false;
		this->malformedLines = 0;
		this->received.clear();
	}
	//---------------------------------------------------------
	void DebugServer::update()
	{
		this->acceptPending();
		if (this->clientAttached)
		{
			this->connection.pump();
			this->drainLines();
			if (!this->connection.isOpen())
			{
				this->connection.close();
				this->clientAttached = false;
				this->clientDisconnectedEvent = true;
			}
		}
	}
	//---------------------------------------------------------
	bool DebugServer::consumeClientConnected()
	{
		const bool event = this->clientConnectedEvent;
		this->clientConnectedEvent = false;
		return event;
	}
	//---------------------------------------------------------
	bool DebugServer::consumeClientDisconnected()
	{
		const bool event = this->clientDisconnectedEvent;
		this->clientDisconnectedEvent = false;
		return event;
	}
	//---------------------------------------------------------
	bool DebugServer::receive(DebugMessage & out)
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
	bool DebugServer::send(DebugMessage const & message)
	{
		if (!this->clientAttached)
		{
			return false;
		}
		return this->connection.sendLine(message.encode());
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void DebugServer::acceptPending()
	{
		if (this->listenHandle == DebugSocketUtil::INVALID_SOCKET_HANDLE)
		{
			return;
		}
		for (;;)
		{
			const DebugSocketUtil::SocketHandle accepted =
				static_cast<DebugSocketUtil::SocketHandle>(
					::accept(this->listenHandle, NULL, NULL));
			if (accepted == DebugSocketUtil::INVALID_SOCKET_HANDLE)
			{
				return; // nothing pending (or a transient error - retry next frame)
			}
			if (this->clientAttached)
			{
				// single-client protocol: refuse extra connections
				DebugSocketUtil::SocketHandle refused = accepted;
				DebugSocketUtil::closeSocket(refused);
				continue;
			}
			// the accepted socket does not inherit O_NONBLOCK everywhere -
			// set it explicitly through the platform seam
#ifdef _WIN32
			u_long nonBlocking = 1;
			ioctlsocket(accepted, FIONBIO, &nonBlocking);
#else
			{
				const int flags = ::fcntl(accepted, F_GETFL, 0);
				if (flags >= 0)
				{
					::fcntl(accepted, F_SETFL, flags | O_NONBLOCK);
				}
			}
#endif
			this->connection.attach(accepted);
			this->clientAttached = true;
			this->clientConnectedEvent = true;
		}
	}
	//---------------------------------------------------------
	void DebugServer::drainLines()
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
