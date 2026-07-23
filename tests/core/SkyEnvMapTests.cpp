/**************************************************************
	created:	2026/07/19 at 10:30
	filename: 	SkyEnvMapTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless procedural-sky environment synthesis: the sky model reads
	blue-ish overhead and warm toward the sun, the cube face directions map
	to the canonical cubemap axes, the box-downsampled mip chain packs to the
	exact tight layout, and the recapture key fires on a material sun swing /
	colour change but not on a still sky. The rendered proof (a scene lit by
	the synthesized environment) is the render_facade_selfcheck image-lighting
	procedural leg per flavor.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/SkyEnvMap.h>
#include <core_util/AtmosphereDesc.h>
#include <core_util/AtmosphereSunDrive.h>

#include <cmath>
#include <vector>

using namespace Orkige;

namespace
{
	AtmosphereDesc dayDesc()
	{
		return AtmospherePreset::forSky(AtmospherePreset::SKY_DAY);
	}
}

TEST_CASE("SkyEnvMap: the sky is blue-ish overhead, warm toward the sun",
	"[skyenvmap]")
{
	const AtmosphereDesc desc = dayDesc();
	// a sun low toward +X, so straight-up is well away from the sun's glow
	const float sx = 1.0f, sy = 0.2f, sz = 0.0f;
	const float slen = std::sqrt(sx * sx + sy * sy + sz * sz);
	const float nsx = sx / slen, nsy = sy / slen, nsz = sz / slen;

	// straight up, away from the sun: the blue zenith tint dominates
	const SkyEnvMap::Colour zenith =
		SkyEnvMap::skyColour(0.0f, 1.0f, 0.0f, desc, nsx, nsy, nsz);
	// looking exactly at the sun: the glow whitens/warms it (red >= blue)
	const SkyEnvMap::Colour sun =
		SkyEnvMap::skyColour(nsx, nsy, nsz, desc, nsx, nsy, nsz);

	CHECK(zenith.b > zenith.r);
	CHECK(sun.r >= sun.b);			// the warm sun core is not blue-dominant
	CHECK(sun.r > zenith.r);		// the sun direction is redder than the blue zenith
}

TEST_CASE("SkyEnvMap: face directions map to the canonical cubemap axes",
	"[skyenvmap]")
{
	struct Expect { unsigned int face; float x, y, z; };
	const Expect centres[] = {
		{ 0,  1.0f,  0.0f,  0.0f },	// +X
		{ 1, -1.0f,  0.0f,  0.0f },	// -X
		{ 2,  0.0f,  1.0f,  0.0f },	// +Y
		{ 3,  0.0f, -1.0f,  0.0f },	// -Y
		{ 4,  0.0f,  0.0f,  1.0f },	// +Z
		{ 5,  0.0f,  0.0f, -1.0f },	// -Z
	};
	for(Expect const & each : centres)
	{
		float x, y, z;
		SkyEnvMap::faceDirection(each.face, 0.5f, 0.5f, x, y, z);
		CHECK(std::fabs(x - each.x) < 1e-5f);
		CHECK(std::fabs(y - each.y) < 1e-5f);
		CHECK(std::fabs(z - each.z) < 1e-5f);
		// every sampled direction is unit length
		CHECK(std::fabs(std::sqrt(x * x + y * y + z * z) - 1.0f) < 1e-5f);
	}
}

TEST_CASE("SkyEnvMap: the +Y face reads sky-blue, sun face reads warm",
	"[skyenvmap]")
{
	const AtmosphereDesc desc = dayDesc();
	const float sx = 1.0f, sy = 0.0f, sz = 0.0f;	// sun on +X
	const unsigned int edge = 8u;
	std::vector<unsigned char> up(edge * edge * 4u);
	std::vector<unsigned char> plusX(edge * edge * 4u);
	std::vector<unsigned char> minusX(edge * edge * 4u);
	SkyEnvMap::renderFaceRgba8(2u, edge, desc, sx, sy, sz, up.data());		// +Y
	SkyEnvMap::renderFaceRgba8(0u, edge, desc, sx, sy, sz, plusX.data());	// +X
	SkyEnvMap::renderFaceRgba8(1u, edge, desc, sx, sy, sz, minusX.data());	// -X

	const size_t c = ((edge / 2u) * edge + (edge / 2u)) * 4u;
	// the +Y face centre: blue channel dominates red (a blue-ish overhead sky)
	CHECK(up[c + 2] > up[c + 0]);
	// the +X face points at the sun, the -X face away from it: the sun glow
	// warms the +X face's red measurably above the away-facing -X face
	CHECK(plusX[c + 0] > minusX[c + 0]);
}

TEST_CASE("SkyEnvMap: the mip chain packs to the exact tight layout",
	"[skyenvmap]")
{
	const AtmosphereDesc desc = dayDesc();
	const unsigned int edge = 32u;
	std::vector<unsigned char> chain;
	unsigned int mips = 0u;
	SkyEnvMap::buildCubemapChainRgba8(edge, desc, 0.0f, 1.0f, 0.0f,
		chain, mips);

	CHECK(mips == SkyEnvMap::mipCountForEdge(edge));
	CHECK(mips == 6u);	// 32,16,8,4,2,1

	// the total size equals the sum of edge_m^2 * 4 * 6 across every mip, and
	// the last face's offset + its bytes is exactly the buffer end (no gaps,
	// no overrun - the layout contract both backends upload against)
	size_t expected = 0;
	for(unsigned int m = 0; m < mips; ++m)
	{
		const unsigned int e = edge >> m;
		expected += size_t(e) * e * 4u * 6u;
	}
	CHECK(chain.size() == expected);

	const unsigned int lastEdge = edge >> (mips - 1u);	// 1
	const size_t lastOffset =
		SkyEnvMap::faceMipOffset(edge, mips - 1u, 5u);
	CHECK(lastOffset + size_t(lastEdge) * lastEdge * 4u == chain.size());

	// face 0 offset at mip 0 is the very start
	CHECK(SkyEnvMap::faceMipOffset(edge, 0u, 0u) == 0u);
}

TEST_CASE("SkyEnvMap: box downsample averages a 2x2 block", "[skyenvmap]")
{
	// a 2x2 face with distinct values -> one texel of their average
	unsigned char src[2 * 2 * 4] = {
		/* (0,0) */ 40, 80, 120, 255,
		/* (0,1) */ 60, 80, 120, 255,
		/* (1,0) */ 40, 100, 120, 255,
		/* (1,1) */ 60, 100, 120, 255,
	};
	unsigned char dst[4] = { 0, 0, 0, 0 };
	SkyEnvMap::halveFaceRgba8(src, 2u, dst);
	CHECK(int(dst[0]) == 50);	// (40+60+40+60)/4
	CHECK(int(dst[1]) == 90);	// (80+80+100+100)/4
	CHECK(int(dst[2]) == 120);
	CHECK(int(dst[3]) == 255);
}

