/********************************************************************
	created:	Friday 2026/07/31 at 16:00
	filename: 	ExportIcons.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportIcons_h__31_7_2026__16_00_00__
#define __ExportIcons_h__31_7_2026__16_00_00__

#include "ExportImage.h"
#include "ExportProject.h"

#include <core_util/String.h>

#include <functional>
#include <vector>

//! @file ExportIcons.h
//! @brief app-icon generation for project export.
//!
//! Turns ONE square source PNG (the project's `export.icon`, or the neutral
//! engine default) into the per-platform icon sets each package needs:
//!
//!   macOS    a `.iconset` directory (10 icon_NxN[@2x].png) for `iconutil`
//!   iOS      the loose `CFBundleIconFiles` PNGs a bundle honours at its root
//!            (no asset catalog / actool - those need Xcode)
//!   Android  res/mipmap-<density>/ic_launcher.png at the five legacy
//!            densities
//!   web      one favicon
//!
//! Every resize is an area-average downscale from the SOURCE (never from an
//! already-shrunk step), so a ~1024px source yields clean icons at every size.
//! An app should always ship an icon, so a set-but-missing `export.icon` warns
//! and falls back rather than failing the export.

namespace OrkigeExport
{
	//! @brief one icon file to write: its name and its pixel size
	struct IconEntry
	{
		Orkige::String	fileName;
		int				size;
	};

	//! the macOS `.iconset` entries `iconutil` expects
	std::vector<IconEntry> macosIconsetEntries();
	//! the loose iOS icons: the iPhone app icon @2x/@3x plus the iPad @2x
	std::vector<IconEntry> iosIconEntries();
	//! the five legacy Android launcher densities, as (density, size)
	std::vector<IconEntry> androidMipmapEntries();

	//! @brief the icon source for a project: `export.icon` when set AND
	//! present, else the engine default at @p defaultIconPath. A set-but-
	//! missing `export.icon` reports through @p log and falls back.
	Orkige::String resolveIconSource(ExportProject const & project,
		Orkige::String const & defaultIconPath,
		std::function<void(Orkige::String const &)> const & log);

	//! @brief decode an icon source into a SQUARE image, centre-cropping a
	//! non-square one. False with an @p error on a missing, undecodable or
	//! too-small (< 64px) source.
	bool loadSquareIconSource(Orkige::String const & path, ExportImage & out,
		Orkige::String * error);

	//! @brief write @p entries as downscaled copies of @p source into
	//! @p outDirectory; @p outNames receives the file names written.
	bool writeIconSizes(ExportImage const & source,
		Orkige::String const & outDirectory,
		std::vector<IconEntry> const & entries,
		std::vector<Orkige::String> * outNames, Orkige::String * error);

	//! @brief write the 10 `icon_NxN[@2x].png` entries `iconutil` expects
	bool makeMacosIconset(ExportImage const & source,
		Orkige::String const & iconsetDirectory, Orkige::String * error);

	//! @brief write the loose iOS icon PNGs at a bundle root; @p outNames
	//! receives the file names (the caller lists them, sans `.png`, in
	//! `CFBundleIconFiles`).
	bool makeIosIcons(ExportImage const & source,
		Orkige::String const & bundleDirectory,
		std::vector<Orkige::String> * outNames, Orkige::String * error);

	//! @brief write `res/mipmap-<density>/ic_launcher.png` at the five legacy
	//! densities
	bool makeAndroidMipmaps(ExportImage const & source,
		Orkige::String const & resDirectory, Orkige::String * error);
}

#endif //__ExportIcons_h__31_7_2026__16_00_00__
