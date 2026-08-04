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
//! @brief the zip an export WRITES and the zip it READS - one module over the
//! same zlib the engine's `MiniZip` runs on.
//!
//! An export produces two kinds of zip: the `.ipa` an App Store upload is, and
//! the game pak a browser build mounts. Both are read back by code in this
//! tree (Apple's tooling and `MiniZip`), so the writer only needs the two
//! methods that side understands: STORED (verbatim, what a mounted pak reads
//! in place) and DEFLATE (raw, via zlib - what a download-sized archive uses).
//!
//! It also CONSUMES one: an Android library archive is a zip a project points
//! at, and its parts are routed into the package being assembled
//! (@see ExportAndroidLibrary.h). Reading and writing share the format's
//! constants here rather than growing a second, drifting copy.
//!
//! Deliberately NOT a shell-out to `zip`/`unzip`: neither binary exists on
//! Windows, and an export must behave the same on every host it runs on.
//!
//! Two properties matter beyond "it is a zip":
//!  - FILE MODE travels. An `.ipa` carries an executable; a bundle whose
//!    binary unpacks without its executable bit does not launch, so each
//!    entry's POSIX mode goes into the central directory's external
//!    attributes the way every unix zip writer records it. A mode is always
//!    written; where it is DERIVED from a staged file it needs a host that
//!    has modes to derive it from (@see hostCarriesFileModes).
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
		//! executable bit across where the host has one to carry (0644 for
		//! every entry where it does not - @see hostCarriesFileModes)
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

	//! @brief a zip archive being read - the reverse of @ref ExportZip, for the
	//! archives an export CONSUMES rather than produces.
	//!
	//! The whole file is held in memory: what this reads is a library archive a
	//! project points at, which is orders of magnitude below the cap it refuses
	//! past. Nothing about the archive is taken on faith - an entry name that
	//! escapes the tree it unpacks into, an encrypted entry and a zip64 archive
	//! are each refused BY NAME rather than unpacked somewhere surprising.
	class ExportZipReader
	{
		//--- Types -------------------------------------------------
	public:
		//! @brief one entry the central directory names
		struct Entry
		{
			//! the stored name, forward-slashed (a zip name always is)
			Orkige::String		name;
			unsigned long long	size = 0;		//!< uncompressed bytes
			//! a directory entry (a trailing slash - it carries no content)
			bool				directory = false;
		};

		//--- Methods -----------------------------------------------
	public:
		//! @brief read @p path's central directory. False with an honest
		//! @p error when the file is missing, is not a zip, needs zip64, holds
		//! an encrypted entry, or names an entry that would unpack outside the
		//! destination.
		bool open(Orkige::String const & path, Orkige::String * error);

		//! @brief every entry, in central-directory order
		std::vector<Entry> const & entries() const { return this->mEntries; }

		//! @brief does the archive carry an entry named @p name?
		bool has(Orkige::String const & name) const;

		//! @brief the uncompressed bytes of the entry named @p name
		bool read(Orkige::String const & name,
			std::vector<unsigned char> & out, Orkige::String * error) const;

		//--- Types -------------------------------------------------
	private:
		//! an entry plus where its bytes are and how they are stored
		struct Located
		{
			Entry				entry;
			unsigned int		method = 0;
			unsigned int		crc = 0;
			unsigned long long	compressedSize = 0;
			unsigned long long	localHeaderOffset = 0;
		};

		//--- Attributes --------------------------------------------
	private:
		Orkige::String				mPath;
		std::vector<unsigned char>	mBytes;
		std::vector<Entry>			mEntries;
		std::vector<Located>		mLocated;
	};

	//! @brief is @p entryName safe to unpack under a destination directory?
	//! PURE. False for an absolute path, a Windows drive letter, a backslash
	//! separator and any `..` component - the four ways an archive from
	//! somewhere else writes outside the tree it was told to write into.
	bool isSafeArchiveEntryName(Orkige::String const & entryName);

	//! @brief raw-deflate @p input into @p out (no zlib wrapper - the byte
	//! stream a zip entry carries). False with an @p error when zlib refuses.
	bool deflateRaw(std::vector<unsigned char> const & input,
		std::vector<unsigned char> & out, Orkige::String * error);

	//! @brief raw-inflate @p input, whose content is @p originalSize bytes
	//! long. False with an @p error when zlib refuses or the stream does not
	//! produce exactly that many bytes.
	bool inflateRaw(std::vector<unsigned char> const & input,
		unsigned long long originalSize, std::vector<unsigned char> & out,
		Orkige::String * error);

	//! @brief whether this host stores POSIX file modes.
	//! @remarks false on Windows, which models only a read-only flag - and
	//! reports every file as executable through std::filesystem, so a mode
	//! read there would be an invention rather than a fact.
	//! `ExportZip::addFile` records the deterministic default 0644 for every
	//! entry on such a host; an explicit mode passed to `addBytes` is always
	//! recorded verbatim. The archives whose executable bit decides whether
	//! the artifact runs - the `.ipa` an Apple bundle is packed into - are
	//! built on macOS, where the bit is real.
	bool hostCarriesFileModes();
}

#endif //__ExportZip_h__1_8_2026__10_00_00__
