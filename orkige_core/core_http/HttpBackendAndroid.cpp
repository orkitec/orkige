/**************************************************************
	created:	2026/07/30 at 16:00
	filename: 	HttpBackendAndroid.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The Android transport behind HttpClient, and the ONE translation unit in the
// tree that speaks JNI to the platform's own HTTP stack (its Java half is
// OrkigeHttp.java beside this file). Nothing above the HttpBackend seam knows
// it exists: HttpClient, the Lua `http` table and every caller stay plain C++,
// the same confinement the Apple backend's Objective-C gets.
//
// WHY the platform's stack rather than a bundled library: certificate
// verification must go through the trust store the DEVICE maintains, and on
// Android that store is more than a directory of public roots. It includes the
// user-installed and enterprise anchors a managed device or a developer's own
// proxy adds, and it is filtered by the app's Network Security Config - neither
// of which a library reading /system/etc/security/cacerts can see. The
// platform stack also inherits the device's proxy settings, and it costs the
// closure nothing: no TLS library and no HTTP library ship in the APK.
//
// WHAT STAYS ABOVE IT: every decision. Redirects are NOT followed by the
// platform (setInstanceFollowRedirects(false)); each hop comes back here and
// HttpPolicy::resolveRedirect decides whether there is a next one, so the rule
// that a secure request can never be redirected onto a plain one has ONE
// implementation for every platform. The size cap, the whole-request deadline
// and the save-to-file funnel are enforced here too.
//
// THE THREADING SHAPE: one worker thread per transfer, attached to the Java VM
// for its lifetime. The main thread only pushes intents (submit/cancel) and
// drains the HttpEventQueue - the worker-pushes/main-drains discipline the
// physics contact queue uses. A worker publishes exactly one completion and
// then marks itself reapable; the main thread joins it in poll().

#include "core_http/HttpBackend.h"

#ifdef ORKIGE_HTTP_ANDROID

#include "core_filesystem/FileWriter.h"
#include "core_http/HttpAndroid.h"
#include "core_http/HttpPolicy.h"

#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Orkige
{
	namespace
	{
		//! the Java half's result codes - kept in step with OrkigeHttp.java
		const jint JAVA_RESULT_OK = 0;
		const jint JAVA_RESULT_CONNECT = 1;
		const jint JAVA_RESULT_TLS = 2;
		const jint JAVA_RESULT_TIMEOUT = 3;
		const jint JAVA_RESULT_CANCELLED = 4;
		const jint JAVA_RESULT_TRANSPORT = 5;
		const jint JAVA_RESULT_STOPPED = 6;

		//! the Java companion class the transport drives
		char const * const JAVA_CLASS = "com/orkitec/orkige/OrkigeHttp";

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

		//! a Java string as an engine String ("" for null)
		String toString(JNIEnv * env, jstring text)
		{
			if (text == NULL)
			{
				return String();
			}
			char const * bytes = env->GetStringUTFChars(text, NULL);
			if (bytes == NULL)
			{
				return String();
			}
			const String result(bytes);
			env->ReleaseStringUTFChars(text, bytes);
			return result;
		}
	}
	//---------------------------------------------------------
	class AndroidHttpBackend;
	//---------------------------------------------------------
	//! @brief the platform-HTTP-stack transport (@see the file comment).
	class AndroidHttpBackend : public HttpBackend
	{
		//--- Types -------------------------------------------
	private:
		//! one in-flight request; its worker thread owns it until it finishes
		struct Transfer
		{
			HttpRequestId			id = 0;
			AndroidHttpBackend *	owner = NULL;	//!< for the static JNI hooks
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
			std::vector<char>		scratch;		//!< the JNI chunk landing pad
			unsigned long long		received = 0;
			unsigned long long		expected = 0;
			std::chrono::steady_clock::time_point	deadline;
			std::atomic<bool>		cancelled;		//!< cancel() ran
			bool					capExceeded = false;
			//! was the cap hit by the ANNOUNCED size (so the refusal can name
			//! it) rather than by the bytes that actually arrived
			bool					capAnnounced = false;
			bool					timedOut = false;
			bool					writeFailed = false;
			String					writeError;
			String					reason;			//!< the platform's own words
			std::thread				worker;
			std::atomic<bool>		finished;		//!< the main thread may reap
			Transfer() : cancelled(false), finished(false) {}
		};
		//--- Variables ---------------------------------------
	private:
		JavaVM *						mVm;
		jclass							mClass;		//!< global ref, the Java half
		jmethodID						mPerform;
		jmethodID						mCancel;
		bool							mRunning;
		std::mutex						mLiveMutex;	//!< guards mLive
		std::map<HttpRequestId, Transfer *>	mLive;	//!< every unreaped transfer
		HttpEventQueue					mEvents;
		//--- Methods -----------------------------------------
	public:
		AndroidHttpBackend()
			: mVm(NULL), mClass(NULL), mPerform(NULL), mCancel(NULL),
			mRunning(false) {}
		virtual ~AndroidHttpBackend() { this->stop(); }
		//---------------------------------------------------------
		//! @brief resolve the Java half and wire the callbacks. Runs on the
		//! MAIN thread (the seam's contract), which is the one attachment that
		//! can see the app's own classes - a worker thread's class loader only
		//! finds system classes, so the lookup can never move off it.
		bool start() override
		{
			if (this->mRunning)
			{
				return true;
			}
			this->mVm = static_cast<JavaVM *>(HttpAndroid::getJavaVM());
			if (this->mVm == NULL)
			{
				// no host registered a VM: refuse honestly rather than crash
				return false;
			}
			JNIEnv * env = this->attachedEnv();
			if (env == NULL)
			{
				return false;
			}
			jclass local = env->FindClass(JAVA_CLASS);
			if (local == NULL || env->ExceptionCheck())
			{
				env->ExceptionClear();
				return false;
			}
			this->mClass = static_cast<jclass>(env->NewGlobalRef(local));
			env->DeleteLocalRef(local);
			if (this->mClass == NULL)
			{
				return false;
			}
			this->mPerform = env->GetStaticMethodID(this->mClass, "perform",
				"(JLjava/lang/String;Ljava/lang/String;[Ljava/lang/String;"
				"[Ljava/lang/String;[BII)I");
			this->mCancel = env->GetStaticMethodID(this->mClass, "cancel",
				"(J)V");
			if (this->mPerform == NULL || this->mCancel == NULL ||
				env->ExceptionCheck())
			{
				env->ExceptionClear();
				env->DeleteGlobalRef(this->mClass);
				this->mClass = NULL;
				return false;
			}
			const JNINativeMethod hooks[] = {
				{ const_cast<char *>("nativeHead"),
					const_cast<char *>("(JI[Ljava/lang/String;JLjava/lang/"
						"String;)I"),
					reinterpret_cast<void *>(&AndroidHttpBackend::onHead) },
				{ const_cast<char *>("nativeBody"),
					const_cast<char *>("(J[BI)I"),
					reinterpret_cast<void *>(&AndroidHttpBackend::onBody) },
				{ const_cast<char *>("nativeReason"),
					const_cast<char *>("(JLjava/lang/String;)V"),
					reinterpret_cast<void *>(&AndroidHttpBackend::onReason) },
			};
			if (env->RegisterNatives(this->mClass, hooks, 3) != JNI_OK ||
				env->ExceptionCheck())
			{
				env->ExceptionClear();
				env->DeleteGlobalRef(this->mClass);
				this->mClass = NULL;
				return false;
			}
			this->mRunning = true;
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
				this->callJavaCancel(live[at]);
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
				delete live[at];
			}
			JNIEnv * env = this->attachedEnv();
			if (env != NULL && this->mClass != NULL)
			{
				env->UnregisterNatives(this->mClass);
				env->DeleteGlobalRef(this->mClass);
			}
			this->mClass = NULL;
			this->mPerform = NULL;
			this->mCancel = NULL;
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
			transfer->sendBody = !request.body.empty() ||
				HttpPolicy::methodTakesBody(request.method);
			transfer->finalUrl = url.rebuild();
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
			transfer->worker = std::thread(&AndroidHttpBackend::runTransfer,
				this, transfer);
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
			// the flag stops the next chunk; the Java disconnect unblocks a
			// transfer that is waiting on the network right now
			transfer->cancelled = true;
			this->callJavaCancel(transfer);
		}
		//---------------------------------------------------------
		void poll(std::vector<HttpBackendEvent> & out) override
		{
			this->mEvents.drain(out);
			this->reapFinished();
		}
		//---------------------------------------------------------
		char const * name() const override { return "android"; }
	private:
		//---------------------------------------------------------
		//! this thread's JNIEnv, attaching it to the VM if it is not already
		JNIEnv * attachedEnv()
		{
			if (this->mVm == NULL)
			{
				return NULL;
			}
			JNIEnv * env = NULL;
			const jint state = this->mVm->GetEnv(
				reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
			if (state == JNI_OK)
			{
				return env;
			}
			if (state != JNI_EDETACHED)
			{
				return NULL;
			}
			JavaVMAttachArgs args;
			args.version = JNI_VERSION_1_6;
			args.name = const_cast<char *>("orkige-http");
			args.group = NULL;
			if (this->mVm->AttachCurrentThread(&env, &args) != JNI_OK)
			{
				return NULL;
			}
			return env;
		}
		//---------------------------------------------------------
		//! ask the Java half to abort @p transfer (any thread)
		void callJavaCancel(Transfer * transfer)
		{
			JNIEnv * env = this->attachedEnv();
			if (env == NULL || this->mClass == NULL || this->mCancel == NULL)
			{
				return;
			}
			env->CallStaticVoidMethod(this->mClass, this->mCancel,
				static_cast<jlong>(reinterpret_cast<std::intptr_t>(transfer)));
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
			}
		}
		//---------------------------------------------------------
		//! @brief the worker: one attached thread walking this request's hops
		//! until the policy says there is no next one.
		void runTransfer(Transfer * transfer)
		{
			JNIEnv * env = this->attachedEnv();
			if (env == NULL)
			{
				this->finish(transfer, HF_TRANSPORT,
					"the request could not reach the platform's HTTP stack");
				transfer->finished.store(true, std::memory_order_release);
				return;
			}
			HttpFailure failure = HF_NONE;
			String reason;
			for (;;)
			{
				const jint result = this->performHop(env, transfer);
				if (result == JAVA_RESULT_OK && transfer->request.followRedirects
					&& isRedirectStatus(transfer->status)
					&& !transfer->location.empty())
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
				failure = this->failureFor(transfer, result, reason);
				break;
			}
			this->finish(transfer, failure, reason);
			if (this->mVm != NULL)
			{
				this->mVm->DetachCurrentThread();
			}
			// LAST: nothing may touch the transfer after the main thread is
			// told it may reap it
			transfer->finished.store(true, std::memory_order_release);
		}
		//---------------------------------------------------------
		//! run ONE exchange through the Java half
		jint performHop(JNIEnv * env, Transfer * transfer)
		{
			HttpClientRequest const & request = transfer->request;
			const std::size_t headerCount = request.headers.size();
			// every string below is a local reference; one frame keeps them
			// bounded however many headers a caller sent
			if (env->PushLocalFrame(static_cast<jint>(16 + 2 * headerCount))
				!= JNI_OK)
			{
				transfer->reason = "the platform's HTTP stack ran out of "
					"references";
				return JAVA_RESULT_TRANSPORT;
			}
			jclass stringClass = env->FindClass("java/lang/String");
			jobjectArray names = env->NewObjectArray(
				static_cast<jsize>(headerCount), stringClass, NULL);
			jobjectArray values = env->NewObjectArray(
				static_cast<jsize>(headerCount), stringClass, NULL);
			for (std::size_t at = 0; at < headerCount; ++at)
			{
				env->SetObjectArrayElement(names, static_cast<jsize>(at),
					env->NewStringUTF(request.headers[at].first.c_str()));
				env->SetObjectArrayElement(values, static_cast<jsize>(at),
					env->NewStringUTF(request.headers[at].second.c_str()));
			}
			jbyteArray body = NULL;
			if (transfer->sendBody)
			{
				body = env->NewByteArray(
					static_cast<jsize>(request.body.size()));
				if (!request.body.empty())
				{
					env->SetByteArrayRegion(body, 0,
						static_cast<jsize>(request.body.size()),
						reinterpret_cast<jbyte const *>(request.body.data()));
				}
			}
			// the whole-request deadline is what remains of it: the platform
			// stack bounds a connect and a read, and this bounds the request
			const long long remaining = std::chrono::duration_cast<
				std::chrono::milliseconds>(transfer->deadline -
					std::chrono::steady_clock::now()).count();
			const jint budget = static_cast<jint>(remaining > 1 ? remaining : 1);
			const jint connectBudget = budget < 10000 ? budget : 10000;
			jstring url = env->NewStringUTF(transfer->url.rebuild().c_str());
			jstring method = env->NewStringUTF(transfer->method.c_str());
			jint result = env->CallStaticIntMethod(this->mClass, this->mPerform,
				static_cast<jlong>(reinterpret_cast<std::intptr_t>(transfer)),
				url, method, names, values, body, connectBudget, budget);
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
				transfer->reason = "the platform's HTTP stack refused the "
					"request";
				result = JAVA_RESULT_TRANSPORT;
			}
			env->PopLocalFrame(NULL);
			return result;
		}
		//---------------------------------------------------------
		//! the Java result plus what the hooks recorded -> the one failure
		HttpFailure failureFor(Transfer * transfer, jint result,
			String & reason) const
		{
			// what a hook stopped the transfer FOR outranks the stop itself
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
					std::to_string(transfer->request.timeoutMs) +
					"ms deadline";
				return HF_TIMEOUT;
			}
			if (transfer->cancelled)
			{
				reason = "the request was cancelled";
				return HF_CANCELLED;
			}
			switch (result)
			{
			case JAVA_RESULT_OK:
				return HF_NONE;
			case JAVA_RESULT_CONNECT:
				reason = transfer->reason.empty()
					? String("the host could not be reached") : transfer->reason;
				return HF_CONNECT_FAILED;
			case JAVA_RESULT_TLS:
				reason = transfer->reason.empty()
					? String("the secure connection could not be established")
					: transfer->reason;
				return HF_TLS_FAILED;
			case JAVA_RESULT_TIMEOUT:
				reason = transfer->reason.empty()
					? String("the request timed out") : transfer->reason;
				return HF_TIMEOUT;
			case JAVA_RESULT_CANCELLED:
				reason = "the request was cancelled";
				return HF_CANCELLED;
			case JAVA_RESULT_STOPPED:
				// a hook stopped it and none of the reasons above is set: the
				// only remaining one is a cancel that raced the flag
				reason = "the request was cancelled";
				return HF_CANCELLED;
			default:
				reason = transfer->reason.empty()
					? String("the request could not be completed")
					: transfer->reason;
				return HF_TRANSPORT;
			}
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
				delete done[at];
			}
		}
		//---------------------------------------------------------
		//--- the JNI hooks (worker thread, inside perform()) ------
		//---------------------------------------------------------
		//! the response head of one hop: record it and apply the announced cap
		static jint onHead(JNIEnv * env, jclass, jlong token, jint status,
			jobjectArray headers, jlong contentLength, jstring finalUrl)
		{
			Transfer * transfer = reinterpret_cast<Transfer *>(
				static_cast<std::intptr_t>(token));
			// only the FINAL response is the answer: a redirect hop's head
			// replaces whatever the previous one left behind
			transfer->headers.clear();
			transfer->body.clear();
			transfer->location.clear();
			transfer->received = 0;
			transfer->status = static_cast<int>(status);
			transfer->finalUrl = toString(env, finalUrl);
			transfer->expected = contentLength > 0
				? static_cast<unsigned long long>(contentLength) : 0;
			const jsize count = headers != NULL
				? env->GetArrayLength(headers) : 0;
			for (jsize at = 0; at + 1 < count; at += 2)
			{
				jstring name = static_cast<jstring>(
					env->GetObjectArrayElement(headers, at));
				jstring value = static_cast<jstring>(
					env->GetObjectArrayElement(headers, at + 1));
				const String folded = HttpPolicy::toLowerAscii(
					toString(env, name));
				const String text = toString(env, value);
				transfer->headers[folded] = text;
				if (folded == "location")
				{
					transfer->location = text;
				}
				env->DeleteLocalRef(name);
				env->DeleteLocalRef(value);
			}
			if (transfer->cancelled)
			{
				return 1;
			}
			// the ANNOUNCED size is capped too, so an oversized download is
			// refused before its first body byte rather than after
			if (transfer->expected > transfer->request.maxResponseBytes)
			{
				transfer->capExceeded = true;
				transfer->capAnnounced = true;
				return 1;
			}
			return 0;
		}
		//---------------------------------------------------------
		//! body bytes: bound them, then store or stream them
		static jint onBody(JNIEnv * env, jclass, jlong token, jbyteArray chunk,
			jint length)
		{
			Transfer * transfer = reinterpret_cast<Transfer *>(
				static_cast<std::intptr_t>(token));
			if (transfer->cancelled)
			{
				return 1;
			}
			if (std::chrono::steady_clock::now() > transfer->deadline)
			{
				// the platform bounds a connect and a single read; the
				// WHOLE-request deadline is ours to keep
				transfer->timedOut = true;
				return 1;
			}
			if (transfer->request.followRedirects &&
				isRedirectStatus(transfer->status) &&
				!transfer->location.empty() &&
				transfer->hop < transfer->request.maxRedirects)
			{
				// a hop we are about to leave: its body is not the answer, and
				// letting it through would prepend explanatory HTML to the file
				// the NEXT hop streams (the in-memory buffer is cleared per
				// head, but a file's bytes are already gone)
				return 0;
			}
			const unsigned long long size =
				static_cast<unsigned long long>(length);
			if (transfer->received + size > transfer->request.maxResponseBytes)
			{
				transfer->capExceeded = true;
				return 1;
			}
			if (transfer->scratch.size() < static_cast<std::size_t>(length))
			{
				transfer->scratch.resize(static_cast<std::size_t>(length));
			}
			env->GetByteArrayRegion(chunk, 0, length,
				reinterpret_cast<jbyte *>(transfer->scratch.data()));
			if (transfer->request.savePath.empty())
			{
				transfer->body.append(transfer->scratch.data(),
					static_cast<std::size_t>(length));
			}
			else if (!transfer->file.write(transfer->scratch.data(), size,
				transfer->writeError))
			{
				transfer->writeFailed = true;
				return 1;
			}
			transfer->received += size;
			transfer->owner->mEvents.pushProgress(transfer->id,
				transfer->received, transfer->expected);
			return 0;
		}
		//---------------------------------------------------------
		//! the platform's own words for a failure, kept for the answer
		static void onReason(JNIEnv * env, jclass, jlong token, jstring reason)
		{
			Transfer * transfer = reinterpret_cast<Transfer *>(
				static_cast<std::intptr_t>(token));
			transfer->reason = toString(env, reason);
		}
	};
	//---------------------------------------------------------
	HttpBackend * createHttpBackend()
	{
		return new AndroidHttpBackend();
	}
}

#endif // ORKIGE_HTTP_ANDROID
