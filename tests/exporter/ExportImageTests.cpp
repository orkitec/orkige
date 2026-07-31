/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportImageTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	The image arithmetic every icon set and every cooked texture is built from.
	It is asserted on EXACT pixel values because both consumers are
	reproducibility promises: two machines packaging the same project must
	produce the same bytes, so an "about right" resize is a defect.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportFiles.h"
#include "ExportImage.h"

#include <filesystem>

using namespace OrkigeExport;

namespace
{
	struct ScratchDir
	{
		Orkige::String path;
		explicit ScratchDir(Orkige::String const & name)
		{
			this->path = (std::filesystem::temp_directory_path() /
				("orkige_image_test_" + name)).string();
			ExportFiles::removeTree(this->path, 0);
			ExportFiles::makeDirectories(this->path, 0);
		}
		~ScratchDir() { ExportFiles::removeTree(this->path, 0); }
	};

	ExportImage solid(int width, int height, unsigned char red,
		unsigned char green, unsigned char blue, unsigned char alpha)
	{
		ExportImage image(width, height);
		for(std::size_t index = 0; index + 3 < image.pixels.size(); index += 4)
		{
			image.pixels[index] = red;
			image.pixels[index + 1] = green;
			image.pixels[index + 2] = blue;
			image.pixels[index + 3] = alpha;
		}
		return image;
	}

	void pixelAt(ExportImage const & image, int x, int y, unsigned char * rgba)
	{
		const std::size_t index =
			(static_cast<std::size_t>(y) * image.width + x) * 4;
		for(int channel = 0; channel < 4; ++channel)
		{
			rgba[channel] = image.pixels[index + channel];
		}
	}
}

TEST_CASE("fitWithin caps the longest side keeping aspect", "[unit][export]")
{
	int width = 0;
	int height = 0;
	// 0 or negative means uncapped, and an image already within the cap is
	// left exactly alone (no resample, no drift)
	fitWithin(100, 50, 0, width, height);
	CHECK((width == 100 && height == 50));
	fitWithin(100, 50, 200, width, height);
	CHECK((width == 100 && height == 50));

	fitWithin(100, 50, 50, width, height);
	CHECK((width == 50 && height == 25));
	fitWithin(50, 100, 50, width, height);
	CHECK((width == 25 && height == 50));
	fitWithin(64, 64, 16, width, height);
	CHECK((width == 16 && height == 16));

	// an extreme aspect never collapses a side to zero
	fitWithin(1000, 3, 100, width, height);
	CHECK((width == 100 && height == 1));
}

TEST_CASE("downscale area-averages", "[unit][export]")
{
	// a 2x2 of four known colours averages to their exact mean at 1x1
	ExportImage quad(2, 2);
	const unsigned char values[4][4] = {
		{ 0, 0, 0, 255 }, { 100, 100, 100, 255 },
		{ 200, 200, 200, 255 }, { 255, 255, 255, 255 } };
	for(int index = 0; index < 4; ++index)
	{
		for(int channel = 0; channel < 4; ++channel)
		{
			quad.pixels[index * 4 + channel] = values[index][channel];
		}
	}
	const ExportImage one = downscaleImage(quad, 1, 1);
	REQUIRE(one.valid());
	unsigned char rgba[4] = {};
	pixelAt(one, 0, 0, rgba);
	CHECK(static_cast<int>(rgba[0]) == (0 + 100 + 200 + 255) / 4);
	CHECK(static_cast<int>(rgba[3]) == 255);

	// a uniform image stays exactly itself at every size - averaging must not
	// introduce drift
	const ExportImage flat = solid(64, 64, 200, 100, 50, 128);
	const ExportImage shrunk = downscaleImage(flat, 16, 16);
	REQUIRE(shrunk.valid());
	CHECK(shrunk.width == 16);
	CHECK(shrunk.height == 16);
	pixelAt(shrunk, 8, 8, rgba);
	CHECK(static_cast<int>(rgba[0]) == 200);
	CHECK(static_cast<int>(rgba[1]) == 100);
	CHECK(static_cast<int>(rgba[2]) == 50);
	CHECK(static_cast<int>(rgba[3]) == 128);

	// an unchanged size is a copy, not a resample
	const ExportImage same = downscaleImage(flat, 64, 64);
	CHECK(same.pixels == flat.pixels);
}

