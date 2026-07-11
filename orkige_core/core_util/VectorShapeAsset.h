/********************************************************************
	created:	Thursday 2026/07/10 at 10:00
	filename: 	VectorShapeAsset.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __VectorShapeAsset_h__10_7_2026__10_00_00__
#define __VectorShapeAsset_h__10_7_2026__10_00_00__

//! @file VectorShapeAsset.h
//! @brief parser for the lean, agent-authorable `.oshape` text asset
//! @remarks The `.oshape` is the NATIVE runtime form of a flat-colour vector
//! shape (the `.svg` import cooks to it): flattened contours already in world
//! units, so the runtime never links a curve/SVG parser. It is plain text an
//! agent can write directly over write_project_file. This parser is pure and
//! headless (orkige_core), unit-tested without a renderer; it turns the text
//! into VectorTessellator::Region lists the tessellator consumes.
//!
//! Grammar (v1), one token stream, `#` starts a line comment:
//!   version 1
//!   fill  r g b a                 straight RGBA 0..1 - opens a filled region
//!   contour N                     N follows as N `v x y` lines (the outer loop)
//!   v  x y                        one contour/hole vertex (world units, +y up)
//!   hole M                        optional inner loop cut out of the region
//!   morph NAME                    opens a MORPH TARGET: a same-structure pose
//!                                 (its own fill/contour/v/hole run) the runtime
//!                                 blends toward for soft-shape squash/stretch
//!                                 animation. Everything before the first `morph`
//!                                 is the BASE pose; each `morph` starts another.
//! Reserved words for later phases (stroke/gradient) are ignored when present.

#include "core_util/VectorTessellator.h"
#include <core_util/String.h>
#include <vector>

namespace Orkige
{
	//! @brief the `.oshape` text -> regions front end (pure, headless)
	class VectorShapeAsset
	{
	public:
		//--- Types -------------------------------------------------
		//! one named morph pose: a same-structure region set the runtime blends
		//! toward (@see SoftBodyDeform). Topology matching against the base is the
		//! consumer's check (the deformer rejects a mismatch honestly).
		struct MorphTarget
		{
			String								name;		//!< target name from the asset
			std::vector<VectorTessellator::Region>	regions;	//!< this pose's regions
		};
		//! a parsed shape: the base pose plus any morph targets
		struct ParsedShape
		{
			std::vector<VectorTessellator::Region>	base;	//!< the rest pose regions
			std::vector<MorphTarget>			morphs;	//!< optional morph poses
		};

		//! @brief parse `.oshape` text into filled regions (BASE pose only - a
		//! backward-compatible convenience over the full parse; morph targets are
		//! discarded). @see parse(String const&, ParsedShape&) for the full form.
		//! @return true on a well-formed shape (>= 1 region, every contour count
		//! honoured, every filled region >= 3 vertices). On ANY malformation it
		//! returns false and leaves outRegions EMPTY - the honest "no shape"
		//! fallback (SpriteComponent's atlas-parse discipline), never a crash.
		static bool parse(String const & text,
			std::vector<VectorTessellator::Region> & outRegions);
		//! @brief parse `.oshape` text into the base pose AND its morph targets.
		//! Each region set (base and every morph) must independently be
		//! well-formed; otherwise returns false and leaves out EMPTY. Topology
		//! agreement between base and morphs is NOT enforced here (the deformer
		//! reports a mismatch), so a partly-authored file still loads its base.
		static bool parse(String const & text, ParsedShape & out);
	};
}

#endif //__VectorShapeAsset_h__10_7_2026__10_00_00__
