/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpBackendFetch.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The browser transport behind HttpClient, and the ONE translation unit that
// includes Emscripten's fetch API. A wasm module has no sockets, so a bundled
// HTTP library cannot work here at all: the page's own fetch stack is the only
// road out, and it brings the browser's certificate verification, its proxy and
// its cache with it - nothing is added to the module.
//
// TWO honest differences from the native backends, both documented in
// Docs/http.md: the browser owns redirect handling (it refuses a mixed-content
// https -> http hop itself, which is the same rule the engine's policy states),
// and save-to-file writes the COMPLETE body once it has arrived rather than
// streaming it - the fetch API hands a wasm module no incremental file sink. The
// response-size cap still applies, and is checked against the announced size
// before the body is accepted.
//
// SINGLE-THREADED BY CONSTRUCTION: the callbacks below run on the browser's
// event loop between our frames, so there is no worker thread and no lock
// contention - the HttpEventQueue is still the handover, so HttpClient::update()
// delivers a completion at exactly the same frame boundary as everywhere else.

#include "core_http/HttpBackend.h"

#ifdef ORKIGE_HTTP_FETCH

#include "core_filesystem/FileWriter.h"
#include "core_http/HttpPolicy.h"

#include <emscripten/fetch.h>

#include <map>
#include <string>
#include <vector>

