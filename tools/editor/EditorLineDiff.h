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

	//! @brief the kind of a change hunk (a maximal contiguous run of change).
	enum class HunkKind : unsigned char
	{
		Added,		//!< current lines with no baseline counterpart (insertion)
		Modified,	//!< baseline lines replaced by current lines (both non-empty)
		Deleted		//!< baseline lines with no surviving current line (removal)
	};

	//! @brief one change hunk: a contiguous span of current and/or baseline
	//! lines that differ. The RANGES index into the buffers (@see LineDiff) so
	//! the actual text is sliced ON DEMAND from the cached line vectors - a hunk
	//! copies no strings. A CURRENT-line range [curStart, curStart+curCount) and
	//! a BASELINE-line range [baseStart, baseStart+baseCount): Added carries
	//! baseCount==0, Deleted carries curCount==0 (curStart is then the gap line,
	//! matching LineDiff::deletions), Modified carries both non-zero.
	struct DiffHunk
	{
		int curStart = 0;	//!< first current line (the gap line when Deleted)
		int curCount = 0;	//!< current lines in the hunk (0 when Deleted)
		int baseStart = 0;	//!< first baseline line
		int baseCount = 0;	//!< baseline lines in the hunk (0 when Added)
		HunkKind kind = HunkKind::Modified;
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
		//! every change hunk (Added/Modified/Deleted), in ascending current-line
		//! order - the popup/revert/navigation index. A superset of the same
		//! truth `states`/`deletions` carry, plus the baseline ranges those two
		//! cannot express.
		std::vector<DiffHunk> hunks;

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

	//! @brief the hunk whose CURRENT range covers `line` (an Added/Modified
	//! change bar sits on it), or -1. Pure lookup over `diff.hunks`.
	int hunkForCurrentLine(LineDiff const& diff, int line);

	//! @brief the Deleted hunk whose gap sits at current-line `gapLine` (a
	//! deletion triangle is drawn there), or -1. Pure lookup over `diff.hunks`.
	int hunkForDeletionGap(LineDiff const& diff, int gapLine);

	//! @brief the current line a change-navigation step lands on, from cursor
	//! `currentLine`: the anchor (curStart) of the next hunk (forward) or the
	//! previous hunk (backward), WRAPPING around the ends. -1 when there are no
	//! hunks. Pure so next/prev-change is unit-testable.
	int navigateHunkLine(std::vector<DiffHunk> const& hunks, int currentLine,
		bool forward);

	//! @brief the hunk's slice of a line vector (its baseline lines from
	//! `baseline`, or its current lines from `current`). A bounds-clamped copy
	//! sliced on demand - the popup's before/after text.
	std::vector<std::string> hunkBaselineLines(
		std::vector<std::string> const& baseline, DiffHunk const& hunk);
	std::vector<std::string> hunkCurrentLines(
		std::vector<std::string> const& current, DiffHunk const& hunk);

	//! @brief the popup clamps a long hunk to at most this many shown lines,
	//! then an honest "... N more" line. @see clampHunkPreview
	inline constexpr int kMaxHunkPreviewLines = 20;

	//! @brief how many of `total` lines the popup shows and how many it hides:
	//! shown = min(total, kMaxHunkPreviewLines), remaining = total - shown. Pure
	//! so the clamp math is unit-testable. @param outRemaining lines elided.
	int clampHunkPreview(int total, int& outRemaining);

	//! @brief revert one hunk PURELY: replace the current-line slice
	//! [curStart, curStart+curCount) with the hunk's baseline slice, returning
	//! the new buffer lines. Uniform across kinds - an Added hunk (empty
	//! baseline slice) drops its lines, a Deleted hunk (empty current slice)
	//! re-inserts the baseline lines at the gap, a Modified hunk swaps them.
	//! The editor rejoins the result with '\n' and applies it through the
	//! widget's undoable text path (never touching disk).
	std::vector<std::string> applyHunkRevert(
		std::vector<std::string> const& current,
		std::vector<std::string> const& baseline, DiffHunk const& hunk);
}

#endif //__EditorLineDiff_h__27_7_2026__12_00_00__
