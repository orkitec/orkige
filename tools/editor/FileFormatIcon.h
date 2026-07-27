/**************************************************************
	created:	2026/07/27 at 12:00
	filename: 	FileFormatIcon.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __FileFormatIcon_h__27_7_2026__12_00_00__
#define __FileFormatIcon_h__27_7_2026__12_00_00__

#include <string>

namespace OrkigeEditor
{
	//! @brief a plain RGB tint (no alpha - callers pick their own, e.g.
	//! IM_COL32(color.r, color.g, color.b, 255) or an ImVec4 with alpha 1).
	//! Kept ImGui-free so this module stays a pure, headlessly-testable seam.
	struct FileFormatColor
	{
		unsigned char r = 0;
		unsigned char g = 0;
		unsigned char b = 0;
	};

	//! @brief the fallback file-kind glyph + tint for a file extension - the
	//! ONE mapping every file list in the editor draws through wherever no
	//! richer, format-specific visual (a real thumbnail, a baked preview)
	//! already covers the entry: the asset browser's rows/tiles for a
	//! not-yet-thumbnailed kind and the script editor's document tabs. `glyph`
	//! is a Font Awesome 6 Solid UTF-8 sequence (see IconsFontAwesome6.h) -
	//! never null or empty, so a caller can always draw SOMETHING.
	struct FileFormatIcon
	{
		const char* glyph = nullptr;
		FileFormatColor color;
	};

	//! @brief the glyph + tint for `extension` (with or without the leading
	//! dot, any case). Every house file format maps to a specific glyph in a
	//! family-consistent tint (scene/level assets, render/material assets,
	//! audio, script code, UI layouts, gameplay config, project files, plain
	//! text/markup); a small set of common non-house extensions (mesh/audio/
	//! image containers, fonts) map the same way so an imported asset reads
	//! at a glance too. An extension this table has never heard of (or an
	//! empty one) returns the generic file glyph in the neutral "unknown"
	//! tint - callers never need to special-case a miss.
	FileFormatIcon fileFormatIcon(std::string const& extension);
}

#endif //__FileFormatIcon_h__27_7_2026__12_00_00__
