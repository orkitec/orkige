/********************************************************************
	created:	Saturday 2026/07/11 at 03:30
	filename: 	SvgRasterImpl.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The ONE translation unit that talks to nanosvg. The vcpkg nanosvg port
	precompiles the parser/rasteriser into static libs (NanoSVG::nanosvg /
	NanoSVG::nanosvgrast), so this TU only includes the declaration headers
	and links the libs - no NANOSVG_IMPLEMENTATION define. Nothing else in
	the tree includes nanosvg, so it stays out of every header, the neutral
	umbrella and the precompiled header; FontAtlas and the tests reach it
	only through the SvgRaster seam (SvgRaster.h).
*********************************************************************/

#include "engine_fastgui/SvgRaster.h"

#include <algorithm>
#include <cmath>

#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Wcast-qual"
#elif defined(__GNUC__)
#	pragma GCC diagnostic push
#endif

#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>

#if defined(__clang__)
#	pragma clang diagnostic pop
#elif defined(__GNUC__)
#	pragma GCC diagnostic pop
#endif

namespace Orkige
{
	namespace SvgRaster
	{
		//----------------------------------------------------
		Image rasterize(unsigned char const * svg, int size, int targetWidthPx)
		{
			Image out;
			if(svg == NULL || size <= 0 || targetWidthPx <= 0)
			{
				return out;
			}
			// nsvgParse mutates + requires a NUL-terminated buffer
			std::vector<char> text(svg, svg + size);
			text.push_back('\0');

			NSVGimage* image = nsvgParse(text.data(), "px", 96.0f);
			if(image == NULL || image->width <= 0.0f || image->height <= 0.0f)
			{
				if(image != NULL)
				{
					nsvgDelete(image);
				}
				return out;
			}

			// uniform fit: the natural width maps to the requested device px
			const float scale = float(targetWidthPx) / image->width;
			const int width = targetWidthPx;
			const int height = std::max(1,
				int(std::lround(double(image->height) * double(scale))));

			NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
			if(rasterizer == NULL)
			{
				nsvgDelete(image);
				return out;
			}
			out.rgba.assign(size_t(width) * size_t(height) * 4u, 0);
			nsvgRasterize(rasterizer, image, 0.0f, 0.0f, scale,
				out.rgba.data(), width, height, width * 4);
			out.width = width;
			out.height = height;

			nsvgDeleteRasterizer(rasterizer);
			nsvgDelete(image);
			return out;
		}
	}
}
