// VectorShapeRasterTests.cpp - the pure CPU shape-thumbnail rasterizer:
// a known opaque triangle fills the expected pixels with the exact flat
// colour and leaves the outside transparent, per-vertex alpha interpolates
// (the feather ramp), and a degenerate mesh stays blank.
#include <catch2/catch_test_macros.hpp>

#include "core_util/VectorShapeRaster.h"
#include "core_util/VectorTessellator.h"

#include <algorithm>
#include <vector>

using Orkige::VectorShapeRaster;
using Orkige::VectorTessellator;

namespace
{
	//! build a single-triangle mesh with a fill colour per vertex + bounds
	VectorTessellator::Mesh makeTriangle(
		VectorTessellator::Colour const& c0,
		VectorTessellator::Colour const& c1,
		VectorTessellator::Colour const& c2)
	{
		VectorTessellator::Mesh mesh;
		mesh.positions = {
			VectorTessellator::Point(-1.0f, -1.0f),
			VectorTessellator::Point(1.0f, -1.0f),
			VectorTessellator::Point(0.0f, 1.0f)
		};
		mesh.colours = { c0, c1, c2 };
		mesh.indices = { 0, 1, 2 };
		// bounds directly over the three points
		mesh.bounds.minX = -1.0f; mesh.bounds.maxX = 1.0f;
		mesh.bounds.minY = -1.0f; mesh.bounds.maxY = 1.0f;
		mesh.bounds.valid = true;
		return mesh;
	}

	unsigned char toByte(float v)
	{
		if (v < 0.0f) v = 0.0f;
		if (v > 1.0f) v = 1.0f;
		return static_cast<unsigned char>(v * 255.0f + 0.5f);
	}
}

TEST_CASE("VectorShapeRaster fills an opaque triangle with its exact flat "
	"colour and leaves the outside transparent", "[vector][raster]")
{
	const VectorTessellator::Colour fill(0.8f, 0.4f, 0.2f, 1.0f);
	const VectorTessellator::Mesh mesh = makeTriangle(fill, fill, fill);

	const int W = 32, H = 32;
	std::vector<unsigned char> pixels(static_cast<std::size_t>(W) * H * 4, 0xAB);
	VectorShapeRaster::rasterize(mesh, W, H, pixels.data());

	int covered = 0;
	int transparent = 0;
	for (std::size_t i = 0; i < pixels.size(); i += 4)
	{
		const unsigned char a = pixels[i + 3];
		if (a == 0)
		{
			// a transparent pixel must be fully cleared (all channels 0)
			CHECK(pixels[i] == 0);
			CHECK(pixels[i + 1] == 0);
			CHECK(pixels[i + 2] == 0);
			++transparent;
		}
		else
		{
			// a covered pixel is the exact flat fill (no edge AA, opaque)
			CHECK(a == 255);
			CHECK(pixels[i] == toByte(0.8f));
			CHECK(pixels[i + 1] == toByte(0.4f));
			CHECK(pixels[i + 2] == toByte(0.2f));
			++covered;
		}
	}
	CHECK(covered > 0);        // the triangle filled a real area
	CHECK(transparent > 0);    // and the corners/margin stayed clear

	// the top-left corner pixel sits in the margin, outside the triangle
	CHECK(pixels[3] == 0);
}

TEST_CASE("VectorShapeRaster interpolates per-vertex alpha (the feather ramp)",
	"[vector][raster]")
{
	// two bottom vertices opaque, the top vertex fully transparent -> the fill
	// alpha ramps from 255 at the bottom edge to 0 at the top vertex
	const VectorTessellator::Colour opaque(0.5f, 0.6f, 0.7f, 1.0f);
	const VectorTessellator::Colour clear(0.5f, 0.6f, 0.7f, 0.0f);
	const VectorTessellator::Mesh mesh = makeTriangle(opaque, opaque, clear);

	const int W = 48, H = 48;
	std::vector<unsigned char> pixels(static_cast<std::size_t>(W) * H * 4, 0);
	VectorShapeRaster::rasterize(mesh, W, H, pixels.data());

	bool sawIntermediate = false;
	unsigned char maxAlpha = 0;
	unsigned char minCoveredAlpha = 255;
	for (std::size_t i = 0; i < pixels.size(); i += 4)
	{
		const unsigned char a = pixels[i + 3];
		if (a > 0)
		{
			maxAlpha = std::max(maxAlpha, a);
			minCoveredAlpha = std::min(minCoveredAlpha, a);
			if (a > 0 && a < 255)
			{
				sawIntermediate = true;
			}
		}
	}
	CHECK(sawIntermediate);        // proves the alpha ramp, not a hard mask
	// pixel centres sit just inside the opaque bottom edge, so the peak alpha
	// approaches but need not hit 255 - it stays near-solid
	CHECK(maxAlpha > 200);         // the opaque bottom edge stays near-solid
	CHECK(minCoveredAlpha < 200);  // and it fades toward the clear vertex
}

TEST_CASE("VectorShapeRaster leaves a degenerate mesh transparent",
	"[vector][raster]")
{
	VectorTessellator::Mesh empty; // no indices, invalid bounds
	const int W = 8, H = 8;
	std::vector<unsigned char> pixels(static_cast<std::size_t>(W) * H * 4, 0x77);
	VectorShapeRaster::rasterize(empty, W, H, pixels.data());
	for (unsigned char byte : pixels)
	{
		CHECK(byte == 0);
	}
}
