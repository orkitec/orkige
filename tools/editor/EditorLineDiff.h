/**************************************************************
	created:	2026/07/27 at 12:00
	filename: 	EditorLineDiff.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorLineDiff_h__27_7_2026__12_00_00__
#define __EditorLineDiff_h__27_7_2026__12_00_00__

#include <string>
#include <vector>

namespace OrkigeEditor
{
	//! @brief per-current-line change classification of a buffer against a
	//! baseline (the git-index blob). Drives the code editor's gutter change
	//! bars: Added = green, Modified = amber, None = no bar. Deletions carry no
	//! surviving current line and are reported separately (@see LineDiff).
	enum class LineChange : unsigned char
	{
		None,		//!< unchanged since the baseline
		Added,		//!< a line the baseline did not have (a pure insertion)
		Modified	//!< a changed line (an insertion paired with a deletion)
	};

	//! @brief the gutter verdict for one buffer against its baseline.
	struct LineDiff
	{
		//! one entry per CURRENT line (size == the buffer's line count)
		std::vector<LineChange> states;
		//! CURRENT-line indices with a run of baseline lines deleted immediately
		//! ABOVE them (each in [0 .. line count]; == line count means the
		//! deletion sits at end-of-file). Sorted ascending, unique - the gutter
		//! draws a small triangle at each boundary.
		std::vector<int> deletions;

		//! @brief does any current line carry a non-None state?
		bool hasStateChange() const
		{
			for (LineChange state : this->states)
			{
				if (state != LineChange::None)
				{
					return true;
				}
			}
			return false;
		}
		//! @brief no change at all (an unmodified tracked file).
		bool empty() const
		{
			return this->deletions.empty() && !this->hasStateChange();
		}
	};

	//! @brief buffers at or below this line count recompute the live (per-edit)
	//! diff on each idle debounce; a larger buffer refreshes only on save - an
	//! honest cap so a huge file never stalls the type loop. @see shouldLiveDiff
	inline constexpr int kMaxLiveDiffLines = 20000;

	//! @brief may the LIVE (debounced, per-edit) diff run for a buffer of this
	//! many lines? A pure policy predicate so the cap decision is unit-testable.
	inline bool shouldLiveDiff(int lineCount)
	{
		return lineCount <= kMaxLiveDiffLines;
	}

	//! @brief split a text blob into lines exactly the way the code-editor widget
	//! counts them: split on '\n' KEEPING a trailing empty segment (so a blob
	//! ending in '\n' yields one more line), and strip a '\r' before each '\n' so
	//! a CRLF baseline compares clean against the widget's LF buffer. "" -> [""].
	std::vector<std::string> splitLines(std::string const& text);

	//! @brief compute the per-line change set of `current` against `baseline`.
	//! Pure and bounded: an O(N) common prefix/suffix trim, then an LCS over the
	//! differing middle. A pathologically large middle (a full rewrite of a big
	//! file) exceeds the cell cap and falls back to a coarse overlap=Modified
	//! verdict, so the call always returns bounded work.
	LineDiff computeLineDiff(std::vector<std::string> const& baseline,
		std::vector<std::string> const& current);
}

#endif //__EditorLineDiff_h__27_7_2026__12_00_00__
