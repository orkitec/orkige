/**************************************************************
	created:	2026/07/11 at 18:00
	filename: 	GuiStateTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless gui-state unit tests for the pure logic the runtime gui shares
	with these tests: the single-selection toggle-group machine, the modal
	stack + z allocation, and the timed toast queue. No renderer, no widget.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/ToggleGroupState.h"
#include "core_util/ModalStack.h"
#include "core_util/ToastQueue.h"

using namespace Orkige;

//--- ToggleGroupState -------------------------------------------------------

TEST_CASE("ToggleGroupState: tapping a member selects it and clears the rest",
	"[unit][gui][togglegroup]")
{
	ToggleGroupState group;
	group.count = 3;

	// nothing selected until the first tap
	CHECK(group.selected == -1);

	CHECK(group.onMemberTapped(1) == 1);
	CHECK(group.isSelected(1));
	CHECK_FALSE(group.isSelected(0));
	CHECK_FALSE(group.isSelected(2));

	// a second member steals the selection - single-selection semantics
	CHECK(group.onMemberTapped(2) == 2);
	CHECK(group.isSelected(2));
	CHECK_FALSE(group.isSelected(1));
}

TEST_CASE("ToggleGroupState: a plain radio group keeps one selected",
	"[unit][gui][togglegroup]")
{
	ToggleGroupState group;
	group.count = 3;
	group.allowNone = false;

	group.onMemberTapped(0);
	// re-tapping the selected member does NOT clear it (radio semantics)
	CHECK(group.onMemberTapped(0) == 0);
	CHECK(group.isSelected(0));
}

TEST_CASE("ToggleGroupState: an allow-none group toggles the selected member off",
	"[unit][gui][togglegroup]")
{
	ToggleGroupState group;
	group.count = 3;
	group.allowNone = true;

	group.onMemberTapped(2);
	CHECK(group.isSelected(2));
	// re-tapping clears it when the empty state is allowed
	CHECK(group.onMemberTapped(2) == -1);
	CHECK(group.selected == -1);
}

TEST_CASE("ToggleGroupState: select() validates the index and reports change",
	"[unit][gui][togglegroup]")
{
	ToggleGroupState group;
	group.count = 3;

	CHECK(group.select(1));			// changed
	CHECK_FALSE(group.select(1));	// no change
	CHECK_FALSE(group.select(9));	// out of range, ignored (no allow-none)
	CHECK(group.isSelected(1));

	group.allowNone = true;
	CHECK(group.select(-1));		// clears when allowed
	CHECK(group.selected == -1);
	// taps outside the range never move the selection
	CHECK(group.onMemberTapped(-5) == -1);
}

//--- ModalStack -------------------------------------------------------------

TEST_CASE("ModalStack: each pushed modal climbs above the previous",
	"[unit][gui][modal]")
{
	ModalStack stack;
	CHECK(stack.empty());

	ModalStack::Entry first = stack.push("confirm");
	CHECK(first.scrimZ == 1000u);
	CHECK(first.contentZ == 1001u);	// dialog widgets one layer above the scrim
	CHECK(stack.size() == 1u);
	CHECK(stack.topId() == "confirm");

	ModalStack::Entry second = stack.push("alert");
	CHECK(second.scrimZ == 1100u);	// a full zStep above the first
	CHECK(second.contentZ == 1101u);
	CHECK(second.scrimZ > first.contentZ);	// the second modal covers the first
	CHECK(stack.topId() == "alert");
}

TEST_CASE("ModalStack: pop removes the top; remove targets any modal",
	"[unit][gui][modal]")
{
	ModalStack stack;
	stack.push("a");
	stack.push("b");
	stack.push("c");

	CHECK(stack.popTop() == "c");
	CHECK(stack.size() == 2u);

	// remove out of order (a dialog dismissed while another sits on top)
	CHECK(stack.remove("a"));
	CHECK_FALSE(stack.remove("a"));	// already gone
	CHECK(stack.topId() == "b");

	CHECK(stack.popTop() == "b");
	CHECK(stack.empty());
	CHECK(stack.popTop() == "");	// popping empty is harmless
}

//--- ToastQueue -------------------------------------------------------------

TEST_CASE("ToastQueue: toasts surface one at a time in order and expire",
	"[unit][gui][toast]")
{
	ToastQueue queue;
	queue.enqueue("first", 2.0f, 0.0f);
	queue.enqueue("second", 2.0f, 0.0f);
	CHECK(queue.size() == 2u);
	CHECK(queue.pending() == 1u);
	CHECK(queue.activeText() == "first");

	queue.update(1.0f);
	CHECK(queue.activeText() == "first");	// still within its 2s lifetime

	queue.update(1.5f);						// 2.5s total > 2s -> first expires
	CHECK(queue.activeText() == "second");	// the next surfaced
	CHECK(queue.pending() == 0u);

	queue.update(2.0f);						// second expires too
	CHECK_FALSE(queue.hasActive());
	CHECK(queue.activeText() == "");
}

TEST_CASE("ToastQueue: a long frame can retire several toasts, carrying time",
	"[unit][gui][toast]")
{
	ToastQueue queue;
	queue.enqueue("a", 1.0f, 0.0f);
	queue.enqueue("b", 1.0f, 0.0f);
	queue.enqueue("c", 1.0f, 0.0f);

	// 2.5s in one step: a (0..1) and b (1..2) expire, c is 0.5s into its life
	queue.update(2.5f);
	CHECK(queue.activeText() == "c");
	CHECK(queue.size() == 1u);
}

TEST_CASE("ToastQueue: alpha ramps in, holds, then ramps out",
	"[unit][gui][toast]")
{
	ToastQueue queue;
	queue.enqueue("fade", 2.0f, 0.5f);	// 0.5s in, 1s hold, 0.5s out

	CHECK(queue.activeAlpha() == 0.0f);			// t=0, just starting to fade in
	queue.update(0.25f);
	CHECK(queue.activeAlpha() > 0.4f);			// halfway through the fade in
	CHECK(queue.activeAlpha() < 0.6f);
	queue.update(0.75f);						// t=1.0, in the hold
	CHECK(queue.activeAlpha() == 1.0f);
	queue.update(0.75f);						// t=1.75, into the fade out
	CHECK(queue.activeAlpha() > 0.0f);
	CHECK(queue.activeAlpha() < 1.0f);
}
