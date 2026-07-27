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

using OrkigeEditor::LineChange;
using OrkigeEditor::LineDiff;
using OrkigeEditor::computeLineDiff;
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
}
