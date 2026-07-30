/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpBackendApple.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The macOS + iOS transport behind HttpClient. The platform's OWN HTTP stack
// (NSURLSession) is the right answer here rather than a bundled library: it
// verifies certificates against the SYSTEM trust store - which a bundled TLS
// library cannot do on Apple platforms without shipping (and slowly rotting) a
// CA bundle of our own - it adds NOTHING to the binary or the dependency
// closure, and it is what the platform expects an app to use. This is the ONE
// translation unit that touches Foundation for networking; HttpClient and every
// caller above it stay plain C++ behind the HttpBackend seam, the same split as
// engine_input/HapticManager + HapticBridgeApple.mm.

#include "core_http/HttpBackend.h"

#ifdef ORKIGE_HTTP_APPLE

#include "core_filesystem/FileWriter.h"
#include "core_http/HttpPolicy.h"

#import <Foundation/Foundation.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#if __has_feature(objc_arc)
#	define ORKIGE_HTTP_RELEASE(object)	((void)0)
#	define ORKIGE_HTTP_RETAIN(object)	(object)
#else
#	define ORKIGE_HTTP_RELEASE(object)	[(object) release]
#	define ORKIGE_HTTP_RETAIN(object)	[(object) retain]
#endif

namespace Orkige
{
	class AppleHttpBackend;
}

//! @brief the session delegate: a thin forwarder into the backend. Streaming
//! (rather than NSURLSession's one-shot completion handler) is what buys
//! progress reporting, the response-size cap and save-to-file without ever
//! holding a download twice in memory.
@interface OrkigeHttpSessionDelegate : NSObject <NSURLSessionDataDelegate>
{
@public
	Orkige::AppleHttpBackend * backend;	//!< non-owning, outlives the session
}
@end

