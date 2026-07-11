/**************************************************************
	created:	2026/07/11 at 20:40
	filename: 	GuiAnimationTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the pure gui-animation logic the runtime gui shares
	with these tests: the 2D scale/rotation-about-pivot transform, the flick
	scroll-momentum physics, and the show/hide transition vocabulary. No
	renderer, no widget, no clock (delta is fed in).
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core_util/Ui2DTransform.h"
#include "core_util/ScrollMomentum.h"
#include "core_util/UiTransition.h"

#include <cmath>

using namespace Orkige;
using Catch::Matchers::WithinAbs;

//--- Ui2DTransform ----------------------------------------------------------

TEST_CASE("Ui2DTransform: the default transform is the identity",
	"[unit][gui][animation][transform]")
{
	Ui2DTransform t;
	CHECK(t.isIdentity());
	float x = 0.0f, y = 0.0f;
	t.apply(37.0f, -9.0f, x, y);
	CHECK(x == 37.0f);
	CHECK(y == -9.0f);
}

TEST_CASE("Ui2DTransform: scale grows about the pivot, which stays fixed",
	"[unit][gui][animation][transform]")
{
	Ui2DTransform t;
	t.pivotX = 100.0f;
	t.pivotY = 100.0f;
	t.scaleX = 2.0f;
	t.scaleY = 2.0f;
	CHECK_FALSE(t.isIdentity());

	// the pivot itself does not move
	float px = 0.0f, py = 0.0f;
	t.apply(100.0f, 100.0f, px, py);
	CHECK_THAT(px, WithinAbs(100.0f, 1e-4f));
	CHECK_THAT(py, WithinAbs(100.0f, 1e-4f));

	// a point one unit right of the pivot ends two units right
	float x = 0.0f, y = 0.0f;
	t.apply(110.0f, 100.0f, x, y);
	CHECK_THAT(x, WithinAbs(120.0f, 1e-4f));
	CHECK_THAT(y, WithinAbs(100.0f, 1e-4f));
}

TEST_CASE("Ui2DTransform: rotation turns points about the pivot",
	"[unit][gui][animation][transform]")
{
	Ui2DTransform t;
	t.pivotX = 0.0f;
	t.pivotY = 0.0f;
	t.rotation = 3.14159265358979f / 2.0f;	// 90 degrees

	// (10,0) rotates to (0,10) in the top-left-origin pixel space
	float x = 0.0f, y = 0.0f;
	t.apply(10.0f, 0.0f, x, y);
	CHECK_THAT(x, WithinAbs(0.0f, 1e-4f));
	CHECK_THAT(y, WithinAbs(10.0f, 1e-4f));

	// the pivot is fixed under rotation too
	Ui2DTransform p;
	p.pivotX = 50.0f;
	p.pivotY = 20.0f;
	p.rotation = 1.2345f;
	float qx = 0.0f, qy = 0.0f;
	p.apply(50.0f, 20.0f, qx, qy);
	CHECK_THAT(qx, WithinAbs(50.0f, 1e-4f));
	CHECK_THAT(qy, WithinAbs(20.0f, 1e-4f));
}

//--- ScrollMomentum ---------------------------------------------------------

TEST_CASE("ScrollMomentum: a wheel notch snaps within bounds and kills inertia",
	"[unit][gui][animation][scroll]")
{
	ScrollMomentum m;
	m.setBounds(-300.0f, 0.0f);	// content shift convention: [-maxScroll, 0]
	m.wheelBy(-48.0f);
	CHECK_THAT(m.offset(), WithinAbs(-48.0f, 1e-4f));
	CHECK(m.velocity() == 0.0f);

	// past the bound it clamps, no overscroll from a wheel
	m.wheelBy(-1000.0f);
	CHECK_THAT(m.offset(), WithinAbs(-300.0f, 1e-4f));
	CHECK_FALSE(m.isMoving());
}

TEST_CASE("ScrollMomentum: dragging past a bound rubber-bands (resisted travel)",
	"[unit][gui][animation][scroll]")
{
	ScrollMomentum m;
	m.setBounds(-300.0f, 0.0f);
	m.beginDrag();
	// drag 200px past the top bound (offset 0); the resisted travel is smaller
	m.dragBy(200.0f);
	CHECK(m.offset() > 0.0f);			// it does move past the edge
	CHECK(m.offset() < 200.0f);			// but less than the raw drag (resistance)

	// pulling further resists more (the ratio of travel to raw shrinks)
	const float firstOvershoot = m.offset();
	m.dragBy(200.0f);					// another 200 raw
	const float secondOvershoot = m.offset();
	CHECK(secondOvershoot > firstOvershoot);
	CHECK((secondOvershoot - firstOvershoot) < 200.0f);
}

TEST_CASE("ScrollMomentum: a released overscroll springs back to the bound",
	"[unit][gui][animation][scroll]")
{
	ScrollMomentum m;
	m.setBounds(-300.0f, 0.0f);
	m.beginDrag();
	m.dragBy(120.0f);					// overscroll past the top (0)
	CHECK(m.offset() > 0.0f);
	m.endDrag();

	// advance ~1s of frames: the offset settles back exactly to the bound
	for(int frame = 0; frame < 120; ++frame)
	{
		m.update(1.0f / 60.0f);
	}
	CHECK_THAT(m.offset(), WithinAbs(0.0f, 1e-3f));
	CHECK_FALSE(m.isMoving());
}

TEST_CASE("ScrollMomentum: a flick coasts then settles inside the bounds",
	"[unit][gui][animation][scroll]")
{
	ScrollMomentum m;
	m.setBounds(-300.0f, 0.0f);
	m.setOffset(-150.0f);				// mid-range
	m.beginDrag();
	// drag upward (toward -300) over several frames so a velocity builds
	for(int frame = 0; frame < 5; ++frame)
	{
		m.dragBy(-20.0f);
		m.update(1.0f / 60.0f);
	}
	CHECK(m.velocity() < 0.0f);			// moving toward the lower bound
	m.endDrag();

	const float releaseOffset = m.offset();
	// one coast tick moves it further in the flick direction (inertia)
	m.update(1.0f / 60.0f);
	CHECK(m.offset() < releaseOffset);

	// it eventually comes to rest, and always inside the legal range
	for(int frame = 0; frame < 600; ++frame)
	{
		m.update(1.0f / 60.0f);
	}
	CHECK_FALSE(m.isMoving());
	CHECK(m.offset() >= -300.0f);
	CHECK(m.offset() <= 0.0f);
}

TEST_CASE("ScrollMomentum: content that fits pins to the upper bound",
	"[unit][gui][animation][scroll]")
{
	ScrollMomentum m;
	// an empty range (content <= viewport): min collapses onto max
	m.setBounds(0.0f, 0.0f);
	m.beginDrag();
	m.dragBy(-500.0f);
	m.endDrag();
	for(int frame = 0; frame < 120; ++frame)
	{
		m.update(1.0f / 60.0f);
	}
	CHECK_THAT(m.offset(), WithinAbs(0.0f, 1e-3f));
}

//--- UiTransition -----------------------------------------------------------

TEST_CASE("UiTransition: parse resolves families, separators and durations",
	"[unit][gui][animation][transition]")
{
	CHECK(parseTransition("fade 0.2").type == UTT_Fade);
	CHECK_THAT(parseTransition("fade 0.2").duration, WithinAbs(0.2f, 1e-5f));

	// a missing duration uses the default; case and separator are flexible
	CHECK(parseTransition("POP").type == UTT_Pop);
	CHECK_THAT(parseTransition("POP").duration,
		WithinAbs(UI_TRANSITION_DEFAULT_DURATION, 1e-5f));
	CHECK(parseTransition("slide_up 0.3").type == UTT_SlideUp);
	CHECK(parseTransition("slide-left").type == UTT_SlideLeft);

	// empty / unknown / explicit none -> no transition
	CHECK(parseTransition("").type == UTT_None);
	CHECK(parseTransition("wobble 2").type == UTT_None);
	CHECK(parseTransition("none").type == UTT_None);
}

TEST_CASE("UiTransition: an enter plan animates away->rest, exit reverses it",
	"[unit][gui][animation][transition]")
{
	const UiTransitionSpec fade = parseTransition("fade 0.25");

	const UiTransitionPlan enter = planTransition(fade, true, 0.0f, 0.0f);
	CHECK(enter.animatesAlpha);
	CHECK_THAT(enter.alphaFrom, WithinAbs(0.0f, 1e-5f));	// invisible -> visible
	CHECK_THAT(enter.alphaTo, WithinAbs(1.0f, 1e-5f));
	CHECK_THAT(enter.duration, WithinAbs(0.25f, 1e-5f));

	const UiTransitionPlan exit = planTransition(fade, false, 0.0f, 0.0f);
	CHECK_THAT(exit.alphaFrom, WithinAbs(1.0f, 1e-5f));	// visible -> invisible
	CHECK_THAT(exit.alphaTo, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("UiTransition: a slide enters from the away offset toward rest (0,0)",
	"[unit][gui][animation][transition]")
{
	const UiTransitionSpec up = parseTransition("slide-up 0.3");
	const UiTransitionPlan enter = planTransition(up, true, 100.0f, 64.0f);
	CHECK(enter.animatesOffset);
	// slide-up starts BELOW its rest (positive y in top-left-origin space)
	CHECK_THAT(enter.offsetFromY, WithinAbs(64.0f, 1e-5f));
	CHECK_THAT(enter.offsetToY, WithinAbs(0.0f, 1e-5f));
	CHECK_THAT(enter.offsetFromX, WithinAbs(0.0f, 1e-5f));

	const UiTransitionSpec left = parseTransition("slide-left");
	const UiTransitionPlan enterLeft = planTransition(left, true, 100.0f, 64.0f);
	CHECK_THAT(enterLeft.offsetFromX, WithinAbs(100.0f, 1e-5f));	// starts right
}

TEST_CASE("UiTransition: pop scales up from zero with an overshoot ease",
	"[unit][gui][animation][transition]")
{
	const UiTransitionSpec pop = parseTransition("pop 0.3");
	const UiTransitionPlan enter = planTransition(pop, true, 0.0f, 0.0f);
	CHECK(enter.animatesScale);
	CHECK_THAT(enter.scaleFrom, WithinAbs(0.0f, 1e-5f));
	CHECK_THAT(enter.scaleTo, WithinAbs(1.0f, 1e-5f));
	CHECK(enter.ease == String("backOut"));	// the springy overshoot
}
