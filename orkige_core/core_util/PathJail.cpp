/********************************************************************
	created:	Sunday 2026/07/20 at 00:30
	filename: 	PathJail.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_util/PathJail.h"

namespace Orkige
{
	namespace PathJail
	{
		//---------------------------------------------------------
		bool isSafeRelativeEntry(std::string const & entry)
		{
			if(entry.empty())
			{
				return false;
			}
			// absolute (POSIX '/' or a stray '\\') roots are never relative
			if(entry[0] == '/' || entry[0] == '\\')
			{
				return false;
			}
			// a Windows drive root "C:..." (letter + ':') escapes the jail
			if(entry.size() >= 2 && entry[1] == ':' &&
				((entry[0] >= 'A' && entry[0] <= 'Z') ||
					(entry[0] >= 'a' && entry[0] <= 'z')))
			{
				return false;
			}
			// walk the segments (both '/' and '\\' count as separators, so a
			// Windows-style traversal is caught) and reject any ".." component
			std::string::size_type start = 0;
			for(std::string::size_type i = 0; i <= entry.size(); ++i)
			{
				if(i == entry.size() || entry[i] == '/' || entry[i] == '\\')
				{
					if(i - start == 2 &&
						entry[start] == '.' && entry[start + 1] == '.')
					{
						return false;
					}
					start = i + 1;
				}
			}
			return true;
		}
		//---------------------------------------------------------
		bool escapesRoot(std::filesystem::path const & base,
			std::filesystem::path const & target)
		{
			// an outside path relativises to a ".."-led (or empty) one. Compare
			// the first COMPONENT as a path, not a string prefix, so a sibling
			// literally named "..foo" is not mistaken for an escape.
			const std::filesystem::path relative = target.lexically_relative(base);
			return relative.empty() ||
				(relative.begin() != relative.end() &&
					*relative.begin() == std::filesystem::path(".."));
		}
		//---------------------------------------------------------
		bool resolveExtractPath(std::filesystem::path const & root,
			std::string const & entry, std::filesystem::path & outDest)
		{
			// lexical gate first: a ".."/absolute/drive entry never gets to touch
			// the filesystem
			if(!isSafeRelativeEntry(entry))
			{
				return false;
			}
			namespace fs = std::filesystem;
			const fs::path rootNormal = (root / "").lexically_normal();
			const fs::path dest = (root / entry).lexically_normal();
			// lexical containment
			if(escapesRoot(rootNormal, dest))
			{
				return false;
			}
			// symlink containment: resolve symlinks over the existing prefix of
			// both paths and re-check (a component symlinked outside the root
			// passes the lexical test but must fail here)
			std::error_code ec;
			const fs::path canonicalRoot = fs::weakly_canonical(rootNormal, ec);
			const fs::path canonicalDest = fs::weakly_canonical(dest, ec);
			if(!canonicalRoot.empty() && !canonicalDest.empty() &&
				escapesRoot(canonicalRoot, canonicalDest))
			{
				return false;
			}
			outDest = dest;
			return true;
		}
	}
}
