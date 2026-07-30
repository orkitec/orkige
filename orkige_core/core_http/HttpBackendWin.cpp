/**************************************************************
	created:	2026/07/30 at 19:00
	filename: 	HttpBackendWin.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The Windows transport behind HttpClient, and the ONE translation unit in the
// tree that includes winhttp.h. Nothing above the HttpBackend seam knows it
// exists: HttpClient, the Lua `http` table and every caller stay plain C++,
// the same confinement the Apple backend's Objective-C and the Android
// backend's JNI get.
//
// WHY the platform's stack rather than a bundled library: certificate
// verification must go through the trust store the MACHINE maintains. On
// Windows that store is not a file of public roots - it is the certificate
// stores the OS keeps current, including the enterprise anchors a domain
// pushes down and the ones an administrator installed by hand, and it is
// Schannel that applies the machine's crypto policy to them. WinHTTP also
// resolves the machine's proxy configuration (WINHTTP_ACCESS_TYPE_AUTOMATIC_-
// PROXY covers the system settings AND the per-user browser configuration,
// which the older static-registry access type never sees), and it costs the
// shipped closure nothing: winhttp.dll is part of Windows, so no HTTP library
// and no TLS library are linked into the game.
//
// WHAT STAYS ABOVE IT: every decision. Automatic redirect following is turned
// OFF (WINHTTP_OPTION_REDIRECT_POLICY_NEVER on the session, WINHTTP_DISABLE_-
// REDIRECTS on each request), so each hop comes back here and
// HttpPolicy::resolveRedirect decides whether there is a next one - the rule
// that a secure request can never be redirected onto a plain one keeps ONE
// implementation for every platform. The response cap is checked against the
// ANNOUNCED size and against the bytes arriving, and worded by the shared
// HttpPolicy::sizeCapReason either way. The whole-request deadline is ours
// (WinHttpSetTimeouts bounds a resolve, a connect, a send and a receive - not
// the request), the save-to-file bytes go through the FileWriter funnel, and
// exactly one completion is published per request through the HttpEventQueue.
//
// THE THREADING SHAPE: one worker thread per transfer, walking that request's
// hops top to bottom - the same straight-line shape as the Android backend.
// The main thread only pushes intents (submit/cancel) and drains the event
// queue; a worker publishes its one completion and marks itself reapable, and
// poll() joins it.
//
// WHY ASYNCHRONOUS WinHTTP under that synchronous-looking worker: cancelling a
// transfer that is BLOCKED on the network is a contract every other backend in
// this tree honours, and it is what keeps stop() from stalling a shutdown for
// the length of a request timeout. On Windows the only way to abort an
// in-flight request is to close its handle - and Microsoft documents that
// closing a SYNCHRONOUS request handle "can create a race condition" and must
// never be done, while closing an ASYNCHRONOUS one is the documented way to
// terminate a request in progress (the pending operation then completes with
// ERROR_WINHTTP_OPERATION_CANCELLED). So the session is opened
// WINHTTP_FLAG_ASYNC - which Microsoft recommends anyway - and every
// completion is turned straight back into a blocking wait on the transfer's
// own condition variable. The result reads like the sequential code it is,
// with no state machine, and a cancel unblocks it at once.
//
// TWO LOCKS, each with one job. mApiMutex is held ONLY while a WinHTTP call on
// the request handle is executing, and the canceller takes it to detach the
// handle before closing it: that is exactly the "handles cannot be closed
// while an API call using the handle is in progress" rule. mStateMutex guards
// the completion handshake and is the only lock the status callback takes -
// which matters because WinHTTP may run a completion callback on the calling
// thread, before the initiating function has even returned.

#include "core_http/HttpBackend.h"

#ifdef ORKIGE_HTTP_WIN

// windows.h FIRST, before any engine header: it is the one include whose
// declarations other headers are expected to have seen already, and putting it
// after them is how a stray macro ends up rewriting the Win32 API's own types.
#include <windows.h>
#include <winhttp.h>

#include "core_debug/DebugMacros.h"
#include "core_filesystem/FileWriter.h"
#include "core_http/HttpPolicy.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Orkige
{
	namespace
	{
		//! @brief the body read buffer. Microsoft warns that a small buffer
		//! makes WinHttpReadData complete synchronously and can recurse into
		//! the next read from inside the completion, and names 8 KB (WinHTTP's
		//! own internal read buffer) as the floor - this worker never recurses
		//! (a completion only wakes the waiting thread), and 64 KB keeps the
		//! number of round trips down on a large download.
		const std::size_t READ_CHUNK_BYTES = 64 * 1024;
		//! the connect/resolve slice of a request budget, in milliseconds
		const long long CONNECT_BUDGET_MS = 10000;

		//! is @p status a redirect the client may follow?
		bool isRedirectStatus(int status)
		{
			return status == 301 || status == 302 || status == 303 ||
				status == 307 || status == 308;
		}

		//! @brief does @p status turn the next hop into a GET? 301/302/303 do
		//! by long-standing convention; 307/308 exist precisely to preserve the
		//! method and body.
		bool redirectRewritesToGet(int status)
		{
			return status == 301 || status == 302 || status == 303;
		}

		//! an engine String (UTF-8) as a wide string for the Win32 API
		std::wstring toWide(String const & text)
		{
			if (text.empty())
			{
				return std::wstring();
			}
			const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
				static_cast<int>(text.size()), NULL, 0);
			if (needed <= 0)
			{
				return std::wstring();
			}
			std::wstring wide(static_cast<std::size_t>(needed), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
				static_cast<int>(text.size()), &wide[0], needed);
			return wide;
		}

		//! a wide string of @p length characters as a UTF-8 engine String
		String toUtf8(wchar_t const * text, std::size_t length)
		{
			if (text == NULL || length == 0)
			{
				return String();
			}
			const int needed = WideCharToMultiByte(CP_UTF8, 0, text,
				static_cast<int>(length), NULL, 0, NULL, NULL);
			if (needed <= 0)
			{
				return String();
			}
			String utf8(static_cast<std::size_t>(needed), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length),
				&utf8[0], needed, NULL, NULL);
			return utf8;
		}

		//! a WinHTTP/Win32 error code -> the engine's failure taxonomy
		HttpFailure failureFromWin(DWORD code)
		{
			switch (code)
			{
			case 0:
				return HF_NONE;
			case ERROR_WINHTTP_TIMEOUT:
				return HF_TIMEOUT;
			case ERROR_WINHTTP_NAME_NOT_RESOLVED:
			case ERROR_WINHTTP_CANNOT_CONNECT:
			case ERROR_WINHTTP_CONNECTION_ERROR:
				return HF_CONNECT_FAILED;
			case ERROR_WINHTTP_SECURE_FAILURE:
			case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
			case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
			case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
			case ERROR_WINHTTP_SECURE_CERT_REVOKED:
			case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:
			case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
			case ERROR_WINHTTP_SECURE_INVALID_CA:
			case ERROR_WINHTTP_SECURE_INVALID_CERT:
			case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
				return HF_TLS_FAILED;
			case ERROR_WINHTTP_OPERATION_CANCELLED:
				return HF_CANCELLED;
			case ERROR_WINHTTP_INVALID_URL:
			case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
				return HF_BAD_URL;
			case ERROR_WINHTTP_REDIRECT_FAILED:
				return HF_REDIRECT_REFUSED;
			default:
				return HF_TRANSPORT;
			}
		}

		//! @brief the platform's own words for @p code. WinHTTP's error strings
		//! live in winhttp.dll rather than the system table, so the message
		//! module is asked first and the system table is the fallback.
		String describeWinError(DWORD code)
		{
			wchar_t buffer[512];
			buffer[0] = L'\0';
			DWORD written = FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE |
				FORMAT_MESSAGE_IGNORE_INSERTS, GetModuleHandleW(L"winhttp.dll"),
				code, 0, buffer, 512, NULL);
			if (written == 0)
			{
				written = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
					FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, buffer, 512,
					NULL);
			}
			while (written > 0 && (buffer[written - 1] == L'\n' ||
				buffer[written - 1] == L'\r' || buffer[written - 1] == L' '))
			{
				--written;
			}
			if (written == 0)
			{
				return "the request failed (Windows HTTP error " +
					std::to_string(static_cast<unsigned long>(code)) + ")";
			}
			return toUtf8(buffer, static_cast<std::size_t>(written));
		}

		//! the Content-Length a response announced (0 = none/unparseable)
		unsigned long long announcedLength(std::map<String, String> const & headers)
		{
			std::map<String, String>::const_iterator found =
				headers.find("content-length");
			if (found == headers.end() || found->second.empty())
			{
				return 0;
			}
			char * end = NULL;
			const unsigned long long value =
				std::strtoull(found->second.c_str(), &end, 10);
			return end != NULL && end != found->second.c_str() ? value : 0;
		}
	}
	//---------------------------------------------------------
	class WinHttpBackend;
	//---------------------------------------------------------
	//! @brief the WinHTTP-backed transport (@see the file comment).
	class WinHttpBackend : public HttpBackend
	{
		//--- Types -------------------------------------------
	private:
		//! one in-flight request; its worker thread owns it until it finishes
		struct Transfer
		{
			HttpRequestId			id = 0;
			HttpClientRequest		request;		//!< kept for its body + bounds
			HttpUrlParts			url;			//!< the CURRENT hop's URL
			String					method;			//!< the current hop's method
			bool					sendBody = false;//!< does this hop carry one
			unsigned int			hop = 0;		//!< redirects taken so far
			int						status = 0;		//!< the last hop's status
			String					location;		//!< the last hop's Location
			String					finalUrl;
			std::map<String, String>	headers;	//!< the last hop's headers
			String					body;			//!< in-memory mode
			FileWriter				file;			//!< save-to-file mode
			std::vector<char>		buffer;			//!< the read landing pad
			unsigned long long		received = 0;
			unsigned long long		expected = 0;
			std::chrono::steady_clock::time_point	deadline;
			//--- the transport handles ---------------------------
			//! @brief guards hRequest: held ONLY while a WinHTTP call on that
			//! handle is executing, so the handle can never be closed
			//! underneath one (@see the file comment)
			std::mutex				apiMutex;
			HINTERNET				hConnect = NULL;	//!< worker-owned
			HINTERNET				hRequest = NULL;	//!< guarded by apiMutex
			//--- the async completion handshake ------------------
			std::mutex				stateMutex;		//!< guards the fields below
			std::condition_variable	ready;
			bool					pending = false;	//!< an operation is out
			DWORD					lastStatus = 0;	//!< the completion that came
			DWORD					lastError = 0;	//!< REQUEST_ERROR's code
			DWORD					readLength = 0;	//!< READ_COMPLETE's count
			bool					closingSeen = false;//!< HANDLE_CLOSING came
			bool					callbackArmed = false;//!< a callback + context is installed
			//--- the outcome -------------------------------------
			std::atomic<bool>		cancelled;		//!< cancel() ran
			DWORD					opError = 0;	//!< the transport's own code
			bool					capExceeded = false;
			//! was the cap hit by the ANNOUNCED size (so the refusal can name
			//! it) rather than by the bytes that actually arrived
			bool					capAnnounced = false;
			bool					timedOut = false;
			bool					writeFailed = false;
			String					writeError;
			std::thread				worker;
			std::atomic<bool>		finished;		//!< the main thread may reap
			//! @brief the transport never reported the request handle closed,
			//! so it may still hold this object as a callback context: it is
			//! joined but NEVER freed (@see retireHandles)
			bool					leaked = false;
			Transfer() : cancelled(false), finished(false) {}
		};
		//! @brief holds a transfer's API lock for ONE WinHTTP call on its
		//! request handle. @c handle is NULL when the request was already
		//! closed by a cancel - the caller must stop rather than call.
		//! @remarks the MEMBER ORDER carries the guarantee: the lock is
		//! declared first, so it is taken before the handle is read, and the
		//! handle cannot be detached between the two.
		struct RequestCall
		{
			std::lock_guard<std::mutex>	lock;
			HINTERNET					handle;
			explicit RequestCall(Transfer * transfer)
				: lock(transfer->apiMutex), handle(transfer->hRequest) {}
		};
		//--- Variables ---------------------------------------
	private:
		HINTERNET						mSession;
		bool							mRunning;
		std::mutex						mLiveMutex;	//!< guards mLive
		std::map<HttpRequestId, Transfer *>	mLive;	//!< every unreaped transfer
		HttpEventQueue					mEvents;
		//--- Methods -----------------------------------------
	public:
		WinHttpBackend() : mSession(NULL), mRunning(false) {}
		virtual ~WinHttpBackend() { this->stop(); }
		//---------------------------------------------------------
		bool start() override
		{
			if (this->mRunning)
			{
				return true;
			}
			// WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY resolves the system AND the
			// per-user proxy configuration (including the browser's, which the
			// older static-registry access type never sees) and handles
			// failover between several proxies. It needs Windows 8.1; the
			// deprecated static configuration is the fallback for anything
			// older, so a request is never silently sent direct past a proxy
			// the machine requires.
			this->mSession = WinHttpOpen(L"orkige",
				WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
			if (this->mSession == NULL)
			{
				this->mSession = WinHttpOpen(L"orkige",
					WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
					WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
			}
			if (this->mSession == NULL)
			{
				return false;
			}
			// THE redirect gate: WinHTTP follows 3xx itself by default (and its
			// own https->http guard is a DIFFERENT rule from ours). Turning it
			// off entirely is what routes every hop back through
			// HttpPolicy::resolveRedirect.
			DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
			WinHttpSetOption(this->mSession, WINHTTP_OPTION_REDIRECT_POLICY,
				&policy, static_cast<DWORD>(sizeof(policy)));
			// TLS floor 1.2 (this option is a SESSION-handle option). The
			// chain itself is verified by Schannel against the machine's
			// certificate stores and nothing here weakens that.
			DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
			protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
			if (!WinHttpSetOption(this->mSession, WINHTTP_OPTION_SECURE_PROTOCOLS,
				&protocols, static_cast<DWORD>(sizeof(protocols))))
			{
				// an OS that does not know the TLS 1.3 bit rejects the whole
				// value: ask for the floor alone rather than leave the
				// system default in place
				protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
				WinHttpSetOption(this->mSession, WINHTTP_OPTION_SECURE_PROTOCOLS,
					&protocols, static_cast<DWORD>(sizeof(protocols)));
			}
			this->mRunning = true;
			return true;
		}
		//---------------------------------------------------------
		void stop() override
		{
			if (!this->mRunning)
			{
				if (this->mSession != NULL)
				{
					WinHttpCloseHandle(this->mSession);
					this->mSession = NULL;
				}
				return;
			}
			this->mRunning = false;
			// unblock every worker, then wait for each to publish and leave
			std::vector<Transfer *> live;
			{
				std::lock_guard<std::mutex> lock(this->mLiveMutex);
				for (std::map<HttpRequestId, Transfer *>::const_iterator at =
					this->mLive.begin(); at != this->mLive.end(); ++at)
				{
					live.push_back(at->second);
				}
			}
			for (std::size_t at = 0; at < live.size(); ++at)
			{
				live[at]->cancelled = true;
				this->abortRequest(live[at]);
			}
			for (std::size_t at = 0; at < live.size(); ++at)
			{
				if (live[at]->worker.joinable())
				{
					live[at]->worker.join();
				}
			}
			{
				std::lock_guard<std::mutex> lock(this->mLiveMutex);
				this->mLive.clear();
			}
			for (std::size_t at = 0; at < live.size(); ++at)
			{
				this->release(live[at]);
			}
			// LAST: every request and connection handle derived from the
			// session is gone by now, so closing the parent cannot invalidate
			// one a worker is still using
			if (this->mSession != NULL)
			{
				WinHttpCloseHandle(this->mSession);
				this->mSession = NULL;
			}
			this->mEvents.clear();
		}
		//---------------------------------------------------------
		void submit(HttpRequestId requestId, HttpClientRequest const & request,
			HttpUrlParts const & url) override
		{
			if (!this->mRunning || this->mSession == NULL)
			{
				this->fail(requestId, HF_UNAVAILABLE,
					"the HTTP transport is not up");
				return;
			}
			Transfer * transfer = new Transfer();
			transfer->id = requestId;
			transfer->request = request;
			transfer->url = url;
			transfer->method = request.method;
			transfer->sendBody = !request.body.empty() ||
				HttpPolicy::methodTakesBody(request.method);
			transfer->finalUrl = url.rebuild();
			transfer->buffer.resize(READ_CHUNK_BYTES);
			transfer->deadline = std::chrono::steady_clock::now() +
				std::chrono::milliseconds(request.timeoutMs);
			if (!request.savePath.empty())
			{
				String error;
				if (!transfer->file.begin(request.savePath, error))
				{
					delete transfer;
					this->fail(requestId, HF_BAD_SAVE_PATH, error);
					return;
				}
			}
			{
				std::lock_guard<std::mutex> lock(this->mLiveMutex);
				this->mLive[requestId] = transfer;
			}
			transfer->worker = std::thread(&WinHttpBackend::runTransfer, this,
				transfer);
		}
		//---------------------------------------------------------
		void cancel(HttpRequestId requestId) override
		{
			Transfer * transfer = NULL;
			{
				std::lock_guard<std::mutex> lock(this->mLiveMutex);
				std::map<HttpRequestId, Transfer *>::const_iterator found =
					this->mLive.find(requestId);
				if (found == this->mLive.end())
				{
					return;
				}
				transfer = found->second;
			}
			// the flag stops the loop between chunks; closing the request
			// handle unblocks a transfer that is waiting on the network right
			// now (its pending operation completes CANCELLED)
			transfer->cancelled = true;
			this->abortRequest(transfer);
		}
		//---------------------------------------------------------
		void poll(std::vector<HttpBackendEvent> & out) override
		{
			this->mEvents.drain(out);
			this->reapFinished();
		}
		//---------------------------------------------------------
		char const * name() const override { return "winhttp"; }
	private:
		//---------------------------------------------------------
		//! @brief close @p transfer's request handle from ANOTHER thread.
		//! The handle is detached under the API lock first, so the close can
		//! never race a WinHTTP call that is executing on the worker; the
		//! close itself happens OUTSIDE the lock, because WinHTTP may deliver
		//! the handle-closing callback on this very thread.
		void abortRequest(Transfer * transfer)
		{
			HINTERNET handle = NULL;
			{
				std::lock_guard<std::mutex> lock(transfer->apiMutex);
				handle = transfer->hRequest;
				transfer->hRequest = NULL;
			}
			if (handle != NULL)
			{
				WinHttpCloseHandle(handle);
			}
		}
		//---------------------------------------------------------
		//! @brief the worker: one thread walking this request's hops until the
		//! policy says there is no next one.
		void runTransfer(Transfer * transfer)
		{
			HttpFailure failure = HF_NONE;
			String reason;
			for (;;)
			{
				const bool hopOk = this->performHop(transfer);
				if (hopOk && !transfer->cancelled &&
					transfer->request.followRedirects &&
					isRedirectStatus(transfer->status) &&
					!transfer->location.empty())
				{
					if (transfer->hop >= transfer->request.maxRedirects)
					{
						failure = HF_REDIRECT_REFUSED;
						reason = "the request was redirected more than " +
							std::to_string(transfer->request.maxRedirects) +
							" times";
						break;
					}
					HttpUrlParts next;
					String why;
					const HttpFailure resolved = HttpPolicy::resolveRedirect(
						transfer->url, transfer->location,
						transfer->request.allowInsecureHttp, next, why);
					if (resolved != HF_NONE)
					{
						failure = resolved;
						reason = why;
						break;
					}
					if (redirectRewritesToGet(transfer->status) &&
						transfer->method != "HEAD")
					{
						transfer->method = "GET";
						transfer->sendBody = false;
					}
					transfer->url = next;
					++transfer->hop;
					continue;
				}
				failure = this->failureFor(transfer, reason);
				break;
			}
			this->finish(transfer, failure, reason);
			// LAST: nothing may touch the transfer after the main thread is
			// told it may reap it
			transfer->finished.store(true, std::memory_order_release);
		}
		//---------------------------------------------------------
		//! @brief run ONE exchange (open, send, read the head and, unless this
		//! hop is a redirect we are about to leave, its body).
		//! @return false when the hop did not produce a complete response -
		//! the reason is on the transfer (@see failureFor)
		bool performHop(Transfer * transfer)
		{
			// only the FINAL response is the answer: a redirect hop's head and
			// body replace whatever the previous one left behind
			transfer->status = 0;
			transfer->location.clear();
			transfer->headers.clear();
			transfer->body.clear();
			transfer->received = 0;
			transfer->expected = 0;
			transfer->opError = 0;
			transfer->finalUrl = transfer->url.rebuild();
			const bool completed = this->openAndRun(transfer);
			this->retireHandles(transfer);
			return completed;
		}
		//---------------------------------------------------------
		//! the hop's actual work, with retireHandles() as its epilogue
		bool openAndRun(Transfer * transfer)
		{
			if (transfer->cancelled)
			{
				return false;
			}
			// --- the handles --------------------------------------------
			transfer->hConnect = WinHttpConnect(this->mSession,
				toWide(transfer->url.host).c_str(),
				static_cast<INTERNET_PORT>(transfer->url.effectivePort()), 0);
			if (transfer->hConnect == NULL)
			{
				transfer->opError = GetLastError();
				return false;
			}
			const std::wstring method = toWide(transfer->method);
			const std::wstring path = toWide(transfer->url.path);
			HINTERNET handle = WinHttpOpenRequest(transfer->hConnect,
				method.c_str(), path.c_str(), NULL, WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES,
				transfer->url.secure ? WINHTTP_FLAG_SECURE : 0);
			if (handle == NULL)
			{
				transfer->opError = GetLastError();
				return false;
			}
			// The context is what a completion callback resolves back into
			// this transfer, and it is also what makes WinHTTP deliver the
			// final HANDLE_CLOSING notification - without which the transfer
			// could be freed while a callback is still in flight. Set it
			// BEFORE the callback, and treat a failure as a setup failure.
			DWORD_PTR context = reinterpret_cast<DWORD_PTR>(transfer);
			if (!WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE,
				&context, static_cast<DWORD>(sizeof(context))))
			{
				transfer->opError = GetLastError();
				WinHttpCloseHandle(handle);
				return false;
			}
			if (WinHttpSetStatusCallback(handle, &WinHttpBackend::onStatus,
				WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
				WINHTTP_CALLBACK_FLAG_HANDLES, 0) ==
				WINHTTP_INVALID_STATUS_CALLBACK)
			{
				transfer->opError = GetLastError();
				WinHttpCloseHandle(handle);
				return false;
			}
			// PUBLISH the handle and arm the retirement wait in one step: from
			// here on the handle is always closed (and its HANDLE_CLOSING
			// always waited for) by retireHandles, whichever way this hop ends
			{
				std::lock_guard<std::mutex> lock(transfer->apiMutex);
				transfer->hRequest = handle;
			}
			{
				std::lock_guard<std::mutex> lock(transfer->stateMutex);
				transfer->callbackArmed = true;
			}
			// a cancel that landed while the handle was still being built
			// could not close it - so re-read the flag now that it can be
			// found. Either order of the two is covered: a later cancel takes
			// the API lock and closes the published handle instead.
			if (transfer->cancelled)
			{
				return false;
			}
			// --- the per-request settings -------------------------------
			{
				RequestCall call(transfer);
				if (call.handle == NULL)
				{
					return false;
				}
				// WinHTTP follows redirects, replays cookies and answers auth
				// challenges on its own; all three are decisions this client
				// makes above the transport (or refuses to make at all - no
				// request carries an identity the caller did not put in a
				// header). This option is a REQUEST-handle option and must be
				// set after the handle exists and before the request is sent.
				DWORD features = WINHTTP_DISABLE_REDIRECTS |
					WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
				WinHttpSetOption(call.handle, WINHTTP_OPTION_DISABLE_FEATURE,
					&features, static_cast<DWORD>(sizeof(features)));
				// WinHttpSetTimeouts bounds a name resolution, a connect, a
				// send and a receive - never the request as a whole, which is
				// what the caller asked for. Both are kept: these stop a hop
				// from hanging, and the deadline below stops a trickling
				// server from outliving the request's own budget.
				const int budget = this->remainingMs(transfer);
				const int connectBudget = budget <
					static_cast<int>(CONNECT_BUDGET_MS)
					? budget : static_cast<int>(CONNECT_BUDGET_MS);
				WinHttpSetTimeouts(call.handle, connectBudget, connectBudget,
					budget, budget);
			}
			for (std::size_t at = 0; at < transfer->request.headers.size(); ++at)
			{
				const String line = transfer->request.headers[at].first + ": " +
					transfer->request.headers[at].second + "\r\n";
				const std::wstring wide = toWide(line);
				RequestCall call(transfer);
				if (call.handle == NULL)
				{
					return false;
				}
				WinHttpAddRequestHeaders(call.handle, wide.c_str(),
					static_cast<DWORD>(-1),
					WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
			}
			// --- send ---------------------------------------------------
			if (!this->sendRequest(transfer, context))
			{
				return false;
			}
			// --- the response head --------------------------------------
			if (!this->receiveResponse(transfer))
			{
				return false;
			}
			if (!this->readHead(transfer))
			{
				return false;
			}
			// a hop we are about to leave: its body is not the answer, and
			// letting it through would prepend explanatory HTML to the file
			// the NEXT hop streams
			if (transfer->request.followRedirects &&
				isRedirectStatus(transfer->status) &&
				!transfer->location.empty() &&
				transfer->hop < transfer->request.maxRedirects)
			{
				return true;
			}
			// the ANNOUNCED size is capped too, so an oversized download is
			// refused before its first body byte rather than after
			if (transfer->expected > transfer->request.maxResponseBytes)
			{
				transfer->capExceeded = true;
				transfer->capAnnounced = true;
				return false;
			}
			// a progress step with the known total before any body byte, so a
			// progress bar can size itself right away. Nothing received and no
			// announced total is NOT progress - it is noise a caller cannot
			// use, and it would hide the announced size behind a leading 0.
			if (transfer->expected != 0)
			{
				this->mEvents.pushProgress(transfer->id, 0, transfer->expected);
			}
			return this->readBody(transfer);
		}
		//---------------------------------------------------------
		//! WinHttpSendRequest + its completion
		bool sendRequest(Transfer * transfer, DWORD_PTR context)
		{
			LPVOID body = WINHTTP_NO_REQUEST_DATA;
			DWORD bodyLength = 0;
			if (transfer->sendBody && !transfer->request.body.empty())
			{
				// the buffer must stay valid until the completion arrives -
				// it lives on the transfer, which outlives this call
				body = &transfer->request.body[0];
				bodyLength =
					static_cast<DWORD>(transfer->request.body.size());
			}
			this->armPending(transfer);
			BOOL started = FALSE;
			DWORD error = 0;
			{
				RequestCall call(transfer);
				if (call.handle == NULL)
				{
					this->clearPending(transfer);
					return false;
				}
				started = WinHttpSendRequest(call.handle,
					WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, bodyLength,
					bodyLength, context);
				if (!started)
				{
					error = GetLastError();
				}
			}
			if (!started)
			{
				// a WinHTTP function that fails outright makes NO callback
				this->clearPending(transfer);
				transfer->opError = error;
				return false;
			}
			return this->awaitCompletion(transfer,
				WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
		}
		//---------------------------------------------------------
		//! WinHttpReceiveResponse + its completion
		bool receiveResponse(Transfer * transfer)
		{
			this->armPending(transfer);
			BOOL started = FALSE;
			DWORD error = 0;
			{
				RequestCall call(transfer);
				if (call.handle == NULL)
				{
					this->clearPending(transfer);
					return false;
				}
				started = WinHttpReceiveResponse(call.handle, NULL);
				if (!started)
				{
					error = GetLastError();
				}
			}
			if (!started)
			{
				this->clearPending(transfer);
				transfer->opError = error;
				return false;
			}
			return this->awaitCompletion(transfer,
				WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
		}
		//---------------------------------------------------------
		//! the status line and headers of the response that just arrived
		bool readHead(Transfer * transfer)
		{
			DWORD status = 0;
			DWORD size = static_cast<DWORD>(sizeof(status));
			{
				RequestCall call(transfer);
				if (call.handle == NULL)
				{
					return false;
				}
				if (!WinHttpQueryHeaders(call.handle,
					WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
					WINHTTP_NO_HEADER_INDEX))
				{
					transfer->opError = GetLastError();
					return false;
				}
			}
			transfer->status = static_cast<int>(status);
			// the raw block, sized by the documented two-call pattern: the
			// first call has no buffer, fails with ERROR_INSUFFICIENT_BUFFER
			// and reports the byte count needed
			DWORD needed = 0;
			{
				RequestCall call(transfer);
				if (call.handle == NULL)
				{
					return false;
				}
				const BOOL sized = WinHttpQueryHeaders(call.handle,
					WINHTTP_QUERY_RAW_HEADERS_CRLF,
					WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
					&needed, WINHTTP_NO_HEADER_INDEX);
				if (sized || GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
					needed == 0)
				{
					// no header block to read: the status alone is the answer
					return true;
				}
			}
			std::vector<wchar_t> block(needed / sizeof(wchar_t) + 2, L'\0');
			// the second call is told the buffer's OWN size and reports back
			// how many bytes it wrote
			DWORD written = static_cast<DWORD>(block.size() * sizeof(wchar_t));
			{
				RequestCall call(transfer);
				if (call.handle == NULL)
				{
					return false;
				}
				if (!WinHttpQueryHeaders(call.handle,
					WINHTTP_QUERY_RAW_HEADERS_CRLF,
					WINHTTP_HEADER_NAME_BY_INDEX, &block[0], &written,
					WINHTTP_NO_HEADER_INDEX))
				{
					transfer->opError = GetLastError();
					return false;
				}
			}
			HttpPolicy::parseHeaderBlock(
				toUtf8(&block[0], written / sizeof(wchar_t)), transfer->headers);
			std::map<String, String>::const_iterator location =
				transfer->headers.find("location");
			if (location != transfer->headers.end())
			{
				transfer->location = location->second;
			}
			transfer->expected = announcedLength(transfer->headers);
			return true;
		}
		//---------------------------------------------------------
		//! read the body chunk by chunk, bounding it as it arrives
		bool readBody(Transfer * transfer)
		{
			for (;;)
			{
				if (transfer->cancelled)
				{
					return false;
				}
				if (std::chrono::steady_clock::now() > transfer->deadline)
				{
					// the platform bounds a connect and a receive; the
					// WHOLE-request deadline is ours to keep
					transfer->timedOut = true;
					return false;
				}
				this->armPending(transfer);
				BOOL started = FALSE;
				DWORD error = 0;
				{
					RequestCall call(transfer);
					if (call.handle == NULL)
					{
						this->clearPending(transfer);
						return false;
					}
					// the byte count MUST be read from the completion, never
					// through the out-parameter, when WinHTTP runs async
					started = WinHttpReadData(call.handle,
						&transfer->buffer[0],
						static_cast<DWORD>(transfer->buffer.size()), NULL);
					if (!started)
					{
						error = GetLastError();
					}
				}
				if (!started)
				{
					this->clearPending(transfer);
					transfer->opError = error;
					return false;
				}
				if (!this->awaitCompletion(transfer,
					WINHTTP_CALLBACK_STATUS_READ_COMPLETE))
				{
					return false;
				}
				DWORD length = 0;
				{
					std::lock_guard<std::mutex> lock(transfer->stateMutex);
					length = transfer->readLength;
				}
				if (length == 0)
				{
					return true;	// end of the response
				}
				const unsigned long long size =
					static_cast<unsigned long long>(length);
				if (transfer->received + size >
					transfer->request.maxResponseBytes)
				{
					transfer->capExceeded = true;
					return false;
				}
				if (transfer->request.savePath.empty())
				{
					transfer->body.append(&transfer->buffer[0],
						static_cast<std::size_t>(length));
				}
				else if (!transfer->file.write(&transfer->buffer[0], size,
					transfer->writeError))
				{
					transfer->writeFailed = true;
					return false;
				}
				transfer->received += size;
				this->mEvents.pushProgress(transfer->id, transfer->received,
					transfer->expected);
			}
		}
		//---------------------------------------------------------
		//! @brief close this hop's handles and WAIT for the last callback.
		//! Microsoft's rule: a context bound to a handle must stay alive until
		//! the handle-closing notification arrives, because a callback can
		//! still be running after the close returns. This wait is what makes
		//! it safe for poll() to delete the transfer afterwards - and it
		//! serves the cancel path too, where ANOTHER thread did the closing.
		void retireHandles(Transfer * transfer)
		{
			HINTERNET handle = NULL;
			{
				std::lock_guard<std::mutex> lock(transfer->apiMutex);
				handle = transfer->hRequest;
				transfer->hRequest = NULL;
			}
			if (handle != NULL)
			{
				WinHttpCloseHandle(handle);
			}
			bool stranded = false;
			{
				std::unique_lock<std::mutex> lock(transfer->stateMutex);
				if (transfer->callbackArmed)
				{
					// BOUNDED, and the bound is not a shortcut: if the
					// notification never came, WinHTTP may still hold the
					// context, so the transfer is marked unreapable and simply
					// never freed rather than deleted underneath a callback.
					// A hang would be the worse answer, a use-after-free the
					// worst.
					stranded = !transfer->ready.wait_for(lock,
						std::chrono::seconds(15),
						[transfer]() { return transfer->closingSeen; });
					transfer->leaked = transfer->leaked || stranded;
					transfer->closingSeen = false;
					transfer->callbackArmed = false;
				}
			}
			if (stranded)
			{
				oDebugWarn("http", 0, "the transport did not report a closed "
					"request handle - its state is retained rather than freed");
			}
			if (transfer->hConnect != NULL)
			{
				// no callback and no context were ever bound to this one, so
				// it just goes
				WinHttpCloseHandle(transfer->hConnect);
				transfer->hConnect = NULL;
			}
		}
		//---------------------------------------------------------
		//! arm the handshake for ONE asynchronous operation
		void armPending(Transfer * transfer)
		{
			std::lock_guard<std::mutex> lock(transfer->stateMutex);
			transfer->pending = true;
			transfer->lastStatus = 0;
			transfer->lastError = 0;
			transfer->readLength = 0;
		}
		//---------------------------------------------------------
		//! disarm it again (the operation never started, so no callback comes)
		void clearPending(Transfer * transfer)
		{
			std::lock_guard<std::mutex> lock(transfer->stateMutex);
			transfer->pending = false;
		}
		//---------------------------------------------------------
		//! @brief block until the armed operation completes.
		//! @return true when @p expected arrived; false when it failed (the
		//! transport's code is recorded on the transfer)
		bool awaitCompletion(Transfer * transfer, DWORD expected)
		{
			DWORD status = 0;
			DWORD error = 0;
			{
				std::unique_lock<std::mutex> lock(transfer->stateMutex);
				transfer->ready.wait(lock,
					[transfer]() { return !transfer->pending; });
				status = transfer->lastStatus;
				error = transfer->lastError;
			}
			if (status == expected)
			{
				return true;
			}
			// the only other completion an armed operation can produce is the
			// error one - including the CANCELLED a closed handle raises
			transfer->opError = error != 0 ? error : ERROR_WINHTTP_INTERNAL_ERROR;
			return false;
		}
		//---------------------------------------------------------
		//! the milliseconds left of this request's whole-request budget
		int remainingMs(Transfer * transfer) const
		{
			const long long remaining = std::chrono::duration_cast<
				std::chrono::milliseconds>(transfer->deadline -
					std::chrono::steady_clock::now()).count();
			if (remaining < 1)
			{
				return 1;
			}
			return remaining > 2147483647LL ? 2147483647
				: static_cast<int>(remaining);
		}
		//---------------------------------------------------------
		//! what the hop recorded -> the ONE failure the caller is told
		HttpFailure failureFor(Transfer * transfer, String & reason) const
		{
			// what stopped the transfer outranks the stop itself
			if (transfer->capExceeded)
			{
				reason = HttpPolicy::sizeCapReason(
					transfer->request.maxResponseBytes,
					transfer->capAnnounced ? transfer->expected : 0);
				return HF_TOO_LARGE;
			}
			if (transfer->writeFailed)
			{
				reason = transfer->writeError;
				return HF_WRITE_FAILED;
			}
			if (transfer->timedOut)
			{
				reason = "the request passed its " +
					std::to_string(transfer->request.timeoutMs) + "ms deadline";
				return HF_TIMEOUT;
			}
			if (transfer->cancelled)
			{
				reason = "the request was cancelled";
				return HF_CANCELLED;
			}
			if (transfer->opError != 0)
			{
				const HttpFailure failure = failureFromWin(transfer->opError);
				reason = failure == HF_CANCELLED
					? String("the request was cancelled")
					: describeWinError(transfer->opError);
				return failure;
			}
			reason.clear();
			return HF_NONE;
		}
		//---------------------------------------------------------
		//! publish the ONE completion for a transfer
		void finish(Transfer * transfer, HttpFailure failure,
			String const & reason)
		{
			HttpClientResponse response;
			response.id = transfer->id;
			response.status = transfer->status;
			response.bytes = transfer->received;
			response.finalUrl = transfer->finalUrl;
			response.headers = transfer->headers;
			if (failure == HF_NONE)
			{
				if (!transfer->request.savePath.empty())
				{
					String error;
					if (!transfer->file.commit(error))
					{
						response.failure = HF_WRITE_FAILED;
						response.reason = error;
						this->mEvents.pushCompletion(response);
						return;
					}
					response.savedPath = transfer->request.savePath;
				}
				else
				{
					response.body.swap(transfer->body);
				}
				response.completed = true;
			}
			else
			{
				// a failed or cancelled save-to-file leaves NO file behind
				transfer->file.abort();
				response.failure = failure;
				response.reason = reason;
			}
			this->mEvents.pushCompletion(response);
		}
		//---------------------------------------------------------
		//! refuse a request that never reached the transport
		void fail(HttpRequestId requestId, HttpFailure failure,
			String const & reason)
		{
			HttpClientResponse response;
			response.id = requestId;
			response.failure = failure;
			response.reason = reason;
			this->mEvents.pushCompletion(response);
		}
		//---------------------------------------------------------
		//! @brief free a finished transfer - unless the transport may still
		//! hold it as a callback context (@see retireHandles)
		void release(Transfer * transfer)
		{
			if (transfer->leaked)
			{
				return;
			}
			delete transfer;
		}
		//---------------------------------------------------------
		//! join and destroy every worker that has published its answer
		void reapFinished()
		{
			std::vector<Transfer *> done;
			{
				std::lock_guard<std::mutex> lock(this->mLiveMutex);
				std::map<HttpRequestId, Transfer *>::iterator at =
					this->mLive.begin();
				while (at != this->mLive.end())
				{
					if (at->second->finished.load(std::memory_order_acquire))
					{
						done.push_back(at->second);
						at = this->mLive.erase(at);
					}
					else
					{
						++at;
					}
				}
			}
			for (std::size_t at = 0; at < done.size(); ++at)
			{
				if (done[at]->worker.joinable())
				{
					done[at]->worker.join();
				}
				this->release(done[at]);
			}
		}
		//---------------------------------------------------------
		//! @brief the WinHTTP status callback - runs on a transport thread (or
		//! on the worker itself, when an operation completes inline). It only
		//! records what happened and wakes the waiting worker: no network
		//! work, no allocation and no long lock is ever done from here.
		static void CALLBACK onStatus(HINTERNET, DWORD_PTR context,
			DWORD status, LPVOID information, DWORD length)
		{
			Transfer * transfer = reinterpret_cast<Transfer *>(context);
			if (transfer == NULL)
			{
				return;
			}
			switch (status)
			{
			case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
			case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
				{
					std::lock_guard<std::mutex> lock(transfer->stateMutex);
					transfer->lastStatus = status;
					transfer->pending = false;
				}
				transfer->ready.notify_all();
				break;
			case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
				{
					std::lock_guard<std::mutex> lock(transfer->stateMutex);
					transfer->lastStatus = status;
					// a zero-length read is the end of the response
					transfer->readLength = length;
					transfer->pending = false;
				}
				transfer->ready.notify_all();
				break;
			case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
				{
					WINHTTP_ASYNC_RESULT * result =
						static_cast<WINHTTP_ASYNC_RESULT *>(information);
					std::lock_guard<std::mutex> lock(transfer->stateMutex);
					transfer->lastStatus = status;
					transfer->lastError = result != NULL
						? result->dwError : ERROR_WINHTTP_INTERNAL_ERROR;
					transfer->pending = false;
				}
				transfer->ready.notify_all();
				break;
			case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
				{
					// the LAST callback for this handle: after it, no thread
					// of WinHTTP's holds the context any more
					std::lock_guard<std::mutex> lock(transfer->stateMutex);
					transfer->closingSeen = true;
				}
				transfer->ready.notify_all();
				break;
			default:
				break;
			}
		}
	};
	//---------------------------------------------------------
	HttpBackend * createHttpBackend()
	{
		return new WinHttpBackend();
	}
}

#endif // ORKIGE_HTTP_WIN
