/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpTypes.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __HttpTypes_h__30_7_2026__10_00_00__
#define __HttpTypes_h__30_7_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace Orkige
{
	/** \addtogroup Http
	*  @{ */

	//! @brief a submitted request's handle; 0 is the never-valid id.
	typedef unsigned int HttpRequestId;

	//! @brief why a request did not complete as an HTTP exchange.
	//! @remarks an HTTP status is NOT a failure: a 404 or a 500 is a
	//! COMPLETED exchange (HF_NONE) whose status carries the server's
	//! verdict. Only the values below mean "no answer was obtained".
	enum HttpFailure
	{
		HF_NONE = 0,			//!< the exchange completed (read the status)
		HF_UNAVAILABLE,			//!< no client in this build (@see HttpClient::available)
		HF_BAD_URL,				//!< the URL does not parse
		HF_UNSUPPORTED_SCHEME,	//!< not http:// or https://
		HF_INSECURE_SCHEME,		//!< plain http:// without the explicit opt-in
		HF_CREDENTIALS_IN_URL,	//!< user:password@host - use an Authorization header
		HF_BAD_HEADER,			//!< a header name/value is empty or carries control bytes
		HF_BAD_METHOD,			//!< the method is empty or not a token
		HF_BAD_SAVE_PATH,		//!< the save-to-file path is empty or not creatable
		HF_CONNECT_FAILED,		//!< DNS/connect refused or unreachable
		HF_TLS_FAILED,			//!< certificate/handshake verification failed
		HF_TIMEOUT,				//!< the per-request timeout elapsed
		HF_TOO_LARGE,			//!< the response exceeded maxResponseBytes
		HF_REDIRECT_REFUSED,	//!< a redirect the security policy forbids (or too many)
		HF_WRITE_FAILED,		//!< save-to-file could not be written
		HF_CANCELLED,			//!< cancel() was called before completion
		HF_TRANSPORT			//!< any other transport-level error (reason carries it)
	};

	//! @brief a request to submit through HttpClient.
	//! @remarks defaults are the SECURE, BOUNDED ones: https-only, certificates
	//! verified, a 30 s timeout and a response cap - a caller opts OUT
	//! explicitly and visibly, never by forgetting a field.
	struct ORKIGE_CORE_DLL HttpClientRequest
	{
		//! the absolute URL ("https://host[:port]/path?query")
		String								url;
		//! HTTP method - "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"
		String								method = "GET";
		//! request headers, in submission order (name -> value)
		std::vector<std::pair<String, String> >	headers;
		//! entity body (POST/PUT/PATCH); empty for GET/HEAD
		String								body;
		//! @brief convenience for the Content-Type header: set when the body
		//! is not empty and no explicit content-type header was given
		String								contentType;
		//! @brief non-empty = SAVE-TO-FILE mode: the body is streamed to this
		//! path instead of being kept in memory (asset/update downloads).
		//! The bytes land in a sibling temp file that is renamed over the
		//! target ONLY on success, so a failed/cancelled transfer never
		//! leaves a truncated file where a good one was.
		String								savePath;
		//! whole-request timeout in milliseconds (0 = the backend default)
		unsigned int						timeoutMs = 30000;
		//! @brief hard cap on the response body; a bigger response is aborted
		//! with HF_TOO_LARGE instead of growing memory without bound
		unsigned long long					maxResponseBytes = 16ull * 1024ull * 1024ull;
		//! @brief allow a plain-http:// URL (a localhost dev service is the
		//! real use case). Default false: https or an honest refusal.
		bool								allowInsecureHttp = false;
		//! follow 3xx redirects (never https -> http, @see HttpPolicy)
		bool								followRedirects = true;
		//! most redirects to follow before HF_REDIRECT_REFUSED
		unsigned int						maxRedirects = 5;
	};

	//! @brief the answer delivered to the completion callback.
	struct ORKIGE_CORE_DLL HttpClientResponse
	{
		//! the request this answers
		HttpRequestId						id = 0;
		//! @brief did the exchange complete (status/headers/body are valid)?
		//! false means the transfer never produced an HTTP answer - `failure`
		//! and `reason` say why, in words fit to show a player.
		bool								completed = false;
		//! HTTP status code (valid when completed)
		int									status = 0;
		//! response headers, names lower-cased (HTTP names are case-insensitive)
		std::map<String, String>			headers;
		//! response body; ALWAYS empty in save-to-file mode
		String								body;
		//! the file the body landed in (save-to-file mode, on success)
		String								savedPath;
		//! bytes of body received
		unsigned long long					bytes = 0;
		//! the final URL after any followed redirects
		String								finalUrl;
		//! why the exchange did not complete (HF_NONE when it did)
		HttpFailure							failure = HF_NONE;
		//! @brief a one-line, human-readable reason - populated for EVERY
		//! refusal and error (the engine never fails a request silently)
		String								reason;

		//! @brief the "it worked" shorthand: completed with a 2xx status
		bool ok() const
		{
			return this->completed && this->status >= 200 && this->status < 300;
		}
		//! a response header by (lower-cased) name, "" when absent
		String header(String const & lowerName) const;
	};

	//! @brief completion callback - invoked ONCE per submitted request, on the
	//! main thread, from HttpClient::update()
	typedef std::function<void(HttpClientResponse const &)> HttpCompleteCallback;
	//! @brief progress callback - received/total bytes of the response body
	//! (total is 0 while unknown, i.e. no Content-Length yet); invoked on the
	//! main thread from HttpClient::update(), coalesced to at most one call per
	//! request per drain
	typedef std::function<void(unsigned long long received,
		unsigned long long total)> HttpProgressCallback;

	//! @brief a failure's stable short name ("timeout", "tls", ...) - the
	//! token the Lua surface and the logs report
	ORKIGE_CORE_DLL String const & httpFailureName(HttpFailure failure);

	/** @} */
}

#endif //__HttpTypes_h__30_7_2026__10_00_00__
