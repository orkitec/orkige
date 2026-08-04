/********************************************************************
	created:	Sunday 2026/08/03 at 12:00
	filename: 	DevicelessSelectionTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

// Headless coverage for the deviceless word in the ORKIGE_RENDERSYSTEM
// vocabulary (engine_render/RenderSystemSelection.h). The parse decides
// whether a process boots with no window and no GPU, so the rule that matters
// most is the NEGATIVE one: every graphics name must read as "not deviceless",
// because a false positive would silently turn the display off. Runs on both
// flavors; the availability answer is the flavor's own compile-time fact.
#include <catch2/catch_test_macros.hpp>
#include <engine_render/RenderSystemSelection.h>

using Orkige::RenderSystemSelection::devicelessAvailable;
using Orkige::RenderSystemSelection::isDevicelessName;

TEST_CASE("the deviceless words are recognised", "[engine][render][deviceless]")
{
	// the render system's own name and what the word means to a caller
	CHECK(isDevicelessName("NULL"));
	CHECK(isDevicelessName("null"));
	CHECK(isDevicelessName("Null"));
	CHECK(isDevicelessName("headless"));
	CHECK(isDevicelessName("HEADLESS"));
	// typed by hand into an environment variable - blanks do not change meaning
	CHECK(isDevicelessName("  NULL  "));
	CHECK(isDevicelessName("\tnull\n"));
}

TEST_CASE("a graphics name never reads as deviceless", "[engine][render][deviceless]")
{
	// the whole graphics vocabulary both flavors use
	CHECK_FALSE(isDevicelessName("Metal"));
	CHECK_FALSE(isDevicelessName("Vulkan"));
	CHECK_FALSE(isDevicelessName("GL3Plus"));
	CHECK_FALSE(isDevicelessName("GL"));
	CHECK_FALSE(isDevicelessName("GLES2"));
	// unset / empty is a graphics boot, not a headless one
	CHECK_FALSE(isDevicelessName(""));
	CHECK_FALSE(isDevicelessName("   "));
	// near-misses stay graphics names: only the exact words count
	CHECK_FALSE(isDevicelessName("nullish"));
	CHECK_FALSE(isDevicelessName("no"));
	CHECK_FALSE(isDevicelessName("none"));
	CHECK_FALSE(isDevicelessName("head"));
	CHECK_FALSE(isDevicelessName("NULL RenderSystem"));
}

TEST_CASE("the flavor answers honestly whether it can go deviceless",
	"[engine][render][deviceless]")
{
#if defined(ORKIGE_RENDER_NEXT)
	// this flavor links the deviceless render system beside its graphics one
	CHECK(devicelessAvailable());
#else
	// the classic flavor carries no deviceless render system, and says so
	// rather than booting a window nobody asked for
	CHECK_FALSE(devicelessAvailable());
#endif
}
