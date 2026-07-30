/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpPolicy.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __HttpPolicy_h__30_7_2026__10_00_00__
#define __HttpPolicy_h__30_7_2026__10_00_00__

#include "core_http/HttpTypes.h"
#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Http
	*  @{ */

	//! @brief a URL split into its parts (what the transport needs to know).
	struct ORKIGE_CORE_DLL HttpUrlParts
	{
		String	scheme;		//!< "http" or "https", lower-cased
		String	host;		//!< host or IP literal (no brackets for IPv6)
		String	userinfo;	//!< anything before an '@' (refused, @see HttpPolicy)
		String	path;		//!< path + query + fragment, always starting with '/'
		int		port = 0;	//!< explicit port, 0 = the scheme default
		bool	secure = false;	//!< is the scheme https

		//! the effective port (the scheme default when none was given)
		int effectivePort() const
		{
			return this->port != 0 ? this->port : (this->secure ? 443 : 80);
		}
		//! the URL rebuilt from the parts (canonical form)
		String rebuild() const;
	};

	//! @brief the PURE decisions of the HTTP client - URL parsing, the
	//! security policy and header hygiene, with no transport and no state.
	//!
	//! Every backend runs a request through validate() BEFORE touching a
	//! socket, so the refusal a caller sees is identical on every platform
	//! (and is unit-testable headlessly). The rules, in one place:
	//!
	//!  - only http and https exist; anything else is refused by name
	//!  - https is the default: a plain http:// URL needs the caller's
	//!    explicit allowInsecureHttp opt-in (a localhost dev service is the
	//!    real use case) and is otherwise refused
	//!  - credentials in the URL (user:password@host) are refused - they leak
	//!    into logs and proxies; an Authorization header is the way
	//!  - a redirect may never DOWNGRADE: an https request that is redirected
	//!    to http:// is refused, whatever the opt-in says (a plain-http
	//!    request may be redirected to either, since it started insecure)
	//!  - header names must be tokens and no header may carry CR/LF (a
	//!    request-splitting injection) or NUL
	namespace HttpPolicy
	{
		//! @brief split an absolute URL into its parts.
		//! @return HF_NONE on success, else the failure describing the refusal
		//! (HF_BAD_URL / HF_UNSUPPORTED_SCHEME / HF_CREDENTIALS_IN_URL)
		ORKIGE_CORE_DLL HttpFailure parseUrl(String const & url,
			HttpUrlParts & out, String & reason);

		//! @brief the FULL gate a request passes before any transport work:
		//! parseUrl + the https-only policy + method and header hygiene +
		//! the save-path and cap sanity checks.
		//! @return HF_NONE (parts filled) or the failure, with @p reason set
		//! to a one-line explanation fit to show a player
		ORKIGE_CORE_DLL HttpFailure validate(HttpClientRequest const & request,
			HttpUrlParts & out, String & reason);

		//! @brief resolve a redirect Location against the URL it came from -
		//! absolute, scheme-relative ("//host/x"), absolute-path ("/x") and
		//! relative ("x") forms all resolve here.
		//! @return HF_NONE with @p out set, or the failure (the reason names
		//! the refused downgrade / unsupported scheme)
		ORKIGE_CORE_DLL HttpFailure resolveRedirect(HttpUrlParts const & from,
			String const & location, bool allowInsecureHttp, HttpUrlParts & out,
			String & reason);

		//! is @p name a valid header name (a non-empty HTTP token)
		ORKIGE_CORE_DLL bool validHeaderName(String const & name);
		//! is @p value a valid header value (no CR, LF or NUL)
		ORKIGE_CORE_DLL bool validHeaderValue(String const & value);
		//! is @p method a valid method token (non-empty, no separators)
		ORKIGE_CORE_DLL bool validMethod(String const & method);

		//! @brief does @p method carry a request body by convention (POST,
		//! PUT, PATCH)? A body on a GET is allowed but unusual - the client
		//! sends what the caller asked for and never rewrites the method.
		ORKIGE_CORE_DLL bool methodTakesBody(String const & method);

		//! @brief lower-case an ASCII string (header-name folding; the engine
		//! stores response header names lower-cased)
		ORKIGE_CORE_DLL String toLowerAscii(String const & text);

		//! @brief split a raw header block ("Name: value\r\n...") into a
		//! lower-cased-name map - the shape every backend hands back
		ORKIGE_CORE_DLL void parseHeaderBlock(String const & block,
			std::map<String, String> & out);
	}

	/** @} */
}

#endif //__HttpPolicy_h__30_7_2026__10_00_00__
