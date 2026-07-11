/********************************************************************
	created:	Friday 2026/07/11 at 10:00
	filename: 	VectorShapeRaster.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "core_util/VectorShapeRaster.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		//! straight-alpha source-over of one interpolated fragment onto a pixel
		void compositeOver(unsigned char * pixel, float sr, float sg, float sb,
			float sa)
		{
			if (sa <= 0.0f)
			{
				return;
			}
			sa = std::min(sa, 1.0f);
			const float dr = pixel[0] / 255.0f;
			const float dg = pixel[1] / 255.0f;
			const float db = pixel[2] / 255.0f;
			const float da = pixel[3] / 255.0f;
			const float outA = sa + da * (1.0f - sa);
			float outR = sr;
			float outG = sg;
			float outB = sb;
			if (outA > 0.0f)
			{
				const float inv = 1.0f / outA;
				outR = (sr * sa + dr * da * (1.0f - sa)) * inv;
				outG = (sg * sa + dg * da * (1.0f - sa)) * inv;
				outB = (sb * sa + db * da * (1.0f - sa)) * inv;
			}
			auto toByte = [](float v)
			{
				const float clamped = std::min(std::max(v, 0.0f), 1.0f);
				return static_cast<unsigned char>(clamped * 255.0f + 0.5f);
			};
			pixel[0] = toByte(outR);
			pixel[1] = toByte(outG);
			pixel[2] = toByte(outB);
			pixel[3] = toByte(outA);
		}
	}

	void VectorShapeRaster::rasterize(VectorTessellator::Mesh const & mesh,
		int width, int height, unsigned char * rgba)
	{
		if (width <= 0 || height <= 0 || rgba == nullptr)
		{
			return;
		}
		// start fully transparent
		std::fill(rgba, rgba + static_cast<std::size_t>(width) * height * 4, 0);
		if (mesh.indices.empty() || !mesh.bounds.valid)
		{
			return;
		}

		// fit the shape's local bounds into the pixel rect: preserve aspect,
		// centre, leave a small margin so the soft feather edge is not clipped
		const float margin = std::max(1.0f,
			std::min(width, height) * 0.06f);
		const float boundsW = std::max(mesh.bounds.maxX - mesh.bounds.minX, 1e-6f);
		const float boundsH = std::max(mesh.bounds.maxY - mesh.bounds.minY, 1e-6f);
		const float scale = std::min(
			(width - 2.0f * margin) / boundsW,
			(height - 2.0f * margin) / boundsH);
		const float centreX = (mesh.bounds.minX + mesh.bounds.maxX) * 0.5f;
		const float centreY = (mesh.bounds.minY + mesh.bounds.maxY) * 0.5f;
		// shape-local (x,y) -> pixel (px,py); +y up flips to +y down
		auto project = [&](VectorTessellator::Point const & p, float & px,
			float & py)
		{
			px = (p.x - centreX) * scale + width * 0.5f;
			py = height * 0.5f - (p.y - centreY) * scale;
		};

		for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
		{
			const unsigned int i0 = mesh.indices[t];
			const unsigned int i1 = mesh.indices[t + 1];
			const unsigned int i2 = mesh.indices[t + 2];
			if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() ||
				i2 >= mesh.positions.size())
			{
				continue;
			}
			float x0, y0, x1, y1, x2, y2;
			project(mesh.positions[i0], x0, y0);
			project(mesh.positions[i1], x1, y1);
			project(mesh.positions[i2], x2, y2);
			const VectorTessellator::Colour & c0 = mesh.colours[i0];
			const VectorTessellator::Colour & c1 = mesh.colours[i1];
			const VectorTessellator::Colour & c2 = mesh.colours[i2];

			const float denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
			if (std::fabs(denom) < 1e-9f)
			{
				continue; // degenerate (zero-area) triangle
			}
			const float invDenom = 1.0f / denom;

			int minX = static_cast<int>(std::floor(std::min({ x0, x1, x2 })));
			int maxX = static_cast<int>(std::ceil(std::max({ x0, x1, x2 })));
			int minY = static_cast<int>(std::floor(std::min({ y0, y1, y2 })));
			int maxY = static_cast<int>(std::ceil(std::max({ y0, y1, y2 })));
			minX = std::max(minX, 0);
			minY = std::max(minY, 0);
			maxX = std::min(maxX, width - 1);
			maxY = std::min(maxY, height - 1);

			for (int py = minY; py <= maxY; ++py)
			{
				const float sampleY = py + 0.5f;
				for (int px = minX; px <= maxX; ++px)
				{
					const float sampleX = px + 0.5f;
					const float w0 = ((y1 - y2) * (sampleX - x2) +
						(x2 - x1) * (sampleY - y2)) * invDenom;
					const float w1 = ((y2 - y0) * (sampleX - x2) +
						(x0 - x2) * (sampleY - y2)) * invDenom;
					const float w2 = 1.0f - w0 - w1;
					// a tiny negative tolerance keeps shared edges seamless
					const float edgeEps = -1e-4f;
					if (w0 < edgeEps || w1 < edgeEps || w2 < edgeEps)
					{
						continue;
					}
					const float r = w0 * c0.r + w1 * c1.r + w2 * c2.r;
					const float g = w0 * c0.g + w1 * c1.g + w2 * c2.g;
					const float b = w0 * c0.b + w1 * c1.b + w2 * c2.b;
					const float a = w0 * c0.a + w1 * c1.a + w2 * c2.a;
					unsigned char * pixel = rgba +
						(static_cast<std::size_t>(py) * width + px) * 4;
					compositeOver(pixel, r, g, b, a);
				}
			}
		}
	}
}
