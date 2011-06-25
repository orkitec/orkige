//created 2008/09/21
#ifndef __OGRESWFRENDERHANDLER_H__
#define __OGRESWFRENDERHANDLER_H__

#include "engine_swf/SwfApiDefs.h"


namespace Orkige
{
	struct SwfRenderHandler : public gameswf::render_handler
	{
		struct fill_style
		{
			enum mode
			{
				INVALID,
				COLOR,
				BITMAP_WRAP,
				BITMAP_CLAMP,
				LINEAR_GRADIENT,
				RADIAL_GRADIENT,
			};
			// Attributes --------------------------------------------------------------
			mode						fillMode;
			gameswf::rgba				fillColor;
			const gameswf::bitmap_info*	bitmapInfo;
			gameswf::matrix				bitmapMatrix;
			gameswf::cxform				bitmapColorTransform;
			bool						hasNonzeroBitmapAdditiveColor;
			// Methods -----------------------------------------------------------------
			fill_style();

			// Push our style into OpenGL.
			void	apply(/*const matrix& current_matrix*/) const;

			// Return true if we need to do a second pass to make
			// a valid color.  This is for cxforms with additive
			// parts; this is the simplest way (that we know of)
			// to implement an additive color with stock OpenGL.
			bool	needs_second_pass() const;

			// Set OpenGL state for a necessary second pass.
			void	apply_second_pass() const;

			void	cleanup_second_pass() const;


			void	disable();
			void	set_color(gameswf::rgba color);
			void	set_bitmap(const gameswf::bitmap_info* bi, const gameswf::matrix& m, bitmap_wrap_mode wm, const gameswf::cxform& color_transform);
			bool	is_valid() const;
		};

		// Style state.
		enum style_index
		{
			LEFT_STYLE = 0,
			RIGHT_STYLE,
			LINE_STYLE,

			STYLE_COUNT
		};

		// Attributes --------------------------------------------------------------
		// Some renderer state.

		fill_style		currentStyles[STYLE_COUNT];

		// Enable/disable antialiasing.
		bool			enableAntialias;

		// Output size.
		float			displayWidth;
		float			displayHeight;

		gameswf::matrix	currentMatrix;
		gameswf::cxform	currentCxform;
		// Methods -----------------------------------------------------------------

		//constructor
		SwfRenderHandler();
		//destructor
		~SwfRenderHandler();

		void set_antialiased(bool enable);

		// Utility.  Mutates *width, *height and *data to create the
		// next mip level.
		static void make_next_miplevel(int* width, int* height, Uint8* data);

		// Given an image, returns a pointer to a bitmap_info struct
		// that can later be passed to fill_styleX_bitmap(), to set a
		// bitmap fill style.
		gameswf::bitmap_info*	create_bitmap_info_rgb(image::rgb* im);

		// Given an image, returns a pointer to a bitmap_info struct
		// that can later be passed to fill_style_bitmap(), to set a
		// bitmap fill style.
		//
		// This version takes an image with an alpha channel.
		gameswf::bitmap_info*	create_bitmap_info_rgba(image::rgba* im);

		// Create a placeholder bitmap_info.  Used when
		// DO_NOT_LOAD_BITMAPS is set; then later on the host program
		// can use movie_definition::get_bitmap_info_count() and
		// movie_definition::get_bitmap_info() to stuff precomputed
		// textures into these bitmap infos.
		gameswf::bitmap_info*	create_bitmap_info_empty();

		// Create a bitmap_info so that it contains an alpha texture
		// with the given data (1 byte per texel).
		//
		// Munges *data (in order to make mipmaps)!!
		gameswf::bitmap_info*	create_bitmap_info_alpha(int w, int h, Uint8* data);

		// Delete the given bitmap info struct.
		void	delete_bitmap_info(gameswf::bitmap_info* bi);
		
		// Set up to render a full frame from a movie and fills the
		// background.	Sets up necessary transforms, to scale the
		// movie to fit within the given dimensions.  Call
		// end_display() when you're done.
		//
		// The rectangle (viewport_x0, viewport_y0, viewport_x0 +
		// viewport_width, viewport_y0 + viewport_height) defines the
		// window coordinates taken up by the movie.
		//
		// The rectangle (x0, y0, x1, y1) defines the pixel
		// coordinates of the movie that correspond to the viewport
		// bounds.
		void	begin_display(
			gameswf::rgba background_color,
			int viewport_x0, int viewport_y0,
			int viewport_width, int viewport_height,
			float x0, float x1, float y0, float y1);

		// Clean up after rendering a frame.  Client program is still
		// responsible for calling glSwapBuffers() or whatever.
		void	end_display();

		// Set the current transform for mesh & line-strip rendering.
		void	set_matrix(const gameswf::matrix& m);

		// Set the current color transform for mesh & line-strip rendering.
		void	set_cxform(const gameswf::cxform& cx);

		// multiply current matrix with opengl matrix
		static void	apply_matrix(const gameswf::matrix& m);

		// Set the given color.
		static void	apply_color(const gameswf::rgba& c);

		// Don't fill on the {0 == left, 1 == right} side of a path.
		void	fill_style_disable(int fill_side);

		// Don't draw a line on this path.
		void	line_style_disable();

		// Set fill style for the left interior of the shape.  If
		// enable is false, turn off fill for the left interior.
		void	fill_style_color(int fill_side, const gameswf::rgba& color);

		// Set the line style of the shape.  If enable is false, turn
		// off lines for following curve segments.
		void	line_style_color(gameswf::rgba color);


		void	fill_style_bitmap(int fill_side, gameswf::bitmap_info* bi, const gameswf::matrix& m, bitmap_wrap_mode wm, bitmap_blend_mode bm);

		void	line_style_width(float width);

		void	draw_mesh_strip(const void* coords, int vertex_count);

		// Draw the line strip formed by the sequence of points.
		void	draw_line_strip(const void* coords, int vertex_count);

		// Draw a rectangle textured with the given bitmap, with the
		// given color.	 Apply given transform; ignore any currently
		// set transforms.
		//
		// Intended for textured glyph rendering.
		void	draw_bitmap(
			const gameswf::matrix& m,
			gameswf::bitmap_info* bi,
			const gameswf::rect& coords,
			const gameswf::rect& uv_coords,
			gameswf::rgba color);

		void begin_submit_mask();

		void end_submit_mask();

		void disable_mask();

		bool is_visible(const gameswf::rect& bound);

		void   open();

		void   draw_triangle_list(const void* coords, int vertex_count);

		gameswf::video_handler* create_video_handler(void);

		bool test_stencil_buffer(const gameswf::rect &,Uint8);
	};	// end struct render_handler_ogre
};
#endif //__OGRESWFRENDERHANDLER_H__