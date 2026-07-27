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
		//! baseline lines as one end-of-middle deletion. Honest, not precise.
		void coarseMiddle(std::vector<LineChange>& states, int curStart,
			int curEnd, int baseCount, std::vector<int>& deletions)
		{
			const int curCount = curEnd - curStart;
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
		}

		//! diff the trimmed middle base[baseStart,baseEnd) vs cur[curStart,curEnd)
		//! into `states` (indexed in full-current space) and `deletions`.
		void diffMiddle(std::vector<std::string> const& base, int baseStart,
			int baseEnd, std::vector<std::string> const& cur, int curStart,
			int curEnd, std::vector<LineChange>& states,
			std::vector<int>& deletions)
		{
			const int n = baseEnd - baseStart;	// baseline middle length
			const int m = curEnd - curStart;	// current middle length
			if (n == 0)
			{
				// pure insertion: every current middle line is Added
				for (int j = curStart; j < curEnd; ++j)
				{
					states[j] = LineChange::Added;
				}
				return;
			}
			if (m == 0)
			{
				// pure deletion: a run vanished before the current line curStart
				deletions.push_back(curStart);
				return;
			}
			if (static_cast<long long>(n) * m > kLcsCellCap)
			{
				coarseMiddle(states, curStart, curEnd, n, deletions);
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
			auto flush = [&]()
			{
				if (hunkDel == 0 && hunkIns == 0)
				{
					return;
				}
				if (hunkIns > 0)
				{
					const LineChange kind = hunkDel > 0
						? LineChange::Modified : LineChange::Added;
					for (int k = 0; k < hunkIns; ++k)
					{
						states[hunkInsStart + k] = kind;
					}
				}
				else
				{
					deletions.push_back(hunkGap);
				}
				hunkDel = 0;
				hunkIns = 0;
				hunkInsStart = -1;
				hunkGap = -1;
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
			out.states, out.deletions);
		return out;
	}
}