TEST_CASE("SkyEnvMap: the scaled chain preserves the HDR horizon's hue ratio",
	"[skyenvmap]")
{
	// the sunset look with a low sun: the sun-side horizon's unclamped model
	// radiance runs far past 1 with red dominant - the clamped chain flattens
	// it to equal R,G while the scaled chain must keep the ratio
	const AtmosphereDesc desc =
		AtmospherePreset::forSky(AtmospherePreset::SKY_SUNSET);
	const float sx = 0.0f, sy = 0.33f, sz = -0.944f;
	const unsigned int edge = 16u;

	std::vector<unsigned char> scaled;
	unsigned int mips = 0u;
	float scale = 1.0f;
	SkyEnvMap::buildCubemapChainScaledRgba8(edge, desc, sx, sy, sz,
		scaled, mips, scale);

	// the same tight layout as the clamped sibling
	std::vector<unsigned char> clamped;
	unsigned int clampedMips = 0u;
	SkyEnvMap::buildCubemapChainRgba8(edge, desc, sx, sy, sz,
		clamped, clampedMips);
	CHECK(mips == clampedMips);
	CHECK(scaled.size() == clamped.size());

	// this sky is HDR: the exposure runs well past 1
	CHECK(scale > 2.0f);

	// a toward-sun horizon texel: reconstruct byte * scale and compare the
	// hue ratio against the unclamped model (quantization tolerance); the
	// clamped sibling reads exactly 255,255 there (the flattening this
	// sibling exists to avoid). Row 6, column 8 of the -Z face (texel
	// centres at (i+0.5)/16) is a direction slightly above the horizon
	// toward the sun.
	const unsigned int texelRow = 6u, texelCol = 8u;
	float r = 0.0f, g = 0.0f, b = 0.0f;
	float dirX, dirY, dirZ;
	SkyEnvMap::faceDirection(5u,
		(texelCol + 0.5f) / edge, (texelRow + 0.5f) / edge,
		dirX, dirY, dirZ);
	AtmosphereSunDrive::skyModelColour(desc, sx, sy, sz, dirX, dirY, dirZ,
		false, r, g, b);
	REQUIRE(r > 1.0f);				// the texel the clamp would flatten
	REQUIRE(g > 1.0f);
	const float modelRatio = r / g;
	CHECK(modelRatio > 1.2f);		// red-dominant warm horizon

	const size_t texel = SkyEnvMap::faceMipOffset(edge, 0u, 5u) +
		(size_t(texelRow) * edge + texelCol) * 4u;
	const float storedR = scaled[texel + 0] / 255.0f * scale;
	const float storedG = scaled[texel + 1] / 255.0f * scale;
	REQUIRE(storedG > 0.0f);
	CHECK(std::fabs(storedR / storedG - modelRatio) < 0.1f);
	CHECK(std::fabs(storedR - r) < scale / 255.0f + 1e-3f);
	CHECK(int(clamped[texel + 0]) == 255);
	CHECK(int(clamped[texel + 1]) == 255);
}

