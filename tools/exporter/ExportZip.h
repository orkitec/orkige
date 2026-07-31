/********************************************************************
	created:	Saturday 2026/08/01 at 10:00
	filename: 	ExportZip.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportZip_h__1_8_2026__10_00_00__
#define __ExportZip_h__1_8_2026__10_00_00__

#include <core_util/String.h>

#include <vector>

//! @file ExportZip.h
//! @brief the zip WRITER an export packages archives with - the write sibling
//! of the engine's `MiniZip` reader, over the same zlib.
//!
//! An export produces two kinds of zip: the `.ipa` an App Store upload is, and
//! the game pak a browser build mounts. Both are read back by code in this
//! tree (Apple's tooling and `MiniZip`), so the writer only needs the two
//! methods that side understands: STORED (verbatim, what a mounted pak reads
//! in place) and DEFLATE (raw, via zlib - what a download-sized archive uses).
//!
//! Deliberately NOT a shell-out to `zip`: that binary does not exist on
//! Windows, and an export must behave the same on every host it runs on.
//!
//! Two properties matter beyond "it is a zip":
//!  - FILE MODE travels. An `.ipa` carries an executable; a bundle whose
//!    binary unpacks without its executable bit does not launch, so each
//!    entry's POSIX mode goes into the central directory's external
//!    attributes the way every unix zip writer records it.
//!  - The archive is REPRODUCIBLE. Entries are written in the order added,
//!    with one fixed timestamp, so packaging the same tree twice yields the
//!    same bytes.
//!
//! The 32-bit zip format is the whole contract - no zip64. Anything a game
//! ships is orders of magnitude below the 4 GiB / 65535-entry ceilings, and an
//! archive that would cross one is refused by name rather than written
//! corrupt.

namespace OrkigeExport
{
	//! @brief a zip archive being assembled in memory (@see ExportZip.h)
	class ExportZip
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief how one entry's bytes are stored
		enum Method
		{
			METHOD_STORE	= 0,	//!< verbatim - readable without inflating
			METHOD_DEFLATE	= 8		//!< raw deflate, via zlib
		};

		//--- Methods -----------------------------------------------
	public:
		//! @brief add @p bytes as @p archiveName (a forward-slash internal
		//! path). @p posixMode is the entry's file mode (0644 / 0755); pass 0
		//! for the default 0644. False with an honest @p error on a duplicate
		//! or empty name, a compression failure, or a size the format cannot
		//! carry.
		bool addBytes(Orkige::String const & archiveName,
			std::vector<unsigned char> const & bytes, Method method,
			unsigned int posixMode, Orkige::String * error);

		//! @brief add the file at @p path as @p archiveName, carrying its own
		//! executable bit across
		bool addFile(Orkige::String const & archiveName,
			Orkige::String const & path, Method method, Orkige::String * error);

		//! @brief the number of entries added so far
		std::size_t entryCount() const { return this->mEntries.size(); }

		//! @brief the finished archive's bytes (central directory + end
		//! record appended). False with an @p error when the archive exceeds
		//! what the 32-bit format carries.
		bool finish(std::vector<unsigned char> & out,
			Orkige::String * error) const;

		//! @brief write the finished archive to @p path
		bool write(Orkige::String const & path, Orkige::String * error) const;

		//--- Types -------------------------------------------------
	private:
		//! one added entry, already compressed
		struct Entry
		{
			Orkige::String				name;
			std::vector<unsigned char>	data;			//!< as stored
			unsigned int				method = 0;
			unsigned int				crc = 0;
			unsigned long long			originalSize = 0;
			unsigned int				externalAttributes = 0;
		};

		//--- Attributes --------------------------------------------
	private:
		std::vector<Entry>	mEntries;
	};

	//! @brief raw-deflate @p input into @p out (no zlib wrapper - the byte
	//! stream a zip entry carries). False with an @p error when zlib refuses.
	bool deflateRaw(std::vector<unsigned char> const & input,
		std::vector<unsigned char> & out, Orkige::String * error);
}

#endif //__ExportZip_h__1_8_2026__10_00_00__
