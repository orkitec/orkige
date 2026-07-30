/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpBackend.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __HttpBackend_h__30_7_2026__10_00_00__
#define __HttpBackend_h__30_7_2026__10_00_00__

#include "core_http/HttpPolicy.h"
#include "core_http/HttpTypes.h"
#include "core_module/OrkigePrerequisites.h"

#include <mutex>
#include <vector>

namespace Orkige
{
	/** \addtogroup Http
	*  @{ */

	//! @brief one thing that happened to a request, handed from the transport
	//! to the main thread: either a progress step or the final completion.
	struct HttpBackendEvent
	{
		HttpRequestId		id = 0;
		bool				completion = false;	//!< false = a progress step
		unsigned long long	received = 0;		//!< body bytes so far
		unsigned long long	total = 0;			//!< expected total (0 = unknown)
		HttpClientResponse	response;			//!< filled when completion
	};

	//! @brief the transport -> main-thread handover: a mutex-guarded event
	//! queue drained once per frame, the same shape the physics contact queue
	//! uses (worker threads push, the main thread swaps the buffer out and
	//! works on it lock-free).
	//! @remarks progress events COALESCE: a pending, undrained progress step
	//! for a request is overwritten by the next one, so a fast transfer can
	//! never flood the frame with thousands of stale callbacks - a progress
	//! bar only ever wants the latest number.
	class ORKIGE_CORE_DLL HttpEventQueue
	{
		//--- Variables ---------------------------------------
	private:
		mutable std::mutex				mMutex;	//!< guards mQueue
		std::vector<HttpBackendEvent>	mQueue;	//!< events awaiting a drain
		//--- Methods -----------------------------------------
	public:
		//! queue (or coalesce onto) a progress step for @p id
		void pushProgress(HttpRequestId id, unsigned long long received,
			unsigned long long total);
		//! queue the ONE final completion for a request
		void pushCompletion(HttpClientResponse const & response);
		//! @brief hand every queued event over into @p out (which is cleared
		//! first); the swap keeps both buffers warm, so a steady-state frame
		//! drains without allocating
		void drain(std::vector<HttpBackendEvent> & out);
		//! drop everything queued (teardown)
		void clear();
	};

	//! @brief the per-platform transport behind HttpClient - the ONE seam.
	//! Exactly one implementation is compiled per platform (the CMake picks
	//! the TU, the HapticManager/HapticBridgeApple split), so no call site
	//! carries a platform #ifdef:
	//!
	//!   HttpBackendApple.mm   macOS + iOS   NSURLSession
	//!   HttpBackendCurl.cpp   Windows/Linux/Android   libcurl
	//!   HttpBackendFetch.cpp  wasm          the browser's fetch
	//!   HttpBackendNone.cpp   ORKIGE_HTTP=OFF   honest refusals
	//!
	//! THE THREADING CONTRACT: submit()/cancel()/poll() are called on the MAIN
	//! thread only. Whether a backend does its work on a worker thread (curl),
	//! in the platform's own queue (NSURLSession) or in the page's event loop
	//! (fetch) is its own business - it publishes results ONLY through an
	//! HttpEventQueue, and poll() is where the main thread picks them up.
	class HttpBackend
	{
	public:
		virtual ~HttpBackend() = default;
		//! @brief bring the transport up; false = this build/platform cannot
		//! (the client then refuses submissions honestly)
		virtual bool start() = 0;
		//! tear the transport down, dropping every in-flight transfer
		virtual void stop() = 0;
		//! @brief begin a VALIDATED request (@see HttpPolicy::validate ran
		//! already); the backend must publish exactly one completion for it
		virtual void submit(HttpRequestId id, HttpClientRequest const & request,
			HttpUrlParts const & url) = 0;
		//! @brief abort a request; a completion may or may not still arrive -
		//! the client drops it (it already answered the caller)
		virtual void cancel(HttpRequestId id) = 0;
		//! main-thread drain of whatever finished/progressed since the last call
		virtual void poll(std::vector<HttpBackendEvent> & out) = 0;
		//! the transport's name for logs and diagnostics ("curl", "fetch", ...)
		virtual char const * name() const = 0;
	};

	//! @brief create the platform's backend - DEFINED ONCE per platform in the
	//! backend TU the build selected (never called by application code).
	HttpBackend * createHttpBackend();

	/** @} */
}

#endif //__HttpBackend_h__30_7_2026__10_00_00__
