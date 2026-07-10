/**************************************************************
	created:	2026/07/11 at 11:00
	filename: 	Breadcrumbs.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __Breadcrumbs_h__11_7_2026__11_00_00__
#define __Breadcrumbs_h__11_7_2026__11_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"

#include <deque>
#include <fstream>
#include <utility>
#include <vector>

namespace Orkige
{
	//! @brief the crash-survivable event trail: an always-on, cheap, bounded
	//! ring of recent engine events (scene loads, script errors, warnings, boot/
	//! shutdown markers) that is FLUSHED to disk after every entry, so a hard
	//! crash (SIGSEGV / OOM / watchdog kill on a device) leaves a readable trail
	//! of what happened up to the instant of death.
	//! @remarks Distinct from TraceWriter (the on-demand flight recorder that
	//! buffers in memory and writes once at the end - a crash loses that buffer).
	//! Breadcrumbs share TraceWriter's JSON-line shape (one object per line,
	//! parseable incrementally, a truncated tail still yields whole lines) but
	//! diverge on lifecycle: the ring is authoritative in memory AND mirrored to
	//! the file after each record(), bounded by a small entry count and byte cap.
	//! On boot the player rotates the previous session's file aside
	//! (breadcrumbs.jsonl -> breadcrumbs.prev.jsonl) so the editor can read the
	//! survived trail after an abnormal exit. Player-owned like LevelManager: the
	//! editor never sets a file, so recording is an honest in-memory no-op there.
	class ORKIGE_CORE_DLL Breadcrumbs : public Singleton<Breadcrumbs>
	{
		DECL_OSINGLETON(Breadcrumbs);
		//--- Variables ---------------------------------------
	public:
		//! default ring size (last N events kept in memory and on disk)
		static const size_t DEFAULT_MAX_ENTRIES;
		//! default per-entry text cap (a longer message is truncated)
		static const size_t DEFAULT_MAX_MESSAGE;
		//! default whole-file byte cap (a hard bound on disk footprint)
		static const size_t DEFAULT_MAX_FILE_BYTES;
	private:
		size_t				mMaxEntries;	//!< ring capacity
		size_t				mMaxMessage;	//!< per-entry message cap (chars)
		size_t				mMaxFileBytes;	//!< whole-file byte cap
		std::deque<String>	mRing;			//!< the last N encoded lines
		String				mFile;			//!< live file path ("" = memory only)
		String				mPrevFile;		//!< rotated (previous session) path
		std::ofstream		mStream;		//!< the live append stream (flushed per entry)
		size_t				mFileBytes;		//!< bytes written to the live file so far
		double				mStartSeconds;	//!< monotonic epoch for the "t" field
		//--- Methods -----------------------------------------
	public:
		Breadcrumbs(size_t maxEntries = DEFAULT_MAX_ENTRIES,
			size_t maxFileBytes = DEFAULT_MAX_FILE_BYTES);
		virtual ~Breadcrumbs();

		//! @brief set the live file path ("" keeps recording in memory only).
		//! Does not open/rotate by itself - call rotate() at boot to move any
		//! previous session's file aside and start a fresh one.
		void setFile(String const & path);
		String const & getFile() const { return mFile; }
		//! the rotated (previous-session) file path derived from the live one
		String const & getPreviousFile() const { return mPrevFile; }

		//! @brief rotate at session start: move the live file to the previous
		//! slot (replacing any older previous file) and begin a fresh live ring.
		//! No-op (memory ring reset only) when no file is set.
		void rotate();

		//! @brief record one event line: {"t","kind","msg"[,<k>:<v>...]}. kind
		//! is a short category ("scene","script_error","log","boot","shutdown"),
		//! msg the human-readable text (truncated to the per-entry cap), fields
		//! optional flat key/value pairs. Cheap and always safe (a closed file
		//! just keeps the in-memory ring).
		void record(String const & kind, String const & msg,
			std::vector<std::pair<String, String>> const & fields = {});

		//! entries currently held in the ring
		size_t count() const { return mRing.size(); }
		//! @brief the whole ring as .jsonl text (each line one JSON object,
		//! trailing newline) - what a reader/test parses
		String contents() const;

		//! @brief read a breadcrumb file straight off disk (the editor reads the
		//! survived file; the player may be dead - pure file I/O). false (outText
		//! untouched) when the file is missing or unreadable.
		static bool loadFile(String const & path, String & outText);
	protected:
	private:
		//! (re)open the live stream in append mode; recomputes mFileBytes
		void openStream();
		//! rewrite the whole file from the in-memory ring (byte-cap enforcement)
		void rewriteFromRing();
	};
}

#endif //__Breadcrumbs_h__11_7_2026__11_00_00__
