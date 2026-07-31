/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportImage.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportImage.h"

#include <core_util/PngWriter.h>

#include <algorithm>
#include <cstddef>

namespace OrkigeExport
{
	//---------------------------------------------------------
	bool encodePngFile(ExportImage const & image, Orkige::String const & path,
		Orkige::String * error)
	{
		if(!image.valid())
		{
			if(error != 0)
			{
				*error = "cannot write '" + path + "': the image is empty";
			}
			return false;
		}
		if(!Orkige::PngWriter::writeFile(path, image.pixels.data(),
			image.width, image.height))
		{
			if(error != 0)
			{
				*error = "cannot write the PNG '" + path + "'";
			}
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	ExportImage downscaleImage(ExportImage const & image, int targetWidth,
		int targetHeight)
	{
		if(!image.valid() || targetWidth <= 0 || targetHeight <= 0)
		{
			return ExportImage();
		}
		if(targetWidth == image.width && targetHeight == image.height)
		{
			return image;
		}
		ExportImage out(targetWidth, targetHeight);
		for(int ty = 0; ty < targetHeight; ++ty)
		{
			const int sy0 = (ty * image.height) / targetHeight;
			const int sy1 = std::max(sy0 + 1,
				((ty + 1) * image.height) / targetHeight);
			for(int tx = 0; tx < targetWidth; ++tx)
			{
				const int sx0 = (tx * image.width) / targetWidth;
				const int sx1 = std::max(sx0 + 1,
					((tx + 1) * image.width) / targetWidth);
				unsigned int red = 0;
				unsigned int green = 0;
				unsigned int blue = 0;
				unsigned int alpha = 0;
				unsigned int count = 0;
				for(int sy = sy0; sy < sy1; ++sy)
				{
					std::size_t source =
						(static_cast<std::size_t>(sy) * image.width + sx0) * 4;
					for(int sx = sx0; sx < sx1; ++sx)
					{
						red += image.pixels[source];
						green += image.pixels[source + 1];
						blue += image.pixels[source + 2];
						alpha += image.pixels[source + 3];
						source += 4;
						++count;
					}
				}
				std::size_t destination =
					(static_cast<std::size_t>(ty) * targetWidth + tx) * 4;
				out.pixels[destination] =
					static_cast<unsigned char>(red / count);
				out.pixels[destination + 1] =
					static_cast<unsigned char>(green / count);
				out.pixels[destination + 2] =
					static_cast<unsigned char>(blue / count);
				out.pixels[destination + 3] =
					static_cast<unsigned char>(alpha / count);
			}
		}
		return out;
	}
	//---------------------------------------------------------
	void premultiplyImage(ExportImage & image)
	{
		for(std::size_t index = 0; index + 3 < image.pixels.size(); index += 4)
		{
			const unsigned int alpha = image.pixels[index + 3];
			if(alpha == 255)
			{
				continue;
			}
			for(std::size_t channel = 0; channel < 3; ++channel)
			{
				image.pixels[index + channel] = static_cast<unsigned char>(
					(image.pixels[index + channel] * alpha) / 255);
			}
		}
	}
	//---------------------------------------------------------
	void fitWithin(int width, int height, int maxSize, int & outWidth,
		int & outHeight)
	{
		outWidth = width;
		outHeight = height;
		if(maxSize <= 0 || std::max(width, height) <= maxSize)
		{
			return;
		}
		// round-half-away-from-zero on the derived side, matching the
		// deterministic integer scaling the sidecar's maxSize documents
		if(width >= height)
		{
			outWidth = maxSize;
			outHeight = std::max(1, static_cast<int>(
				(static_cast<double>(height) * maxSize) / width + 0.5));
		}
		else
		{
			outHeight = maxSize;
			outWidth = std::max(1, static_cast<int>(
				(static_cast<double>(width) * maxSize) / height + 0.5));
		}
	}
	//---------------------------------------------------------
	ExportImage cropToSquare(ExportImage const & image)
	{
		if(!image.valid() || image.width == image.height)
		{
			return image;
		}
		const int side = std::min(image.width, image.height);
		const int offsetX = (image.width - side) / 2;
		const int offsetY = (image.height - side) / 2;
		ExportImage out(side, side);
		for(int y = 0; y < side; ++y)
		{
			const std::size_t source = (static_cast<std::size_t>(offsetY + y) *
				image.width + offsetX) * 4;
			const std::size_t destination =
				static_cast<std::size_t>(y) * side * 4;
			std::copy(image.pixels.begin() + source,
				image.pixels.begin() + source + side * 4,
				out.pixels.begin() + destination);
		}
		return out;
	}
	//---------------------------------------------------------
	bool imageHasAlpha(ExportImage const & image)
	{
		for(std::size_t index = 3; index < image.pixels.size(); index += 4)
		{
			if(image.pixels[index] != 255)
			{
				return true;
			}
		}
		return false;
	}
}
