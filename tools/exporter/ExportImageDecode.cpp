/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportImageDecode.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file ExportImageDecode.cpp
//! @brief the single translation unit that pulls in the stb_image single-file
//! decoder for the exporter - nothing else in the library sees it (the
//! engine_sound/StbVorbisImpl.cpp isolation pattern).
//!
//! STB_IMAGE_STATIC keeps every decoder symbol internal to this TU, so a
//! binary that ALSO carries another stb_image implementation (the editor's own
//! thumbnail decoder) links both without a duplicate-symbol collision.

#include "ExportImage.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <cstddef>
#include <cstdio>
#include <vector>

namespace OrkigeExport
{
	//---------------------------------------------------------
	bool decodeImageFile(Orkige::String const & path, ExportImage & out,
		Orkige::String * error)
	{
		auto fail = [error](Orkige::String const & message) -> bool
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		};
		std::FILE * file = std::fopen(path.c_str(), "rb");
		if(file == 0)
		{
			return fail("cannot open the image '" + path + "'");
		}
		std::fseek(file, 0, SEEK_END);
		const long size = std::ftell(file);
		std::fseek(file, 0, SEEK_SET);
		if(size <= 0)
		{
			std::fclose(file);
			return fail("the image '" + path + "' is empty");
		}
		std::vector<unsigned char> encoded(static_cast<std::size_t>(size));
		const std::size_t read =
			std::fread(encoded.data(), 1, encoded.size(), file);
		std::fclose(file);
		if(read != encoded.size())
		{
			return fail("cannot read the image '" + path + "'");
		}
		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc * pixels = stbi_load_from_memory(encoded.data(),
			static_cast<int>(encoded.size()), &width, &height, &channels,
			4 /*force RGBA*/);
		if(pixels == 0 || width <= 0 || height <= 0)
		{
			if(pixels != 0)
			{
				stbi_image_free(pixels);
			}
			return fail("cannot decode the image '" + path + "' (8-bit PNG "
				"expected)");
		}
		out.width = width;
		out.height = height;
		out.pixels.assign(pixels,
			pixels + static_cast<std::size_t>(width) * height * 4);
		stbi_image_free(pixels);
		return true;
	}
}
