/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpBackendNone.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// The transport for a build made WITHOUT an HTTP client (ORKIGE_HTTP=OFF): the
// seam still compiles and every call site still builds, HttpClient::available()
// answers false, and a submitted request is refused with a reason that names the
// build option rather than failing silently or looking like a network problem.
// The size-constrained-target lever (@see Docs/http.md) and the honest-refusal
// path a test can assert are the same code.

#include "core_http/HttpBackend.h"

#ifdef ORKIGE_HTTP_NONE

#include <vector>

namespace Orkige
{
	namespace
	{
		//! @brief the no-transport backend: start() fails, so HttpClient never
		//! reaches submit() and answers every request with HF_UNAVAILABLE.
		class NoHttpBackend : public HttpBackend
		{
		public:
			bool start() override { return false; }
			void stop() override {}
			void submit(HttpRequestId, HttpClientRequest const &,
				HttpUrlParts const &) override {}
			void cancel(HttpRequestId) override {}
			void poll(std::vector<HttpBackendEvent> & out) override
			{
				out.clear();
			}
			char const * name() const override { return "none"; }
		};
	}
	//---------------------------------------------------------
	HttpBackend * createHttpBackend()
	{
		return new NoHttpBackend();
	}
}

#endif // ORKIGE_HTTP_NONE
