/**************************************************************
	created:	2026/07/07 at 23:30
	filename: 	DebugClient.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __DebugClient_h__7_7_2026__23_30_00__
#define __DebugClient_h__7_7_2026__23_30_00__

#include "core_debugnet/DebugProtocol.h"
#include "core_debugnet/DebugSocket.h"

#include <deque>

namespace Orkige
{
	//! @brief editor end of the remote debugging link: connects to a
	//! DebugServer on localhost and exchanges DebugMessages over it.
	//! @remarks fully non-blocking: connect() only starts the TCP handshake,
	//! update() (pump every frame) completes it and moves the client through
	//! Connecting -> Connected; a refused/failed connect ends in Failed, a
	//! peer drop after connecting in Disconnected (both consumed by starting
	//! the next connect()). The caller owns retry policy - the runtime needs
	//! a moment to boot before it listens, so callers re-connect() until it
	//! answers. Malformed lines are counted and dropped, never fatal.
	class ORKIGE_CORE_DLL DebugClient
	{
		//--- Types -------------------------------------------
	public:
		//! connection lifecycle
		enum class State
		{
			Idle,			//!< never connected / disconnect()ed
			Connecting,		//!< TCP handshake in flight
			Connected,		//!< link is up
			Failed,			//!< connect attempt failed (refused/timeout)
			Disconnected	//!< peer dropped after the link was up
		};
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		DebugSocketUtil::SocketHandle	pendingHandle;	//!< socket during the handshake
		DebugLineConnection				connection;		//!< the live link
		State							state;			//!< connection lifecycle state
		unsigned int					malformedLines;	//!< dropped undecodable lines
		std::deque<DebugMessage>		received;		//!< decoded inbound messages
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor (idle)
		DebugClient();
		//! destructor (closes the link)
		~DebugClient();
		//! @brief start a non-blocking connect to host:port (host must be a
		//! dotted-quad address, normally "127.0.0.1"); returns false when the
		//! socket setup fails immediately
		bool connect(String const & host, unsigned short port);
		//! close the link and return to Idle
		void disconnect();
		//! handshake/read/write pump - call once per frame, never blocks
		void update();
		//! connection lifecycle state
		inline State getState() const;
		//! is the link up
		inline bool isConnected() const;
		//! is the TCP handshake still in flight
		inline bool isConnecting() const;
		//! pop the next received message; false when none is pending
		bool receive(DebugMessage & out);
		//! @brief send a message; false when the link is down or the encoded
		//! line was refused/failed
		bool send(DebugMessage const & message);
		//! number of received lines that failed to decode (dropped)
		inline unsigned int getMalformedLineCount() const;
	protected:
		//! drain complete lines from the connection into received
		void drainLines();
	private:
		DebugClient(DebugClient const &) = delete;
		DebugClient & operator=(DebugClient const &) = delete;
	};
	//---------------------------------------------------------
	inline DebugClient::State DebugClient::getState() const
	{
		return this->state;
	}
	//---------------------------------------------------------
	inline bool DebugClient::isConnected() const
	{
		return this->state == State::Connected;
	}
	//---------------------------------------------------------
	inline bool DebugClient::isConnecting() const
	{
		return this->state == State::Connecting;
	}
	//---------------------------------------------------------
	inline unsigned int DebugClient::getMalformedLineCount() const
	{
		return this->malformedLines;
	}
}

#endif //__DebugClient_h__7_7_2026__23_30_00__
