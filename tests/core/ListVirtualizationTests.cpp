/**************************************************************
	created:	2026/07/29 at 10:00
	filename: 	ListVirtualizationTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the uniform-height list windowing math
	(core_util/UiLayout::virtualWindow): which rows a scroll viewport needs
	as live widgets, the overscan ring, the clamps at both ends of the model
	and the widget-count BOUND a virtualized list is allowed to materialise.
	The widget side (GuiListView creating/releasing exactly that window,
	rows staying hit-testable at their virtual offsets) is asserted by the
	player_gallery selfcheck.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/UiLayout.h"

#include <cmath>

using namespace Orkige;

TEST_CASE("virtualWindow: an unscrolled viewport shows its first rows",
	"[unit][uilayout][listview]")
{
	// 200px viewport, 40px rows -> 5 rows visible, +1 overscan below
	const ListWindow window = virtualWindow(0.0f, 200.0f, 40.0f, 100, 1);
	CHECK(window.first == 0);				// clamped at the top of the model
	CHECK(window.count == 6);				// 5 visible + 1 overscan
	CHECK(window.end() == 6);
	CHECK(window.contains(0));
	CHECK(window.contains(5));
	CHECK_FALSE(window.contains(6));
}

TEST_CASE("virtualWindow: scrolling moves the window with overscan on both sides",
	"[unit][uilayout][listview]")
{
	// scrolled down by 10 rows (offset is negative - the content shifts UP)
	const ListWindow window = virtualWindow(-400.0f, 200.0f, 40.0f, 100, 1);
	CHECK(window.first == 9);				// row 10 is the first visible, -1 overscan
	CHECK(window.end() == 16);				// rows 10..14 visible, +1 overscan
	CHECK(window.count == 7);
	// the visible band is fully inside the window
	for (int each = 10; each <= 14; ++each)
	{
		CHECK(window.contains(each));
	}
}

TEST_CASE("virtualWindow: a partially scrolled row is materialised",
	"[unit][uilayout][listview]")
{
	// half a row scrolled off: rows 0..5 are (partly) visible
	const ListWindow window = virtualWindow(-20.0f, 200.0f, 40.0f, 100, 0);
	CHECK(window.first == 0);
	CHECK(window.end() == 6);				// the sixth row peeks in at the bottom
}

TEST_CASE("virtualWindow: the bottom of the model clamps the window",
	"[unit][uilayout][listview]")
{
	// scrolled to the very end of a 10-row model
	const ListWindow window = virtualWindow(-200.0f, 200.0f, 40.0f, 10, 1);
	CHECK(window.end() == 10);				// never past the last row
	CHECK(window.first == 4);				// rows 5..9 visible, -1 overscan
	CHECK(window.count == 6);
}

TEST_CASE("virtualWindow: an empty or unmeasured list materialises nothing",
	"[unit][uilayout][listview]")
{
	CHECK(virtualWindow(0.0f, 200.0f, 40.0f, 0, 1).count == 0);
	CHECK(virtualWindow(0.0f, 200.0f, 0.0f, 100, 1).count == 0);
	CHECK(virtualWindow(0.0f, 200.0f, -5.0f, 100, 1).count == 0);
	// scrolled entirely past a short model
	CHECK(virtualWindow(-10000.0f, 200.0f, 40.0f, 3, 0).count == 0);
}

TEST_CASE("virtualWindow: the widget count stays bounded over the whole scroll",
	"[unit][uilayout][listview]")
{
	// THE WIN: a 1000-row list in a 200px viewport of 40px rows never needs
	// more than ceil(viewport/item) + 1 + 2*overscan widgets, wherever it is
	// scrolled - the assertion the gallery selfcheck mirrors on real widgets
	const float viewport = 200.0f;
	const float item = 40.0f;
	const int count = 1000;
	const int overscan = 1;
	const int bound = int(std::ceil(viewport / item)) + 1 + 2 * overscan;
	CHECK(bound == 8);
	for (int step = 0; step <= count; ++step)
	{
		const float offset = -float(step) * item * 0.5f;
		const ListWindow window =
			virtualWindow(offset, viewport, item, count, overscan);
		CHECK(window.count <= bound);
		CHECK(window.first >= 0);
		CHECK(window.end() <= count);
	}
	// and it is a WINDOW, not the whole model
	CHECK(virtualWindow(-2000.0f, viewport, item, count, overscan).count < count);
}

TEST_CASE("virtualWindow: a negative overscan is treated as none",
	"[unit][uilayout][listview]")
{
	const ListWindow window = virtualWindow(-400.0f, 200.0f, 40.0f, 100, -3);
	CHECK(window.first == 10);
	CHECK(window.end() == 15);
}
