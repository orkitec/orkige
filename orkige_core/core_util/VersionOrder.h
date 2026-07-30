/********************************************************************
	created:	Thursday 2026/07/30 at 18:00
	filename: 	VersionOrder.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VersionOrder_h__30_7_2026__18_00_00__
#define __VersionOrder_h__30_7_2026__18_00_00__

//! @file VersionOrder.h
//! @brief ordered build identities: compose one, parse one, and answer which
//! of two is newer.
//! @remarks A commit sha answers "which tree is this" but not "is that
//! download newer than what I run" - shas have no order. An ORDERED identity
//! does, and this is the one definition of its grammar and its precedence, so
//! the binary, the packaging tooling and any updater agree by construction.
//!
//! The grammar is semantic versioning 2.0.0. A build of the nightly channel
//! reads
//!
//!     2.0.0-nightly.20260730+dea551f9e
//!     ^^^^^ ^^^^^^^ ^^^^^^^^ ^^^^^^^^^
//!     base  channel  date     commit (build metadata)
//!
//! which orders by base version first, then by date, and carries the commit
//! along without letting it affect precedence. Two builds of the same base on
//! the same date are therefore the SAME version even when their commits
//! differ - deliberate: a second build of one day's tree is not an update, and
//! a client that treats it as one would re-download forever.
//!
//! A stamped RELEASE (no prerelease part, "2.1.0") outranks every prerelease
//! of that base - the semantic-versioning rule, and the one a channel switch
//! depends on.
//!
//! Pure string and number work: no filesystem, no engine, no allocation
//! beyond the parsed parts. Malformed input is answered honestly with
//! VO_INCOMPARABLE rather than guessed at - an unstamped developer build
//! ("2.0.0 (local build)") is not a version, and nothing may silently rank it.

#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	//! @brief the ordered-identity vocabulary (@see the file comment)
	namespace VersionOrder
	{
		//! how one version relates to another (@see compare)
		enum Order
		{
			VO_OLDER = -1,			//!< the left version precedes the right
			VO_SAME = 0,			//!< equal precedence - NOT an update
			VO_NEWER = 1,			//!< the left version follows the right
			VO_INCOMPARABLE = 2	//!< at least one side is not a version
		};

		//! @brief one parsed identity. @p mBuild is carried but never ordered.
		struct Version
		{
			Version() : mMajor(0), mMinor(0), mPatch(0) {}

			unsigned long mMajor;
			unsigned long mMinor;
			unsigned long mPatch;
			//! the dot-separated prerelease identifiers ("nightly", "20260730");
			//! empty for a release version
			StringVector mPrerelease;
			//! the build metadata (the source commit); never affects precedence
			String mBuild;
		};

		//! @brief parse @p text into @p outVersion.
		//! @return false when @p text is not a version, leaving @p outVersion
		//! untouched-by-contract (callers must not read it on false).
		//! @remarks Accepts an optional leading "v" (the shape a git tag
		//! carries) and both renderings of the build metadata: the canonical
		//! "+<commit>" and the FILENAME rendering "_<commit>", which exists
		//! because "+" does not survive every download path (@see
		//! filenameToken). Strict about everything else - three numeric base
		//! fields, no leading zeroes, no empty identifiers - so a string that
		//! only looks like a version is refused instead of mis-ranked.
		bool parse(String const & text, Version & outVersion);

		//! @brief which of two identities is newer.
		//! @return VO_NEWER when @p left follows @p right, VO_OLDER when it
		//! precedes it, VO_SAME on equal precedence (build metadata ignored),
		//! VO_INCOMPARABLE when either side fails to parse.
		Order compare(String const & left, String const & right);

		//! @brief the updater's question: is @p candidate strictly newer than
		//! @p current? False for equal, older AND incomparable - an update is
		//! only ever offered on a proven ordering.
		bool isUpdate(String const & candidate, String const & current);

		//! @brief compose the ordered identity of a nightly build from the
		//! engine's base version, the build date ("YYYY-MM-DD" or the compact
		//! "YYYYMMDD") and the source commit.
		//! @return "" when @p base or @p date is unusable - an identity is
		//! never invented from an incomplete stamp. @p commit may be empty
		//! (the identity then carries no build metadata).
		String compose(String const & base, String const & date,
			String const & commit);

		//! @brief the commit an identity carries, or "" when it carries none.
		//! Recovering the source commit from the version is why the metadata
		//! is part of the identity at all.
		String commitOf(String const & text);

		//! @brief the same identity rendered for a FILENAME: the "+" that
		//! separates the build metadata becomes "_". Download paths and
		//! asset stores sanitise characters outside [A-Za-z0-9._-], which
		//! would rewrite "+" to something the client can no longer match
		//! against the version it polled; "_" is not a semantic-versioning
		//! character at all, so parse() reads the token back to the SAME
		//! version with no ambiguity.
		String filenameToken(String const & text);
	}
}

#endif //__VersionOrder_h__30_7_2026__18_00_00__
