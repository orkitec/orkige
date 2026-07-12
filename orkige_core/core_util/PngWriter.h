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
//! @brief a minimal, dependency-free PNG encoder for a straight-RGBA8 buffer
//! @remarks Lives in orkige_core beside VectorShapeRaster ON PURPOSE: it is
//! the headless CPU counterpart of the render backend's texture-readback save.
//! A CPU rasterizer (VectorShapeRaster, the vector-shape thumbnail/preview) has
//! pixels but no render target to hand to `RenderTexture::writeContentsToFile`,
//! so this turns the RGBA8 buffer straight into a PNG file with no renderer and
//! no image library. The stream is a valid 8-bit RGBA PNG built with STORED
//! (uncompressed) DEFLATE blocks - no zlib link, no compression, just a
//! correct container - which every PNG reader accepts; the files are a little
//! larger than a compressed encoder would produce, which is irrelevant for the
//! small preview/thumbnail images this serves. Pure and unit-testable.

#include <core_util/String.h>
#include <vector>

namespace Orkige
{
	//! @brief straight-RGBA8 -> PNG, no renderer, no image library (@see the
	//! file comment)
	class PngWriter
	{
	public:
		//! @brief encode a straight-RGBA8 buffer (row-major, width*4 bytes per
		//! row, R,G,B,A, top-down - the VectorShapeRaster layout) into a PNG
		//! byte stream appended to out. @return false on a non-positive
		//! dimension or a null buffer (out untouched).
		static bool encode(unsigned char const * rgba, int width, int height,
			std::vector<unsigned char> & out);
		//! @brief encode and write the PNG to path (binary). @return false on
		//! bad arguments or a write failure.
		static bool writeFile(String const & path, unsigned char const * rgba,
			int width, int height);
	};
}

#endif //__PngWriter_h__12_7_2026__17_00_00__
