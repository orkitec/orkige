/**************************************************************
	created:	2026/07/07 at 23:30
	filename: 	DebugServer.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __DebugServer_h__7_7_2026__23_30_00__
#define __DebugServer_h__7_7_2026__23_30_00__

#include "core_debugnet/DebugProtocol.h"
#include "core_debugnet/DebugSocket.h"

#include <deque>

namespace Orkige
{
	//! @brief runtime end of the remote debugging link: listens on localhost
	//! (127.0.0.1 only - never exposed to the network), accepts a single
	//! editor connection and exchanges DebugMessages over it.
	//! @remarks fully non-blocking: update() must be pumped every frame; it
	//! accepts/reads/writes whatever is ready and never waits. Malformed
	//! lines are counted and dropped, they never tear the connection down.
	//! A second connection attempt while a client is attached is refused
	//! (accepted and immediately closed).
	class ORKIGE_CORE_DLL DebugServer
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		DebugSocketUtil::SocketHandle	listenHandle;		//!< listening socket
		DebugLineConnection				connection;			//!< the single attached client
		unsigned short					port;				//!< bound port (resolved when start() got 0)
		bool							loopbackOnly;		//!< did start() bind 127.0.0.1 only
		bool							clientAttached;		//!< is a client currently attached
		bool							clientConnectedEvent;	//!< pending "client connected" edge
		bool							clientDisconnectedEvent;//!< pending "client disconnected" edge
		unsigned int					malformedLines;		//!< dropped undecodable lines
		std::deque<DebugMessage>		received;			//!< decoded inbound messages
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor (not listening)
		DebugServer();
		//! destructor (stops listening, drops the client)
		~DebugServer();
		//! @brief start listening on port (0 = pick a free port, query it with
		//! getPort()); returns false when the socket setup fails. By DEFAULT
		//! binds 127.0.0.1 ONLY - the play-mode debug link must not be
		//! reachable off the machine. @p exposeNonLoopback is the explicit
		//! opt-in that binds ALL interfaces (INADDR_ANY) instead - only safe
		//! behind a trusted boundary.
		bool start(unsigned short listenPort, bool exposeNonLoopback = false);
		//! stop listening and drop the client
		void stop();
		//! accept/read/write pump - call once per frame, never blocks
		void update();
		//! is the listen socket up
		inline bool isListening() const;
		//! the bound port (valid after a successful start())
		inline unsigned short getPort() const;
		//! @brief did the last start() bind loopback (127.0.0.1) ONLY, rather
		//! than every interface? The security-regression seam.
		inline bool isLoopbackOnly() const;
		//! is a client currently attached
		inline bool hasClient() const;
		//! true once per new client connection (edge, consumed by the call)
		bool consumeClientConnected();
		//! true once per client disconnect (edge, consumed by the call)
		bool consumeClientDisconnected();
		//! pop the next received message; false when none is pending
		bool receive(DebugMessage & out);
		//! @brief send a message to the attached client; false when no client
		//! is attached or the encoded line was refused/failed
		bool send(DebugMessage const & message);
		//! number of received lines that failed to decode (dropped)
		inline unsigned int getMalformedLineCount() const;
		//! number of oversized received lines the transport discarded
		inline unsigned int getDroppedLineCount() const;
	protected:
		//! accept a pending connection (attach first client, refuse extras)
		void acceptPending();
		//! drain complete lines from the connection into received
		void drainLines();
	private:
		DebugServer(DebugServer const &) = delete;
		DebugServer & operator=(DebugServer const &) = delete;
	};
	//---------------------------------------------------------
	inline bool DebugServer::isListening() const
	{
		return this->listenHandle != DebugSocketUtil::INVALID_SOCKET_HANDLE;
	}
	//---------------------------------------------------------
	inline unsigned short DebugServer::getPort() const
	{
		return this->port;
	}
	//---------------------------------------------------------
	inline bool DebugServer::isLoopbackOnly() const
	{
		return this->loopbackOnly;
	}
	//---------------------------------------------------------
	inline bool DebugServer::hasClient() const
	{
		return this->clientAttached;
	}
	//---------------------------------------------------------
	inline unsigned int DebugServer::getMalformedLineCount() const
	{
		return this->malformedLines;
	}
	//---------------------------------------------------------
	inline unsigned int DebugServer::getDroppedLineCount() const
	{
		return this->connection.getDroppedLineCount();
	}
}

#endif //__DebugServer_h__7_7_2026__23_30_00__