namespace Orkige
{
	class FetchHttpBackend;
	//---------------------------------------------------------
	//! @brief the Emscripten-fetch-backed transport.
	class FetchHttpBackend : public HttpBackend
	{
		//--- Types -------------------------------------------
	public:
		//! one in-flight fetch and everything it must keep alive
		struct Transfer
		{
			HttpRequestId			id = 0;
			FetchHttpBackend *		owner = NULL;
			emscripten_fetch_t *	fetch = NULL;
			HttpClientRequest		request;		//!< holds the body bytes
			HttpUrlParts			url;
			String					method;			//!< the copy attr points at
			std::vector<String>		headerStorage;	//!< name/value strings
			std::vector<char const *>	headerPointers;	//!< the NULL-terminated array
			bool					retired = false;	//!< a completion was published
		};
		//--- Variables ---------------------------------------
	private:
		std::map<HttpRequestId, Transfer *>	mTransfers;
		HttpEventQueue						mEvents;
		//--- Methods -----------------------------------------
	public:
		FetchHttpBackend() {}
		virtual ~FetchHttpBackend() { this->stop(); }
		//---------------------------------------------------------
		bool start() override { return true; }
		//---------------------------------------------------------
		void stop() override
		{
			while (!this->mTransfers.empty())
			{
				Transfer * transfer = this->mTransfers.begin()->second;
				this->mTransfers.erase(this->mTransfers.begin());
				emscripten_fetch_t * fetch = transfer->fetch;
				transfer->fetch = NULL;
				delete transfer;
				// closing an EXECUTING fetch calls onerror synchronously, from
				// inside the close - so the handle must not still point at the
				// transfer we just dropped
				closeFetch(fetch);
			}
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
			transfer->method = request.method;
			for (std::size_t at = 0; at < request.headers.size(); ++at)
			{
				transfer->headerStorage.push_back(request.headers[at].first);
				transfer->headerStorage.push_back(request.headers[at].second);
			}
			for (std::size_t at = 0; at < transfer->headerStorage.size(); ++at)
			{
				transfer->headerPointers.push_back(
					transfer->headerStorage[at].c_str());
			}
			transfer->headerPointers.push_back(NULL);

			emscripten_fetch_attr_t attributes;
			emscripten_fetch_attr_init(&attributes);
			const std::size_t methodRoom = sizeof(attributes.requestMethod) - 1;
			const std::size_t methodLength =
				transfer->method.size() < methodRoom
					? transfer->method.size() : methodRoom;
			for (std::size_t at = 0; at < methodLength; ++at)
			{
				attributes.requestMethod[at] = transfer->method[at];
			}
			attributes.requestMethod[methodLength] = '\0';
			// LOAD_TO_MEMORY: the body arrives whole. REPLACE keeps the fetch
			// out of IndexedDB entirely - a game request is not a cached asset.
			attributes.attributes =
				EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_REPLACE;
			attributes.timeoutMSecs = request.timeoutMs;
			// no ambient credentials: a cross-origin request carries only what
			// the caller put in its headers
			attributes.withCredentials = false;
			attributes.requestHeaders = transfer->headerPointers.data();
			if (!transfer->request.body.empty())
			{
				attributes.requestData = transfer->request.body.data();
				attributes.requestDataSize = transfer->request.body.size();
			}
			attributes.userData = transfer;
			attributes.onsuccess = &FetchHttpBackend::onSuccess;
			attributes.onerror = &FetchHttpBackend::onError;
			attributes.onprogress = &FetchHttpBackend::onProgress;

			this->mTransfers[requestId] = transfer;
			transfer->fetch = emscripten_fetch(&attributes,
				transfer->url.rebuild().c_str());
			if (transfer->fetch == NULL)
			{
				this->mTransfers.erase(requestId);
				delete transfer;
				this->fail(requestId, HF_TRANSPORT,
					"the browser refused to start the request");
			}
		}
		//---------------------------------------------------------
		void cancel(HttpRequestId requestId) override
		{
			std::map<HttpRequestId, Transfer *>::iterator found =
				this->mTransfers.find(requestId);
			if (found == this->mTransfers.end())
			{
				return;
			}
			Transfer * transfer = found->second;
			this->mTransfers.erase(found);
			emscripten_fetch_t * fetch = transfer->fetch;
			transfer->fetch = NULL;
			HttpClientResponse response;
			response.id = requestId;
			response.failure = HF_CANCELLED;
			response.reason = "the request was cancelled";
			this->mEvents.pushCompletion(response);
			delete transfer;
			// the close calls onerror synchronously for a still-executing
			// fetch, so the handle must no longer name the dropped transfer
			closeFetch(fetch);
		}
		//---------------------------------------------------------
		void poll(std::vector<HttpBackendEvent> & out) override
		{
			this->mEvents.drain(out);
		}
		//---------------------------------------------------------
		char const * name() const override { return "fetch"; }
	private:
		//---------------------------------------------------------
		//! @brief close a fetch handle SAFELY: clearing userData first is what
		//! keeps the synchronous onerror the close may fire from reaching a
		//! transfer that has already been retired
		static void closeFetch(emscripten_fetch_t * fetch)
		{
			if (fetch == NULL)
			{
				return;
			}
			fetch->userData = NULL;
			emscripten_fetch_close(fetch);
		}
		//---------------------------------------------------------
		//! the response headers the browser reported, lower-cased
		static void readHeaders(emscripten_fetch_t * fetch,
			std::map<String, String> & out)
		{
			const size_t length =
				emscripten_fetch_get_response_headers_length(fetch);
			if (length == 0)
			{
				return;
			}
			String block;
			block.resize(length + 1, '\0');
			emscripten_fetch_get_response_headers(fetch, &block[0],
				block.size());
			HttpPolicy::parseHeaderBlock(block, out);
		}
		//---------------------------------------------------------
		//! publish the ONE completion and retire the transfer
		void finish(Transfer * transfer, emscripten_fetch_t * fetch,
			HttpFailure failure, String const & reason)
		{
			if (transfer->retired)
			{
				return;
			}
			transfer->retired = true;
			this->mTransfers.erase(transfer->id);
			HttpClientResponse response;
			response.id = transfer->id;
			response.finalUrl = transfer->url.rebuild();
			if (fetch != NULL)
			{
				response.status = static_cast<int>(fetch->status);
				response.bytes = fetch->numBytes;
				readHeaders(fetch, response.headers);
			}
			if (failure == HF_NONE)
			{
				const unsigned long long size = fetch != NULL
					? static_cast<unsigned long long>(fetch->numBytes) : 0;
				if (size > transfer->request.maxResponseBytes)
				{
					response.failure = HF_TOO_LARGE;
					response.reason = "the response is " +
						std::to_string(size) + " bytes, over the " +
						std::to_string(transfer->request.maxResponseBytes) +
						"-byte cap";
				}
				else if (!transfer->request.savePath.empty())
				{
					// web: the whole body is written at once - the fetch API
					// gives a wasm module no incremental file sink
					String error;
					String bytes;
					if (fetch != NULL && fetch->data != NULL)
					{
						bytes.assign(fetch->data, static_cast<size_t>(size));
					}
					if (!FileWriter::writeWholeFile(
						transfer->request.savePath, bytes, error))
					{
						response.failure = HF_WRITE_FAILED;
						response.reason = error;
					}
					else
					{
						response.savedPath = transfer->request.savePath;
						response.completed = true;
					}
				}
				else
				{
					if (fetch != NULL && fetch->data != NULL)
					{
						response.body.assign(fetch->data,
							static_cast<size_t>(size));
					}
					response.completed = true;
				}
			}
			else
			{
				response.failure = failure;
				response.reason = reason;
			}
			this->mEvents.pushCompletion(response);
			delete transfer;
		}
		//---------------------------------------------------------
		//! refuse a request that never started
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
		//--- the fetch callbacks (the page's event loop) ----------
		//---------------------------------------------------------
		static void onSuccess(emscripten_fetch_t * fetch)
		{
			Transfer * transfer = static_cast<Transfer *>(fetch->userData);
			if (transfer == NULL || transfer->retired)
			{
				closeFetch(fetch);
				return;
			}
			FetchHttpBackend * backend = transfer->owner;
			transfer->fetch = NULL;
			backend->finish(transfer, fetch, HF_NONE, String());
			closeFetch(fetch);
		}
		//---------------------------------------------------------
		static void onError(emscripten_fetch_t * fetch)
		{
			Transfer * transfer = static_cast<Transfer *>(fetch->userData);
			if (transfer == NULL || transfer->retired)
			{
				return;	// a cancel already answered the caller
			}
			FetchHttpBackend * backend = transfer->owner;
			transfer->fetch = NULL;
			// the browser deliberately does not say WHY a cross-origin or
			// network request failed (it would be a probing oracle); status 0
			// with no answer is the connect/CORS case, a timeout says so
			const bool timedOut = fetch->status == 0 &&
				fetch->readyState == 0;
			backend->finish(transfer, fetch,
				timedOut ? HF_TIMEOUT : HF_CONNECT_FAILED,
				timedOut
					? String("the request timed out")
					: String("the browser could not complete the request "
						"(network, CORS or a blocked mixed-content hop) - "
						"status ") + std::to_string(fetch->status));
			closeFetch(fetch);
		}
		//---------------------------------------------------------
		static void onProgress(emscripten_fetch_t * fetch)
		{
			Transfer * transfer = static_cast<Transfer *>(fetch->userData);
			if (transfer == NULL || transfer->retired)
			{
				return;
			}
			const unsigned long long total = fetch->totalBytes;
			const unsigned long long received =
				fetch->dataOffset + fetch->numBytes;
			if (total > transfer->request.maxResponseBytes)
			{
				// the ANNOUNCED size is over the cap: refuse before the body
				FetchHttpBackend * backend = transfer->owner;
				emscripten_fetch_t * closing = fetch;
				transfer->fetch = NULL;
				backend->finish(transfer, fetch, HF_TOO_LARGE,
					"the response announces " + std::to_string(total) +
					" bytes, over the " +
					std::to_string(transfer->request.maxResponseBytes) +
					"-byte cap");
				closeFetch(closing);
				return;
			}
			transfer->owner->mEvents.pushProgress(transfer->id, received, total);
		}
	};
	//---------------------------------------------------------
	HttpBackend * createHttpBackend()
	{
		return new FetchHttpBackend();
	}
}

#endif // ORKIGE_HTTP_FETCH
