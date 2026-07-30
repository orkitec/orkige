/********************************************************************
	created:	Thursday 2026/07/30 at 18:00
	filename: 	VersionOrder.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// @see VersionOrder.h - the ordered build identity: its grammar, its
// precedence rules and the composition every surface derives from.
#include "core_util/VersionOrder.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Orkige
{
	namespace
	{
		bool isDigits(String const & text)
		{
			if (text.empty())
			{
				return false;
			}
			for (std::size_t index = 0; index < text.size(); ++index)
			{
				if (text[index] < '0' || text[index] > '9')
				{
					return false;
				}
			}
			return true;
		}

		//! a numeric identifier: digits with no leading zero (the semantic
		//! versioning rule - "01" is not a number here, it is malformed)
		bool isNumericIdentifier(String const & text)
		{
			if (!isDigits(text))
			{
				return false;
			}
			return text.size() == 1 || text[0] != '0';
		}

		//! prerelease identifiers are alphanumerics and hyphens; anything else
		//! (a space, a bracket - what "2.0.0 (local build)" carries) is not a
		//! version at all
		bool isIdentifier(String const & text)
		{
			if (text.empty())
			{
				return false;
			}
			for (std::size_t index = 0; index < text.size(); ++index)
			{
				const char letter = text[index];
				const bool allowed = (letter >= '0' && letter <= '9') ||
					(letter >= 'a' && letter <= 'z') ||
					(letter >= 'A' && letter <= 'Z') || letter == '-';
				if (!allowed)
				{
					return false;
				}
			}
			return true;
		}

		//! build metadata allows dots as well (it is never ordered, only carried)
		bool isMetadata(String const & text)
		{
			if (text.empty())
			{
				return false;
			}
			String::size_type start = 0;
			while (start <= text.size())
			{
				const String::size_type dot = text.find('.', start);
				const String part = text.substr(start,
					dot == String::npos ? String::npos : dot - start);
				if (!isIdentifier(part))
				{
					return false;
				}
				if (dot == String::npos)
				{
					break;
				}
				start = dot + 1;
			}
			return true;
		}

		//! digits to a number, refusing anything that would not fit (a version
		//! nobody can represent is not comparable, and must not wrap)
		bool toNumber(String const & text, unsigned long & outValue)
		{
			if (!isNumericIdentifier(text))
			{
				return false;
			}
			unsigned long value = 0;
			for (std::size_t index = 0; index < text.size(); ++index)
			{
				const unsigned long digit =
					static_cast<unsigned long>(text[index] - '0');
				if (value > (0xFFFFFFFFul - digit) / 10ul)
				{
					return false;
				}
				value = value * 10ul + digit;
			}
			outValue = value;
			return true;
		}

		StringVector splitDots(String const & text)
		{
			StringVector parts;
			String::size_type start = 0;
			while (true)
			{
				const String::size_type dot = text.find('.', start);
				if (dot == String::npos)
				{
					parts.push_back(text.substr(start));
					break;
				}
				parts.push_back(text.substr(start, dot - start));
				start = dot + 1;
			}
			return parts;
		}

		//! one prerelease identifier against another, by the semantic
		//! versioning rules: numbers compare numerically and rank BELOW
		//! alphanumerics, alphanumerics compare by ASCII order
		int compareIdentifier(String const & left, String const & right)
		{
			const bool leftNumeric = isNumericIdentifier(left);
			const bool rightNumeric = isNumericIdentifier(right);
			if (leftNumeric && rightNumeric)
			{
				unsigned long leftValue = 0;
				unsigned long rightValue = 0;
				const bool leftOk = toNumber(left, leftValue);
				const bool rightOk = toNumber(right, rightValue);
				if (leftOk && rightOk)
				{
					if (leftValue == rightValue)
					{
						return 0;
					}
					return leftValue < rightValue ? -1 : 1;
				}
				// an identifier too large to represent falls back to text
				// order rather than pretending to a numeric answer
			}
			else if (leftNumeric != rightNumeric)
			{
				return leftNumeric ? -1 : 1;
			}
			if (left == right)
			{
				return 0;
			}
			return left < right ? -1 : 1;
		}

		//! the prerelease sets of two equal base versions. An EMPTY set is a
		//! release and outranks any prerelease.
		int comparePrerelease(StringVector const & left,
			StringVector const & right)
		{
			if (left.empty() || right.empty())
			{
				if (left.empty() && right.empty())
				{
					return 0;
				}
				return left.empty() ? 1 : -1;
			}
			const std::size_t shared =
				left.size() < right.size() ? left.size() : right.size();
			for (std::size_t index = 0; index < shared; ++index)
			{
				const int verdict = compareIdentifier(left[index], right[index]);
				if (verdict != 0)
				{
					return verdict;
				}
			}
			if (left.size() == right.size())
			{
				return 0;
			}
			// all shared identifiers equal: the longer set follows
			return left.size() < right.size() ? -1 : 1;
		}
	}

	//---------------------------------------------------------
	bool VersionOrder::parse(String const & text, Version & outVersion)
	{
		String rest = text;
		if (!rest.empty() && (rest[0] == 'v' || rest[0] == 'V'))
		{
			rest.erase(0, 1);
		}
		Version parsed;

		// build metadata: the canonical "+" or the filename rendering "_"
		// (@see filenameToken). Whichever comes first wins; the other
		// character may not appear at all, so a mixed string is refused.
		const String::size_type plus = rest.find('+');
		const String::size_type underscore = rest.find('_');
		String::size_type metaAt = String::npos;
		if (plus != String::npos && underscore != String::npos)
		{
			return false;
		}
		metaAt = plus != String::npos ? plus : underscore;
		if (metaAt != String::npos)
		{
			parsed.mBuild = rest.substr(metaAt + 1);
			if (!isMetadata(parsed.mBuild))
			{
				return false;
			}
			rest = rest.substr(0, metaAt);
		}

		// prerelease: everything after the first hyphen following the base
		const String::size_type hyphen = rest.find('-');
		if (hyphen != String::npos)
		{
			const String prerelease = rest.substr(hyphen + 1);
			rest = rest.substr(0, hyphen);
			parsed.mPrerelease = splitDots(prerelease);
			for (std::size_t index = 0; index < parsed.mPrerelease.size();
				++index)
			{
				const String & identifier = parsed.mPrerelease[index];
				if (!isIdentifier(identifier))
				{
					return false;
				}
				if (isDigits(identifier) && !isNumericIdentifier(identifier))
				{
					return false;	// a leading-zero number is malformed
				}
			}
		}

		// the base: exactly three numeric fields
		const StringVector base = splitDots(rest);
		if (base.size() != 3)
		{
			return false;
		}
		if (!toNumber(base[0], parsed.mMajor) ||
			!toNumber(base[1], parsed.mMinor) ||
			!toNumber(base[2], parsed.mPatch))
		{
			return false;
		}
		outVersion = parsed;
		return true;
	}

	//---------------------------------------------------------
	VersionOrder::Order VersionOrder::compare(String const & left,
		String const & right)
	{
		Version leftVersion;
		Version rightVersion;
		if (!parse(left, leftVersion) || !parse(right, rightVersion))
		{
			return VO_INCOMPARABLE;
		}
		const unsigned long leftBase[3] = { leftVersion.mMajor,
			leftVersion.mMinor, leftVersion.mPatch };
		const unsigned long rightBase[3] = { rightVersion.mMajor,
			rightVersion.mMinor, rightVersion.mPatch };
		for (std::size_t index = 0; index < 3; ++index)
		{
			if (leftBase[index] != rightBase[index])
			{
				return leftBase[index] < rightBase[index] ? VO_OLDER : VO_NEWER;
			}
		}
		const int verdict = comparePrerelease(leftVersion.mPrerelease,
			rightVersion.mPrerelease);
		if (verdict == 0)
		{
			// build metadata is carried, never ordered: same date, different
			// commit is the SAME version and never an update
			return VO_SAME;
		}
		return verdict < 0 ? VO_OLDER : VO_NEWER;
	}

	//---------------------------------------------------------
	bool VersionOrder::isUpdate(String const & candidate, String const & current)
	{
		return compare(candidate, current) == VO_NEWER;
	}

	//---------------------------------------------------------
	String VersionOrder::compose(String const & base, String const & date,
		String const & commit)
	{
		// the base must itself be a version (the engine declares it once, in
		// the root CMakeLists project() call)
		Version parsedBase;
		if (!parse(base, parsedBase) || !parsedBase.mPrerelease.empty() ||
			!parsedBase.mBuild.empty())
		{
			return "";
		}
		String compact;
		for (std::size_t index = 0; index < date.size(); ++index)
		{
			if (date[index] != '-')
			{
				compact += date[index];
			}
		}
		if (compact.size() != 8 || !isDigits(compact))
		{
			return "";
		}
		String identity = base + "-nightly." + compact;
		if (!commit.empty())
		{
			if (!isMetadata(commit))
			{
				return "";
			}
			identity += "+" + commit;
		}
		return identity;
	}

	//---------------------------------------------------------
	String VersionOrder::commitOf(String const & text)
	{
		Version parsed;
		if (!parse(text, parsed))
		{
			return "";
		}
		return parsed.mBuild;
	}

	//---------------------------------------------------------
	String VersionOrder::filenameToken(String const & text)
	{
		Version parsed;
		if (!parse(text, parsed))
		{
			return "";
		}
		String token;
		for (std::size_t index = 0; index < text.size(); ++index)
		{
			token += text[index] == '+' ? '_' : text[index];
		}
		return token;
	}
}
