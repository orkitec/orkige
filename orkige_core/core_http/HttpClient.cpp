/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpClient.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_http/HttpClient.h"

#include "core_debug/Breadcrumbs.h"
#include "core_debug/DebugMacros.h"
#include "core_http/HttpBackend.h"
#include "core_http/HttpPolicy.h"

#include <utility>

namespace Orkige
{
	IMPL_OSINGLETON(HttpClient);
	//---------------------------------------------------------
	const unsigned long long HttpClient::DEFAULT_MAX_RESPONSE_BYTES =
		16ull * 1024ull * 1024ull;
	//---------------------------------------------------------
	//--- HttpEventQueue --------------------------------------
	//---------------------------------------------------------
	void HttpEventQueue::pushProgress(HttpRequestId id,
		unsigned long long received, unsigned long long total)
	{
		std::lock_guard<std::mutex> lock(this->mMutex);
		// COALESCE onto an undrained progress step for the same request: only
		// the newest number matters, and a fast transfer must not be able to
		// queue thousands of callbacks for one frame
		for (std::size_t at = this->mQueue.size(); at > 0; --at)
		{
			HttpBackendEvent & existing = this->mQueue[at - 1];
			if (existing.id == id && !existing.completion)
			{
				existing.received = received;
				existing.total = total;
				return;
			}
			if (existing.id == id && existing.completion)
			{
				// a completion is already queued: a later progress step is
				// noise the caller must never see AFTER its answer
				return;
			}
		}
		HttpBackendEvent event;
		event.id = id;
		event.completion = false;
		event.received = received;
		event.total = total;
		this->mQueue.push_back(event);
	}
	//---------------------------------------------------------
	void HttpEventQueue::pushCompletion(HttpClientResponse const & response)
	{
		std::lock_guard<std::mutex> lock(this->mMutex);
		HttpBackendEvent event;
		event.id = response.id;
		event.completion = true;
		event.received = response.bytes;
		event.total = response.bytes;
		event.response = response;
		this->mQueue.push_back(event);
	}
	//---------------------------------------------------------
	void HttpEventQueue::drain(std::vector<HttpBackendEvent> & out)
	{
		out.clear();
		std::lock_guard<std::mutex> lock(this->mMutex);
		out.swap(this->mQueue);
	}
	//---------------------------------------------------------
	void HttpEventQueue::clear()
	{
		std::lock_guard<std::mutex> lock(this->mMutex);
		this->mQueue.clear();
	}
	//---------------------------------------------------------
	//--- HttpClient: public ----------------------------------
	//---------------------------------------------------------
	HttpClient::HttpClient()
	{
		this->mBackendStarted = false;
		this->mBackendFailed = false;
		this->mNextId = 1;
		this->mCompletedCount = 0;
		this->mUserAgent = "orkige";
	}
	//---------------------------------------------------------
	HttpClient::~HttpClient()
	{
		// teardown: every in-flight transfer dies WITHOUT a callback - the
		// owners are going away with us and must not be called into
		this->cancelAll();
		if (this->mBackend)
		{
			this->mBackend->stop();
			this->mBackend.reset();
		}
	}
	//---------------------------------------------------------
	bool HttpClient::compiled()
	{
#ifdef ORKIGE_HTTP
		return true;
#else
		return false;
#endif
	}
	//---------------------------------------------------------
	bool HttpClient::available() const
	{
		return HttpClient::compiled() && !this->mBackendFailed;
	}
	//---------------------------------------------------------
	char const * HttpClient::backendName() const
	{
		return this->mBackend ? this->mBackend->name() : "none";
	}
	//---------------------------------------------------------
	HttpRequestId HttpClient::submit(HttpClientRequest const & request,
		HttpCompleteCallback const & onComplete,
		HttpProgressCallback const & onProgress, void const * owner)
	{
		if (!onComplete)
		{
			// a request whose answer nobody reads is a caller mistake, not a
			// silent fire-and-forget: say so and do nothing
			oDebugError("http", 0, "submit('" << request.url << "'): no "
				"completion callback - the answer would go nowhere");
			return 0;
		}
		const HttpRequestId id = this->mNextId++;
		Pending pending;
		pending.id = id;
		pending.onComplete = onComplete;
		pending.onProgress = onProgress;
		pending.owner = owner;
		pending.url = request.url;
		this->mPending[id] = pending;

		// (1) the PURE gate: scheme/security policy, method and header hygiene.
		// A refusal is queued as this request's ONE completion, so the caller
		// handles it exactly where it handles a server error.
		HttpUrlParts url;
		String reason;
		const HttpFailure verdict = HttpPolicy::validate(request, url, reason);
		if (verdict != HF_NONE)
		{
			this->refuse(id, verdict, reason);
			return id;
		}
		// (2) the transport, brought up on first use
		if (!this->ensureBackend())
		{
			this->refuse(id, HF_UNAVAILABLE, HttpClient::compiled()
				? String("the HTTP transport could not be started")
				: String("this build carries no HTTP client "
					"(built with ORKIGE_HTTP=OFF)"));
			return id;
		}
		// (3) fill in the defaults the caller left open, then hand it over
		HttpClientRequest prepared = request;
		if (prepared.timeoutMs == 0)
		{
			prepared.timeoutMs = 30000;
		}
		if (prepared.maxResponseBytes == 0)
		{
			prepared.maxResponseBytes = HttpClient::DEFAULT_MAX_RESPONSE_BYTES;
		}
		bool hasUserAgent = false;
		bool hasContentType = false;
		for (std::size_t at = 0; at < prepared.headers.size(); ++at)
		{
			const String name =
				HttpPolicy::toLowerAscii(prepared.headers[at].first);
			hasUserAgent = hasUserAgent || name == "user-agent";
			hasContentType = hasContentType || name == "content-type";
		}
		if (!hasUserAgent && !this->mUserAgent.empty())
		{
			prepared.headers.push_back(
				std::make_pair(String("User-Agent"), this->mUserAgent));
		}
		if (!hasContentType && !prepared.contentType.empty() &&
			!prepared.body.empty())
		{
			prepared.headers.push_back(
				std::make_pair(String("Content-Type"), prepared.contentType));
		}
		this->mBackend->submit(id, prepared, url);
		return id;
	}
	//---------------------------------------------------------
	bool HttpClient::cancel(HttpRequestId id)
	{
		std::map<HttpRequestId, Pending>::iterator found = this->mPending.find(id);
		if (found == this->mPending.end() || found->second.cancelled)
		{
			return false;
		}
		found->second.cancelled = true;
		if (this->mBackend)
		{
			this->mBackend->cancel(id);
		}
		// the caller still gets its ONE completion (with HF_CANCELLED) at the
		// next update() - one place to clean up a progress bar, always
		this->mCancelled.push_back(id);
		return true;
	}
	//---------------------------------------------------------
	int HttpClient::cancelOwner(void const * owner)
	{
		int retired = 0;
		std::map<HttpRequestId, Pending>::iterator at = this->mPending.begin();
		while (at != this->mPending.end())
		{
			if (at->second.owner != owner)
			{
				++at;
				continue;
			}
			if (this->mBackend)
			{
				this->mBackend->cancel(at->first);
			}
			// SILENT by design: the owner (a script sandbox being destroyed, a
			// closing screen) must never be called into after it went away
			this->mPending.erase(at++);
			++retired;
		}
		return retired;
	}
	//---------------------------------------------------------
	void HttpClient::cancelAll()
	{
		if (this->mBackend)
		{
			for (std::map<HttpRequestId, Pending>::const_iterator at =
				this->mPending.begin(); at != this->mPending.end(); ++at)
			{
				this->mBackend->cancel(at->first);
			}
		}
		this->mPending.clear();
		this->mImmediate.clear();
		this->mCancelled.clear();
	}
	//---------------------------------------------------------
	void HttpClient::update()
	{
		// (1) refusals decided during submit() - delivered here, never inline,
		// so a caller's completion path is the same for every outcome
		if (!this->mImmediate.empty())
		{
			std::vector<HttpClientResponse> refusals;
			refusals.swap(this->mImmediate);
			for (std::size_t at = 0; at < refusals.size(); ++at)
			{
				this->deliver(refusals[at]);
			}
		}
		// (2) cancellations
		if (!this->mCancelled.empty())
		{
			std::vector<HttpRequestId> cancelled;
			cancelled.swap(this->mCancelled);
			for (std::size_t at = 0; at < cancelled.size(); ++at)
			{
				HttpClientResponse response;
				response.id = cancelled[at];
				response.failure = HF_CANCELLED;
				response.reason = "the request was cancelled";
				this->deliver(response);
			}
		}
		// (3) the transport's own progress/completion events
		if (!this->mBackend || !this->mBackendStarted)
		{
			return;
		}
		this->mBackend->poll(this->mDrainScratch);
		for (std::size_t at = 0; at < this->mDrainScratch.size(); ++at)
		{
			HttpBackendEvent const & event = this->mDrainScratch[at];
			std::map<HttpRequestId, Pending>::iterator found =
				this->mPending.find(event.id);
			if (found == this->mPending.end() || found->second.cancelled)
			{
				// unknown or already answered (a cancel that raced the
				// transport): the caller has its one completion already
				continue;
			}
			if (!event.completion)
			{
				if (found->second.onProgress)
				{
					found->second.onProgress(event.received, event.total);
				}
				continue;
			}
			this->deliver(event.response);
		}
		this->mDrainScratch.clear();
	}
	//---------------------------------------------------------
	//--- HttpClient: private ---------------------------------
	//---------------------------------------------------------
	bool HttpClient::ensureBackend()
	{
		if (this->mBackendStarted)
		{
			return true;
		}
		if (this->mBackendFailed)
		{
			return false;
		}
		if (!this->mBackend)
		{
			this->mBackend.reset(createHttpBackend());
		}
		if (!this->mBackend || !this->mBackend->start())
		{
			this->mBackendFailed = true;
			oDebugWarn("http", 0, "no HTTP transport available ("
				<< (this->mBackend ? this->mBackend->name() : "none")
				<< ") - requests will be refused");
			return false;
		}
		this->mBackendStarted = true;
		oDebugMsg("http", 0, "HTTP transport '" << this->mBackend->name()
			<< "' ready");
		return true;
	}
	//---------------------------------------------------------
	void HttpClient::refuse(HttpRequestId id, HttpFailure failure,
		String const & reason)
	{
		HttpClientResponse response;
		response.id = id;
		response.failure = failure;
		response.reason = reason;
		this->mImmediate.push_back(response);
	}
	//---------------------------------------------------------
	void HttpClient::deliver(HttpClientResponse const & response)
	{
		std::map<HttpRequestId, Pending>::iterator found =
			this->mPending.find(response.id);
		if (found == this->mPending.end())
		{
			return;
		}
		// take the callback OUT before invoking it: the handler may submit the
		// next request (or cancel others), and mPending must not be holding a
		// reference into the entry we are about to erase
		const HttpCompleteCallback onComplete = found->second.onComplete;
		const String url = found->second.url;
		this->mPending.erase(found);
		++this->mCompletedCount;
		if (response.failure == HF_CANCELLED)
		{
			// a cancellation is what the caller ASKED for, not an anomaly
			oDebugMsg("http", 0, "request to '" << url << "' was cancelled");
		}
		else if (response.failure != HF_NONE)
		{
			// an honest, single log line per failed request; the reason is
			// already human-readable. Never the body, never the headers - a
			// request may carry a token.
			oDebugWarn("http", 0, "request to '" << url << "' failed ("
				<< httpFailureName(response.failure) << "): "
				<< response.reason);
			if (Breadcrumbs::getSingletonPtr() != NULL)
			{
				Breadcrumbs::getSingleton().record("http",
					httpFailureName(response.failure) + " " + url);
			}
		}
		if (onComplete)
		{
			onComplete(response);
		}
	}
}
