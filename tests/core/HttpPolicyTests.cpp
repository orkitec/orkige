/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpPolicyTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_http/HttpPolicy.h>

#include <map>

using Orkige::HF_BAD_HEADER;
using Orkige::HF_BAD_METHOD;
using Orkige::HF_BAD_URL;
using Orkige::HF_CREDENTIALS_IN_URL;
using Orkige::HF_INSECURE_SCHEME;
using Orkige::HF_NONE;
using Orkige::HF_REDIRECT_REFUSED;
using Orkige::HF_UNSUPPORTED_SCHEME;
using Orkige::HttpClientRequest;
using Orkige::HttpFailure;
using Orkige::HttpUrlParts;
using Orkige::String;
namespace HttpPolicy = Orkige::HttpPolicy;

namespace
{
	//! parse a URL, asserting it was accepted
	HttpUrlParts parsed(String const & url)
	{
		HttpUrlParts parts;
		String reason;
		REQUIRE(HttpPolicy::parseUrl(url, parts, reason) == HF_NONE);
		return parts;
	}
	//! the verdict parseUrl reaches for a URL
	HttpFailure verdict(String const & url)
	{
		HttpUrlParts parts;
		String reason;
		const HttpFailure failure = HttpPolicy::parseUrl(url, parts, reason);
		if (failure != HF_NONE)
		{
			// every refusal explains itself - a silent one would be the bug
			CHECK_FALSE(reason.empty());
		}
		return failure;
	}
}

TEST_CASE("HttpPolicy splits absolute URLs into their parts", "[http]")
{
	HttpUrlParts simple = parsed("https://api.example.com/scores?top=10");
	CHECK(simple.scheme == "https");
	CHECK(simple.host == "api.example.com");
	CHECK(simple.path == "/scores?top=10");
	CHECK(simple.port == 0);
	CHECK(simple.effectivePort() == 443);
	CHECK(simple.secure);

	HttpUrlParts ported = parsed("http://127.0.0.1:8080/health");
	CHECK(ported.scheme == "http");
	CHECK(ported.host == "127.0.0.1");
	CHECK(ported.port == 8080);
	CHECK(ported.effectivePort() == 8080);
	CHECK_FALSE(ported.secure);

	// a missing path is the root, and the scheme folds to lower case
	HttpUrlParts bare = parsed("HTTPS://Example.COM");
	CHECK(bare.scheme == "https");
	CHECK(bare.path == "/");
	CHECK(bare.rebuild() == "https://Example.COM/");

	// a query with no path still leaves a rooted path
	CHECK(parsed("https://example.com?q=1").path == "/?q=1");

	// an IPv6 literal keeps its brackets out of the host
	HttpUrlParts sixth = parsed("https://[::1]:9000/x");
	CHECK(sixth.host == "::1");
	CHECK(sixth.port == 9000);
}

TEST_CASE("HttpPolicy refuses URLs it cannot serve, by name", "[http]")
{
	CHECK(verdict("") == HF_BAD_URL);
	CHECK(verdict("example.com/x") == HF_BAD_URL);			// not absolute
	CHECK(verdict("https://") == HF_BAD_URL);				// no host
	CHECK(verdict("https://example.com:/x") == HF_BAD_URL);	// empty port
	CHECK(verdict("https://example.com:70000/") == HF_BAD_URL);
	CHECK(verdict("https://example.com:80x/") == HF_BAD_URL);
	CHECK(verdict("https://exa mple.com/") == HF_BAD_URL);
	CHECK(verdict("https://[::1/x") == HF_BAD_URL);			// unclosed literal
	// no protocol beyond http/https exists for this client
	CHECK(verdict("ftp://example.com/f") == HF_UNSUPPORTED_SCHEME);
	CHECK(verdict("file:///etc/passwd") == HF_UNSUPPORTED_SCHEME);
	// credentials in a URL leak into logs and proxies - an Authorization
	// header is the way, and the refusal says so
	CHECK(verdict("https://user:pass@example.com/x") == HF_CREDENTIALS_IN_URL);
}

TEST_CASE("HttpPolicy makes https the default and http an explicit choice",
	"[http][security]")
{
	HttpClientRequest request;
	HttpUrlParts parts;
	String reason;

	request.url = "http://127.0.0.1:8080/health";
	CHECK(HttpPolicy::validate(request, parts, reason) == HF_INSECURE_SCHEME);
	CHECK(reason.find("allowInsecureHttp") != String::npos);

	// the opt-in is explicit, visible and per-request
	request.allowInsecureHttp = true;
	CHECK(HttpPolicy::validate(request, parts, reason) == HF_NONE);

	// https never needs it
	HttpClientRequest secure;
	secure.url = "https://api.example.com/v1";
	CHECK(HttpPolicy::validate(secure, parts, reason) == HF_NONE);
}

TEST_CASE("HttpPolicy rejects header and method injection", "[http][security]")
{
	HttpClientRequest request;
	request.url = "https://api.example.com/v1";
	HttpUrlParts parts;
	String reason;

	// a CR or LF in a header value would SPLIT the request into a second one
	request.headers.push_back(std::make_pair(String("X-Token"),
		String("abc\r\nX-Admin: 1")));
	CHECK(HttpPolicy::validate(request, parts, reason) == HF_BAD_HEADER);
	request.headers.clear();
	request.headers.push_back(std::make_pair(String("X-Token"),
		String("line\nbreak")));
	CHECK(HttpPolicy::validate(request, parts, reason) == HF_BAD_HEADER);
	request.headers.clear();

	// a header name must be a token
	request.headers.push_back(std::make_pair(String("Bad Name"), String("v")));
	CHECK(HttpPolicy::validate(request, parts, reason) == HF_BAD_HEADER);
	request.headers.clear();
	request.headers.push_back(std::make_pair(String(), String("v")));
	CHECK(HttpPolicy::validate(request, parts, reason) == HF_BAD_HEADER);
	request.headers.clear();

	// a valid pair passes
	request.headers.push_back(std::make_pair(String("Authorization"),
		String("Bearer abc.def")));
	CHECK(HttpPolicy::validate(request, parts, reason) == HF_NONE);

	// the method is a token too
	request.method = "GET HTTP/1.1";
	CHECK(HttpPolicy::validate(request, parts, reason) == HF_BAD_METHOD);
	request.method = "";
	CHECK(HttpPolicy::validate(request, parts, reason) == HF_BAD_METHOD);
	request.method = "PATCH";
	CHECK(HttpPolicy::validate(request, parts, reason) == HF_NONE);
}