namespace Orkige
{
	namespace
	{
		//! NSError -> the engine's failure taxonomy (the reason keeps the
		//! platform's own wording, which is already user-readable)
		HttpFailure failureFromNSError(NSError * error)
		{
			if (error == nil)
			{
				return HF_NONE;
			}
			if (![error.domain isEqualToString:NSURLErrorDomain])
			{
				return HF_TRANSPORT;
			}
			switch (error.code)
			{
			case NSURLErrorCancelled:
				return HF_CANCELLED;
			case NSURLErrorTimedOut:
				return HF_TIMEOUT;
			case NSURLErrorCannotFindHost:
			case NSURLErrorCannotConnectToHost:
			case NSURLErrorNetworkConnectionLost:
			case NSURLErrorDNSLookupFailed:
			case NSURLErrorNotConnectedToInternet:
			case NSURLErrorInternationalRoamingOff:
			case NSURLErrorCallIsActive:
			case NSURLErrorDataNotAllowed:
				return HF_CONNECT_FAILED;
			case NSURLErrorSecureConnectionFailed:
			case NSURLErrorServerCertificateHasBadDate:
			case NSURLErrorServerCertificateUntrusted:
			case NSURLErrorServerCertificateHasUnknownRoot:
			case NSURLErrorServerCertificateNotYetValid:
			case NSURLErrorClientCertificateRejected:
			case NSURLErrorClientCertificateRequired:
				return HF_TLS_FAILED;
			case NSURLErrorAppTransportSecurityRequiresSecureConnection:
				return HF_INSECURE_SCHEME;
			case NSURLErrorDataLengthExceedsMaximum:
				return HF_TOO_LARGE;
			default:
				return HF_TRANSPORT;
			}
		}
		//! an NSString as an engine String (nil-safe)
		String toEngineString(NSString * text)
		{
			return text != nil ? String([text UTF8String]) : String();
		}
	}
	//---------------------------------------------------------
	//! @brief the NSURLSession-backed transport.
	//! @remarks THE LOCKING RULE: per-transfer state is only ever touched
	//! while mMutex is held, and a Transfer pointer NEVER escapes a locked
	//! scope - so a teardown on the main thread cannot free state a delegate
	//! callback is still using. The delegate methods are pure forwarders into
	//! the on*() handlers below, each of which takes the lock exactly once.
	class AppleHttpBackend : public HttpBackend
	{
		//--- Types -------------------------------------------
	private:
		//! everything one in-flight request needs, owned by the backend
		struct Transfer
		{
			HttpRequestId		id = 0;
			HttpClientResponse	response;		//!< filled as the answer arrives
			unsigned long long	maxBytes = 0;	//!< the caller's cap
			unsigned long long	received = 0;	//!< body bytes so far
			unsigned long long	expected = 0;	//!< Content-Length (0 = unknown)
			String				savePath;		//!< non-empty = save-to-file mode
			FileWriter			file;			//!< the streaming sink (save mode)
			bool				allowInsecure = false;	//!< the caller's opt-in
			bool				followRedirects = true;
			unsigned int		redirects = 0;	//!< followed so far
			unsigned int		maxRedirects = 5;
			double				deadline = 0.0;	//!< absolute whole-request deadline
			HttpUrlParts		url;			//!< the current (possibly redirected) URL
			NSURLSessionTask *	task = nil;		//!< retained while in flight
		};
		//--- Variables ---------------------------------------
	private:
		NSURLSession *					mSession;
		OrkigeHttpSessionDelegate *		mDelegate;
		std::mutex						mMutex;		//!< guards mTransfers
		std::map<HttpRequestId, Transfer *>	mTransfers;
		HttpEventQueue					mEvents;
		//--- Methods -----------------------------------------
	public:
		AppleHttpBackend()
		{
			this->mSession = nil;
			this->mDelegate = nil;
		}
		virtual ~AppleHttpBackend()
		{
			this->stop();
		}
		//---------------------------------------------------------
		bool start() override
		{
			if (this->mSession != nil)
			{
				return true;
			}
			this->mDelegate = [[OrkigeHttpSessionDelegate alloc] init];
			this->mDelegate->backend = this;
			NSURLSessionConfiguration * configuration =
				[NSURLSessionConfiguration ephemeralSessionConfiguration];
			// no shared cache and no cookie jar: a game's API call must not be
			// answered out of a stale disk cache, and no request carries an
			// implicit identity the caller did not put in a header
			configuration.URLCache = nil;
			configuration.requestCachePolicy =
				NSURLRequestReloadIgnoringLocalCacheData;
			configuration.HTTPShouldSetCookies = NO;
			configuration.HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyNever;
			// TLS floor 1.2; the platform verifies the chain against the system
			// trust store and nothing here weakens that
			configuration.TLSMinimumSupportedProtocolVersion =
				tls_protocol_version_TLSv12;
			NSOperationQueue * queue = [[NSOperationQueue alloc] init];
			// SERIAL delegate queue: one callback at a time, so the lock is
			// never contended by the transport against itself
			queue.maxConcurrentOperationCount = 1;
			this->mSession = ORKIGE_HTTP_RETAIN([NSURLSession
				sessionWithConfiguration:configuration
				delegate:this->mDelegate delegateQueue:queue]);
			ORKIGE_HTTP_RELEASE(queue);
			return this->mSession != nil;
		}
		//---------------------------------------------------------
		void stop() override
		{
			// ORDER MATTERS. The session keeps its own strong reference to the
			// delegate until invalidation finishes, so a callback can still
			// fire after this method starts. Cutting the delegate's pointer
			// FIRST is what makes that safe: every handler below checks it and
			// returns, so no callback can enter a backend that is being torn
			// down. A handler already inside one holds mMutex, and the cleanup
			// below waits on that same mutex - so nothing is freed underneath
			// a running callback either.
			if (this->mDelegate != nil)
			{
				this->mDelegate->backend = NULL;
			}
			if (this->mSession != nil)
			{
				[this->mSession invalidateAndCancel];
				ORKIGE_HTTP_RELEASE(this->mSession);
				this->mSession = nil;
			}
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				for (std::map<HttpRequestId, Transfer *>::iterator at =
					this->mTransfers.begin(); at != this->mTransfers.end(); ++at)
				{
					if (at->second->task != nil)
					{
						ORKIGE_HTTP_RELEASE(at->second->task);
					}
					delete at->second;
				}
				this->mTransfers.clear();
			}
			if (this->mDelegate != nil)
			{
				ORKIGE_HTTP_RELEASE(this->mDelegate);
				this->mDelegate = nil;
			}
			this->mEvents.clear();
		}
		//---------------------------------------------------------
		void submit(HttpRequestId requestId, HttpClientRequest const & request,
			HttpUrlParts const & url) override
		{
			if (this->mSession == nil)
			{
				this->fail(requestId, HF_UNAVAILABLE,
					"the HTTP session is not up");
				return;
			}
			NSMutableURLRequest * nsRequest = [NSMutableURLRequest
				requestWithURL:[NSURL URLWithString:
					[NSString stringWithUTF8String:url.rebuild().c_str()]]];
			if (nsRequest == nil)
			{
				this->fail(requestId, HF_BAD_URL,
					"the URL could not be built");
				return;
			}
			nsRequest.HTTPMethod =
				[NSString stringWithUTF8String:request.method.c_str()];
			nsRequest.timeoutInterval =
				static_cast<double>(request.timeoutMs) / 1000.0;
			for (std::size_t at = 0; at < request.headers.size(); ++at)
			{
				[nsRequest setValue:[NSString stringWithUTF8String:
						request.headers[at].second.c_str()]
					forHTTPHeaderField:[NSString stringWithUTF8String:
						request.headers[at].first.c_str()]];
			}
			if (!request.body.empty())
			{
				nsRequest.HTTPBody = [NSData dataWithBytes:request.body.data()
					length:request.body.size()];
			}
			NSURLSessionDataTask * task =
				[this->mSession dataTaskWithRequest:nsRequest];
			if (task == nil)
			{
				this->fail(requestId, HF_TRANSPORT,
					"the transfer could not be created");
				return;
			}
			// the task carries its own request id, so a delegate callback
			// resolves its transfer with no search
			task.taskDescription = [NSString stringWithFormat:@"orkige:%u",
				requestId];
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				Transfer * transfer = new Transfer();
				transfer->id = requestId;
				transfer->response.id = requestId;
				transfer->maxBytes = request.maxResponseBytes;
				transfer->savePath = request.savePath;
				transfer->allowInsecure = request.allowInsecureHttp;
				transfer->followRedirects = request.followRedirects;
				transfer->maxRedirects = request.maxRedirects;
				transfer->url = url;
				transfer->deadline = [NSDate timeIntervalSinceReferenceDate] +
					(static_cast<double>(request.timeoutMs) / 1000.0);
				transfer->task = ORKIGE_HTTP_RETAIN(task);
				if (!transfer->savePath.empty())
				{
					String error;
					if (!transfer->file.begin(transfer->savePath, error))
					{
						ORKIGE_HTTP_RELEASE(task);
						delete transfer;
						this->fail(requestId, HF_BAD_SAVE_PATH, error);
						return;
					}
				}
				this->mTransfers[requestId] = transfer;
			}
			[task resume];
		}
		//---------------------------------------------------------
		void cancel(HttpRequestId requestId) override
		{
			NSURLSessionTask * task = nil;
			{
				std::lock_guard<std::mutex> lock(this->mMutex);
				std::map<HttpRequestId, Transfer *>::const_iterator found =
					this->mTransfers.find(requestId);
				if (found == this->mTransfers.end())
				{
					return;
				}
				task = found->second->task;
			}
			// the delegate's didCompleteWithError(NSURLErrorCancelled) retires
			// the state, so a cancel and a natural finish take the same path
			[task cancel];
		}
		//---------------------------------------------------------
		void poll(std::vector<HttpBackendEvent> & out) override
		{
			this->mEvents.drain(out);
		}
		//---------------------------------------------------------
		char const * name() const override { return "apple"; }
		//---------------------------------------------------------
		//--- the delegate-facing handlers (delegate queue) --------
		//---------------------------------------------------------
		//! @brief the response head arrived: status, headers and the announced
		//! size. @return false when the transfer must be cancelled.
		bool onResponse(HttpRequestId requestId, int status,
			std::map<String, String> const & headers, long long expectedLength)
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			Transfer * transfer = this->find(requestId);
			if (transfer == NULL)
			{
				return false;
			}
			transfer->response.status = status;
			transfer->response.headers = headers;
			if (expectedLength > 0)
			{
				transfer->expected =
					static_cast<unsigned long long>(expectedLength);
				// the CAP applies to the ANNOUNCED size too: an oversized
				// download is refused before its first byte, not after
				if (transfer->expected > transfer->maxBytes)
				{
					this->finishLocked(transfer, HF_TOO_LARGE,
						HttpPolicy::sizeCapReason(transfer->maxBytes,
							transfer->expected));
					return false;
				}
			}
			// a progress step with the known total before any body byte, so a
			// progress bar can size itself right away
			this->mEvents.pushProgress(requestId, 0, transfer->expected);
			return true;
		}
		//! a body chunk arrived: cap it, store or stream it, report progress
		void onData(HttpRequestId requestId, NSData * data)
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			Transfer * transfer = this->find(requestId);
			if (transfer == NULL)
			{
				return;
			}
			if ([NSDate timeIntervalSinceReferenceDate] > transfer->deadline)
			{
				// the WHOLE-request deadline: the platform's own timeout is an
				// IDLE one, so a trickling server would never trip it.
				// Cancel BEFORE retiring the transfer - finishLocked releases
				// our reference to the task, and the cancel's own completion
				// cannot run until this (serial) callback returns, by which
				// time the transfer is gone and it becomes a no-op.
				[transfer->task cancel];
				this->finishLocked(transfer, HF_TIMEOUT,
					"the request exceeded its timeout");
				return;
			}
			__block bool overflowed = false;
			__block bool writeFailed = false;
			__block String writeError;
			__block unsigned long long received = transfer->received;
			const unsigned long long maxBytes = transfer->maxBytes;
			const bool toFile = !transfer->savePath.empty();
			FileWriter * file = &transfer->file;
			String * body = &transfer->response.body;
			[data enumerateByteRangesUsingBlock:^(const void * bytes,
				NSRange range, BOOL * stop)
			{
				const unsigned long long length =
					static_cast<unsigned long long>(range.length);
				if (received + length > maxBytes)
				{
					overflowed = true;
					*stop = YES;
					return;
				}
				if (!toFile)
				{
					body->append(static_cast<char const *>(bytes), range.length);
				}
				else if (!file->write(static_cast<char const *>(bytes), length,
					writeError))
				{
					writeFailed = true;
					*stop = YES;
					return;
				}
				received += length;
			}];
			transfer->received = received;
			if (overflowed || writeFailed)
			{
				// cancel first, retire second (@see the deadline branch above)
				[transfer->task cancel];
				const HttpFailure failure =
					overflowed ? HF_TOO_LARGE : HF_WRITE_FAILED;
				this->finishLocked(transfer, failure, overflowed
					? HttpPolicy::sizeCapReason(maxBytes)
					: writeError);
				return;
			}
			this->mEvents.pushProgress(requestId, transfer->received,
				transfer->expected);
		}
		//! @brief a 3xx arrived. @return true to follow @p location.
		bool onRedirect(HttpRequestId requestId, String const & location)
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			Transfer * transfer = this->find(requestId);
			if (transfer == NULL)
			{
				return false;
			}
			if (!transfer->followRedirects)
			{
				// hand the 3xx itself back - the caller asked not to follow
				return false;
			}
			if (++transfer->redirects > transfer->maxRedirects)
			{
				this->finishLocked(transfer, HF_REDIRECT_REFUSED,
					"more than " + std::to_string(transfer->maxRedirects) +
					" redirects");
				return false;
			}
			// THE policy gate, shared with every other backend: never
			// https -> http
			HttpUrlParts next;
			String reason;
			const HttpFailure verdict = HttpPolicy::resolveRedirect(
				transfer->url, location, transfer->allowInsecure, next, reason);
			if (verdict != HF_NONE)
			{
				this->finishLocked(transfer, verdict, reason);
				return false;
			}
			transfer->url = next;
			// the redirected response starts over
			transfer->response.body.clear();
			transfer->response.headers.clear();
			transfer->received = 0;
			transfer->expected = 0;
			return true;
		}
		//! the transfer ended (naturally, cancelled or errored)
		void onComplete(HttpRequestId requestId, HttpFailure failure,
			String const & reason)
		{
			std::lock_guard<std::mutex> lock(this->mMutex);
			Transfer * transfer = this->find(requestId);
			if (transfer == NULL)
			{
				return;	// already retired (a cap/deadline refusal got there first)
			}
			this->finishLocked(transfer, failure, reason);
		}
	private:
		//! the transfer for an id, or NULL - CALLER HOLDS mMutex
		Transfer * find(HttpRequestId requestId)
		{
			std::map<HttpRequestId, Transfer *>::const_iterator found =
				this->mTransfers.find(requestId);
			return found != this->mTransfers.end() ? found->second : NULL;
		}
		//! @brief publish the ONE completion and retire the transfer -
		//! CALLER HOLDS mMutex; @p transfer is deleted here
		void finishLocked(Transfer * transfer, HttpFailure failure,
			String const & reason)
		{
			HttpClientResponse response = transfer->response;
			response.id = transfer->id;
			response.bytes = transfer->received;
			response.finalUrl = transfer->url.rebuild();
			if (failure == HF_NONE)
			{
				if (!transfer->savePath.empty())
				{
					String error;
					if (!transfer->file.commit(error))
					{
						response.completed = false;
						response.failure = HF_WRITE_FAILED;
						response.reason = error;
						this->retireLocked(transfer, response);
						return;
					}
					response.savedPath = transfer->savePath;
				}
				response.completed = true;
				response.failure = HF_NONE;
			}
			else
			{
				// a failed or cancelled save-to-file leaves NO file behind: the
				// temp file is dropped and any previous file stays untouched
				transfer->file.abort();
				response.completed = false;
				response.failure = failure;
				response.reason = reason;
			}
			this->retireLocked(transfer, response);
		}
		//! queue the completion, drop the transfer - CALLER HOLDS mMutex
		void retireLocked(Transfer * transfer, HttpClientResponse const & response)
		{
			this->mTransfers.erase(transfer->id);
			if (transfer->task != nil)
			{
				ORKIGE_HTTP_RELEASE(transfer->task);
				transfer->task = nil;
			}
			delete transfer;
			this->mEvents.pushCompletion(response);
		}
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
	};
	//---------------------------------------------------------
	HttpBackend * createHttpBackend()
	{
		return new AppleHttpBackend();
	}
	//---------------------------------------------------------
	namespace
	{
		//! the request id a task carries (@see submit), or 0
		HttpRequestId requestIdOfTask(NSURLSessionTask * task)
		{
			NSString * description = task.taskDescription;
			if (description == nil ||
				![description hasPrefix:@"orkige:"])
			{
				return 0;
			}
			return static_cast<HttpRequestId>(
				[[description substringFromIndex:7] intValue]);
		}
	}
}

