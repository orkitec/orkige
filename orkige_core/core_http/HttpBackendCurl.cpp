/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpBackendCurl.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The Windows / Linux / Android transport behind HttpClient, and the ONE
// translation unit in the tree that includes libcurl. Nothing above the
// HttpBackend seam knows this file exists: HttpClient, the Lua `http` table and
// every caller stay plain C++ (the engine_sound/StbVorbisImpl.cpp confinement
// pattern), so the library can be swapped or dropped per platform without
// touching a line of game-facing code.
//
// WHY libcurl here and the platform stack on Apple/web: certificate
// verification must go through the trust store the PLATFORM maintains. Windows
// gets that from curl's Schannel backend (the vcpkg default there) and
// Linux/Android from OpenSSL over the system CA store, so this file never ships
// a CA bundle of its own. Apple has no such curl backend any more, and a
// browser has no sockets at all - hence their own backends.
//
// THE THREADING SHAPE: one worker thread owns the curl multi handle and every
// easy handle on it. The main thread only ever pushes intents (submit/cancel)
// into mutex-guarded queues and drains results out of the HttpEventQueue - the
// same worker-pushes/main-drains discipline the physics contact queue uses.

#include "core_http/HttpBackend.h"

#ifdef ORKIGE_HTTP_CURL

#include "core_filesystem/FileWriter.h"
#include "core_http/HttpPolicy.h"

