/**************************************************************
	created:	2011/06/25 at 4:20
	filename: 	SwfBitmapInfo.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_swf/SwfBitmapInfo.h"
#include "engine_swf/SwfRenderHandler.h"
#include "engine_swf/SwfMovieManager.h"

namespace Orkige
{
	SwfBitmapInfo::SwfBitmapInfo()
	{
		this->textureId = 0;
		this->width = 0;
		this->height = 0;
	}
	// -------------------------------------------------------------------------------
	SwfBitmapInfo::SwfBitmapInfo(int width, int height, Uint8* data)
	{
		assert(width > 0);
		assert(height > 0);
		assert(data);

		this->textureId = 0;

		this->width = width;
		this->height = height;

#ifndef NDEBUG
		// You must use power-of-two dimensions!!
		int	w = 1; while (w < width) { w <<= 1; }
		int	h = 1; while (h < height) { h <<= 1; }
		assert(w == width);
		assert(h == height);
#endif // not NDEBUG

		// Build mips.
		int	level = 1;
		while (width > 1 || height > 1)
		{
			Orkige::SwfRenderHandler::make_next_miplevel(&width, &height, data);
			level++;
		}
	}
	// -------------------------------------------------------------------------------
	SwfBitmapInfo::SwfBitmapInfo(image::rgb* im)
	{
		assert(im);

		// Create the texture.
		this->width = im->m_width;
		this->height = im->m_height;

		int	w = 1; while (w < im->m_width) { w <<= 1; }
		int	h = 1; while (h < im->m_height) { h <<= 1; }

		image::rgb*	rescaled = image::create_rgb(w, h);
		image::resample(rescaled, 0, 0, w - 1, h - 1,
			im, 0, 0, (float) im->m_width, (float) im->m_height);

		SwfMovieManager::getSingleton().stream_bitmap(rescaled->m_width, rescaled->m_height, (char*)rescaled->m_data, (unsigned int&)this->textureId);

		delete rescaled;
	}
	// -------------------------------------------------------------------------------
	SwfBitmapInfo::SwfBitmapInfo(image::rgba* im)
	{
		assert(im);

		// Create the texture.
		this->width = im->m_width;
		this->height = im->m_height;

		int	w = 1; while (w < im->m_width) { w <<= 1; }
		int	h = 1; while (h < im->m_height) { h <<= 1; }

		if (w != im->m_width
			|| h != im->m_height)
		{
			image::rgba*	rescaled = image::create_rgba(w, h);
			image::resample(rescaled, 0, 0, w - 1, h - 1,
				im, 0, 0, (float) im->m_width, (float) im->m_height);

			SwfMovieManager::getSingleton().stream_bitmapAlpha(rescaled->m_width, rescaled->m_height, (char*)rescaled->m_data, (unsigned int&)this->textureId);

			delete rescaled;
		}
		else
		{
			// Use original image directly.
			SwfMovieManager::getSingleton().stream_bitmapAlpha(im->m_width, im->m_height, (char*)im->m_data, (unsigned int&)this->textureId);
		}
	}
	// -------------------------------------------------------------------------------
	int SwfBitmapInfo::get_width() const 
	{ 
		return this->width; 
	}
	// -------------------------------------------------------------------------------
	int SwfBitmapInfo::get_height() const 
	{ 
		return this->height; 
	}
	// -------------------------------------------------------------------------------
};