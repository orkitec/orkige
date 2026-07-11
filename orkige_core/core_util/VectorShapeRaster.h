/********************************************************************
	created:	Friday 2026/07/11 at 10:00
	filename: 	VectorShapeRaster.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorShapeRaster_h__11_7_2026__10_00_00__
#define __VectorShapeRaster_h__11_7_2026__10_00_00__

//! @file VectorShapeRaster.h
//! @brief software rasterizer for the tessellated flat-colour vector shape -
//! fills the triangle mesh into a small straight-RGBA8 buffer, no renderer
//! @remarks Lives in orkige_core alongside VectorTessellator ON PURPOSE: it is
//! pure CPU pixel math with no Ogre/facade dependency, so the unit suite pins
//! the fill + feather-alpha compositing WITHOUT booting a render system. The
//! editor's asset browser uses it to draw real .oshape thumbnails (raster once,
//! upload via RenderSystem::createTexture2D); it is deliberately NOT a full RTT
//! scene render. Input is already-CPU geometry (VectorTessellator::Mesh, whose
//! per-vertex colours carry the feather alpha ramp), so a thumbnail is a
//! barycentric triangle fill with straight-alpha source-over compositing.

#include "core_util/VectorTessellator.h"

namespace Orkige
{
	//! @brief the pure CPU fill core of the vector-shape thumbnail path
	//! @remarks Static functions only - no state, no renderer types.
	class VectorShapeRaster
	{
	public:
		//! @brief rasterize a tessellated shape mesh into a width x height
		//! straight-RGBA8 buffer (row-major, width*4 bytes per row, R,G,B,A;
		//! straight/non-premultiplied alpha). The mesh's local bounds are fit
		//! into the pixel rect preserving aspect and centred, with a small
		//! margin, and the shape's +y-up flips to pixel +y-down. The buffer
		//! starts fully transparent; each triangle is filled by barycentric
		//! interpolation of its per-vertex colour (the feather ramp is already
		//! in the vertex alpha) composited source-over over what is there. A
		//! degenerate mesh (no triangles or invalid bounds) leaves the buffer
		//! transparent. rgba must point at width*height*4 writable bytes; a
		//! non-positive width/height is a no-op.
		static void rasterize(VectorTessellator::Mesh const & mesh,
			int width, int height, unsigned char * rgba);
	};
}

#endif //__VectorShapeRaster_h__11_7_2026__10_00_00__
