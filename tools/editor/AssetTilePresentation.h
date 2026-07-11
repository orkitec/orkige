// AssetTilePresentation - pure, UI-free rules for how an asset-browser tile
// paints, split out so the headless unit tests (tests/editor_core) exercise
// exactly the decision the panel draws with (no ImGui, no filesystem).
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

namespace Orkige
{
	//! @brief does a grid tile paint a backing rectangle behind its icon?
	//! A backing belongs ONLY to a real texture thumbnail (the transparency
	//! checkerboard so alpha reads against any tile colour). Every glyph
	//! fallback - folders, scenes, prefabs, scripts, audio, unknown - draws
	//! bare, so the icon floats on the grid with no tinted box behind it.
	//! @param isFolder the tile is a folder (always a glyph, never backed)
	//! @param hasThumbnail a real texture thumbnail is loaded for the tile
	inline bool tileDrawsBacking(bool isFolder, bool hasThumbnail)
	{
		return hasThumbnail && !isFolder;
	}

	//! @brief does a SELECTED item paint a filled highlight behind its icon, or
	//! just an outline accent? A grid tile draws its selection as an OUTLINE only
	//! (a filled box hid the thumbnail's transparency checkerboard and the kind
	//! glyph); the compact list/tree rows, which have no thumbnail area to
	//! obscure, keep the classic filled highlight.
	//! @param isTile the item draws as a grid tile (false = a list/tree row)
	inline bool tileSelectionDrawsFill(bool isTile)
	{
		return !isTile;
	}
}
