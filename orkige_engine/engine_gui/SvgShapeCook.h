/********************************************************************
	created:	Wednesday 2026/07/29 at 21:00
	filename: 	SvgShapeCook.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SvgShapeCook_h__29_7_2026__21_00_00__
#define __SvgShapeCook_h__29_7_2026__21_00_00__

//! @file SvgShapeCook.h
//! @brief the `.svg` -> `.oshape` cook: the SVG on-ramp for flat-colour vector
//! shapes, as a seam with no SVG library in its interface
//! @remarks The RUNTIME never reads SVG - it reads the pre-flattened `.oshape`.
//! This is the IMPORT-side transform behind that contract: parse a drawing,
//! flatten its curves to polylines, and hand the contours to the pure tail
//! (core_util/VectorShapeCook) that places them in world units and writes the
//! text. Every caller - the editor's drag-drop/import path, the MCP
//! import_asset verb and the shapecook CLI - goes through here, so there is ONE
//! implementation of the conversion.
//!
//! nanosvg does the parsing, and its headers are included in EXACTLY one TU
//! (SvgShapeCookImpl.cpp, the sibling of SvgRasterImpl.cpp) so the library
//! stays out of every header, the neutral umbrella and the precompiled header -
//! the isolation pattern of engine_sound/StbVorbisImpl.cpp. Because a cook is
//! host-side tooling this seam depends on orkige_core ONLY (no render facade,
//! no engine prerequisites), which lets the shapecook CLI compile the one TU
//! directly instead of linking the whole engine closure.
//!
//! What the cook maps, and what it deliberately does not:
//!   - every FILLED shape becomes one or more `.oshape` fill regions, in
//!     document paint order, with transforms, groups and inherited
//!     presentation attributes already applied (nanosvg resolves them);
//!   - a subpath whose first vertex lies INSIDE an earlier subpath of the same
//!     shape becomes that region's `hole`, so a donut reads as a donut;
//!   - `fill="none"` and hidden (`display:none`) shapes are skipped, as is a
//!     shape with no usable contour;
//!   - a gradient fill flattens to its FIRST stop's colour (the `.oshape`
//!     gradient vocabulary is reserved, not yet painted by the runtime);
//!   - a stroke paints nothing: an unfilled outline cooks to no region. The
//!     `.oshape` stroke vocabulary is authored, not derived.

#include "core_util/VectorShapeCook.h"
#include "core_util/VectorTessellator.h"
#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	//! @brief the SVG front half of the vector-shape cook
	namespace SvgShapeCook
	{
		//! one named SVG blob - a pose of a morph set (the name becomes the
		//! `morph` target's name; the first pose is the base and is unnamed)
		struct Source
		{
			unsigned char const *	data = nullptr;
			int						size = 0;
			String					name;
		};

		//! @brief cook one SVG blob into `.oshape` text.
		//! @return false with a single-line reason in *error (an unparseable
		//! document, no filled shape, no contour with >= 3 vertices); outText
		//! is untouched on failure.
		bool cook(unsigned char const * svg, int size,
			VectorShapeCook::Options const & options, String & outText,
			String * error);

		//! @brief cook a base SVG plus N pose SVGs into ONE `.oshape` carrying
		//! morph targets. Every pose is flattened at the FIXED resolution
		//! (VectorShapeCook::UNIFORM_CURVE_SEGMENTS) and must share the base's
		//! contour structure; a mismatch is refused with the message naming
		//! both structures. All poses are placed with the base's transform so
		//! corresponding vertices line up in world space.
		//! @param poses [0] is the base; the rest are targets in order
		bool cookMorphSet(std::vector<Source> const & poses,
			VectorShapeCook::Options const & options, String & outText,
			String * error);

		//! @brief the parse+flatten step alone: the drawing's filled regions in
		//! its OWN coordinate space (y-down, unplaced), bezier-flattened at the
		//! tolerance `options` resolves against the parsed document size.
		//! Exposed so the conversion is unit-testable without the placement and
		//! text tail (and so a future consumer can cook straight to geometry).
		//! @param outFilledShapes how many filled, visible shapes the document
		//! carried (0 distinguishes "nothing to cook" from "nothing usable")
		bool extractRegions(unsigned char const * svg, int size,
			VectorShapeCook::Options const & options,
			std::vector<VectorTessellator::Region> & outRegions,
			int * outFilledShapes, String * error);
	}
}

#endif //__SvgShapeCook_h__29_7_2026__21_00_00__