TEST_CASE("SkyEnvMap: a bounded sky captures scale 1 and matches the clamped "
	"chain byte for byte", "[skyenvmap]")
{
	// the night look stays inside [0;1] everywhere: the scaled chain must
	// store it verbatim (scale exactly 1) so a dim sky loses no precision
	const AtmosphereDesc desc =
		AtmospherePreset::forSky(AtmospherePreset::SKY_NIGHT);
	const float sx = 0.0f, sy = 0.4f, sz = -0.9f;
	const unsigned int edge = 16u;

	std::vector<unsigned char> scaled;
	unsigned int mips = 0u;
	float scale = 0.0f;
	SkyEnvMap::buildCubemapChainScaledRgba8(edge, desc, sx, sy, sz,
		scaled, mips, scale);
	std::vector<unsigned char> clamped;
	unsigned int clampedMips = 0u;
	SkyEnvMap::buildCubemapChainRgba8(edge, desc, sx, sy, sz,
		clamped, clampedMips);

	CHECK(scale == 1.0f);
	CHECK(mips == clampedMips);
	CHECK(scaled == clamped);
}

TEST_CASE("SkyEnvMap: recapture fires on a material sun swing, not a still sky",
	"[skyenvmap]")
{
	const AtmosphereDesc desc = dayDesc();
	const float cosThreshold = std::cos(6.0f * 3.14159265f / 180.0f);

	const SkyEnvMap::CaptureKey noon = SkyEnvMap::keyFor(desc, 0.0f, 1.0f, 0.0f);

	// the identical sky does not recapture
	CHECK_FALSE(SkyEnvMap::materiallyDiffers(noon, noon, cosThreshold));

	// a tiny 1-degree sun nudge stays under the threshold (no recapture spam
	// on a slow arc frame-to-frame)
	const float tiny = 1.0f * 3.14159265f / 180.0f;
	const SkyEnvMap::CaptureKey nudged =
		SkyEnvMap::keyFor(desc, std::sin(tiny), std::cos(tiny), 0.0f);
	CHECK_FALSE(SkyEnvMap::materiallyDiffers(noon, nudged, cosThreshold));

	// a big 30-degree sun swing recaptures
	const float big = 30.0f * 3.14159265f / 180.0f;
	const SkyEnvMap::CaptureKey swung =
		SkyEnvMap::keyFor(desc, std::sin(big), std::cos(big), 0.0f);
	CHECK(SkyEnvMap::materiallyDiffers(noon, swung, cosThreshold));
}

TEST_CASE("SkyEnvMap: recapture fires when the sky colours change",
	"[skyenvmap]")
{
	const float cosThreshold = std::cos(6.0f * 3.14159265f / 180.0f);
	const SkyEnvMap::CaptureKey day =
		SkyEnvMap::keyFor(AtmospherePreset::forSky(AtmospherePreset::SKY_DAY),
			0.0f, 1.0f, 0.0f);
	// same sun, but the night look's tint/power - a material colour change
	const SkyEnvMap::CaptureKey night =
		SkyEnvMap::keyFor(AtmospherePreset::forSky(AtmospherePreset::SKY_NIGHT),
			0.0f, 1.0f, 0.0f);
	CHECK(SkyEnvMap::materiallyDiffers(day, night, cosThreshold));
}
