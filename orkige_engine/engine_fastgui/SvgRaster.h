/********************************************************************
	created:	Saturday 2026/07/11 at 03:30
	filename: 	SvgRaster.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SvgRaster_h__11_7_2026__03_30_00__
#define __SvgRaster_h__11_7_2026__03_30_00__

//! @file SvgRaster.h
//! @brief the vector-sprite rasterisation seam: a thin, renderer-free
//! wrapper over the nanosvg parser + rasteriser.
//! @remarks the ONE place nanosvg's headers are included is
//! SvgRasterImpl.cpp; everything else (FontAtlas, the unit tests) reaches
//! it ONLY through this function, so the library never leaks into headers,
//! the neutral umbrella or the precompiled header - the isolation pattern
//! of engine_sound/StbVorbisImpl.cpp / FontBakeImpl.cpp. Straight
//! (non-premultiplied) RGBA out, matching the DrawLayer2D vertex contract.

#include "engine_module/EnginePrerequisites.h"

#include <vector>

namespace Orkige
{
	namespace SvgRaster
	{
		//! a rasterised vector image: tightly packed RGBA8 rows, straight alpha
		struct Image
		{
			std::vector<unsigned char>	rgba;	//!< width*height*4 bytes
			int	width = 0, height = 0;
		};

		//! @brief rasterise an SVG blob so its natural width maps to
		//! `targetWidthPx` device pixels, preserving aspect ratio (the height
		//! follows). An empty image (width/height 0) on a parse failure.
		//! @remarks baking at the display's device resolution (design px x the
		//! integer content scale) is what keeps the sprite crisp on retina /
		//! phone screens - the whole point of a vector source over a fixed PNG.
		Image rasterize(unsigned char const * svg, int size, int targetWidthPx);
	}
}

#endif //__SvgRaster_h__11_7_2026__03_30_00__
