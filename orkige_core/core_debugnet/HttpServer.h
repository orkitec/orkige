/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	HttpServer.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __HttpServer_h__7_9_2026__12_00_00__
#define __HttpServer_h__7_9_2026__12_00_00__

#include "core_debugnet/DebugSocket.h"
#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace Orkige
{
	//! @brief one parsed HTTP/1.1 request (request line + headers + body).
	//! Header names are stored lower-cased so lookups are case-insensitive
	//! (HTTP header names are case-insensitive by the spec).
	struct ORKIGE_CORE_DLL HttpRequest
	{
		String								method;		//!< "POST", "GET", ...
		String								target;		//!< request target, e.g. "/mcp"
		std::map<String, String>			headers;	//!< lower-cased name -> value
		String								body;		//!< entity body (Content-Length bytes)

		//! a header value by (lower-cased) name, "" when absent
		String header(String const & lowerName) const;
	};

	//! @brief one HTTP/1.1 response the handler returns for a request.
	struct ORKIGE_CORE_DLL HttpResponse
	{
		int									status = 200;					//!< status code
		String								reason = "OK";					//!< reason phrase
		String								contentType = "application/json";	//!< Content-Type (empty = omit)
		String								body;							//!< entity body
		std::vector<std::pair<String, String> > extraHeaders;			//!< additional response headers
		bool								closeConnection = false;		//!< close after sending
	};

	//! @brief a hand-rolled, single-purpose, non-blocking HTTP/1.1 server for
	//! ONE loopback endpoint (the editor's in-process MCP endpoint).
	//! @remarks NOT a general web server: it binds 127.0.0.1 only, reuses the
	//! DebugSocketUtil BSD-socket seam (the same non-blocking accept/poll/recv/
	//! send plumbing the debug link uses) and frames requests by Content-Length
	//! instead of the debug link's '\n' lines. It supports HTTP/1.1 keep-alive
	//! and a handful of concurrent connections (an MCP client may pool a few),
	//! caps request size so a broken/hostile peer cannot exhaust memory, and
	//! never blocks - update() must be pumped every frame. Everything the MCP
	//! protocol layer needs lives ABOVE this (EditorControlServer): this class
	//! knows nothing about JSON-RPC.
	class ORKIGE_CORE_DLL HttpServer
	{
		//--- Types -------------------------------------------
	public:
		//! request handler: given a fully parsed request, return its response
		typedef std::function<HttpResponse(HttpRequest const &)> Handler;
	protected:
		//! one accepted client connection with its raw byte buffers
		struct Connection
		{
			DebugSocketUtil::SocketHandle	handle;		//!< the socket
			String							inBuffer;	//!< received bytes not yet consumed
			String							outBuffer;	//!< queued response bytes
			bool							open;		//!< false once closed/errored
			bool							closeAfterFlush;	//!< half-close once outBuffer drains
		};
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		DebugSocketUtil::SocketHandle	listenHandle;	//!< listening socket
		unsigned short					port;			//!< bound port (resolved when start() got 0)
		std::vector<Connection>			connections;	//!< live client connections
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor (not listening)
		HttpServer();
		//! destructor (stops listening, drops all clients)
		~HttpServer();
		//! @brief start listening on 127.0.0.1:port (0 = pick a free port, query
		//! it with getPort()); returns false when the socket setup fails
		bool start(unsigned short listenPort);
		//! stop listening and drop all clients
		void stop();
		//! accept/read/dispatch/write pump - call once per frame, never blocks
		void update(Handler const & handler);
		//! is the listen socket up
		bool isListening() const
		{
			return this->listenHandle != DebugSocketUtil::INVALID_SOCKET_HANDLE;
		}
		//! the bound port (valid after a successful start())
		unsigned short getPort() const { return this->port; }
	protected:
		//! accept every pending connection into connections
		void acceptPending();
		//! non-blocking recv/send for one connection (poll-gated)
		void pump(Connection & connection);
		//! parse and dispatch as many complete requests as inBuffer holds
		void serviceConnection(Connection & connection, Handler const & handler);
		//! build the wire bytes for a response and queue them on the connection
		void queueResponse(Connection & connection, HttpResponse const & response,
			bool keepAlive);
	private:
		HttpServer(HttpServer const &) = delete;
		HttpServer & operator=(HttpServer const &) = delete;
	};
}

#endif //__HttpServer_h__7_9_2026__12_00_00__
