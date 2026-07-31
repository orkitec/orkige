/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportFiles.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportFiles_h__31_7_2026__12_00_00__
#define __ExportFiles_h__31_7_2026__12_00_00__

#include <core_util/String.h>

#include <vector>

//! @file ExportFiles.h
//! @brief the filesystem moves an export makes: copy a tree, remove one, count
//! what a bundle weighs, and the two attributes a packaged file needs (the
//! executable bit and a dylib's loader symlink).
//!
//! Thin, honest wrappers over std::filesystem: each reports its own failure
//! with the path in it rather than throwing, because an export that cannot
//! stage a file has to say which one. Lives in tools/ - the filesystem funnel
//! doctrine gates orkige_core and orkige_engine, and a host packaging tool is
//! neither.

namespace OrkigeExport
{
	//! @brief the file operations an export performs (@see ExportFiles.h)
	class ExportFiles
	{
	public:
		static bool exists(Orkige::String const & path);
		static bool isDirectory(Orkige::String const & path);
		static bool isRegularFile(Orkige::String const & path);

		//! @brief create @p path and every missing parent. True when it
		//! already is a directory.
		static bool makeDirectories(Orkige::String const & path,
			Orkige::String * error);

		//! @brief remove @p path and everything under it. True when it was
		//! already absent.
		static bool removeTree(Orkige::String const & path,
			Orkige::String * error);

		//! @brief copy one file, creating the destination's parent
		//! directories. An existing destination is overwritten.
		static bool copyFile(Orkige::String const & source,
			Orkige::String const & destination, Orkige::String * error);

		//! @brief copy a whole directory tree into @p destination, merging
		//! into an existing one. Symlinks are copied AS symlinks (a macOS
		//! bundle's Versions/Current and a dylib's loader aliases must not
		//! become file copies). @p outFileCount, when given, receives the
		//! number of regular files staged.
		static bool copyTree(Orkige::String const & source,
			Orkige::String const & destination, Orkige::String * error,
			int * outFileCount = 0);

		//! @brief the number of regular files under @p path (1 for a file)
		static int countFiles(Orkige::String const & path);

		//! @brief total bytes of @p path (a file's size, or the sum of every
		//! non-symlink regular file beneath a directory)
		static unsigned long long treeSize(Orkige::String const & path);

		//! @brief make @p path owner-executable (the packaged player binary)
		static bool makeExecutable(Orkige::String const & path,
			Orkige::String * error);

		//! @brief create a symlink at @p linkPath pointing at @p target
		//! (relative, as a dylib's loader alias is). An existing entry at
		//! @p linkPath is replaced.
		static bool makeSymlink(Orkige::String const & target,
			Orkige::String const & linkPath, Orkige::String * error);

		//! @brief write @p text to @p path with LF line endings, creating the
		//! parent directories
		static bool writeTextFile(Orkige::String const & path,
			Orkige::String const & text, Orkige::String * error);

		//! @brief read @p path into @p out. False with an @p error when it
		//! cannot be read.
		static bool readTextFile(Orkige::String const & path,
			Orkige::String & out, Orkige::String * error);

		//! @brief write raw bytes (a cooked texture container, a packaged
		//! archive), creating the parent directories
		static bool writeBytes(Orkige::String const & path,
			std::vector<unsigned char> const & bytes, Orkige::String * error);

		//! @brief read @p path as raw bytes
		static bool readBytes(Orkige::String const & path,
			std::vector<unsigned char> & out, Orkige::String * error);

		//! @brief every regular file under @p root, as paths relative to it,
		//! sorted - the deterministic walk order a package's contents are
		//! written in
		static std::vector<Orkige::String> listFilesRecursive(
			Orkige::String const & root);

		//! @brief join two path fragments with the platform separator
		static Orkige::String join(Orkige::String const & left,
			Orkige::String const & right);

		//! @brief the absolute, normalised form of @p path
		static Orkige::String absolute(Orkige::String const & path);

		//! @brief the file name without its extension ("a/b/pose.svg" ->
		//! "pose")
		static Orkige::String stem(Orkige::String const & path);

		//! @brief the file name with its extension ("a/b/pose.svg" ->
		//! "pose.svg")
		static Orkige::String fileName(Orkige::String const & path);

		//! @brief @p path with its extension replaced by @p extension (given
		//! without the dot)
		static Orkige::String replaceExtension(Orkige::String const & path,
			Orkige::String const & extension);
	};
}

#endif //__ExportFiles_h__31_7_2026__12_00_00__