@implementation OrkigeHttpSessionDelegate
//---------------------------------------------------------
- (void)URLSession:(NSURLSession *)session
	dataTask:(NSURLSessionDataTask *)dataTask
	didReceiveResponse:(NSURLResponse *)response
	completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler
{
	(void)session;
	const Orkige::HttpRequestId requestId = Orkige::requestIdOfTask(dataTask);
	if (self->backend == NULL || requestId == 0)
	{
		completionHandler(NSURLSessionResponseCancel);
		return;
	}
	int status = 0;
	std::map<Orkige::String, Orkige::String> headers;
	if ([response isKindOfClass:[NSHTTPURLResponse class]])
	{
		NSHTTPURLResponse * http = (NSHTTPURLResponse *)response;
		status = static_cast<int>(http.statusCode);
		NSDictionary * fields = http.allHeaderFields;
		for (NSString * key in fields)
		{
			NSObject * value = [fields objectForKey:key];
			if ([value isKindOfClass:[NSString class]])
			{
				headers[Orkige::HttpPolicy::toLowerAscii(
						Orkige::toEngineString(key))] =
					Orkige::toEngineString((NSString *)value);
			}
		}
	}
	const bool proceed = self->backend->onResponse(requestId, status, headers,
		response.expectedContentLength);
	completionHandler(proceed ? NSURLSessionResponseAllow
		: NSURLSessionResponseCancel);
}
//---------------------------------------------------------
- (void)URLSession:(NSURLSession *)session
	dataTask:(NSURLSessionDataTask *)dataTask
	didReceiveData:(NSData *)data
{
	(void)session;
	const Orkige::HttpRequestId requestId = Orkige::requestIdOfTask(dataTask);
	if (self->backend == NULL || requestId == 0)
	{
		return;
	}
	self->backend->onData(requestId, data);
}
//---------------------------------------------------------
- (void)URLSession:(NSURLSession *)session
	task:(NSURLSessionTask *)task
	willPerformHTTPRedirection:(NSHTTPURLResponse *)response
	newRequest:(NSURLRequest *)request
	completionHandler:(void (^)(NSURLRequest *))completionHandler
{
	(void)session;
	(void)response;
	const Orkige::HttpRequestId requestId = Orkige::requestIdOfTask(task);
	if (self->backend == NULL || requestId == 0)
	{
		completionHandler(nil);
		return;
	}
	const bool follow = self->backend->onRedirect(requestId,
		Orkige::toEngineString(request.URL.absoluteString));
	completionHandler(follow ? request : nil);
}
//---------------------------------------------------------
- (void)URLSession:(NSURLSession *)session
	task:(NSURLSessionTask *)task
	didCompleteWithError:(NSError *)error
{
	(void)session;
	const Orkige::HttpRequestId requestId = Orkige::requestIdOfTask(task);
	if (self->backend == NULL || requestId == 0)
	{
		return;
	}
	self->backend->onComplete(requestId, Orkige::failureFromNSError(error),
		Orkige::toEngineString(error.localizedDescription));
}
@end

#endif // ORKIGE_HTTP_APPLE
