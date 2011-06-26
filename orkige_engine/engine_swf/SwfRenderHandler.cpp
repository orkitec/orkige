/**************************************************************
	created:	2011/06/26 at 20:46
	filename: 	SwfRenderHandler.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include <gameswf/gameswf.h>
#include <gameswf/gameswf_types.h>
#include <base/image.h>
#include <base/utility.h>
#include <string.h>

#include "engine_swf/SwfRenderHandler.h"
#include "engine_swf/SwfMovieManager.h"
#include "engine_swf/SwfBitmapInfo.h"

#include <Ogre.h>

namespace Orkige
{
	SwfRenderHandler::fill_style::fill_style()	:
		fillMode(INVALID), hasNonzeroBitmapAdditiveColor(false)
	{
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::fill_style::apply(/*const matrix& current_matrix*/) const
	{
		assert(this->fillMode != INVALID);

		if (this->fillMode == COLOR)
		{
			apply_color(this->fillColor);
			/*SwfMovieManager::getSingleton().fill_style_color(this->fillColor.m_r/255.0, this->fillColor.m_g/255.0, this->fillColor.m_b/255.0);*/
			SwfMovieManager::getSingleton().fill_style_color(this->fillColor.m_r/255.0, this->fillColor.m_g/255.0, this->fillColor.m_b/255.0, this->fillColor.m_a/255.0);
		}
		else if (this->fillMode == BITMAP_WRAP
			|| this->fillMode == BITMAP_CLAMP)
		{
			assert(this->bitmapInfo != NULL);

			apply_color(this->fillColor);

			if (this->bitmapInfo == NULL)
			{
			}
			else
			{
				// Set up the texture for rendering.

				{
					// Do the modulate part of the color
					// transform in the first pass.  The
					// additive part, if any, needs to
					// happen in a second pass.
				}

				const Orkige::SwfBitmapInfo* bitmap_info = static_cast<const Orkige::SwfBitmapInfo*>(this->bitmapInfo);
				if(bitmap_info)
					SwfMovieManager::getSingleton().fill_style_bitmap(bitmap_info->textureId);

				if (this->fillMode == BITMAP_CLAMP)
				{
				}
				else
				{
					assert(this->fillMode == BITMAP_WRAP);
				}

				// Set up the bitmap matrix for texgen.

				float	inv_width = 1.0f / this->bitmapInfo->get_width()/*m_original_width*/;
				float	inv_height = 1.0f / this->bitmapInfo->get_height()/*m_original_height*/;

				const gameswf::matrix&	m = this->bitmapMatrix;

				float	p[4] = { 0, 0, 0, 0 };
				p[0] = m.m_[0][0] * inv_width;
				p[1] = m.m_[0][1] * inv_width;
				p[3] = m.m_[0][2] * inv_width;

				p[0] = m.m_[1][0] * inv_height;
				p[1] = m.m_[1][1] * inv_height;
				p[3] = m.m_[1][2] * inv_height;
			}
		}
	}

	// -------------------------------------------------------------------------------
	bool SwfRenderHandler::fill_style::needs_second_pass() const
	{
		if (this->fillMode == BITMAP_WRAP
			|| this->fillMode == BITMAP_CLAMP)
		{
			return this->hasNonzeroBitmapAdditiveColor;
		}
		else
		{
			return false;
		}
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::fill_style::apply_second_pass() const
	{
		assert(needs_second_pass());

		// The additive color also seems to be modulated by the texture. So,
		// maybe we can fake this in one pass using using the mean value of 
		// the colors: c0*t+c1*t = ((c0+c1)/2) * t*2
		// I don't know what the alpha component of the color is for.
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::fill_style::cleanup_second_pass() const
	{
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::fill_style::disable() 
	{ 
		this->fillMode = INVALID; 
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::fill_style::set_color(gameswf::rgba color) 
	{
		this->fillMode = COLOR; 
		this->fillColor = color; 
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::fill_style::set_bitmap(const gameswf::bitmap_info* bi, const gameswf::matrix& m, bitmap_wrap_mode wm, const gameswf::cxform& color_transform)
	{
		this->fillMode = (wm == WRAP_REPEAT) ? BITMAP_WRAP : BITMAP_CLAMP;
		this->fillColor = gameswf::rgba();
		this->bitmapInfo = bi;
		this->bitmapMatrix = m;
		this->bitmapColorTransform = color_transform;

		if (this->bitmapColorTransform.m_[0][1] > 1.0f
			|| this->bitmapColorTransform.m_[1][1] > 1.0f
			|| this->bitmapColorTransform.m_[2][1] > 1.0f
			|| this->bitmapColorTransform.m_[3][1] > 1.0f)
		{
			this->hasNonzeroBitmapAdditiveColor = true;
		}
		else
		{
			this->hasNonzeroBitmapAdditiveColor = false;
		}
	}

	// -------------------------------------------------------------------------------
	bool SwfRenderHandler::fill_style::is_valid() const 
	{ 
		return this->fillMode != INVALID; 
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::set_antialiased(bool enable)
	{
		this->enableAntialias = enable;
	}

	// -------------------------------------------------------------------------------
	/*static */void SwfRenderHandler::make_next_miplevel(int* width, int* height, Uint8* data)
		// Utility.  Mutates *width, *height and *data to create the
		// next mip level.
	{
		assert(width);
		assert(height);
		assert(data);

		int	new_w = *width >> 1;
		int	new_h = *height >> 1;
		if (new_w < 1) new_w = 1;
		if (new_h < 1) new_h = 1;

		if (new_w * 2 != *width	 || new_h * 2 != *height)
		{
			// Image can't be shrunk along (at least) one
			// of its dimensions, so don't bother
			// resampling.	Technically we should, but
			// it's pretty useless at this point.  Just
			// change the image dimensions and leave the
			// existing pixels.
		}
		else
		{
			// Resample.  Simple average 2x2 --> 1, in-place.
			for (int j = 0; j < new_h; j++) {
				Uint8*	out = ((Uint8*) data) + j * new_w;
				Uint8*	in = ((Uint8*) data) + (j << 1) * *width;
				for (int i = 0; i < new_w; i++) {
					int	a;
					a = (*(in + 0) + *(in + 1) + *(in + 0 + *width) + *(in + 1 + *width));
					*(out) = a >> 2;
					out++;
					in += 2;
				}
			}
		}

		// Munge parameters to reflect the shrunken image.
		*width = new_w;
		*height = new_h;
	}

	// -------------------------------------------------------------------------------
	gameswf::bitmap_info* SwfRenderHandler::create_bitmap_info_rgb(image::rgb* im)
	{
		return new Orkige::SwfBitmapInfo(im);
	}

	// -------------------------------------------------------------------------------
	gameswf::bitmap_info* SwfRenderHandler::create_bitmap_info_rgba(image::rgba* im)
	{
		return new Orkige::SwfBitmapInfo(im);
	}
	
	// -------------------------------------------------------------------------------
	gameswf::bitmap_info* SwfRenderHandler::create_bitmap_info_empty()
	{
		return new Orkige::SwfBitmapInfo();
	}

	// -------------------------------------------------------------------------------
	gameswf::bitmap_info* SwfRenderHandler::create_bitmap_info_alpha(int w, int h, Uint8* data)
	{
		return new Orkige::SwfBitmapInfo(w, h, data);
	}
	
	// -------------------------------------------------------------------------------
	void SwfRenderHandler::delete_bitmap_info(gameswf::bitmap_info* bi)
	{
		delete bi;
	}

	// -------------------------------------------------------------------------------
	SwfRenderHandler::SwfRenderHandler()
	{
	}

	// -------------------------------------------------------------------------------
	SwfRenderHandler::~SwfRenderHandler()
	{
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::begin_display(
		gameswf::rgba background_color,
		int viewport_x0, int viewport_y0,
		int viewport_width, int viewport_height,
		float x0, float x1, float y0, float y1)
	{
		this->displayWidth = fabsf(x1 - x0);
		this->displayHeight = fabsf(y1 - y0);

		SwfMovieManager::getSingleton().begin_display(
			background_color.m_r/255.0, 
			background_color.m_g/255.0, 
			background_color.m_b/255.0
			);

		// Clear the background, if background color has alpha > 0.
		if (background_color.m_a > 0)
		{
			// Draw a big quad.
			apply_color(background_color);
		}
	}
	
	// -------------------------------------------------------------------------------
	void SwfRenderHandler::end_display()
	{
		SwfMovieManager::getSingleton().end_display();
	}
	
	// -------------------------------------------------------------------------------
	void SwfRenderHandler::set_matrix(const gameswf::matrix& m)
	{
		this->currentMatrix = m;
	}
	
	// -------------------------------------------------------------------------------
	void SwfRenderHandler::set_cxform(const gameswf::cxform& cx)
	{
		this->currentCxform = cx;
	}

	// -------------------------------------------------------------------------------
	/*static */void SwfRenderHandler::apply_matrix(const gameswf::matrix& m)
	{
		float	mat[16];
		memset(&mat[0], 0, sizeof(mat));
		mat[0] = m.m_[0][0];
		mat[1] = m.m_[1][0];
		mat[4] = m.m_[0][1];
		mat[5] = m.m_[1][1];
		mat[10] = 1;
		mat[12] = m.m_[0][2];
		mat[13] = m.m_[1][2];
		mat[15] = 1;
	}

	// -------------------------------------------------------------------------------
	/*static */void	SwfRenderHandler::apply_color(const gameswf::rgba& c)
	{
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::fill_style_disable(int fill_side)
	{
		assert(fill_side >= 0 && fill_side < 2);

		this->currentStyles[fill_side].disable();
	}
	
	// -------------------------------------------------------------------------------
	void SwfRenderHandler::line_style_disable()
	{
		this->currentStyles[LINE_STYLE].disable();
	}
	
	// -------------------------------------------------------------------------------
	void SwfRenderHandler::fill_style_color(int fill_side, const gameswf::rgba& color)
	{
		assert(fill_side >= 0 && fill_side < 2);

		this->currentStyles[fill_side].set_color(this->currentCxform.transform(color));
	}
	
	// -------------------------------------------------------------------------------
	void SwfRenderHandler::line_style_color(gameswf::rgba color)
	{
		this->currentStyles[LINE_STYLE].set_color(this->currentCxform.transform(color));
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::fill_style_bitmap(int fill_side, gameswf::bitmap_info* bi, const gameswf::matrix& m, bitmap_wrap_mode wm, bitmap_blend_mode bm)
	{
		assert(fill_side >= 0 && fill_side < 2);
		this->currentStyles[fill_side].set_bitmap(bi, m, wm, this->currentCxform);
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::line_style_width(float width)
	{
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::draw_mesh_strip(const void* coords, int vertex_count)
	{
		// Set up current style.
		this->currentStyles[LEFT_STYLE].apply();

		float	mat[16];
		memset(&mat[0], 0, sizeof(mat));
		mat[0]  =  this->currentMatrix.m_[0][0];
		mat[1]  =  this->currentMatrix.m_[1][0];
		mat[4]  =  this->currentMatrix.m_[0][1];
		mat[5]  =  this->currentMatrix.m_[1][1];
		mat[10] =  1;
		mat[12] = this->currentMatrix.m_[0][2] * 0.0005 - 0.01; //to center the mesh horizontal
		mat[13] = this->currentMatrix.m_[1][2] * 0.0005 - 0.01; //to center the mesh vertical
		mat[15] =  1;

		SwfMovieManager::getSingletonPtr()->draw_mesh_strip(
			coords, //vertex Data
			vertex_count, //vertex count
			mat, //transformations
			this->displayWidth * 0.00025, // x-offset
			this->displayHeight * 0.00025 // y-offset
			);
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::draw_line_strip(const void* coords, int vertex_count)
	{
		// Set up current style.
		this->currentStyles[LINE_STYLE].apply();

		apply_matrix(this->currentMatrix);
	}
	
	// -------------------------------------------------------------------------------
	void SwfRenderHandler::draw_bitmap(
		const gameswf::matrix& m,
		gameswf::bitmap_info* bi,
		const gameswf::rect& coords,
		const gameswf::rect& uv_coords,
		gameswf::rgba color)
	{
		assert(bi);

		apply_color(color);

		gameswf::point a, b, c, d;
		m.transform(&a, gameswf::point(coords.m_x_min, coords.m_y_min));
		m.transform(&b, gameswf::point(coords.m_x_max, coords.m_y_min));
		m.transform(&c, gameswf::point(coords.m_x_min, coords.m_y_max));
		d.m_x = b.m_x + c.m_x - a.m_x;
		d.m_y = b.m_y + c.m_y - a.m_y;
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::begin_submit_mask()
	{
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::end_submit_mask()
	{	     
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::disable_mask()
	{	       
	}

	// -------------------------------------------------------------------------------
	bool SwfRenderHandler::is_visible(const gameswf::rect& bound)
	{
		gameswf::rect viewport;
		viewport.m_x_min = 0;
		viewport.m_y_min = 0;
		viewport.m_x_max = this->displayWidth;
		viewport.m_y_max = this->displayHeight;
		return viewport.bound_test(bound);
	}

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::open() 
	{ 
		assert( 0 &&"SwfRenderHandler::open Needs to be implemented"); 
	} 

	// -------------------------------------------------------------------------------
	void SwfRenderHandler::draw_triangle_list(const void* coords, int vertex_count) 
	{ 
		assert( 0 &&"SwfRenderHandler::draw_triangle_list Needs to be implemented"); 
	} 

	// -------------------------------------------------------------------------------
	gameswf::video_handler* SwfRenderHandler::create_video_handler(void) 
	{ 
		assert( 0 &&"SwfRenderHandler::create_video_handler Needs to be implemented"); 
		return NULL; 
	}

	// -------------------------------------------------------------------------------
	bool SwfRenderHandler::test_stencil_buffer(const gameswf::rect &,Uint8)
	{
		assert( 0 &&"SwfRenderHandler::test_stencil_buffer Needs to be implemented"); 
		return false; 
	}
}
