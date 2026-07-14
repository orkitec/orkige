/********************************************************************
	created:	Monday 2026/07/14 at 15:00
	filename: 	EditorImageDecode.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file EditorImageDecode.cpp
//! @brief the single translation unit that pulls in the stb_image single-file
//! decoder (@see EditorImageDecode.h) - nothing else in the editor sees it.

#include "EditorImageDecode.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <cstdio>

namespace OrkigeEditor
{
	bool decodeImageRgba(std::string const& path,
		std::vector<unsigned char>& outRgba, int& outWidth, int& outHeight)
	{
		outRgba.clear();
		outWidth = 0;
		outHeight = 0;

		std::FILE* file = std::fopen(path.c_str(), "rb");
		if(!file)
		{
			return false;
		}
		std::fseek(file, 0, SEEK_END);
		const long size = std::ftell(file);
		std::fseek(file, 0, SEEK_SET);
		if(size <= 0)
		{
			std::fclose(file);
			return false;
		}
		std::vector<unsigned char> encoded(static_cast<std::size_t>(size));
		const std::size_t read =
			std::fread(encoded.data(), 1, encoded.size(), file);
		std::fclose(file);
		if(read != encoded.size())
		{
			return false;
		}

		int channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(encoded.data(),
			static_cast<int>(encoded.size()), &outWidth, &outHeight, &channels,
			4 /*force RGBA*/);
		if(!pixels || outWidth <= 0 || outHeight <= 0)
		{
			if(pixels)
			{
				stbi_image_free(pixels);
			}
			outWidth = 0;
			outHeight = 0;
			return false;
		}
		outRgba.assign(pixels, pixels +
			static_cast<std::size_t>(outWidth) * outHeight * 4);
		stbi_image_free(pixels);
		return true;
	}
}
