/**************************************************************
	created:	2026/07/29 at 12:00
	filename: 	EditorTreeDndTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the shared tree drag-drop drop-zone classifier
	(tools/editor/EditorTreeDnd): the Into (middle half) vs Before (top
	quarter) vs After (bottom quarter) band split, used by BOTH the visual
	.oui editor's widget tree and the scene GameObject Hierarchy. Pure
	geometry - no ImGui, no engine types.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "EditorTreeDnd.h"

using namespace OrkigeEditor;

TEST_CASE("tree-dnd: drop-zone bands - top/bottom quarter reorder, middle half "
	"parents", "[unit][treednd]")
{
	// a row at y in [100, 120): height 20, so the quarters are 5px each
	const float top = 100.0f;
	const float h = 20.0f;

	// top quarter (y < 105) => Before
	CHECK(classifyTreeDrop(top, h, 100.0f) == TreeDropZone::Before);
	CHECK(classifyTreeDrop(top, h, 104.0f) == TreeDropZone::Before);
	// middle half (105..115) => Into
	CHECK(classifyTreeDrop(top, h, 106.0f) == TreeDropZone::Into);
	CHECK(classifyTreeDrop(top, h, 110.0f) == TreeDropZone::Into);
	CHECK(classifyTreeDrop(top, h, 114.0f) == TreeDropZone::Into);
	// bottom quarter (y > 115) => After
	CHECK(classifyTreeDrop(top, h, 116.0f) == TreeDropZone::After);
	CHECK(classifyTreeDrop(top, h, 119.0f) == TreeDropZone::After);

	// the exact band edges: 0.25 and 0.75 are INSIDE the middle band (< / > strict)
	CHECK(classifyTreeDrop(top, h, 105.0f) == TreeDropZone::Into);
	CHECK(classifyTreeDrop(top, h, 115.0f) == TreeDropZone::Into);

	// a cursor above the row clamps to Before, below to After (gap drops resolve)
	CHECK(classifyTreeDrop(top, h, 90.0f) == TreeDropZone::Before);
	CHECK(classifyTreeDrop(top, h, 200.0f) == TreeDropZone::After);

	// a degenerate row height falls back to Into
	CHECK(classifyTreeDrop(top, 0.0f, 100.0f) == TreeDropZone::Into);
	CHECK(classifyTreeDrop(top, -5.0f, 100.0f) == TreeDropZone::Into);
}
