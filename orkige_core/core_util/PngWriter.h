/********************************************************************
	created:	Saturday 2026/07/12 at 17:00
	filename: 	PngWriter.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __PngWriter_h__12_7_2026__17_00_00__
#define __PngWriter_h__12_7_2026__17_00_00__

//! @file PngWriter.h
//! @brief a minimal PNG encoder for a straight-RGBA8 buffer
//! @remarks Lives in orkige_core beside VectorShapeRaster ON PURPOSE: it is
//! the headless CPU counterpart of the render backend's texture-readback save.
//! A CPU rasterizer (VectorShapeRaster, the vector-shape thumbnail/preview) has
//! pixels but no render target to hand to `RenderTexture::writeContentsToFile`,
//! so this turns the RGBA8 buffer straight into a PNG file with no renderer and
//! no image library: the container (chunks, CRCs, scanline filter bytes) is
//! owned here, and only the DEFLATE stream inside IDAT is delegated to zlib -
//! the one compression library the engine already links everywhere. Real
//! compression is part of the contract: the screenshot suites push
//! full-resolution frames through this encoder, where an uncompressed stream
//! would be two orders of magnitude larger. Pure and unit-testable.

#include <core_util/String.h>
#include <vector>

namespace Orkige
{
	//! @brief straight-RGBA8 -> PNG, no renderer, no image library (@see the
	//! file comment)
	class PngWriter
	{
	public:
		//! @brief encode a straight-RGBA8 buffer (row-major, R,G,B,A, top-down
		//! - the VectorShapeRaster layout) into a PNG byte stream appended to
		//! out. @param strideBytes the distance between two row starts; 0 (the
		//! default) means the rows are tightly packed at width*4. A GPU
		//! readback hands back PADDED rows, so the stride is part of the
		//! buffer's description and guessing it shears the image. @return false
		//! on a non-positive dimension, a null buffer, or a stride narrower
		//! than one row (out untouched).
		static bool encode(unsigned char const * rgba, int width, int height,
			std::vector<unsigned char> & out, int strideBytes = 0);
		//! @brief encode and write the PNG to path (binary). @return false on
		//! bad arguments or a write failure. @see encode for strideBytes.
		static bool writeFile(String const & path, unsigned char const * rgba,
			int width, int height, int strideBytes = 0);
	};
}

#endif //__PngWriter_h__12_7_2026__17_00_00__
