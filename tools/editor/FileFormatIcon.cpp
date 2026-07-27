/**************************************************************
	created:	2026/07/27 at 12:00
	filename: 	FileFormatIcon.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "FileFormatIcon.h"

#include "IconsFontAwesome6.h"

#include <cctype>

namespace OrkigeEditor
{
	namespace
	{
		//! a lower-case, dot-prefixed extension ("Lua" or ".LUA" -> ".lua") -
		//! the same tiny normalization EditorTextDiagnostics.cpp's classifiers
		//! use, kept as its own copy here (a one-line helper, not worth a
		//! shared header) so this module stays link-independent.
		std::string normalizeExtension(std::string const& extension)
		{
			std::string ext = extension;
			if (!ext.empty() && ext.front() != '.')
			{
				ext.insert(ext.begin(), '.');
			}
			for (char& c : ext)
			{
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return ext;
		}

		//! the family tints, one per visual grouping. Where an extension
		//! already has an established colour elsewhere in the editor (the
		//! asset browser's per-AssetKind glyph tint - Texture/Mesh/Script/
		//! Scene/Prefab/Audio/VectorShape/Material) the SAME value is used
		//! here too, so a format never shows two different colours depending
		//! on which list draws it. The remaining families (UI layouts,
		//! gameplay config, project files, fonts, plain text/markup) are new
		//! and picked from the same muted, low-saturation range - a taste
		//! pass, open to the owner's polish.
		constexpr FileFormatColor kSceneColor{ 150, 122, 202 };	// scene/level sequence
		constexpr FileFormatColor kPrefabColor{ 212, 150, 88 };	// prefab subtree
		constexpr FileFormatColor kRenderColor{ 176, 148, 96 };	// PBS material
		constexpr FileFormatColor kVectorColor{ 198, 132, 196 };	// vector shape/animation
		constexpr FileFormatColor kMeshColor{ 92, 132, 200 };		// imported mesh
		constexpr FileFormatColor kTextureColor{ 88, 168, 158 };	// image/texture
		constexpr FileFormatColor kAudioColor{ 200, 110, 152 };	// sound/music
		constexpr FileFormatColor kCodeColor{ 110, 178, 112 };		// Lua script
		constexpr FileFormatColor kUiColor{ 96, 176, 214 };		// declarative UI layout
		constexpr FileFormatColor kGameplayColor{ 150, 168, 96 };	// input/physics config
		constexpr FileFormatColor kProjectColor{ 150, 140, 168 };	// project manifest/sidecar
		constexpr FileFormatColor kFontColor{ 196, 140, 120 };		// TrueType font
		constexpr FileFormatColor kTextColor{ 150, 158, 168 };		// plain text/markup/data
		constexpr FileFormatColor kUnknownColor{ 122, 122, 122 };	// no format recognized
	}

	FileFormatIcon fileFormatIcon(std::string const& extension)
	{
		const std::string ext = normalizeExtension(extension);

		// scene/level structure
		if (ext == ".oscene")
		{
			return { ICON_FA_FILM, kSceneColor };
		}
		if (ext == ".oprefab")
		{
			return { ICON_FA_CLONE, kPrefabColor };
		}
		if (ext == ".olevels")
		{
			return { ICON_FA_LIST_OL, kSceneColor };
		}

		// render/material assets
		if (ext == ".omat")
		{
			return { ICON_FA_PALETTE, kRenderColor };
		}
		if (ext == ".oshape")
		{
			return { ICON_FA_SHAPES, kVectorColor };
		}
		if (ext == ".oanim")
		{
			return { ICON_FA_PERSON_RUNNING, kVectorColor };
		}
		if (ext == ".glb" || ext == ".gltf")
		{
			return { ICON_FA_CUBE, kMeshColor };
		}
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" ||
			ext == ".bmp" || ext == ".gif" || ext == ".dds" || ext == ".ktx" ||
			ext == ".oitd")
		{
			return { ICON_FA_IMAGE, kTextureColor };
		}
		if (ext == ".ttf")
		{
			return { ICON_FA_FONT, kFontColor };
		}

		// audio
		if (ext == ".wav" || ext == ".caf" || ext == ".ogg" || ext == ".mp3" ||
			ext == ".flac")
		{
			return { ICON_FA_MUSIC, kAudioColor };
		}

		// script code
		if (ext == ".lua")
		{
			return { ICON_FA_FILE_CODE, kCodeColor };
		}

		// declarative UI layouts
		if (ext == ".oui")
		{
			return { ICON_FA_WINDOW_MAXIMIZE, kUiColor };
		}
		if (ext == ".ogui")
		{
			return { ICON_FA_TABLE_CELLS, kUiColor };
		}

		// gameplay config assets
		if (ext == ".oactions")
		{
			return { ICON_FA_GAMEPAD, kGameplayColor };
		}
		if (ext == ".olayers")
		{
			return { ICON_FA_LAYER_GROUP, kGameplayColor };
		}

		// project manifest + asset-id sidecar (the sidecar is normally
		// filtered out of the asset browser's listing entirely - see
		// AssetDatabase::META_FILE_EXTENSION - but the mapping stays honest
		// for any OTHER file list that might show one)
		if (ext == ".orkproj")
		{
			return { ICON_FA_DIAGRAM_PROJECT, kProjectColor };
		}
		if (ext == ".orkmeta")
		{
			return { ICON_FA_TAG, kProjectColor };
		}

		// plain text/markup/data - the XMLArchive/XLIFF and JSON families
		// EditorTextDiagnostics::textDocumentKindForExtension also
		// recognizes, kept generic here on purpose: every house extension in
		// THAT list (.oscene/.oprefab/.orkproj/...) already has its own
		// specific entry above, so this bucket only ever catches a bare
		// .xml/.xlf/.json/.md file
		if (ext == ".xlf")
		{
			return { ICON_FA_LANGUAGE, kTextColor };
		}
		if (ext == ".json" || ext == ".jsonl" || ext == ".xml" || ext == ".md")
		{
			return { ICON_FA_FILE_LINES, kTextColor };
		}

		return { ICON_FA_FILE, kUnknownColor };
	}
}
