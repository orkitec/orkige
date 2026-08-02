/********************************************************************
	created:	Thursday 2026/07/30 at 09:00
	filename: 	EditorResourceBinding.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "EditorResourcePaths.h"

#include <SDL3/SDL.h>

#include <cstdlib>

//! @file EditorResourceBinding.cpp
//! @brief binds the pure resource locator (EditorResourcePaths.h) to THIS
//! build: the app's own base directory plus the developer-tree fallbacks CMake
//! bakes in. The locator itself lives in editor_core and knows no absolute
//! path, so this file is the ONE place a baked path enters the resolution.

namespace OrkigeEditor
{
	namespace
	{
		//! @brief the developer-tree fallbacks, or NONE of them when
		//! ORKIGE_EDITOR_BUNDLE_ONLY is set.
		//! @remarks The knob answers one question honestly: "is this app
		//! self-sufficient?" With it set, every developer-tree path is treated
		//! as absent, so anything the bundle failed to carry fails visibly here
		//! instead of silently borrowing from the machine that built it. The
		//! bundle selfcheck runs the staged app that way.
		EditorResourceFallbacks makeFallbacks()
		{
			EditorResourceFallbacks fallbacks;
			// the flavor's Media/ marker subdirectory ("Hlms" on next, "Main"
			// on classic): a directory carrying it is a real engine-media root
			fallbacks.engineMediaMarker = ORKIGE_EDITOR_MEDIA_MARKER;
			fallbacks.flavor = ORKIGE_EDITOR_RENDER_BACKEND;
			if(std::getenv("ORKIGE_EDITOR_BUNDLE_ONLY") != nullptr)
			{
				return fallbacks;
			}
			fallbacks.engineMedia = ORKIGE_EDITOR_MEDIA_DIR;
			fallbacks.fonts = ORKIGE_EDITOR_FONT_DIR;
			fallbacks.water = ORKIGE_EDITOR_WATER_DIR;
			fallbacks.decals = ORKIGE_EDITOR_DECAL_DIR;
			fallbacks.bloom = ORKIGE_EDITOR_BLOOM_DIR;
			fallbacks.grade = ORKIGE_EDITOR_GRADE_DIR;
			fallbacks.uiFonts = ORKIGE_EDITOR_ICON_FONT_DIR;
			fallbacks.defaultIcon = ORKIGE_EDITOR_DEFAULT_ICON;
			fallbacks.player = ORKIGE_EDITOR_PLAYER_PATH;
			fallbacks.texcook = ORKIGE_EDITOR_TEXCOOK_PATH;
			return fallbacks;
		}
	}
	//---------------------------------------------------------
	EditorResourceLocator const & editorResources()
	{
		// one instance for the process: the roots cannot change while it runs
		static const EditorResourceLocator locator(
			[]()
			{
				const char* const base = SDL_GetBasePath();
				return Orkige::String(base != nullptr ? base : "");
			}(),
			makeFallbacks());
		return locator;
	}
}
