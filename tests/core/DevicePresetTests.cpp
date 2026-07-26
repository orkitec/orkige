/**************************************************************
	created:	2026/07/24 at 12:30
	filename: 	DevicePresetTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless device-preset policy for the Preview panel: the
	real-device preset table is self-consistent (orientation matches
	the dimensions, notch/punch presets carry insets), the token
	dialect round-trips, and the pure device-FRAME geometry encloses
	the screen and places its cutout/home-button/indicator correctly
	(the connected notch flush to the bezel, the detached island pill,
	the home-button chin, the ~1/3-width indicator). The rendered
	proof is the editor_game_preview / editor_previews selfchecks.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/DevicePreset.h>

#include <string>

using namespace Orkige;

TEST_CASE("DevicePreset: the table is self-consistent", "[devicepreset]")
{
	CHECK(DevicePreset::count() == DevicePreset::DP_COUNT);
	for(int i = 0; i < DevicePreset::count(); ++i)
	{
		const DevicePreset::Preset& preset =
			DevicePreset::forKind(static_cast<DevicePreset::Kind>(i));
		INFO("preset " << preset.token);
		CHECK(preset.label != nullptr);
		CHECK(std::string(preset.token).size() > 0);
		CHECK_FALSE((preset.panelSized && preset.custom));
		if(!preset.panelSized && !preset.custom)
		{
			CHECK(preset.width > 0u);
			CHECK(preset.height > 0u);
			CHECK(preset.contentScale > 0.0f);
		}
	}
}

TEST_CASE("DevicePreset: Free is panel-sized, Custom is caller-sized, neither frames",
	"[devicepreset]")
{
	const DevicePreset::Preset& free = DevicePreset::forKind(DevicePreset::DP_FREE);
	CHECK(free.panelSized);
	CHECK_FALSE(free.frame.framed);
	const DevicePreset::Preset& custom =
		DevicePreset::forKind(DevicePreset::DP_CUSTOM);
	CHECK(custom.custom);
	CHECK_FALSE(custom.frame.framed);
}

TEST_CASE("DevicePreset: orientation matches the dimensions", "[devicepreset]")
{
	auto portrait = [](DevicePreset::Kind k)
	{
		const DevicePreset::Preset& p = DevicePreset::forKind(k);
		return p.height > p.width;
	};
	auto landscape = [](DevicePreset::Kind k)
	{
		const DevicePreset::Preset& p = DevicePreset::forKind(k);
		return p.width > p.height;
	};
	CHECK(portrait(DevicePreset::DP_IPHONE_SE));
	CHECK(portrait(DevicePreset::DP_IPHONE_NOTCH));
	CHECK(landscape(DevicePreset::DP_IPHONE_NOTCH_LANDSCAPE));
	CHECK(portrait(DevicePreset::DP_IPHONE_ISLAND));
	CHECK(portrait(DevicePreset::DP_ANDROID_COMPACT));
	CHECK(portrait(DevicePreset::DP_IPAD_PRO_129));
	CHECK(landscape(DevicePreset::DP_IPAD_LANDSCAPE));
	CHECK(portrait(DevicePreset::DP_GALAXY_FOLD_COVER));
	CHECK(portrait(DevicePreset::DP_GALAXY_FOLD_MAIN));
	CHECK(portrait(DevicePreset::DP_GALAXY_FLIP));
}

TEST_CASE("DevicePreset: the fold cover is tall and the unfolded is near-square",
	"[devicepreset]")
{
	const DevicePreset::Preset& cover =
		DevicePreset::forKind(DevicePreset::DP_GALAXY_FOLD_COVER);
	const DevicePreset::Preset& main =
		DevicePreset::forKind(DevicePreset::DP_GALAXY_FOLD_MAIN);
	const float coverAspect = float(cover.width) / float(cover.height);
	const float mainAspect = float(main.width) / float(main.height);
	CHECK(coverAspect < 0.5f);					// a tall narrow strip
	CHECK(mainAspect > 0.7f);					// ~6:5 near-square
	CHECK(mainAspect < 0.95f);
}

TEST_CASE("DevicePreset: cutout presets carry safe-area insets", "[devicepreset]")
{
	auto topInset = [](DevicePreset::Kind k)
	{
		return DevicePreset::forKind(k).insets.mTop;
	};
	CHECK(topInset(DevicePreset::DP_IPHONE_NOTCH) > 0u);
	CHECK(topInset(DevicePreset::DP_IPHONE_ISLAND) >
		topInset(DevicePreset::DP_IPHONE_NOTCH));	// the island sits lower
	CHECK(topInset(DevicePreset::DP_ANDROID_COMPACT) > 0u);
	// the SE (home button) has a status-bar top inset but NO bottom home bar
	CHECK(DevicePreset::forKind(DevicePreset::DP_IPHONE_SE).insets.mBottom == 0u);
}

TEST_CASE("DevicePreset: the token dialect round-trips", "[devicepreset]")
{
	for(int i = 0; i < DevicePreset::count(); ++i)
	{
		const DevicePreset::Kind kind = static_cast<DevicePreset::Kind>(i);
		DevicePreset::Kind parsed = DevicePreset::DP_FREE;
		REQUIRE(DevicePreset::parseToken(DevicePreset::tokenFor(kind), parsed));
		CHECK(parsed == kind);
	}
	DevicePreset::Kind upper = DevicePreset::DP_FREE;
	CHECK(DevicePreset::parseToken("iPhone_Notch", upper));
	CHECK(upper == DevicePreset::DP_IPHONE_NOTCH);
	DevicePreset::Kind untouched = DevicePreset::DP_IPAD;
	CHECK_FALSE(DevicePreset::parseToken("nonsense", untouched));
	CHECK(untouched == DevicePreset::DP_IPAD);
}

TEST_CASE("DevicePreset: a frameless preset echoes the screen rect",
	"[devicepreset]")
{
	const DevicePreset::Preset& free = DevicePreset::forKind(DevicePreset::DP_FREE);
	const DevicePreset::FrameGeometry g =
		DevicePreset::deriveFrame(free, 10.0f, 20.0f, 300.0f, 200.0f);
	CHECK_FALSE(g.framed);
	CHECK(g.screenX == 10.0f);
	CHECK(g.screenW == 300.0f);
	CHECK(g.cutout == DevicePreset::CUT_NONE);
}

TEST_CASE("DevicePreset: a notch phone's notch connects to the top bezel",
	"[devicepreset]")
{
	const DevicePreset::Preset& notch =
		DevicePreset::forKind(DevicePreset::DP_IPHONE_NOTCH);
	const float sx = 100.0f, sy = 50.0f, sw = 200.0f, sh = 420.0f;
	const DevicePreset::FrameGeometry g =
		DevicePreset::deriveFrame(notch, sx, sy, sw, sh);
	REQUIRE(g.framed);
	CHECK(g.bezelX < sx);
	CHECK(g.bezelY < sy);
	CHECK(g.bezelX + g.bezelW > sx + sw);
	CHECK(g.bezelY + g.bezelH > sy + sh);
	CHECK(g.screenRadius > 0.0f);
	CHECK(g.bezelRadius > g.screenRadius);
	// the notch is FLUSH with the top edge (connects to the bezel, not floating)
	CHECK(g.cutout == DevicePreset::CUT_NOTCH);
	CHECK(g.notchOnTop);
	CHECK(g.cutoutY == sy);
	CHECK(g.cutoutX > sx);
	CHECK(g.cutoutX + g.cutoutW < sx + sw);
	// the home indicator is ~1/3 the screen WIDTH (not the long edge), centred
	REQUIRE(g.hasIndicator);
	CHECK(g.indicatorW > sw * 0.25f);
	CHECK(g.indicatorW < sw * 0.45f);
	CHECK(g.indicatorX > sx);
	CHECK(g.indicatorX + g.indicatorW < sx + sw);
}

TEST_CASE("DevicePreset: a landscape notch hugs the left edge as a side band",
	"[devicepreset]")
{
	const DevicePreset::Preset& notch =
		DevicePreset::forKind(DevicePreset::DP_IPHONE_NOTCH_LANDSCAPE);
	const float sx = 0.0f, sy = 0.0f, sw = 400.0f, sh = 200.0f;
	const DevicePreset::FrameGeometry g =
		DevicePreset::deriveFrame(notch, sx, sy, sw, sh);
	REQUIRE(g.framed);
	CHECK(g.cutout == DevicePreset::CUT_NOTCH);
	CHECK_FALSE(g.notchOnTop);
	CHECK(g.cutoutX == sx);
	CHECK(g.cutoutW < g.cutoutH);				// a vertical side band
}

TEST_CASE("DevicePreset: the Dynamic Island is a detached pill below the top",
	"[devicepreset]")
{
	const DevicePreset::Preset& island =
		DevicePreset::forKind(DevicePreset::DP_IPHONE_ISLAND);
	const float sx = 0.0f, sy = 0.0f, sw = 200.0f, sh = 440.0f;
	const DevicePreset::FrameGeometry g =
		DevicePreset::deriveFrame(island, sx, sy, sw, sh);
	REQUIRE(g.framed);
	CHECK(g.cutout == DevicePreset::CUT_PILL);
	CHECK(g.cutoutY > sy);						// DETACHED - a gap below the top
	CHECK(g.cutoutW > g.cutoutH);				// wider than tall
	CHECK(g.cutoutRadius * 2.0f == g.cutoutH);	// fully rounded (a pill)
}

TEST_CASE("DevicePreset: a punch-hole draws a round cutout near the top",
	"[devicepreset]")
{
	const DevicePreset::Preset& punch =
		DevicePreset::forKind(DevicePreset::DP_ANDROID_COMPACT);
	const float sx = 0.0f, sy = 0.0f, sw = 200.0f, sh = 440.0f;
	const DevicePreset::FrameGeometry g =
		DevicePreset::deriveFrame(punch, sx, sy, sw, sh);
	REQUIRE(g.framed);
	CHECK(g.cutout == DevicePreset::CUT_PUNCHHOLE);
	CHECK(g.cutoutW == g.cutoutH);
	CHECK(g.cutoutRadius * 2.0f == g.cutoutW);
	CHECK(g.cutoutY > sy);
	CHECK(g.cutoutY + g.cutoutH < sy + sh * 0.5f);
}

TEST_CASE("DevicePreset: a home-button phone has a chin + a button, no cutout",
	"[devicepreset]")
{
	const DevicePreset::Preset& se =
		DevicePreset::forKind(DevicePreset::DP_IPHONE_SE);
	const float sx = 0.0f, sy = 0.0f, sw = 200.0f, sh = 356.0f;	// 16:9
	const DevicePreset::FrameGeometry g =
		DevicePreset::deriveFrame(se, sx, sy, sw, sh);
	REQUIRE(g.framed);
	CHECK(g.cutout == DevicePreset::CUT_NONE);
	// a home-button phone has a thicker forehead + chin (asymmetric bezel)
	const float topBezel = sy - g.bezelY;
	const float bottomBezel = (g.bezelY + g.bezelH) - (sy + sh);
	const float sideBezel = sx - g.bezelX;
	CHECK(topBezel > sideBezel * 2.0f);
	CHECK(bottomBezel > sideBezel * 2.0f);
	CHECK(bottomBezel > topBezel);				// the chin is the tallest
	// the physical home button sits in the chin, centred, no home indicator
	REQUIRE(g.hasHomeButton);
	CHECK_FALSE(g.hasIndicator);
	CHECK(g.homeButtonRadius > 0.0f);
	CHECK(g.homeButtonY > sy + sh);				// below the screen, in the chin
}

TEST_CASE("DevicePreset: the frame scales proportionately with the screen",
	"[devicepreset]")
{
	const DevicePreset::Preset& notch =
		DevicePreset::forKind(DevicePreset::DP_IPHONE_NOTCH);
	const DevicePreset::FrameGeometry smallFrame =
		DevicePreset::deriveFrame(notch, 0.0f, 0.0f, 100.0f, 210.0f);
	const DevicePreset::FrameGeometry big =
		DevicePreset::deriveFrame(notch, 0.0f, 0.0f, 200.0f, 420.0f);
	const float smallBezel = smallFrame.screenX - smallFrame.bezelX;
	const float bigBezel = big.screenX - big.bezelX;
	CHECK(bigBezel > smallBezel * 1.9f);
	CHECK(bigBezel < smallBezel * 2.1f);
	CHECK(big.cutoutW > smallFrame.cutoutW * 1.9f);
	CHECK(big.cutoutW < smallFrame.cutoutW * 2.1f);
}

TEST_CASE("DevicePreset: the picture-in-picture inset stays inside the panel",
	"[devicepreset]")
{
	float x = 0, y = 0, w = 0, h = 0;
	DevicePreset::insetRect(800.0f, 600.0f, 16.0f / 9.0f, 0.25f, 10.0f,
		x, y, w, h);
	CHECK(w > 0.0f);
	CHECK(h > 0.0f);
	CHECK(x + w <= 800.0f - 10.0f + 0.01f);
	CHECK(y + h <= 600.0f - 10.0f + 0.01f);
	CHECK(x >= 0.0f);
	CHECK(y >= 0.0f);
	CHECK(w / h > 1.7f);
	CHECK(w / h < 1.8f);

	float tx = 0, ty = 0, tw = 0, th = 0;
	DevicePreset::insetRect(400.0f, 300.0f, 0.1f, 0.9f, 5.0f, tx, ty, tw, th);
	CHECK(th <= 300.0f);
	CHECK(ty >= 0.0f);
	CHECK(tx >= 0.0f);
}
