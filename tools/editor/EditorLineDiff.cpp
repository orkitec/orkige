/**************************************************************
	created:	2026/07/27 at 12:00
	filename: 	EditorLineDiff.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "EditorLineDiff.h"

#include <algorithm>

namespace OrkigeEditor
{
	namespace
	{
		//! the LCS table is n*m cells; a middle larger than this many cells
		//! bails to the coarse verdict (an unusual full rewrite of a big file -
		//! kept bounded so the diff never allocates a gigabyte matrix). 4M cells
		//! is ~16MB of int at the ceiling, only reachable well past the live cap.
		constexpr long long kLcsCellCap = 4LL * 1000LL * 1000LL;

		//! coarse verdict for a middle too large to LCS: the overlapping lines
		//! read as Modified, any extra current lines as Added, and an excess of
		//! baseline lines as one end-of-middle deletion. Honest, not precise. The
		//! whole middle folds into ONE Modified hunk (base[baseStart,baseEnd) vs
		//! cur[curStart,curEnd)) - the popup/revert see the region, not a precise
		//! split we did not compute.
		void coarseMiddle(std::vector<LineChange>& states, int curStart,
			int curEnd, int baseStart, int baseEnd, std::vector<int>& deletions,
			std::vector<DiffHunk>& hunks)
		{
			const int curCount = curEnd - curStart;
			const int baseCount = baseEnd - baseStart;
			const int overlap = std::min(baseCount, curCount);
			for (int k = 0; k < curCount; ++k)
			{
				states[curStart + k] =
					k < overlap ? LineChange::Modified : LineChange::Added;
			}
			if (baseCount > curCount)
			{
				deletions.push_back(curEnd);
			}
			DiffHunk hunk;
			hunk.curStart = curStart;
			hunk.curCount = curCount;
			hunk.baseStart = baseStart;
			hunk.baseCount = baseCount;
			hunk.kind = HunkKind::Modified;
			hunks.push_back(hunk);
		}

		//! diff the trimmed middle base[baseStart,baseEnd) vs cur[curStart,curEnd)
		//! into `states` (indexed in full-current space), `deletions` and the
		//! `hunks` record (baseline ranges the first two cannot express).
		void diffMiddle(std::vector<std::string> const& base, int baseStart,
			int baseEnd, std::vector<std::string> const& cur, int curStart,
			int curEnd, std::vector<LineChange>& states,
			std::vector<int>& deletions, std::vector<DiffHunk>& hunks)
		{
			const int n = baseEnd - baseStart;	// baseline middle length
			const int m = curEnd - curStart;	// current middle length
			if (n == 0 && m == 0)
			{
				return;	// empty middle - identical files, no hunk
			}
			if (n == 0)
			{
				// pure insertion: every current middle line is Added
				for (int j = curStart; j < curEnd; ++j)
				{
					states[j] = LineChange::Added;
				}
				DiffHunk hunk;
				hunk.curStart = curStart;
				hunk.curCount = m;
				hunk.baseStart = baseStart;
				hunk.baseCount = 0;
				hunk.kind = HunkKind::Added;
				hunks.push_back(hunk);
				return;
			}
			if (m == 0)
			{
				// pure deletion: a run vanished before the current line curStart
				deletions.push_back(curStart);
				DiffHunk hunk;
				hunk.curStart = curStart;
				hunk.curCount = 0;
				hunk.baseStart = baseStart;
				hunk.baseCount = n;
				hunk.kind = HunkKind::Deleted;
				hunks.push_back(hunk);
				return;
			}
			if (static_cast<long long>(n) * m > kLcsCellCap)
			{
				coarseMiddle(states, curStart, curEnd, baseStart, baseEnd,
					deletions, hunks);
				return;
			}
			// LCS length table over the middles: dp[i][j] = LCS of base[i..],
			// cur[j..] (both middle-local indices), filled back to front
			std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
			for (int i = n - 1; i >= 0; --i)
			{
				for (int j = m - 1; j >= 0; --j)
				{
					dp[i][j] = base[baseStart + i] == cur[curStart + j]
						? dp[i + 1][j + 1] + 1
						: std::max(dp[i + 1][j], dp[i][j + 1]);
				}
			}
			// walk the table front to back, grouping maximal runs of deletes and
			// inserts (bounded by matches / the ends) into one HUNK, then classify:
			// deletes-only -> a deletion triangle; inserts-only -> Added; both ->
			// the inserted lines read as Modified
			int hunkDel = 0;
			int hunkIns = 0;
			int hunkInsStart = -1;	//!< first current index of the hunk's inserts
			int hunkGap = -1;		//!< current index the hunk begins at (deletion)
			int hunkBaseStart = -1;	//!< first baseline index of the hunk
			auto flush = [&]()
			{
				if (hunkDel == 0 && hunkIns == 0)
				{
					return;
				}
				DiffHunk hunk;
				hunk.curStart = hunkGap;
				hunk.curCount = hunkIns;
				hunk.baseStart = hunkBaseStart;
				hunk.baseCount = hunkDel;
				if (hunkIns > 0)
				{
					const LineChange kind = hunkDel > 0
						? LineChange::Modified : LineChange::Added;
					hunk.kind = hunkDel > 0
						? HunkKind::Modified : HunkKind::Added;
					for (int k = 0; k < hunkIns; ++k)
					{
						states[hunkInsStart + k] = kind;
					}
				}
				else
				{
					hunk.kind = HunkKind::Deleted;
					deletions.push_back(hunkGap);
				}
				hunks.push_back(hunk);
				hunkDel = 0;
				hunkIns = 0;
				hunkInsStart = -1;
				hunkGap = -1;
				hunkBaseStart = -1;
			};
			int i = 0;
			int j = 0;
			while (i < n || j < m)
			{
				if (i < n && j < m &&
					base[baseStart + i] == cur[curStart + j])
				{
					flush();		// a match closes the current hunk
					++i;
					++j;
				}
				else if (j < m && (i >= n || dp[i][j + 1] >= dp[i + 1][j]))
				{
					// an inserted current line (curStart + j)
					if (hunkDel == 0 && hunkIns == 0)
					{
						hunkGap = curStart + j;
						hunkBaseStart = baseStart + i;
					}
					if (hunkIns == 0)
					{
						hunkInsStart = curStart + j;
					}
					++hunkIns;
					++j;
				}
				else
				{
					// a deleted baseline line - the gap sits at the current pos
					if (hunkDel == 0 && hunkIns == 0)
					{
						hunkGap = curStart + j;
						hunkBaseStart = baseStart + i;
					}
					++hunkDel;
					++i;
				}
			}
			flush();
		}
	}

	std::vector<std::string> splitLines(std::string const& text)
	{
		std::vector<std::string> lines;
		std::string line;
		for (char c : text)
		{
			if (c == '\n')
			{
				lines.push_back(line);
				line.clear();
			}
			else if (c != '\r')
			{
				line.push_back(c);
			}
		}
		lines.push_back(line);	// the segment after the last '\n' (may be empty)
		return lines;
	}

	LineDiff computeLineDiff(std::vector<std::string> const& baseline,
		std::vector<std::string> const& current)
	{
		LineDiff out;
		out.states.assign(current.size(), LineChange::None);
		const int n = static_cast<int>(baseline.size());
		const int m = static_cast<int>(current.size());
		// common prefix
		int prefix = 0;
		while (prefix < n && prefix < m && baseline[prefix] == current[prefix])
		{
			++prefix;
		}
		// common suffix (never crossing the prefix)
		int baseEnd = n;
		int curEnd = m;
		while (baseEnd > prefix && curEnd > prefix &&
			baseline[baseEnd - 1] == current[curEnd - 1])
		{
			--baseEnd;
			--curEnd;
		}
		diffMiddle(baseline, prefix, baseEnd, current, prefix, curEnd,
			out.states, out.deletions, out.hunks);
		return out;
	}

	int hunkForCurrentLine(LineDiff const& diff, int line)
	{
		for (std::size_t index = 0; index < diff.hunks.size(); ++index)
		{
			DiffHunk const& hunk = diff.hunks[index];
			if (hunk.curCount > 0 && line >= hunk.curStart &&
				line < hunk.curStart + hunk.curCount)
			{
				return static_cast<int>(index);
			}
		}
		return -1;
	}

	int hunkForDeletionGap(LineDiff const& diff, int gapLine)
	{
		for (std::size_t index = 0; index < diff.hunks.size(); ++index)
		{
			DiffHunk const& hunk = diff.hunks[index];
			if (hunk.kind == HunkKind::Deleted && hunk.curStart == gapLine)
			{
				return static_cast<int>(index);
			}
		}
		return -1;
	}

	int navigateHunkLine(std::vector<DiffHunk> const& hunks, int currentLine,
		bool forward)
	{
		if (hunks.empty())
		{
			return -1;
		}
		// the hunks are already in ascending current-line order
		if (forward)
		{
			for (DiffHunk const& hunk : hunks)
			{
				if (hunk.curStart > currentLine)
				{
					return hunk.curStart;
				}
			}
			return hunks.front().curStart;	// wrap to the first change
		}
		for (auto it = hunks.rbegin(); it != hunks.rend(); ++it)
		{
			if (it->curStart < currentLine)
			{
				return it->curStart;
			}
		}
		return hunks.back().curStart;		// wrap to the last change
	}

	std::vector<std::string> hunkBaselineLines(
		std::vector<std::string> const& baseline, DiffHunk const& hunk)
	{
		std::vector<std::string> out;
		const int begin = std::max(0, hunk.baseStart);
		const int end = std::min(static_cast<int>(baseline.size()),
			hunk.baseStart + hunk.baseCount);
		for (int i = begin; i < end; ++i)
		{
			out.push_back(baseline[i]);
		}
		return out;
	}

	std::vector<std::string> hunkCurrentLines(
		std::vector<std::string> const& current, DiffHunk const& hunk)
	{
		std::vector<std::string> out;
		const int begin = std::max(0, hunk.curStart);
		const int end = std::min(static_cast<int>(current.size()),
			hunk.curStart + hunk.curCount);
		for (int i = begin; i < end; ++i)
		{
			out.push_back(current[i]);
		}
		return out;
	}

	int clampHunkPreview(int total, int& outRemaining)
	{
		const int shown = std::min(std::max(0, total), kMaxHunkPreviewLines);
		outRemaining = std::max(0, total - shown);
		return shown;
	}

	std::vector<std::string> applyHunkRevert(
		std::vector<std::string> const& current,
		std::vector<std::string> const& baseline, DiffHunk const& hunk)
	{
		std::vector<std::string> out;
		const int size = static_cast<int>(current.size());
		const int cutStart = std::clamp(hunk.curStart, 0, size);
		const int cutEnd = std::clamp(hunk.curStart + hunk.curCount, cutStart,
			size);
		// current[0, cutStart)
		for (int i = 0; i < cutStart; ++i)
		{
			out.push_back(current[i]);
		}
		// the hunk's baseline slice takes the removed current range's place
		for (std::string const& line : hunkBaselineLines(baseline, hunk))
		{
			out.push_back(line);
		}
		// current[cutEnd, end)
		for (int i = cutEnd; i < size; ++i)
		{
			out.push_back(current[i]);
		}
		return out;
	}
}
