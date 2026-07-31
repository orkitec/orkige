/********************************************************************
	created:	Friday 2026/07/31 at 16:00
	filename: 	ExportIconsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	App-icon generation. The size tables are what the platform tooling reads by
	NAME - iconutil wants exactly these ten file names, and a bundle's
	CFBundleIconFiles list has to match the files beside it - so both the names
	and the produced pixel sizes are asserted rather than assumed.

	The source resolution is the other half: a project that names an icon which
	is not there must still export, with an icon, and say what happened. An
	export that fails because someone renamed a PNG would be the wrong trade.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportFiles.h"
#include "ExportIcons.h"
#include "ExportImage.h"
#include "ExportProject.h"

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
				("orkige_icons_test_" + name)).string();
			ExportFiles::removeTree(this->path, 0);
			ExportFiles::makeDirectories(this->path, 0);
		}
		~ScratchDir() { ExportFiles::removeTree(this->path, 0); }
	};

	//! a deterministic non-uniform square source (a diagonal colour ramp), so
	//! the resizes exercise real downscaling rather than copying one colour
	ExportImage rampSource(int side)
	{
		ExportImage image(side, side);
		for(int y = 0; y < side; ++y)
		{
			for(int x = 0; x < side; ++x)
			{
				const std::size_t index =
					(static_cast<std::size_t>(y) * side + x) * 4;
				image.pixels[index] = static_cast<unsigned char>(x * 255 / side);
				image.pixels[index + 1] =
					static_cast<unsigned char>(y * 255 / side);
				image.pixels[index + 2] =
					static_cast<unsigned char>((x + y) * 255 / (2 * side));
				image.pixels[index + 3] = 255;
			}
		}
		return image;
	}

	int decodedWidth(Orkige::String const & path)
	{
		ExportImage image;
		Orkige::String error;
		REQUIRE(decodeImageFile(path, image, &error));
		return image.width;
	}
}

TEST_CASE("the macOS iconset carries the names iconutil expects",
	"[unit][export]")
{
	ScratchDir scratch("macos");
	const Orkige::String iconset =
		ExportFiles::join(scratch.path, "App.iconset");
	REQUIRE(makeMacosIconset(rampSource(256), iconset, 0));

	const std::vector<IconEntry> entries = macosIconsetEntries();
	CHECK(entries.size() == 10);
	for(IconEntry const & entry : entries)
	{
		INFO(entry.fileName);
		const Orkige::String path =
			ExportFiles::join(iconset, entry.fileName);
		REQUIRE(ExportFiles::isRegularFile(path));
		CHECK(decodedWidth(path) == entry.size);
	}
}

TEST_CASE("the iOS icons report the names for CFBundleIconFiles",
	"[unit][export]")
{
	ScratchDir scratch("ios");
	std::vector<Orkige::String> names;
	REQUIRE(makeIosIcons(rampSource(256), scratch.path, &names, 0));

	const std::vector<IconEntry> entries = iosIconEntries();
	REQUIRE(names.size() == entries.size());
	for(std::size_t index = 0; index < entries.size(); ++index)
	{
		// the reported name is what the plist lists (sans .png), so it has to
		// be the file that is actually there
		CHECK(names[index] == entries[index].fileName);
		const Orkige::String path =
			ExportFiles::join(scratch.path, entries[index].fileName);
		REQUIRE(ExportFiles::isRegularFile(path));
		CHECK(decodedWidth(path) == entries[index].size);
	}
}

TEST_CASE("the Android mipmaps land at the five legacy densities",
	"[unit][export]")
{
	ScratchDir scratch("android");
	const Orkige::String res = ExportFiles::join(scratch.path, "res");
	REQUIRE(makeAndroidMipmaps(rampSource(256), res, 0));

	for(IconEntry const & entry : androidMipmapEntries())
	{
		INFO(entry.fileName);
		const Orkige::String path = ExportFiles::join(
			ExportFiles::join(res, "mipmap-" + entry.fileName),
			"ic_launcher.png");
		REQUIRE(ExportFiles::isRegularFile(path));
		CHECK(decodedWidth(path) == entry.size);
	}
}

TEST_CASE("an icon source is squared and size-checked", "[unit][export]")
{
	ScratchDir scratch("source");
	const Orkige::String wide = ExportFiles::join(scratch.path, "wide.png");
	ExportImage rectangle(200, 100);
	REQUIRE(encodePngFile(rectangle, wide, 0));

	ExportImage square;
	Orkige::String error;
	REQUIRE(loadSquareIconSource(wide, square, &error));
	CHECK(square.width == 100);
	CHECK(square.height == 100);

	// too small to produce the sizes an icon set needs
	const Orkige::String tiny = ExportFiles::join(scratch.path, "tiny.png");
	REQUIRE(encodePngFile(ExportImage(32, 32), tiny, 0));
	error.clear();
	CHECK_FALSE(loadSquareIconSource(tiny, square, &error));
	CHECK_FALSE(error.empty());

	error.clear();
	CHECK_FALSE(loadSquareIconSource(
		ExportFiles::join(scratch.path, "absent.png"), square, &error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("the icon source resolves and degrades honestly", "[unit][export]")
{
	ScratchDir scratch("resolve");
	std::vector<Orkige::String> messages;
	auto log = [&messages](Orkige::String const & message)
	{
		messages.push_back(message);
	};
	const Orkige::String fallback =
		ExportFiles::join(scratch.path, "default.png");

	ExportProject project;
	project.root = scratch.path;
	project.name = "Game";

	// no export.icon: the engine default, said out loud so it is not a surprise
	CHECK(resolveIconSource(project, fallback, log) == fallback);
	REQUIRE(messages.size() == 1);
	CHECK(messages[0].find("engine default") != Orkige::String::npos);

	// a present export.icon wins
	const Orkige::String custom = ExportFiles::join(scratch.path, "brand.png");
	REQUIRE(encodePngFile(ExportImage(64, 64), custom, 0));
	project.settings["export.icon"] = "brand.png";
	messages.clear();
	CHECK(resolveIconSource(project, fallback, log) ==
		ExportFiles::absolute(custom));

	// a set-but-MISSING export.icon warns and falls back - an app should still
	// ship an icon, so this is never a reason to fail the export
	project.settings["export.icon"] = "gone.png";
	messages.clear();
	CHECK(resolveIconSource(project, fallback, log) == fallback);
	REQUIRE(messages.size() == 1);
	CHECK(messages[0].find("WARNING") != Orkige::String::npos);
	CHECK(messages[0].find("gone.png") != Orkige::String::npos);
}

TEST_CASE("every icon size comes off the source", "[unit][export]")
{
	// a chained downscale (1024 -> 512 -> ... -> 16) compounds rounding; each
	// entry must be one area-average from the original instead
	ScratchDir scratch("chain");
	const ExportImage source = rampSource(256);
	std::vector<Orkige::String> names;
	REQUIRE(writeIconSizes(source, scratch.path,
		{ { "a.png", 128 }, { "b.png", 16 } }, &names, 0));
	REQUIRE(names.size() == 2);

	ExportImage direct = downscaleImage(source, 16, 16);
	ExportImage written;
	Orkige::String error;
	REQUIRE(decodeImageFile(ExportFiles::join(scratch.path, "b.png"), written,
		&error));
	CHECK(written.pixels == direct.pixels);
}
