// EditorLabelFormat - see header. Pure string transforms, no dependencies.
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorLabelFormat.h"

#include <cctype>

namespace Orkige
{
	namespace
	{
		bool isUpperAscii(char c)
		{
			return c >= 'A' && c <= 'Z';
		}
		bool isLowerAscii(char c)
		{
			return c >= 'a' && c <= 'z';
		}
		bool isDigitAscii(char c)
		{
			return c >= '0' && c <= '9';
		}
		char toUpperAscii(char c)
		{
			return isLowerAscii(c) ? static_cast<char>(c - 'a' + 'A') : c;
		}

		//! insert spaces at camelCase / acronym / digit boundaries, then
		//! capitalise the first letter of every resulting word
		std::string spaceAndTitleCase(std::string const& name)
		{
			std::string out;
			out.reserve(name.size() + 4);
			const std::size_t n = name.size();
			for (std::size_t i = 0; i < n; ++i)
			{
				const char c = name[i];
				if (i > 0)
				{
					const char prev = name[i - 1];
					// lower/digit -> Upper : the "castShadows" boundary
					const bool lowerToUpper =
						isUpperAscii(c) &&
						(isLowerAscii(prev) || isDigitAscii(prev));
					// Upper -> Upper followed by lower : end of an acronym run,
					// e.g. "GLSLProgram" -> "GLSL Program"
					const bool acronymEnd =
						isUpperAscii(c) && isUpperAscii(prev) &&
						i + 1 < n && isLowerAscii(name[i + 1]);
					// letter -> digit : "uv0" -> "Uv 0"
					const bool letterToDigit =
						isDigitAscii(c) &&
						(isLowerAscii(prev) || isUpperAscii(prev));
					if (lowerToUpper || acronymEnd || letterToDigit)
					{
						out.push_back(' ');
					}
				}
				out.push_back(c);
			}
			// capitalise the first letter of the string and any letter after a
			// space (subsequent words already start upper from the split, but
			// the first word - "position", "x" - needs it)
			bool wordStart = true;
			for (char& c : out)
			{
				if (c == ' ')
				{
					wordStart = true;
					continue;
				}
				if (wordStart)
				{
					c = toUpperAscii(c);
					wordStart = false;
				}
			}
			return out;
		}
	}

	//---------------------------------------------------------
	std::string prettifyPropertyLabel(std::string const& name)
	{
		if (name.empty())
		{
			return name;
		}
		return spaceAndTitleCase(name);
	}
	//---------------------------------------------------------
	std::string prettifyComponentTitle(std::string const& typeName)
	{
		static const std::string suffix = "Component";
		std::string base = typeName;
		if (base.size() > suffix.size() &&
			base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0)
		{
			base.erase(base.size() - suffix.size());
		}
		if (base.empty())
		{
			return typeName; // a type literally named "Component": keep it
		}
		return spaceAndTitleCase(base);
	}
}
