/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpClient.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __HttpClient_h__30_7_2026__10_00_00__
#define __HttpClient_h__30_7_2026__10_00_00__

#include "core_http/HttpBackend.h"
#include "core_http/HttpTypes.h"
#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

namespace Orkige
{
	/** \addtogroup Http
	*  @{ */

	//! @brief the engine's HTTP(S) client: the ONE seam a game, a tool or the
	//! engine itself talks to when it needs something off a web server -
	//! a leaderboard, remote config, a backend API, an asset download.
	//!
	//! ASYNC BY CONSTRUCTION. There is no blocking call in this class. submit()
	//! returns immediately with a handle; the transfer progresses off the main
	//! thread (or in the platform's own event loop); and its PROGRESS and
	//! COMPLETION are delivered ONLY from update(), which the host runtime
	//! calls at a frame boundary. A game therefore never stalls a frame on the
	//! network, and a callback never runs in the middle of a world update - the
	//! same discipline the physics contact drain and the injected-input applier
	//! follow.
	//!
	//! THE CALLBACK CONTRACT: every submitted request delivers EXACTLY ONE
	//! completion, on the main thread, from update() - a success, an HTTP
	//! status, a refusal or a cancellation. A caller that got a handle can
	//! always rely on being told how it ended (except after cancelOwner() /
	//! cancelAll(), which drop the callbacks precisely because their owner is
	//! going away and must not be called into).
	//!
	//! SECURE BY DEFAULT. Certificates are verified through the PLATFORM's
	//! trust store; https is the default and a plain-http URL needs the
	//! caller's explicit opt-in; a redirect from https to http is refused; the
	//! per-request timeout and response-size cap are always enforced. Every
	//! refusal reports a failure code AND a one-line reason - the client never
	//! fails silently. The rules themselves are pure and unit-tested in
	//! HttpPolicy; the trust model is documented in Docs/http.md.
	//!
	//! @remarks like SaveStore and TweenManager this is an OWNED singleton: a
	//! runtime that wants HTTP creates one (the player does), and code that
	//! reaches for it guards on getSingletonPtr() - so the Lua `http` table is
	//! an honest no-op in a host that has none (the editor's edit mode).
	//! @remarks NO MCP verb exposes this. The editor's MCP endpoint gives an
	//! agent control over the EDITOR, not a general network egress path out of
	//! the machine - the same reasoning that keeps git mutations off MCP.
	class ORKIGE_CORE_DLL HttpClient : public Singleton<HttpClient>
	{
		DECL_OSINGLETON(HttpClient);
		//--- Types -------------------------------------------
	private:
		//! one submitted request the client still owes an answer for
		struct Pending
		{
			HttpRequestId			id = 0;
			HttpCompleteCallback	onComplete;
			HttpProgressCallback	onProgress;
			void const *			owner = NULL;	//!< the sandbox/system that asked
			String					url;			//!< for the log line only
			bool					cancelled = false;	//!< cancel() ran, answer pending
		};
		//--- Variables ---------------------------------------
	public:
		//! the default cap a request carries when the caller sets none
		static const unsigned long long DEFAULT_MAX_RESPONSE_BYTES;
		//--- Variables ---------------------------------------
	private:
		std::unique_ptr<HttpBackend>	mBackend;		//!< the platform transport (lazy)
		bool							mBackendStarted;//!< did start() succeed
		bool							mBackendFailed;	//!< start() failed - refuse honestly
		HttpRequestId					mNextId;		//!< handle allocator
		std::map<HttpRequestId, Pending>	mPending;	//!< requests awaiting an answer
		std::vector<HttpClientResponse>	mImmediate;		//!< refusals to deliver next update()
		std::vector<HttpRequestId>		mCancelled;		//!< cancel()ed ids to answer next update()
		std::vector<HttpBackendEvent>	mDrainScratch;	//!< reused drain buffer (no per-frame allocation)
		String							mUserAgent;		//!< the User-Agent header default
		unsigned long long				mCompletedCount;//!< completions delivered (diagnostics)
		//--- Methods -----------------------------------------
	public:
		//! constructor (no transport is created until the first submit)
		HttpClient();
		//! destructor - drops every in-flight transfer WITHOUT calling back
		~HttpClient();

		//! @brief does this BUILD carry a real HTTP transport? False in an
		//! ORKIGE_HTTP=OFF build, where every submission refuses with
		//! HF_UNAVAILABLE and an honest reason.
		static bool compiled();
		//! @brief can this client serve requests (a real transport that came
		//! up)? @see compiled - a transport that failed to start also reports
		//! false here, and submissions refuse with the reason.
		bool available() const;
		//! the transport's name ("apple", "curl", "fetch", "none")
		char const * backendName() const;

		//! @brief submit a request. Returns its handle IMMEDIATELY - never
		//! blocks, never resolves a name, never opens a socket on this thread.
		//! A request the security policy refuses still gets a handle: the
		//! refusal arrives through @p onComplete at the next update(), so the
		//! caller has ONE error path instead of two.
		//! @param owner an optional token (the script sandbox, a subsystem)
		//! that cancelOwner() can retire in one call; NULL = unowned
		//! @return the handle, or 0 when @p onComplete is empty (a request
		//! nobody listens to is a caller mistake, reported as a log line)
		HttpRequestId submit(HttpClientRequest const & request,
			HttpCompleteCallback const & onComplete,
			HttpProgressCallback const & onProgress = HttpProgressCallback(),
			void const * owner = NULL);

		//! @brief abort a request. The caller still gets its ONE completion,
		//! with HF_CANCELLED, at the next update().
		//! @return false when the handle is unknown (already completed)
		bool cancel(HttpRequestId id);
		//! @brief retire every request submitted with @p owner - SILENTLY: the
		//! owner is going away (a script sandbox being torn down, a screen
		//! closing), so calling back into it would be unsafe.
		//! @return how many were retired
		int cancelOwner(void const * owner);
		//! @brief drop every in-flight request without calling back (teardown)
		void cancelAll();

		//! @brief THE FRAME BOUNDARY: drain the transport and deliver progress
		//! and completion callbacks on this thread. Call once per frame.
		void update();

		//! how many requests are still awaiting an answer
		std::size_t getPendingCount() const { return this->mPending.size(); }
		//! how many completions have been delivered (diagnostics/tests)
		unsigned long long getCompletedCount() const
		{
			return this->mCompletedCount;
		}
		//! @brief the User-Agent every request sends unless it sets its own
		//! (a server-side log wants to know which engine build called)
		void setUserAgent(String const & userAgent)
		{
			this->mUserAgent = userAgent;
		}
		//! @see HttpClient::setUserAgent
		String const & getUserAgent() const { return this->mUserAgent; }
	private:
		//! bring the transport up on first use (zero cost when never used)
		bool ensureBackend();
		//! deliver one completion to its pending entry and retire it
		void deliver(HttpClientResponse const & response);
		//! queue a refusal that update() will deliver
		void refuse(HttpRequestId id, HttpFailure failure, String const & reason);

		HttpClient(HttpClient const &) = delete;
		HttpClient & operator=(HttpClient const &) = delete;
	};

	/** @} */
}

#endif //__HttpClient_h__30_7_2026__10_00_00__
