/**************************************************************
	created:	2026/07/30 at 10:00
	filename: 	HttpTypes.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_http/HttpTypes.h"

namespace Orkige
{
	//---------------------------------------------------------
	String HttpClientResponse::header(String const & lowerName) const
	{
		std::map<String, String>::const_iterator found =
			this->headers.find(lowerName);
		return found != this->headers.end() ? found->second : String();
	}
	//---------------------------------------------------------
	String const & httpFailureName(HttpFailure failure)
	{
		// stable, script-facing tokens (the Lua `error` field and the logs)
		static const String names[] =
		{
			"none",
			"unavailable",
			"bad-url",
			"unsupported-scheme",
			"insecure-scheme",
			"credentials-in-url",
			"bad-header",
			"bad-method",
			"bad-save-path",
			"connect-failed",
			"tls-failed",
			"timeout",
			"too-large",
			"redirect-refused",
			"write-failed",
			"cancelled",
			"transport"
		};
		const int index = static_cast<int>(failure);
		const int count = static_cast<int>(sizeof(names) / sizeof(names[0]));
		return (index >= 0 && index < count) ? names[index] : names[count - 1];
	}
}
