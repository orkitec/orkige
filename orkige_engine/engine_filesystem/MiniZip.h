/********************************************************************
	created:	Friday 2026/07/18 at 06:00
	filename: 	MiniZip.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __MiniZip_h__18_7_2026__06_00_00__
#define __MiniZip_h__18_7_2026__06_00_00__

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Orkige
{
	//! @brief a small, dependency-light zip reader: parses a zip's central
	//! directory and reads (STORED verbatim, DEFLATE inflated via zlib) any
	//! entry by name.
	//! @remarks WHY this exists: classic OGRE ships a zziplib-backed Zip
	//! archive, but the Ogre-Next build carries NO zip support (no zziplib), so
	//! there is no stock zip reader to reuse on BOTH flavors. Rather than vendor
	//! a whole zip library or add a heavy port dependency, this wraps the zlib
	//! that is already in the closure (classic pulls it through zziplib,
	//! Ogre-Next depends on it directly) to read the two compression methods a
	//! game pak / an Android APK actually uses. It is renderer-free and pure
	//! (unit-tested headlessly - MiniZipTests); PakArchive adapts it to the Ogre
	//! Archive interface. The file is opened per read (seekable real path), so a
	//! large APK is never held whole in memory.
	class MiniZip
	{
		//--- Types -------------------------------------------------
	public:
		struct Entry
		{
			std::uint16_t	method;			//!< 0 = STORED, 8 = DEFLATE
			std::uint64_t	compressedSize;
			std::uint64_t	uncompressedSize;
			std::uint64_t	localHeaderOffset;
		};
		//--- Methods -----------------------------------------------
	public:
		MiniZip();
		~MiniZip();

		//! @brief open @p path and read its central directory (idempotent per
		//! instance). @return false when the file is missing or not a zip.
		bool open(std::string const & path);
		//! @brief was a zip successfully opened?
		bool isOpen() const { return this->mOpen; }
		//! @brief the path opened
		std::string const & path() const { return this->mPath; }

		//! @brief every entry name in the archive (full internal paths)
		std::vector<std::string> names() const;
		//! @brief the parsed entries (name -> record)
		std::map<std::string, Entry> const & entries() const { return this->mEntries; }
		//! @brief does @p name exist as an entry?
		bool contains(std::string const & name) const;
		//! @brief the uncompressed size of @p name; false when it is unknown
		bool sizeOf(std::string const & name,
			std::uint64_t & outUncompressedSize) const;

		//! @brief read @p name's bytes (STORED verbatim, DEFLATE inflated) into
		//! @p out. @return false when the entry is missing, the stored/inflated
		//! size disagrees with the directory, or an IO/inflate error occurs.
		bool read(std::string const & name,
			std::vector<unsigned char> & out) const;

	private:
		std::string						mPath;
		std::map<std::string, Entry>	mEntries;
		bool							mOpen;
	};
}

#endif //__MiniZip_h__18_7_2026__06_00_00__
