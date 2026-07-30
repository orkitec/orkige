/********************************************************************
	created:	2026/07/30 at 10:00
	filename: 	FileWriter.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __FileWriter_h__30_7_2026__10_00_00__
#define __FileWriter_h__30_7_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Filesystem
	*  @{ */

	//! @brief the WRITE side of the filesystem funnel: a streaming,
	//! crash-safe file sink. ResourceReader answers "give me this named
	//! resource's bytes"; this answers "put these bytes on disk, and either
	//! all of them arrive or none do".
	//!
	//! WHY it exists: a core writer that opens a file itself spreads
	//! platform-path handling, missing-parent-directory handling and the
	//! temp-then-rename dance across every call site - and each copy is a
	//! chance to leave a HALF-WRITTEN file where a good one used to be. A
	//! streamed download is the case that forces the issue: megabytes arrive
	//! over time, so the bytes cannot be buffered until the end, yet a
	//! failure mid-transfer must not destroy the previous file.
	//!
	//! THE CONTRACT: begin() opens a SIBLING temp file (never the target),
	//! write() appends to it, and commit() is the single instant the target
	//! changes - a rename over it. Anything else (an error, a cancel, the
	//! destructor) removes the temp file and leaves the target exactly as it
	//! was. Parent directories are created on begin().
	//!
	//! It is deliberately MINIMAL and non-owning of policy: no path jail (the
	//! caller owns that decision - @see PathJail), no formatting, no text/
	//! binary distinction (bytes are bytes).
	class ORKIGE_CORE_DLL FileWriter
	{
		//--- Variables ---------------------------------------
	private:
		void *				mHandle;	//!< the open temp file (FILE*), NULL when closed
		String				mPath;		//!< the target path commit() renames onto
		String				mTempPath;	//!< the sibling temp file bytes land in
		unsigned long long	mWritten;	//!< bytes handed to write() so far
		//--- Methods -----------------------------------------
	public:
		//! constructor (nothing open)
		FileWriter();
		//! destructor - an uncommitted transfer is ABORTED (temp file removed)
		~FileWriter();

		//! @brief open a transfer onto @p path: creates the parent directories
		//! and opens the sibling temp file bytes are appended to.
		//! @return false with @p error set (nothing opened) when the path is
		//! empty, its directory cannot be created or the temp file cannot be
		//! opened
		bool begin(String const & path, String & error);
		//! @brief append @p count bytes; @return false with @p error set on a
		//! short/failed write (the transfer is aborted then, @see abort)
		bool write(char const * bytes, unsigned long long count, String & error);
		//! @brief flush + close the temp file and rename it over the target -
		//! the ONE instant the target file changes.
		//! @return false with @p error set (target untouched, temp removed)
		bool commit(String & error);
		//! @brief drop the transfer: close and remove the temp file, leaving
		//! the target exactly as it was. Safe to call when nothing is open.
		void abort();

		//! is a transfer open (begun, not yet committed/aborted)
		bool isOpen() const { return this->mHandle != NULL; }
		//! bytes accepted by write() so far
		unsigned long long getWritten() const { return this->mWritten; }
		//! the target path of the open transfer ("" when none)
		String const & getPath() const { return this->mPath; }

		//! @brief the whole-file convenience: begin + write + commit in one
		//! call (the small-payload case - a config, a manifest, a save)
		static bool writeWholeFile(String const & path, String const & bytes,
			String & error);
	private:
		FileWriter(FileWriter const &) = delete;
		FileWriter & operator=(FileWriter const &) = delete;
	};

	/** @} */
}

#endif //__FileWriter_h__30_7_2026__10_00_00__
