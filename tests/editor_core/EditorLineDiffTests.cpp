/********************************************************************
	created:	2026/07/27 at 12:00
	filename: 	EditorLineDiffTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Exhaustive unit battery for the pure git-gutter line diff (EditorLineDiff):
// adds/mods/deletes/moves/mixed, empty/identical files, first/last-line edits,
// the buffer-vs-baseline line-count semantics, and the size-cap policy.
#include "EditorLineDiff.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using OrkigeEditor::DiffHunk;
using OrkigeEditor::HunkKind;
using OrkigeEditor::LineChange;
using OrkigeEditor::LineDiff;
using OrkigeEditor::applyHunkRevert;
using OrkigeEditor::clampHunkPreview;
using OrkigeEditor::computeLineDiff;
using OrkigeEditor::hunkBaselineLines;
using OrkigeEditor::hunkCurrentLines;
using OrkigeEditor::hunkForCurrentLine;
using OrkigeEditor::hunkForDeletionGap;
using OrkigeEditor::navigateHunkLine;
using OrkigeEditor::splitLines;

namespace
{
	//! the diff of two blobs, split the widget way (the real call path)
	LineDiff diffText(std::string const& baseline, std::string const& current)
	{
		return computeLineDiff(splitLines(baseline), splitLines(current));
	}

	//! count the current lines carrying a given change kind
	int countState(LineDiff const& diff, LineChange kind)
	{
		int total = 0;
		for (LineChange state : diff.states)
		{
			if (state == kind)
			{
				++total;
			}
		}
		return total;
	}
}

TEST_CASE("splitLines matches the code-editor line counting", "[linediff]")
{
	// "" is one (empty) line, like the widget's freshly-appended first line
	REQUIRE(splitLines("") == std::vector<std::string>{ "" });
	REQUIRE(splitLines("a\nb") == std::vector<std::string>{ "a", "b" });
	// a trailing newline yields one MORE (empty) line - the widget shows it
	REQUIRE(splitLines("a\nb\n") == std::vector<std::string>{ "a", "b", "" });
	// a CRLF baseline strips the '\r' so it compares clean against an LF buffer
	REQUIRE(splitLines("a\r\nb\r\n") == std::vector<std::string>{ "a", "b", "" });
}

TEST_CASE("identical buffers report no change", "[linediff]")
{
	const LineDiff diff = diffText("a\nb\nc\n", "a\nb\nc\n");
	REQUIRE(diff.empty());
	REQUIRE(diff.deletions.empty());
	REQUIRE(countState(diff, LineChange::Added) == 0);
	REQUIRE(countState(diff, LineChange::Modified) == 0);
}

TEST_CASE("an empty baseline marks every current line added", "[linediff]")
{
	// an untracked-but-computed case: no baseline lines at all. splitLines("")
	// is one empty line, so use genuinely-empty vectors for the corner
	const LineDiff diff = computeLineDiff({}, { "a", "b", "c" });
	REQUIRE(diff.states.size() == 3);
	REQUIRE(countState(diff, LineChange::Added) == 3);
	REQUIRE(diff.deletions.empty());
}

TEST_CASE("a single added line in the middle", "[linediff]")
{
	// baseline a,b ; current a,X,b -> X (index 1) added, no deletions
	const LineDiff diff = diffText("a\nb", "a\nX\nb");
	REQUIRE(diff.states.size() == 3);
	REQUIRE(diff.states[0] == LineChange::None);
	REQUIRE(diff.states[1] == LineChange::Added);
	REQUIRE(diff.states[2] == LineChange::None);
	REQUIRE(diff.deletions.empty());
}

TEST_CASE("a single modified line", "[linediff]")
{
	// one changed line reads as Modified (an insertion paired with a deletion)
	const LineDiff diff = diffText("a\nb\nc", "a\nX\nc");
	REQUIRE(diff.states[0] == LineChange::None);
	REQUIRE(diff.states[1] == LineChange::Modified);
	REQUIRE(diff.states[2] == LineChange::None);
	REQUIRE(diff.deletions.empty());
}

TEST_CASE("a whole-file single line change", "[linediff]")
{
	const LineDiff diff = diffText("a", "b");
	REQUIRE(diff.states.size() == 1);
	REQUIRE(diff.states[0] == LineChange::Modified);
	REQUIRE(diff.deletions.empty());
}

TEST_CASE("a deleted line reports a deletion gap, no surviving state",
	"[linediff]")
{
	// baseline a,b,c ; current a,c -> b vanished between a and c: a gap before
	// current index 1, no line carries a state
	const LineDiff diff = diffText("a\nb\nc", "a\nc");
	REQUIRE(diff.states.size() == 2);
	REQUIRE(countState(diff, LineChange::Added) == 0);
	REQUIRE(countState(diff, LineChange::Modified) == 0);
	REQUIRE(diff.deletions == std::vector<int>{ 1 });
}

TEST_CASE("a deletion at the very top", "[linediff]")
{
	// baseline X,a,b ; current a,b -> the gap sits before current index 0
	const LineDiff diff = diffText("X\na\nb", "a\nb");
	REQUIRE(!diff.hasStateChange());
	REQUIRE(diff.deletions == std::vector<int>{ 0 });
}

TEST_CASE("a deletion at end-of-file gaps at the line count", "[linediff]")
{
	// baseline a,b,c ; current a,b -> c vanished off the end: gap index == count
	const LineDiff diff = diffText("a\nb\nc", "a\nb");
	REQUIRE(!diff.hasStateChange());
	REQUIRE(diff.deletions == std::vector<int>{ 2 });
}

TEST_CASE("first-line edits", "[linediff]")
{
	SECTION("modified first line")
	{
		const LineDiff diff = diffText("a\nb\nc", "X\nb\nc");
		REQUIRE(diff.states[0] == LineChange::Modified);
		REQUIRE(diff.states[1] == LineChange::None);
	}
	SECTION("added first line")
	{
		const LineDiff diff = diffText("a\nb", "X\na\nb");
		REQUIRE(diff.states[0] == LineChange::Added);
		REQUIRE(diff.states[1] == LineChange::None);
		REQUIRE(diff.states[2] == LineChange::None);
	}
}

TEST_CASE("last-line edits", "[linediff]")
{
	SECTION("modified last line")
	{
		const LineDiff diff = diffText("a\nb\nc", "a\nb\nX");
		REQUIRE(diff.states[2] == LineChange::Modified);
		REQUIRE(diff.states[0] == LineChange::None);
	}
	SECTION("added last line")
	{
		const LineDiff diff = diffText("a\nb", "a\nb\nX");
		REQUIRE(diff.states[2] == LineChange::Added);
		REQUIRE(diff.deletions.empty());
	}
}

TEST_CASE("a contiguous block of added lines", "[linediff]")
{
	const LineDiff diff = diffText("a\nb", "a\nX\nY\nZ\nb");
	REQUIRE(diff.states.size() == 5);
	REQUIRE(diff.states[0] == LineChange::None);
	REQUIRE(diff.states[1] == LineChange::Added);
	REQUIRE(diff.states[2] == LineChange::Added);
	REQUIRE(diff.states[3] == LineChange::Added);
	REQUIRE(diff.states[4] == LineChange::None);
}

TEST_CASE("a block replaced by a longer block is all modified", "[linediff]")
{
	// two lines changed to three: the inserted lines all read Modified (a
	// change hunk with both deletes and inserts), no separate deletion triangle
	const LineDiff diff = diffText("a\nX\nY\nb", "a\nP\nQ\nR\nb");
	REQUIRE(diff.states.size() == 5);
	REQUIRE(diff.states[0] == LineChange::None);
	REQUIRE(diff.states[1] == LineChange::Modified);
	REQUIRE(diff.states[2] == LineChange::Modified);
	REQUIRE(diff.states[3] == LineChange::Modified);
	REQUIRE(diff.states[4] == LineChange::None);
	REQUIRE(diff.deletions.empty());
}

TEST_CASE("a block replaced by a shorter block is modified, not deleted",
	"[linediff]")
{
	// three lines changed to one: a change hunk (both deletes and inserts)
	// reads as Modified for the surviving line - no separate deletion triangle
	// (only a PURE deletion draws one, the VS Code model)
	const LineDiff diff = diffText("a\nX\nY\nZ\nb", "a\nP\nb");
	REQUIRE(diff.states.size() == 3);
	REQUIRE(diff.states[1] == LineChange::Modified);
	REQUIRE(diff.deletions.empty());
}

TEST_CASE("mixed edits across the file", "[linediff]")
{
	// baseline: a b c d e
	// current : a C c X e   (b deleted, d->X modified, new line C after a)
	// walk it as: keep a, +C/-b hunk (modified C at 1), keep c, d->X modified,
	// keep e
	const LineDiff diff = diffText("a\nb\nc\nd\ne", "a\nC\nc\nX\ne");
	REQUIRE(diff.states.size() == 5);
	REQUIRE(diff.states[0] == LineChange::None);
	REQUIRE(diff.states[1] == LineChange::Modified);
	REQUIRE(diff.states[2] == LineChange::None);
	REQUIRE(diff.states[3] == LineChange::Modified);
	REQUIRE(diff.states[4] == LineChange::None);
}

TEST_CASE("a moved line reads as one deletion and one addition", "[linediff]")
{
	// baseline a,b,c,d ; current b,c,d,a (a moved to the end). The LCS keeps
	// b,c,d; a is deleted at the top and re-added at the bottom.
	const LineDiff diff = diffText("a\nb\nc\nd", "b\nc\nd\na");
	REQUIRE(diff.states.size() == 4);
	REQUIRE(diff.states[3] == LineChange::Added);	// a re-added at the end
	REQUIRE(diff.deletions == std::vector<int>{ 0 });	// a deleted from the top
	REQUIRE(countState(diff, LineChange::Modified) == 0);
}

TEST_CASE("emptying a file to nothing", "[linediff]")
{
	// baseline three lines, current a single empty line (an emptied buffer):
	// the widget's empty buffer is one empty line, so this is a modify+delete
	const LineDiff diff = diffText("a\nb\nc", "");
	REQUIRE(diff.states.size() == 1);
	// the surviving empty line pairs with the deletion -> Modified, remaining
	// baseline lines gap off the end
	REQUIRE_FALSE(diff.empty());
}

TEST_CASE("appending to an empty file", "[linediff]")
{
	// baseline empty (one empty line), current one real line + trailing newline
	const LineDiff diff = diffText("", "hello\n");
	REQUIRE(diff.hasStateChange());
}

TEST_CASE("a two-for-one replacement is a single modified verdict", "[linediff]")
{
	// baseline a,b,c,d ; current a,X,d : b and c both replaced by X -> the one
	// surviving line is Modified, the net removal is folded into the change hunk
	const LineDiff diff = diffText("a\nb\nc\nd", "a\nX\nd");
	REQUIRE(diff.states[1] == LineChange::Modified);
	REQUIRE(diff.deletions.empty());
}

TEST_CASE("shouldLiveDiff caps at the documented line count", "[linediff]")
{
	REQUIRE(OrkigeEditor::shouldLiveDiff(0));
	REQUIRE(OrkigeEditor::shouldLiveDiff(OrkigeEditor::kMaxLiveDiffLines));
	REQUIRE_FALSE(
		OrkigeEditor::shouldLiveDiff(OrkigeEditor::kMaxLiveDiffLines + 1));
}

TEST_CASE("the coarse cap still returns a bounded, non-empty verdict",
	"[linediff]")
{
	// a fully-rewritten large middle blows past the LCS cell cap and falls back
	// to the coarse overlap=Modified verdict - it must still terminate and mark
	// the buffer as changed (never silently clean)
	std::vector<std::string> baseline;
	std::vector<std::string> current;
	for (int i = 0; i < 3000; ++i)
	{
		baseline.push_back("base-" + std::to_string(i));
		current.push_back("curr-" + std::to_string(i));
	}
	const LineDiff diff = computeLineDiff(baseline, current);
	REQUIRE(diff.states.size() == current.size());
	REQUIRE(diff.hasStateChange());
	REQUIRE(countState(diff, LineChange::Modified) == 3000);
	// the whole rewritten middle folds into one coarse Modified hunk over both
	// ranges (the popup/revert see the region, not a split we never computed)
	REQUIRE(diff.hunks.size() == 1);
	REQUIRE(diff.hunks[0].kind == HunkKind::Modified);
	REQUIRE(diff.hunks[0].curCount == 3000);
	REQUIRE(diff.hunks[0].baseCount == 3000);
}

//--- hunk records: baseline ranges the states/deletions cannot express -------

TEST_CASE("an added hunk carries an empty baseline range", "[linediff][hunk]")
{
	// baseline a,b ; current a,X,Y,b -> one Added hunk spanning current 1..3
	const LineDiff diff = diffText("a\nb", "a\nX\nY\nb");
	REQUIRE(diff.hunks.size() == 1);
	DiffHunk const& hunk = diff.hunks[0];
	REQUIRE(hunk.kind == HunkKind::Added);
	REQUIRE(hunk.curStart == 1);
	REQUIRE(hunk.curCount == 2);
	REQUIRE(hunk.baseCount == 0);
	// the current slice is exactly the inserted lines; the baseline slice empty
	REQUIRE(hunkCurrentLines(splitLines("a\nX\nY\nb"), hunk) ==
		std::vector<std::string>{ "X", "Y" });
	REQUIRE(hunkBaselineLines(splitLines("a\nb"), hunk).empty());
}

TEST_CASE("a modified hunk carries both ranges", "[linediff][hunk]")
{
	// baseline a,b,c ; current a,X,c -> one Modified hunk (b -> X)
	const std::vector<std::string> baseline = splitLines("a\nb\nc");
	const std::vector<std::string> current = splitLines("a\nX\nc");
	const LineDiff diff = computeLineDiff(baseline, current);
	REQUIRE(diff.hunks.size() == 1);
	DiffHunk const& hunk = diff.hunks[0];
	REQUIRE(hunk.kind == HunkKind::Modified);
	REQUIRE(hunkBaselineLines(baseline, hunk) ==
		std::vector<std::string>{ "b" });
	REQUIRE(hunkCurrentLines(current, hunk) ==
		std::vector<std::string>{ "X" });
}

TEST_CASE("a deleted hunk carries the removed baseline lines and no current",
	"[linediff][hunk]")
{
	// baseline a,X,Y,b ; current a,b -> one Deleted hunk (X,Y removed at gap 1)
	const std::vector<std::string> baseline = splitLines("a\nX\nY\nb");
	const std::vector<std::string> current = splitLines("a\nb");
	const LineDiff diff = computeLineDiff(baseline, current);
	REQUIRE(diff.hunks.size() == 1);
	DiffHunk const& hunk = diff.hunks[0];
	REQUIRE(hunk.kind == HunkKind::Deleted);
	REQUIRE(hunk.curStart == 1);		// the gap, matching deletions
	REQUIRE(hunk.curCount == 0);
	REQUIRE(hunkBaselineLines(baseline, hunk) ==
		std::vector<std::string>{ "X", "Y" });
	REQUIRE(hunkCurrentLines(current, hunk).empty());
}

TEST_CASE("hunk lookup maps a marked line and a deletion gap to its hunk",
	"[linediff][hunk]")
{
	// baseline a,b,c,d ; current a,B,C,d -> a Modified hunk on lines 1..2
	const LineDiff diff = diffText("a\nb\nc\nd", "a\nB\nC\nd");
	REQUIRE(hunkForCurrentLine(diff, 0) == -1);	// unchanged line
	REQUIRE(hunkForCurrentLine(diff, 1) == 0);
	REQUIRE(hunkForCurrentLine(diff, 2) == 0);
	REQUIRE(hunkForCurrentLine(diff, 3) == -1);
	// a pure deletion is reached by its gap, not by a current line
	const LineDiff del = diffText("a\nX\nb", "a\nb");
	REQUIRE(hunkForCurrentLine(del, 1) == -1);
	REQUIRE(hunkForDeletionGap(del, 1) == 0);
	REQUIRE(hunkForDeletionGap(del, 0) == -1);
}

TEST_CASE("navigation walks the hunk anchors and wraps", "[linediff][hunk]")
{
	// two distinct changes: modify line 0, add a line after line 2
	// baseline: a,b,c,d ; current: A,b,c,X,d  (A mod@0, X add@3)
	const LineDiff diff = diffText("a\nb\nc\nd", "A\nb\nc\nX\nd");
	REQUIRE(diff.hunks.size() == 2);
	// from the very top, forward reaches the second hunk; from there, again
	// forward WRAPS to the first
	REQUIRE(navigateHunkLine(diff.hunks, 0, true) == 3);
	REQUIRE(navigateHunkLine(diff.hunks, 3, true) == 0);
	// backward is the mirror
	REQUIRE(navigateHunkLine(diff.hunks, 3, false) == 0);
	REQUIRE(navigateHunkLine(diff.hunks, 0, false) == 3);
	// no hunks -> no target
	REQUIRE(navigateHunkLine({}, 0, true) == -1);
}

TEST_CASE("the popup clamp shows at most the cap and reports the remainder",
	"[linediff][hunk]")
{
	int remaining = -1;
	REQUIRE(clampHunkPreview(5, remaining) == 5);
	REQUIRE(remaining == 0);
	REQUIRE(clampHunkPreview(OrkigeEditor::kMaxHunkPreviewLines, remaining) ==
		OrkigeEditor::kMaxHunkPreviewLines);
	REQUIRE(remaining == 0);
	REQUIRE(clampHunkPreview(OrkigeEditor::kMaxHunkPreviewLines + 7, remaining) ==
		OrkigeEditor::kMaxHunkPreviewLines);
	REQUIRE(remaining == 7);
	REQUIRE(clampHunkPreview(0, remaining) == 0);
	REQUIRE(remaining == 0);
}

//--- the revert transform: buffer lines + hunk + baseline -> new buffer ------

namespace
{
	//! revert the diff's first hunk against the buffers, returning the new
	//! current lines - the exact transform the editor applies (then rejoins)
	std::vector<std::string> revertFirst(std::string const& baseText,
		std::string const& curText)
	{
		const std::vector<std::string> baseline = splitLines(baseText);
		const std::vector<std::string> current = splitLines(curText);
		const LineDiff diff = computeLineDiff(baseline, current);
		REQUIRE_FALSE(diff.hunks.empty());
		return applyHunkRevert(current, baseline, diff.hunks[0]);
	}
}

TEST_CASE("reverting a modified hunk restores the baseline lines",
	"[linediff][revert]")
{
	REQUIRE(revertFirst("a\nb\nc", "a\nX\nc") == splitLines("a\nb\nc"));
}

TEST_CASE("reverting an added hunk drops the inserted lines",
	"[linediff][revert]")
{
	REQUIRE(revertFirst("a\nb", "a\nX\nY\nb") == splitLines("a\nb"));
}

TEST_CASE("reverting a deleted hunk re-inserts the baseline lines",
	"[linediff][revert]")
{
	REQUIRE(revertFirst("a\nX\nb", "a\nb") == splitLines("a\nX\nb"));
}

TEST_CASE("reverting the first line", "[linediff][revert]")
{
	SECTION("modified first line")
	{
		REQUIRE(revertFirst("a\nb\nc", "X\nb\nc") == splitLines("a\nb\nc"));
	}
	SECTION("added first line")
	{
		REQUIRE(revertFirst("a\nb", "X\na\nb") == splitLines("a\nb"));
	}
	SECTION("deleted first line")
	{
		REQUIRE(revertFirst("X\na\nb", "a\nb") == splitLines("X\na\nb"));
	}
}

TEST_CASE("reverting the last line and EOF hunks", "[linediff][revert]")
{
	SECTION("modified last line")
	{
		REQUIRE(revertFirst("a\nb\nc", "a\nb\nX") == splitLines("a\nb\nc"));
	}
	SECTION("added last line")
	{
		REQUIRE(revertFirst("a\nb", "a\nb\nX") == splitLines("a\nb"));
	}
	SECTION("a deletion off the end (gap == line count) re-appends")
	{
		// baseline a,b,c ; current a,b -> c vanished off the end
		REQUIRE(revertFirst("a\nb\nc", "a\nb") == splitLines("a\nb\nc"));
	}
	SECTION("a line added past a trailing newline")
	{
		// baseline ends in a newline (trailing empty segment); the add lands
		// after it - the splice must land it in the same spot
		const std::vector<std::string> reverted =
			revertFirst("a\nb\n", "a\nb\nX\n");
		REQUIRE(reverted == splitLines("a\nb\n"));
	}
}

TEST_CASE("reverting one hunk of several leaves the others in place",
	"[linediff][revert]")
{
	// two independent hunks: modify line 0, add after line 2. Reverting the
	// FIRST (the modify) restores line 0 but keeps the added line.
	const std::vector<std::string> baseline = splitLines("a\nb\nc");
	const std::vector<std::string> current = splitLines("A\nb\nX\nc");
	const LineDiff diff = computeLineDiff(baseline, current);
	REQUIRE(diff.hunks.size() == 2);
	const std::vector<std::string> reverted =
		applyHunkRevert(current, baseline, diff.hunks[0]);
	// line 0 back to "a", the inserted "X" still present
	REQUIRE(reverted == splitLines("a\nb\nX\nc"));
}
