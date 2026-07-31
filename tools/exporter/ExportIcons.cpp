/********************************************************************
	created:	Friday 2026/07/31 at 16:00
	filename: 	ExportIcons.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportIcons.h"

#include "ExportFiles.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>

namespace OrkigeExport
{
	namespace
	{
		bool report(Orkige::String * error, Orkige::String const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
		//---------------------------------------------------------
		Orkige::String trimmed(Orkige::String const & text)
		{
			std::size_t first = 0;
			std::size_t last = text.size();
			while(first < last && std::isspace(
				static_cast<unsigned char>(text[first])) != 0)
			{
				++first;
			}
			while(last > first && std::isspace(
				static_cast<unsigned char>(text[last - 1])) != 0)
			{
				--last;
			}
			return text.substr(first, last - first);
		}
	}
	//---------------------------------------------------------
	std::vector<IconEntry> macosIconsetEntries()
	{
		return {
			{ "icon_16x16.png", 16 },		{ "icon_16x16@2x.png", 32 },
			{ "icon_32x32.png", 32 },		{ "icon_32x32@2x.png", 64 },
			{ "icon_128x128.png", 128 },	{ "icon_128x128@2x.png", 256 },
			{ "icon_256x256.png", 256 },	{ "icon_256x256@2x.png", 512 },
			{ "icon_512x512.png", 512 },	{ "icon_512x512@2x.png", 1024 },
		};
	}
	//---------------------------------------------------------
	std::vector<IconEntry> iosIconEntries()
	{
		// the simulator and a device both pick by scale from
		// CFBundleIconFiles; no asset catalog needed
		return {
			{ "AppIcon60x60@2x.png", 120 },
			{ "AppIcon60x60@3x.png", 180 },
			{ "AppIcon76x76@2x.png", 152 },
		};
	}
	//---------------------------------------------------------
	std::vector<IconEntry> androidMipmapEntries()
	{
		// adaptive icons need a vector foreground/background XML pair and are
		// deferred; the legacy PNG densities cover every launcher
		return { { "mdpi", 48 }, { "hdpi", 72 }, { "xhdpi", 96 },
			{ "xxhdpi", 144 }, { "xxxhdpi", 192 } };
	}
	//---------------------------------------------------------
	Orkige::String resolveIconSource(ExportProject const & project,
		Orkige::String const & defaultIconPath,
		std::function<void(Orkige::String const &)> const & log)
	{
		auto emit = [&log](Orkige::String const & message)
		{
			if(log)
			{
				log(message);
			}
		};
		const Orkige::String relative = trimmed(project.setting("export.icon"));
		if(!relative.empty())
		{
			const Orkige::String candidate =
				ExportFiles::join(project.root, relative);
			if(ExportFiles::isRegularFile(candidate))
			{
				emit("icon: " + relative);
				return ExportFiles::absolute(candidate);
			}
			// never fatal: an app should still ship an icon
			emit("WARNING: export.icon references '" + relative + "' but no "
				"such file exists - using the engine default icon");
		}
		else
		{
			emit("icon: engine default (set export.icon to override)");
		}
		return defaultIconPath;
	}
	//---------------------------------------------------------
	bool loadSquareIconSource(Orkige::String const & path, ExportImage & out,
		Orkige::String * error)
	{
		if(!ExportFiles::isRegularFile(path))
		{
			return report(error, "icon source '" + path + "' does not exist");
		}
		ExportImage image;
		if(!decodeImageFile(path, image, error))
		{
			return false;
		}
		const int side = std::min(image.width, image.height);
		if(side < 64)
		{
			return report(error, "icon source '" + path + "' is too small (" +
				std::to_string(image.width) + "x" +
				std::to_string(image.height) + ", need >= 64px)");
		}
		out = cropToSquare(image);
		return true;
	}
	//---------------------------------------------------------
	bool writeIconSizes(ExportImage const & source,
		Orkige::String const & outDirectory,
		std::vector<IconEntry> const & entries,
		std::vector<Orkige::String> * outNames, Orkige::String * error)
	{
		if(!ExportFiles::makeDirectories(outDirectory, error))
		{
			return false;
		}
		for(IconEntry const & entry : entries)
		{
			// every size comes off the SOURCE, never off an already-shrunk
			// step - chaining downscales compounds their rounding
			const ExportImage scaled =
				downscaleImage(source, entry.size, entry.size);
			if(!encodePngFile(scaled,
				ExportFiles::join(outDirectory, entry.fileName), error))
			{
				return false;
			}
			if(outNames != 0)
			{
				outNames->push_back(entry.fileName);
			}
		}
		return true;
	}
	//---------------------------------------------------------
	bool makeMacosIconset(ExportImage const & source,
		Orkige::String const & iconsetDirectory, Orkige::String * error)
	{
		return writeIconSizes(source, iconsetDirectory, macosIconsetEntries(),
			0, error);
	}
	//---------------------------------------------------------
	bool makeIosIcons(ExportImage const & source,
		Orkige::String const & bundleDirectory,
		std::vector<Orkige::String> * outNames, Orkige::String * error)
	{
		return writeIconSizes(source, bundleDirectory, iosIconEntries(),
			outNames, error);
	}
	//---------------------------------------------------------
	bool makeAndroidMipmaps(ExportImage const & source,
		Orkige::String const & resDirectory, Orkige::String * error)
	{
		for(IconEntry const & entry : androidMipmapEntries())
		{
			const Orkige::String densityDirectory =
				ExportFiles::join(resDirectory, "mipmap-" + entry.fileName);
			if(!ExportFiles::makeDirectories(densityDirectory, error))
			{
				return false;
			}
			const ExportImage scaled =
				downscaleImage(source, entry.size, entry.size);
			if(!encodePngFile(scaled,
				ExportFiles::join(densityDirectory, "ic_launcher.png"), error))
			{
				return false;
			}
		}
		return true;
	}
}
