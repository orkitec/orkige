/**************************************************************
	created:	2026/07/24 at 12:00
	filename: 	EditorTextDiagnostics.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "EditorTextDiagnostics.h"

#include <tinyxml2.h>

#include <cctype>
#include <cstdlib>

namespace OrkigeEditor
{
	TextDiagnostic xmlDiagnostic(std::string const& text)
	{
		TextDiagnostic diagnostic;
		// an empty/whitespace-only document is not an error to shout about
		// while someone is still typing
		bool blank = true;
		for (const char c : text)
		{
			if (std::isspace(static_cast<unsigned char>(c)) == 0)
			{
				blank = false;
				break;
			}
		}
		if (blank)
		{
			return diagnostic;
		}
		tinyxml2::XMLDocument document;
		if (document.Parse(text.c_str(), text.size()) != tinyxml2::XML_SUCCESS)
		{
			diagnostic.valid = false;
			diagnostic.line = document.ErrorLineNum();	// tinyxml2 is 1-based
			diagnostic.message =
				document.ErrorStr() != nullptr ? document.ErrorStr() : "";
		}
		return diagnostic;
	}

	int luaErrorLine(std::string const& error, std::string const& chunkName)
	{
		// the loader prefixes errors with the chunk name: "name:line: message".
		// The name may appear more than once (wrapped in loader decoration) -
		// take the first occurrence that is IMMEDIATELY followed by ":<int>:"
		if (chunkName.empty())
		{
			return 0;
		}
		std::size_t name = error.find(chunkName);
		while (name != std::string::npos)
		{
			const std::size_t colon = name + chunkName.size();
			if (colon < error.size() && error[colon] == ':')
			{
				const char* digits = error.c_str() + colon + 1;
				char* end = nullptr;
				const long line = std::strtol(digits, &end, 10);
				if (end != digits && end != nullptr && *end == ':' && line > 0)
				{
					return static_cast<int>(line);
				}
			}
			name = error.find(chunkName, name + 1);
		}
		return 0;
	}
}