TEST_CASE("HttpPolicy resolves redirects and never downgrades out of https",
	"[http][security]")
{
	const HttpUrlParts from = parsed("https://a.example.com/one/two");
	HttpUrlParts next;
	String reason;

	// absolute
	REQUIRE(HttpPolicy::resolveRedirect(from, "https://b.example.com/x", false,
		next, reason) == HF_NONE);
	CHECK(next.host == "b.example.com");
	CHECK(next.path == "/x");
	// absolute path against the same host
	REQUIRE(HttpPolicy::resolveRedirect(from, "/root", false, next, reason)
		== HF_NONE);
	CHECK(next.host == "a.example.com");
	CHECK(next.path == "/root");
	// relative, against the directory of the current path
	REQUIRE(HttpPolicy::resolveRedirect(from, "three", false, next, reason)
		== HF_NONE);
	CHECK(next.path == "/one/three");
	// scheme-relative keeps the scheme we came in on
	REQUIRE(HttpPolicy::resolveRedirect(from, "//c.example.com/y", false, next,
		reason) == HF_NONE);
	CHECK(next.scheme == "https");
	CHECK(next.host == "c.example.com");

	// THE downgrade rule: an https request never continues in the clear, and
	// the caller's insecure opt-in does NOT buy an exception
	CHECK(HttpPolicy::resolveRedirect(from, "http://b.example.com/x", false,
		next, reason) == HF_REDIRECT_REFUSED);
	CHECK(HttpPolicy::resolveRedirect(from, "http://b.example.com/x", true,
		next, reason) == HF_REDIRECT_REFUSED);
	CHECK(reason.find("https") != String::npos);

	// another protocol is refused as such
	CHECK(HttpPolicy::resolveRedirect(from, "ftp://b.example.com/x", true, next,
		reason) == HF_UNSUPPORTED_SCHEME);
	// an empty Location is a refusal, not a silent stall
	CHECK(HttpPolicy::resolveRedirect(from, "   ", false, next, reason)
		== HF_REDIRECT_REFUSED);

	// a request that STARTED as plain http may be redirected either way - it
	// was never secure, so there is nothing to downgrade
	const HttpUrlParts plain = parsed("http://127.0.0.1:9000/a");
	CHECK(HttpPolicy::resolveRedirect(plain, "http://127.0.0.1:9000/b", true,
		next, reason) == HF_NONE);
	CHECK(HttpPolicy::resolveRedirect(plain, "https://secure.example.com/b",
		true, next, reason) == HF_NONE);
}

TEST_CASE("HttpPolicy parses a response header block case-insensitively",
	"[http]")
{
	std::map<String, String> headers;
	HttpPolicy::parseHeaderBlock(
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/json\r\n"
		"content-length: 12\r\n"
		"X-Trailing:   spaced value   \r\n"
		"Set-Cookie: a=1\r\n"
		"Set-Cookie: b=2\r\n"
		"junk-without-colon\r\n"
		"\r\n", headers);
	CHECK(headers["content-type"] == "application/json");
	CHECK(headers["content-length"] == "12");
	CHECK(headers["x-trailing"] == "spaced value");
	// a repeated header folds into one comma-joined value (the HTTP list rule)
	CHECK(headers["set-cookie"] == "a=1, b=2");
	// the status line and malformed lines contribute nothing
	CHECK(headers.find("http/1.1") == headers.end());
	CHECK(headers.size() == 4);
}

TEST_CASE("HttpPolicy names every failure with a stable token", "[http]")
{
	CHECK(Orkige::httpFailureName(Orkige::HF_NONE) == "none");
	CHECK(Orkige::httpFailureName(Orkige::HF_TIMEOUT) == "timeout");
	CHECK(Orkige::httpFailureName(Orkige::HF_TLS_FAILED) == "tls-failed");
	CHECK(Orkige::httpFailureName(Orkige::HF_TOO_LARGE) == "too-large");
	CHECK(Orkige::httpFailureName(Orkige::HF_CANCELLED) == "cancelled");
	CHECK(Orkige::httpFailureName(Orkige::HF_UNAVAILABLE) == "unavailable");
	CHECK(Orkige::httpFailureName(Orkige::HF_INSECURE_SCHEME) ==
		"insecure-scheme");
}

TEST_CASE("HttpPolicy words a size-cap refusal the same on every backend",
	"[http]")
{
	// a cap hit by the bytes arriving names the limit
	const String arriving = HttpPolicy::sizeCapReason(4096);
	CHECK(arriving.find("cap") != String::npos);
	CHECK(arriving.find("4096") != String::npos);
	// a cap hit by the ANNOUNCED size names both numbers, so a caller can
	// tell "it was refused before a byte arrived" from "it grew past it"
	const String announced = HttpPolicy::sizeCapReason(4096, 262144);
	CHECK(announced.find("cap") != String::npos);
	CHECK(announced.find("4096") != String::npos);
	CHECK(announced.find("262144") != String::npos);
	CHECK(announced != arriving);
}
