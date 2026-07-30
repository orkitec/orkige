/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpPolicy.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_http/HttpPolicy.h"

#include <map>
#include <string>

namespace Orkige
{
	namespace
	{
		//! is c an ASCII letter
		bool isAsciiLetter(char c)
		{
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
		}
		//! is c an ASCII digit
		bool isAsciiDigit(char c)
		{
			return c >= '0' && c <= '9';
		}
		//! @brief is c an HTTP token character (RFC 7230 tchar)? Header names
		//! and methods are tokens; a separator or control byte in either is a
		//! request-splitting hazard, so both are checked against this set.
		bool isTokenChar(char c)
		{
			if (isAsciiLetter(c) || isAsciiDigit(c))
			{
				return true;
			}
			switch (c)
			{
			case '!': case '#': case '$': case '%': case '&': case '\'':
			case '*': case '+': case '-': case '.': case '^': case '_':
			case '`': case '|': case '~':
				return true;
			default:
				return false;
			}
		}
		//! trim leading/trailing spaces and tabs
		String trimSpaces(String const & text)
		{
			size_t first = 0;
			while (first < text.size() &&
				(text[first] == ' ' || text[first] == '\t'))
			{
				++first;
			}
			size_t last = text.size();
			while (last > first &&
				(text[last - 1] == ' ' || text[last - 1] == '\t'))
			{
				--last;
			}
			return text.substr(first, last - first);
		}
		//! @brief drop the last path segment ("/a/b/c" -> "/a/b/", "/a" -> "/")
		//! - the base a relative redirect resolves against
		String pathDirectory(String const & path)
		{
			const size_t slash = path.rfind('/');
			if (slash == String::npos)
			{
				return "/";
			}
			return path.substr(0, slash + 1);
		}
	}
	//---------------------------------------------------------
	String HttpUrlParts::rebuild() const
	{
		String url = this->scheme + "://" + this->host;
		if (this->port != 0)
		{
			url += ":" + std::to_string(this->port);
		}
		url += this->path.empty() ? String("/") : this->path;
		return url;
	}
	//---------------------------------------------------------
	namespace HttpPolicy
	{
		//---------------------------------------------------------
		String toLowerAscii(String const & text)
		{
			String lower = text;
			for (size_t at = 0; at < lower.size(); ++at)
			{
				if (lower[at] >= 'A' && lower[at] <= 'Z')
				{
					lower[at] = static_cast<char>(lower[at] - 'A' + 'a');
				}
			}
			return lower;
		}
		//---------------------------------------------------------
		bool validHeaderName(String const & name)
		{
			if (name.empty())
			{
				return false;
			}
			for (size_t at = 0; at < name.size(); ++at)
			{
				if (!isTokenChar(name[at]))
				{
					return false;
				}
			}
			return true;
		}
		//---------------------------------------------------------
		bool validHeaderValue(String const & value)
		{
			for (size_t at = 0; at < value.size(); ++at)
			{
				const char c = value[at];
				// CR/LF would SPLIT the request into a second one; NUL
				// truncates it inside the C transport
				if (c == '\r' || c == '\n' || c == '\0')
				{
					return false;
				}
			}
			return true;
		}
		//---------------------------------------------------------
		bool validMethod(String const & method)
		{
			return validHeaderName(method);	// a method is a token too
		}
		//---------------------------------------------------------
		bool methodTakesBody(String const & method)
		{
			const String upper = method;
			return upper == "POST" || upper == "PUT" || upper == "PATCH";
		}
		//---------------------------------------------------------
		HttpFailure parseUrl(String const & url, HttpUrlParts & out,
			String & reason)
		{
			out = HttpUrlParts();
			if (url.empty())
			{
				reason = "the URL is empty";
				return HF_BAD_URL;
			}
			const size_t schemeEnd = url.find("://");
			if (schemeEnd == String::npos || schemeEnd == 0)
			{
				reason = "'" + url + "' is not an absolute URL "
					"(expected http://host/... or https://host/...)";
				return HF_BAD_URL;
			}
			out.scheme = toLowerAscii(url.substr(0, schemeEnd));
			if (out.scheme == "https")
			{
				out.secure = true;
			}
			else if (out.scheme != "http")
			{
				reason = "the scheme '" + out.scheme + "' is not supported "
					"(only http and https are)";
				return HF_UNSUPPORTED_SCHEME;
			}
			String authority = url.substr(schemeEnd + 3);
			// the path starts at the first '/', '?' or '#' after the authority
			size_t pathStart = authority.size();
			for (size_t at = 0; at < authority.size(); ++at)
			{
				if (authority[at] == '/' || authority[at] == '?' ||
					authority[at] == '#')
				{
					pathStart = at;
					break;
				}
			}
			out.path = authority.substr(pathStart);
			authority = authority.substr(0, pathStart);
			if (out.path.empty() || out.path[0] != '/')
			{
				// "https://host?q" and "https://host" both mean the root
				out.path = "/" + out.path;
			}
			const size_t at = authority.rfind('@');
			if (at != String::npos)
			{
				out.userinfo = authority.substr(0, at);
				authority = authority.substr(at + 1);
				reason = "credentials in the URL are refused - pass an "
					"Authorization header instead (they leak into logs, "
					"proxies and crash reports)";
				return HF_CREDENTIALS_IN_URL;
			}
			// an IPv6 literal keeps its brackets out of the host
			if (!authority.empty() && authority[0] == '[')
			{
				const size_t close = authority.find(']');
				if (close == String::npos)
				{
					reason = "the IPv6 host literal in '" + url +
						"' is not closed";
					return HF_BAD_URL;
				}
				out.host = authority.substr(1, close - 1);
				authority = authority.substr(close + 1);
				if (!authority.empty() && authority[0] != ':')
				{
					reason = "unexpected text after the IPv6 host in '" +
						url + "'";
					return HF_BAD_URL;
				}
			}
			else
			{
				const size_t colon = authority.find(':');
				out.host = authority.substr(0, colon == String::npos
					? authority.size() : colon);
				authority = colon == String::npos
					? String() : authority.substr(colon);
			}
			if (out.host.empty())
			{
				reason = "'" + url + "' has no host";
				return HF_BAD_URL;
			}
			for (size_t index = 0; index < out.host.size(); ++index)
			{
				const char c = out.host[index];
				// a host is letters/digits/'-'/'.'/'_' (plus ':' inside the
				// IPv6 literal already split off above); anything else is
				// either an encoding mistake or a smuggling attempt
				if (!isAsciiLetter(c) && !isAsciiDigit(c) && c != '-' &&
					c != '.' && c != '_' && c != ':')
				{
					reason = "the host '" + out.host + "' carries an invalid "
						"character";
					return HF_BAD_URL;
				}
			}
			if (!authority.empty())
			{
				const String portText = authority.substr(1);
				if (portText.empty())
				{
					reason = "'" + url + "' ends in a ':' with no port";
					return HF_BAD_URL;
				}
				int port = 0;
				for (size_t index = 0; index < portText.size(); ++index)
				{
					if (!isAsciiDigit(portText[index]))
					{
						reason = "the port '" + portText + "' is not a number";
						return HF_BAD_URL;
					}
					port = port * 10 + (portText[index] - '0');
					if (port > 65535)
					{
						reason = "the port '" + portText + "' is out of range";
						return HF_BAD_URL;
					}
				}
				if (port == 0)
				{
					reason = "the port must be 1..65535";
					return HF_BAD_URL;
				}
				out.port = port;
			}
			reason.clear();
			return HF_NONE;
		}
		//---------------------------------------------------------
		HttpFailure validate(HttpClientRequest const & request,
			HttpUrlParts & out, String & reason)
		{
			const HttpFailure parsed = parseUrl(request.url, out, reason);
			if (parsed != HF_NONE)
			{
				return parsed;
			}
			// THE https default: plain http needs the explicit opt-in
			if (!out.secure && !request.allowInsecureHttp)
			{
				reason = "refusing the plain-http URL '" + request.url +
					"' - use https, or set allowInsecureHttp when you really "
					"mean an unencrypted connection (a local dev service)";
				return HF_INSECURE_SCHEME;
			}
			if (!validMethod(request.method))
			{
				reason = "'" + request.method + "' is not a valid HTTP method";
				return HF_BAD_METHOD;
			}
			for (size_t index = 0; index < request.headers.size(); ++index)
			{
				String const & name = request.headers[index].first;
				String const & value = request.headers[index].second;
				if (!validHeaderName(name))
				{
					reason = "the header name '" + name + "' is not a valid "
						"HTTP token";
					return HF_BAD_HEADER;
				}
				if (!validHeaderValue(value))
				{
					reason = "the value of the header '" + name + "' carries a "
						"line break or NUL byte";
					return HF_BAD_HEADER;
				}
			}
			if (!request.contentType.empty() &&
				!validHeaderValue(request.contentType))
			{
				reason = "the content type carries a line break or NUL byte";
				return HF_BAD_HEADER;
			}
			if (request.maxResponseBytes == 0)
			{
				reason = "maxResponseBytes is 0 - nothing could be received";
				return HF_TOO_LARGE;
			}
			reason.clear();
			return HF_NONE;
		}
		//---------------------------------------------------------
		HttpFailure resolveRedirect(HttpUrlParts const & from,
			String const & location, bool allowInsecureHttp, HttpUrlParts & out,
			String & reason)
		{
			const String target = trimSpaces(location);
			if (target.empty())
			{
				reason = "the redirect carries no Location";
				return HF_REDIRECT_REFUSED;
			}
			String absolute;
			if (target.find("://") != String::npos)
			{
				absolute = target;
			}
			else if (target.size() >= 2 && target[0] == '/' && target[1] == '/')
			{
				// scheme-relative: keep the scheme we came from
				absolute = from.scheme + ":" + target;
			}
			else
			{
				String base = from.scheme + "://" + from.host;
				if (from.port != 0)
				{
					base += ":" + std::to_string(from.port);
				}
				absolute = target[0] == '/'
					? base + target
					: base + pathDirectory(from.path) + target;
			}
			const HttpFailure parsed = parseUrl(absolute, out, reason);
			if (parsed != HF_NONE)
			{
				return parsed;
			}
			// THE DOWNGRADE RULE: an https request never continues in the
			// clear, no matter what the caller opted into. A request that
			// STARTED as plain http may go either way (it was never secure).
			if (from.secure && !out.secure)
			{
				reason = "refusing the redirect from https to the plain-http "
					"URL '" + out.rebuild() + "' - a secure request never "
					"continues unencrypted";
				return HF_REDIRECT_REFUSED;
			}
			if (!out.secure && !allowInsecureHttp && !from.secure)
			{
				// unreachable for a validated request (an insecure start
				// implies the opt-in), kept explicit so the rule reads whole
				reason = "refusing the plain-http redirect target '" +
					out.rebuild() + "'";
				return HF_INSECURE_SCHEME;
			}
			reason.clear();
			return HF_NONE;
		}
		//---------------------------------------------------------
		void parseHeaderBlock(String const & block,
			std::map<String, String> & out)
		{
			size_t at = 0;
			while (at < block.size())
			{
				size_t lineEnd = block.find('\n', at);
				if (lineEnd == String::npos)
				{
					lineEnd = block.size();
				}
				String line = block.substr(at, lineEnd - at);
				at = lineEnd + 1;
				if (!line.empty() && line[line.size() - 1] == '\r')
				{
					line.erase(line.size() - 1);
				}
				if (line.empty())
				{
					continue;
				}
				const size_t colon = line.find(':');
				if (colon == String::npos)
				{
					continue;	// the status line and any junk are skipped
				}
				const String name = toLowerAscii(
					trimSpaces(line.substr(0, colon)));
				const String value = trimSpaces(line.substr(colon + 1));
				if (name.empty())
				{
					continue;
				}
				// a repeated header folds into one comma-joined value (the
				// HTTP list rule; Set-Cookie is the documented exception we
				// do not need - the client keeps no cookie jar)
				std::map<String, String>::iterator existing = out.find(name);
				if (existing != out.end())
				{
					existing->second += ", " + value;
				}
				else
				{
					out[name] = value;
				}
			}
		}
	}
}