#include <curl/curl.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Orkige
{
	namespace
	{
		//! CURLcode -> the engine's failure taxonomy
		HttpFailure failureFromCurl(CURLcode code)
		{
			switch (code)
			{
			case CURLE_OK:
				return HF_NONE;
			case CURLE_OPERATION_TIMEDOUT:
				return HF_TIMEOUT;
			case CURLE_COULDNT_RESOLVE_HOST:
			case CURLE_COULDNT_RESOLVE_PROXY:
			case CURLE_COULDNT_CONNECT:
			case CURLE_SEND_ERROR:
			case CURLE_RECV_ERROR:
				return HF_CONNECT_FAILED;
			case CURLE_SSL_CONNECT_ERROR:
			case CURLE_PEER_FAILED_VERIFICATION:
			case CURLE_SSL_CERTPROBLEM:
			case CURLE_SSL_CIPHER:
			case CURLE_SSL_CACERT_BADFILE:
			case CURLE_SSL_ISSUER_ERROR:
			case CURLE_USE_SSL_FAILED:
				return HF_TLS_FAILED;
			case CURLE_TOO_MANY_REDIRECTS:
				return HF_REDIRECT_REFUSED;
			case CURLE_UNSUPPORTED_PROTOCOL:
			case CURLE_URL_MALFORMAT:
				return HF_BAD_URL;
			case CURLE_FILESIZE_EXCEEDED:
				return HF_TOO_LARGE;
			case CURLE_ABORTED_BY_CALLBACK:
				return HF_CANCELLED;
			default:
				return HF_TRANSPORT;
			}
		}
	}
	//---------------------------------------------------------
	class CurlHttpBackend;
	//---------------------------------------------------------
	//! @brief the libcurl-backed transport (@see the file comment).
	class CurlHttpBackend : public HttpBackend
	{
		//--- Types -------------------------------------------
	private:
		//! one in-flight transfer, owned by the worker thread once started
		struct Transfer
		{
			HttpRequestId		id = 0;
			CurlHttpBackend *	owner = NULL;	//!< for the static curl hooks
			CURL *				easy = NULL;
			curl_slist *		headers = NULL;	//!< the request header list curl holds
			HttpClientRequest	request;		//!< kept alive for its body bytes
			HttpUrlParts		url;
			String				headerBlock;	//!< the response head as it arrives
			String				body;			//!< in-memory mode
			FileWriter			file;			//!< save-to-file mode
			unsigned long long	received = 0;
			unsigned long long	expected = 0;
			std::atomic<bool>	cancelled;		//!< set by cancel(), read in the progress hook
			bool				capExceeded = false;
			bool				writeFailed = false;
			String				writeError;
			Transfer() : cancelled(false) {}
		};
		//--- Variables ---------------------------------------
	private:
		CURLM *							mMulti;
		std::thread						mWorker;
		std::atomic<bool>				mRunning;
		std::mutex						mIntentMutex;	//!< guards the two intent queues
		std::vector<Transfer *>			mSubmitted;		//!< handed to the worker
		std::vector<HttpRequestId>		mCancelIds;		//!< cancel intents
		std::map<HttpRequestId, Transfer *>	mLive;		//!< worker-owned, id -> transfer
		std::map<HttpRequestId, Transfer *>	mCancelLookup;	//!< id -> transfer, guarded by mIntentMutex
		HttpEventQueue					mEvents;
		//--- Methods -----------------------------------------
	public:
		CurlHttpBackend() : mMulti(NULL), mRunning(false) {}
		virtual ~CurlHttpBackend() { this->stop(); }
		//---------------------------------------------------------
		bool start() override
		{
			if (this->mRunning)
			{
				return true;
			}
			if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
			{
				return false;
			}
			this->mMulti = curl_multi_init();
			if (this->mMulti == NULL)
			{
				curl_global_cleanup();
				return false;
			}
			this->mRunning = true;
			this->mWorker = std::thread(&CurlHttpBackend::runWorker, this);
			return true;
		}
		//---------------------------------------------------------
		void stop() override
		{
			if (!this->mRunning)
			{
				return;
			}
			this->mRunning = false;
			curl_multi_wakeup(this->mMulti);
			if (this->mWorker.joinable())
			{
				this->mWorker.join();
			}
			// anything the worker never picked up (taken OUT of the queue
			// before destroying: destroy() takes the same lock)
			std::vector<Transfer *> orphans;
			{
				std::lock_guard<std::mutex> lock(this->mIntentMutex);
				orphans.swap(this->mSubmitted);
				this->mCancelIds.clear();
			}
			for (std::size_t at = 0; at < orphans.size(); ++at)
			{
				this->destroy(orphans[at]);
			}
			curl_multi_cleanup(this->mMulti);
			this->mMulti = NULL;
			curl_global_cleanup();
			this->mEvents.clear();
		}
		//---------------------------------------------------------
		void submit(HttpRequestId requestId, HttpClientRequest const & request,
			HttpUrlParts const & url) override
		{
			Transfer * transfer = new Transfer();
			transfer->id = requestId;
			transfer->owner = this;
			transfer->request = request;
			transfer->url = url;
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
			if (!this->configure(transfer))
			{
				this->destroy(transfer);
				this->fail(requestId, HF_TRANSPORT,
					"the transfer could not be set up");
				return;
			}
			{
				std::lock_guard<std::mutex> lock(this->mIntentMutex);
				this->mSubmitted.push_back(transfer);
				this->mCancelLookup[requestId] = transfer;
			}
			curl_multi_wakeup(this->mMulti);
		}
		//---------------------------------------------------------
		void cancel(HttpRequestId requestId) override
		{
			{
				std::lock_guard<std::mutex> lock(this->mIntentMutex);
				std::map<HttpRequestId, Transfer *>::const_iterator found =
					this->mCancelLookup.find(requestId);
				if (found == this->mCancelLookup.end())
				{
					return;
				}
				// the flag makes the progress hook abort PROMPTLY, mid-chunk;
				// the queued intent is what retires the handle
				found->second->cancelled = true;
				this->mCancelIds.push_back(requestId);
			}
			curl_multi_wakeup(this->mMulti);
		}
		//---------------------------------------------------------
		void poll(std::vector<HttpBackendEvent> & out) override
		{
			this->mEvents.drain(out);
		}
		//---------------------------------------------------------
		char const * name() const override { return "curl"; }
	private:
		//---------------------------------------------------------
		//! build the easy handle: THE place every security default is set
		bool configure(Transfer * transfer)
		{
			CURL * easy = curl_easy_init();
			if (easy == NULL)
			{
				return false;
			}
			transfer->easy = easy;
			HttpClientRequest const & request = transfer->request;
			curl_easy_setopt(easy, CURLOPT_URL, transfer->url.rebuild().c_str());
			curl_easy_setopt(easy, CURLOPT_PRIVATE, transfer);
			// --- security defaults ---------------------------------------
			// verify the peer AND that the certificate matches the host: the
			// two options that turn TLS from obfuscation into authentication
			curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
			curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
			// NOTE the explicit long: curl reads its numeric options with
			// va_arg(long), and an enum constant promotes to int - on a
			// 64-bit non-Windows target that leaves the upper half of the
			// value undefined, i.e. a silently wrong TLS floor
			curl_easy_setopt(easy, CURLOPT_SSLVERSION,
				static_cast<long>(CURL_SSLVERSION_TLSv1_2));
#ifdef __ANDROID__
			// Android keeps the system trust anchors as a hashed PEM directory
			// (there is no single bundle file to point CURLOPT_CAINFO at)
			curl_easy_setopt(easy, CURLOPT_CAPATH,
				"/system/etc/security/cacerts");
#endif
			// only http and https exist for this client - no ftp, no file://,
			// no gopher, whatever the URL or a redirect says
#if LIBCURL_VERSION_NUM >= 0x075500
			curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
			// a redirect may never DOWNGRADE out of https (the same rule
			// HttpPolicy::resolveRedirect states for the other backends)
			curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR,
				transfer->url.secure ? "https" : "http,https");
#else
			curl_easy_setopt(easy, CURLOPT_PROTOCOLS,
				static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
			curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS,
				transfer->url.secure
					? static_cast<long>(CURLPROTO_HTTPS)
					: static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
			curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION,
				request.followRedirects ? 1L : 0L);
			curl_easy_setopt(easy, CURLOPT_MAXREDIRS,
				static_cast<long>(request.maxRedirects));
			// no ambient identity: no cookie jar, no netrc, no proxy auth
			// guessing - a request carries exactly what the caller put in it
			curl_easy_setopt(easy, CURLOPT_COOKIEFILE,
				static_cast<char const *>(NULL));
			curl_easy_setopt(easy, CURLOPT_NETRC,
				static_cast<long>(CURL_NETRC_IGNORED));
			// --- bounds ---------------------------------------------------
			curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS,
				static_cast<long>(request.timeoutMs));
			curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS,
				static_cast<long>(request.timeoutMs < 10000
					? request.timeoutMs : 10000));
			curl_easy_setopt(easy, CURLOPT_MAXFILESIZE_LARGE,
				static_cast<curl_off_t>(request.maxResponseBytes));
			// in a threaded process curl must not use signals for timeouts
			curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
			// --- method + body --------------------------------------------
			if (request.method == "HEAD")
			{
				curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
			}
			else if (request.method != "GET")
			{
				curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST,
					request.method.c_str());
			}
			if (!request.body.empty())
			{
				curl_easy_setopt(easy, CURLOPT_POSTFIELDS,
					transfer->request.body.data());
				curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE,
					static_cast<curl_off_t>(transfer->request.body.size()));
			}
			else if (HttpPolicy::methodTakesBody(request.method))
			{
				// an empty POST is still a POST with a zero-length body
				curl_easy_setopt(easy, CURLOPT_POSTFIELDS, "");
				curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE,
					static_cast<curl_off_t>(0));
			}
			for (std::size_t at = 0; at < request.headers.size(); ++at)
			{
				const String line = request.headers[at].first + ": " +
					request.headers[at].second;
				transfer->headers =
					curl_slist_append(transfer->headers, line.c_str());
			}
			// curl would otherwise add its own Expect: 100-continue for larger
			// POSTs, which costs a round trip against servers that ignore it
			transfer->headers = curl_slist_append(transfer->headers, "Expect:");
			curl_easy_setopt(easy, CURLOPT_HTTPHEADER, transfer->headers);
			// --- the data hooks -------------------------------------------
			curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &writeBody);
			curl_easy_setopt(easy, CURLOPT_WRITEDATA, transfer);
			curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &writeHeader);
			curl_easy_setopt(easy, CURLOPT_HEADERDATA, transfer);
			curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, &reportProgress);
			curl_easy_setopt(easy, CURLOPT_XFERINFODATA, transfer);
			curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
			return true;
		}
		//---------------------------------------------------------
		//! the worker: owns the multi handle and every live easy handle
		void runWorker()
		{
			while (this->mRunning)
			{
				this->takeIntents();
				int running = 0;
				curl_multi_perform(this->mMulti, &running);
				this->reapFinished();
				if (!this->mRunning)
				{
					break;
				}
				int ready = 0;
				// blocks until something happens, a wakeup arrives or 50 ms
				// pass - no busy spin, and submit/cancel are answered at once
				curl_multi_poll(this->mMulti, NULL, 0, 50, &ready);
			}
			// teardown: every live transfer is retired without a callback (the
			// client is going away too)
			for (std::map<HttpRequestId, Transfer *>::iterator at =
				this->mLive.begin(); at != this->mLive.end(); ++at)
			{
				curl_multi_remove_handle(this->mMulti, at->second->easy);
				this->destroy(at->second);
			}
			this->mLive.clear();
		}
		//---------------------------------------------------------
		//! pick up what the main thread queued
		void takeIntents()
		{
			std::vector<Transfer *> submitted;
			std::vector<HttpRequestId> cancelled;
			{
				std::lock_guard<std::mutex> lock(this->mIntentMutex);
				submitted.swap(this->mSubmitted);
				cancelled.swap(this->mCancelIds);
			}
			for (std::size_t at = 0; at < submitted.size(); ++at)
			{
				Transfer * transfer = submitted[at];
				this->mLive[transfer->id] = transfer;
				curl_multi_add_handle(this->mMulti, transfer->easy);
			}
			for (std::size_t at = 0; at < cancelled.size(); ++at)
			{
				std::map<HttpRequestId, Transfer *>::iterator found =
					this->mLive.find(cancelled[at]);
				if (found == this->mLive.end())
				{
					continue;
				}
				Transfer * transfer = found->second;
				curl_multi_remove_handle(this->mMulti, transfer->easy);
				this->mLive.erase(found);
				this->finish(transfer, HF_CANCELLED,
					"the request was cancelled");
			}
		}
		//---------------------------------------------------------
		//! drain curl's completion messages
		void reapFinished()
		{
			CURLMsg * message = NULL;
			int queued = 0;
			while ((message = curl_multi_info_read(this->mMulti, &queued)) != NULL)
			{
				if (message->msg != CURLMSG_DONE)
				{
					continue;
				}
				CURL * easy = message->easy_handle;
				// CURLINFO_PRIVATE hands the pointer back as char*
				char * privateData = NULL;
				curl_easy_getinfo(easy, CURLINFO_PRIVATE, &privateData);
				Transfer * transfer = reinterpret_cast<Transfer *>(privateData);
				const CURLcode result = message->data.result;
				curl_multi_remove_handle(this->mMulti, easy);
				if (transfer == NULL)
				{
					curl_easy_cleanup(easy);
					continue;
				}
				this->mLive.erase(transfer->id);
				if (transfer->capExceeded)
				{
					this->finish(transfer, HF_TOO_LARGE,
						"the response exceeded the " +
						std::to_string(transfer->request.maxResponseBytes) +
						"-byte cap");
					continue;
				}
				if (transfer->writeFailed)
				{
					this->finish(transfer, HF_WRITE_FAILED,
						transfer->writeError);
					continue;
				}
				if (result != CURLE_OK)
				{
					const HttpFailure failure = transfer->cancelled
						? HF_CANCELLED : failureFromCurl(result);
					this->finish(transfer, failure, transfer->cancelled
						? String("the request was cancelled")
						: String(curl_easy_strerror(result)));
					continue;
				}
				this->finish(transfer, HF_NONE, String());
			}
		}
		//---------------------------------------------------------
		//! publish the ONE completion for a transfer and destroy it
		void finish(Transfer * transfer, HttpFailure failure,
			String const & reason)
		{
			HttpClientResponse response;
			response.id = transfer->id;
			response.bytes = transfer->received;
			response.finalUrl = transfer->url.rebuild();
			if (transfer->easy != NULL)
			{
				long status = 0;
				curl_easy_getinfo(transfer->easy, CURLINFO_RESPONSE_CODE,
					&status);
				response.status = static_cast<int>(status);
				char * effective = NULL;
				curl_easy_getinfo(transfer->easy, CURLINFO_EFFECTIVE_URL,
					&effective);
				if (effective != NULL)
				{
					response.finalUrl = effective;
				}
			}
			HttpPolicy::parseHeaderBlock(transfer->headerBlock,
				response.headers);
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
						this->destroy(transfer);
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
			this->destroy(transfer);
		}
		//---------------------------------------------------------
		//! release everything one transfer owns
		void destroy(Transfer * transfer)
		{
			{
				std::lock_guard<std::mutex> lock(this->mIntentMutex);
				this->mCancelLookup.erase(transfer->id);
			}
			if (transfer->easy != NULL)
			{
				curl_easy_cleanup(transfer->easy);
				transfer->easy = NULL;
			}
			if (transfer->headers != NULL)
			{
				curl_slist_free_all(transfer->headers);
				transfer->headers = NULL;
			}
			delete transfer;
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
		//--- the curl callbacks (worker thread) -------------------
		//---------------------------------------------------------
		//! body bytes: cap them, then store or stream them
		static size_t writeBody(char * bytes, size_t size, size_t count,
			void * userData)
		{
			Transfer * transfer = static_cast<Transfer *>(userData);
			const unsigned long long length =
				static_cast<unsigned long long>(size) *
				static_cast<unsigned long long>(count);
			if (transfer->received + length >
				transfer->request.maxResponseBytes)
			{
				// a short write is curl's "abort this transfer" signal
				transfer->capExceeded = true;
				return 0;
			}
			if (transfer->request.savePath.empty())
			{
				transfer->body.append(bytes, static_cast<size_t>(length));
			}
			else if (!transfer->file.write(bytes, length, transfer->writeError))
			{
				transfer->writeFailed = true;
				return 0;
			}
			transfer->received += length;
			return static_cast<size_t>(length);
		}
		//---------------------------------------------------------
		//! response header lines, kept raw and parsed once at completion
		static size_t writeHeader(char * bytes, size_t size, size_t count,
			void * userData)
		{
			Transfer * transfer = static_cast<Transfer *>(userData);
			const size_t length = size * count;
			// a new status line means a new header section (a followed
			// redirect): only the FINAL response's headers are the answer
			if (length >= 5 && std::memcmp(bytes, "HTTP/", 5) == 0)
			{
				transfer->headerBlock.clear();
			}
			transfer->headerBlock.append(bytes, length);
			return length;
		}
		//---------------------------------------------------------
		//! progress + the prompt cancellation check
		static int reportProgress(void * userData, curl_off_t downloadTotal,
			curl_off_t downloadNow, curl_off_t /*uploadTotal*/,
			curl_off_t /*uploadNow*/)
		{
			Transfer * transfer = static_cast<Transfer *>(userData);
			if (transfer->cancelled)
			{
				return 1;	// aborts the transfer (CURLE_ABORTED_BY_CALLBACK)
			}
			transfer->expected = downloadTotal > 0
				? static_cast<unsigned long long>(downloadTotal) : 0;
			// the ANNOUNCED size is capped too: an oversized download is
			// refused before its body arrives
			if (downloadTotal > 0 &&
				static_cast<unsigned long long>(downloadTotal) >
					transfer->request.maxResponseBytes)
			{
				transfer->capExceeded = true;
				return 1;
			}
			transfer->owner->mEvents.pushProgress(transfer->id,
				static_cast<unsigned long long>(downloadNow),
				transfer->expected);
			return 0;
		}
	};
	//---------------------------------------------------------
	HttpBackend * createHttpBackend()
	{
		return new CurlHttpBackend();
	}
}

#endif // ORKIGE_HTTP_CURL
