// Unit tests for the pure shelf allocator behind the runtime font atlas
// (engine_gui/FontPacker.h). No render system, no image data - just the
// geometry: placements stay in bounds, never overlap, and the page fills and
// then honestly refuses.
#include <catch2/catch_test_macros.hpp>

#include <engine_gui/FontPacker.h>

#include <vector>

using Orkige::FontPacker;

namespace
{
	//! do two placed rects share any pixel?
	bool overlaps(FontPacker::Rect const& a, FontPacker::Rect const& b)
	{
		return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
			a.y + a.h <= b.y || b.y + b.h <= a.y);
	}
}

TEST_CASE("font packer places boxes in bounds without overlap",
	"[engine][gui][fontpacker]")
{
	FontPacker packer(128, 128, 1);
	std::vector<FontPacker::Rect> placed;

	// a spread of box sizes (glyph cells + a few wider sprites)
	for (int i = 0; i < 40; ++i)
	{
		FontPacker::Rect r;
		const Orkige::uint w = 6u + Orkige::uint(i % 7) * 3u;
		const Orkige::uint h = 12u + Orkige::uint(i % 3) * 4u;
		REQUIRE(packer.allocate(w, h, r));
		CHECK(r.w == w);
		CHECK(r.h == h);
		// in bounds
		CHECK(r.x + r.w <= packer.width());
		CHECK(r.y + r.h <= packer.height());
		// disjoint from everything already placed
		for (FontPacker::Rect const& other : placed)
		{
			INFO("box " << i << " overlaps a previous placement");
			CHECK_FALSE(overlaps(r, other));
		}
		placed.push_back(r);
	}
}

TEST_CASE("font packer refuses a box that cannot fit",
	"[engine][gui][fontpacker]")
{
	FontPacker packer(32, 32, 1);
	FontPacker::Rect r;
	// larger than the page in either axis
	CHECK_FALSE(packer.allocate(40, 8, r));
	CHECK_FALSE(packer.allocate(8, 40, r));
	CHECK_FALSE(packer.allocate(0, 8, r));

	// fill the page top to bottom, then the next shelf must be refused
	FontPacker small(16, 16, 0);
	REQUIRE(small.allocate(16, 8, r));	// shelf 0 (y 0..8)
	REQUIRE(small.allocate(16, 8, r));	// shelf 1 (y 8..16)
	CHECK_FALSE(small.allocate(16, 8, r));	// no vertical room left
}

TEST_CASE("font packer fills a shelf then opens the next",
	"[engine][gui][fontpacker]")
{
	FontPacker packer(20, 100, 0);
	FontPacker::Rect a, b, c;
	REQUIRE(packer.allocate(10, 10, a));	// shelf 0, x 0
	REQUIRE(packer.allocate(10, 10, b));	// shelf 0, x 10 (fills the row)
	REQUIRE(packer.allocate(10, 10, c));	// row full -> shelf 1
	CHECK(a.y == b.y);						// same shelf
	CHECK(b.x == a.x + a.w);				// packed edge to edge
	CHECK(c.y == a.y + a.h);				// a new shelf below
}

TEST_CASE("font packer reset empties the page",
	"[engine][gui][fontpacker]")
{
	FontPacker packer(32, 32, 0);
	FontPacker::Rect r;
	REQUIRE(packer.allocate(32, 32, r));	// fills the page
	CHECK(packer.usedHeight() == 32u);
	CHECK_FALSE(packer.allocate(1, 1, r));	// full
	packer.reset();
	CHECK(packer.usedHeight() == 0u);
	CHECK(packer.allocate(32, 32, r));		// room again
}