TEST_CASE("premultiply folds alpha into rgb", "[unit][export]")
{
	ExportImage image = solid(4, 4, 200, 100, 50, 128);
	premultiplyImage(image);
	unsigned char rgba[4] = {};
	pixelAt(image, 2, 2, rgba);
	CHECK(static_cast<int>(rgba[0]) == 200 * 128 / 255);
	CHECK(static_cast<int>(rgba[1]) == 100 * 128 / 255);
	CHECK(static_cast<int>(rgba[2]) == 50 * 128 / 255);
	CHECK(static_cast<int>(rgba[3]) == 128);	// alpha itself is untouched

	// a fully opaque texel is left exactly alone (the common case must be a
	// no-op, not a rounding trip)
	ExportImage opaque = solid(2, 2, 200, 100, 50, 255);
	const std::vector<unsigned char> before = opaque.pixels;
	premultiplyImage(opaque);
	CHECK(opaque.pixels == before);
}

TEST_CASE("imageHasAlpha drives the compressed-format choice", "[unit][export]")
{
	CHECK_FALSE(imageHasAlpha(solid(4, 4, 10, 20, 30, 255)));
	CHECK(imageHasAlpha(solid(4, 4, 10, 20, 30, 254)));
	ExportImage mostlyOpaque = solid(4, 4, 10, 20, 30, 255);
	mostlyOpaque.pixels[7] = 0;	// one transparent texel is enough
	CHECK(imageHasAlpha(mostlyOpaque));
}

TEST_CASE("cropToSquare takes the centred square", "[unit][export]")
{
	// a wide source crops to its height
	ExportImage wide(200, 100);
	const ExportImage cropped = cropToSquare(wide);
	CHECK(cropped.width == 100);
	CHECK(cropped.height == 100);

	// a tall source crops to its width
	ExportImage tall(60, 90);
	const ExportImage narrow = cropToSquare(tall);
	CHECK(narrow.width == 60);
	CHECK(narrow.height == 60);

	// a square source is returned unchanged
	const ExportImage square = solid(32, 32, 1, 2, 3, 255);
	CHECK(cropToSquare(square).pixels == square.pixels);
}

TEST_CASE("crop keeps the centre pixels", "[unit][export]")
{
	// left half black, right half white: cropping a 4x2 to 2x2 must take the
	// MIDDLE two columns, one of each
	ExportImage strip(4, 2);
	for(int y = 0; y < 2; ++y)
	{
		for(int x = 0; x < 4; ++x)
		{
			const std::size_t index =
				(static_cast<std::size_t>(y) * 4 + x) * 4;
			const unsigned char value = (x < 2) ? 0 : 255;
			strip.pixels[index] = value;
			strip.pixels[index + 1] = value;
			strip.pixels[index + 2] = value;
			strip.pixels[index + 3] = 255;
		}
	}
	const ExportImage cropped = cropToSquare(strip);
	REQUIRE(cropped.width == 2);
	unsigned char rgba[4] = {};
	pixelAt(cropped, 0, 0, rgba);
	CHECK(static_cast<int>(rgba[0]) == 0);
	pixelAt(cropped, 1, 0, rgba);
	CHECK(static_cast<int>(rgba[0]) == 255);
}

TEST_CASE("a PNG round-trips through the writer and the decoder",
	"[unit][export]")
{
	ScratchDir scratch("png");
	const Orkige::String path = ExportFiles::join(scratch.path, "a.png");

	// a non-uniform image, so a channel swap or a stride bug cannot pass
	ExportImage source(8, 5);
	for(int y = 0; y < 5; ++y)
	{
		for(int x = 0; x < 8; ++x)
		{
			const std::size_t index =
				(static_cast<std::size_t>(y) * 8 + x) * 4;
			source.pixels[index] = static_cast<unsigned char>(x * 30);
			source.pixels[index + 1] = static_cast<unsigned char>(y * 50);
			source.pixels[index + 2] = 128;
			source.pixels[index + 3] =
				static_cast<unsigned char>(255 - x * 10);
		}
	}
	Orkige::String error;
	REQUIRE(encodePngFile(source, path, &error));

	ExportImage decoded;
	REQUIRE(decodeImageFile(path, decoded, &error));
	CHECK(decoded.width == 8);
	CHECK(decoded.height == 5);
	CHECK(decoded.pixels == source.pixels);
}

TEST_CASE("a missing or undecodable image refuses", "[unit][export]")
{
	ScratchDir scratch("baddecode");
	ExportImage image;
	Orkige::String error;
	CHECK_FALSE(decodeImageFile(
		ExportFiles::join(scratch.path, "absent.png"), image, &error));
	CHECK_FALSE(error.empty());

	const Orkige::String junk = ExportFiles::join(scratch.path, "junk.png");
	REQUIRE(ExportFiles::writeTextFile(junk, "not a png at all", 0));
	error.clear();
	CHECK_FALSE(decodeImageFile(junk, image, &error));
	CHECK_FALSE(error.empty());

	// an empty image is not written silently
	error.clear();
	CHECK_FALSE(encodePngFile(ExportImage(),
		ExportFiles::join(scratch.path, "empty.png"), &error));
	CHECK_FALSE(error.empty());
}
