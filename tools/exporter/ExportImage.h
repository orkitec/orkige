/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportImage.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportImage_h__31_7_2026__12_00_00__
#define __ExportImage_h__31_7_2026__12_00_00__

#include <core_util/String.h>

#include <vector>

//! @file ExportImage.h
//! @brief the RGBA8 raster and the three operations an export performs on one:
//! area-average downscale, alpha premultiply, and the aspect-preserving size
//! cap.
//!
//! Decode is stb_image confined to ONE translation unit (ExportImageDecode.cpp,
//! the engine_sound/StbVorbisImpl.cpp isolation pattern); encode is
//! `core_util/PngWriter`, which is already the engine's dependency-free RGBA8
//! writer. The operations themselves are pure integer arithmetic and produce
//! the same pixels on every host - an icon set or a cooked texture must not
//! depend on which machine packaged it.

namespace OrkigeExport
{
	//! @brief an 8-bit RGBA raster, row-major, top-left origin
	struct ExportImage
	{
		int								width = 0;
		int								height = 0;
		std::vector<unsigned char>		pixels;	//!< width*height*4 bytes

		ExportImage() {}
		ExportImage(int w, int h)
			: width(w), height(h),
			pixels(static_cast<std::size_t>(w) * h * 4, 0) {}

		bool valid() const
		{
			return this->width > 0 && this->height > 0 &&
				this->pixels.size() ==
				static_cast<std::size_t>(this->width) * this->height * 4;
		}
	};

	//! @brief decode a PNG (or any format the bundled decoder reads) into
	//! straight RGBA8. False with an honest @p error on a missing or
	//! undecodable file.
	bool decodeImageFile(Orkige::String const & path, ExportImage & out,
		Orkige::String * error);

	//! @brief write @p image as an 8-bit RGBA PNG. False with an @p error on a
	//! bad image or a write failure.
	bool encodePngFile(ExportImage const & image, Orkige::String const & path,
		Orkige::String * error);

	//! @brief area-average (box) downscale to the target size - deterministic,
	//! and free of the aliasing a nearest-neighbour resize would show. Only
	//! meant for shrinking; an unchanged size returns a copy.
	ExportImage downscaleImage(ExportImage const & image, int targetWidth,
		int targetHeight);

	//! @brief fold alpha into RGB in place (straight -> premultiplied)
	void premultiplyImage(ExportImage & image);

	//! @brief the largest (w, h) with the same aspect whose longest side is
	//! <= @p maxSize (>= 1 each); unchanged when @p maxSize is 0/negative or
	//! the image already fits.
	void fitWithin(int width, int height, int maxSize, int & outWidth,
		int & outHeight);

	//! @brief centre-crop to the largest centred square (a square image
	//! returns a copy) - how an icon source of any aspect becomes an icon.
	ExportImage cropToSquare(ExportImage const & image);

	//! @brief does any texel carry an alpha below 255 (drives the ETC2/BCn
	//! variant a texture cooks to)
	bool imageHasAlpha(ExportImage const & image);
}

#endif //__ExportImage_h__31_7_2026__12_00_00__
