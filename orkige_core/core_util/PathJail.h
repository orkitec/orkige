/********************************************************************
	created:	Sunday 2026/07/20 at 00:30
	filename: 	PathJail.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __PathJail_h__20_7_2026__00_30_00__
#define __PathJail_h__20_7_2026__00_30_00__

#include <filesystem>
#include <string>

namespace Orkige
{
	//! @brief the ONE path-containment primitive: keeps an untrusted path
	//! (a zip/pak entry name, an MCP-authored project path, an asset copied off
	//! the web) from resolving or extracting OUTSIDE its intended root.
	//! @remarks Two classic escapes it stops: zip-slip - an archive entry named
	//! "../../evil" or "/etc/evil" that a naive extractor would write outside the
	//! extraction root - and path traversal in a project-file jail. The lexical
	//! predicates here are PURE (no filesystem access, headless-unit-tested); the
	//! extract-path resolver adds a symlink check by canonicalising the target.
	//! The full threat model is in Docs/filesystem.md (Security).
	namespace PathJail
	{
		//! @brief is @p entry safe to resolve/extract under a root? A pure
		//! lexical predicate over an archive entry name (or any untrusted
		//! relative path). Rejects an empty name, an ABSOLUTE path (leading '/'
		//! or '\\'), a DRIVE/UNC root ("C:...", "\\\\server"), and ANY ".."
		//! traversal segment (splitting on both '/' and '\\', so a
		//! Windows-style "..\\..\\evil" is caught too). A legitimate nested name
		//! ("assets/textures/foo.png") passes; a bare ".." segment fails while a
		//! filename that merely STARTS with dots ("..foo", ".hidden") passes.
		bool isSafeRelativeEntry(std::string const & entry);

		//! @brief does @p target escape @p base? A pure containment test on two
		//! already-normalised paths: true when target's form relative to base is
		//! empty or begins with a ".." component. This is the shared containment
		//! decision behind the MCP project-file jail and AssetDatabase's
		//! inside-root resolver (an outside path relativises to a "../"-led one).
		bool escapesRoot(std::filesystem::path const & base,
			std::filesystem::path const & target);

		//! @brief resolve the on-disk destination for extracting @p entry under
		//! @p root, refusing any escape. Fails when @p entry is not a safe
		//! relative name (isSafeRelativeEntry) OR when the destination - after
		//! joining and resolving symlinks over the existing prefix (weakly
		//! canonical) - would land outside @p root. @p root need not exist yet.
		//! On success @p outDest is the path to write. This is the guard the
		//! extract-to-disk boundary calls BEFORE any write, so a hostile entry
		//! can never be written through a symlink out of the extraction root.
		//! (Touches the filesystem for the symlink check; the lexical core is
		//! the two pure predicates above.)
		bool resolveExtractPath(std::filesystem::path const & root,
			std::string const & entry, std::filesystem::path & outDest);
	}
}

#endif //__PathJail_h__20_7_2026__00_30_00__
