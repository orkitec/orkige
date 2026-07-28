/**************************************************************
	created:	2026/07/24 at 20:00
	filename: 	ProjectPaths.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ProjectPaths_h__24_7_2026__20_00_00__
#define __ProjectPaths_h__24_7_2026__20_00_00__

//! @file ProjectPaths.h
//! @brief the ONE reserved-output-dirs policy: which project directories are
//! generated OUTPUT / editor-private and must NEVER be scanned, indexed,
//! watched or browsed. The exporter writes `<project>/builds/<platform>/`
//! which CONTAINS COPIES of the project's assets, and native modules build
//! under `native/build*/`; walking those causes duplicate indexing, sidecar
//! churn and phantom browser entries. Every project-tree walker (the
//! AssetDatabase scan, the asset browser tree/listing/search, the script /
//! `.oui` / animation watchers, the MCP file-listing verbs) funnels its
//! directory-pruning decision through here so the exclusion is defined once.
//! Pure path logic (header-only) so it links everywhere without a dependency.

#include <filesystem>
#include <string>

namespace Orkige
{
	namespace ProjectPaths
	{
		//! @brief is a directory (by its own name + its immediate parent's name)
		//! a reserved OUTPUT / editor-private directory? A recursive walker knows
		//! both while iterating, so it can `disable_recursion_pending()` on a hit
		//! WITHOUT needing the project root. Reserved:
		//!   - `builds`  (exporter output; copies of the project payload live here)
		//!   - `.orkige` (editor-private state: breadcrumbs, breakpoints, ...)
		//!   - `build*`  whose parent is `native` (native-module build trees:
		//!     `native/build-<flavor>`, `native/build-export-<flavor>`)
		//!   - `.git` / `.svn` (VCS metadata - never project content)
		inline bool isReservedOutputDir(std::string const & dirName,
			std::string const & parentDirName)
		{
			if(dirName == "builds" || dirName == ".orkige" ||
				dirName == ".git" || dirName == ".svn")
			{
				return true;
			}
			if(parentDirName == "native" && dirName.rfind("build", 0) == 0)
			{
				return true;
			}
			return false;
		}

		//! @brief the same decision from an absolute directory path (derives the
		//! name + parent name). Use this on a `directory_iterator` entry that is a
		//! directory before descending / listing it.
		inline bool isReservedOutputDir(std::filesystem::path const & directory)
		{
			return isReservedOutputDir(directory.filename().string(),
				directory.parent_path().filename().string());
		}

		//! @brief is a FILE (by its own name) a reserved editor-private file that
		//! must never surface in a project file listing? Currently the project-
		//! scope MCP discovery file `.mcp.json` (the editor writes it at the
		//! project root while its MCP endpoint is live so a `claude` session in
		//! the project directory auto-discovers the editor; it is generated
		//! editor state, not project content - gitignored, never exported).
		inline bool isReservedProjectFile(std::string const & fileName)
		{
			return fileName == ".mcp.json";
		}

		//! @brief is a project-RELATIVE path inside a reserved output directory?
		//! (walks the path's components, so `builds/macos/assets/foo.png` and
		//! `native/build-next/x.o` are caught). For listing/browsing a rel path.
		inline bool isUnderReservedOutput(std::string const & projectRelPath)
		{
			std::filesystem::path rel(projectRelPath);
			std::string parent;
			for(std::filesystem::path const & part : rel)
			{
				const std::string name = part.string();
				if(isReservedOutputDir(name, parent))
				{
					return true;
				}
				parent = name;
			}
			return false;
		}
	}
}

#endif //__ProjectPaths_h__24_7_2026__20_00_00__
