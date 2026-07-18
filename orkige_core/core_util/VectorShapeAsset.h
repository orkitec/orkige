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
//! Grammar (v3), one token stream, `#` starts a line comment. v3 ADDED the
//! `texture` vocabulary, so every v1/v2 file is a valid v3 file:
//!   version 3
//!   fill  r g b a                 straight RGBA 0..1 - opens a region
//!   texture NAME x y w h [u0 v0 u1 v1]
//!                                 makes the open region a textured CUTOUT
//!                                 part: NAME is the texture asset's file name
//!                                 (no whitespace; resolved like a sprite
//!                                 texture), pasted into the shape-local rect
//!                                 (x, y) .. (x+w, y+h) with the texture's TOP
//!                                 row on the rect's TOP edge; the optional
//!                                 u0 v0 u1 v1 is an atlas sub-rect (default
//!                                 the full texture, v down). The region's
//!                                 `fill` becomes the multiply TINT (author
//!                                 white for the art verbatim); its contour is
//!                                 any polygon - each vertex samples where it
//!                                 sits in the rect (a full quad = the rect's
//!                                 4 corners). Comes after `fill`, before
//!                                 `contour`; a textured region takes no
//!                                 `hole` and no `stroke`, and gets no baked
//!                                 feather (the texture's alpha is its edge).
//!   stroke W CAP JOIN LIMIT ENDS  makes the open region a STROKE of width W:
//!                                 its `contour` is a CENTRELINE the renderer
//!                                 sweeps, not a filled boundary. CAP is
//!                                 butt|round|square (how an open end finishes),
//!                                 JOIN is miter|round|bevel (how a corner
//!                                 turns), LIMIT is the miter ceiling in half
//!                                 widths, ENDS is open|closed (a closed
//!                                 centreline has no caps). It comes after
//!                                 `fill`, before `contour`; a stroke takes no
//!                                 `hole`, and its centreline may be 2 points.
//!   contour N                     N follows as N `v x y` lines (the outer loop,
//!                                 or the stroke's centreline)
//!   v  x y                        one contour/hole/mask vertex (world units, +y up)
//!   hole M                        optional inner loop cut out of the region
//!   mask K                        optional CONVEX clip polygon (K >= 3 `v`
//!                                 lines) the region's stroke is clipped against
//!                                 - an authored/cooked layer mask
//!   morph NAME                    opens a MORPH TARGET: a same-structure pose
//!                                 (its own fill/contour/v/hole run) the runtime
//!                                 blends toward for soft-shape squash/stretch
//!                                 animation. Everything before the first `morph`
//!                                 is the BASE pose; each `morph` starts another.
//! Reserved words (gradient paint) are ignored when present. A v1 file is a
//! valid v2 file (v2 only ADDS the stroke/mask vocabulary) and a v2 file a
//! valid v3 file (v3 only ADDS the texture vocabulary).

#include "core_util/VectorTessellator.h"
#include <core_util/String.h>
#include <sstream>
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

		//! @brief parse the `stroke W CAP JOIN LIMIT ENDS` grammar fragment from
		//! an open token stream into region (kind, width, cap, join, miter limit,
		//! closedness). The `.oanim` shape keys reuse this VERBATIM - one
		//! definition of the stroke vocabulary, two assets.
		//! @return false on a malformed spec (region left untouched)
		static bool parseStrokeSpec(std::istringstream & tokens,
			VectorTessellator::Region & region);

		//! @brief parse the `texture NAME x y w h [u0 v0 u1 v1]` grammar
		//! fragment from an open token stream into region (texture name,
		//! shape-local rect, optional uv window). Shared with the `.oanim`
		//! shape keys exactly like parseStrokeSpec - one definition of the
		//! texture vocabulary, two assets. The derived per-vertex UVs are
		//! projected later, once the region's contour is complete
		//! (@see VectorTessellator::projectTextureUVs).
		//! @return false on a malformed spec (w/h <= 0, a half-given uv
		//! window); region is only written on success
		static bool parseTextureSpec(std::istringstream & tokens,
			VectorTessellator::Region & region);
	};
}

#endif //__VectorShapeAsset_h__10_7_2026__10_00_00__
