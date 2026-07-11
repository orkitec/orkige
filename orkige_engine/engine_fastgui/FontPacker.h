/********************************************************************
	created:	Saturday 2026/07/11 at 03:30
	filename: 	FontPacker.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __FontPacker_h__11_7_2026__03_30_00__
#define __FontPacker_h__11_7_2026__03_30_00__

//! @file FontPacker.h
//! @brief a pure shelf allocator for the runtime-baked font atlas: places
//! axis-aligned boxes (baked glyph cells, rasterized sprites) into a fixed
//! texture without overlap. No render system, no image data - just the
//! geometry, so it is unit-tested headlessly (FontPackerTests).
//! @remarks Shelf packing is a near-perfect fit for glyph pages, where every
//! cell of one font shares the same height: cells tile a shelf edge to edge
//! and a full shelf opens a new one. A one-texel padding gutter is reserved
//! around each box so the point-filtered atlas never bleeds a neighbour's
//! texels across a cell edge.

#include "engine_module/EnginePrerequisites.h"

#include <vector>

namespace Orkige
{
	//! @brief fixed-size shelf allocator (see the file comment)
	class ORKIGE_ENGINE_DLL FontPacker
	{
	public:
		//! a placed box in texture pixels (top-left origin)
		struct Rect
		{
			uint x = 0, y = 0, w = 0, h = 0;
		};

		//! an empty 0x0 packer (configure() before use); nothing fits yet
		FontPacker();
		//! @param width/height the texture page size in pixels
		//! @param padding transparent gutter reserved around every box
		FontPacker(uint width, uint height, uint padding = 1);

		//! (re)set the page size + padding and forget every placement
		void configure(uint width, uint height, uint padding = 1);

		//! @brief place a w x h box; false (out untouched) when it will not fit
		//! @remarks best-fit over existing shelves (smallest shelf that holds
		//! the box), else a new shelf at the bottom; deterministic placement
		bool allocate(uint w, uint h, Rect & out);

		//! forget every placement (the page is considered empty again)
		void reset();

		inline uint width() const { return this->mWidth; }
		inline uint height() const { return this->mHeight; }
		//! first free y a new shelf would open at (occupancy probe for tests)
		inline uint usedHeight() const { return this->mBottom; }
	private:
		//! one horizontal band of a fixed height filled left to right
		struct Shelf
		{
			uint y = 0;			//!< top of the shelf
			uint height = 0;	//!< reserved band height (box height + padding)
			uint cursorX = 0;	//!< next free x inside the shelf
		};

		uint				mWidth, mHeight, mPadding;
		std::vector<Shelf>	mShelves;
		uint				mBottom;	//!< next free y for a new shelf
	};
}

#endif //__FontPacker_h__11_7_2026__03_30_00__
