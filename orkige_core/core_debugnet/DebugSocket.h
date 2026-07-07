/**************************************************************
	created:	2026/07/07 at 23:30
	filename: 	DebugSocket.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __DebugSocket_h__7_7_2026__23_30_00__
#define __DebugSocket_h__7_7_2026__23_30_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

namespace Orkige
{
	//! @brief platform seam for the debug protocol's BSD sockets: everything
	//! platform-specific (handle type, non-blocking mode, close, error codes)
	//! goes through here so a winsock port only touches DebugSocket.{h,cpp}.
	namespace DebugSocketUtil
	{
#ifdef _WIN32
		typedef unsigned long long SocketHandle;	//!< winsock SOCKET (UINT_PTR)
#else
		typedef int SocketHandle;					//!< POSIX file descriptor
#endif
		extern ORKIGE_CORE_DLL const SocketHandle INVALID_SOCKET_HANDLE;

		//! process-wide transport init (winsock WSAStartup seam; POSIX no-op,
		//! aside from ignoring SIGPIPE so a dead peer never kills the process)
		bool ORKIGE_CORE_DLL initialise();
		//! create a non-blocking TCP socket (INVALID_SOCKET_HANDLE on failure)
		SocketHandle ORKIGE_CORE_DLL createNonBlockingTcpSocket();
		//! close a socket handle (safe on INVALID_SOCKET_HANDLE)
		void ORKIGE_CORE_DLL closeSocket(SocketHandle & handle);
		//! did the last socket call fail with would-block/in-progress
		bool ORKIGE_CORE_DLL lastErrorWouldBlock();
	}

	//! @brief a line-framed, non-blocking connection used by both protocol
	//! ends: buffers partial reads/writes and splits the byte stream back
	//! into '\n'-terminated lines.
	//! @remarks defensive by construction: lines longer than
	//! DebugProtocol::MAX_LINE_LENGTH are refused on send and discarded on
	//! receive (the connection survives, only the oversized line is lost);
	//! peer close and socket errors surface through isOpen() after pump().
	class ORKIGE_CORE_DLL DebugLineConnection
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		DebugSocketUtil::SocketHandle	handle;			//!< connected socket or INVALID_SOCKET_HANDLE
		String							inBuffer;		//!< received bytes not yet split into lines
		String							outBuffer;		//!< queued bytes the socket has not accepted yet
		bool							open;			//!< false once the peer closed or a socket error hit
		bool							discardingLine;	//!< inside an oversized line: drop bytes until '\n'
		unsigned int					partialLength;	//!< length of the unterminated line at the buffer tail
		unsigned int					droppedLines;	//!< count of discarded oversized lines
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor (unattached)
		DebugLineConnection();
		//! destructor (closes the socket)
		~DebugLineConnection();
		//! take ownership of an already connected non-blocking socket
		void attach(DebugSocketUtil::SocketHandle socketHandle);
		//! close the socket and clear all buffers
		void close();
		//! is a live socket attached (turns false when pump() hits EOF/error)
		inline bool isOpen() const;
		//! @brief queue one line for sending (terminator is appended here)
		//! and try to flush; returns false when the connection is down or the
		//! line exceeds DebugProtocol::MAX_LINE_LENGTH (nothing is queued then)
		bool sendLine(String const & line);
		//! @brief non-blocking read/write pump - call once per frame (or in a
		//! polling loop); detects peer shutdown and socket errors
		void pump();
		//! @brief pop the next complete received line (without terminator);
		//! returns false when no complete line is buffered
		bool nextLine(String & out);
		//! number of oversized incoming lines that were discarded
		inline unsigned int getDroppedLineCount() const;
	protected:
	private:
		DebugLineConnection(DebugLineConnection const &) = delete;
		DebugLineConnection & operator=(DebugLineConnection const &) = delete;
	};
	//---------------------------------------------------------
	inline bool DebugLineConnection::isOpen() const
	{
		return this->open;
	}
	//---------------------------------------------------------
	inline unsigned int DebugLineConnection::getDroppedLineCount() const
	{
		return this->droppedLines;
	}
}

#endif //__DebugSocket_h__7_7_2026__23_30_00__
